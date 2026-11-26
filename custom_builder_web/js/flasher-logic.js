/**
 * Flasher and builder logic that needs no DOM: command generation, catalog file
 * matching and quoting. Loaded by the pages and by node --test.
 */
(function (root) {
  const FIRMWARE_URL_PREFIX = /^https:\/\/((xejarque|liquidraver)\.github\.io\/ZephCore\/firmware|cdn\.jsdelivr\.net\/gh\/(xejarque|liquidraver)\/ZephCore@firmware-dist)\//;
  const FILE_SUFFIX = { uf2: "\\.uf2", hex: "\\.hex", bin: "-merged\\.bin", dfu: "\\.zip" };

  // Coding rate: the builder stores 1-4 (4/5-4/8), the firmware takes 5-8.
  function firmwareCodingRate(cr) {
    const n = parseInt(cr, 10);
    return n <= 4 ? n + 4 : n;
  }

  function buildConfigCommands(cfg, nowEpoch = Math.floor(Date.now() / 1000)) {
    const cr = firmwareCodingRate(cfg.cr);

    if (cfg.role === "observer") {
      return [
        `set name ${cfg.name}`,
        `set freq ${cfg.freq}`,
        `set sf ${cfg.sf}`,
        `set bw ${cfg.bw}`,
        `set cr ${cr}`,
        cfg.lat ? `set lat ${cfg.lat}` : null,
        cfg.lon ? `set lon ${cfg.lon}` : null,
        `reboot`
      ].filter(Boolean);
    }

    return [
      `set name ${cfg.name}`,
      // A companion has no admin login and refuses the command.
      cfg.adminPass && cfg.role !== "companion" ? `password ${cfg.adminPass}` : null,
      cfg.guestPass && cfg.role !== "companion" ? `set guest.password ${cfg.guestPass}` : null,
      `set radio ${cfg.freq},${cfg.bw},${cfg.sf},${cr}`,
      `set tx ${cfg.txPower}`,
      `set z.mt.enable on`,
      `set z.mt.relay ${cfg.mtRelay}`,
      `set z.mt.relay.opaque ${cfg.mtOpaque ? "on" : "off"}`,
      `set z.mt.hops ${cfg.mtHops}`,
      cfg.pingBot ? `set z.mt.ping.enable on /ping` : null,
      cfg.role === "repeater" && cfg.vroomEnable ? `set z.room.access ${cfg.vroomAccess || "open"}` : null,
      cfg.role === "repeater" && cfg.vroomEnable && cfg.vroomPass ? `set z.room.password ${cfg.vroomPass}` : null,
      cfg.role === "repeater" && cfg.vroomEnable ? `set z.room.on on` : null,
      cfg.lat ? `set lat ${cfg.lat}` : null,
      cfg.lon ? `set lon ${cfg.lon}` : null,
      cfg.autoShutdownMv ? `set z.autoshutdown ${cfg.autoShutdownMv}` : null,
      Number.isInteger(nowEpoch) && nowEpoch > 0 ? `time ${nowEpoch}` : null,
      // A board without GPS refuses it, which stops the send: everything else is applied by then.
      cfg.gps ? `gps ${cfg.gps}` : null,
      `reboot`
    ].filter(Boolean);
  }

  const SECRET_COMMAND = /^(password|set guest\.password|set z\.room\.password) /;

  // What may be shown of a command that carries a password; "" when it carries none.
  function maskedCommand(cmd) {
    const m = SECRET_COMMAND.exec(cmd);
    return m ? `${m[1]} ••••` : "";
  }

  function newestVersion(versions) {
    return Object.keys(versions || {}).sort((x, y) => x.localeCompare(y, undefined, { numeric: true })).pop();
  }

  // Every file of the newest version of every firmware option, whatever the device tile.
  function catalogFiles(catalog) {
    const out = [];
    for (const dev of catalog.device || []) {
      for (const fw of dev.firmware || []) {
        const ver = newestVersion(fw.version);
        if (ver) out.push(...(fw.version[ver].files || []).map((f) => ({ ...f, erase: fw.erase, erase_sha256: fw.erase_sha256, erase_uf2_sha256: fw.erase_uf2_sha256 })));
      }
    }
    return out;
  }

  // build.sh names files <target with / as ->>-<role>-<variant>-<git hash><suffix>.
  // The flasher injects z.* settings, so it only offers the zephtacore variant.
  // kind picks the file type: the board's own (uf2, hex, bin) or "dfu", the .zip of an nRF52 board.
  function findCatalogFile(files, board, role, kind = board.flashType) {
    const stem = board.id.replace(/\//g, "-").replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    const pat = new RegExp(`^${stem}-${role}-zephtacore-[0-9a-f]{7,40}${FILE_SUFFIX[kind] || FILE_SUFFIX.bin}$`);
    return files.find((f) => pat.test(f.name || "") && FIRMWARE_URL_PREFIX.test(f.url || "")) || null;
  }

  // The catalog's `erase` of a firmware option is the .zip of the formatter for the board's
  // SoftDevice; the .uf2 sits beside it (ext "zip" or "uf2"). Anything else is not trusted.
  const FORMATTER_ZIP = /^https:\/\/((xejarque|liquidraver)\.github\.io\/ZephCore\/firmware|cdn\.jsdelivr\.net\/gh\/(xejarque|liquidraver)\/ZephCore@firmware-dist)\/SoftDevice_v[67]_formatter\.zip$/;
  function formatterUrl(file, ext = "uf2") {
    const zip = file && file.erase;
    return FORMATTER_ZIP.test(zip || "") ? zip.replace(/\.zip$/, `.${ext}`) : null;
  }

  function kconfigString(value) {
    return `"${String(value).replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;
  }

  function shellQuote(value) {
    return `'${String(value).replace(/'/g, "'\\''")}'`;
  }

  // A node echoes the command and answers "  -> <reply>", ended by CR LF; the lines inside a reply
  // end with LF alone. The reply received so far, or null while none has arrived.
  function nodeReply(received) {
    const mark = /(?:^|\n)[ \t]*-> /.exec(received || "");
    if (!mark) return null;
    return received.slice(mark.index + mark[0].length).split("\r\n")[0].trim();
  }

  // A refused command. "(ERR: clock cannot go backwards)" is left out: the node's clock is already ahead.
  const REFUSED = /^(?:> )?(?:(?:error|err)\b|unknown (?:command|config)\b|\?\?:|gps toggle not found)/i;
  function isErrorReply(reply) {
    return REFUSED.test(reply || "");
  }

  // True when the catalog gives no sha256 for a file or its bytes have that SHA-256.
  async function sha256Matches(bytes, expected, subtle) {
    if (!expected) return true;
    if (!subtle) throw new Error("Este navegador no puede comprobar el SHA-256 del firmware (hace falta HTTPS).");
    const digest = new Uint8Array(await subtle.digest("SHA-256", bytes));
    const hex = Array.from(digest, (b) => b.toString(16).padStart(2, "0")).join("");
    return hex === String(expected).toLowerCase();
  }

  const api = { nodeReply, isErrorReply, maskedCommand, sha256Matches, firmwareCodingRate, buildConfigCommands, catalogFiles, findCatalogFile, formatterUrl, kconfigString, shellQuote };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.ZephtacoreLogic = api;
})(typeof window !== "undefined" ? window : globalThis);
