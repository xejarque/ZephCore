const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const BOARDS_DIR = path.join(__dirname, "../../zephcore/boards");
const PLATFORM_OF_DIR = { nrf52840: "nrf52", nrf54l: "nrf54l", esp32: "esp32", mg24: "mg24", stm32wl: "stm32wl" };

function loadWebBoards() {
  global.window = {};
  require("../../custom_builder_web/js/boards-manifest.js");
  return global.window.ZEPHTACORE_BOARDS;
}

function loadTargets() {
  const targets = new Map();
  for (const dir of Object.keys(PLATFORM_OF_DIR)) {
    const root = path.join(BOARDS_DIR, dir);
    for (const board of fs.readdirSync(root, { withFileTypes: true })) {
      const yml = path.join(root, board.name, "zephcore.yml");
      if (!board.isDirectory() || !fs.existsSync(yml)) continue;
      const target = fs.readFileSync(yml, "utf8").match(/^target:\s*(\S+)/m);
      if (target) targets.set(target[1], PLATFORM_OF_DIR[dir]);
    }
  }
  return targets;
}

const web = loadWebBoards();
const targets = loadTargets();

test("every builder board has a zephcore.yml", () => {
  const missing = web.map((b) => b.id).filter((id) => !targets.has(id));
  assert.deepEqual(missing, []);
});

test("builder board ids are unique", () => {
  const ids = web.map((b) => b.id);
  assert.equal(new Set(ids).size, ids.length);
});

test("arch and flash type follow the board's platform", () => {
  for (const b of web.filter((b) => targets.has(b.id))) {
    const platform = targets.get(b.id);
    assert.equal(b.arch, platform, `${b.id}: arch`);
    assert.equal(b.flashType === "bin", platform === "esp32", `${b.id}: flashType ${b.flashType}`);
  }
});

test("auto-shutdown is offered where the board wakes on battery, up to 300 mV under its wake voltage", () => {
  const wake = new Map();
  for (const dir of Object.keys(PLATFORM_OF_DIR)) {
    const root = path.join(BOARDS_DIR, dir);
    for (const board of fs.readdirSync(root, { withFileTypes: true })) {
      const dts = path.join(root, board.name, `${board.name}.dts`);
      if (!board.isDirectory() || !fs.existsSync(dts)) continue;
      const mv = fs.readFileSync(dts, "utf8").match(/battery-wake-mv = <(\d+)>/);
      if (mv) wake.set(board.name, Number(mv[1]));
    }
  }
  const offered = web.filter((b) => b.batteryWake);
  assert.deepEqual(offered.map((b) => b.id).sort(), [...wake.keys()].sort());
  for (const b of offered) {
    assert.equal(b.batteryWake.maxMv, wake.get(b.id) - 300, `${b.id}: maxMv`);
    assert.ok(b.batteryWake.suggestedMv >= 2900 && b.batteryWake.suggestedMv <= b.batteryWake.maxMv, `${b.id}: suggestedMv`);
  }
});
