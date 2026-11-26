/**
 * Zephtacore Hardware & Boards Manifest
 * Catálogo unificado y exhaustivo de placas oficiales soportadas por ZephCore.
 */
window.ZEPHTACORE_BOARDS = [
  // =========================================================================
  // --- 1. Serie Elecrow ThinkNode (Muy Populares) ---
  // =========================================================================
  {
    id: "thinknode_m1",
    name: "Elecrow ThinkNode M1",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion", "repeater"],
    desc: "Pantalla E-Paper SSD1681 1.54\" (200x200), transceptor SX1262, GPS Air530Z, sensor RTC y batería integrada.",
    defaultOffset: "0x00"
  },
  {
    id: "thinknode_m3",
    name: "Elecrow ThinkNode M3",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "LR1110",
    flashType: "uf2",
    recommendedRoles: ["companion", "repeater"],
    desc: "Transceptor Semtech LR1110 multifrecuencia con escaneo WiFi/GNSS pasivo y ultrabajo consumo.",
    defaultOffset: "0x00"
  },
  {
    id: "thinknode_m6",
    name: "Elecrow ThinkNode M6",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion", "repeater"],
    desc: "Nodo de bolsillo compacto con transceptor SX1262 optimizado para portabilidad y bajo consumo.",
    defaultOffset: "0x00"
  },
  {
    id: "thinknode_m9/esp32s3/procpu",
    name: "Elecrow ThinkNode M9",
    mcu: "ESP32-S3",
    arch: "esp32",
    radio: "LR1110",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater", "observer"],
    desc: "Pantalla a color ST7789 TFT, transceptor Semtech LR1110, WiFi 4 y Bluetooth 5 BLE.",
    defaultOffset: "0x00"
  },

  // =========================================================================
  // --- 2. Repetidores & Estaciones de Alta Potencia (1W / 30dBm) ---
  // =========================================================================
  {
    id: "station_g2/esp32s3/procpu",
    name: "B&Q / UnitEng Station G2",
    mcu: "ESP32-S3",
    arch: "esp32",
    radio: "SX1262 + PA",
    flashType: "bin",
    recommendedRoles: ["repeater", "room_server", "observer"],
    desc: "Estación base de alta potencia (1W / 30dBm), pantalla OLED, Ethernet/WiFi y caja metálica para nodos fijos.",
    defaultOffset: "0x00"
  },
  {
    id: "gat562_30s",
    name: "GAT-IoT GAT562 30S Power Repeater",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262 + PA",
    flashType: "uf2",
    recommendedRoles: ["repeater"],
    desc: "Placa de alta potencia (1 Watt / 30dBm) con circuitería blindada optimizada para repetidores solares y cumbres.",
    batteryWake: { maxMv: 3080, suggestedMv: 3000 },
    defaultOffset: "0x00"
  },
  {
    id: "rak3401_1watt",
    name: "RAK WisMesh 1W Booster (RAK3401)",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262 + PA",
    flashType: "uf2",
    recommendedRoles: ["repeater"],
    desc: "Módulo de alta potencia RAK3401 + amplificador SKY66122 (1000mW) para enlaces de larguísimo alcance.",
    defaultOffset: "0x00"
  },
  {
    id: "ikoka_nano_30dbm",
    name: "Ikoka Nano 30dBm (1W)",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262 + PA",
    flashType: "uf2",
    recommendedRoles: ["repeater", "companion"],
    desc: "Placa de 1W basada en XIAO nRF52840 con etapa de potencia E22-900M30S para repetidores ligeros.",
    defaultOffset: "0x00"
  },
  {
    id: "sensecap_solar",
    name: "Seeed SenseCAP Solar Node (P1 / P1-Pro)",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["repeater", "observer"],
    desc: "Nodo autónomo estanco para intemperie con panel solar y gestión MPPT de batería integrada.",
    defaultOffset: "0x00"
  },

  // =========================================================================
  // --- 3. Familia Heltec Mesh & WiFi LoRa 32 ---
  // =========================================================================
  {
    id: "heltec_wifi_lora32_v3/esp32s3/procpu",
    name: "Heltec WiFi LoRa 32 V3",
    mcu: "ESP32-S3",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater", "observer", "room_server"],
    desc: "La placa más extendida: pantalla OLED 0.96\", conectividad WiFi, BLE y transceptor Semtech SX1262.",
    defaultOffset: "0x00"
  },
  {
    id: "heltec_wifi_lora32_v4/esp32s3/procpu",
    name: "Heltec WiFi LoRa 32 V4",
    mcu: "ESP32-S3",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater", "observer"],
    desc: "Revisión V4 con receptor SX1262 optimizado, filtrado RF mejorado y pantalla OLED.",
    defaultOffset: "0x00"
  },
  {
    id: "heltec_wifi_lora32_v43/esp32s3/procpu",
    name: "Heltec WiFi LoRa 32 V4.3",
    mcu: "ESP32-S3",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater", "observer"],
    desc: "Revisión V4.3 con mejoras en el circuito de alimentación y gestión de batería LiPo.",
    defaultOffset: "0x00"
  },
  {
    id: "heltec_wifi_lora32_v4_r8/esp32s3/procpu",
    name: "Heltec WiFi LoRa 32 V4-R8 (8MB PSRAM)",
    mcu: "ESP32-S3",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater", "room_server", "observer"],
    desc: "Versión con 8MB Octal PSRAM para soportar servidores de salas BBS pesados y colas MQTT extensas.",
    defaultOffset: "0x00"
  },
  {
    id: "heltec_t114",
    name: "Heltec Mesh Node T114",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion", "repeater"],
    desc: "Dispositivo de bajísimo consumo con pantalla TFT opcional y excelente receptor Semtech SX1262.",
    defaultOffset: "0x00"
  },
  {
    id: "heltec_t1",
    name: "Heltec Mesh Node T1",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion", "repeater"],
    desc: "Nodo portable nRF52840 con pantalla TFT integrada, antena optimizada y gestión de carga USB-C.",
    defaultOffset: "0x00"
  },
  {
    id: "heltec_t096",
    name: "Heltec Mesh Node T096",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion", "repeater"],
    desc: "Formato ultracompacto con pantalla 0.96\" TFT para dispositivos de campo y sensores remotos.",
    defaultOffset: "0x00"
  },
  {
    id: "heltec_wireless_tracker/esp32s3/procpu",
    name: "Heltec Wireless Tracker V1.1",
    mcu: "ESP32-S3",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion"],
    desc: "Pantalla TFT color 0.96\", módulo GNSS UC6580 multi-constelación y transceptor LoRa SX1262.",
    defaultOffset: "0x00"
  },
  {
    id: "heltec_wireless_tracker_v2/esp32s3/procpu",
    name: "Heltec Wireless Tracker V2",
    mcu: "ESP32-S3",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion"],
    desc: "Segunda generación con antena GPS mejorada, mayor autonomía y pantalla TFT a color.",
    defaultOffset: "0x00"
  },

  // =========================================================================
  // --- 4. LilyGO & TTGO Devices ---
  // =========================================================================
  {
    id: "lilygo_techo",
    name: "LilyGO T-Echo",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion"],
    desc: "Dispositivo autónomo con pantalla E-Paper monocroma, receptor GPS L76K y batería integrada.",
    defaultOffset: "0x00"
  },
  {
    id: "lilygo_timpulse_plus",
    name: "LilyGO T-Impulse Plus",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion"],
    desc: "Reloj / tracker de muñeca con pantalla OLED SSD1315, GPS U-Blox M10 y transceptor SX1262.",
    defaultOffset: "0x00"
  },
  {
    id: "lilygo_t3s3/esp32s3/procpu",
    name: "LilyGO T3-S3 LoRa",
    mcu: "ESP32-S3",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater", "observer"],
    desc: "Módulo ESP32-S3 con slot MicroSD, pantalla OLED, gestión de batería y transceptor LoRa SX1262.",
    defaultOffset: "0x00"
  },
  {
    id: "lilygo_tlora_c6/esp32c6/hpcore",
    name: "LilyGO T-LoRa C6",
    mcu: "ESP32-C6",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater"],
    desc: "Procesador RISC-V mononúcleo ESP32-C6 con soporte nativo para WiFi 6 (802.11ax) y LoRa SX1262.",
    defaultOffset: "0x00"
  },
  {
    id: "ttgo_tbeam/esp32/procpu",
    name: "LilyGO TTGO T-Beam",
    mcu: "ESP32",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater"],
    desc: "Clásico diseño con zócalo para batería 18650, receptor GPS NEO-6M/NEO-M8N y gestión AXP192/2101.",
    defaultOffset: "0x1000"
  },
  {
    id: "ttgo_lora32/esp32/procpu",
    name: "LilyGO TTGO LoRa32 V2.1",
    mcu: "ESP32",
    arch: "esp32",
    radio: "SX1276",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater", "observer"],
    desc: "Placa clásica ESP32 con pantalla OLED 0.96\", radio SX1276 y lector para tarjetas MicroSD.",
    defaultOffset: "0x1000"
  },

  // =========================================================================
  // --- 5. RAK Wireless & Seeed Studio Devices ---
  // =========================================================================
  {
    id: "rak4631",
    name: "RAK Wireless WisBlock RAK4631",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["repeater", "companion"],
    desc: "Ecosistema modular líder de bajísimo consumo para repetidores solares y despliegues profesionales.",
    defaultOffset: "0x00"
  },
  {
    id: "rak_wismesh_tag",
    name: "RAK WisMesh Tag",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion"],
    desc: "Etiqueta rastreadora ultracompacta con GNSS, acelerómetro y batería LiPo integrada.",
    defaultOffset: "0x00"
  },
  {
    id: "t1000_e",
    name: "Seeed SenseCAP Card T1000-E",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "LR1110",
    flashType: "uf2",
    recommendedRoles: ["companion"],
    desc: "Tarjeta ultra-fina con GPS, acelerómetro, botón SOS y transceptor Semtech LR1110.",
    defaultOffset: "0x00"
  },
  {
    id: "xiao_nrf52840",
    name: "Seeed Studio XIAO nRF52840",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion", "repeater"],
    desc: "Factor de forma miniatura (tamaño pulgar) con cargador LiPo integrado; ideal para trackers y portables.",
    defaultOffset: "0x00"
  },
  {
    id: "wio_tracker_l1",
    name: "Seeed Studio Wio Tracker L1 Pro",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion"],
    desc: "Tracker geográfico con GPS multi-constelación, sensores IMU y carcasa robusta.",
    defaultOffset: "0x00"
  },
  {
    id: "wio_tracker_l1_1w",
    name: "Seeed Wio Tracker L1 Pro 1W",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262 + PA",
    flashType: "uf2",
    recommendedRoles: ["repeater", "companion"],
    desc: "Edición especial con amplificador de potencia de 1000mW y pantalla OLED integrada.",
    defaultOffset: "0x00"
  },
  {
    id: "meshtracker_x1",
    name: "Seeed SenseCAP MeshTracker X1",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "LR2021",
    flashType: "uf2",
    recommendedRoles: ["companion"],
    desc: "Placa de nueva generación con radio Semtech LR2021 y receptor GNSS de doble banda L1+L5.",
    defaultOffset: "0x00"
  },
  {
    id: "xiao_esp32s3/esp32s3/procpu",
    name: "Seeed Studio XIAO ESP32-S3",
    mcu: "ESP32-S3",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater"],
    desc: "Módulo miniatura de doble núcleo ESP32-S3 con antena externa y soporte para radio LoRa.",
    defaultOffset: "0x00"
  },
  {
    id: "xiao_esp32c3",
    name: "Seeed Studio XIAO ESP32-C3",
    mcu: "ESP32-C3",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater"],
    desc: "Módulo económico mononúcleo RISC-V de bajo consumo con transceptor LoRa SX1262.",
    defaultOffset: "0x00"
  },
  {
    id: "xiao_esp32c6/esp32c6/hpcore",
    name: "Seeed Studio XIAO ESP32-C6",
    mcu: "ESP32-C6",
    arch: "esp32",
    radio: "SX1262",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater"],
    desc: "Módulo miniatura RISC-V con conectividad WiFi 6, Zigbee, Thread y radio SX1262.",
    defaultOffset: "0x00"
  },

  // =========================================================================
  // --- 6. Diseños Open Source & DIY ---
  // =========================================================================
  {
    id: "promicro_sx1262",
    name: "ProMicro SuperMini SX1262",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion", "repeater"],
    desc: "Build artesanal DIY ultra-económica y ligera para nodos móviles o mochileros.",
    batteryWake: { maxMv: 3300, suggestedMv: 3200 },
    defaultOffset: "0x00"
  },
  {
    id: "promicro_lr2021",
    name: "ProMicro SuperMini LR2021",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "LR2021",
    flashType: "uf2",
    recommendedRoles: ["companion", "repeater"],
    desc: "Build DIY equipada con el moderno chip Semtech LR2021 para máxima sensibilidad.",
    defaultOffset: "0x00"
  },
  {
    id: "muziworks_r1neo",
    name: "Muzi Works R1 Neo",
    mcu: "nRF52840",
    arch: "nrf52",
    radio: "SX1262",
    flashType: "uf2",
    recommendedRoles: ["companion", "repeater"],
    desc: "Diseño de código abierto ultracompacto para nodos de malla discretos.",
    defaultOffset: "0x00"
  },
  {
    id: "meshnology_w12/esp32s3/procpu",
    name: "Meshnology W12 LR2021",
    mcu: "ESP32-S3",
    arch: "esp32",
    radio: "LR2021",
    flashType: "bin",
    recommendedRoles: ["companion", "repeater", "observer"],
    desc: "Placa ESP32-S3 de alto rendimiento con transceptor Semtech LR2021 y pantalla OLED.",
    defaultOffset: "0x00"
  },

  // =========================================================================
  // --- 7. Plataformas Next-Gen nRF54L15 / MG24 / STM32WL ---
  // =========================================================================
  {
    id: "xiao_nrf54l15/nrf54l15/cpuapp",
    name: "Seeed Studio XIAO nRF54L15",
    mcu: "nRF54L15",
    arch: "nrf54l",
    radio: "SX1262",
    flashType: "hex",
    recommendedRoles: ["companion", "repeater"],
    desc: "Procesador nórdico de última generación Cortex-M33 a 128MHz con récord de eficiencia energética.",
    defaultOffset: "0x00"
  },
  {
    id: "me25ls02/nrf54l15/cpuapp",
    name: "MinewSemi ME25LS02 (nRF54L15)",
    mcu: "nRF54L15",
    arch: "nrf54l",
    radio: "SX1262",
    flashType: "hex",
    recommendedRoles: ["companion", "repeater"],
    desc: "Módulo industrial nRF54L15 con transceptor SX1262 integrado para entornos exigentes.",
    defaultOffset: "0x00"
  },
  {
    id: "seeed_lr2021_evk/nrf54l15/cpuapp",
    name: "Semtech LR2021 EVK (nRF54L15)",
    mcu: "nRF54L15",
    arch: "nrf54l",
    radio: "LR2021",
    flashType: "hex",
    recommendedRoles: ["companion", "repeater"],
    desc: "Kit de evaluación oficial con radio Semtech LR2021 y microcontrolador nRF54L15.",
    defaultOffset: "0x00"
  },
  {
    id: "xiao_mg24",
    name: "Seeed Studio XIAO MG24",
    mcu: "MG24",
    arch: "mg24",
    radio: "SX1262",
    flashType: "hex",
    recommendedRoles: ["companion", "repeater"],
    desc: "Microcontrolador Silicon Labs EFR32MG24 con transceptor LoRa SX1262.",
    defaultOffset: "0x00"
  },
  {
    id: "lora_e5_mini",
    name: "Seeed Studio LoRa-E5 mini",
    mcu: "STM32WL",
    arch: "stm32wl",
    radio: "SX1262",
    flashType: "hex",
    recommendedRoles: ["companion", "repeater"],
    desc: "Módulo compacto basado en STM32WLE5JC (Cortex-M4 + LoRa integrado en chip único).",
    defaultOffset: "0x00"
  }
];

window.ZEPHTACORE_ROLES = [
  {
    id: "companion",
    name: "Companion (Personal)",
    desc: "Para sincronizar con la app oficial de MeshCore vía Bluetooth o USB. Puente de canales #mt-* y chat.",
    defaultRelay: "off"
  },
  {
    id: "repeater",
    name: "Repeater (Repetidor de Red)",
    desc: "Reenvío autónomo para la comunidad con backoff reactivo por SNR, Zero-Hop y estadísticas remotas.",
    defaultRelay: "essential"
  },
  {
    id: "room_server",
    name: "Room Server (Servidor BBS)",
    desc: "Tablón de anuncios público y almacenamiento de mensajes offline para nodos de la malla.",
    defaultRelay: "off"
  },
  {
    id: "observer",
    name: "Observer (Gateway / MQTT)",
    desc: "Solo escucha y reenvía todos los paquetes escuchados por WiFi hacia un broker MQTT central.",
    defaultRelay: "off"
  },
  {
    id: "kiss_modem",
    name: "KISS Modem (TNC)",
    desc: "Modem LoRa transparente para control desde software host en PC/Raspberry Pi vía USB.",
    defaultRelay: "off"
  }
];
