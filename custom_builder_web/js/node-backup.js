/**
 * Backup of a repeater or room server: read from the node over USB with its own CLI commands,
 * and the commands that put it back. No DOM: loaded by flasher.html and by node --test.
 */
(function (root) {
  const FORMAT = "zephtacore-node-backup";
  const VERSION = 1;
  const ROLES = ["repeater", "room_server"];
  const WARNING = "Contiene la clave privada y las contraseñas del nodo: quien tenga este fichero puede suplantarlo y administrarlo.";
  const PRIVATE_KEY = /^[0-9a-f]{128}$/i;

  // "> value" as the node writes a setting.
  const value = (reply) => (reply.startsWith(">") ? reply.slice(1).replace(/^ /, "") : null);
  const word = (pattern) => (reply) => (pattern.exec(reply) || [])[1] || null;

  // A setting: how to ask for it, what its reply means (null: the node does not have it) and the
  // commands that set it again.
  function plain(key, set = `set ${key}`) {
    return { key, get: `get ${key}`, parse: value, set: (v) => [`${set} ${v}`] };
  }

  // A gain the node stores even when it answers that its radio has none.
  const gain = (key) => ({ ...plain(key), keptIfUnsupported: true });

  // A value read with a unit or a note around it.
  const read = (key, pattern) => ({ ...plain(key), parse: word(pattern) });

  function firewall(protocol) {
    const key = `z.${protocol}.fw.drop`;
    return {
      key,
      get: `get ${key}`,
      // "> none", or each dropped rule with its count.
      parse: (reply) => (value(reply) === null ? null : value(reply).split(" ").filter((w) => w !== "none" && !/^\d*$/.test(w)).join(" ")),
      set: (v) => [`set ${key} none`, ...v.split(" ").filter(Boolean).map((rule) => `set ${key} ${rule} on`)]
    };
  }

  const ZERO_HOP = {
    key: "z.mt.zerohop",
    get: "get z.mt.zerohop",
    parse: value, // "off", or "auto !a1b2c3d4 ..."
    set: (v) => {
      const [mode, ...nodes] = v.split(" ");
      return [`set z.mt.zerohop ${mode}`, "set z.mt.zerohop.clear", ...nodes.map((n) => `set z.mt.zerohop.add ${n}`)];
    }
  };

  // The node names the primary channel but never prints a key of its own.
  const CHANNEL = {
    key: "z.mt.channel",
    get: "get z.mt.channel",
    parse: (reply) => (/\((default|custom) key\)$/.test(reply) ? value(reply) : null),
    set: (v) => {
      const m = /^\s*(.*?)\s*\((default|custom) key\)$/.exec(v);
      return m && m[1] && m[2] === "default" ? [`set z.mt.channel ${m[1]} default`] : [];
    }
  };

  const ROOM_PASSWORD = {
    key: "z.room.password",
    get: "get z.room.password",
    secret: true,
    parse: (reply) => (value(reply) === "(none)" ? "" : value(reply)),
    set: (v) => [`set z.room.password ${v || "clear"}`]
  };

  // In the order they are set again: the radio before its CAD values, which a new radio resets, a ping
  // bot's command before it is switched on, the room's access and password before the room.
  // What belongs to the board rather than to the node (battery calibration, LEDs, buzzer, rotations,
  // time zone) is not saved.
  const SETTINGS = [
    plain("name"),
    plain("owner.info"),
    { ...plain("guest.password"), secret: true },
    plain("radio"),
    plain("tx"),
    gain("radio.rxgain"),
    gain("radio.fem.rxgain"),
    plain("af"),
    plain("int.thresh"),
    plain("multi.acks"),
    plain("cad.auto"),
    plain("cad.offset"),
    read("cad.busycap", /^> (\d+)/),
    plain("probe.interval"),
    // Without any, the reply is a sentence, and there is nothing to set again.
    { key: "extra.sf", get: "get extra.sf", parse: word(/^(\d+(?:,\d+)*)$/), set: (v) => [`set extra.sf ${v.replace(/,/g, " ")}`] },
    plain("rxduty"),
    plain("lat"),
    plain("lon"),
    { key: "gps", get: "gps", parse: word(/^(on|off)\b/), set: (v) => [`gps ${v}`] },
    { key: "gps.advert", get: "gps advert", parse: value, set: (v) => [`gps advert ${v}`] },
    { key: "gps.duty", get: "get gps duty", parse: word(/^> (?:always on \()?(\d+)\)?$/), set: (v) => [`set gps duty ${v}`] },
    { key: "gps.standby", get: "get gps standby", parse: word(/^> (\d+)$/), set: (v) => [`set gps standby ${v}`] },
    plain("repeat"),
    plain("advert.interval"),
    plain("flood.advert.interval"),
    plain("flood.max"),
    plain("flood.max.unscoped"),
    plain("flood.max.advert"),
    plain("backoff.multiplier"),
    plain("loop.detect"),
    plain("path.hash.mode"),
    plain("allow.read.only"),
    { key: "meshtimesync", get: "get meshtimesync", parse: word(/^(?:> )?(on|off)\b/), set: (v) => [`set meshtimesync ${v}`] },
    plain("z.mt.enable"),
    plain("z.mt.role"),
    plain("z.mt.relay"),
    plain("z.mt.relay.opaque"),
    plain("z.mt.hops"),
    plain("z.mt.nodeinfo"),
    plain("z.mt.telemetry"),
    CHANNEL,
    ZERO_HOP,
    firewall("mc"),
    plain("z.mc.fw.gap"),
    plain("z.mc.fw.rate"),
    firewall("mt"),
    plain("z.mt.fw.gap"),
    plain("z.mt.fw.rate"),
    plain("z.mc.ping.trigger"),
    plain("z.mc.ping.enable"),
    plain("z.mt.ping.trigger"),
    plain("z.mt.ping.enable"),
    plain("z.ping.snr"),
    plain("z.ping.rssi"),
    plain("z.ping.ignore.direct"),
    plain("z.ping.ignore.remote"),
    plain("z.room.access"),
    ROOM_PASSWORD,
    plain("z.room.on"),
    {
      key: "z.autoshutdown",
      get: "get z.autoshutdown",
      parse: (reply) => (reply === "> off" ? "0" : word(/^> (\d+) mV$/)(reply)),
      set: (v) => [`set z.autoshutdown ${v}`]
    },
    // Last of all: on ESP32 it lets the chip sleep, and the console only stays awake for a while.
    { key: "powersaving", get: "powersaving", parse: word(/^(on|off)$/), set: (v) => [`powersaving ${v}`], last: true }
  ];
  // Without these the node could not be rebuilt.
  const ESSENTIAL = ["name", "radio", "tx"];
  // An empty trigger is no trigger: there is no command to set it.
  const UNSET_WHEN_EMPTY = ["z.mc.ping.trigger", "z.mt.ping.trigger"];

  const regionName = (line) => (/^\s*([^\s^]+)/.exec(line) || [])[1];

  // ask(command, {secret}) resolves to {reply, refused}, and rejects when the node does not answer.
  async function readRegions(ask) {
    const lines = [];
    for (let page = 1, pages = 1; page <= pages; page++) {
      const { reply, refused } = await ask(`get z.mc.regions ${page}`);
      if (refused) return null;
      const [title, ...rows] = reply.split(/\r?\n/);
      const m = /^Regions (\d+)\/(\d+)$/.exec(title.trim());
      if (!m || Number(m[1]) !== page) throw new Error(`respuesta de regiones inesperada: ${title}`);
      pages = Number(m[2]);
      lines.push(...rows.map((r) => r.trimEnd()).filter(Boolean));
    }
    const answer = await ask("region default");
    const scope = word(/default scope is (\S+)$/)(answer.reply);
    return { lines, default: !scope || scope === "<null>" ? null : scope };
  }

  // Reads the node. askPassword() is used when its firmware has no "get password".
  async function collect(ask, askPassword) {
    const required = async (command, options) => {
      const { reply, refused } = await ask(command, options);
      if (refused) throw new Error(`el nodo no admite "${command}": ${reply}`);
      return reply;
    };
    const role = value(await required("get role"));
    if (!ROLES.includes(role)) throw new Error(`el respaldo es para repetidor y room server, no para "${role}"`);

    const publicKey = value(await required("get public.key"));
    const prv = value(await required("get prv.key", { secret: true }));
    if (!PRIVATE_KEY.test(prv || "")) throw new Error("el nodo no entregó su clave privada");

    const warnings = [];
    const answer = await ask("get password", { secret: true });
    let password = answer.refused ? null : value(answer.reply);
    if (password === null) password = (await askPassword()) || null;
    if (password === null) warnings.push("La clave de administrador no está en el respaldo.");

    const settings = {};
    for (const s of SETTINGS) {
      const { reply, refused } = await ask(s.get, { secret: s.secret });
      const v = refused ? null : s.parse(reply);
      if (v !== null) settings[s.key] = v;
      else if (ESSENTIAL.includes(s.key)) throw new Error(`no se pudo leer "${s.key}": ${reply}`);
    }
    if (/\(custom key\)$/.test(settings["z.mt.channel"] || "")) {
      warnings.push("El canal Meshtastic usa una clave propia, que el nodo no muestra: apúntala aparte.");
    }

    const regions = await readRegions(ask);
    if (!regions) warnings.push("El firmware no permite leer las regiones completas: no están en el respaldo.");

    return {
      backup: {
        format: FORMAT, version: VERSION, warning: WARNING, created: new Date().toISOString(),
        role, publicKey, identity: { prv }, password, settings, regions
      },
      warnings
    };
  }

  // A value goes into a command line: a line break in it would run another command.
  const isLine = (text) => typeof text === "string" && !/[\x00-\x1f\x7f]/.test(text);

  function parseBackup(json) {
    const b = JSON.parse(json);
    if (!b || b.format !== FORMAT || b.version !== VERSION) throw new Error("no es un respaldo de nodo de esta versión");
    const regions = b.regions;
    const valid =
      ROLES.includes(b.role) &&
      isLine(b.created) &&
      PRIVATE_KEY.test((b.identity && b.identity.prv) || "") &&
      (b.password === null || (isLine(b.password) && b.password.length > 0)) &&
      b.settings && ESSENTIAL.every((k) => k in b.settings) && Object.values(b.settings).every(isLine) &&
      (!regions || (Array.isArray(regions.lines) && regions.lines.every(isLine) &&
        (regions.default === null || isLine(regions.default))));
    if (!valid) throw new Error("el respaldo está incompleto o dañado");
    return b;
  }

  // One command of a restore. shown stands for it where a secret must not appear.
  const step = (cmd, wait = true) => ({ cmd, wait, secret: false, shown: cmd });
  const secretStep = (name, secret) => ({ cmd: `${name} ${secret}`, wait: true, secret: true, shown: `${name} ••••` });

  function regionSteps(regions) {
    // The wildcard line is not indented: "region load" skips it, its flags are set below.
    // "^" marks the home region, and "region load" would read it as part of the name.
    const rows = regions.lines.filter((l) => l.startsWith(" ")).map((l) => l.replace("^", ""));
    const home = regions.lines.find((l) => l.includes("^"));
    return [
      step("region load", false),
      ...rows.map((row) => step(row, false)),
      step(""),
      // Setting the default scope allows its floods, so the flags follow it.
      step(`region default ${regions.default || "<null>"}`),
      ...regions.lines.filter(regionName).map((line) =>
        step(`region ${/ F$/.test(line) ? "allowf" : "denyf"} ${regionName(line)}`)),
      ...(home ? [step(`region home ${regionName(home)}`)] : []),
      step("region save")
    ];
  }

  function settingSteps(settings, selected) {
    const present = (s) => {
      const v = settings[s.key];
      return v !== undefined && !(v === "" && UNSET_WHEN_EMPTY.includes(s.key));
    };
    return selected.filter(present).flatMap((s) => s.set(settings[s.key]).map((cmd) => (s.secret
      ? { cmd, wait: true, secret: true, shown: `set ${s.key} ••••` }
      : { ...step(cmd), keptIfUnsupported: s.keptIfUnsupported })));
  }

  // The commands that put a backup on a node, in order: the settings, the regions, the password and
  // the identity, and then what has to be last before the reboot.
  function restoreSteps(backup) {
    return [
      ...settingSteps(backup.settings, SETTINGS.filter((s) => !s.last)),
      ...(backup.regions ? regionSteps(backup.regions) : []),
      ...(backup.password ? [secretStep("password", backup.password)] : []),
      secretStep("set prv.key", backup.identity.prv),
      ...settingSteps(backup.settings, SETTINGS.filter((s) => s.last)),
      step("reboot", false)
    ];
  }

  const lost = (s, answer) => answer.refused && !(s.keptIfUnsupported && answer.reply === "Error: unsupported");

  // send(step) resolves to {reply, refused}, at once for a step that has no reply, and rejects when
  // the node does not answer. A refused command does not stop the ones after it.
  async function restore(backup, send) {
    const refused = [];
    for (const s of restoreSteps(backup)) {
      const answer = await send(s);
      if (lost(s, answer)) refused.push({ command: s.shown, reply: s.secret ? "" : answer.reply });
    }
    return refused;
  }

  const api = { FORMAT, VERSION, collect, parseBackup, restoreSteps, restore };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.ZephtacoreBackup = api;
})(typeof window !== "undefined" ? window : globalThis);
