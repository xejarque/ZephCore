/**
 * Zephtacore Web Serial & Interactive Terminal Controller
 */
window.ZephtacoreSerial = (function () {
  let port = null;
  let reader = null;
  let writer = null;
  let readableStreamClosed = null;
  let writableStreamClosed = null;
  let isConnected = false;
  let rxListeners = [];
  let muted = false;
  let commandHistory = [];
  let historyIdx = -1;

  const terminalBody = () => document.getElementById("terminalBody");
  const terminalInput = () => document.getElementById("terminalInput");
  const connectBtn = () => document.getElementById("btnSerialConnect");
  const statusBadge = () => document.getElementById("serialStatusBadge");

  function logToTerminal(text, type = "info") {
    const el = terminalBody();
    if (!el) return;
    const span = document.createElement("span");
    span.className = `term-line term-${type}`;
    span.textContent = text;
    el.appendChild(span);
    el.scrollTop = el.scrollHeight;
  }

  // Opens the given port, or asks the person for one when none is given.
  async function connect(given = null) {
    if (isConnected && given && given !== port) await disconnect();
    if (isConnected) return true;
    if (!("serial" in navigator)) {
      alert("Tu navegador no soporta Web Serial API. Por favor usa Google Chrome, Microsoft Edge o Opera en escritorio.");
      return false;
    }

    try {
      port = given || (await navigator.serial.requestPort());
      await port.open({ baudRate: 115200 });

      isConnected = true;
      updateUIState(true);
      logToTerminal("\n[✓] Conectado exitosamente al puerto serie a 115200 baudios.\n", "success");
      logToTerminal("Escribe un comando o pulsa un botón de diagnóstico rápido abajo.\n", "dim");

      // Setup writer
      const textEncoder = new TextEncoderStream();
      writableStreamClosed = textEncoder.readable.pipeTo(port.writable);
      writer = textEncoder.writable.getWriter();

      // Setup reader
      readLoop();
      return true;
    } catch (err) {
      console.error("Serial connection error:", err);
      logToTerminal(`[!] Error al abrir puerto: ${err.message}\n`, "error");
      isConnected = false;
      port = null;
      updateUIState(false);
      return false;
    }
  }

  async function disconnect() {
    if (!port) return;
    try {
      if (reader) {
        await reader.cancel();
        await readableStreamClosed.catch(() => {});
        reader = null;
      }
      if (writer) {
        await writer.close();
        await writableStreamClosed;
        writer = null;
      }
      await port.close();
      port = null;
      isConnected = false;
      updateUIState(false);
      logToTerminal("\n[-] Puerto serie desconectado.\n", "info");
    } catch (err) {
      console.error("Disconnect error:", err);
    }
  }

  // The board was unplugged: drop the dead streams and show the port as closed.
  function handleLost() {
    reader = null;
    writer = null;
    port = null;
    isConnected = false;
    updateUIState(false);
    logToTerminal("\n[!] Puerto serie desconectado (cable retirado o placa reiniciada).\n", "error");
  }

  async function readLoop() {
    const textDecoder = new TextDecoderStream();
    readableStreamClosed = port.readable.pipeTo(textDecoder.writable);
    const streamReader = textDecoder.readable.getReader();
    reader = streamReader;

    try {
      while (true) {
        const { value, done } = await streamReader.read();
        if (done) break;
        if (value) {
          if (!muted) logToTerminal(value, "rx");
          rxListeners.forEach((fn) => fn(value));
        }
      }
    } catch (err) {
      if (isConnected) {
        logToTerminal(`[!] Error de lectura: ${err.message}\n`, "error");
      }
    } finally {
      streamReader.releaseLock();
    }
  }

  function isReady() {
    if (writer && isConnected) return true;
    logToTerminal("[!] No hay conexión serie activa. Pulsa 'Conectar Puerto Serie'.\n", "error");
    return false;
  }

  // Writes a line exactly as given; shown stands for it in the terminal.
  async function writeLine(line, shown = line) {
    if (!isReady()) return false;
    logToTerminal(`> ${shown}\n`, "tx");
    // The node collapses a CR LF pair: after one, only a lone LF reads as a blank line.
    await writer.write(line ? `${line}\r\n` : "\n");
    return true;
  }

  // A command typed or composed here: trimmed, never empty, kept in the history.
  async function sendCommand(cmd) {
    const cleanCmd = cmd.trim();
    if (!cleanCmd || !(await writeLine(cleanCmd))) return false;

    if (commandHistory[commandHistory.length - 1] !== cleanCmd) {
      commandHistory.push(cleanCmd);
    }
    historyIdx = commandHistory.length;
    return true;
  }

  // Sends a command and returns the node's reply, complete once it stays quiet for quietMs;
  // timedOut when no reply arrived in timeoutMs.
  // raw: the line goes as it is (it may start or end with spaces, or be empty).
  // secret: a line shown instead of it; nothing the node sends meanwhile reaches the terminal.
  async function sendCommandAndWait(cmd, { timeoutMs = 3000, quietMs = 300, raw = false, secret = "" } = {}) {
    let received = "";
    let quietTimer = null;
    let timeoutTimer = null;
    let finish;
    const done = new Promise((resolve) => (finish = resolve));

    const settle = () => {
      const reply = window.ZephtacoreLogic.nodeReply(received);
      finish({ text: reply || "", timedOut: reply === null });
    };
    // The echo of the command arrives before the reply, which may take a flash write.
    const listener = (chunk) => {
      received += chunk;
      clearTimeout(quietTimer);
      if (window.ZephtacoreLogic.nodeReply(received) !== null) quietTimer = setTimeout(settle, quietMs);
    };
    rxListeners.push(listener);
    timeoutTimer = setTimeout(settle, timeoutMs);
    if (secret) muted = true;

    try {
      const sent = secret ? writeLine(cmd, secret) : raw ? writeLine(cmd) : sendCommand(cmd);
      if (!(await sent)) return { text: "", timedOut: false, sent: false };
      return { ...(await done), sent: true };
    } finally {
      muted = false;
      clearTimeout(quietTimer);
      clearTimeout(timeoutTimer);
      rxListeners = rxListeners.filter((fn) => fn !== listener);
    }
  }

  function updateUIState(connected) {
    const btn = connectBtn();
    const badge = statusBadge();
    if (btn) {
      btn.textContent = connected ? "🔌 Desconectar" : "⚡ Conectar Puerto Serie";
      btn.className = connected ? "btn btn-outline btn-sm" : "btn btn-primary btn-sm";
    }
    if (badge) {
      badge.textContent = connected ? "ONLINE (115200)" : "OFFLINE";
      badge.style.color = connected ? "#10b981" : "#94a3b8";
    }
  }

  function setup() {
    if ("serial" in navigator) {
      navigator.serial.addEventListener("disconnect", (e) => {
        if (e.target === port) handleLost();
      });
    }

    const btn = connectBtn();
    if (btn) {
      btn.addEventListener("click", () => {
        if (isConnected) disconnect();
        else connect();
      });
    }

    const input = terminalInput();
    if (input) {
      input.addEventListener("keydown", (e) => {
        if (e.key === "Enter") {
          sendCommand(input.value);
          input.value = "";
        } else if (e.key === "ArrowUp") {
          if (historyIdx > 0) {
            historyIdx--;
            input.value = commandHistory[historyIdx] || "";
          }
        } else if (e.key === "ArrowDown") {
          if (historyIdx < commandHistory.length - 1) {
            historyIdx++;
            input.value = commandHistory[historyIdx] || "";
          } else {
            historyIdx = commandHistory.length;
            input.value = "";
          }
        }
      });
    }

    // Diagnostic Shortcuts
    document.querySelectorAll(".btn-command-shortcut").forEach((b) => {
      b.addEventListener("click", () => {
        const cmd = b.getAttribute("data-cmd");
        if (cmd) sendCommand(cmd);
      });
    });
  }

  return {
    setup,
    sendCommand,
    writeLine,
    sendCommandAndWait,
    log: logToTerminal,
    connect,
    disconnect,
    isConnected: () => isConnected
  };
})();

document.addEventListener("DOMContentLoaded", () => {
  window.ZephtacoreSerial.setup();
});
