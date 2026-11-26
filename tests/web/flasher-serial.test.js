const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const L = require("../../custom_builder_web/js/flasher-logic.js");

// Loads flasher-serial.js against a fake port whose node answers via reply(cmd).
async function connectedSerial(reply) {
  const sent = [];
  let rx;
  const readable = new ReadableStream({ start: (c) => (rx = c) });
  const writable = new WritableStream({
    write(chunk) {
      const line = new TextDecoder().decode(chunk);
      sent.push(line);
      const answer = reply(line.trim());
      if (answer !== null) setTimeout(() => rx.enqueue(new TextEncoder().encode(answer)), 5);
    }
  });
  const port = { open: async () => {}, readable, writable, close: async () => {} };
  const shown = [];
  const terminal = { appendChild: (span) => shown.push(span.textContent) };
  const doc = {
    getElementById: (id) => (id === "terminalBody" ? terminal : null),
    createElement: () => ({}),
    querySelectorAll: () => [],
    addEventListener: () => {}
  };
  const nav = { serial: { requestPort: async () => port, addEventListener: () => {} } };
  const src = fs.readFileSync(path.join(__dirname, "../../custom_builder_web/js/flasher-serial.js"), "utf8");
  const win = { ZephtacoreLogic: L };
  new Function("window", "document", "navigator", src)(win, doc, nav);
  assert.equal(await win.ZephtacoreSerial.connect(), true);
  return {
    serial: win.ZephtacoreSerial,
    sent,
    shown,
    push: (text) => rx.enqueue(new TextEncoder().encode(text))
  };
}

// The bytes a node writes for a command: its echo, then the reply line.
const answer = (cmd, reply) => `${cmd}\r\n  -> ${reply}\r\n`;

test("the reply is what follows the node's reply mark, not the echo", () => {
  assert.equal(L.nodeReply(answer("set tx 22", "OK")), "OK");
  assert.equal(L.nodeReply("-> > 869.618,62.5,7,6"), "> 869.618,62.5,7,6");
  assert.equal(L.nodeReply("set name a -> b\r\n  -> OK"), "OK");
  assert.equal(L.nodeReply("set tx 22\r\n"), null);
  assert.equal(L.nodeReply(""), null);
});

test("the reply ends at its CR LF: its own lines stay, what the node prints next does not", () => {
  assert.equal(L.nodeReply("get z.mc.regions\r\n  -> Regions 1/1\n* F\n es F\r\n"), "Regions 1/1\n* F\n es F");
  assert.equal(L.nodeReply(answer("get tx", "> 22") + "RX, len=40\r\n"), "> 22");
});

test("a refused command is recognised in every form the firmware writes", () => {
  for (const reply of [
    "Error: not supported on companion", "Error, invalid radio params", "ERROR: Power management not supported",
    "ERR: bad pubkey", "Err - save failed", "> Error: bad key", "unknown config: z.nope", "Unknown command",
    "??: nope", "gps toggle not found"
  ]) {
    assert.equal(L.isErrorReply(L.nodeReply(answer("cmd", reply))), true, reply);
  }
  for (const reply of ["OK", "ok", "OK - reboot to apply", "> essential", "password now: errar",
    "(ERR: clock cannot go backwards)"]) {
    assert.equal(L.isErrorReply(L.nodeReply(answer("cmd", reply))), false, reply);
  }
  assert.equal(L.isErrorReply(null), false);
});

test("connecting again keeps the open port", async () => {
  const { serial, sent } = await connectedSerial((cmd) => answer(cmd, "OK"));
  assert.equal(await serial.connect(), true);
  await serial.sendCommandAndWait("set tx 22", { timeoutMs: 1000, quietMs: 20 });
  assert.deepEqual(sent, ["set tx 22\r\n"]);
});

test("sendCommandAndWait returns what the node answers", async () => {
  const { serial, sent } = await connectedSerial((cmd) => answer(cmd, "OK"));
  const r = await serial.sendCommandAndWait("set tx 22", { timeoutMs: 1000, quietMs: 30 });
  assert.deepEqual(sent, ["set tx 22\r\n"]);
  assert.equal(r.sent, true);
  assert.equal(r.timedOut, false);
  assert.equal(r.text, "OK");
});

test("sendCommandAndWait waits for a reply that comes long after the echo", async () => {
  const { serial, push } = await connectedSerial((cmd) => `${cmd}\r\n`);
  setTimeout(() => push("  -> Error: out of range\r\n"), 120);
  const r = await serial.sendCommandAndWait("set tx 99", { timeoutMs: 1000, quietMs: 30 });
  assert.equal(r.timedOut, false);
  assert.equal(L.isErrorReply(r.text), true);
});

test("sendCommandAndWait reports a node that only echoes or stays silent", async () => {
  const { serial } = await connectedSerial((cmd) => (cmd === "set tx 22" ? `${cmd}\r\n` : null));
  const echoed = await serial.sendCommandAndWait("set tx 22", { timeoutMs: 80, quietMs: 30 });
  assert.equal(echoed.timedOut, true);
  const silent = await serial.sendCommandAndWait("set name X", { timeoutMs: 80, quietMs: 30 });
  assert.equal(silent.timedOut, true);
});

test("sendCommandAndWait without a connection sends nothing", async () => {
  const doc = { getElementById: () => null, querySelectorAll: () => [], addEventListener: () => {} };
  const src = fs.readFileSync(path.join(__dirname, "../../custom_builder_web/js/flasher-serial.js"), "utf8");
  const win = {};
  new Function("window", "document", "navigator", src)(win, doc, {});
  const r = await win.ZephtacoreSerial.sendCommandAndWait("reboot", { timeoutMs: 50 });
  assert.equal(r.sent, false);
});

test("a raw line keeps its spaces, and an empty one is a lone LF", async () => {
  const { serial, sent } = await connectedSerial((cmd) => answer(cmd, "OK"));
  const options = { raw: true, timeoutMs: 1000, quietMs: 20 };
  await serial.sendCommandAndWait("set owner.info ", options);
  await serial.sendCommandAndWait("", options);
  await serial.writeLine("  es F");
  assert.deepEqual(sent, ["set owner.info \r\n", "\n", "  es F\r\n"]);
});

test("a secret exchange shows only its stand-in, and the terminal listens again afterwards", async () => {
  const { serial, shown, push } = await connectedSerial((cmd) => answer(cmd, "> hunter2"));
  const r = await serial.sendCommandAndWait("password hunter2", { secret: "password ••••", timeoutMs: 1000, quietMs: 20 });
  assert.equal(r.text, "> hunter2");
  assert.ok(shown.includes("> password ••••\n"));
  assert.equal(shown.join("").includes("hunter2"), false);
  push("later\r\n");
  await new Promise((resolve) => setTimeout(resolve, 20));
  assert.ok(shown.join("").includes("later"));
});
