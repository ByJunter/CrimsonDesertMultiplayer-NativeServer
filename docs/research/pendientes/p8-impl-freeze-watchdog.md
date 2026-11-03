# P8 sesión 10: log por partida y vigilante de congelones (implementación)

10 de octubre de 2026, desde `opcion-d` `b728ec7`. Build del juego **1.0.0.2976** (base `0x140000000`, RVA). Parte de
[p8-s10.md](p8-s10.md) §2: el hilo principal del invitado se paró 2,35 s, 14,4 s y 1,67 s en una pelea y el log del
invitado de esa fase se perdió (sobrescrito). No se lanzó el juego ni se desplegó nada. **[V]** = comprobado (código,
tests); **[H]** = a medir en vivo.

## 1. Un log por partida (los dos papeles)

`log::open(dir + "cdmp.log")` (`src/mod/log.cpp`, nombres en `src/mod/log_names.h`):

- Cada partida escribe **`cdmp_AAAAMMDD-HHMMSS.log`** (hora local del arranque; si ya existe en ese segundo,
  `_2`, `_3`…). Nunca se sobrescribe una partida anterior.
- **`cdmp.log` es un enlace duro** al fichero de la partida actual: el mismo fichero con dos nombres, sin copia ni
  enlace simbólico. Quien lea `cdmp.log` (instrucciones del kit, scripts, Shadow) lee la partida actual. Al arrancar:
  - si `cdmp.log` es un enlace de una partida anterior, se quita solo ese nombre (su fichero fechado sigue);
  - si es un fichero normal (un mod anterior al 10-10), se renombra a `cdmp_<su hora de modificación>.log`.
- El fichero fechado se crea con `CREATE_NEW` probando `_2`, `_3`… mientras el nombre exista: nunca se trunca un
  fichero que ya estaba. Si no hay nombre posible, se **añade** a `cdmp.log` (sin truncar) y no hay alias. El
  `cdmp.log` viejo se renombra igual, sin pisar nada.
- Se guardan las **10 últimas** partidas fechadas. Solo se borran nombres exactamente `cdmp_DDDDDDDD-DDDDDD[_N].log`:
  nunca `cdmp_packets_*` ni los antiguos `cdmp.1.log`…`cdmp.5.log` (se quedan donde estén; ya no rotan). Nunca el de
  esta partida (aunque su hora ordene primero, p. ej. con el reloj atrasado) y nunca uno abierto por otro proceso:
  se borra abriéndolo con `DELETE`, **sin compartir** y `FILE_FLAG_DELETE_ON_CLOSE`; si otro lo tiene abierto (un
  juego anterior aún escribiendo), la apertura falla y se queda.
- La línea `Config: P8 session 10: freeze_watchdog … | log file <ruta del fechado>` dice qué fichero es.
- Sin enlaces duros (FAT) o si `cdmp.log` sigue abierto por otro proceso: no hay alias y el log es solo el fechado.
- Ojo: el Explorador puede mostrar `cdmp.log` con tamaño 0 o viejo mientras el juego escribe (los metadatos del
  directorio de un enlace duro se actualizan tarde); el contenido es el bueno. [V prueba sin juego]

## 2. `[session2] freeze_watchdog = 0 | 1` (los dos papeles, solo lectura)

`src/mod/session2/freeze_watchdog.{h,cpp}`, lógica pura en `freeze_watchdog_logic.h`, la muestra en `freeze_sample.h`.
Arranca en `mod.cpp` justo después de leer la configuración, **antes** de que ningún módulo instale ganchos.

### 2.1 Fuente de fotogramas

- **Bucle principal:** el gancho en línea de `ui_pause` en `0x35CD990` (lo llama el bucle principal una vez por
  vuelta; activo con `ui_pause_block ≠ 0`, que está a 1 en los cuatro `.ini`) y el gancho de `guest_seq_play` en
  `0x8B6144` (invitado). Cada uno llama a `freeze_watchdog_frame()`. **El primer hilo que los llama es el hilo
  principal** (`fwd::MainThread`, un CAS una vez); solo sus llamadas cuentan como fotogramas. Una llamada desde otro
  hilo no cuenta (podría tapar un congelón del principal) y se avisa (`a main-loop hook ran on T…`, ≤ 5 líneas, y en
  el resumen). Ese hilo es el que se muestrea.
- **Respaldo:** si ninguno corrió en 60 s, el contador de `Present` y el hilo de presentación (se dice una vez).

### 2.2 Episodio y muestras

- Un hilo del mod mira el contador cada 25 ms. Sin fotograma > **300 ms**: episodio (solo después de haber visto
  avanzar el contador: no en el arranque ni en la pantalla de título).
- **Muestra**: `SuspendThread`, `GetThreadContext`, una copia de ≤ 16 KB de su pila desde `rsp` (solo si
  `StackLimit ≤ rsp < StackBase`: páginas comprometidas) a un búfer estático del mod, `ResumeThread`. **Nada más con el
  hilo suspendido**: sin asignar, sin candados, sin log, sin SEH (el hilo puede tener el candado del heap o del
  cargador). Lo demás se hace después, ya reanudado. Medido en el test: el hilo sigue corriendo.
- 5 muestras a 50 ms, luego 1 por segundo. Los topes son de **suspensiones** (se miran antes de suspender): ≤ 20 por
  episodio y ≤ 600 por sesión; pasado el de sesión, los episodios solo se cronometran (líneas de fin, ≤ 300).
- La lista de módulos y las regiones de stubs se leen al arrancar y se refrescan cada 30 s **solo entre episodios**,
  nunca con el juego congelado. (`K32EnumProcessModules` lee la lista del cargador con `ReadProcessMemory`, sin su
  candado [E]; aun así queda fuera de los episodios.)
- **Línea de muestra** (`WRN`):
  `FreezeWatch #E.k +T ms T<tid> (main loop): rip <módulo+RVA> rsp 0x… (N B of stack copied, suspended X us) | returns:
  exe+0x…, cdmp+0x… [mod], 0x… [stub], … | IN mod hook '<módulo> <punto>' for N ms | MOD CODE on this stack`
  - `rip`: con nombre de módulo (`ntdll.dll+0x…` si espera un candado).
  - `returns`: los primeros 16 valores de la pila que caen en el exe, en `cdmp.asi` o en memoria privada ejecutable
    (trampolines y stubs de safetyhook) **y** tienen una instrucción `call` justo antes (`is_call_before`: `E8`, y
    todas las formas `FF /2` con o sin REX). Es un barrido heurístico de la pila, no un desenrollado: algún valor
    puede ser un resto; el `rip` y los primeros suelen bastar.
  - Gancho: **cada mid hook instalado con `p8_install_mid`** (casi todos los de P8: `guest_x_veto` V1/limpieza/
    cámara, `guest_seq_route`, `guest_sched_load`, `guest_seq_play`, `remote_interp`, …) va envuelto, si
    `freeze_watchdog=1`, en una función que marca el hilo con el nombre del gancho mientras corre. También los
    desvíos en línea de V0 (`guest_x_veto`, incluye el original del juego) y de `ui_pause`. La línea dice en qué
    gancho está el hilo y desde cuándo, o cuál fue el último y hace cuánto salió.
- **Fin**: `FreezeWatch #E END: T<tid> (main loop) had no frame for N ms; K sample(s) | rips: exe+0x… x3, … | mod code
  was on its stack`.
- Cada 10 min un resumen (`FreezeWatch summary`).

### 2.3 Coste y riesgos

- Por fotograma: dos escrituras relajadas. Por llamada a un mid hook envuelto: un acceso TLS y cuatro escrituras
  relajadas (`GetTickCount64` ×2). Con `freeze_watchdog=0` no se envuelve nada y los ganchos quedan como antes.
- El hilo principal se suspende microsegundos por muestra, y solo cuando ya lleva > 300 ms parado.
- Cada 30 s fuera de episodios se recorre el mapa de memoria (`VirtualQuery`) para localizar los stubs: decenas de
  ms en el hilo del mod. Un stub creado después del último refresco sale como dirección sin módulo.
- La marca del gancho es RAII (`fwd::MarkScope`): se deja también si una excepción C++ atraviesa el gancho.
- Las cargas largas también darán episodios (el bucle principal no avanza); útiles igual, con tope de líneas.
- Si un gancho termina por una excepción manejada más arriba, su marca puede quedarse puesta (la línea diría "IN").

## 3. `.ini`

`freeze_watchdog=1` en `cdmp.p8-s7-host.ini`, `cdmp.p8-s7-shadow.ini`, `cdmp.p8-s7-shadow-fase2.ini` y
`cdmp.p8-noche-local.ini`.

## 4. Tests [V]

`.\scripts\build.ps1`: 40/40.
- `optd_freeze_watchdog` (nuevo): la máquina de episodios (arranque, 300 ms, cadencia 50 ms ×5 y luego 1 s, topes
  de suspensiones por episodio y por sesión, fin con su duración); `is_call_before` con 12 codificaciones reales de
  `call` y 5 que no lo son; las marcas por hilo (anidado, 80 hilos sobre 64 huecos, RAII con una excepción); el hilo
  principal (el primero gana, los demás no cuentan, 8 hilos a la vez); y **la muestra sobre un hilo real** del test
  girando en el exe (rip en el exe, copia de la pila, al menos una dirección de retorno encontrada, el hilo sigue).
- `optd_log_ring`: nombres fechados, `_2` en el mismo segundo, el `cdmp.log` viejo conservado, poda a 10 sin tocar
  `cdmp_packets_*` ni `cdmp.1.log` ni **un fichero abierto por otro** (se queda), nunca el de esta partida aunque
  ordene primero, `cdmp.log` = mismo contenido que el fechado, reapertura con fichero nuevo.
- Prueba sin juego `cdmp_host` dos veces: dos ficheros fechados, el `cdmp.log` anterior conservado, `cdmp.log`
  enlazado a la última partida (`fsutil hardlink list`).

## 5. Qué mirar en la próxima prueba

En Shadow, durante la pelea del campamento: `FreezeWatch #N.k` (¿`IN mod hook 'GuestXVeto …'`? ¿`rip` en `ntdll` =
espera de candado? ¿`[mod]`/`[stub]` en la pila?) y `FreezeWatch #N END` con la duración, contra los huecos C>S de la
captura. Con eso se decide el A/B de p8-s10.md §2.5.3.
