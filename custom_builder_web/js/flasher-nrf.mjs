// nRF52 flashing over Web Serial: the Adafruit bootloader speaks serial DFU on its USB CDC port.
// The protocol lives in nrf-dfu-core.js / nrf-dfu-session.js; this file only owns the browser serial port.
import { readDfuPackage, takeFrame } from "./nrf-dfu-core.mjs";
import { runDfu } from "./nrf-dfu-session.mjs";
import { unzipSync } from "../vendor/fflate.js";

const DFU_BAUDRATE = 115200;
const TOUCH_BAUDRATE = 1200; // opening the port at 1200 baud and closing it asks the firmware to reboot into the bootloader
const TOUCH_SETTLE_MS = 100;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const TIMEOUT = Symbol("timeout");

const concat = (a, b) => {
  const joined = new Uint8Array(a.length + b.length);
  joined.set(a);
  joined.set(b, a.length);
  return joined;
};

/** The application image and init packet inside a DFU .zip. */
export function openDfuZip(bytes) {
  let entries;
  try {
    entries = Object.entries(unzipSync(bytes)).map(([name, data]) => ({ name, data }));
  } catch {
    throw new Error("El paquete DFU está dañado y no se puede abrir.");
  }
  return readDfuPackage(entries);
}

/**
 * Reboots a running node into the bootloader and returns the port it was running on. The board then
 * shows up as a different USB device, which the browser has its own permission for.
 */
export async function touchBootloader() {
  const port = await navigator.serial.requestPort();
  await port.open({ baudRate: TOUCH_BAUDRATE });
  await sleep(TOUCH_SETTLE_MS);
  await port.close();
  return port;
}

const disconnected = new WeakSet();

/** Notes when the port goes away, which waitForPort relies on. */
export function trackPort(port) {
  port.addEventListener("disconnect", () => disconnected.add(port));
  return port;
}

/** Asks for the bootloader's port. */
export async function requestPort() {
  return trackPort(await navigator.serial.requestPort());
}

/**
 * The same USB device after it rebooted (the formatter returns the board to the bootloader): the browser
 * keeps the permission already granted, so the port comes back from getPorts() without asking again.
 */
export async function waitForPort(previous, timeoutMs) {
  const { usbVendorId, usbProductId } = previous.getInfo();
  const deadline = performance.now() + timeoutMs;
  while (performance.now() < deadline) {
    if (disconnected.has(previous)) {
      const ports = await navigator.serial.getPorts();
      const found = ports.find((p) => {
        const info = p.getInfo();
        return info.usbVendorId === usbVendorId && info.usbProductId === usbProductId;
      });
      if (found) return trackPort(found);
    }
    await sleep(500);
  }
  throw new Error("La placa no ha vuelto a modo DFU. Pulsa dos veces RESET y vuelve a intentarlo.");
}

/** The `link` that runDfu expects, on top of an open Web Serial port. */
function serialLink(port) {
  const reader = port.readable.getReader();
  const writer = port.writable.getWriter();
  let buffer = new Uint8Array(0);
  let pending = null; // a read that timed out is kept and reused, so no byte is lost between calls
  let closed = false;

  return {
    write: (bytes) => writer.write(bytes),

    async readFrame(timeoutMs) {
      const deadline = performance.now() + timeoutMs;
      for (;;) {
        const taken = takeFrame(buffer);
        if (taken) {
          buffer = taken.rest;
          return taken.frame;
        }
        const remaining = deadline - performance.now();
        if (remaining <= 0) return null;
        pending ??= reader.read();
        const result = await Promise.race([
          pending,
          sleep(remaining).then(() => TIMEOUT),
        ]);
        if (result === TIMEOUT) return null;
        pending = null;
        if (result.done)
          throw new Error("El puerto se cerró de forma inesperada.");
        buffer = concat(buffer, result.value);
      }
    },

    async close() {
      if (closed) return;
      closed = true;
      await reader.cancel().catch(() => {});
      pending?.catch(() => {});
      reader.releaseLock();
      writer.releaseLock();
      await port.close().catch(() => {});
    },
  };
}

/**
 * Flashes a DFU package on the bootloader's port (asked for when not given).
 * @param {{bin: Uint8Array, dat: Uint8Array}} dfu
 * @param {Parameters<typeof runDfu>[2] & {onConnected?: () => void}} hooks
 */
export async function flashNrf(dfu, { port = null, onConnected = () => {}, ...hooks } = {}) {
  port ??= await requestPort();
  await port.open({ baudRate: DFU_BAUDRATE });
  onConnected();
  const link = serialLink(port);
  try {
    await runDfu(link, dfu, hooks);
  } finally {
    await link.close();
  }
}
