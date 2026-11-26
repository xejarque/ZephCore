/**
 * "Respaldo del nodo" of the flasher: saves the backup of the node on the serial port and restores
 * one from a file. node-backup.js knows what is read and sent; this file drives the page.
 */
function initFlasherBackup() {
  const section = document.getElementById("backupSection");
  const saveButton = document.getElementById("btnBackupSave");
  const pickButton = document.getElementById("btnBackupPick");
  const fileInput = document.getElementById("backupFileInput");
  const restoreButton = document.getElementById("btnBackupRestore");
  const status = document.getElementById("backupStatus");
  if (!section) return;

  // A node takes a line without answering it ("region load" rows) well within this.
  const NO_REPLY_PAUSE_MS = 100;
  // A reply may wait for a flash write.
  const REPLY_TIMEOUT_MS = 5000;

  let loaded = null;
  let busy = false;

  const say = (text) => (status.textContent = text);

  async function answerTo(command, options) {
    const reply = await window.ZephtacoreSerial.sendCommandAndWait(command, { timeoutMs: REPLY_TIMEOUT_MS, ...options });
    const name = options.secret || command;
    if (!reply.sent) throw new Error(`no se pudo enviar "${name}"`);
    if (reply.timedOut) throw new Error(`el nodo no respondió a "${name}"`);
    return { reply: reply.text, refused: window.ZephtacoreLogic.isErrorReply(reply.text) };
  }

  const ask = (command, { secret } = {}) => answerTo(command, { secret: secret ? command : "" });

  async function send(step) {
    if (step.wait) return answerTo(step.cmd, step.secret ? { secret: step.shown } : { raw: true });
    if (!(await window.ZephtacoreSerial.writeLine(step.cmd, step.shown))) {
      throw new Error(`no se pudo enviar "${step.shown}"`);
    }
    await new Promise((resolve) => setTimeout(resolve, NO_REPLY_PAUSE_MS));
    return { reply: "", refused: false };
  }

  const askPassword = () =>
    window.prompt("El firmware de este nodo no permite leer la clave de administrador. Escríbela para guardarla en el respaldo (vacío: no se guarda).");

  // Runs task on the node and leaves the serial port as it was: flashing needs it closed. The port
  // is asked for first, because the browser only allows that right after the click.
  async function onNode(task) {
    const consoleSection = document.getElementById("consoleSection");
    if (consoleSection) consoleSection.open = true;
    const serial = window.ZephtacoreSerial;
    const wasConnected = serial.isConnected();
    if (!(await serial.connect())) return;
    try {
      await task();
    } finally {
      if (!wasConnected) await serial.disconnect();
    }
  }

  // One exchange with the node at a time: its replies are matched to commands by order.
  async function alone(task) {
    if (busy) return;
    busy = true;
    try {
      await task();
    } finally {
      busy = false;
    }
  }

  function fileName(backup) {
    const name = backup.settings.name.replace(/[^\w-]+/g, "_");
    return `zephtacore-respaldo-${name}-${backup.created.slice(0, 10)}.json`;
  }

  function download(name, text) {
    const link = document.createElement("a");
    link.href = URL.createObjectURL(new Blob([text], { type: "application/json" }));
    link.download = name;
    link.click();
  }

  async function save() {
    say("Leyendo el nodo, no lo desconectes...");
    try {
      const { backup, warnings } = await window.ZephtacoreBackup.collect(ask, askPassword);
      download(fileName(backup), JSON.stringify(backup, null, 2));
      say([`Respaldo guardado en "${fileName(backup)}".`, ...warnings].join(" "));
    } catch (err) {
      say(`No se ha guardado ningún respaldo: ${err.message}.`);
    }
  }

  async function pick(file) {
    loaded = null;
    restoreButton.style.display = "none";
    try {
      loaded = window.ZephtacoreBackup.parseBackup(await file.text());
    } catch (err) {
      say(`No se puede usar "${file.name}": ${err.message}.`);
      return;
    }
    restoreButton.style.display = "";
    say(`Cargado el respaldo de "${loaded.settings.name}" (${loaded.role}, ${loaded.created.slice(0, 10)}).`);
  }

  function confirmation(backup, nodeRole) {
    const lines = [
      `Se va a restaurar "${backup.settings.name}" en el nodo conectado.`,
      "Sustituye su identidad, sus claves y sus ajustes, y lo reinicia."
    ];
    if (nodeRole !== backup.role) lines.push(`Atención: el nodo es ${nodeRole} y el respaldo es de ${backup.role}.`);
    lines.push("No dejes encendidos a la vez dos nodos con la misma identidad.");
    return lines.join("\n\n");
  }

  function summary(refused) {
    if (!refused.length) return "Respaldo restaurado. El nodo se reinicia.";
    const list = refused.map((r) => (r.reply ? `${r.command} (${r.reply})` : r.command)).join("; ");
    const identity = refused.some((r) => r.command.startsWith("set prv.key")) ? "La identidad NO se ha restaurado. " : "";
    return `${identity}El nodo se reinicia. Rechazó: ${list}.`;
  }

  async function restore() {
    try {
      const nodeRole = (await ask("get role")).reply.replace(/^>\s*/, "");
      if (!["repeater", "room_server"].includes(nodeRole)) {
        say(`No se ha enviado nada: el nodo conectado es ${nodeRole}, y el respaldo es para repetidor o room server.`);
        return;
      }
      if (!window.confirm(confirmation(loaded, nodeRole))) return;
      say("Restaurando, no desconectes el nodo...");
      say(summary(await window.ZephtacoreBackup.restore(loaded, send)));
    } catch (err) {
      say(`Restauración interrumpida: ${err.message}. El nodo puede haber quedado a medias: vuelve a restaurar.`);
    }
  }

  saveButton.addEventListener("click", () => alone(() => onNode(save)));
  pickButton.addEventListener("click", () => fileInput.click());
  fileInput.addEventListener("change", () => {
    const [file] = fileInput.files;
    // Cleared so that choosing the same file again is a change.
    fileInput.value = "";
    if (file) return alone(() => pick(file));
    return undefined;
  });
  restoreButton.addEventListener("click", () => alone(() => onNode(restore)));

  // The Builder links here.
  if (window.location && window.location.hash === "#backupSection") section.open = true;
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", initFlasherBackup);
} else {
  initFlasherBackup();
}
