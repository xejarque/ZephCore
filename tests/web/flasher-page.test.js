const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const L = require("../../custom_builder_web/js/flasher-logic.js");

const CDN = "https://xejarque.github.io/ZephCore/firmware/";
const catalog = {
  device: [{ firmware: [{ role: "companionUsb", version: { "1.18.2": { files: [
    { name: "t1000_e-companion-zephtacore-abc1234.uf2", url: CDN + "t1000_e-companion-zephtacore-abc1234.uf2" },
    { name: "t1000_e-companion-zephtacore-abc1234.zip", url: CDN + "t1000_e-companion-zephtacore-abc1234.zip" }
  ] } } }] }]
};

// Runs initFlasherCore against a minimal DOM and a fake catalog fetch.
async function loadPage({ fetchOk = true, board = "t1000_e", esptool = null, boards = null, install = "keep", serial = true, nrf = null, pending = null } = {}) {
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
    ZephtacoreSerial: { log() {} },
    ZephtacoreEsptool: esptool,
    ZephtacoreNrf: nrf,
    ZEPHTACORE_BOARDS: boards || [
      { id: "t1000_e", name: "T1000-E", mcu: "nRF52840", radio: "LR1110", flashType: "uf2", desc: "" },
      { id: "rak4631", name: "RAK4631", mcu: "nRF52840", radio: "SX1262", flashType: "uf2", desc: "" }
    ],
    ZEPHTACORE_ROLES: [{ id: "companion", name: "Companion" }, { id: "repeater", name: "Repeater" }, { id: "observer", name: "Observer" }],
    location: { search: "" }
  };
  const store = { getItem: () => (pending ? JSON.stringify(pending) : null), setItem() {}, removeItem() {} };
  const fetchFn = async (url) =>
    String(url).includes("catalog.json")
      ? { ok: fetchOk, json: async () => catalog }
      : { ok: true, arrayBuffer: async () => (String(url).endsWith(".zip") ? new TextEncoder().encode(String(url)) : new Uint8Array([1, 2, 3, 4])).buffer };
  const src = fs.readFileSync(path.join(__dirname, "../../custom_builder_web/js/flasher-core.js"), "utf8");
  el("installClean").checked = install === "clean";
  el("installKeep").checked = install !== "clean";
  els.flasherBoardSelect = { ...el("flasherBoardSelect"), value: board };
  els.flasherRoleSelect = { ...el("flasherRoleSelect"), value: "companion" };
  const navigatorStub = serial ? { serial: { requestPort: async () => ({ fake: "port" }) } } : {};
  new Function("window", "document", "sessionStorage", "navigator", "fetch", "URLSearchParams", src)(win, doc, store, navigatorStub, fetchFn, URLSearchParams);
  await new Promise((r) => setTimeout(r, 20));
  els.downloads = downloads;
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
  assert.match(withSettings.flashStatusText.textContent, /pulsa "Enviar ajustes por USB"/);
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
function fakeNrf({ failFirstFlash = false, failFirstPort = false } = {}) {
  const calls = [];
  let failed = false;
  let portFailed = false;
  return {
    calls,
    module: {
      async touchBootloader() { calls.push("touch"); },
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
        hooks.onConnected();
        hooks.onStage("write");
        hooks.onProgress("write", 1);
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
