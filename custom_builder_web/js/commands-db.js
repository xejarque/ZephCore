/**
 * Zephtacore CLI Command Reference Database
 * Source of truth: docs/CLI_commands.md
 */
window.ZEPHTACORE_COMMANDS = [
  // --- Meshtastic (z.mt.*) ---
  {
    name: "z.mt.enable",
    category: "meshtastic",
    type: "get|set",
    params: "on|off|1|0",
    default: "on",
    desc: "Activa o desactiva el lado Meshtastic. En 'off' el receptor oye solo con la palabra de sincronía de MeshCore, no se repite ninguna trama como Meshtastic y el NodeInfo se detiene.",
    example: "set z.mt.enable on",
    tags: ["meshtastic", "dual-mesh", "radio"]
  },
  {
    name: "z.mt.role",
    category: "meshtastic",
    type: "get|set",
    params: "repeater: client|client_base|router|router_late|repeater · companion: client_mute|client",
    default: "client_base (repeater) / client_mute (companion)",
    desc: "Rol anunciado en el NodeInfo y su temporización. 'client_base' avisa de que el nodo es fijo, y otros pueden conservar los saltos de lo que repite. 'client_mute' no repite nada, diga lo que diga z.mt.relay. Ningún rol cancela una repetición al oír el eco: la propia solo espera más.",
    example: "set z.mt.role client_base",
    tags: ["meshtastic", "rol", "timing"]
  },
  {
    name: "z.mt.relay",
    category: "meshtastic",
    type: "get|set",
    params: "off|essential|all",
    default: "essential (repeater) / off (companion)",
    desc: "Qué se repite de Meshtastic. 'essential': texto, DMs, sus ACK, NodeInfo y traceroute, que salen siempre, y la posición, que se descarta si otro nodo la repite mientras la nuestra espera. 'all': lo anterior más telemetría y el resto de puertos.",
    example: "set z.mt.relay essential",
    tags: ["meshtastic", "relay", "repetidor"]
  },
  {
    name: "z.mt.relay.opaque",
    category: "meshtastic",
    type: "get|set",
    params: "on|off|1|0",
    default: "on",
    desc: "También repite las tramas de canales cuya clave no tiene este nodo. No se sabe qué llevan, así que en 'essential' se repiten sea lo que sea; se descartan si otro nodo las repite mientras la nuestra espera.",
    example: "set z.mt.relay.opaque on",
    tags: ["meshtastic", "relay", "cifrado"]
  },
  {
    name: "z.mt.channel",
    category: "meshtastic",
    type: "get|set",
    params: "<nombre> [default|<clave_base64>]",
    default: "SFNarrow (clave por defecto)",
    desc: "Canal primario en el que sale el NodeInfo (solo repetidor, el companion lo rechaza). Nombre de hasta 11 caracteres y clave de 1 o 16 bytes en base64; con solo el nombre se conserva la clave. La clave no se muestra nunca.",
    example: "set z.mt.channel SFNarrow",
    tags: ["meshtastic", "canales", "repetidor"]
  },
  {
    name: "z.mt.channel.add",
    category: "meshtastic",
    type: "set",
    params: "<nombre> [default|<clave_base64>]",
    default: "-",
    desc: "Añade un canal Meshtastic (solo companion). La clave se escribe como la muestra Meshtastic: 1 byte (AQ== la clave por defecto, Ag== su variante 2) o 16 bytes en base64. La app lo lista como #mt-<nombre> en minúsculas. Hasta 8 canales.",
    example: "set z.mt.channel.add Catalunya AQ==",
    tags: ["meshtastic", "canales", "companion"]
  },
  {
    name: "z.mt.channel.remove",
    category: "meshtastic",
    type: "set",
    params: "<nombre>",
    default: "-",
    desc: "Elimina un canal Meshtastic y su ranura en la app (solo companion). Borrar el canal en la app hace lo mismo.",
    example: "set z.mt.channel.remove Catalunya",
    tags: ["meshtastic", "canales", "companion"]
  },
  {
    name: "z.mt.channels",
    category: "meshtastic",
    type: "get",
    params: "",
    default: "-",
    desc: "Canales Meshtastic del companion. Un número es una variante de la clave por defecto y 'key' una clave propia, que nunca se imprime.",
    example: "get z.mt.channels",
    tags: ["meshtastic", "canales", "companion"]
  },
  {
    name: "z.mt.hops",
    category: "meshtastic",
    type: "get|set",
    params: "<0-7>",
    default: "7 (CONFIG_ZEPHTACORE_MT_HOP_LIMIT)",
    desc: "Saltos (hop_limit) de las tramas que origina este nodo hacia Meshtastic.",
    example: "set z.mt.hops 7",
    tags: ["meshtastic", "saltos", "routing"]
  },
  {
    name: "z.mt.zerohop",
    category: "meshtastic",
    type: "get|set",
    params: "off|auto",
    default: "off",
    desc: "Conserva los saltos de lo que repite un vecino fijo. Con 'auto', una trama que un vecino fijo repitió hasta nosotros se repite con el hop_limit con que llegó, y una cadena de nodos fijos cuesta un salto. Un vecino fijo es un nodo oído directamente en las últimas 72 h cuyo NodeInfo anuncia router, router_late o client_base, o uno de la lista. Necesita z.mt.relay activo.",
    example: "set z.mt.zerohop auto",
    tags: ["meshtastic", "saltos", "routing"]
  },
  {
    name: "z.mt.zerohop.add",
    category: "meshtastic",
    type: "set",
    params: "<nodo>",
    default: "-",
    desc: "Añade a la lista un nodo cuyas repeticiones conservan los saltos sea cual sea su rol (hasta 8), por número de nodo: !a1b2c3d4 o a1b2c3d4. Solo viaja el último byte del número, así que todo nodo que lo comparta recibe el mismo trato.",
    example: "set z.mt.zerohop.add !a1b2c3d4",
    tags: ["meshtastic", "saltos", "routing"]
  },
  {
    name: "z.mt.zerohop.remove",
    category: "meshtastic",
    type: "set",
    params: "<nodo>",
    default: "-",
    desc: "Quita un nodo de la lista de zero hop.",
    example: "set z.mt.zerohop.remove !a1b2c3d4",
    tags: ["meshtastic", "saltos", "routing"]
  },
  {
    name: "z.mt.zerohop.clear",
    category: "meshtastic",
    type: "set",
    params: "",
    default: "-",
    desc: "Vacía la lista de nodos de zero hop.",
    example: "set z.mt.zerohop.clear",
    tags: ["meshtastic", "saltos", "routing"]
  },
  {
    name: "z.mt.nodeinfo",
    category: "meshtastic",
    type: "get|set",
    params: "<segundos>",
    default: "43200",
    desc: "Segundos entre los NodeInfo propios: 0 nunca, de lo contrario de 600 a 604800. El primero sale 30 s tras el arranque o tras un cambio. La posición (set lat / set lon) lo acompaña mientras advert.loc no sea none.",
    example: "set z.mt.nodeinfo 43200",
    tags: ["meshtastic", "nodeinfo", "anuncio"]
  },
  {
    name: "z.mt.telemetry",
    category: "meshtastic",
    type: "get|set",
    params: "<segundos>",
    default: "0",
    desc: "Métricas del dispositivo (batería, ocupación del canal, aire de TX, uptime) en el canal primario. 0 nunca, de lo contrario de 60 a 86400. Solo en el repetidor: el companion lo rechaza.",
    example: "set z.mt.telemetry 3600",
    tags: ["meshtastic", "telemetria", "repetidor"]
  },
  {
    name: "z.mt.ping.enable",
    category: "meshtastic",
    type: "get|set",
    params: "on <comando>|off|1 <comando>|0",
    default: "off",
    desc: "Bot de ping en el canal 'Bots': responde con los saltos, el último relay y el SNR. No se activa sin un comando, que puede cambiarse después; no puede ser direct ni relayed.",
    example: "set z.mt.ping.enable on /ping",
    tags: ["meshtastic", "ping", "diagnostico"]
  },
  {
    name: "z.mt.ping.trigger",
    category: "meshtastic",
    type: "get|set",
    params: "<comando>",
    default: "(vacío)",
    desc: "Comando al que contesta el bot Meshtastic, de 1 a 15 caracteres sin espacios. El mensaje entero debe coincidir con él, sin distinguir mayúsculas ni los espacios de alrededor.",
    example: "set z.mt.ping.trigger /ping",
    tags: ["meshtastic", "ping", "diagnostico"]
  },
  {
    name: "z.mt.ping.status",
    category: "meshtastic",
    type: "get",
    params: "",
    default: "-",
    desc: "Estado del bot Meshtastic: activado, comando, pings contestados e ignorados desde el arranque.",
    example: "get z.mt.ping.status",
    tags: ["meshtastic", "ping", "metricas"]
  },
  {
    name: "z.mt.stats",
    category: "meshtastic",
    type: "get",
    params: "",
    default: "-",
    desc: "Contadores del puente Meshtastic. heard (tramas oídas), relayed (repeticiones encoladas), opaque (canales desconocidos), dropped (no abiertas y sin forma de MeshCore), shed (repeticiones no encoladas por falta de hueco), long (más de 184 bytes sin ranura de cola) y kept (saltos conservados).",
    example: "get z.mt.stats",
    tags: ["meshtastic", "metricas", "diagnostico"]
  },
  {
    name: "z.mt.neighbors",
    category: "meshtastic",
    type: "get",
    params: "[página] [recent]",
    default: "-",
    desc: "Nodos Meshtastic oídos directamente, uno por línea y por mejor SNR; con 'recent', el último oído primero. 4 líneas por página. Alias: z.mt.vecinos y 'recientes'.",
    example: "get z.mt.neighbors 2 recent",
    tags: ["meshtastic", "vecinos", "topologia"]
  },
  {
    name: "z.mt.hopstats",
    category: "meshtastic",
    type: "get",
    params: "",
    default: "-",
    desc: "Tramas oídas según los saltos recorridos (hop_start - hop_limit): 0:12 1:5 7+:1 ?:2; '?' son las de un emisor sin hop_start. Solo repetidor.",
    example: "get z.mt.hopstats",
    tags: ["meshtastic", "saltos", "metricas"]
  },
  {
    name: "z.mt.relays",
    category: "meshtastic",
    type: "get",
    params: "",
    default: "-",
    desc: "Quién entregó las tramas, el que más primero: el byte de relay de la cabecera, el último byte de un número de nodo. Se guardan 16. Solo repetidor.",
    example: "get z.mt.relays",
    tags: ["meshtastic", "vecinos", "topologia"]
  },
  {
    name: "z.mt.fw.drop",
    category: "meshtastic",
    type: "get|set",
    params: "<regla> on|off|1|0 | none",
    default: "none",
    desc: "Cortafuegos Meshtastic (solo repetidor): puertos cuyas tramas nuevas se descartan: text, position, nodeinfo, telemetry, traceroute y other. El routing (ACK y NAK) nunca se descarta.",
    example: "set z.mt.fw.drop telemetry on",
    tags: ["meshtastic", "firewall", "seguridad"]
  },
  {
    name: "z.mt.fw.gap",
    category: "meshtastic",
    type: "get|set",
    params: "<0-255>",
    default: "0",
    desc: "Minutos entre dos NodeInfo de un mismo nodo que se aceptan; 0 sin límite.",
    example: "set z.mt.fw.gap 30",
    tags: ["meshtastic", "firewall", "seguridad"]
  },
  {
    name: "z.mt.fw.rate",
    category: "meshtastic",
    type: "get|set",
    params: "<0-255>",
    default: "0",
    desc: "Tramas por minuto que se aceptan de un mismo repetidor vecino, por el byte de relay de la cabecera; 0 sin límite.",
    example: "set z.mt.fw.rate 60",
    tags: ["meshtastic", "firewall", "seguridad"]
  },
  {
    name: "z.mt.fw.stats",
    category: "meshtastic",
    type: "get",
    params: "",
    default: "-",
    desc: "Descartes del cortafuegos Meshtastic desde el arranque: por gap, por rate y por las reglas de drop.",
    example: "get z.mt.fw.stats",
    tags: ["meshtastic", "firewall", "metricas"]
  },

  // --- MeshCore y cortafuegos (z.mc.*) ---
  {
    name: "z.mc.fw.drop",
    category: "meshcore",
    type: "get|set",
    params: "<regla> on|off|1|0 | none",
    default: "none",
    desc: "Cortafuegos MeshCore (solo repetidor): tipos de trama flood que se descartan: advert, request, text, group, ack, path y trace. Las tramas de control nunca se descartan y las directas no se tocan. 'get' lista las reglas activas con lo descartado.",
    example: "set z.mc.fw.drop advert on",
    tags: ["meshcore", "firewall", "seguridad"]
  },
  {
    name: "z.mc.fw.gap",
    category: "meshcore",
    type: "get|set",
    params: "<0-255>",
    default: "0",
    desc: "Minutos entre dos adverts de una misma clave que se aceptan; 0 sin límite. Solo cuenta el advert cuya firma es válida. Se recuerdan 48 claves.",
    example: "set z.mc.fw.gap 10",
    tags: ["meshcore", "firewall", "seguridad"]
  },
  {
    name: "z.mc.fw.rate",
    category: "meshcore",
    type: "get|set",
    params: "<0-255>",
    default: "0",
    desc: "Tramas flood por minuto que se aceptan de un mismo repetidor vecino, por el último hash del camino; 0 sin límite. Con hash de 1 byte también cuenta, pero lo comparten todos los repetidores cuya clave empieza por ese byte.",
    example: "set z.mc.fw.rate 60",
    tags: ["meshcore", "firewall", "seguridad"]
  },
  {
    name: "z.mc.fw.stats",
    category: "meshcore",
    type: "get",
    params: "",
    default: "-",
    desc: "Descartes del cortafuegos MeshCore desde el arranque: adverts por gap, tramas por rate y tramas por las reglas de drop.",
    example: "get z.mc.fw.stats",
    tags: ["meshcore", "firewall", "metricas"]
  },
  {
    name: "z.mc.ping.enable",
    category: "meshcore",
    type: "get|set",
    params: "on <comando>|off|1 <comando>|0",
    default: "off",
    desc: "Bot de respuesta automática en el canal MeshCore '#bot'. No se activa sin un comando, que puede cambiarse después; no puede ser direct ni relayed.",
    example: "set z.mc.ping.enable on /ping",
    tags: ["meshcore", "ping", "diagnostico"]
  },
  {
    name: "z.mc.ping.trigger",
    category: "meshcore",
    type: "get|set",
    params: "<comando>",
    default: "(vacío)",
    desc: "Comando al que contesta el bot MeshCore, de 1 a 15 caracteres sin espacios. El mensaje entero debe coincidir con él, sin distinguir mayúsculas ni los espacios de alrededor.",
    example: "set z.mc.ping.trigger /ping",
    tags: ["meshcore", "ping", "diagnostico"]
  },
  {
    name: "z.mc.ping.status",
    category: "meshcore",
    type: "get",
    params: "",
    default: "-",
    desc: "Estado del bot MeshCore: activado, comando, pings contestados e ignorados desde el arranque.",
    example: "get z.mc.ping.status",
    tags: ["meshcore", "ping", "metricas"]
  },
  {
    name: "z.mc.neighbors",
    category: "meshcore",
    type: "get",
    params: "[página] [recent]",
    default: "-",
    desc: "Repetidores MeshCore cuyo advert de salto cero se oyó, uno por línea y por mejor SNR; con 'recent', el último oído primero. 4 líneas por página. Alias: z.mc.vecinos y 'recientes'.",
    example: "get z.mc.neighbors",
    tags: ["meshcore", "vecinos", "topologia"]
  },
  {
    name: "z.mc.hopstats",
    category: "meshcore",
    type: "get",
    params: "",
    default: "-",
    desc: "Tramas flood oídas según los saltos recorridos: 0:12 1:5 7+:1. Solo repetidor.",
    example: "get z.mc.hopstats",
    tags: ["meshcore", "saltos", "metricas"]
  },
  {
    name: "z.mc.relays",
    category: "meshcore",
    type: "get",
    params: "",
    default: "-",
    desc: "Quién entregó las tramas flood, el que más primero: el último hash del camino. Se guardan 16. Solo repetidor.",
    example: "get z.mc.relays",
    tags: ["meshcore", "vecinos", "topologia"]
  },

  {
    name: "z.room.on",
    category: "meshcore",
    type: "get|set",
    params: "on|off|1|0",
    default: "off",
    desc: "Activa o desactiva la Sala Virtual integrada en el repetidor (identidad secundaria ADV_TYPE_ROOM en /lfs/repeater/room.id). Al activarla emite un advert inicial; los periódicos salen escalonados 30 s tras el del repetidor.",
    example: "set z.room.on on",
    tags: ["meshcore", "room", "sala", "repetidor"]
  },
  {
    name: "z.room.access",
    category: "meshcore",
    type: "get|set",
    params: "open|readonly|private",
    default: "open",
    desc: "Política de acceso de la sala virtual. 'open': abierta (lectura y escritura sin clave); 'readonly': solo lectura pública ('[RO]'); 'private': privada ('[Priv]', requiere z.room.password o clave admin). Re-anuncia el nombre derivado automáticamente.",
    example: "set z.room.access readonly",
    tags: ["meshcore", "room", "acceso", "privacidad"]
  },
  {
    name: "z.room.password",
    category: "meshcore",
    type: "get|set",
    params: "<clave>|clear",
    default: "(none)",
    desc: "Establece o limpia la contraseña de escritura para miembros de la sala virtual (1–15 caracteres), independiente de guest.password del repetidor.",
    example: "set z.room.password miclave",
    tags: ["meshcore", "room", "seguridad", "password"]
  },
  {
    name: "z.room.post",
    category: "meshcore",
    type: "set",
    params: "<texto>",
    default: "-",
    desc: "Publica un mensaje de sistema en la sala virtual cuando z.room.on está activo.",
    example: "set z.room.post Mantenimiento programado a las 22h",
    tags: ["meshcore", "room", "bbs"]
  },
  {
    name: "z.room.advert",
    category: "meshcore",
    type: "set",
    params: "[flood]",
    default: "-",
    desc: "Envía de inmediato un advert zero-hop o flood de la identidad de la sala virtual.",
    example: "set z.room.advert flood",
    tags: ["meshcore", "room", "advert"]
  },
  {
    name: "z.room.acl",
    category: "meshcore",
    type: "get",
    params: "",
    default: "-",
    desc: "Lista las entradas de la ACL de la sala virtual (/lfs/repeater/room_acl) con sus permisos y prefijo de clave pública.",
    example: "get z.room.acl",
    tags: ["meshcore", "room", "acl"]
  },

  // --- Ping compartido y diagnóstico ---
  {
    name: "z.ping.snr",
    category: "diagnostico",
    type: "get|set",
    params: "on|off|1|0",
    default: "on",
    desc: "El bot de ping muestra el SNR en su respuesta. Lo comparten MeshCore y Meshtastic.",
    example: "set z.ping.snr on",
    tags: ["ping", "diagnostico"]
  },
  {
    name: "z.ping.rssi",
    category: "diagnostico",
    type: "get|set",
    params: "on|off|1|0",
    default: "off",
    desc: "El bot de ping muestra el RSSI en su respuesta. Lo comparten MeshCore y Meshtastic.",
    example: "set z.ping.rssi on",
    tags: ["ping", "diagnostico"]
  },
  {
    name: "z.ping.ignore.direct",
    category: "diagnostico",
    type: "get|set",
    params: "on|off|1|0",
    default: "off",
    desc: "El bot de ping no contesta a un ping oído con 0 saltos. Compartido.",
    example: "set z.ping.ignore.direct on",
    tags: ["ping", "diagnostico"]
  },
  {
    name: "z.ping.ignore.remote",
    category: "diagnostico",
    type: "get|set",
    params: "on|off|1|0",
    default: "off",
    desc: "El bot de ping no contesta a un ping oído con 1 salto o más. Compartido.",
    example: "set z.ping.ignore.remote on",
    tags: ["ping", "diagnostico"]
  },
  {
    name: "z.info",
    category: "diagnostico",
    type: "get",
    params: "",
    default: "-",
    desc: "Resumen en una línea: versión del firmware, uptime, batería, flash, heap del kernel y contactos.",
    example: "get z.info",
    tags: ["diagnostico", "telemetria", "sistema"]
  },
  {
    name: "z.stats",
    category: "diagnostico",
    type: "get",
    params: "",
    default: "-",
    desc: "Contadores de MeshCore (RX, TX, errores, flood y directos, aire, cola) y, si los hay, los de Meshtastic.",
    example: "get z.stats",
    tags: ["diagnostico", "metricas", "radio"]
  },
  {
    name: "z.tx.stalls",
    category: "diagnostico",
    type: "get",
    params: "",
    default: "-",
    desc: "Veces que el nodo no pudo transmitir durante 4 s. Por causa: lbt (el CAD de la radio siguió rechazando el envío) y rx (la puerta de recepción siguió cerrada).",
    example: "get z.tx.stalls",
    tags: ["diagnostico", "cad", "rf"]
  },
  {
    name: "cad.stats",
    category: "diagnostico",
    type: "get",
    params: "",
    default: "-",
    desc: "Estadísticas del detector adaptativo de portadora LoRa (Channel Activity Detection).",
    example: "get cad.stats",
    tags: ["radio", "cad", "rf"]
  },

  // --- Sistema ---
  {
    name: "power",
    category: "sistema",
    type: "get",
    params: "",
    default: "-",
    desc: "Muestra las fuentes de alimentación: batería en mV, USB, carga solar, alimentación externa y cargador.",
    example: "power",
    tags: ["bateria", "energia", "hardware"]
  },
  {
    name: "reboot",
    category: "sistema",
    type: "set",
    params: "",
    default: "-",
    desc: "Ejecuta un reinicio seguro por software del nodo.",
    example: "reboot",
    tags: ["sistema", "mantenimiento"]
  }
];
