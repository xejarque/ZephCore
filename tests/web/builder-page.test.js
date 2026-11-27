const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const L = require("../../custom_builder_web/js/flasher-logic.js");

const KEY = "zephtacore_pending_config";
const BOARDS = [
  { id: "thinknode_m1", name: "M1", mcu: "nRF52840", radio: "SX1262", flashType: "uf2", desc: "" },
  { id: "gat562_30s", name: "GAT562", mcu: "nRF52840", radio: "SX1262", flashType: "uf2", desc: "", batteryWake: { maxMv: 3080, suggestedMv: 3000 } }
];
const ROLES = [{ id: "companion", name: "Companion", defaultRelay: "off" }, { id: "repeater", name: "Repeater", defaultRelay: "essential" }];

// Runs builder.js against a minimal DOM whose form fields start as builder.html leaves them.
function loadBuilder(saved = null) {
  const els = {};
  const blobs = [];
  const newElement = () => ({
    value: "", textContent: "", innerHTML: "", checked: false, style: {}, dataset: {}, listeners: {},
    addEventListener(type, fn) { this.listeners[type] = fn; }, appendChild() {}, click() {}
  });
  const el = (id) => (els[id] ||= newElement());
  Object.assign(el("loraFreq"), { value: "869.618" });
  let ready = null;
  const doc = { getElementById: el, createElement: newElement, addEventListener: (_type, fn) => (ready = fn) };
  const store = {
    data: saved ? { [KEY]: JSON.stringify(saved) } : {},
    getItem(key) { return this.data[key] ?? null; },
    setItem(key, value) { this.data[key] = value; }
  };
  const win = { ZephtacoreLogic: L, ZEPHTACORE_BOARDS: BOARDS, ZEPHTACORE_ROLES: ROLES, location: {} };
  const src = fs.readFileSync(path.join(__dirname, "../../custom_builder_web/js/builder.js"), "utf8");
  new Function("window", "document", "sessionStorage", "URL", "Blob", src)(
    win, doc, store, { createObjectURL: () => "blob:" }, class { constructor(parts) { blobs.push(parts.join("")); } });
  ready();
  return { els, blobs, pending: () => JSON.parse(store.data[KEY]) };
}

const SAVED = {
  boardId: "gat562_30s", role: "repeater", name: "Baldiri", adminPass: "mi-clave", guestPass: "", lat: "41.4", lon: "2.1",
  gps: "off", freq: "869.525", bw: "62.5", sf: "8", cr: "2", txPower: "", mtRelay: "all", mtOpaque: false, mtHops: "5",
  pingBot: true, vroomEnable: false, vroomAccess: "open", vroomPass: "", autoShutdownMv: "3050"
};

test("coming back to the builder keeps the settings waiting to be sent", () => {
  const page = loadBuilder(SAVED);
  const { timestamp, boardName, mcu, flashType, roleName, ...kept } = page.pending();
  assert.deepEqual(kept, SAVED);
  assert.equal(page.els.adminPassInput.value, "mi-clave");
});

test("an admin password left empty stays empty", () => {
  assert.equal(loadBuilder({ ...SAVED, adminPass: "" }).pending().adminPass, "");
});

test("with nothing pending a name and a password are generated and no TX power is set", () => {
  const { pending } = loadBuilder();
  assert.match(pending().name, /^Zephta-[0-9A-F]{4}$/);
  assert.equal(pending().adminPass.length, 12);
  assert.equal(pending().txPower, "");
});

test("the downloaded conf carries a TX power only when one was chosen", () => {
  for (const [txPower, expected] of [["", false], ["14", true]]) {
    const page = loadBuilder({ ...SAVED, txPower });
    page.els.btnDownloadConf.listeners.click();
    assert.equal(page.blobs[0].includes("CONFIG_ZEPHCORE_DEFAULT_TX_POWER_DBM=14"), expected);
    assert.equal(page.blobs[0].includes("CONFIG_ZEPHCORE_DEFAULT_TX_POWER_DBM"), expected);
  }
});
