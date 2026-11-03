# RPT-0: informes de error del juego y cómo contenerlos

**Estado:** elegido K1 + K2 (D-37). K1 programado (`report_guard`); K2 aplicado
(`scripts/crashpad.ps1`). Falta RPT-1.

Auditoría del 2026-10-08, build 1.0.0.2976 (PE `0x6AB28F00`). Estática y de solo lectura: no se
abrió el juego ni se provocó ningún fallo. Detalle con direcciones e instrucciones:
[data/rpt0-estatico.md](data/rpt0-estatico.md) (ejecutable) y
[data/rpt0-sentry-native.md](data/rpt0-sentry-native.md) (código de sentry-native 0.12.5). Parte de
[vision-a7-transversal.md](vision-a7-transversal.md) §4, que pedía esta puerta antes de E1.

## 1. Qué hay en este PC (observado, solo lectura)

- Caché de Sentry/Crashpad en `%LOCALAPPDATA%\Pearl Abyss\DumpCache`: `settings.dat` con subidas
  **activadas**, `reports/` y `attachments/` **vacíos**, una carpeta `<uuid>.run` por partida en
  curso.
- **Seis cierres inesperados enviados**, 3 y 4 de octubre (pruebas del cooperativo de `main`). Los
  logs del juego (`%LOCALAPPDATA%\Pearl Abyss\log\Launcher_*.log`) dicen en cada uno
  `[Crash] 덤프 보내는 중...` ("enviando volcado"). El último (2026-10-04 07:48:54) dejó
  `DumpCache\last_crash`, una captura `temp\dump_screenshot.png` y un intento de subida 2 s después
  (`settings.dat`, último intento 13:48:56Z). Como `reports/` está vacío, lo más probable es que
  se subieran.
- **Ningún error de guardado** en los 105 logs del juego: el informe de guardado fallido nunca se
  ha disparado.
- Lo que identifica al PC en cada evento (`__sentry-event`): `SessionUid` (un hash, que **no es** el
  SteamID), CPU, GPU, placa base, BIOS, versión de Windows, BuildKey/CodeRev. La sesión
  (`session.json`) no se envía al arrancar; sale con el primer error, al cerrar o en el siguiente
  arranque.

## 2. Las rutas que pueden adjuntar archivos (ejecutable, demostrado)

Todo está en `.rsrc`, no en `.xtls`. El interruptor "informes activos" es **un byte** al que se
llega por `[M+0x1988]` (M es un singleton; su accesor `0x1318E70` salta a `.xtls`, así que no se
llama). Solo se pone a 1 en `0x131623B`, tras `sentry_init == 0`, y solo se pone a 0 justo antes de
cada `sentry_close` (`0x131C298`, `0x13250AA`).

| Ruta | Dónde | Qué adjunta | Cómo sale |
|---|---|---|---|
| **Guardado fallido** | `.xtls` (carga/validación/guardado/borrado) → `0x2356270` → `0x1317D40` | Los `*.save` de la **ranura que falló** + **el log de texto del juego** (`0x13003D0`: el primer log que no es `.json`, que contiene `saveRootPath` con tu usuario de Windows y el número de la carpeta de Steam) | Máximo 3 por partida (contador `0x6D9CCE8`). `sentry_scope_attach_filew` (`0x1317E24`) + `sentry_capture_event_with_scope` (`0x1317E91`) |
| **"sendMessageEvent"** | `0x33119D7` → `0x1317D40` | La lista que reciba (sin identificar qué la dispara) | Igual que la anterior |
| **Cierre inesperado** | `on_crash` / minidump → `0x1320E00` → `sentry_attach_filew` (ámbito global) | Archivos del volcado (nombres calculados, sin fijar en estático; la captura de pantalla casi seguro) | El volcado lo sube **`crashpad_handler.exe`**, otro proceso |

`0x1317D40` devuelve void, sus llamadores no usan `rax`, y lo primero que hace es `cmp byte
[rcx],0 / je salida` (`0x1317D60`): **salir sin hacer nada es un camino que el propio juego ya
usa** cuando Sentry no está activo. Comprobado a mano en el ejecutable.

El juego **no** usa `before_send`, consentimiento, `add_attachment` al iniciar ni transporte
propio: no hay ningún ajuste oficial para apagar los informes.

## 3. Cómo se comporta sentry-native 0.12.5 (código fuente)

- Los adjuntos de un evento se **leen del disco al capturarlo**, en el hilo del juego
  (`_wfopen`), y se copian al envelope. Si no se pueden abrir, el evento se envía igual, sin
  ellos.
- **Cortar internet no basta.** Un envío fallido se pierde (no hay reintento), pero lo que siga en
  cola al cerrar se guarda en `<uuid>.run\*.envelope`, con los adjuntos, y **se reenvía al
  siguiente arranque**. Crashpad reintenta los volcados pendientes hasta 5 veces (cada 15 min) y
  los borra a los 2 días.
- `settings.dat` se reescribe con subidas activadas en cada arranque: editarlo a mano no sirve.
- `sentry_close` **no** desinstala el manejador de Crashpad.
- **Sin `crashpad_handler.exe`, `sentry_init` falla y no se envía nada** (ni eventos ni volcados).
  El juego lo tiene previsto: comprueba si el archivo existe (`GetFileAttributesW`, `0x131602D`) y
  si `sentry_init` falla (`0x131622E`) libera un búfer y **sigue** (`0x1317B37` → `0x1316140`), con
  el byte de informes a 0.

## 4. Opciones de contención

**K1. El mod corta la ruta del guardado fallido** (código nuevo, mi parte). Hook en `0x1317D40`
(`.rsrc`; cabecera `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57`): mientras el guardián esté
activo, **sale sin hacer nada**, igual que con Sentry apagado, y apunta en `cdmp.log` el motivo
(`rdx`) y los adjuntos que se habrían pedido. Cubre las rutas 1 y 2, que son las que adjuntan
partidas. Además nos dice si E1 habría generado un informe, sin enviarlo. **No cubre** los cierres
inesperados.

**K2. Apartar `crashpad_handler.exe` durante el bloque B** (entorno, decisión tuya). Se mueve a
`backups\` y se devuelve después. Sentry no arranca y **no sale nada**: ni guardados ni volcados
ni sesiones. Steam lo repone si se verifican los archivos. Antes hay que comprobar en una partida
sin guardián (prueba **RPT-1**) que el juego arranca y va normal así, y que `Launcher_*.log` no
dice nada raro. (Si `sentry_init` llega a crear la carpeta `<uuid>.run` antes de fallar no está
fijado; lo que importa es que no aparezcan informes ni `*.envelope`.)

**K3. Bloquear la salida de `CrimsonDesert.exe` y `crashpad_handler.exe` en el cortafuegos**
(sistema, lo haces tú). Funciona mientras dure el bloqueo, pero lo que quede en cola se envía al
quitarlo: habría que vaciar `DumpCache\reports` y los `*.envelope` antes. Más frágil que K2.

**K4. Partidas de laboratorio** (lo de la síntesis): durante el bloque B, en la carpeta de partidas
solo hay copias. Con K1 + K2 ya no hace falta para evitar envíos, pero sigue siendo prudente
contra fallos del propio guardián. La copia de seguridad ya está hecha.

**Recomendación: K1 + K2**, con la copia de partidas como siempre. K1 se queda en el mod para
siempre; K2 solo durante el bloque B. K3 solo si no quieres tocar los archivos del juego.

## 5. Para cerrar RPT-0

1. Tú eliges la contención (recomendado K1 + K2).
2. Yo programo K1, con tests y revisión.
3. **RPT-1** (si eliges K2): partida normal con `crashpad_handler.exe` apartado, solo la sonda. Se
   mira que el juego arranca y va normal y que en `DumpCache` no aparecen informes ni
   `*.envelope`.
4. Después de cada prueba del bloque B: comprobar que `DumpCache\reports` y `attachments` siguen
   vacíos y que no hay `*.envelope`.
