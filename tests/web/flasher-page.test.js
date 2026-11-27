const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const L = require("../../custom_builder_web/js/flasher-logic.js");

const CDN = "https://xejarque.github.io/ZephCore/firmware/";
const catalog = {
  device: [{ firmware: [{ role: "companionUsb", version: { "1.18.2": { files: [
    { name: "t1000_e-companion-zephtacore-abc1234.uf2", url: CDN + "t1000_e-companion-zephtacore-abc1234.uf2" },
    { name: "t1000_e-companion-zephtacore-abc1234.zip", url: CDN + "t1000_e-companion-zephtacore-abc1234.zip" },
    { name: "t1000_e-repeater-zephtacore-abc1234.zip", url: CDN + "t1000_e-repeater-zephtacore-abc1234.zip" }
  ] } } }] }]
};

// Runs initFlasherCore against a minimal DOM and a fake catalog fetch.
async function loadPage({ fetchOk = true, board = "t1000_e", esptool = null, boards = null, install = "keep", serial = true, nrf = null, pending = null, node = null, role = "companion" } = {}) {
  const els = {};
  const downloads = [];
  const el = (id) =>
    (els[id] ||= {
      id, textContent: "", style: {}, value: "", disabled: false, innerHTML: "",
      listeners: {}, scrollIntoView() {}, addEventListener(type, fn) { this.listeners[type] = fn; }, classList: { add() {}, remove() {} }
    });
  const doc = {
    readyState: "complete",
    getElementById: el,
    querySelectorAll: () => [],
    addEventListener() {},
    createElement: () => {
      const a = { click() { downloads.push(a.download); } };
      return a;
    }
  };
  const win = {
    ZephtacoreLogic: L,
    ZephtacoreSerial: node ? node.serial : { log() {}, connect: async () => false, disconnect: async () => {} },
    ZephtacoreEsptool: esptool,
    ZephtacoreNrf: nrf,
    ZEPHTACORE_BOARDS: boards || [
      { id: "t1000_e", name: "T1000-E", mcu: "nRF52840", radio: "LR1110", flashType: "uf2", desc: "" },
      { id: "rak4631", name: "RAK4631", mcu: "nRF52840", radio: "SX1262", flashType: "uf2", desc: "" }
    ],
    ZEPHTACORE_ROLES: [{ id: "companion", name: "Companion" }, { id: "repeater", name: "Repeater" }, { id: "observer", name: "Observer" }],
    location: { search: "" },
    addEventListener: (type, fn) => windowListeners.set(type, fn),
    removeEventListener: (type) => windowListeners.delete(type)
  };
  const windowListeners = new Map();
  const fetched = [];
  const removed = [];
  const store = { getItem: () => (pending ? JSON.stringify(pending) : null), setItem() {}, removeItem: (key) => removed.push(key) };
  const alerts = [];
  // The waits for the node's port end at once: a port that is coming has already been reported.
  const soon = (fn, _ms, ...args) => setTimeout(fn, 0, ...args);
  const fetchFn = async (url) => {
    fetched.push(String(url));
    return String(url).includes("catalog.json")
      ? { ok: fetchOk, json: async () => catalog }
      : { ok: true, arrayBuffer: async () => (String(url).endsWith(".zip") ? new TextEncoder().encode(String(url)) : new Uint8Array([1, 2, 3, 4])).buffer };
  };
  const src = fs.readFileSync(path.join(__dirname, "../../custom_builder_web/js/flasher-core.js"), "utf8");
  el("installClean").checked = install === "clean";
  el("installKeep").checked = install !== "clean";
  els.flasherBoardSelect = { ...el("flasherBoardSelect"), value: board };
  els.flasherRoleSelect = { ...el("flasherRoleSelect"), value: role };
  const listeners = new Set();
  const navigatorStub = serial ? { serial: {
    requestPort: async () => ({ fake: "port" }),
    addEventListener: (_type, fn) => listeners.add(fn),
    removeEventListener: (_type, fn) => listeners.delete(fn)
  } } : {};
  if (node) node.plugIn = (port) => listeners.forEach((fn) => fn({ target: port }));
  new Function("window", "document", "sessionStorage", "navigator", "fetch", "URLSearchParams", "alert", "setTimeout", src)(
    win, doc, store, navigatorStub, fetchFn, URLSearchParams, (text) => alerts.push(text), soon);
  await new Promise((r) => setTimeout(r, 20));
  els.downloads = downloads;
  els.alerts = alerts;
  els.removed = removed;
  els.listeners = listeners;
  els.windowListeners = windowListeners;
  els.fetched = fetched;
  return els;
}

test("the page starts and offers only the roles the catalog publishes", async () => {
  const els = await loadPage();
  assert.ok(!els.flasherRoleSelect.innerHTML.includes("observer"));
  assert.ok(els.flasherRoleSelect.innerHTML.includes("companion") && els.flasherRoleSelect.innerHTML.includes("repeater"));
});

test("a role the Builder chose that is not published stays chosen, and nothing is offered to flash", async () => {
  const els = await loadPage({ pending: { name: "Sala", freq: "869.618", role: "observer" } });
  assert.ok(els.flasherRoleSelect.innerHTML.includes("observer"));
  assert.equal(els.flasherRoleSelect.value, "observer");
  assert.match(els.previewBuildTag.textContent, /^Sin build Zephtacore/);
  assert.equal(els.btnUnifiedFlash.disabled, true);
});

test("a board with a Zephtacore build is marked and its download button stays enabled", async () => {
  const els = await loadPage();
  assert.equal(els.previewBuildTag.textContent, "Build Zephtacore publicado");
  assert.equal(els.btnUnifiedFlash.disabled, false);
});

test("an unreachable catalog is reported and blocks the download", async () => {
  const els = await loadPage({ fetchOk: false });
  assert.equal(els.previewBuildTag.textContent, "Catálogo no disponible");
  assert.equal(els.btnUnifiedFlash.disabled, true);
});

test("a board without a Zephtacore build says so and blocks the download", async () => {
  const els = await loadPage({ board: "rak4631" });
  assert.match(els.previewBuildTag.textContent, /^Sin build Zephtacore/);
  assert.equal(els.btnUnifiedFlash.disabled, true);
});

const ESP_CDN_FILE = "heltec_v4-esp32s3-procpu-companion-zephtacore-abc1234-merged.bin";
const espCatalog = () => {
  catalog.device[0].firmware[0].version["1.18.2"].files.push({ name: ESP_CDN_FILE, url: CDN + ESP_CDN_FILE });
};
const espBoards = [{ id: "heltec_v4/esp32s3/procpu", name: "Heltec V4", mcu: "ESP32-S3", radio: "SX1262", flashType: "bin", desc: "" }];

function fakeEsptool(chipName) {
  const calls = { writeFlash: [], after: [], disconnected: 0 };
  class Transport { constructor(port) { this.port = port; } async disconnect() { calls.disconnected++; } }
  class ESPLoader {
    constructor(opts) { this.opts = opts; this.chip = { CHIP_NAME: chipName }; }
    async main() { return `${chipName} (QFN56) (revision v0.2)`; }
    async writeFlash(o) { calls.writeFlash.push(o); o.reportProgress(0, 50, 100); }
    async after(mode) { calls.after.push(mode); }
  }
  return { calls, module: { ESPLoader, Transport } };
}

test("ESP32: the merged image is written at 0x0 unchanged, then the board is reset", async () => {
  espCatalog();
  const esp = fakeEsptool("ESP32-S3");
  const els = await loadPage({ boards: espBoards, board: "heltec_v4/esp32s3/procpu", esptool: esp.module });
  await els.btnUnifiedFlash.listeners.click();
  assert.equal(esp.calls.writeFlash.length, 1);
  const w = esp.calls.writeFlash[0];
  assert.equal(w.fileArray[0].address, 0);
  assert.deepEqual([...w.fileArray[0].data], [1, 2, 3, 4]);
  assert.deepEqual([w.flashSize, w.flashMode, w.flashFreq, w.eraseAll], ["keep", "keep", "keep", false]);
  assert.deepEqual(esp.calls.after, ["hard_reset"]);
  assert.equal(esp.calls.disconnected, 1);
  assert.match(els.flashStatusText.textContent, /Firmware grabado/);
});

test("ESP32: a chip of another family stops the flash before anything is written", async () => {
  espCatalog();
  const esp = fakeEsptool("ESP32-C6");
  const els = await loadPage({ boards: espBoards, board: "heltec_v4/esp32s3/procpu", esptool: esp.module });
  await els.btnUnifiedFlash.listeners.click();
  assert.equal(esp.calls.writeFlash.length, 0);
  assert.deepEqual(esp.calls.after, []);
  assert.equal(esp.calls.disconnected, 1);
  assert.match(els.flashStatusText.textContent, /no es un ESP32-S3.*No se ha escrito nada/);
});

test("ESP32: keeping the data does not erase the flash", async () => {
  espCatalog();
  const esp = fakeEsptool("ESP32-S3");
  const els = await loadPage({ boards: espBoards, board: "heltec_v4/esp32s3/procpu", esptool: esp.module, install: "keep" });
  await els.btnUnifiedFlash.listeners.click();
  assert.equal(els.installModeBox.style.display, "");
  assert.equal(esp.calls.writeFlash[0].eraseAll, false);
});

test("ESP32: the clean choice says what it does for the board", async () => {
  espCatalog();
  const els = await loadPage({ boards: espBoards, board: "heltec_v4/esp32s3/procpu", esptool: fakeEsptool("ESP32-S3").module });
  assert.match(els.installCleanText.textContent, /^Borra TODA la flash/);
});

test("ESP32: the settings hint appears only when settings wait to be sent", async () => {
  espCatalog();
  const opts = { boards: espBoards, board: "heltec_v4/esp32s3/procpu", esptool: fakeEsptool("ESP32-S3").module };
  const plain = await loadPage(opts);
  await plain.btnUnifiedFlash.listeners.click();
  assert.equal(plain.flashStatusText.textContent, "Firmware grabado y placa reiniciada.");
  const withSettings = await loadPage({ ...opts, pending: { name: "Zephta-1", freq: "869.618" } });
  assert.equal(withSettings.pendingConfigBanner.style.display, "");
  await withSettings.btnUnifiedFlash.listeners.click();
  assert.match(withSettings.flashStatusText.textContent, /No se pudo abrir el puerto.*pulsa "Enviar ajustes por USB"/);
});

test("ESP32: a clean install erases the whole flash while writing", async () => {
  espCatalog();
  const esp = fakeEsptool("ESP32-S3");
  const els = await loadPage({ boards: espBoards, board: "heltec_v4/esp32s3/procpu", esptool: esp.module, install: "clean" });
  await els.btnUnifiedFlash.listeners.click();
  assert.equal(esp.calls.writeFlash[0].eraseAll, true);
  assert.deepEqual(esp.calls.after, ["hard_reset"]);
});

test("UF2 without Web Serial: without a formatter in the catalog the choice is hidden and the firmware downloads", async () => {
  const els = await loadPage({ install: "clean", serial: false });
  await els.btnUnifiedFlash.listeners.click();
  assert.equal(els.installModeBox.style.display, "none");
  assert.deepEqual(els.downloads, ["t1000_e-companion-zephtacore-abc1234.uf2"]);
});

test("UF2 without Web Serial: a clean install hands out the formatter first and the firmware after the board is formatted", async () => {
  catalog.device[0].firmware[0].erase = CDN + "SoftDevice_v7_formatter.zip";
  try {
    const els = await loadPage({ install: "clean", serial: false });
    await els.btnUnifiedFlash.listeners.click();
    assert.equal(els.installModeBox.style.display, "");
    assert.deepEqual(els.downloads, ["SoftDevice_v7_formatter.uf2"]);
    assert.equal(els.btnNextStep.style.display, "");
    await els.btnNextStep.listeners.click();
    assert.deepEqual(els.downloads, ["SoftDevice_v7_formatter.uf2", "t1000_e-companion-zephtacore-abc1234.uf2"]);
    assert.equal(els.btnNextStep.style.display, "none");
  } finally {
    delete catalog.device[0].firmware[0].erase;
  }
});

test("UF2 without Web Serial: keeping the data downloads only the firmware even when a formatter exists", async () => {
  catalog.device[0].firmware[0].erase = CDN + "SoftDevice_v7_formatter.zip";
  try {
    const els = await loadPage({ install: "keep", serial: false });
    await els.btnUnifiedFlash.listeners.click();
    assert.deepEqual(els.downloads, ["t1000_e-companion-zephtacore-abc1234.uf2"]);
  } finally {
    delete catalog.device[0].firmware[0].erase;
  }
});

// nRF52 with Web Serial: the Adafruit bootloader's DFU, one port choice for the whole install.
const usbPort = (usbProductId) => ({ getInfo: () => ({ usbVendorId: 0x239a, usbProductId }) });
const RUNNING_PORT = usbPort(0x8029);
// appears: the port the browser reports after the touch, when it already has permission for one.
function fakeNrf({ failFirstFlash = false, failFirstPort = false, node = null, appears = null, hold = null } = {}) {
  const calls = [];
  let failed = false;
  let portFailed = false;
  return {
    calls,
    module: {
      async touchBootloader() {
        calls.push("touch");
        if (appears) node.plugIn(appears);
        return RUNNING_PORT;
      },
      trackPort: (port) => port,
      async requestPort() {
        if (failFirstPort && !portFailed) { portFailed = true; throw Object.assign(new Error("No port selected"), { name: "NotFoundError" }); }
        calls.push("port");
        return { id: calls.length };
      },
      async waitForPort(previous) { calls.push("wait"); return previous; },
      openDfuZip: (bytes) => ({ url: new TextDecoder().decode(bytes) }),
      async flashNrf(pkg, hooks) {
        if (failFirstFlash && !failed) { failed = true; throw new Error("La placa no responde"); }
        calls.push("flash " + pkg.url.split("/").pop());
        await hold;
        hooks.onConnected();
        for (const stage of ["start", "erase", "init", "write", "activate"]) {
          hooks.onStage(stage);
          hooks.onProgress(stage, 1);
        }
        // The board reboots into the application and shows up as a serial port.
        if (node) node.plugIn(node.port);
        node?.flashed.push(hooks.port);
      }
    }
  };
}
const FIRMWARE_ZIP = "t1000_e-companion-zephtacore-abc1234.zip";

test("nRF52: one click reboots the board into its bootloader and writes the firmware zip", async () => {
  const nrf = fakeNrf();
  const els = await loadPage({ nrf: nrf.module, install: "keep" });
  await els.btnUnifiedFlash.listeners.click();
  assert.deepEqual(nrf.calls, ["touch", "port", "flash " + FIRMWARE_ZIP]);
  assert.equal(els.downloads.length, 0);
  assert.match(els.flashStatusText.textContent, /Firmware instalado/);
  assert.equal(els.btnNextStep.style.display, "none");
});

test("nRF52: a clean install loads the formatter, waits for the same port to come back and writes the firmware", async () => {
  catalog.device[0].firmware[0].erase = CDN + "SoftDevice_v7_formatter.zip";
  try {
    const nrf = fakeNrf();
    const els = await loadPage({ nrf: nrf.module, install: "clean" });
    await els.btnUnifiedFlash.listeners.click();
    assert.deepEqual(nrf.calls, ["touch", "port", "flash SoftDevice_v7_formatter.zip", "wait", "flash " + FIRMWARE_ZIP]);
    assert.match(els.flashStatusText.textContent, /Firmware instalado/);
    assert.equal(els.btnNextStep.style.display, "none");
  } finally {
    delete catalog.device[0].firmware[0].erase;
  }
});

test("nRF52: when the browser does not let the port be chosen without a click, the button does it", async () => {
  const nrf = fakeNrf({ failFirstPort: true });
  const els = await loadPage({ nrf: nrf.module, install: "keep" });
  await els.btnUnifiedFlash.listeners.click();
  assert.deepEqual(nrf.calls, ["touch"]);
  assert.equal(els.btnNextStep.style.display, "");
  await els.btnNextStep.listeners.click();
  assert.deepEqual(nrf.calls, ["touch", "port", "flash " + FIRMWARE_ZIP]);
  assert.match(els.flashStatusText.textContent, /Firmware instalado/);
});

test("nRF52: a failed DFU shows the error and offers the install again", async () => {
  const nrf = fakeNrf({ failFirstFlash: true });
  const els = await loadPage({ nrf: nrf.module, install: "keep" });
  await els.btnUnifiedFlash.listeners.click();
  assert.match(els.flashStatusText.textContent, /Error: La placa no responde/);
  assert.equal(els.btnNextStep.style.display, "");
  await els.btnNextStep.listeners.click();
  assert.deepEqual(nrf.calls, ["touch", "port", "port", "flash " + FIRMWARE_ZIP]);
});

test("nRF52: a board whose catalog has no DFU zip reports it and offers no step", async () => {
  const files = catalog.device[0].firmware[0].version["1.18.2"].files;
  const saved = files.splice(files.findIndex((f) => f.name.endsWith(".zip")), 1);
  try {
    const nrf = fakeNrf();
    const els = await loadPage({ nrf: nrf.module, install: "keep" });
    await els.btnUnifiedFlash.listeners.click();
    assert.match(els.flashStatusText.textContent, /No hay paquete DFU publicado/);
    assert.notEqual(els.btnNextStep.style.display, "");
  } finally {
    files.push(...saved);
  }
});

test("ESP32: an image that does not match the catalog's sha256 is not written; one that does is", async () => {
  espCatalog();
  const espFiles = () => catalog.device[0].firmware[0].version["1.18.2"].files.filter((f) => f.name === ESP_CDN_FILE);
  const sha1234 = "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a";   // SHA-256 of bytes 1,2,3,4
  try {
    espFiles().forEach((f) => { f.sha256 = "00" + sha1234.slice(2); });
    let esp = fakeEsptool("ESP32-S3");
    let els = await loadPage({ boards: espBoards, board: "heltec_v4/esp32s3/procpu", esptool: esp.module });
    await els.btnUnifiedFlash.listeners.click();
    assert.equal(esp.calls.writeFlash.length, 0);
    assert.match(els.flashStatusText.textContent, /SHA-256.*No se ha escrito nada/);

    espFiles().forEach((f) => { f.sha256 = sha1234; });
    esp = fakeEsptool("ESP32-S3");
    els = await loadPage({ boards: espBoards, board: "heltec_v4/esp32s3/procpu", esptool: esp.module });
    await els.btnUnifiedFlash.listeners.click();
    assert.equal(esp.calls.writeFlash.length, 1);
  } finally {
    espFiles().forEach((f) => { delete f.sha256; });
  }
});

test("nRF52 UF2 clean install checks erase_uf2_sha256, not the .zip digest", async () => {
  const sha1234 = "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a";
  const fw = catalog.device[0].firmware[0];
  fw.erase = CDN + "SoftDevice_v7_formatter.zip";
  fw.erase_sha256 = "00".repeat(32);      // .zip digest differs from .uf2
  fw.erase_uf2_sha256 = sha1234;
  try {
    let els = await loadPage({ install: "clean", serial: false });
    await els.btnUnifiedFlash.listeners.click();
    assert.deepEqual(els.downloads, ["SoftDevice_v7_formatter.uf2"]);

    fw.erase_uf2_sha256 = "ff".repeat(32);
    els = await loadPage({ install: "clean", serial: false });
    await els.btnUnifiedFlash.listeners.click();
    assert.deepEqual(els.downloads, []);
    assert.match(els.flashStatusText.textContent, /SHA-256.*No se ha escrito nada/);
  } finally {
    delete fw.erase;
    delete fw.erase_sha256;
    delete fw.erase_uf2_sha256;
  }
});

test("nRF52 DFU clean install checks erase_sha256, not erase_uf2_sha256", async () => {
  const crypto = require("node:crypto");
  const zipUrl = CDN + "SoftDevice_v7_formatter.zip";
  const zipHash = crypto.createHash("sha256").update(zipUrl).digest("hex");
  const fw = catalog.device[0].firmware[0];
  fw.erase = zipUrl;
  fw.erase_sha256 = zipHash;
  fw.erase_uf2_sha256 = "00".repeat(32);      // .uf2 digest differs from .zip
  try {
    let nrf = fakeNrf();
    await loadPage({ nrf: nrf.module, install: "clean" }).then((els) => els.btnUnifiedFlash.listeners.click());
    assert.deepEqual(nrf.calls, ["touch", "port", "flash SoftDevice_v7_formatter.zip", "wait", "flash " + FIRMWARE_ZIP]);

    fw.erase_sha256 = "ff".repeat(32);
    nrf = fakeNrf();
    const els = await loadPage({ nrf: nrf.module, install: "clean" });
    await els.btnUnifiedFlash.listeners.click();
    assert.deepEqual(nrf.calls, ["touch"]);
    assert.match(els.flashStatusText.textContent, /SHA-256.*No se ha escrito nada/);
  } finally {
    delete fw.erase;
    delete fw.erase_sha256;
    delete fw.erase_uf2_sha256;
  }
});

test("nRF52: the progress bar never goes back during a clean install", async () => {
  catalog.device[0].firmware[0].erase = CDN + "SoftDevice_v7_formatter.zip";
  try {
    const els = await loadPage({ nrf: fakeNrf().module, install: "clean" });
    const widths = [];
    Object.defineProperty(els.flashProgressBar.style, "width", { set: (w) => widths.push(parseInt(w, 10)), get: () => "" });
    await els.btnUnifiedFlash.listeners.click();
    assert.equal(widths.at(-1), 100);
    assert.deepEqual(widths, [...widths].sort((a, b) => a - b));
  } finally {
    delete catalog.device[0].firmware[0].erase;
  }
});

const SETTINGS = {
  name: "Repe", role: "repeater", adminPass: "secreta", freq: "869.618", bw: "62.5", sf: "7", cr: "2", txPower: "",
  mtRelay: "essential", mtOpaque: true, mtHops: "7"
};

// A node on the serial port that answers like the firmware; replies overrides what a command gets.
function fakeNode({ role = "repeater", replies = {} } = {}) {
  const node = { port: { id: "node" }, sent: [], opened: [], flashed: [], closed: 0 };
  let open = false;
  node.serial = {
    log() {},
    async connect(port) { node.opened.push(port); open = true; return true; },
    async disconnect() { node.closed++; open = false; },
    isConnected: () => open,
    async sendCommand(cmd) { node.sent.push(cmd); return true; },
    async sendCommandAndWait(cmd) {
      node.sent.push(cmd);
      const normal = cmd === "get role" ? `> ${role}` : cmd.startsWith("password ") ? `password now: ${cmd.slice(9)}` : "OK";
      return { sent: true, timedOut: false, text: cmd in replies ? replies[cmd] : normal };
    }
  };
  return node;
}

async function flashWithSettings(node) {
  const els = await loadPage({ nrf: fakeNrf({ node }).module, pending: SETTINGS, node, role: "repeater" });
  await els.btnUnifiedFlash.listeners.click();
  return els;
}

test("after an install the pending settings go to the port the board comes up on", async () => {
  const node = fakeNode();
  const els = await flashWithSettings(node);
  assert.deepEqual(node.opened, [node.port]);
  assert.deepEqual(node.sent.slice(0, 3), ["get role", "set name Repe", "password secreta"]);
  assert.equal(node.sent.at(-1), "reboot");
  assert.deepEqual(els.removed, ["zephtacore_pending_config"]);
  assert.match(els.alerts[0], /^Configuración enviada[\s\S]*Clave de administrador: la de tus ajustes/);
  assert.match(els.flashStatusText.textContent, /^Ajustes enviados/);
  assert.equal(els.listeners.size, 0);
});

test("settings already sent are not sent again by a later install", async () => {
  const node = fakeNode();
  const els = await flashWithSettings(node);
  const sent = node.sent.length;
  await els.btnUnifiedFlash.listeners.click();
  assert.equal(node.sent.length, sent);
});

test("without a port after the install, the person is told to send the settings", async () => {
  const els = await loadPage({ nrf: fakeNrf().module, pending: SETTINGS, role: "repeater" });
  await els.btnUnifiedFlash.listeners.click();
  assert.match(els.flashStatusText.textContent, /Firmware instalado\. Cuando la placa arranque, pulsa "Enviar ajustes por USB"/);
  assert.equal(els.listeners.size, 0);
});

test("a node of another role gets nothing but the question", async () => {
  const node = fakeNode({ role: "companion" });
  const els = await flashWithSettings(node);
  assert.deepEqual(node.sent, ["get role"]);
  assert.deepEqual(els.removed, []);
  assert.match(els.alerts[0], /"companion" y los ajustes son para "repeater"/);
});

test("a password the node does not confirm stops the send, says so and keeps the settings", async () => {
  const node = fakeNode({ replies: { "password secreta": "OK" } });
  const els = await flashWithSettings(node);
  assert.equal(node.sent.at(-1), "password secreta");
  assert.deepEqual(els.removed, []);
  assert.match(els.alerts[0], /^Envío interrumpido en "password ••••"[\s\S]*siguen pendientes/);
  assert.ok(!els.alerts[0].includes("secreta"));
});

test("no board is preselected: nothing is offered to flash until one is chosen", async () => {
  const els = await loadPage({ board: "" });
  assert.match(els.flasherBoardSelect.innerHTML, /^<option value="" disabled selected>Elige tu placa/);
  assert.equal(els.btnUnifiedFlash.disabled, true);
  assert.equal(els.btnUnifiedFlash.textContent, "Elige tu placa para grabar");
  assert.equal(els.boardPreviewBox.style.display, "none");
  assert.equal(els.installModeBox.style.display, "none");
});

test("the list marks the boards the catalog has no build for", async () => {
  const els = await loadPage();
  assert.match(els.flasherBoardSelect.innerHTML, /RAK4631 — \[nRF52840\] · sin build</);
  assert.match(els.flasherBoardSelect.innerHTML, /T1000-E — \[nRF52840\]</);
});

test("while flashing, a second click does nothing, the controls are off and leaving is warned about", async () => {
  let release;
  const nrf = fakeNrf({ hold: new Promise((resolve) => (release = resolve)) });
  const els = await loadPage({ nrf: nrf.module });
  const flashing = els.btnUnifiedFlash.listeners.click();
  await new Promise((r) => setTimeout(r, 20));
  assert.deepEqual([els.btnUnifiedFlash.disabled, els.flasherBoardSelect.disabled, els.btnBackupSave.disabled], [true, true, true]);
  assert.ok(els.windowListeners.has("beforeunload"));
  await els.btnUnifiedFlash.listeners.click();
  release();
  await flashing;
  assert.deepEqual(nrf.calls, ["touch", "port", "flash " + FIRMWARE_ZIP]);
  assert.deepEqual([els.btnUnifiedFlash.disabled, els.flasherBoardSelect.disabled, els.btnBackupSave.disabled], [false, false, false]);
  assert.equal(els.windowListeners.has("beforeunload"), false);
});

test("an open console is closed before flashing", async () => {
  const node = fakeNode();
  await node.serial.connect(node.port);
  const els = await loadPage({ nrf: fakeNrf().module, node });
  await els.btnUnifiedFlash.listeners.click();
  assert.equal(node.closed, 1);
});

test("nRF52: a bootloader port the browser already allows is used without asking for it", async () => {
  const bootloader = usbPort(0x0029);
  const node = fakeNode();
  const nrf = fakeNrf({ node, appears: bootloader });
  const els = await loadPage({ nrf: nrf.module, node });
  await els.btnUnifiedFlash.listeners.click();
  assert.deepEqual(nrf.calls, ["touch", "flash " + FIRMWARE_ZIP]);
  assert.deepEqual(node.flashed, [bootloader]);
});

test("nRF52: the application coming back after the touch is not taken for the bootloader", async () => {
  const node = fakeNode();
  const nrf = fakeNrf({ node, appears: usbPort(0x8029) });
  const els = await loadPage({ nrf: nrf.module, node });
  await els.btnUnifiedFlash.listeners.click();
  assert.deepEqual(nrf.calls, ["touch", "port", "flash " + FIRMWARE_ZIP]);
});

test("nRF52: the DFU package is downloaded when the board is chosen, and only once", async () => {
  const els = await loadPage({ nrf: fakeNrf().module });
  const zips = () => els.fetched.filter((url) => url.endsWith(".zip"));
  assert.equal(zips().length, 1);
  await els.btnUnifiedFlash.listeners.click();
  assert.equal(zips().length, 1);
});

test("a board without GPS gets every other setting and the reboot, and the person is told", async () => {
  const node = fakeNode({ replies: { "gps off": "gps toggle not found" } });
  const els = await loadPage({ nrf: fakeNrf({ node }).module, pending: { ...SETTINGS, gps: "off" }, node, role: "repeater" });
  await els.btnUnifiedFlash.listeners.click();
  assert.equal(node.sent.at(-1), "reboot");
  assert.match(els.alerts[0], /^Configuración enviada[\s\S]*no detecta ningún GPS[\s\S]*los demás comandos/);
});

test("the pending settings banner goes away once they are sent", async () => {
  const els = await flashWithSettings(fakeNode());
  assert.equal(els.pendingConfigBanner.style.display, "none");
});
