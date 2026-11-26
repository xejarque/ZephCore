const test = require("node:test");
const assert = require("node:assert/strict");
const L = require("../../custom_builder_web/js/flasher-logic.js");
const B = require("../../custom_builder_web/js/node-backup.js");

const PRV = "a1".repeat(64);
const PUB = "b2".repeat(32);

// Replies as the firmware writes them, after the "-> " mark.
const REPEATER = {
  "get role": "> repeater",
  "get public.key": `> ${PUB}`,
  "get prv.key": `> ${PRV}`,
  "get password": "> admin pass",
  "get name": "> Cima Norte",
  "get owner.info": ">",
  "get guest.password": "> guest",
  "get radio": "> 869.618,62.5,7,6",
  "get tx": "> 22",
  "get radio.rxgain": "> on",
  "get radio.fem.rxgain": "> off",
  "get af": "> 1.00",
  "get int.thresh": "> 14",
  "get multi.acks": "> 1",
  "get cad.auto": "> on",
  "get cad.offset": "> -3",
  "get cad.busycap": "> 0 (off)",
  "get probe.interval": "> 15",
  "get extra.sf": "9,10",
  "get rxduty": "> 0",
  "get backoff.multiplier": "> 0.20",
  "get gps duty": "> always on (0)",
  "get gps standby": "> 300",
  "powersaving": "off",
  "get lat": "> 41.390000",
  "get lon": "> 2.170000",
  "gps": "off",
  "gps advert": "> prefs",
  "get repeat": "> on",
  "get advert.interval": "> 0",
  "get flood.advert.interval": "> 47",
  "get flood.max": "> 64",
  "get flood.max.unscoped": "> 64",
  "get flood.max.advert": "> 8",
  "get loop.detect": "> off",
  "get path.hash.mode": "> 1",
  "get allow.read.only": "unknown config: allow.read.only",
  "get meshtimesync": "on - 2 peers\r\n a1b2 h1",
  "get z.mt.enable": "> on",
  "get z.mt.role": "> router",
  "get z.mt.relay": "> essential",
  "get z.mt.relay.opaque": "> on",
  "get z.mt.hops": "> 7",
  "get z.mt.nodeinfo": "> 3",
  "get z.mt.telemetry": "> 1",
  "get z.mt.channel": "> LongFast (default key)",
  "get z.mt.zerohop": "> auto !a1b2c3d4 !0000beef",
  "get z.mc.fw.drop": "> none",
  "get z.mc.fw.gap": "> 0",
  "get z.mc.fw.rate": "> 0",
  "get z.mt.fw.drop": "> text 3 position 0",
  "get z.mt.fw.gap": "> 0",
  "get z.mt.fw.rate": "> 0",
  "get z.mc.ping.trigger": ">",
  "get z.mc.ping.enable": "> off",
  "get z.mt.ping.trigger": "> !ping",
  "get z.mt.ping.enable": "> on",
  "get z.ping.snr": "> on",
  "get z.ping.rssi": "> on",
  "get z.ping.ignore.direct": "> off",
  "get z.ping.ignore.remote": "> off",
  "get z.room.access": "> private",
  "get z.room.password": "> room",
  "get z.room.on": "> on",
  "get z.autoshutdown": "> 3200 mV",
  "get z.mc.regions 1": "Regions 1/2\n* F\n es F\n  cat^ F",
  "get z.mc.regions 2": "Regions 2/2\n  mad\n $privada",
  "region default": "default scope is cat"
};

// A node that answers from the table; a command it does not know is refused.
function fakeNode(replies = {}) {
  const table = { ...REPEATER, ...replies };
  const asked = [];
  const ask = async (command, options = {}) => {
    asked.push({ command, secret: Boolean(options.secret) });
    if (table[command] === null) throw new Error(`el nodo no respondió a "${command}"`);
    const reply = L.nodeReply(`${command}\r\n  -> ${table[command] ?? `??: ${command}`}\r\n`);
    return { reply, refused: L.isErrorReply(reply) };
  };
  return { ask, asked };
}

const noQuestion = async () => assert.fail("the password must not be asked");
const collect = (replies) => B.collect(fakeNode(replies).ask, noQuestion);

test("a repeater is read into a versioned backup that warns what it holds", async () => {
  const { backup, warnings } = await collect();
  assert.deepEqual(warnings, []);
  assert.equal(backup.format, "zephtacore-node-backup");
  assert.equal(backup.version, 1);
  assert.match(backup.warning, /clave privada/);
  assert.equal(backup.role, "repeater");
  assert.equal(backup.publicKey, PUB);
  assert.equal(backup.identity.prv, PRV);
  assert.equal(backup.password, "admin pass");
  const s = backup.settings;
  assert.equal(s.name, "Cima Norte");
  assert.equal(s["owner.info"], "");
  assert.equal(s.gps, "off");
  assert.equal(s.meshtimesync, "on");
  assert.equal(s["z.mt.fw.drop"], "text position");
  assert.equal(s["z.mc.fw.drop"], "");
  assert.equal(s["z.autoshutdown"], "3200");
  assert.equal("allow.read.only" in s, false);
  assert.deepEqual(backup.regions, { lines: ["* F", " es F", "  cat^ F", "  mad", " $privada"], default: "cat" });
});

test("secrets are asked for as secrets", async () => {
  const node = fakeNode();
  await B.collect(node.ask, noQuestion);
  const secrets = node.asked.filter((a) => a.secret).map((a) => a.command);
  assert.deepEqual(secrets, ["get prv.key", "get password", "get guest.password", "get z.room.password"]);
});

test("an older firmware: the password is asked for and the regions are left out", async () => {
  const node = fakeNode({ "get password": "??: password", "get z.mc.regions 1": "Error: unknown command" });
  const { backup, warnings } = await B.collect(node.ask, async () => "typed");
  assert.equal(backup.password, "typed");
  assert.equal(backup.regions, null);
  assert.match(warnings.join(" "), /regiones/);

  const without = await B.collect(node.ask, async () => "");
  assert.equal(without.backup.password, null);
  assert.match(without.warnings.join(" "), /clave de administrador/);
});

test("settings the board does not have are left out", async () => {
  const { backup } = await collect({
    "gps": "Can't find GPS",
    "get z.autoshutdown": "Error: not available on this board",
    "get meshtimesync": "not available"
  });
  for (const key of ["gps", "z.autoshutdown", "meshtimesync"]) assert.equal(key in backup.settings, false, key);
  const bare = await collect({ "get extra.sf": "No extra SF configured", "get gps duty": "> 900" });
  assert.equal("extra.sf" in bare.backup.settings, false);
  assert.equal(bare.backup.settings["gps.duty"], "900");
});

test("radio, power and GPS settings are saved and set again with the words each command takes", async () => {
  const cmds = await commands();
  for (const cmd of [
    "set radio.fem.rxgain off", "set af 1.00", "set int.thresh 14", "set multi.acks 1", "set cad.auto on",
    "set cad.offset -3", "set cad.busycap 0", "set probe.interval 15", "set extra.sf 9 10", "set rxduty 0",
    "set backoff.multiplier 0.20", "set gps duty 0", "set gps standby 300"
  ]) assert.ok(cmds.includes(cmd), cmd);
  assert.ok(cmds.indexOf("set radio 869.618,62.5,7,6") < cmds.indexOf("set cad.offset -3"));
});

test("a gain the radio does not have is stored by the node and not reported", async () => {
  const { backup } = await collect();
  const refused = await B.restore(backup, async (s) => {
    const no = /rxgain|set af /.test(s.cmd);
    return { refused: no, reply: no ? "Error: unsupported" : "OK" };
  });
  assert.deepEqual(refused.map((r) => r.command), ["set af 1.00"]);
});

test("a custom Meshtastic channel is warned about and not set again", async () => {
  const { backup, warnings } = await collect({ "get z.mt.channel": "> Privado (custom key)" });
  assert.match(warnings.join(" "), /canal Meshtastic/);
  assert.equal(B.restoreSteps(backup).some((s) => s.cmd.startsWith("set z.mt.channel")), false);
});

test("nothing is saved from a companion, a silent node or one that withholds what a restore needs", async () => {
  await assert.rejects(collect({ "get role": "> companion" }), /repetidor y room server/);
  await assert.rejects(collect({ "get radio": "Error: nope" }), /radio/);
  await assert.rejects(collect({ "get prv.key": "??: prv.key" }), /prv\.key/);
  await assert.rejects(collect({ "get flood.max": null }), /no respondió/);
  await assert.rejects(collect({ "get z.mc.regions 2": "Regions: page 2 of 1" }), /regiones/);
});

const commands = async (replies) => B.restoreSteps((await collect(replies)).backup).map((s) => s.cmd);

test("restore sets the values again, the identity last and then a reboot", async () => {
  const cmds = await commands();
  assert.equal(cmds[0], "set name Cima Norte");
  assert.equal(cmds[1], "set owner.info ");
  for (const cmd of [
    "set radio 869.618,62.5,7,6", "set tx 22", "gps off", "gps advert prefs", "set meshtimesync on",
    "set z.mt.channel LongFast default", "set z.mt.ping.trigger !ping", "set z.room.password room",
    "set z.autoshutdown 3200"
  ]) assert.ok(cmds.includes(cmd), cmd);
  assert.deepEqual(cmds.slice(-4), ["password admin pass", `set prv.key ${PRV}`, "powersaving off", "reboot"]);
  assert.equal(cmds.some((c) => c.startsWith("set z.mc.ping.trigger")), false);
});

test("restore rebuilds lists: firewall rules and fixed neighbours", async () => {
  const cmds = await commands();
  const from = (start) => cmds.filter((c) => c.startsWith(start));
  assert.deepEqual(from("set z.mt.fw.drop"), ["set z.mt.fw.drop none", "set z.mt.fw.drop text on", "set z.mt.fw.drop position on"]);
  assert.deepEqual(from("set z.mc.fw.drop"), ["set z.mc.fw.drop none"]);
  assert.deepEqual(from("set z.mt.zerohop"), [
    "set z.mt.zerohop auto", "set z.mt.zerohop.clear",
    "set z.mt.zerohop.add !a1b2c3d4", "set z.mt.zerohop.add !0000beef"
  ]);
});

test("the room is switched on after its access and password; no room password clears it", async () => {
  const cmds = await commands({ "get z.room.password": "> (none)" });
  const at = (c) => cmds.indexOf(c);
  assert.ok(at("set z.room.access private") < at("set z.room.password clear"));
  assert.ok(at("set z.room.password clear") < at("set z.room.on on"));
});

test("the region map is loaded without the home mark, then flags, default and home are set", async () => {
  const cmds = await commands();
  const from = cmds.indexOf("region load");
  assert.deepEqual(cmds.slice(from, cmds.indexOf("region save") + 1), [
    "region load", " es F", "  cat F", "  mad", " $privada", "",
    "region default cat",
    "region allowf *", "region allowf es", "region allowf cat", "region denyf mad", "region denyf $privada",
    "region home cat",
    "region save"
  ]);
  const steps = B.restoreSteps((await collect()).backup);
  assert.deepEqual(steps.slice(from, from + 6).map((s) => s.wait), [false, false, false, false, false, true]);
});

test("no secret is shown", async () => {
  const steps = B.restoreSteps((await collect()).backup);
  const shown = steps.map((s) => s.shown).join("\n");
  for (const secret of [PRV, "admin pass", "set guest.password guest", "set z.room.password room"]) {
    assert.equal(shown.includes(secret), false, secret);
  }
  assert.deepEqual(steps.filter((s) => s.secret).map((s) => s.shown), [
    "set guest.password ••••", "set z.room.password ••••", "password ••••", "set prv.key ••••"
  ]);
});

test("restore goes on after a refusal and reports it without secrets", async () => {
  const { backup } = await collect();
  const sent = [];
  const refused = await B.restore(backup, async (s) => {
    sent.push(s.cmd);
    const no = s.cmd === "set tx 22" || s.secret;
    return { refused: no, reply: no ? `Error: ${s.cmd}` : "OK" };
  });
  assert.equal(sent.at(-1), "reboot");
  assert.deepEqual(refused[1], { command: "set tx 22", reply: "Error: set tx 22" });
  assert.equal(JSON.stringify(refused).includes(PRV), false);
});

test("a saved backup is read back; a damaged or crafted file is not", async () => {
  const { backup } = await collect();
  const text = JSON.stringify(backup, null, 2);
  assert.deepEqual(B.parseBackup(text), backup);
  const broken = (change) => {
    const copy = JSON.parse(text);
    change(copy);
    return () => B.parseBackup(JSON.stringify(copy));
  };
  assert.throws(broken((b) => (b.format = "prefs")), /no es un respaldo/);
  assert.throws(broken((b) => (b.version = 2)), /no es un respaldo/);
  assert.throws(broken((b) => (b.role = "companion")), /dañado/);
  assert.throws(broken((b) => (b.identity.prv = "abc")), /dañado/);
  assert.throws(broken((b) => (b.settings.name = "x\r\nerase")), /dañado/);
  assert.throws(broken((b) => (b.password = "")), /dañado/);
  assert.throws(broken((b) => delete b.created), /dañado/);
  assert.throws(broken((b) => delete b.settings.name), /dañado/);
  assert.throws(broken((b) => (b.regions.lines = [" a\nerase"])), /dañado/);
  assert.throws(() => B.parseBackup("not json"), SyntaxError);
});
