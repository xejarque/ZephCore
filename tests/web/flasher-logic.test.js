const test = require("node:test");
const assert = require("node:assert/strict");
const L = require("../../custom_builder_web/js/flasher-logic.js");

const CDN = "https://xejarque.github.io/ZephCore/firmware/";
const file = (name, url = CDN + name) => ({ name, url });
const catalog = (versions) => ({ device: [{ firmware: [{ role: "x", version: versions }] }] });

test("coding rate: builder values 1-4 become 5-8, firmware values pass through", () => {
  assert.equal(L.firmwareCodingRate("1"), 5);
  assert.equal(L.firmwareCodingRate(2), 6);
  assert.equal(L.firmwareCodingRate("4"), 8);
  assert.equal(L.firmwareCodingRate("5"), 5);
  assert.equal(L.firmwareCodingRate("8"), 8);
});

const cfg = {
  name: "Node", adminPass: "pw", freq: "869.618", bw: "62.5", sf: "7", cr: "2", txPower: "22",
  mtRelay: "essential", mtOpaque: true, mtHops: "7", pingBot: false, lat: "", lon: ""
};

test("repeater and companion commands follow docs/CLI_commands.md", () => {
  assert.deepEqual(L.buildConfigCommands({ ...cfg, role: "repeater" }, 1700000000), [
    "set name Node", "password pw", "set radio 869.618,62.5,7,6", "set tx 22",
    "set z.mt.enable on", "set z.mt.relay essential", "set z.mt.relay.opaque on", "set z.mt.hops 7",
    "time 1700000000", "reboot"
  ]);
  const defaultTimeCmds = L.buildConfigCommands({ ...cfg, role: "companion" });
  const timeIdx = defaultTimeCmds.findIndex((c) => /^time \d{10,}$/.test(c));
  assert.ok(timeIdx >= 0 && timeIdx === defaultTimeCmds.length - 2);
  assert.equal(defaultTimeCmds.at(-1), "reboot");
});

test("no password is sent when none is set; ping bot carries its command; position only when set", () => {
  const cmds = L.buildConfigCommands({ ...cfg, role: "repeater", adminPass: "", pingBot: true, lat: "41.4", lon: "2.1", mtOpaque: false });
  assert.ok(!cmds.some((c) => c.startsWith("password")));
  assert.ok(cmds.includes("set z.mc.ping.enable on /ping") && cmds.includes("set z.mt.ping.enable on /ping"));
  assert.ok(cmds.includes("set z.mt.relay.opaque off"));
  assert.ok(cmds.includes("set lat 41.4") && cmds.includes("set lon 2.1"));
  assert.equal(cmds.at(-1), "reboot");
});

test("a companion gets no password, which its firmware refuses", () => {
  const cmds = L.buildConfigCommands({ ...cfg, role: "companion", guestPass: "guest" });
  assert.ok(!cmds.some((c) => c.startsWith("password") || c.startsWith("set guest.password")));
});

test("guest password, GPS and auto-shutdown are sent only when chosen", () => {
  const none = L.buildConfigCommands({ ...cfg, role: "repeater", guestPass: "", gps: "", autoShutdownMv: "" });
  assert.ok(!none.some((c) => /guest|^gps|autoshutdown/.test(c)));
  const all = L.buildConfigCommands({ ...cfg, role: "repeater", guestPass: "guest", gps: "off", autoShutdownMv: "3000" });
  assert.deepEqual(all.slice(-2), ["gps off", "reboot"]);
  assert.ok(all.includes("set guest.password guest"));
  assert.ok(all.includes("gps off"));
  assert.ok(all.includes("set z.autoshutdown 3000"));
  assert.equal(all.at(-1), "reboot");
});

test("observer gets its own commands: no password, tx or z.mt", () => {
  assert.deepEqual(L.buildConfigCommands({ ...cfg, role: "observer" }), [
    "set name Node", "set freq 869.618", "set sf 7", "set bw 62.5", "set cr 6", "reboot"
  ]);
});

test("repeater virtual room emits access, optional password, and room.on before reboot", () => {
  const openCmds = L.buildConfigCommands({ ...cfg, role: "repeater", vroomEnable: true, vroomAccess: "open", vroomPass: "" });
  assert.ok(openCmds.includes("set z.room.access open"));
  assert.ok(openCmds.includes("set z.room.on on"));
  assert.ok(!openCmds.some((c) => c.startsWith("set z.room.password")));
  assert.ok(openCmds.indexOf("set z.room.access open") < openCmds.indexOf("set z.room.on on"));
  assert.equal(openCmds.at(-1), "reboot");

  const privCmds = L.buildConfigCommands({ ...cfg, role: "repeater", vroomEnable: true, vroomAccess: "private", vroomPass: "sala123" });
  assert.ok(privCmds.indexOf("set z.room.access private") < privCmds.indexOf("set z.room.password sala123"));
  assert.ok(privCmds.indexOf("set z.room.password sala123") < privCmds.indexOf("set z.room.on on"));
  assert.equal(privCmds.at(-1), "reboot");

  const offCmds = L.buildConfigCommands({ ...cfg, role: "repeater", vroomEnable: false, vroomAccess: "private", vroomPass: "sala123" });
  assert.ok(!offCmds.some((c) => c.startsWith("set z.room.")));

  const compCmds = L.buildConfigCommands({ ...cfg, role: "companion", vroomEnable: true, vroomAccess: "open" });
  assert.ok(!compCmds.some((c) => c.startsWith("set z.room.")));
});

test("catalogFiles takes only the newest version of each firmware", () => {
  const files = L.catalogFiles(catalog({
    "1.9.0": { files: [file("old")] },
    "1.18.2": { files: [file("new")] },
    "1.10.0": { files: [file("mid")] }
  }));
  assert.deepEqual(files.map((f) => f.name), ["new"]);
});

test("findCatalogFile matches the exact target, role and zephtacore variant", () => {
  const files = [
    file("t1000_e-companion-abc1234.uf2"),
    file("t1000_e-companion-zephtacore-abc1234.uf2"),
    file("t1000_e-repeater-zephtacore-abc1234.uf2"),
    file("heltec_v4-esp32s3-procpu-companion-zephtacore-def5678-merged.bin"),
    file("heltec_v4-esp32s3-procpu-companion-zephtacore-def5678-update.bin")
  ];
  const uf2 = { id: "t1000_e", flashType: "uf2" };
  assert.equal(L.findCatalogFile(files, uf2, "companion").name, "t1000_e-companion-zephtacore-abc1234.uf2");
  assert.equal(L.findCatalogFile(files, uf2, "repeater").name, "t1000_e-repeater-zephtacore-abc1234.uf2");
  assert.equal(
    L.findCatalogFile(files, { id: "heltec_v4/esp32s3/procpu", flashType: "bin" }, "companion").name,
    "heltec_v4-esp32s3-procpu-companion-zephtacore-def5678-merged.bin"
  );
});

test("findCatalogFile finds nothing for a board without a zephtacore build or the wrong format", () => {
  const files = [file("t1000_e-companion-abc1234.uf2"), file("t1000_e-companion-zephtacore-abc1234.uf2")];
  assert.equal(L.findCatalogFile(files, { id: "rak4631", flashType: "uf2" }, "companion"), null);
  assert.equal(L.findCatalogFile(files, { id: "t1000_e", flashType: "hex" }, "companion"), null);
  assert.equal(L.findCatalogFile(files, { id: "t1000_e", flashType: "uf2" }, "repeater"), null);
});

test("findCatalogFile does not treat a board id as a regex or a prefix", () => {
  const files = [file("t1000_e-companion-zephtacore-abc1234.uf2")];
  assert.equal(L.findCatalogFile(files, { id: "t1000.e", flashType: "uf2" }, "companion"), null);
  assert.equal(L.findCatalogFile(files, { id: "t1000", flashType: "uf2" }, "companion"), null);
});

test("findCatalogFile refuses a URL outside the ZephCore firmware-dist CDN", () => {
  const name = "t1000_e-companion-zephtacore-abc1234.uf2";
  const board = { id: "t1000_e", flashType: "uf2" };
  assert.equal(L.findCatalogFile([file(name, "https://evil.example/" + name)], board, "companion"), null);
  assert.equal(L.findCatalogFile([file(name, "http://cdn.jsdelivr.net/gh/xejarque/ZephCore@firmware-dist/" + name)], board, "companion"), null);
  assert.equal(L.findCatalogFile([file(name, "https://cdn.jsdelivr.net/gh/other/ZephCore@firmware-dist/" + name)], board, "companion"), null);
});

test("kconfigString escapes quotes and backslashes; shellQuote survives a single quote", () => {
  assert.equal(L.kconfigString('a"b\\c'), '"a\\"b\\\\c"');
  assert.equal(L.shellQuote("it's"), "'it'\\''s'");
});

test("the formatter is the .uf2 beside the .zip the catalog names, from the firmware-dist CDN only", () => {
  const zip = (name) => ({ erase: CDN + name });
  assert.equal(L.formatterUrl(zip("SoftDevice_v6_formatter.zip")), CDN + "SoftDevice_v6_formatter.uf2");
  assert.equal(L.formatterUrl(zip("SoftDevice_v7_formatter.zip")), CDN + "SoftDevice_v7_formatter.uf2");
  assert.equal(L.formatterUrl(zip("SoftDevice_v9_formatter.zip")), null);
  assert.equal(L.formatterUrl(zip("evil.zip")), null);
  assert.equal(L.formatterUrl({ erase: "https://example.com/SoftDevice_v7_formatter.zip" }), null);
  assert.equal(L.formatterUrl({}), null);
  assert.equal(L.formatterUrl(null), null);
});

test("each catalog file carries the erase package of its firmware option", () => {
  const cat = { device: [{ firmware: [{ role: "x", erase: CDN + "SoftDevice_v7_formatter.zip", version: { "1.0": { files: [{ name: "a.uf2", url: CDN + "a.uf2" }] } } }] }] };
  assert.deepEqual(L.catalogFiles(cat), [{ name: "a.uf2", url: CDN + "a.uf2", erase: CDN + "SoftDevice_v7_formatter.zip", erase_sha256: undefined, erase_uf2_sha256: undefined }]);
});

test("the formatter's digests travel with the firmware option, so both DFU and UF2 clean installs check them", () => {
  const cat = { device: [{ firmware: [{ role: "x", erase: CDN + "SoftDevice_v7_formatter.zip", erase_sha256: "ab12", erase_uf2_sha256: "cd34", version: { "1.0": { files: [{ name: "a.uf2", url: CDN + "a.uf2" }] } } }] }] };
  assert.equal(L.catalogFiles(cat)[0].erase_sha256, "ab12");
  assert.equal(L.catalogFiles(cat)[0].erase_uf2_sha256, "cd34");
});

test("the DFU zip of a board is found by kind, and the formatter has a .zip and a .uf2", () => {
  const board = { id: "t1000_e", flashType: "uf2" };
  const files = [
    { name: "t1000_e-companion-zephtacore-abc1234.uf2", url: CDN + "t1000_e-companion-zephtacore-abc1234.uf2" },
    { name: "t1000_e-companion-zephtacore-abc1234.zip", url: CDN + "t1000_e-companion-zephtacore-abc1234.zip" }
  ];
  assert.equal(L.findCatalogFile(files, board, "companion").name, "t1000_e-companion-zephtacore-abc1234.uf2");
  assert.equal(L.findCatalogFile(files, board, "companion", "dfu").name, "t1000_e-companion-zephtacore-abc1234.zip");
  assert.equal(L.formatterUrl({ erase: CDN + "SoftDevice_v7_formatter.zip" }, "zip"), CDN + "SoftDevice_v7_formatter.zip");
});

test("sha256: no hash in the catalog passes, the right one passes in any case, another one fails", async () => {
  const { subtle } = require("node:crypto").webcrypto;
  const bytes = new TextEncoder().encode("abc");
  const abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  assert.equal(await L.sha256Matches(bytes, undefined, subtle), true);
  assert.equal(await L.sha256Matches(bytes, abc, subtle), true);
  assert.equal(await L.sha256Matches(bytes, abc.toUpperCase(), subtle), true);
  assert.equal(await L.sha256Matches(bytes, "00" + abc.slice(2), subtle), false);
  await assert.rejects(L.sha256Matches(bytes, abc, undefined));
  assert.equal(await L.sha256Matches(bytes, undefined, undefined), true);
});

test("every command with a password has a stand-in without it", () => {
  const cmds = L.buildConfigCommands({
    role: "repeater", name: "n", freq: "869.618", sf: "7", bw: "62.5", cr: "2", txPower: "22",
    adminPass: "s3cret one", guestPass: "gu3st", vroomEnable: true, vroomAccess: "private", vroomPass: "r00m"
  }, 0);
  const shown = cmds.map((c) => L.maskedCommand(c) || c);
  assert.equal(shown.some((c) => /s3cret|gu3st|r00m/.test(c)), false);
  assert.deepEqual(cmds.map(L.maskedCommand).filter(Boolean), ["password ••••", "set guest.password ••••", "set z.room.password ••••"]);
  assert.equal(L.maskedCommand("set name password x"), "");
});

test("no TX power is sent unless one was chosen, so the board keeps its own", () => {
  const cmds = L.buildConfigCommands({ ...cfg, role: "repeater", txPower: "" });
  assert.ok(!cmds.some((c) => c.startsWith("set tx")));
});

test("the role is read from the reply to get role", () => {
  assert.equal(L.nodeRole("> repeater"), "repeater");
  assert.equal(L.nodeRole("> room_server"), "room_server");
  assert.equal(L.nodeRole("Unknown command"), "");
});

test("the admin password is accepted only when the node echoes it whole", () => {
  assert.equal(L.refusalReason("password abc", "password now: abc"), "");
  assert.match(L.refusalReason("password abcdefghijklmnop", "password now: abcdefghijklmno"), /15 bytes/);
  assert.match(L.refusalReason("password abc", "OK"), /no confirmó/);
  assert.equal(L.refusalReason("set name x", "OK"), "");
  assert.match(L.refusalReason("set name x", "Error: bad"), /Error: bad/);
});

test("the DFU stages follow one another without going back", () => {
  const marks = ["start", "erase", "init", "write", "activate"].flatMap((s) => [L.dfuProgress(s, 0), L.dfuProgress(s, 1)]);
  assert.deepEqual(marks, [...marks].sort((a, b) => a - b));
  assert.deepEqual([marks[0], marks.at(-1)], [0, 1]);
});

test("the ping bot is turned on only in the roles that answer", () => {
  const bots = (role) => L.buildConfigCommands({ ...cfg, role, pingBot: true }).filter((c) => c.includes(".ping."));
  assert.equal(bots("companion").length, 2);
  assert.deepEqual(bots("room_server"), []);
});

test("a companion that is to relay Meshtastic is set to client; no other node changes role", () => {
  const roles = (role, mtRelay) => L.buildConfigCommands({ ...cfg, role, mtRelay }).filter((c) => c.startsWith("set z.mt.role"));
  assert.deepEqual(roles("companion", "essential"), ["set z.mt.role client"]);
  assert.deepEqual(roles("companion", "all"), ["set z.mt.role client"]);
  assert.deepEqual(roles("companion", "off"), []);
  assert.deepEqual(roles("repeater", "all"), []);
});

test("only a missing GPS lets the rest of the settings go on", () => {
  assert.match(L.skippedSetting("gps on", "gps toggle not found"), /GPS/);
  assert.equal(L.skippedSetting("set name x", "gps toggle not found"), "");
  assert.equal(L.skippedSetting("gps on", "Error: bad"), "");
});

test("controls are disabled while a task runs and left as they were, even when it fails", async () => {
  const controls = [{ disabled: false }, null, { disabled: true }];
  await assert.rejects(L.withDisabled(controls, async () => {
    assert.deepEqual([controls[0].disabled, controls[2].disabled], [true, true]);
    throw new Error("boom");
  }), /boom/);
  assert.deepEqual([controls[0].disabled, controls[2].disabled], [false, true]);
});
