const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const L = require("../../custom_builder_web/js/flasher-logic.js");

const BACKUP = { role: "repeater", created: "2026-11-26T10:00:00.000Z", settings: { name: "Cima Norte" } };

// Runs initFlasherBackup against a minimal DOM, a fake serial port and a fake backup module.
function loadSection({ nodeRole = "repeater", accept = true, refused = [], hash = "", connected = false } = {}) {
  const els = {};
  const log = [];
  const el = (id) =>
    (els[id] ||= {
      textContent: "", style: {}, listeners: {}, files: [], open: false,
      click() {}, addEventListener(type, fn) { this.listeners[type] = fn; }
    });
  const doc = {
    readyState: "complete",
    getElementById: el,
    addEventListener() {},
    createElement: () => ({ click() { log.push(`download ${this.download}`); } })
  };
  const win = {
    location: { hash },
    confirm: (text) => { log.push("confirm"); els.confirmation = text; return accept; },
    prompt: () => "",
    ZephtacoreLogic: L,
    ZephtacoreSerial: {
      isConnected: () => connected,
      connect: async () => { log.push("connect"); return true; },
      disconnect: async () => log.push("disconnect"),
      sendCommandAndWait: async (cmd) => { log.push(cmd); return { sent: true, timedOut: false, text: `> ${nodeRole}` }; }
    },
    ZephtacoreBackup: {
      collect: async () => ({ backup: BACKUP, warnings: ["Aviso."] }),
      parseBackup: (text) => JSON.parse(text),
      restore: async () => { log.push("restore"); return refused; }
    }
  };
  const src = fs.readFileSync(path.join(__dirname, "../../custom_builder_web/js/flasher-backup.js"), "utf8");
  new Function("window", "document", "URL", "Blob", src)(win, doc, { createObjectURL: () => "blob:x" }, class {});
  const choose = (content) => {
    els.backupFileInput.files = [{ name: "respaldo.json", text: async () => content }];
    return els.backupFileInput.listeners.change();
  };
  return { els, log, choose };
}

test("saving downloads the backup under the node's name and passes the warnings on", async () => {
  const { els, log } = loadSection();
  await els.btnBackupSave.listeners.click();
  assert.deepEqual(log, ["connect", "download zephtacore-respaldo-Cima_Norte-2026-11-26.json", "disconnect"]);
  assert.match(els.backupStatus.textContent, /Respaldo guardado.*Aviso\./);
});

test("a console that was already connected stays connected", async () => {
  const { els, log } = loadSection({ connected: true });
  await els.btnBackupSave.listeners.click();
  assert.equal(log.includes("disconnect"), false);
});

test("restoring asks for the port first, then confirms with the node's role at hand", async () => {
  const { els, log, choose } = loadSection({ nodeRole: "room_server" });
  await choose(JSON.stringify(BACKUP));
  assert.equal(els.btnBackupRestore.style.display, "");
  await els.btnBackupRestore.listeners.click();
  assert.deepEqual(log, ["connect", "get role", "confirm", "restore", "disconnect"]);
  assert.match(els.confirmation, /room_server.*repeater/);
  assert.match(els.confirmation, /misma identidad/);
  assert.match(els.backupStatus.textContent, /restaurado/);
});

test("nothing is sent to a companion or without the confirmation", async () => {
  for (const options of [{ nodeRole: "companion" }, { accept: false }]) {
    const { els, log, choose } = loadSection(options);
    await choose(JSON.stringify(BACKUP));
    await els.btnBackupRestore.listeners.click();
    assert.equal(log.includes("restore"), false);
  }
});

test("a refused identity is said first, and a file that cannot be used offers no restore", async () => {
  const { els, choose } = loadSection({ refused: [{ command: "set prv.key ••••", reply: "" }] });
  await choose("not json");
  assert.match(els.backupStatus.textContent, /No se puede usar/);
  assert.equal(els.btnBackupRestore.style.display, "none");
  await choose(JSON.stringify(BACKUP));
  await els.btnBackupRestore.listeners.click();
  assert.match(els.backupStatus.textContent, /^La identidad NO se ha restaurado.*set prv\.key/);
});

test("the Builder's link opens the section", () => {
  assert.equal(loadSection({ hash: "#backupSection" }).els.backupSection.open, true);
  assert.equal(loadSection().els.backupSection.open, false);
});

test("the flash buttons are off while the node is being read", async () => {
  const { els, log } = loadSection();
  const click = els.btnBackupSave.listeners.click();
  assert.equal(els.btnUnifiedFlash.disabled, true);
  await click;
  assert.equal(Boolean(els.btnUnifiedFlash.disabled), false);
  assert.ok(log.includes("disconnect"));
});
