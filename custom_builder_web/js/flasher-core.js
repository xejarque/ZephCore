/**
 * Zephtacore Web Flasher Core Engine - Direct USB Configuration Injection
 */
function initFlasherCore() {
  const {
    buildConfigCommands, catalogFiles, dfuProgress, findCatalogFile, formatterUrl, maskedCommand, nodeRole, refusalReason, sha256Matches,
    skippedSetting, withDisabled
  } = window.ZephtacoreLogic;
  const boardSelect = document.getElementById("flasherBoardSelect");
  const roleSelect = document.getElementById("flasherRoleSelect");
  const progressCard = document.getElementById("progressCard");
  const progressBar = document.getElementById("flashProgressBar");
  const progressPercent = document.getElementById("progressPercent");
  const flashStatusText = document.getElementById("flashStatusText");
  const consoleSection = document.getElementById("consoleSection");

  // Preview elements
  const boardPreviewBox = document.getElementById("boardPreviewBox");
  const previewBoardTitle = document.getElementById("previewBoardTitle");
  const previewBoardDesc = document.getElementById("previewBoardDesc");
  const previewMcuTag = document.getElementById("previewMcuTag");
  const previewRadioTag = document.getElementById("previewRadioTag");
  const previewFlashTag = document.getElementById("previewFlashTag");
  const previewBuildTag = document.getElementById("previewBuildTag");

  // Pending config banner
  const pendingConfigBanner = document.getElementById("pendingConfigBanner");
  const cfgBadgeName = document.getElementById("cfgBadgeName");
  const cfgBadgeFreq = document.getElementById("cfgBadgeFreq");
  const cfgBadgeRole = document.getElementById("cfgBadgeRole");
  const cfgBadgeHops = document.getElementById("cfgBadgeHops");
  const cfgBadgeBoard = document.getElementById("cfgBadgeBoard");
  const btnUnifiedFlash = document.getElementById("btnUnifiedFlash");
  const btnInjectOnly = document.getElementById("btnInjectOnly");
  const installModeBox = document.getElementById("installModeBox");
  const installKeep = document.getElementById("installKeep");
  const installClean = document.getElementById("installClean");
  const installCleanText = document.getElementById("installCleanText");
  const btnNextStep = document.getElementById("btnNextStep");

  // Settings waiting to be sent; none once the node accepted them.
  let pendingConfig = null;

  // build.sh publishes only these two roles; the others are built with west.
  const PUBLISHED_ROLES = ["companion", "repeater"];

  // No board is preselected: nothing tells whether an nRF52 firmware is the plugged board's. files,
  // once the catalog loads, marks the boards it has no build for.
  function offerBoards(files = null) {
    if (!boardSelect || !window.ZEPHTACORE_BOARDS) return;
    const chosen = boardSelect.value;
    const built = (b) => !files || PUBLISHED_ROLES.some((role) => findCatalogFile(files, b, role));
    const options = window.ZEPHTACORE_BOARDS.map(
      (b) => `<option value="${b.id}">${b.name} — [${b.mcu}]${built(b) ? "" : " · sin build"}</option>`
    );
    boardSelect.innerHTML = ['<option value="" disabled selected>Elige tu placa…</option>', ...options].join("");
    boardSelect.value = chosen;
  }
  offerBoards();

  // The role the Builder chose is offered too, so that it is not swapped for a published one.
  function offerRoles(chosen) {
    if (!roleSelect || !window.ZEPHTACORE_ROLES) return;
    roleSelect.innerHTML = window.ZEPHTACORE_ROLES.filter((r) => PUBLISHED_ROLES.includes(r.id) || r.id === chosen).map(
      (r) => `<option value="${r.id}">${r.name}</option>`
    ).join("");
  }

  // Settings come from the Builder (same tab, sessionStorage) and are dropped once sent.
  const PENDING_KEY = "zephtacore_pending_config";
  const savedCfgStr = sessionStorage.getItem(PENDING_KEY);

  function showPendingConfig() {
    if (!pendingConfig || !pendingConfigBanner) return;
    pendingConfigBanner.style.display = "";
    if (cfgBadgeName) cfgBadgeName.textContent = `Nodo: ${pendingConfig.name}`;
    if (cfgBadgeFreq) cfgBadgeFreq.textContent = `${pendingConfig.freq} MHz (SF${pendingConfig.sf || 7}/BW${pendingConfig.bw || 62.5})`;
    if (cfgBadgeRole) cfgBadgeRole.textContent = `Rol: ${pendingConfig.role || "companion"}`;
    if (cfgBadgeHops) cfgBadgeHops.textContent = `Saltos: ${pendingConfig.mtHops || 7}`;
    if (cfgBadgeBoard) {
      const b = window.ZEPHTACORE_BOARDS && window.ZEPHTACORE_BOARDS.find((x) => x.id === pendingConfig.boardId);
      cfgBadgeBoard.textContent = `Placa: ${b ? b.name : (pendingConfig.boardName || pendingConfig.boardId || "Auto")}`;
    }
  }

  if (savedCfgStr) {
    try {
      pendingConfig = JSON.parse(savedCfgStr);
      showPendingConfig();
    } catch (e) {
      console.warn("Error parsing pending config:", e);
      sessionStorage.removeItem(PENDING_KEY);
    }
  }

  // Preselect what the Builder chose; the selects stay the source of truth.
  offerRoles(pendingConfig && pendingConfig.role);
  if (boardSelect && pendingConfig && pendingConfig.boardId) boardSelect.value = pendingConfig.boardId;
  if (roleSelect && pendingConfig && pendingConfig.role) roleSelect.value = pendingConfig.role;

  function syncBoardPreview() {
    const board = selectedBoard();
    if (boardPreviewBox) boardPreviewBox.style.display = board ? "" : "none";
    if (!board) return;

    if (previewBoardTitle) previewBoardTitle.textContent = board.name;
    if (previewBoardDesc) previewBoardDesc.textContent = board.desc;
    if (previewMcuTag) previewMcuTag.textContent = board.mcu;
    if (previewRadioTag) previewRadioTag.textContent = board.radio;
    if (previewFlashTag) {
      if (board.flashType === "uf2") {
        previewFlashTag.textContent = "serial" in navigator ? "DFU por USB (Adafruit bootloader)" : "Modo UF2 (Arrastrar a Disco)";
        previewFlashTag.style.background = "rgba(139, 92, 246, 0.1)";
        previewFlashTag.style.color = "var(--brand-purple)";
        previewFlashTag.style.borderColor = "rgba(139, 92, 246, 0.3)";
      } else if (board.flashType === "hex") {
        previewFlashTag.textContent = "Modo SWD / JTAG (zephyr.hex)";
        previewFlashTag.style.background = "rgba(245, 158, 11, 0.1)";
        previewFlashTag.style.color = "#f59e0b";
        previewFlashTag.style.borderColor = "rgba(245, 158, 11, 0.3)";
      } else {
        previewFlashTag.textContent = "Grabado por USB (esptool-js, 0x0)";
        previewFlashTag.style.background = "rgba(0, 242, 254, 0.1)";
        previewFlashTag.style.color = "var(--brand-cyan)";
        previewFlashTag.style.borderColor = "rgba(0, 242, 254, 0.3)";
      }
    }
  }

  if (boardSelect) {
    boardSelect.addEventListener("change", () => {
      syncBoardPreview();
      refreshBuildAvailability();
    });
    syncBoardPreview();
  }
  if (roleSelect) roleSelect.addEventListener("change", refreshBuildAvailability);

  // A node that was just flashed answers once it has booted; the first start after a format takes longest.
  const NODE_READY_TIMEOUT_MS = 30000;
  // How long a board rebooted into its bootloader may take to show up before the person is asked for it.
  const DFU_PORT_TIMEOUT_MS = 3000;
  // How long a flashed board may take to show up as a serial port before the person is asked to send.
  const NODE_PORT_TIMEOUT_MS = 15000;
  // A board behind a USB-UART bridge keeps its port across the reset; one with native USB comes back
  // as a new one, which is waited for this long first.
  const SAME_PORT_GRACE_MS = 3000;

  // Sends the settings over the serial port and stops at the first command the node
  // refuses, does not answer or cannot be sent. Returns true when all were accepted.
  function fail(cmd, reason) {
    const message = `Envío interrumpido en "${cmd}": ${reason}.`;
    if (flashStatusText) flashStatusText.textContent = `${message} Revisa el terminal.`;
    alert(`${message}\n\nLos ajustes siguen pendientes: corrige la causa y vuelve a enviarlos.`);
    return false;
  }

  // The observer has no "get role"; every other role must be the one the settings are for.
  async function nodeAcceptsRole(serial, role) {
    if (role === "observer") return true;
    const reply = await serial.sendCommandAndWait("get role", { timeoutMs: NODE_READY_TIMEOUT_MS });
    if (!reply.sent) return fail("get role", "no se pudo enviar");
    if (reply.timedOut) return fail("get role", "el nodo no respondió");
    const found = nodeRole(reply.text);
    return found === role || fail("get role", `el nodo conectado es "${found || reply.text}" y los ajustes son para "${role}"`);
  }

  function adminPasswordSummary(cfg) {
    if (cfg.role === "companion" || cfg.role === "observer") return "";
    return cfg.adminPass
      ? "Clave de administrador: la de tus ajustes, confirmada por el nodo.\n"
      : "Clave de administrador: no has puesto ninguna, sigue la que tenía el nodo.\n";
  }

  async function injectConfigSequence(cfg) {
    if (!window.ZephtacoreSerial.isConnected()) {
      if (flashStatusText) flashStatusText.textContent = "Sin conexión serie: no se ha enviado ningún ajuste.";
      return false;
    }

    if (flashStatusText) flashStatusText.textContent = "Esperando a que el nodo responda...";

    const serial = window.ZephtacoreSerial;
    if (!(await nodeAcceptsRole(serial, cfg.role))) return false;

    if (flashStatusText) flashStatusText.textContent = "Enviando configuración por puerto serie...";
    const skipped = [];
    for (const cmd of buildConfigCommands(cfg)) {
      // reboot drops the port as it answers, so it is sent without waiting.
      if (cmd === "reboot") {
        if (!(await serial.sendCommand(cmd))) return fail(cmd, "no se pudo enviar");
        continue;
      }
      const secret = maskedCommand(cmd);
      const shown = secret || cmd;
      const reply = await serial.sendCommandAndWait(cmd, { secret });
      if (!reply.sent) return fail(shown, "no se pudo enviar");
      if (reply.timedOut) return fail(shown, "el nodo no respondió");
      const notApplied = skippedSetting(cmd, reply.text);
      if (notApplied) {
        skipped.push(`${notApplied}\n`);
        continue;
      }
      const reason = refusalReason(cmd, reply.text);
      if (reason) return fail(shown, reason);
    }

    forgetPendingConfig();
    alert(
      `Configuración enviada.\n\n` +
      `Nombre: ${cfg.name}\n` +
      `Frecuencia LoRa: ${cfg.freq} MHz (SF${cfg.sf} / BW${cfg.bw})\n` +
      adminPasswordSummary(cfg) +
      skipped.join("") +
      `\nEl nodo aceptó ${skipped.length ? "los demás" : "todos los"} comandos y se reinicia.`
    );
    return true;
  }

  function forgetPendingConfig() {
    sessionStorage.removeItem(PENDING_KEY);
    pendingConfig = null;
    if (pendingConfigBanner) pendingConfigBanner.style.display = "none";
  }

  async function sendSettings(cfg, port = null) {
    if (progressCard) progressCard.style.display = "block";
    if (consoleSection) consoleSection.open = true;
    if (!(await window.ZephtacoreSerial.connect(port))) {
      setProgress(100, `No se pudo abrir el puerto de la placa.${SEND_SETTINGS_HINT()}`);
      return false;
    }
    if (!(await injectConfigSequence({ ...cfg, role: selectedRole() }))) return false;
    setProgress(100, "Ajustes enviados. El nodo se reinicia.");
    return true;
  }

  // Listens for a port that appears from now on, which the browser reports only for a port it already
  // has permission for. Start it before the board reboots.
  function watchForPort() {
    let found;
    const connected = new Promise((resolve) => (found = resolve));
    const listener = (event) => found(event.target);
    navigator.serial.addEventListener("connect", listener);
    const stop = () => navigator.serial.removeEventListener("connect", listener);
    return {
      stop,
      async port(timeoutMs) {
        let timer = null;
        const expired = new Promise((resolve) => (timer = setTimeout(resolve, timeoutMs, null)));
        try {
          return await Promise.race([connected, expired]);
        } finally {
          clearTimeout(timer);
          stop();
        }
      }
    };
  }

  // The flashed board's port is only awaited when there are settings to send to it.
  const watchForNode = () => (pendingConfig && "serial" in navigator ? watchForPort() : null);

  // Ends an install: sends the pending settings to the board's port, or says how to send them.
  async function finishInstall(done, watch, samePort = null) {
    setProgress(100, `${done}${watch ? " Buscando el puerto de la placa para enviar los ajustes..." : SEND_SETTINGS_HINT()}`);
    if (!watch) return;
    try {
      const port = (await watch.port(samePort ? SAME_PORT_GRACE_MS : NODE_PORT_TIMEOUT_MS)) || samePort;
      if (port) await sendSettings(pendingConfig, port);
      else setProgress(100, `${done}${SEND_SETTINGS_HINT()}`);
    } catch (err) {
      console.error("Automatic settings send failed:", err);
      setProgress(100, `${done} No se pudieron enviar los ajustes: ${err.message}.${SEND_SETTINGS_HINT()}`);
    }
  }

  // One install or send at a time: they share the serial port and the progress bar.
  const BUSY_CONTROLS = [
    "btnUnifiedFlash", "btnInjectOnly", "btnNextStep", "flasherBoardSelect", "flasherRoleSelect",
    "installKeep", "installClean", "btnSerialConnect", "btnBackupSave", "btnBackupPick", "btnBackupRestore"
  ];
  let busy = false;

  function warnBeforeLeaving(event) {
    event.preventDefault();
    event.returnValue = "";
  }

  async function exclusively(task) {
    if (busy) return;
    busy = true;
    window.addEventListener("beforeunload", warnBeforeLeaving);
    try {
      await withDisabled(BUSY_CONTROLS.map((id) => document.getElementById(id)), task);
    } finally {
      window.removeEventListener("beforeunload", warnBeforeLeaving);
      busy = false;
    }
  }

  // Button to inject / start flash
  if (btnUnifiedFlash) {
    btnUnifiedFlash.addEventListener("click", () => exclusively(runUnifiedFlash));
  }
  if (btnInjectOnly) {
    btnInjectOnly.addEventListener("click", () => exclusively(runInjectOnly));
  }
  if (btnNextStep) btnNextStep.addEventListener("click", () => exclusively(runNextStep));

  const CATALOG_URLS = [
    "firmware/catalog.json",
    "https://cdn.jsdelivr.net/gh/liquidraver/ZephCore@firmware-dist/catalog.json"
  ];
  // Files of the catalogs that load, fetched once per page.
  let catalogFilesPromise = null;
  function loadCatalogFiles() {
    if (!catalogFilesPromise) {
      catalogFilesPromise = Promise.all(
        CATALOG_URLS.map(async (catUrl) => {
          try {
            const resp = await fetch(`${catUrl}?_t=${Date.now()}`);
            return resp.ok ? catalogFiles(await resp.json()) : null;
          } catch (err) {
            console.warn("Catalog fetch error:", catUrl, err);
            return null;
          }
        })
      ).then((lists) => (lists.some(Boolean) ? lists.flat().filter(Boolean) : null));
    }
    return catalogFilesPromise;
  }

  async function findBuild(board, role, kind) {
    if (!PUBLISHED_ROLES.includes(role)) {
      throw new Error(`El rol "${role}" no se publica como binario: compílalo con west desde el Builder.`);
    }
    const files = await loadCatalogFiles();
    return files ? findCatalogFile(files, board, role, kind) : null;
  }

  // The UF2 formatter the catalog names for the board's SoftDevice, or null.
  async function resolveFormatterUrl(board, role) {
    return formatterUrl(await findBuild(board, role));
  }

  function askForBoard() {
    if (installModeBox) installModeBox.style.display = "none";
    if (!btnUnifiedFlash) return;
    btnUnifiedFlash.disabled = true;
    btnUnifiedFlash.textContent = "Elige tu placa para grabar";
  }

  // Downloads the DFU package ahead, so that flashing starts as soon as the port is chosen.
  async function prefetchDfu(board, role) {
    if (board.flashType !== "uf2" || !usesWebSerialFlash(board)) return;
    try {
      const file = await findBuild(board, role, "dfu");
      if (file) await fetchBytes(file.url, file.sha256);
    } catch (err) {
      console.warn("DFU prefetch failed:", err);
    }
  }

  // Tells whether the catalog has a Zephtacore build for the selected board and role.
  async function refreshBuildAvailability() {
    if (!previewBuildTag) return;
    const board = selectedBoard();
    const role = selectedRole();
    if (!board) {
      askForBoard();
      return;
    }

    previewBuildTag.textContent = "Consultando catálogo...";
    const files = await loadCatalogFiles();
    if (board !== selectedBoard() || role !== selectedRole()) return;

    const available = !!files && PUBLISHED_ROLES.includes(role) && !!findCatalogFile(files, board, role);
    previewBuildTag.textContent = !files
      ? "Catálogo no disponible"
      : available ? "Build Zephtacore publicado" : "Sin build Zephtacore: compílalo con west";
    previewBuildTag.style.color = available ? "var(--brand-emerald)" : "var(--text-muted)";
    if (btnUnifiedFlash) {
      btnUnifiedFlash.disabled = !available;
      btnUnifiedFlash.textContent = usesWebSerialFlash(board) ? "⚡ Grabar firmware por USB" : "⬇️ Descargar firmware";
    }
    if (available) prefetchDfu(board, role);
    await refreshInstallChoice();
  }

  function setProgress(percent, text) {
    if (flashStatusText && text) flashStatusText.textContent = text;
    if (progressBar) progressBar.style.width = `${percent}%`;
    if (progressPercent) progressPercent.textContent = `${percent}%`;
  }

  // Fetches the catalog binary for the board and role. Throws when there is none.
  async function fetchFirmware(board, role) {
    const file = await findBuild(board, role);
    if (!file) throw new Error(`No hay build Zephtacore publicado para ${board.name} (${role}). Compílalo con west desde el Builder.`);

    return { name: file.url.split("/").pop(), bytes: await fetchBytes(file.url, file.sha256) };
  }

  // Files downloaded or on their way, each fetched once per page; a failed one is tried again.
  const downloads = new Map();
  function fetchBytes(url, sha256) {
    const key = `${url} ${sha256}`;
    if (!downloads.has(key)) {
      const bytes = download(url, sha256);
      downloads.set(key, bytes);
      bytes.catch(() => downloads.delete(key));
    }
    return downloads.get(key);
  }

  // Downloads a file; with the catalog's sha256 for it, refuses bytes that do not match.
  async function download(url, sha256) {
    const name = url.split("/").pop();
    const resp = await fetch(url);
    if (!resp.ok) throw new Error(`La descarga de ${name} falló (HTTP ${resp.status}).`);
    const bytes = new Uint8Array(await resp.arrayBuffer());
    if (!(await sha256Matches(bytes, sha256, globalThis.crypto && globalThis.crypto.subtle))) {
      throw new Error(`${name} no coincide con el SHA-256 del catálogo (descarga dañada o alterada). No se ha escrito nada.`);
    }
    return bytes;
  }

  function saveBytes(name, bytes) {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
    a.download = name;
    a.click();
  }

  async function downloadFirmware(board, role) {
    const { name, bytes } = await fetchFirmware(board, role);
    saveBytes(name, bytes);
    return name;
  }

  // ESP32: writes the merged image at 0x0 with esptool-js, erasing the whole flash first when
  // clean. The chip must answer and be the board's own family before anything is written.
  async function flashEsp32(board, role, clean) {
    if (!("serial" in navigator)) {
      alert("Web Serial no está soportado en este navegador. Por favor usa Google Chrome o Microsoft Edge en tu ordenador.");
      return;
    }
    const serial = window.ZephtacoreSerial;
    let transport = null;
    let watch = null;
    let flashedPort = null;
    try {
      // requestPort needs a recent user gesture: ask for it before the download.
      setProgress(5, "Elige el puerto de la placa (si no aparece, mantén BOOT y pulsa RESET).");
      const port = await navigator.serial.requestPort();

      setProgress(10, `Descargando el firmware de ${board.name}...`);
      const { name, bytes } = await fetchFirmware(board, role);
      const { ESPLoader, Transport } = window.ZephtacoreEsptool || (await import("../vendor/esptool-js.bundle.js"));
      transport = new Transport(port, false);
      const terminal = {
        clean() {},
        write: (text) => serial.log(text, "info"),
        writeLine: (text) => serial.log(`${text}\n`, "info")
      };
      const loader = new ESPLoader({ transport, baudrate: 460800, romBaudrate: 115200, terminal });

      setProgress(15, "Conectando con el chip...");
      const chipDescription = await loader.main();
      if (loader.chip.CHIP_NAME.toUpperCase() !== board.mcu.toUpperCase()) {
        throw new Error(`El chip detectado (${chipDescription}) no es un ${board.mcu}. No se ha escrito nada.`);
      }

      setProgress(20, clean ? `Borrando toda la flash y grabando ${name} en 0x0 (puede tardar un minuto)...` : `Grabando ${name} en 0x0...`);
      await loader.writeFlash({
        fileArray: [{ data: bytes, address: 0 }],
        flashSize: "keep",
        flashMode: "keep",
        flashFreq: "keep",
        eraseAll: clean,
        compress: true,
        reportProgress: (_file, written, total) => setProgress(20 + Math.round((written / total) * 75), `Grabando ${name}...`)
      });

      watch = watchForNode();
      await loader.after("hard_reset");
      flashedPort = port;
    } catch (err) {
      console.error("ESP32 flash error:", err);
      setProgress(0, `Error: ${err.message}`);
    } finally {
      if (transport) await transport.disconnect().catch(() => {});
    }
    if (flashedPort) await finishInstall("Firmware grabado y placa reiniciada.", watch, flashedPort);
    else watch?.stop();
  }

  function afterDownloadHint(board, fileName) {
    const next = SEND_SETTINGS_HINT();
    if (board.flashType === "uf2") return `Descargado "${fileName}". Cópialo al disco UF2 de la placa.${next}`;
    if (board.flashType === "hex") return `Descargado "${fileName}". Prográmalo con west flash o una sonda SWD.${next}`;
    return `Descargado "${fileName}".${next}`;
  }

  // The Builder left settings to send once the new firmware runs.
  const SEND_SETTINGS_HINT = () =>
    pendingConfig ? ' Cuando la placa arranque, pulsa "Enviar ajustes por USB".' : "";

  function selectedBoard() {
    const boards = window.ZEPHTACORE_BOARDS || [];
    return boards.find((b) => b.id === (boardSelect && boardSelect.value));
  }

  function selectedRole() {
    return (roleSelect && roleSelect.value) || "companion";
  }

  const CLEAN_LOSS = "Se pierden la identidad del nodo (clave nueva: tus contactos lo verán como otro nodo), los ajustes, los contactos y los emparejamientos Bluetooth.";
  const CLEAN_TEXT = {
    bin: `Borra TODA la flash de la placa antes de grabar el firmware, por si el sistema de archivos quedó corrupto. ${CLEAN_LOSS}`,
    dfu: `Primero se carga en la placa el formateador oficial de su SoftDevice, que borra el sistema de archivos, y después se instala el firmware, todo por USB. El navegador puede pedirte elegir el puerto. ${CLEAN_LOSS}`,
    uf2: `Primero se descarga un formateador que borra el sistema de archivos, y después el firmware (dos copias al disco UF2). ${CLEAN_LOSS}`
  };

  // ESP32 and nRF52 are written from the browser; nRF52 needs Web Serial for the DFU.
  function usesWebSerialFlash(board) {
    return board.flashType === "bin" || (board.flashType === "uf2" && "serial" in navigator);
  }

  // Whether the page can clean the board: the ESP32 flash is erased from here, and a UF2 board needs
  // the SoftDevice formatter of the catalog.
  async function canCleanInstall(board, role) {
    if (!PUBLISHED_ROLES.includes(role)) return false;
    return board.flashType === "bin" || (board.flashType === "uf2" && !!(await resolveFormatterUrl(board, role)));
  }

  // Shows the keep / clean choice only for a board that can be cleaned.
  async function refreshInstallChoice() {
    const board = selectedBoard();
    const role = selectedRole();
    if (!board || !installModeBox) return;
    const cleanable = await canCleanInstall(board, role);
    if (board !== selectedBoard() || role !== selectedRole()) return;
    installModeBox.style.display = cleanable ? "" : "none";
    if (!cleanable) installKeep.checked = true;
    else installCleanText.textContent = CLEAN_TEXT[board.flashType === "uf2" && usesWebSerialFlash(board) ? "dfu" : board.flashType];
  }

  async function selectedInstallMode(board, role) {
    return installClean && installClean.checked && (await canCleanInstall(board, role)) ? "clean" : "keep";
  }

  // Steps that each need their own click: the browser only lets a page open a serial port
  // from a user gesture, and the board changes USB device between steps.
  let nextStep = null;
  function offerNextStep(label, run) {
    nextStep = run;
    btnNextStep.textContent = label;
    btnNextStep.style.display = "";
  }

  function runNextStep() {
    if (!nextStep) return undefined;
    const run = nextStep;
    nextStep = null;
    btnNextStep.style.display = "none";
    return run();
  }

  function offerSteps(steps, index = 0) {
    offerNextStep(steps[index].button, async () => {
      try {
        await steps[index].run();
      } catch (err) {
        console.error("nRF52 DFU error:", err);
        setProgress(0, `Error: ${err.message}`);
        offerSteps(steps, index);
        return;
      }
      if (index + 1 < steps.length) offerSteps(steps, index + 1);
    });
  }

  const DFU_STAGE_TEXT = {
    start: "iniciando",
    erase: "borrando la memoria de la placa",
    init: "enviando el paquete de inicialización",
    write: "escribiendo",
    activate: "activando la imagen, no desconectes la placa"
  };

  const isSameDevice = (a, b) => {
    const first = a.getInfo();
    const second = b.getInfo();
    return first.usbVendorId === second.usbVendorId && first.usbProductId === second.usbProductId;
  };

  // Reboots the running board into its bootloader. Returns the bootloader's port when the browser
  // already has permission for it, null when the person has to choose it.
  async function rebootToBootloader(nrf) {
    const watch = watchForPort();
    try {
      const running = await nrf.touchBootloader();
      const port = await watch.port(DFU_PORT_TIMEOUT_MS);
      // The running application coming back is not the bootloader.
      return port && !isSameDevice(port, running) ? nrf.trackPort(port) : null;
    } catch (err) {
      console.warn("Bootloader touch skipped:", err);
      return null;
    } finally {
      watch.stop();
    }
  }

  // nRF52: the Adafruit bootloader's serial DFU, with the .zip of the catalog. A clean install loads the
  // SoftDevice's formatter first, which erases the file system and returns the board to the bootloader.
  async function flashNrf52(board, role, clean) {
    const nrf = window.ZephtacoreNrf || (await import("./flasher-nrf.mjs"));
    setProgress(5, "Elige el puerto de la placa en marcha para reiniciarla en modo bootloader (si ya está en modo DFU, cancela y sigue).");
    let dfuPort = await rebootToBootloader(nrf);

    setProgress(10, "Descargando los paquetes DFU...");
    const file = await findBuild(board, role, "dfu");
    if (!file) throw new Error(`No hay paquete DFU publicado para ${board.name} (${role}).`);
    const firmware = nrf.openDfuZip(await fetchBytes(file.url, file.sha256));
    let formatter = null;
    if (clean) {
      const url = formatterUrl(file, "zip");
      if (!url) throw new Error(`El catálogo no tiene formateador para ${board.name}.`);
      formatter = nrf.openDfuZip(await fetchBytes(url, file.erase_sha256));
    }

    const flash = async (pkg, label, from, to) => {
      dfuPort ??= await nrf.requestPort();
      const report = (stage, fraction) =>
        setProgress(from + Math.round(dfuProgress(stage, fraction) * (to - from)), `${label}: ${DFU_STAGE_TEXT[stage] || stage}...`);
      await nrf.flashNrf(pkg, {
        port: dfuPort,
        onConnected: () => setProgress(from, `${label}: conectado a la placa.`),
        onStage: (stage) => report(stage, 0),
        onProgress: report
      });
    };

    // One port choice covers the whole install: the formatter returns the board to the same bootloader
    // device, whose port the browser already allows.
    const install = async () => {
      if (formatter) {
        await flash(formatter, "Formateando", 15, 50);
        setProgress(50, "Formateador cargado: la placa borra sus datos y vuelve sola a modo DFU (con memoria externa puede tardar hasta un minuto)...");
        dfuPort = await nrf.waitForPort(dfuPort, 120000);
      }
      const watch = watchForNode();
      try {
        await flash(firmware, "Grabando el firmware", clean ? 55 : 15, 95);
      } catch (err) {
        watch?.stop();
        throw err;
      }
      await finishInstall("Firmware instalado.", watch);
    };

    if (!dfuPort) {
      setProgress(14, "Elige el puerto de la placa en modo DFU: es otro dispositivo USB y el navegador pide permiso aparte.");
    }
    try {
      await install();
    } catch (err) {
      console.warn("nRF52 DFU without a click:", err);
      dfuPort = null;
      const why = err.name === "NotFoundError" || err.name === "SecurityError" ? "" : `Error: ${err.message} `;
      setProgress(14, `${why}Pulsa el botón y elige el puerto de la placa en modo DFU (si no lo está, pulsa dos veces RESET).`);
      offerSteps([{ button: "Elegir el puerto en modo DFU e instalar", run: install }]);
    }
  }

  // nRF52 without Web Serial, clean install, step 1: the formatter UF2 erases the file system
  // and puts the board back in UF2 mode; the firmware is downloaded once the user says it is back.
  async function startCleanUf2(board, role) {
    setProgress(20, `Descargando el formateador para ${board.name}...`);
    const file = await findBuild(board, role);
    const url = formatterUrl(file, "uf2");
    if (!url) throw new Error(`El catálogo no tiene formateador para ${board.name}.`);
    const name = url.split("/").pop();
    saveBytes(name, await fetchBytes(url, file.erase_uf2_sha256));
    setProgress(40, `Descargado "${name}". Pon la placa en modo UF2 (doble pulsación de reset) y cópialo al disco: se formatea y vuelve sola a modo UF2. Cuando el disco reaparezca, pulsa "Ya formateada: descargar firmware".`);
    offerNextStep("Ya formateada: descargar firmware", downloadAfterFormat);
  }

  async function downloadAfterFormat() {
    const board = selectedBoard();
    if (!board) return;
    try {
      const fileName = await downloadFirmware(board, selectedRole());
      setProgress(60, afterDownloadHint(board, fileName));
    } catch (err) {
      console.error("Firmware download error:", err);
      setProgress(0, `Error: ${err.message}`);
    }
  }

  // Step 1: write or download the firmware for the board and role.
  async function runUnifiedFlash() {
    const board = selectedBoard();
    if (!board) return;
    const targetRole = selectedRole();

    const mode = await selectedInstallMode(board, targetRole);

    if (progressCard) {
      progressCard.style.display = "block";
      progressCard.scrollIntoView({ behavior: "smooth" });
    }
    if (btnNextStep) btnNextStep.style.display = "none";
    nextStep = null;
    // An open console holds the port the flasher needs.
    if (usesWebSerialFlash(board)) await window.ZephtacoreSerial.disconnect();

    if (board.flashType === "bin") {
      await flashEsp32(board, targetRole, mode === "clean");
      return;
    }

    try {
      if (usesWebSerialFlash(board)) {
        await flashNrf52(board, targetRole, mode === "clean");
        return;
      }
      if (mode === "clean") {
        await startCleanUf2(board, targetRole);
        return;
      }
      setProgress(20, `Consultando catálogo de firmware para ${board.name}...`);
      const fileName = await downloadFirmware(board, targetRole);
      setProgress(60, afterDownloadHint(board, fileName));
    } catch (err) {
      console.error("Firmware download error:", err);
      setProgress(0, `Error: ${err.message}`);
    }
  }

  // Step 2: the board runs the new firmware, send the settings.
  async function runInjectOnly() {
    if (!pendingConfig) return;
    if (!("serial" in navigator)) {
      alert("Web Serial no está soportado en este navegador. Por favor usa Google Chrome o Microsoft Edge en tu ordenador.");
      return;
    }

    await sendSettings(pendingConfig);
  }

  loadCatalogFiles().then(offerBoards);
  refreshBuildAvailability();
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", initFlasherCore);
} else {
  initFlasherCore();
}
