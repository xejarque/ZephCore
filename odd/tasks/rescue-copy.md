# Copia de rescate para repetidor y room server

Decisión y razones: `docs/adr/0021-rescue-copy-in-settings-nvs.md`. Estado: aprobado el 2026-11-26, en implementación.

## Objetivo

Tras perder LittleFS (o un fichero), el nodo conserva el control remoto y sigue repitiendo MeshCore y Meshtastic como
antes.

## Specs

- **S1.** Se copian identidad, `prefs.json` y `regions2` en la NVS de settings. Sin particiones nuevas. ACL fuera.
- **S2.** Solo repetidor y room server, donde el backend de settings es NVS (nRF52, ESP32). Solo Zephtacore.
- **S3.** La copia se crea sola en un nodo ya configurado y se actualiza con los cambios del administrador.
- **S4.** Una entrada válida nunca se sustituye al arrancar.
- **S5.** El formateo automático mantiene sus disparadores; solo deja de borrar una copia válida.
- **S6.** `get z.rescue` informa por CLI remota. `erase` y el formateador lo borran todo.

## Arranque (`server_main_common.cpp`)

1. Abrir la NVS. Si el backend no arranca, borrar `storage_partition` una vez y reintentar.
2. Clasificar la copia: válida, ausente, rota o error de lectura.
3. `hasRoleData()` falso: formatear LittleFS y QSPI como hoy; la NVS se vacía (a través de NVS, no borrando la
   partición bajo el backend montado) salvo que tenga una entrada válida o diera error de lectura.
4. Antes de cargar nada, por cada fichero: si falta o no carga y su entrada es válida, reponerlo y anotar la
   restauración; si carga y su entrada está ausente o rota, escribirla. Con error de lectura de la NVS o una
   reposición fallida no se escribe en ese arranque.
5. Cargar identidad, ajustes y regiones como hoy. Si un fichero no se pudo reponer, identidad y ajustes se cargan
   en RAM desde la copia.

## Después

- Al terminar un comando de administración (`RepeaterMesh::handleCommand`, `RoomServerMesh::handleCommand`, por USB o
  remoto): escribir las entradas cuyo fichero cambió.
- `erase`: borra todo y deja la copia desactivada hasta el reinicio.

## Piezas

| Pieza | Fichero | Nota |
| --- | --- | --- |
| Reglas puras: secuencia de la copia (`rescue::Copy`), troceado y CRC | `zephcore/helpers/RescuePolicy.h` | nuevo, con tests de host |
| Almacén sobre settings/NVS y acceso a los ficheros | `zephcore/adapters/datastore/RescueCopy.{h,cpp}` | nuevo; trabaja por trozos de 1 KB, sin heap |
| Secuencia de arranque | `zephcore/src/server_main_common.cpp` | |
| Conservar NVS al formatear | `zephcore/adapters/datastore/ZephyrFsFormat.{h,c}` | parámetro; el resto de llamadas igual que hoy |
| Desarmar tras `erase` | `zephcore/app/RepeaterDataStore.cpp` | |
| Escritura tras comando | `zephcore/app/RepeaterMesh.cpp`, `RoomServerMesh.cpp` | |
| `get z.rescue` | `zephcore/helpers/CommonCLI.{h,cpp}` | ADR 0014 |
| Capacidad | `zephcore/Kconfig`, `zephcore/CMakeLists.txt` | `CONFIG_ZEPHTACORE_RESCUE_COPY` |
| Docs | `docs/ARCHITECTURE.md`, `docs/CLI_commands.md`, notas de versión | misma PR |

## Tasks

Una unidad revisable por tarea, con commit propio en rama de trabajo desde `fork/master`.

- [x] **T1.** `RescuePolicy.h` y sus tests (`tests/unit/rescue_tests.cpp`, `tests/catalog.json`).
- [x] **T2.** Almacén NVS y capacidad Kconfig.
- [x] **T3.** Secuencia de arranque, conservación al formatear y desarmado tras `erase`.
- [x] **T4.** Escritura tras comando y `get z.rescue`.
- [x] **T5.** Documentación y notas de versión.
- [x] **T6.** Compilaciones y medida de flash y RAM.
- [x] **T7.** Pruebas en banco (RAK4631, 2026-11-26; ver «Resultado»).

## Tests de host

- Fichero correcto: no se restaura. Falta o no carga, con entrada válida: se restaura. Entrada rota o ausente: no.
- Al arrancar solo se escriben entradas ausentes o rotas; una válida no cambia aunque el fichero difiera.
- Tras un comando solo se escriben los ficheros que cambiaron.
- Tras `erase` no se escribe nada.
- Troceado de ida y vuelta; escritura interrumpida o CRC incorrecto dejan la entrada rota.
- Error de lectura de la NVS: se conserva al formatear y no se escribe.
- Contador de restauraciones; sin hora válida la fecha queda desconocida.

El arnés de host no compila `app/` ni `CommonCLI.cpp` (QA-3): el cableado se verifica en banco.

## Compilaciones

`rak4631` y `gat562_30s` repetidor, un room server nRF52, un repetidor ESP32 (`heltec_wifi_lora32_v3`), un companion
(no debe cambiar) y una placa sin NVS (el código no entra).

## Banco (otra nRF52, antes de tocar DMAR)

| # | Prueba | Debe pasar |
| --- | --- | --- |
| B1 | Actualizar un repetidor ya configurado | La copia se crea sola |
| B2 | Ensuciar toda la partición LittleFS con un firmware de prueba de un solo uso (sin commit) | Misma clave, login remoto, radio, regiones, `z.mt.*` y zero-hop; repite MeshCore y Meshtastic |
| B3 | Dañar solo `prefs.json`, solo la identidad, solo `regions2` | Se repone solo ese fichero |
| B4 | Cortar la alimentación durante la restauración, varias veces | Acaba restaurado |
| B5 | Cortar justo tras un `set` | El siguiente arranque deja la copia correcta |
| B6 | Actualizar por UF2 | La copia sigue |
| B7 | Pasar a ZephCore original y volver | La copia sigue |
| B8 | `erase`; formateador | Copia borrada |
| B9 | Basura en `storage_partition` | Arranca sin colgarse y recrea la copia |
| B10 | Cambiar entre repetidor y room server; flashear companion | La copia sigue; con companion desaparece |

## Resultado de las compilaciones

Contra `f6d8e4f9`, sin avisos nuevos.

| Build | Flash | RAM | Copia |
| --- | --- | --- | --- |
| `rak4631` repetidor | +8004 B (312400) | +1152 B (88292) | sí |
| `rak4631` room server | +8008 B (285468) | +1152 B (86436) | sí |
| `heltec_wifi_lora32_v3` repetidor | irom +8272 B, drom +464 B | dram +1184 B | sí |
| `rak4631` companion | +188 B (CLI común) | 0 | no |
| `gat562_30s` repetidor | 315436 B | 89060 B | sí |
| `xiao_nrf54l15` repetidor, `rak4631` KISS, `xiao_esp32s3` observer | solo compilados | | no |

## Resultado del banco

RAK4631 por USB, radio fuera de la red real (869.85 MHz). Los fallos se forzaron con un firmware de un solo uso, sin
commit, que ensucia las particiones y reinicia tras una escritura concreta.

| # | Resultado |
| --- | --- |
| B1 | Pasa. |
| B2 | Pasa: clave, radio, regiones, `z.mt.*` y zero-hop idénticos. Login remoto y repetición en el aire **sin probar**. |
| B3 | Pasa: cada daño suma una sola restauración. |
| B4 | Pasa con reinicio por software; corte físico de alimentación **sin probar**. |
| B5 | Pasa con reinicio por software, en `set` y `region save`. |
| B6 | Pasa. |
| B7 | Pasa: la copia sigue. Si en ZephCore original se guarda un ajuste, su `prefs.json` pierde `z.mt.*`. |
| B8 | Pasa con `erase` y con el formateador. |
| B9 | Pasa con bytes aleatorios, ceros y bytes de LittleFS: arranca y recrea la copia. |
| B10 | Pasa: room server conserva, companion borra. |

ESP32 sin probar en hardware.

## DMAR

Hoy: guardar su clave (`get prv.key` por USB). Actualizarlo solo con B1–B9 superadas. El salto desde ZephCore 1.17.7
activa además el puente Meshtastic y deja el auto-apagado desactivado: revisar `z.autoshutdown` al actualizar.

## Fuera de alcance

Formateo automático (PERS-8), ACL, companion, observer, KISS, MG24, nRF54L, STM32WL, builder web (tarea aparte) y
propuesta a upstream. ESP32 queda compilado y sin probar en hardware.
