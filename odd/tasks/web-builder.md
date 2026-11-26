# Web builder: envío fiable, ajustes que faltan y respaldo del nodo

Base: `fork/master` `2f66c6ee`. Estado: aprobado el 2026-11-26, en implementación.

## Objetivo

La web hace lo que promete: configura el nodo por USB sin ocultar rechazos y guarda un respaldo leído del propio nodo
con el que se puede recuperar.

## Specs

Envío por USB

- **S1.** El envío se detiene cuando el nodo rechaza un comando, con las respuestas tal como las escribe el firmware
  (`  -> Error…`). Un reloj del nodo ya adelantado no detiene el envío.
- **S2.** A un companion no se le envía `password`. Cada comando enviado a un companion lo acepta su firmware.
- **S3.** Se envían la clave de invitado y el modo GPS. Se quitan la altitud, la opción `NOT_PRESENT` y la casilla de
  firewall.
- **S4.** Las claves admiten como máximo 15 caracteres.
- **S5.** Al elegir repetidor en ProMicro se pregunta el auto-apagado: desmarcado, 3200 mV editable, con aviso sobre
  baterías sin BMS.
- **S6.** Los textos dicen lo que ocurre, incluida la compilación avanzada (solo graba claves, potencia, saltos y
  Meshtastic). Se quita la línea de la sala virtual, que no activa nada.

Firmware

- **S7.** La copia de rescate incluye la identidad de la sala virtual (`room.id`); su lista de clientes, no.
- **S8.** `get password` devuelve la clave de admin, por USB y por LoRa a quien ya es admin, en repetidor y room server.
  Un companion lo rechaza.
- **S8b.** `get z.mc.regions [página]` (ADR 0014: lo de MeshCore va bajo `z.mc.`) muestra el mapa de regiones por páginas de líneas enteras, por USB y por LoRa.
  `region` no cambia.

Respaldo

- **S9.** La web lee del nodo por USB, con los comandos del CLI, y guarda: identidad, nombre, claves, radio, potencia,
  posición, GPS, anuncios y reenvío, Meshtastic, sala virtual, regiones, energía y auto-apagado. Repetidor y room
  server.
- **S10.** El fichero es JSON legible, versionado, con un aviso de que permite suplantar y administrar el nodo.
- **S11.** Restaurar envía esos valores y reinicia; avisa de que no deben quedar dos nodos con la misma identidad.
- **S12.** Con un firmware sin `get password`, la web pide la clave al guardar.
- **S13.** El respaldo antiguo, generado desde el formulario, se elimina.

## Límites

- No se respaldan la lista de clientes, la identidad de la sala (no hay comando) ni los ajustes fuera de S9.
- La clave propia de un canal Meshtastic no se puede leer (`get z.mt.channel` no la muestra): la web avisa.
- Lo propio de la placa queda fuera (decidido tras la revisión): calibración ADC, LEDs, zumbador, rotaciones y zona
  horaria.
- Companion fuera del respaldo.

## Tareas

- [x] T1. S1 y S2, con tests de `tests/web` sobre respuestas reales.
- [x] T2. S3 a S6.
- [x] T3. S7: reglas y tests de host, ADR 0021, `get z.rescue`.
- [x] T4. S8 y S8b: comandos, tests, `docs/CLI_commands.md`.
- [x] T5. S9 a S13: lista exacta de comandos comprobada contra el firmware y fijada con tests.
- [x] T6. Docs y notas de versión; tests de host y web; compilaciones.
- [x] T7. Banco por USB en el RAK4631, sin `erase` (el usuario prueba el room server en él): antes de tocarlo, mirar
      qué tiene y preguntar.
- [ ] T8. Revisión de código (hecha con Opus, esfuerzo low) y una sola fusión a `master`.

## Resultados (2026-11-26)

- Tests: 90 web y 373 de host.
- Compilado en `rak4631` (repetidor, room server, companion) y `heltec_wifi_lora32_v3` (repetidor). Frente a
  `2f66c6ee`: repetidor nRF52 +728 B de flash y 0 de RAM; ESP32-S3 +256 B de flash y +16 B de RAM.
- Banco en RAK4631 (repetidor con sala virtual), sin `erase`, con el código de la web ejecutado en Node contra el puerto
  serie: respaldo con el firmware anterior (S12), respaldo tras flashear (idéntico), 24 regiones en dos páginas,
  restauración tras cambiar nombre, `owner.info`, `flood.max` y regiones (respaldo posterior idéntico), envío de ajustes
  rechazado y aceptado. Ninguna clave en la consola. `get z.rescue` informa `room ok`.
- Revisión: se corrigen el respaldo incompleto (radio, energía y GPS), las claves visibles al enviar ajustes, `gps`
  en una placa sin GPS (ahora va justo antes del reinicio) y la validación del fichero. No se trata una clave de sala
  escrita literalmente `(none)`. Tras los cambios: 93 tests web; en el RAK4631, 57 ajustes leídos, restauración sin
  rechazos y envío con claves sin que aparezcan.
- En los primeros segundos tras arrancar, una placa sin GPS contesta `gps` como `on`: un respaldo tomado ahí lo guarda
  y al restaurar el nodo rechaza `gps on`.

## Fuera, para pensar más adelante

- **Grabar al compilar** nombre, radio, posición y sala dentro del binario (rama `dmar-bcn`, que además fuerza CR 4/5).
  Solo aporta algo si fallan a la vez los ficheros y la copia de rescate, o tras un borrado junto al nodo.
