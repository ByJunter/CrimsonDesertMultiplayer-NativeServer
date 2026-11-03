# S28: lo que midió la sonda U-0 (`user2_stages=probe`) y qué cambia en la opción U

Solo análisis. Build 1.0.0.2976.

Datos:
- Anfitrión: `p8-sesion28-host\cdmp_20261013-053042.log` (paso 1) y `…-055640.log` (paso 2), con sus capturas.
- Invitado: `s28-shadow-p1\cdmp_packets_20261013-043307.bin`. El reloj de Shadow va 1 h por detrás.
- Scripts y extractos en `scratchpad\s28_u0\`:
  - `u2s_*.txt`: las líneas de la sonda;
  - `states.py`: los estados;
  - `deletes.py`: 2335/2126 por intervalo;
  - `travellers.py`: nacimientos junto al invitado.

## 1. Respuestas de §9.3

| Pregunta | Resultado | Evidencia |
|---|---|---|
| Ganchos | Entraron los 9 (C D L U N T S F P) | `05:30:54 User2Stages (…): C … hooked \| … P … hooked` |
| Estado del gestor del usuario 2 en el campo del anfitrión | **Estado 0** (reiniciado), **no 1** como se predijo: listas a 0, última celda (0,0), `pending 2045 = 0`, `pending 2046 = 0`. Igual en los 3 logins (A0100336, A01002D2, A010110D) | `states.py`: en su campo inicial pasa a `st2 p45=1 p46=1` (preparación con sujeto usuario 2, opcode 2549); unos 20 s después, `st0 p45=0 p46=0`, y así sigue en el campo del anfitrión |
| ¿Cadena 2045 bloqueada? | **No**: los contadores vuelven a 0 al reiniciarse. Cuando el gestor corra, las cadenas 2045/2046 arrancan de forma nativa (`0x15025C310` / `0x101FBFA0` programan si el contador es 0) | `p45=0 p46=0` desde 05:34:19 |
| Puerta `0x288B87A` | El gestor del usuario 2 solo entra 3 veces, en su campo inicial (dueño). En el del anfitrión, nunca | `ShopProbe summary … 0xA0100336 … updates 3, gate 2, owner 2` |
| 2043/2045/2046 | El 2043 con sujeto usuario 2 llega al **gestor del anfitrión**: 24 veces (paso 1) y 55 (paso 2); al suyo, solo 2/4 veces en su campo inicial. Los 37/76 de 2045/2046 del usuario 2 son todos en su campo inicial; después ninguno, porque la cadena se corta con el reinicio | `timers … 2043: [subject user 2: host 24 user2 2]`; `[timer]` a 0.6 s entre 05:33:56 y 05:34:17, y luego nada |
| Cierres en el viaje rápido del anfitrión | Los 84 cierres son **motivo 19**, del anfitrión, en sus dos cargas: 34 a las 05:42:09 y 50 a las 05:42:49. Se cerraron 163 entradas de NPC: es lo que vio el invitado (Ctrl+F12). Llamador `exe+0x1022602E` (DeleteStage `.xtls`) ← `exe+0x287D8D5` (la descarga). No hubo ni una descarga del gestor del usuario 2 | `closes (C): [reason 19 host from exe+0x1022602E] x84`, `DeleteStage (D): [reason 19 host from exe+0x287D8D5] x84`, `owner loads host 2 / user 2 0` |
| Motivo 30 (cierre por ventana) | **No pasa ni por C ni por D**. En la cabalgata (05:39–05:42), la captura tiene unos 250 × 2126 al cliente del anfitrión y su gestor de horarios baja de 54 a 32 stages, pero no hay ningún `[close]` / `[delete]`. Es otro camino, sin identificar. En este paseo no se fueron los NPC del invitado (nota del usuario) | `deletes_p1.txt` frente a `u2s_053042.txt` |
| Distancia y celdas | Funciona, también con el anfitrión montado: la cabalgata sube de 0 a 12 celdas (≈ 770 m) | `[state]` 05:39:54 → 05:41:59 |
| `would ARM` / `DRAIN` | Paso 1, tres ARM: (1) **falso**, 05:33:49, a 168 celdas con el usuario 2 aún en **su propio campo**; (2) **real**, 05:41:54, a 11 celdas (la cabalgata); (3) 05:43:34, a 42 celdas (viaje del invitado a la ciudad). Cada uno con su DRAIN | líneas `[would ARM]` / `[would DRAIN]` |
| Duplicados `[create]` | 0 de 382 comprobadas. No dice nada: el gestor del usuario 2 no tenía stages. El «dato» que se lee es el hash de recurso del 2335 (`+9`, p. ej. `0xAC00B3BB`) | `same data alive there 0` |
| Stages del anfitrión junto al usuario 2 | **No vale**: el desplazamiento `+0x90` de la stage lee `(1, 1)` (es la escala, no la posición). Tampoco vale `active` (`+0x74` = 52065) | `[close] … stage at (1, 1)`, `act52065` |
| **Revivir del usuario 2 (paso 2)** | El `ReviveLoadingReq 2362` del usuario 2 **pone el gestor del anfitrión en estado 0**. El anfitrión deja de evaluar stages el resto del paso: 0 creaciones después de las 06:05:04 y 130 de 237 actualizaciones salen antes de la puerta | `06:05:04 ERR TravelRevive … host manager +0x6A0 2 -> 0 … HOST TOUCHED`; `states.py`: host `st0` desde 06:05:08; ShopProbe `updates 237, gate 107` |

## 2. Viajeros: por qué cargan a 400 m del anfitrión

- **Sistema:** `AutoSpawnRoadReq 2550`, manejador `0x29C4930` → `0x2C41E50` → `.xtls 0x10880460` → `0x27E8C90`.
  - Es una Req **del cliente**: cada entrada de 0x18 bytes lleva un id, campos y la **posición** de un punto de camino
    (decompilado).
  - El cliente pide spawns de camino alrededor de **su** personaje, y el servidor los atiende sin mirar al dueño del
    campo. No pasa por `ServerSequencerStageManager` ni por la puerta.
  - En cambio, los aldeanos de pueblo salen de las stages de horario (`0x28078E0`), que crea el gestor de stages del
    dueño detrás de la puerta.
- **Confirmado con S28** (captura del invitado, `travellers_p1.txt`):
  - con el anfitrión a 340–940 m, nacen personas (tipo 3) a 60–98 m del invitado **en grupos de 2 a 4**, sin
    ningún 2424 (pool) en los 39–116 s anteriores;
  - el invitado manda unos 52 × 2550 cada 20 s durante toda la sesión. Ejemplos: `B0101582/157E` (t 442,9, anfitrión
    a 406–418 m), `B01016FB–FE` (t 457,9, a 496 m), `B0101877–7A` (t 478,0, a 620 m);
  - en ese tramo no llega ningún 2335 junto al invitado: los 46 por intervalo están junto al anfitrión.
- **¿Modelo o pieza?** Es el **modelo** de la opción U: la población se crea por solicitante, alrededor de quien la
  pide. No es una pieza que se pueda reutilizar: los viajeros de camino son otro sistema, no crea horarios, tiendas ni
  escenas, y no puede crear stages.
- **Dato útil para U-e:** esos viajeros nacen con controlador = anfitrión (`control-npc-lejos.md`) y aun así se ven
  animados a 400–940 m.
  - La IA lejana del cliente del anfitrión vale para recién nacidos lejos. Lo medido llegaba a 640 m; esto lo alarga a
    ~940 m.
  - Así que **U-e (controlador al nacer) baja de requisito a opcional**, y hay que medirlo en U-1.

## 3. Veredicto sobre la opción U

**Confirmada en lo esencial y modificada en cinco puntos.**
- Confirmado:
  - C1: la carga del anfitrión solo descarga **su** gestor (motivo 19, `0x287DA50` → `0x287D4C0`);
  - C2: el gestor del usuario 2 no hace nada en el campo del anfitrión;
  - el 2043 del usuario 2 va al gestor del anfitrión (U-b hace falta).
- Cambios:
  1. **U-c (armado):** el gestor está en **estado 0** (reiniciado) y con contadores a 0.
     - Armar desde 0 es el caso nativo del login (`0x287EF60` sobre estado 0).
     - El armado mínimo (`0x287F450` → estado 2, más el 2043 periódico) vale, pero `arm_verdict` tiene que aceptar
       el estado 0 (y el 1).
     - No hace falta desbloquear las cadenas 2045/2046.
  2. **«Mismo campo»:** U solo actúa con los dos en el mismo `ServerField`; el ARM falso a 168 celdas lo demuestra. Hay
     que añadirlo a la máquina y a la sonda.
  3. **Fallo previo nuevo, que va antes de U («F4»):**
     - el 2362 del usuario 2 resetea el gestor del anfitrión. Es la misma vf48 que F3 corrige para el 2283: el setter
       `0x2BC4950`, retorno `0x2BC4C53`, pone `[mgr+0x6A0] = 0` en el gestor que da vf48, el del dueño;
     - con U encendido, además, se mezclarían los dos gestores;
     - F3 tiene que cubrir también el 2362 (y el 2437 de recarga): devolver el gestor del usuario 2 en esa vf48.
  4. **Sonda:** corregir la posición de la stage (`+0x90` es la escala; buscar la posición entre `+0x9C` y `+0xAC`,
     contrastando con el `+30` del 2335) y quitar `active` (`+0x74`). Sin eso no hay solape en vivo para fijar los
     umbrales.
  5. **Cierre por ventana (motivo 30):** no pasa por C ni por D.
     - No cambia U-d (el vaciado usa la descarga nativa, que sí está medida).
     - Para medir el traspaso hace falta otro punto: `ServerActor::Logout 0x2068340` con la dirección de retorno y
       motivo 30, solo lectura.

## 4. Orden de implementación siguiente

1. **F4** (`user2_fast_travel` extendido; `0` | `probe` | `1`): en la vf48 de `0x2BC4C53` durante el 2362/2437 del
   usuario 2, su gestor.
   - Abortar si aparece `HOST TOUCHED`, si el estado del anfitrión cambia fuera de sus cargas o si el usuario 2 no
     revive bien.
   - Se prueba en un PC con `cdmp_peer` y un 2362 de captura.
2. **U-0b** (sonda): «mismo campo», la posición de la stage corregida, el punto de Logout para el motivo 30, y quitar
   `active`.
3. **U-1** (`user2_stages = probe | 1`), con solo los elementos de horario:
   - U-a puerta;
   - U-b vf48 para 2043/2045/2046, mientras dure el `ExecuteTroc` del temporizador del usuario 2;
   - U-c armado mínimo desde estado 0 o 1 con listas vacías;
   - U-d vaciado: `0x287DA50(mgr2)` al apagar, en su 2437/2283/2362 y antes de su 2001;
   - U-f solo elementos de horario;
   - U-h sumidero en el cliente del anfitrión, solo contando en `probe`.
   - En `probe`: «would arm / drain / route», sin llamar a nada.
   - **Criterios para abortar U-1** (volver a `probe`):
     - cualquier `HOST TOUCHED`, o el estado del gestor del anfitrión distinto de 2 fuera de sus cargas;
     - `mgr2` no llega a estado 2 en 5 s tras armar, o su `[timer] 2043` no llega a `mgr2`;
     - `[create]` duplicados con el anfitrión a ≥ 8 celdas;
     - `mgr2` prepara algún elemento que no sea de horario (U-f roto);
     - un 2322 inesperado al invitado;
     - cambia el ritmo del 2043 del anfitrión;
     - cualquier ERR o crash.
   - **Prueba en un PC** con `cdmp_peer` quieto: el anfitrión se aleja más de 11 celdas y se esperan `ARM`, el estado
     2 de `mgr2` y 2335 con dueño = el peer en `--record`. Después el anfitrión viaja: ningún cierre de stages del peer.
     Al volver a menos de 7 celdas: `DRAIN` y `0x287D4C0(mgr2)` con sus 2126.
   - **Con Shadow:** NPC de pueblo junto al invitado con el anfitrión lejos, que sobrevivan al viaje del anfitrión, la
     ciudad lejana con NPC, y la IA de esos NPC (U-e solo si no se mueven).
4. **U2** (tiendas): ver §5.

## 5. Tiendas

- Las escenas `cd_seq_funcnpc_*` las prepara la misma actualización con puerta (`0x288A970`, P de ShopProbe). En S28,
  todas con dueño `A0100001` (FuncnpcTwin: «would create» con el gestor del usuario 2 en estado 0).
- Con U-1 encendido, en una ciudad lejos del anfitrión, el gestor del usuario 2 prepararía **sus propias** tiendas:
  2661 con su personaje → M del invitado de forma nativa, y el tendero, que es un personaje único, protegido por el
  registro del campo.
  - Eso hace **innecesaria la gemela S2 lejos del anfitrión**.
- Pero sus Req de escena (2436/2591/2704/2671…) acaban hoy en el gestor del anfitrión por vf48: hace falta **U2**
  (rutar por id de secuenciador a `mgr2`) para que la tienda se abra.
- Cerca del anfitrión, con U apagado, sigue siendo el caso S2 (gemela) / C1 (redirección del cliente).
- Por eso, en U-1, `elements = schedule`; `schedule+funcnpc` solo cuando esté U2.
- **U2: diseño (casos A lejos y B cerca del anfitrión) y paso 1 implementado (caso A, `user2_shop_route` y
  `user2_stages_elements`), con la prueba en un PC: `tiendas-u2.md`.**

## 6. Implementado: F4 (`user2_revive_manager`)

Clave nueva `[session2] user2_revive_manager = 0 | probe | 1`, solo en el anfitrión.
- Es **aparte de la de F3** (`user2_fast_travel`) porque esa clave también manda en F2: con F3 en algo distinto de 1, se
  descarta todo 2283. Mezclarlas obligaría a probar el revivir y el viaje rápido a la vez.
- Código: `fast_travel_logic.h` (`decide_f4`, con tests en `optd_fast_travel_tests.cpp`) y `user2_funnels.cpp`
  (`revive_apply_vf48`, el mismo gancho de entrada de `ServerField::vf48 0x2B00CE0` que usa F3).
- No se parchea nada nuevo: solo se compara el sitio de la llamada `0x2BC4C49–0x2BC4C80`.

### Camino comprobado con Ghidra y capstone

- `ReviveLoadingReq 2362` → `0x298C490` → `.xtls 0x1048FFC0` → `ReviveLoading_SetPendingReload 0x2947E00`.
- Esta llama al fijador `TransformSync::SetReloadDestination 0x2BC4950` desde seis sitios (`0x2948313`, `0x2948850`,
  `0x29489A5`, `0x2948CF0`, `0x2948FB0`, `0x2949728`).
- Dentro del fijador: `0x2BC4C49 mov rcx,[rbp-0x21] ; mov rax,[rcx] ; call [rax+0x48]` (retorno `0x2BC4C53`), sobre el
  campo del que viaja. Después solo una referencia sobre `[mgr+8]` y `mov byte [rbx+0x6A0],0` en `0x2BC4C80`.
- Con F4=1, dentro del 2362 del usuario 2 y en el campo compartido, esa vf48 devuelve el gestor **propio** del usuario
  2, con la vtable `0x5B17A58` y `[mgr+0x18]` = su personaje comprobados.
  - Si no se puede leer, se devuelve el original con un ERR. Nunca se devuelve un bloque, porque se toma referencia
    sobre `[mgr+8]`.
- **El 2437 de recarga:** en la S28, con los cuatro 2437 del usuario 2 el gestor del anfitrión siguió en estado 2, así
  que ese camino no pasa por aquí. Cualquier otra Req del usuario 2 que llegue a esta vf48 sin F3 se cuenta y se
  registra (`another Req of user 2 at the setter's vf48`), pero no se cambia.
- **El detector:** `TravelRevive …` (con `user2_travel_probe=1`) termina ahora en `| F4: <qué hizo>`: `F4 ACTED: user
  2's own manager`, `probe: the ORIGINAL…`, o `the setter's vf48 was not reached…`.

### Ini

- `docs/research/ini/cdmp.p8-s29-host-paso1.ini`: la S28 paso 1 con `user2_revive_manager=probe`.
- `docs/research/ini/cdmp.p8-s29-host-paso2.ini`: la S28 paso 2 con `user2_revive_manager=1`.

### Prueba en un PC (anfitrión + `cdmp_peer`)

- **Lo que un PC no puede probar:** un peer vivo que manda una 2362 es rechazado por el servidor (§11.1 de
  `reaparicion.md`). No se sabe si antes llega al fijador. El caso real (el invitado muere y revive) **necesita
  Shadow**.
- **Sí se puede comprobar en un PC:** que F4 se instala, que no rompe F3 y que la 2362 del peer no toca al anfitrión.
  1. Con `cdmp.p8-s29-host-paso2.ini` en el anfitrión, la regresión del viaje (F3 no cambia):
     `.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00_travel_s14_b.troc
     --var ACCOUNT=1 --var SAVE_SLOT=N --start-after 3000 --linger 30000 --seconds 240`
  2. La 2362 del peer:
     `.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00_revive_s27.troc
     --var ACCOUNT=1 --var SAVE_SLOT=N --start-after 3000 --linger 30000 --seconds 240`.
     El peer falla en `expect 2993`, y eso es lo esperado.
- **Se da por bueno si** el `cdmp.log` del anfitrión cumple todo esto:
  - `User2Funnels: user2_revive_manager 1 LIVE: …`;
  - ninguna línea `HOST TOUCHED` y ningún ERR `User2Funnels revive F4`;
  - en `User2Stages [state]`, el gestor del anfitrión sigue en `state 2` fuera de sus propias cargas, antes y después
    de la 2362 del peer;
  - en la prueba 1, las líneas de F3 de siempre (`returned user 2's manager`) y ninguna `revive F4`;
  - si la 2362 llega al fijador, `TravelRevive … | F4: F4 ACTED: user 2's own manager`.
- **Qué buscar:** en `bin64\cdmp.log`, el patrón
  `user2_revive_manager|revive F4|TravelRevive|HOST TOUCHED|User2Stages \[state\]`.
- **Con Shadow:**
  - paso 1 (`probe`): al revivir el invitado, `revive F4 … probe: the ORIGINAL` y, como en la S28, `HOST TOUCHED`;
  - paso 2 (`1`): `F4 ACTED` y nunca `HOST TOUCHED`, y el anfitrión sigue creando stages después del revivir (en
    `[create]` del anfitrión y en `ShopProbe [gate]`).

## 7. Implementado: U-0b (sigue siendo `user2_stages=probe`, solo lectura)

- **«Mismo campo»:**
  - el campo de cada jugador se lee en `TransformSync+0x500` (su *field mover* `+0x4F0`, `+0x10`), con la vtable
    `ServerField 0x5B29A60` comprobada;
  - la distancia y la máquina «would ARM/DRAIN» solo cuentan con los dos en el mismo campo;
  - si no, `distance unknown (DIFFERENT fields)` y no hay ARM. Corrige el ARM falso a 168 celdas de la S28.
- **Posición de la stage, ahora en `+0xAC`:**
  - el constructor `0x15EE420` copia 40 bytes en `+0x90` en el orden en que los arma `0x288A970`: escala (1,1,1, lo
    que la S28 leyó como «(1, 1)»), cuaternión y posición;
  - el 2335 manda los mismos datos en otro orden: posición `+30`, rotación `+42`, escala `+58`;
  - **comprobación en vivo:** en `User2Stages [state]`, «from the host m..M cells» de las stages del anfitrión debe
    salir en **1–4 celdas**. Si sale ~168 o fuera de rango, el desplazamiento sigue mal.
- **Fuera `active`:** `+0x74` no es un contador.
- **El armado** (`arm check`) acepta el estado 0 (lo medido) y el 1. El 2 es «already running».
- **Punto O, `ServerActor::Logout 0x2068340`** (*thunk* `E9 AB 17 07 0C`; ningún salto cae dentro del parche):
  - cuenta cada baja por (motivo, quién, llamador) en el resumen, en `Logout (O) by reason, who, caller`;
  - para el **motivo 30** escribe `User2Stages [logout] … reason 30 | called from exe+0x… | cells from the host N,
    from user 2 M`. Eso identifica quién cierra por ventana y si alcanza a lo que rodea al invitado.
- **Qué buscar:** `User2Stages \[(state|logout|would)|User2Stages summary|User2Stages \(` y, dentro del `[state]`,
  `distance` / `the same field` / `from the host`.

## 8. Implementado: U-1 (`user2_stages = 0 | probe | 1`) y la ruta de temporizadores (`user2_timer_route`)

Build 1.0.0.2976. Solo en el anfitrión. Código: `src/mod/session2/user2_stages.{h,cpp}` (módulo y ganchos),
`user2_stages_logic.h` (lógica pura, con tests en `tests/optd_user2_stages_tests.cpp`) y llamadas desde
`user2_funnels.cpp` (rutas y los Troc del usuario 2), `shop_probe.cpp` (U-a), `server.cpp` (2322/2335 encolados y su
2001) y `session2.cpp` (U-h). Primera versión en `2a058cf`; esta sección recoge ya la revisión de ese commit (8.7).

- **`user2_stages=probe`:** lo de U-0/U-0b más **lo que haría el modo 1**, sin llamar a nada ni escribir nada:
  - `[would ARM]` / `[would DRAIN]` con el `arm check` y, si falta algo, `mode 1 would NOT act, missing: …`;
  - la puerta (`gate … would`) y los elementos que prepara el gestor del usuario 2, en el resumen `U-1 (probe): …`.
- **`user2_stages=1`:** lo mismo, pero actuando. Solo stages de **horario**.
- **`user2_timer_route = 0 | probe | 1`** (clave propia, revisión A1): en un campo del anfitrión, los 2043/2045/2046
  del usuario 2 llegan **siempre** a su propio gestor. Arregla un fallo que ya existe sin U-1 (8.1). U-1 en modo 1 lo
  necesita a 1.

### 8.1 Las piezas

| Pieza | Dónde | Qué hace |
|---|---|---|
| Ruta de temporizadores (`user2_timer_route`) | la vf48 de `user2_funnels` (`0x2B00CE0`), solo con retorno `.xtls 0x1082F96A` (2043), `0x10830A0F` (2045) o `0x10835A8F` (2046) y el mismo opcode en el Troc del usuario 2 | Con 1, en un campo cuyo dueño es el anfitrión: devuelve **su** gestor (vtable `0x5B17A58` y `[mgr+0x18]` comprobados), **siempre**, armado o no, abortado o no. Con probe: cuenta `would` |
| U-a puerta | `0x288B87A cmp [r15+0x180],eax` (el `create_mid` de ShopProbe; si `shop_probe=0`, uno propio con los mismos bytes y el mismo contexto `0x288B867`) | Para el gestor del usuario 2 (`rdi`), con su personaje (`r13`) en un campo (`r15`) cuyo dueño es el anfitrión y **armado**: `eax := [r15+0x180]`. Solo un registro |
| U-c armado | a la salida del `ExecuteTroc` **más externo** de cualquier Req o temporizador del usuario 2, en un hilo del servidor (TLS `+0x1EC`) | Vuelve a comprobar: estado 0/1, `+0x689`/`+0x68A` a 0, listas vacías, campo del anfitrión, **distancia ≥ 10 celdas en el mismo campo** y las ranuras `[exe+0x6D69680]`/`[exe+0x6FA3ED8]`/periodo distintas de 0. Llama a `0x287F450(mgr2)` con SEH. Pone en cola **su** 2043 periódico solo si su cadena está muerta o terminada (A5, abajo). La línea `[ARMED]` dice también `+0x70`/`+0x74` del mapa activo antes del armado |
| U-d vaciado | por nivel (A2): cada segundo, mientras esté armado con la máquina en OFF o en otro campo, se pide, y se ejecuta a la salida de su siguiente Req. En sus cargas: en la **entrada** del `ExecuteTroc` de su 2283/2362/2437/2001. En su logout: en `ServerActor::Logout` de su personaje | Por nivel: `0x287DA50(mgr2,0,0)` (estado 1 y un 2043 inmediato que la ruta lleva a `mgr2` → `0x287D4C0`, motivo 19). En carga y logout: `0x287DA50` y `0x287D4C0(mgr2,0,0)` en el acto, **también sin armar** si su gestor tiene stages en el campo del anfitrión (A6). Restos que queden tras un vaciado (`stages left in its manager`): otro vaciado, en el acto |
| U-f elementos | gancho **inline** en la entrada de `0x288A970` (5 bytes `48 89 5C 24 08`) | Solo con el gestor del usuario 2 **armado** y su personaje en un campo del anfitrión (A3): los de horario y vacíos pasan al original; los de secuenciador (escenas 2661: tiendas, misiones, clima) y de clase 2 (facciones) devuelven `*res = 0` sin llamar al original |
| U-h sumidero | el sniffer del cliente del anfitrión | Cuenta 2335 (dueño `@1`), 2126 (`@0`), 2958 y 2909 (`@1`) de las stages del usuario 2. No cambia nada |

- **El fallo que arregla la ruta (A1).** El tick `0x287DBB0(mgr, celda, r8b = +0xB del 2043, …)` (Ghidra):
  - en estado 1 descarga y **no** reprograma;
  - en estado 0 y 2 llama a `0x288B7A0` (que en estado 0 sale en `0x288B7FF`) y, con `r8b != 0`, **reprograma** el
    2043 para `[mgr+0x18]`.
  - Sin la ruta, el 2043 periódico del usuario 2 llega al gestor del anfitrión (estado 2) y se reprograma allí: el
    anfitrión gana una cadena más (S29 12:52–12:54 y S30 16:42–16:44: de 12 a 18/24/33 por minuto). Sus 2045/2046 restan
    además los contadores del anfitrión (`[sm+0x90]`, `[fm+0x31]`).
  - Con la ruta, esa cadena vive en su propio gestor: en estado 0 no hace nada, en estado 2 evalúa sus celdas y en
    estado 1 se acaba.
- **Su cadena 2043 (A5).** El punto T lee `r8b`. Cuenta como cadena viva solo un 2043 con `r8b != 0` que llega a
  `mgr2` en estado 0 o 2; uno que lo encuentra en estado 1 (con `+0x68A` a 0) la marca «terminada». U-c pone un 2043
  solo si está muerta (ninguno en 3 periodos, mínimo 15 s) o terminada. Se reinicia con cada personaje.
- **Su logout (A4).** En `ServerActor::Logout` de su personaje (el manejador del 2001 lo llama con motivo 4 desde
  `.xtls 0x1089A3AE`), en un hilo del servidor y con el personaje aún entero, se descarga su gestor si está armado, o
  si U-1 dejó stages en él en el campo del anfitrión. La línea es `[DRAINED now] | its logout | … at ServerActor::Logout
  of its character (reason N)`.
  - El 2001 en `OnRecvPacket` solo se cuenta (`[goodbye while ARMED]`, WRN): ahí no se llama al juego.
  - Un personaje 0 durante menos de 10 s no cambia nada. Otro personaje, o ninguno durante 10 s, reinicia U-1. Si el
    gestor viejo sigue legible y con stages, `[orphan risk]` (WRN).
- **Las decisiones** las toma el hilo del módulo (cada segundo):
  - histéresis: ARM a ≥ 10 celdas durante 10 s; DRAIN a ≤ 7 celdas durante 5 s. Solo con los dos en el mismo campo;
  - un ARM pendiente se **cancela** (`[ARM cancelled]`) si la máquina sale de ON o no están en el mismo campo (A2);
  - DRAIN si pasa a otro campo;
  - lo pide (`[ARM requested]`, `[DRAIN requested]`) y lo ejecuta el hilo del servidor a la salida de la siguiente
    Req del usuario 2. Si en 10 s no llega ninguna, `[request waiting]`. No se arma con su despedida pedida.
- **Requisitos del modo 1** (si falta alguno, no arma y lo dice):
  - la puerta enganchada (ShopProbe o la propia) y U-f enganchado;
  - el sujeto y la vf48 de `user2_funnels` (hace falta `user2_fast_travel` o `user2_revive_manager` distinto de 0);
  - `user2_timer_route=1` y `user2_world_anchor=1` (§5.1 del diseño);
  - los bytes de las funciones que se llaman.

### 8.2 Comprobado con Ghidra y capstone

- `0x288A970`: la entrada es `mov [rsp+8],rbx ; mov [rsp+0x10],rsi ; mov [rsp+0x18],rdi`.
  - Un solo llamador: `0x288760C` en `0x2885BD0` (`rcx = r15` el gestor, `rdx = &res`, `r8` la stage, `r9` el
    elemento, `[rsp+0x20]` el quinto). La otra referencia (`0x17204648`) es de datos.
  - El llamador lee `[rax]` al volver, así que se devuelve `res`.
  - Ningún salto cae dentro de los 5 bytes. ShopProbe engancha `0x288AAEF`, dentro de la función pero lejos de la
    entrada.
- `0x288B7A0`: `rdi = rcx` (el gestor), `r13 = r8` (su personaje), `r15 = rdx` (el campo). Coincide con lo que lee la
  puerta.
- `0x287F450`: `E9 9B B3 A4 0D` → `.xtls 0x102CA7F0`. Su único llamador es `0x287F06F`, dentro de la preparación
  `0x287EF60`. U-c **no** llama a `0x287EF60`, así que no hay 2322.
- `0x2774430` (`EnqueueTimer`): `E9 1B FC 93 0D` → `.xtls 0x100B4050`. En la reprogramación de `0x287DBB0`,
  `[rsp+0x20]` es la celda (`payload+3`): U-c pasa la celda cero.
- `0x287DA50`: `E9 DB 4F A4 0D`. `0x287D4C0`: `48 89 5C 24 18 88 54 24 10`; empieza con
  `cdmp_Actor_GetFieldContext([mgr+0x18])` y se sale si no hay campo.
- `0x287DBB0`: el manejador del 2043 (`.xtls 0x1082F900`) le pasa `r8b = [payload+0xB]` (reprogramar). Con `+0x68A !=
  0` llama a la preparación `0x287EF60` (habría 2322): la línea `[timer]` del usuario 2 dice estado y `reschedule`.
- El 2045: `.xtls 0x108309B0` hace `0x2822D00([vf48()+0x148])`, así que devolver el gestor también vale para él.
- Nadie más engancha `0x288A970`, `0x287F450` ni `0x2774430`. La puerta `0x288B87A` es de ShopProbe, que llama a
  `user2_stages_gate` antes de lo suyo (su línea `[gate]` sigue mostrando el valor del juego).
- `user2_stages` comprueba los bytes de las funciones que llama **antes** de poner sus propios ganchos en `0x287DA50`
  y `0x287D4C0` (L y U de U-0). Esos ganchos cuentan también las llamadas de U-1. La línea `[owner load]` dice ahora
  los dos bytes con que se llamó `0x287DA50`: las de U-1 son `(0, 0)` y las nativas se ven al lado.

### 8.3 Criterios para abortar (modo 1)

Se comprueban cada segundo mientras está armado. El primero que se cumple para U-1 hasta el final de la partida:
línea `User2Stages [ABORT] …` (ERR), vaciado en el acto, y a partir de ahí se comporta como `probe`. La ruta de
temporizadores sigue (es otra clave).
- **HOST TOUCHED:** el gestor del anfitrión fuera del estado 2 durante ≥ 10 s **fuera de su carga**. La carga va de su
  paso de carga (L) a su preparación (P), dure lo que dure, más 10 s tras P (el 2322 y el armado de su cliente) y al
  menos 20 s tras L (revisión M2).
- `mgr2` no está en estado 2 a los 5 s del armado.
- Ningún 2043 llega a `mgr2` en `max(15 s, 3 periodos)` desde el armado o su último tick.
- Un `[create]` con los mismos datos vivos en los dos gestores con los jugadores a ≥ 8 celdas.
- Un 2322 para el personaje del usuario 2 en los 5 s siguientes a un armado.
- Una excepción dentro de una llamada al juego: ERR `User2Stages` de `game_fault_continue`. U-1 se apaga y la excepción
  sigue su curso, que puede ser un crash.
- **Ya no aborta:** el ritmo del 2043 del anfitrión (M1). Sin U-1 ya varía de 12 a 33 por minuto. Ahora es una línea
  `[host cadence]` (WRN) que dice cuántos fueron de la cadena del usuario 2.
- Lo que el módulo no detecta solo lo mira quien lea el log: un elemento que no sea de horario **preparado**. En el
  resumen, `its elements: … sequencer N (skipped N)`: con `1`, «skipped» debe ser igual al total.

### 8.4 Ini

- `docs/research/ini/cdmp.p8-u1-local-probe.ini` y `cdmp.p8-u1-local-1.ini`: `cdmp.p8-noche-local.ini` (la de las
  pruebas en un PC) con `user2_revive_manager=1`, `user2_world_anchor=1` y `user2_stages` / `user2_timer_route` a
  `probe` / `1`.
- `docs/research/ini/cdmp.p8-u1-host-1.ini`: `cdmp.p8-estable-host.ini` con `user2_stages=1` y `user2_timer_route=1`,
  para Shadow.
- `cdmp.p8-estable-host.ini`: `user2_stages=probe` (con este build incluye lo que haría U-1) y `user2_timer_route=probe`.
  Con Shadow, esta ini ya mide el fallo de A1: `the host's manager: 2043s last minute N (user 2's chain M)` y `would` de
  la ruta en el resumen.

### 8.5 Prueba en un PC (anfitrión + `cdmp_peer`)

**Dónde aparece el peer:** con `SAVE_SLOT=108`, en Hernand (−10717; 620; −3915). Es el mismo sitio de la partida del
anfitrión: los logs de hoy dan al anfitrión en (−10718; 620; …) y distancias de 0 a 2 celdas. Así que **el peer no
está a distancia de ARM**:
- un guion no lo puede mover de forma fiable: sus `MoveActorReq` grabados son de ese mismo sitio;
- **es el anfitrión el que se aleja ≥ 11 celdas** (≥ 704 m en X o en Z), con un viaje rápido a otra ciudad o a
  caballo, y se queda allí **90–120 s** (10 s de espera del ARM, la Req siguiente y tiempo para crear stages);
- el `[state]` dice la distancia (`distance N cells … the same field`).

**El guion del peer:** U-1 actúa a la salida de una Req del usuario 2, así que el peer tiene que seguir mandando Req.
- `tools/scripts/make_u1_idle_script.py` toma el paseo `grapple_s25_g1_walk_138_217.troc` hasta su despedida y repite
  su último `MoveActorReq` (misma posición) una vez por segundo durante `--minutes`.
- Después manda el 2001 y espera 2003/2002.
- El fichero sale de ~1,4 MB (15 min) y no se sube al repositorio.

**Preparación** (desde la raíz del repo, con el juego cerrado):
1. `.\scripts\build.ps1` y `.\scripts\deploy.ps1`.
2. Copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups\` (los pasos B y C escriben en la memoria del juego).
3. Los dos guiones:
   - `py -I -B tools\scripts\make_u1_idle_script.py docs\research\data\grapple_s25_g1_walk_138_217.troc
     build\e2e\u1_idle.troc --minutes 15 --every-ms 1000`
   - `py -I -B tools\scripts\make_u1_idle_script.py docs\research\data\grapple_s25_g1_walk_138_217.troc
     build\e2e\u1_idle5.troc --minutes 5 --every-ms 1000`
4. Lanzar el juego, cargar la partida del anfitrión (Hernand) y quedarse quieto.

**Paso A** (`bin64\cdmp.ini` = `cdmp.p8-u1-local-probe.ini`):
```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script build\e2e\u1_idle.troc --var ACCOUNT=1 --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\u1-a.bin --start-after 3000 --linger 30000 --seconds 1200
```
- Cuando el peer termine de andar (unos 2 min; `[state]` con `user 2: character …`), el anfitrión viaja lejos y espera
  90–120 s. Después vuelve junto al peer (a ≤ 7 celdas) y espera 10 s.
- **Se da por bueno si:**
  - `User2Stages (user2_stages=probe, read only, user2_timer_route=probe): … | G 0x288B87A: ShopProbe's hook … |
    E 0x288A970 (U-f) hooked | calls verified`;
  - `[would ARM] … the same field | arm check: READY …` **sin** `mode 1 would NOT act`, y al volver `[would DRAIN]`;
  - en el resumen: `arms 0`, `gate passed 0`. La ruta, `would a/b/c`: **`a` puede ser 0** con el peer quieto (si su
    cadena 2043 no existe, no hay nada que rutar), y no es un fallo. Con `a > 0`, `the host's manager: 2043s last
    minute N (user 2's chain M)` con `M > 0` confirma el fallo A1 en un PC;
  - ningún ERR `User2Stages`, ningún `HOST TOUCHED`, y el anfitrión en `state 2` fuera de sus cargas.

**Paso B** (`bin64\cdmp.ini` = `cdmp.p8-u1-local-1.ini`): el mismo comando con `--record D:\cdmp-grabaciones\u1-b.bin`,
los mismos movimientos y, mientras está armado, un segundo viaje rápido del anfitrión que **acabe a ≥ 8 celdas del
peer** (si acaba más cerca, el DRAIN por distancia es lo esperado y la prueba del viaje no vale).
- **Se da por bueno si:**
  1. `[ARM requested]` y, en ≤ 2 s, `[ARMED] … 0x287F450 on user 2's manager …: state 0 -> 2 (ok) | before it, its
     map … | its 2043 chain: …`;
  2. en el `[state]` siguiente: `user 2: … manager … state 2` y `U ARMED`;
  3. `[create]` en el gestor del peer (`(user 2)`) con `the same data alive in the other player's: no`; en el resumen,
     `gate passed > 0` y `timer route … to its own manager a/…` con `a > 0`;
  4. `its elements:` con `schedule` > 0 y, para `sequencer` y `class-2`, «skipped» igual al total;
  5. `to user 2: 2335 of its own stages` > 0 y `2322 for it … (after an arm 0)`. En la grabación,
     `py tools\scripts\analyze_capture.py D:\cdmp-grabaciones\u1-b.bin show 2335 20` da 2335 con el dueño
     (`u32 @1`) igual a la clave del peer;
  6. en el segundo viaje del anfitrión: `[owner load]` y `[unload]` solo del anfitrión, ningún `[close]` de una stage
     `(user 2)` y ningún `[ARM cancelled]`/`[DRAIN requested]`;
  7. al volver: `[DRAIN requested] | back under the off threshold`, `[DRAINED] | back under …` y después `[unload] …
     (user 2)` con sus `[close] … reason 19`. Si vuelve antes de que se ejecute un ARM pedido: `[ARM cancelled]` y
     ningún `[ARMED]`;
  8. en el resumen: `other vf48 under its timers (the owner's answer): [2043 from exe+0x1AC99F7] …`. Es lo que la
     revisión M3 quiere ver: esas vf48 siguen dando el gestor del anfitrión (la segunda puerta `0x29113CB`);
  9. ningún `[ABORT]`, ni ERR `User2Stages`, ni `HOST TOUCHED`, ni `[orphan risk]`, y el juego sigue vivo.

**Paso C** (la misma ini que el B; el peer se despide **armado**, revisión M5/A4):
```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script build\e2e\u1_idle5.troc --var ACCOUNT=1 --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\u1-c.bin --start-after 3000 --linger 30000 --seconds 600
```
- El anfitrión se va lejos en cuanto el peer termine de andar y **no vuelve**: el guion acaba con el 2001 a los ~7 min,
  con U armado.
- **Se da por bueno si:**
  - `[ARMED]` y, al final, `[goodbye while ARMED]` seguido de **uno** de estos:
    - `[DRAINED now] | its logout | … at ServerActor::Logout of its character (reason 4)` (en el resumen,
      `at its logout 1`);
    - o `[DRAINED now] | its logout | … before its 2001 reaches the game`, si el 2001 corre como Troc de su personaje;
  - después, `[unload] … (user 2)` con sus `[close]`;
  - ningún `[orphan risk]`, `[ABORT]` ni ERR, y el juego sigue vivo.

**Parar la prueba** (cerrar el juego con `Stop-Process`) con cualquier `[ABORT]` o ERR, o si el anfitrión deja de ver
NPC al volver.

**Qué buscar** en `bin64\cdmp.log`:
`User2Stages \((user2_stages|U-1)|User2Stages \[(ARM|ARMED|arm refused|DRAIN|DRAINED|goodbye|orphan|ABORT|request|host cadence|would|state|create|unload|close|element|route|owner load)|User2Stages summary|HOST TOUCHED`.

**Lo que un PC no puede probar:** que los NPC de horario se vean en el cliente del invitado (el peer no tiene
cliente), su IA, y la tienda de una ciudad lejana (U2). Eso **necesita Shadow**, con `cdmp.p8-u1-host-1.ini`:
- el invitado se queda en un pueblo y el anfitrión se va lejos: los NPC del pueblo siguen junto al invitado;
- esos NPC sobreviven a un viaje del anfitrión;
- el invitado viaja a una ciudad lejana y ve sus NPC.

### 8.6 Riesgos que quedan

- **M4 (aceptado):** el cliente del anfitrión nunca recibe los cierres de las stages del usuario 2. Los NPC a más de
  ~940 m tienen al anfitrión como controlador. Es lo esperado del diseño. U-h solo los cuenta.
- La segunda puerta (`0x29113CB`, vía `0x2912FA0 → 0x1AC8DD0 → 0x1AC9840`) hace 608 de las 628 vf48 bajo el 2043 del
  usuario 2 en la S30 (retorno `0x1AC99F7`). Siguen dando el gestor del dueño del campo. El resumen las cuenta por
  llamador (M3). Si el paso B o Shadow muestran que eso mezcla gestores, será la pieza siguiente.
- U-d por nivel vacía con el 2043 inmediato de `0x287DA50`. Si no llega, los restos se vacían en el acto en la Req
  siguiente (`stages left in its manager after a drain`) o en su carga o logout.
- Si su cadena 2043 se mide mal, al armar habría dos cadenas y su gestor se evaluaría el doble de veces. No toca al
  anfitrión. El resumen cuenta `rescheduling ticks` y `ended`.
- Las cadenas 2045/2046 de `mgr2` no se reactivan (U-c solo pone el 2043). Sus stages de horario se crean, pero si sus
  horarios no avanzan se verá en `pending 2045` del `[state]`. Sería la pieza siguiente (§5.4, punto 3, de
  `mundo-escenarios-propios.md`, con la carga exacta del 2045 todavía por leer).

### 8.7 Revisión de `2a058cf` / `d83e3f6`

| Hallazgo | Cambio |
|---|---|
| A1 (ALTA) la ruta se apagaba (vaciado en carga, abortar) y la cadena del usuario 2 volvía al gestor del anfitrión | La ruta es una clave propia, `user2_timer_route`, y no se apaga nunca. Arregla también el fallo que ya existía sin U-1 |
| A2 (ALTA) un ARM pendiente se ejecutaba tras volver el anfitrión; el DRAIN era por flanco | ARM cancelado (CAS) al salir de ON; DRAIN por nivel; `arm_now` vuelve a mirar distancia y campo |
| A3 (ALTA) U-f saltaba elementos sin armar y en el campo propio del usuario 2 | Solo armado y en un campo del anfitrión |
| A4 (ALTA) 2001 / cambio de personaje dejaban stages huérfanas | Vaciado en `ServerActor::Logout` de su personaje; reinicio solo con otro personaje o 10 s sin ninguno; `[orphan risk]` |
| A5 (MEDIA) la cadena muerta contaba cualquier 2043 | T lee `r8b`: vivas solo las que reprograman en `mgr2`; terminada en estado 1; por personaje |
| A6 (MEDIA) vaciado por distancia y una carga antes del 2043 inmediato | Vaciado en el acto en sus cargas también sin armar si su gestor tiene stages en el campo del anfitrión |
| M1 ritmo del anfitrión como aborto | Solo WRN `[host cadence]` |
| M2 HOST TOUCHED en cargas largas | Ventana de carga de L a P (+10 s) |
| M3 vf48 de la segunda puerta | Contadas por llamador en el resumen |
| M4 | Anotado en 8.6 |
| M5 inis y plan | `user2_world_anchor=1` (y en `mode1_missing`); 90–120 s lejos; paso C (despedida armado); `a` puede ser 0; el segundo viaje acaba a ≥ 8 celdas |
| Bajos | TLS `+0x1EC` y despedida no pedida en la salida del Troc; bytes de `0x287DA50` en `[owner load]`; ranuras comprobadas antes de `0x287F450`; mapa `+0x70`/`+0x74` en `[ARMED]` |

### 8.8 Prueba en un PC del 13-10 (`753f275`) y la segunda puerta (U-a2)

Logs: `backups\p8-sesion30-host\u1-local-probe.log` (paso A) y `u1-local-mode1.log` (`u1_idle5`, despedida armado);
grabación `D:\cdmp-grabaciones\u1-bc.bin`.
- **Posiciones:** el anfitrión usa una partida nueva (ranura 1, (−6727; 478; −3047), celda (−106,−48)) y el peer
  (ranura 108) está en Hernand, celda (−168,−62). Están a **62 celdas en el mismo campo**: no hace falta viajar.
- Unos 40 s después de cada arranque se abre un segundo `CrimsonDesert.exe` que sobrescribe `cdmp.log`. El log bueno es
  el `cdmp_2026…log` cuya cabecera lleva el PID del juego.

**Lo que funcionó:**
- Paso A: `[would ARM] READY` a 62 celdas. Solo faltaba `user2_timer_route` (era lo esperado en probe), el ritmo del
  anfitrión salió solo como WRN y no hubo ningún ERR.
- Paso B/C:
  - `[ARM requested]` a las 18:56:13 y `[ARMED] 0x287F450 … state 0 -> 2 (ok)`, con el mapa `+0x70 0 +0x74 0` y la
    cadena viva en su propio gestor;
  - `gate passed 42`, igual que los 42 ticks 2043 en estado 2;
  - a las 19:03:17, `[goodbye while ARMED]` → `[DRAINED now]` en `ServerActor::Logout` (motivo 7), estado 2 → 1;
  - ningún ERR, ABORT, `orphan` ni `HOST TOUCHED`.
- **La ruta de temporizadores funciona en un PC y arregla el fallo que ya existía:**
  - 2043/2045/2046 llegaron a su propio gestor 44/724/724 veces;
  - el gestor del anfitrión tuvo 12–13 2043 por minuto, con `user 2's chain 0`: la cadena del anfitrión no cambió.

**Lo que no funcionó:** el gestor armado no evaluó **ningún** elemento en 7 minutos.
- En el resumen: `its elements` todo a 0, `[create]` del usuario 2 a 0 y 0 2335 propios.
- En `[state]`: `last cell (0,0) seq 0 | schedules: stages 0`.

**Causa (Ghidra):** la **segunda puerta del dueño**.
- La actualización `0x288B7A0` pasa la puerta U-a y evalúa su celda: `0x28818F0(mgr, campo, pj, actor)`.
- Esta llama al recolector de candidatos `0x2911300(ctx, campo, actor, salida)` (`0x28819CF`), que repite la
  comprobación del dueño:
  - `0x29113AD mov r8d,[r8+0x180]` (el dueño del campo);
  - `[[ctx+0x10]+0xA0]+0x60` (el usuario del personaje que evalúa);
  - `0x29113CB cmp r8d,eax ; jne 0x2912FA0`, que es el **epílogo**: sale sin recoger nada.
- Para el usuario 2, su clave de usuario no es la del dueño, así que sale siempre. Sin candidatos, `0x2883AA0` →
  `0x2882C70` → `0x2885BD0` → `0x288A970` no se llega a llamar: `its elements` = 0.
- Pasado ese salto, el recolector llama a `FindUser 0x2A82590` con `r8` = la clave del dueño, y toma el gestor de ese
  usuario (`[[[user+0xD0]+0x68]+0xA8]+0x18`) para su cerrojo (`0x387EF0`). En el tick nativo del dueño ese gestor es
  el que se actualiza.
- **Las 2016 vf48 en `0x1AC99F7`** (M3) no son esa salida. Salen de `0x1AC9840`, que llama a vf48 y luego a
  `0x15EDC50` sobre lo que da `[gestor]->vf5`. La llaman `0x2913080` y `0x29133B0` desde `0x28818F0`, antes y después
  del recolector: unas 46 por tick, resueltas con el gestor **del anfitrión** (qué registra `0x15EDC50` no está leído).
- **Descartado:** el mapa `+0x70/+0x74` vacío es normal tras `0x287F450` (vacía las tablas de celdas). La celda
  `(0,0)` también: con `+0x688` a 0, `0x288B7A0` evalúa cada tick la celda de la posición del actor y deja `+0x680` a 0.

**Arreglo (el mínimo):**
- **U-a2:** un `create_mid` propio en `0x29113B4` (`mov rax,[r13+0x10] ; mov rax,[rax+0xA0]`, 11 bytes, con el
  contexto `0x29113AD–0x29113D3` comprobado). Ningún salto del exe cae dentro (barrido en crudo).
  - Solo actúa dentro del Troc 2043 del propio usuario 2, con su gestor armado, el personaje `[r13+0x10]` = su
    personaje principal y el dueño = el anfitrión.
  - Hace `r8 := su clave de usuario`. El `cmp` pasa y `FindUser` resuelve al usuario 2, así que el cerrojo es el de su
    propio gestor, como en el tick nativo del dueño. Solo cambia un registro.
- **Las vf48 de su tick:** dentro de su tick 2043 **armado** (modo 1, ruta 1, campo del anfitrión, su gestor en estado
  2), toda vf48 del campo devuelve su gestor, no solo la del manejador. Es lo nativo: en el tick del dueño, vf48 es el
  gestor que se actualiza.
- Los dos son requisitos del modo 1 (`mode1_missing`).
- El resumen añade `second gate passed N (would N, another character inside its 2043 N)` y `other vf48 of its tick
  answered with its own manager N`.

**Siguiente prueba en un PC:** misma ini (`cdmp.p8-u1-local-1.ini`) y mismas posiciones (62 celdas, sin viaje):
```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script build\e2e\u1_idle5.troc --var ACCOUNT=1 --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\u1-d.bin --start-after 3000 --linger 30000 --seconds 600
```
- **Se da por bueno si:**
  - `[ARMED]` y, en el resumen, `second gate passed` ≈ los ticks en estado 2, `another character inside its 2043 0` y
    `other vf48 of its tick answered with its own manager` > 0;
  - `its elements: schedule (2335 stage) N` con N > 0, y `sequencer` / `class-2` con «skipped» igual al total;
  - `[create]` en el gestor `(user 2)` con `same data … no`;
  - `to user 2: 2335 of its own stages` > 0; en la grabación, `analyze_capture.py … show 2335 20` da el dueño `@1` =
    la clave del peer;
  - al final, `[DRAINED now] | its logout` y después `[unload] (user 2)` con sus `[close] … reason 19`;
  - ningún ERR, `[ABORT]`, `HOST TOUCHED` ni `[orphan risk]`, y el gestor del anfitrión con su ritmo de siempre.
- Si `second gate passed` sale > 0 pero los elementos siguen a 0, hay otra comprobación más abajo
  (`0x2883AA0`/`0x2882C70`): lo dirá el siguiente análisis.

### 8.9 Prueba en un PC de `bddc236`: el contexto del recolector es el gestor de misiones del dueño

Log: `bin64\cdmp_20261013-191250.log`, grabación `D:\cdmp-grabaciones\u1-d.bin`.
- Funcionó: `[ARMED]`, el vaciado en el logout y la ruta (44/725/725). Ningún ERR, ABORT, `orphan` ni `HOST TOUCHED`.
- No funcionó: `second gate passed 0 (would 0, another character inside its 2043 42)` y los elementos siguieron todos
  a 0.
- En los 42 ticks armados, `[r13+0x10]` en `0x29113B4` **no** era el personaje del usuario 2.

**Por qué (Ghidra):**
- El contexto del recolector (`r13`, `ctx[2]`) lo crea `0x2880BC0`, el constructor del contexto de `0x28818F0`, con
  `0x2880D6F call 0x2B00C70 ; mov [rdi+0x10],rax`.
  - `0x2B00C70` es `ServerField::vf40`: da el **`ServerQuestManager` del dueño del campo**. Es el mismo punto que
    engancha `quest_per_user` (B) en `user2_funnels`.
  - Su `+0x10` es el personaje de ese gestor de misiones, o sea el del **anfitrión**.
- Por eso la segunda puerta pasaba de forma nativa, `FindUser` resolvía al anfitrión y el recolector trabajaba con el
  gestor de misiones del anfitrión.
- `0x28818F0` solo acepta un id de stage de la lista `[mgr+0x150]` si:
  - ya está activo en el mapa `+0x70`;
  - o es uno de los dos ids que salen del contexto (`[ctx+0x178]` y `[ctx+0x188]`);
  - o el gestor de misiones del contexto (`+0x48`/`+0x58`/`+0x60`) lo tiene en estado 2 o 5.
- Con el gestor de misiones del anfitrión (partida nueva, ranura 1; probablemente sin el progreso de Hernand),
  ninguna stage de Hernand pasaba.
- El resumen de `User2Funnels` lo confirma:
  - `vf40 … Req opcodes 2043x74298`: unas 1 770 llamadas a vf40 por tick armado, desde `0x2345xxx` / `0x225Axxx`
    (las condiciones);
  - todas resueltas con el **del anfitrión**, porque el embudo B solo cambia cuatro llamadores.

**Arreglo:**
- Dentro del tick 2043 **armado** del usuario 2, `ServerField::vf40` devuelve **su** `ServerQuestManager` (cadena
  `[[[pj+0x68]+0x138]+0x18]` con las vtables comprobadas; si no se puede leer, el original).
  - Lo decide `user2_stages_tick_vf40`: modo 1, ruta 1, campo del anfitrión, su gestor en estado 2.
  - Lo hace el desvío de vf40 de `user2_funnels`, antes de la decisión del embudo B.
  - Es lo nativo: en el tick del dueño, el contexto y las condiciones usan el gestor de misiones del dueño, que es el
    del personaje que evalúa. Es requisito del modo 1 (`quest_per_user` distinto de 0, para que vf40 esté enganchado).
- Así `[ctx+0x10]` es el personaje del usuario 2 y U-a2 (`r8 := su clave`) actúa: `FindUser` lo resuelve a él y el
  cerrojo es el de su gestor.
- Se registra (solo lectura) la clave y la vtable del último «otro personaje» en `0x29113B4`, y cuántas vf40 del tick
  devolvieron su gestor de misiones.

**Siguiente prueba en un PC:** igual que en 8.8, con `--record D:\cdmp-grabaciones\u1-e.bin`. Además de lo de 8.8, en
el resumen:
- `vf40 of its tick answered with its own quest manager` > 0 (del orden de mil por tick);
- `second gate passed` ≈ los ticks en estado 2, y `another character … 0`;
- `its elements: schedule` > 0.

**Si los elementos siguen a 0:**
- con `second gate passed > 0`, el siguiente sitio es la lista `[mgr+0x150]` de `0x2883AA0`;
- con `another character > 0`, el resumen da la clave y la vtable de ese personaje.

### 8.10 Prueba en un PC de `288f87b`: efecto colateral en los buffs y una sonda de la evaluación

Log: `bin64\cdmp_20261013-193421.log`, grabación `D:\cdmp-grabaciones\u1-e.bin`.
- **Funcionó:**
  - `[ARMED]`, el vaciado en el logout, ningún ERR, ABORT, `orphan` ni `HOST TOUCHED`;
  - `second gate passed 42 (another character 0)`: U-a2 actúa;
  - `vf40 … own quest manager 73035` y `other vf48 … own manager 3195`.
- **No funcionó:** los elementos siguieron a 0 y no hubo ningún 2335 para el peer.
- **Efecto colateral:** el peer recibió muchos más buffs que en el run anterior (`u1-d.bin`):

  | Paquete | `u1-d.bin` | `u1-e.bin` |
  |---|---|---|
  | `ApplyBuffAck` 2506 | 144 | 13 743 |
  | `UnapplyBuffAck` 3007 | 93 | 2 494 |
  | `VaryDataDefinedStaticStatAck` 2434 | 50 | 3 709 |

  - Casi todos van a **NPC** (claves `B…`: `0xB01001A5` ×299, `0xB01000F8` ×177…). Llegan en ráfagas desde
    t ≈ 60 s de la grabación.
  - **Causa:** las ~1 700 vf40 por tick que no son del contexto (`0x2345xxx`, `0x225Axxx`) son condiciones de actores:
    la clase de condición con vtable `0x5983088`, que mira `param_2+8 == 6` y su campo. Con el gestor de misiones del
    usuario 2 dan otro resultado que con el del anfitrión, así que cada tick armado cambia los buffs de los NPC
    compartidos.
  - **No se manda nada que no debiera al anfitrión** en sí, pero **sí cambia el estado de NPC compartidos**: no es
    aceptable.
- **Arreglo:** solo la vf40 del constructor del contexto (`0x2880D6F`, retorno `0x2880D74`) da el gestor de misiones del
  usuario 2. El resto de vf40 del tick vuelve a dar el del dueño, como en `u1-d` (144 buffs), y se cuentan en el
  resumen: `other vf40 of its tick, the owner's N`.
  - **A comprobar:** si las condiciones de las stages candidatas también usan esas vf40, el usuario 2 se evaluaría con
    el progreso del anfitrión. Lo dirá la sonda H.

**Sonda H (solo lectura), en `0x2881A19`**, justo después de `0x2883AA0(mgr, ctx)` dentro de `0x28818F0`.
- Instrucciones `xor edx,edx ; mov [rsp+0x70],rdx`, 7 bytes. Ningún salto del exe cae dentro y el contexto
  `0x2881A04–0x2881A18` se comprueba antes.
- Por cada tick armado del gestor del usuario 2 (y los primeros del anfitrión, luego 1 de cada 20) escribe
  `User2Stages [evaluation]` con:
  - la lista de ids que recorre el bucle de elementos (`[mgr+0x150]`/`+0x158`);
  - los candidatos del recolector (`rbp+0x670`);
  - el mapa activo `+0x74` y la lista `+0xF0`;
  - del gestor de misiones del contexto: la clave de su personaje, `+0x178`, `+0x186`/`+0x188` (los dos ids que se
    aceptan sin más) y el número de estados de stage `+0x4C` (el tercer caso de aceptación).
- El resumen suma `cell evaluations of its manager N (ids in [+0x150] N, collector candidates N)`.

**Cómo leerla:**
- **Ids 0 en el usuario 2 y > 0 en el anfitrión:** el fallo está antes, en `0x2883AA0`. Por ejemplo, filtra por
  `[stage+0x98] == [[ctx+0x48]+0x60]`, la clave del personaje del contexto.
- **Ids > 0 pero ningún elemento:** rechazan los tres casos de aceptación. Si su gestor de misiones tiene pocos estados
  (`+0x4C`) frente al del anfitrión, es el progreso del peer.
  - La ranura 108 está en el capítulo 2. Si las stages de horario de Hernand piden un progreso que el usuario 2 no
    tiene, es **comportamiento nativo**, no un fallo. Se comprobaría poniendo al peer en una zona que su progreso sí
    cubre, o con una partida más avanzada.

**Siguiente prueba en un PC:** la misma ini y el mismo comando con `--record D:\cdmp-grabaciones\u1-f.bin`.
- Buscar `User2Stages \[evaluation\]` (del usuario 2 y del anfitrión) y la línea `U-1` del resumen.
- En la grabación, los buffs deben volver a lo de `u1-d`: `ApplyBuffAck` del orden de cientos.

### 8.11 Prueba en un PC de `2e38ce5` (sonda H): falta la lista de ids que construye la preparación nativa

Log: `backups\p8-sesion30-host\u1-local-probeH.log`, grabación `D:\cdmp-grabaciones\u1-f.bin`.
- **Los buffs volvieron a lo normal:** `ApplyBuffAck` ×354 y `UnapplyBuffAck` ×195. El arreglo de 8.10 funciona.
- **El resto, como antes:** `second gate passed 42`, la vf40 propia 42 veces (la del dueño, 66 762) y 42 evaluaciones.
  Elementos 0 y ningún 2335.
- **H, usuario 2:**
  - lista de ids `[+0x150]` siempre a 0 y 1 candidato del recolector;
  - mapa activo `+0x74` que pasa de 405 a 440;
  - su gestor de misiones con `+0x4C` de 45 991 a 46 026, `+0x178`/`+0x188` a −1.
- **H, anfitrión** (en su celda): lista 52 065, mapa 52 065, gestor de misiones 47 995.
- **El progreso no es el problema:** unos 46 000 estados de stage frente a unos 48 000.

**Las lecturas de H son correctas.** En `0x28818F0`, `[mgr+0x150]` es el puntero y `[mgr+0x158]` el número de ids
(`+0x15C` es la capacidad). `+0x74` es el número de entradas del mapa (`+0x70` son los cubos, `+0x80` la tabla de
cubos y `+0x88` las entradas). En el anfitrión coinciden porque, después de su preparación, el mapa tiene una entrada
por cada id de la lista. El «no es un contador» de la S28 sobre `+0x74` era, probablemente, una lectura de un gestor
sin preparar, no un desplazamiento malo.

**Quién llena `[+0x150]` (Ghidra):**
- `0x2896C60(mgr, campo, actor)` pone `+0x158 = 0` y añade cada id de los datos estáticos de stage de **este campo**
  (`[dato+0x21C]` = el campo, el tipo, la condición `0x236E2B0` sobre el actor), más los dos ids `+0x178`/`+0x188` del
  gestor de misiones.
- Su **único** llamador es la preparación nativa `0x287EF60` (`0x287F084`), justo después del armado `0x287F450`
  (`0x287F06F`). Le siguen:
  - `0x287F7D0`: los secuenciadores «siempre activos»;
  - `0x287FA30(mgr, campo)`: las tablas por categoría en `[mgr+0x400]` que leen los 14 bucles de categoría de
    `0x28818F0`; su único llamador también es `0x287EF60` (`0x287F0A0`).
- El armado mínimo de U-c se saltaba los tres. Sin la lista, el bucle de elementos de `0x28818F0` no tiene nada que
  recorrer.

**El mapa del usuario 2 crece (405 → 440)** sin lista: el mapa `+0x70` existe aparte de la lista (en el anfitrión, una
entrada por id tras la preparación). Quién mete esas 35 entradas en el gestor del usuario 2 queda **sin leer**. No
bloquea, y H lo seguirá contando.

**Arreglo (U-c2):**
- En su **primera** evaluación armada, en la puerta U-a (`0x288B87A`, dentro de `0x288B7A0` bajo su tick 2043: campo
  enganchado, sin cerrojo del gestor), se llama a `0x2896C60(mgr, campo, r12)` y a `0x287FA30(mgr, campo)` con SEH.
  - `r12` es el cuarto argumento de `0x288B7A0`: el mismo actor que el tick pasa a `0x287EF60` (`0x287DCC2`).
  - Una vez por armado, como la preparación nativa.
- **No se llama** a `0x287F7D0` (clima, escenas de progresión de misiones) ni se manda ningún 2322.
- Línea `User2Stages [lists built] … id list [+0x150] 0 -> N | category table [+0x408] N | actor …`.
- H añade `category table +0x408`.
- Los bytes de entrada de las dos funciones se comprueban con las demás llamadas (`calls verified`).
- `ShopProbe` pasa `r12` a `user2_stages_gate`.

**Siguiente prueba en un PC:** la misma ini y el mismo comando con `--record D:\cdmp-grabaciones\u1-g.bin`.
**Se da por bueno si:**
- `[lists built] … id list [+0x150] 0 -> N` con N del orden de miles;
- las líneas `[evaluation]` del usuario 2 siguientes con `id list` > 0;
- `its elements: schedule N` > 0 y `[create] … (user 2)`;
- `to user 2: 2335 of its own stages` > 0, con el dueño `@1` = la clave del peer en la grabación;
- los buffs en cientos;
- el vaciado en el logout con sus `[close] … reason 19`;
- ningún ERR, ABORT ni `HOST TOUCHED`.

### 8.12 Prueba en un PC de `a5f550a` (U-c2): el gestor del usuario 2 crea sus stages; duplicados y U-f2

Log: `backups\p8-sesion30-host\u1-local-c2.log`, grabación `D:\cdmp-grabaciones\u1-g.bin`.

**Funciona:**
- 20:15:43 `[ARMED]`; 20:15:52 `[lists built] 0 -> 52065`.
- En ese mismo tick, los elementos del gestor del usuario 2:

  | Tipo | Vistos | Saltados (U-f) |
  |---|---|---|
  | secuenciador | 21 | 21 |
  | horario | 53 | 0 |
  | clase 2 | 290 | 290 |

- 54 `[create]` del usuario 2 y 53 2335 de sus stages para el peer.
- El cliente del anfitrión también recibió 2335 ×53 y 2126 ×87 (U-h).

**Después:** `[ABORT] a DUPLICATE schedule stage`, vaciado en el acto (2 → 1) y probe. Ningún otro ERR, ningún
`HOST TOUCHED`, buffs normales.

**(1) Los duplicados.** El criterio comparaba **solo el id de datos**, y un fichero de horario se reutiliza en muchas
stages: el usuario 2 creó `0x268D4087` seis veces en seis sitios. En la grabación (2335: `+5` id de stage, `+9` datos,
`+30` posición), comparando los 2335 del anfitrión (dueño `A0100001`, t = 1,6 s, al entrar el peer) con los del
usuario 2 (dueño `A0100210`, t = 21,6 s):

| Stage | Datos | Posición | Celdas al usuario 2 / al anfitrión |
|---|---|---|---|
| `0x0F722C` | `0x60699B8C` | (−11412; 775; −6800) | 45 / 73 |
| `0x0F722E` | `0xAE8EA5EB` | (−11302; 762; −6665) | 43 / 71 |
| `0x0F7232` | `0xDD3DA05D` | (−12545; 1036; −4457) | 29 / 91 |
| `0x0FC11B` | `0x058B9E79` | (−3909; 682; −428) | 106 / 44 |

- Esas cuatro son **la misma stage** en los dos gestores: mismo id estático, mismos datos, mismo sitio.
  - Están lejos de los dos jugadores, y el gestor del anfitrión ya las tenía antes de que entrara el peer.
  - Son stages de horario de largo alcance (no ligadas a la celda): cada gestor que evalúa las crea esté donde esté.
- La quinta, `0x31BC2167`, era un **falso positivo**: el anfitrión la tiene en `0x0FC984`/`0x0FC989` (cerca de él) y
  el usuario 2 en `0x0FD5DA`/`0x0FD5DF`/`0x0FD5E3` (en Hernand).

**(2) La regla correcta (U-f2).** Una stage es «la misma» si el gestor de horarios del otro jugador tiene una **en la
misma posición** (±0,5 m por eje).
- Lo que se sabe del elemento antes de crear (`0x288A970`, rama de horario):
  - la posición en `[elem+0x20C/+0x210/+0x214]`, que pasa al playData en `+0x38`;
  - el constructor copia el playData desde `+0x1C` a la transformada `+0x90` de la stage, así que la posición queda en
    `+0xAC`;
  - el id estático en `[stage+8]`, que va al playData en `+0x10`.
- **U-f2, en el gestor del usuario 2** (armado, campo del anfitrión): un elemento de horario cuya stage el anfitrión
  tiene viva **no se crea**.
  - `*res` = el código nativo de «no se creó stage» (`0x288AD21: mov eax,[exe+0x6CF7DFC]`); no se llama al original.
  - Línea `[element] … NOT CREATED (U-f2 …)` y contador `schedules not created (alive in the other's: user 2 N,
    host N)`.
- **La otra dirección:** el mismo filtro en el gestor **del anfitrión** mientras U está armado. No crea una stage de
  horario que el gestor del usuario 2 ya tiene viva.
  - Cubre que el anfitrión se acerque antes del vaciado (≤ 7 celdas durante 5 s). Así no depende del orden de la
    histéresis.
  - Al vaciar el usuario 2, su stage desaparece y el anfitrión la crea en su siguiente evaluación.
  - **A comprobar:** que el código «no stage» deja el id para reintentar. Lo dirá el log: `[create] … (host)` de esa
    stage después de un `[DRAINED]`.
- **El aborto se queda** solo para un duplicado que se cuele, ahora con la misma comparación: mismos datos y misma
  posición (playData `+0x38` en el punto N). `[create]` escribe ahora el id estático, la posición y «this stage alive in
  the other player's».
- **No verificado aún:** que `[elem+0x20C]` sean coordenadas del mundo como `+0xAC`. Las líneas `[element] NOT CREATED`
  dan la posición, para compararla con la del 2335.

**(3) El cliente del anfitrión recibe los 2335/2126 del usuario 2.**
- Es lo nativo: el 2335 llega a todas las sesiones del campo. Al entrar, el peer recibió los 2335 de las stages del
  anfitrión (t = 1,6 s) aunque estuvieran a kilómetros.
- El cliente del anfitrión guarda el registro de la stage. Sus NPC solo le llegan (2005) por interés de cercanía.
- A 4 km no es un problema más allá de esos registros. U-h los sigue contando.

**Siguiente prueba en un PC:** la misma ini y el mismo comando con `--record D:\cdmp-grabaciones\u1-h.bin`.
**Se da por bueno si:**
- `[lists built]` aparece, y `[element] … NOT CREATED` sale para las 4 stages de arriba (u otras que el anfitrión tenga
  vivas), con su posición;
- ningún `[create] … (user 2)` con `YES (a duplicate)` y **ningún `[ABORT]`**;
- unas 50 stages propias y 2335 propios;
- U-1 sigue armado hasta la despedida: `[DRAINED now] | its logout` y sus `[close] … reason 19`;
- en el resumen, `schedules not created … user 2 N` (≈ 4) y `host 0`, porque el anfitrión no se acerca;
- buffs en cientos; ningún ERR ni `HOST TOUCHED`.

### 8.13 Regla del proyecto: el invitado sigue el progreso del ANFITRIÓN

**Regla del usuario (2026-10-13):**
- El invitado entra para hacer las cosas del anfitrión: no tiene misiones propias.
- Todo lo que dependa del progreso de misiones o del mundo sigue el progreso **del anfitrión**, no el del usuario 2.
- Lo que el juego ya tiene por usuario sigue siendo por usuario: inventario, dinero, tiendas, habilidades y la partida
  propia del personaje invitado.

**Qué leía cada cosa hasta `d9451d8`:**

| Pieza | Gestor de misiones que leía |
|---|---|
| `0x2896C60` (la lista de ids de U-c2) | del **anfitrión**: llama a vf40 por su cuenta (`0x2896CAF`) y lee sus `+0x178`/`+0x188`/`+0x186` y sus condiciones `0x236E2B0` |
| Las ~83 000 vf40 por run de las condiciones | del **anfitrión** |
| El contexto de la evaluación (`0x2880D6F`) | del **usuario 2**, por la sustitución de `4c322ed` |

- Por ese contexto, en `0x28818F0` el caso 2 de aceptación (`[ctx+0x178]`/`[ctx+0x188]`) y el **caso 3** (estado 2/5 en
  `+0x48`/`+0x58`/`+0x60` de `ctx[2]`) leían el progreso **del usuario 2**.
- `0x2883AA0` (`FUN_141AC0AA0(ctx[2], id)`), `0x2913080` y `0x29133B0` reciben también `ctx[2]`.
- Eso va contra la regla.

**Por qué se sustituyó, y por qué no hacía falta.**
- La sustitución se hizo creyendo que la comprobación del dueño del recolector (`0x29113CB`) fallaba. En el run
  `bddc236` en realidad **pasaba de forma nativa**: el personaje del gestor de misiones del anfitrión es el anfitrión,
  que es el dueño.
- Los elementos estaban a 0 por la lista vacía (8.11).
- La sustitución solo cambiaba dos cosas:
  - la clave de `FindUser` (`0x29113F6`), es decir, de qué gestor se toma el cerrojo;
  - la lectura del progreso.
- La propiedad de las stages **no depende** del gestor de misiones:
  - `0x2883AA0` filtra por `[stage+0x98] == [[ctx+0x48]+0x60]`, y `ctx[9]` es el personaje del gestor (el 4.º
    argumento de `0x2880BC0`);
  - el playData lleva el gestor como dueño, y los 2335 salieron con dueño = el usuario 2.

**Ahora:**
- **vf40 ya no se sustituye en ningún sitio** del tick armado. `user2_stages_tick_vf40` solo cuenta: la del contexto
  aparte, y las demás.
  - El contexto, el recolector, los casos 2 y 3, `0x2883AA0`, `0x2913080`, `0x29133B0`, `0x2896C60` y la creación de
    los elementos leen el progreso del anfitrión.
- **U-a2 se mueve** a `0x29113D4`, justo después de la comprobación del dueño, que ya ha pasado.
  - Hace `r8 := la clave de usuario del usuario 2`. `FindUser` resuelve al usuario 2 y el cerrojo es el de su propio
    gestor, como en el tick nativo, donde dueño = personaje que evalúa.
  - Parchea 8 bytes, `mov rax,[r9+0x68] ; mov rcx,[rax+0x20]`, con el contexto `0x29113CB–0x29113FA` comprobado.
    Ningún salto cae dentro (barrido en crudo).
  - `quest_per_user` deja de ser requisito del modo 1.
- **Las stages siguen siendo del usuario 2:** su gestor, sus 2335 y su `[stage+0x98]`.
- El resumen dice el personaje del gestor de misiones del contexto: `… is the host's by the rule: its character
  0x… = the host`.

**Lo demás de la revisión:**
- **M3, solo registro:** las vf48 del tick se cuentan por (opcode, retorno, respuesta propia o del dueño). En el
  resumen: `other vf48 under its timers, by caller and answer`.
  - Sale ya con `0x1F59531`, `0x1ACAC1A`, `0x2929F71` y `0x1AC416F`.
  - **Sin cambio de comportamiento:** dentro del tick armado todas dan su gestor (desde `81cb517`). Lo que haga
    `0x1F59320` con las listas de participantes `+0x1D8` hay que medirlo antes de tocar nada.
- **Las líneas de U-f2** tienen su propio tipo y presupuesto (`[not created]`, 64 cada 5 s por lado), así que ya no se
  pierden.
- **El id estático:** `[stage+8]` era un puntero. Ahora se escribe el **5.º argumento de `0x288A970`**, que es la clave
  de `FUN_140473f20`, igual que los ids de `+0x150`.
  - Hay que compararlo con el `+5` del 2335. En la grabación: `0x0F722C`, `0x0F722E`, `0x0F7232` y `0x0FC11B`.
- **`+0x408`** es el número de la categoría 0, no un criterio: la etiqueta ahora lo dice.
- **43 creadas y 42 cerradas:** en `u1-local-c2.log` salen 53 `[create]` del usuario 2 y 53 stages distintas cerradas.
  - Cada `[close]` aparece **dos veces** (en dos hilos, T45120 y T33160, con el mismo qpc): el vf13 de cierre se llama
    dos veces por stage.
  - La diferencia de 1 que vio la revisión no se reproduce aquí; queda anotada.
- **Los otros cuatro llamadores de `0x28078E0`** (`0x27EA160`, `0x2808490`, `0x2848CE0`, `0x2849400`) no pasan por
  U-f2. Un duplicado por ahí lo para el aborto (N: mismos datos y misma posición).

### 8.14 Prueba en un PC con viaje del anfitrión (M2): la dirección del anfitrión de U-f2 y el reintento

**Ini:** `cdmp.p8-u1-local-1.ini`.
**Guion:** el de 15 min (`build\e2e\u1_idle.troc`). El anfitrión en su partida (ranura 1, a 62 celdas del peer).
```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script build\e2e\u1_idle.troc --var ACCOUNT=1 --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\u1-i.bin --start-after 3000 --linger 30000 --seconds 1200
```

1. **Armado lejos.** Unos 2–3 min tras la entrada del peer:
   - `[ARMED]`, `[lists built] 0 -> 52065`, unas 50 `[create] … (user 2)`;
   - `[not created] user 2's manager …` para las stages de largo alcance que tiene el anfitrión, con sus posiciones y
     el id estático (comparar con el 2335 `+5`);
   - ningún `YES (a duplicate)` y ningún `[ABORT]`.
2. **El anfitrión viaja a Hernand**, a un punto de viaje a ≤ 7 celdas (≤ 448 m) del peer, que está en
   (−10749; −3938). Espera 60 s.
   - Durante su carga: `[owner load] … (host)` y sus `[close] … (host) reason 19`, ningún `[close] … (user 2)` y ningún
     `HOST TOUCHED`.
   - Al llegar, mientras U sigue armado (≥ 5 s): `[not created] the host's manager …`. En el resumen, `schedules not
     created … host N` con N > 0: son las stages de Hernand que tiene el usuario 2.
   - A ≤ 7 celdas durante 5 s: `[DRAIN requested] | back under the off threshold`, luego `[DRAINED] | back under …`,
     luego `[unload] … (user 2)` con sus `[close] … reason 19`.
   - **El reintento:** en las evaluaciones siguientes del anfitrión, `[create] … (host)` para los **mismos** ids
     estáticos y posiciones que los `[not created] the host's manager`.
     - Si alguno no reaparece en 30 s, el código «no stage» no reintenta. Queda medido.
   - Ningún `YES (a duplicate)`, ningún `[ABORT]`, NPC visibles alrededor del anfitrión en Hernand.
3. **El anfitrión vuelve lejos** (viaje rápido a su punto de partida, a ≥ 11 celdas). Espera 90–120 s.
   - En su carga, sus stages de Hernand se cierran (`reason 19 … (host)`).
   - A los 10 s: `[ARM requested]`, `[ARMED]` (la cadena: `alive` o `queued`), `[lists built]` y de nuevo las
     `[create] … (user 2)`, con las que se saltaron en el paso 2 creadas ahora por el usuario 2.
   - `[not created] user 2's manager` solo para las de largo alcance que siga teniendo el anfitrión.
4. **Despedida del peer armado** (fin del guion): `[goodbye while ARMED]`, `[DRAINED now] | its logout` y sus cierres.

**Global:**
- ningún ERR, `HOST TOUCHED` ni `[orphan risk]`;
- los buffs en cientos;
- en el resumen, `other vf48 under its timers, by caller and answer` con el reparto propio/dueño de `0x1F59531`,
  `0x1ACAC1A`, `0x2929F71` y `0x1AC416F` (M3);
- el resumen dice que el personaje del gestor de misiones del contexto es el anfitrión.

**Qué buscar:**
`User2Stages \[(ARM|ARMED|lists built|not created|DRAIN|DRAINED|create|unload|close|ABORT|evaluation|goodbye)|User2Stages summary|HOST TOUCHED`.

**Resultado de la prueba de 8.14** (`c827729`; log `backups\p8-sesion30-host\u1-local-host-travel.log`):

| Hora | Qué pasó |
|---|---|
| 21:04:25 | `[ARMED]` a 62 celdas, estado 0 → 2, la cadena viva en su gestor |
| 21:04:34 | `[lists built] 0 -> 52065` |
| 21:04:34 | `[not created] user 2's manager` para las 4 stages de largo alcance, a (−11413; −6800), (−11303; −6665), (−12545; −4457) y (−3910; −429): las de 8.12 |
| 21:05:44 | El viaje del anfitrión a Hernand: su `[owner load]` |
| 21:05:49 | `[DRAIN requested]` a **2 celdas** |
| 21:05:50 | `[DRAINED]` (2 → 1) y `[unload] (user 2)` con 41 stages de horario |
| 21:05:52 | El anfitrión empieza a crear las suyas en los **mismos sitios**, por ejemplo (−10597; −3900) y (−10664; −3835): 73 `[create] (host)` hasta el rearmado |
| 21:08:32 | El anfitrión vuelve lejos: `[owner load]` |
| 21:08:43 | Rearmado: `[ARMED]` 1 → 2, la cadena muerta y un 2043 puesto. El mapa `+0x74` sigue en 52065 de la vez anterior |
| 21:08:53 | `[lists built]` y de nuevo las 4 `[not created]` |
| 21:21:31 | La despedida: `[DRAINED now] | its logout` y `[unload] (user 2)` con 42 stages |

- Resumen: `arms 2`, `schedules not created … user 2 8, host 0`.
- Ningún `YES (a duplicate)`, ningún `[ABORT]` ni `HOST TOUCHED`.
- **La dirección del anfitrión de U-f2 no se ha ejercitado:** el vaciado llegó antes de que el anfitrión evaluara las
  celdas de Hernand armado. Por eso sigue sin medirse el reintento tras un `[not created] the host's manager`.
  - En cambio, el traspaso nativo sí: tras el vaciado, el anfitrión crea esas stages en su siguiente evaluación.
- **El id que se escribe no es el del 2335:** el quinto argumento de `0x288A970` (`0xC725`, `0xC727`, `0xC72B`,
  `0xC6FC`) es el **índice** de los datos estáticos (la clave de `FUN_140473f20`). En el 2335 van `0x0F722C`… Las líneas
  dicen ahora `static stage index`.
- **El playData `+0x10` es un puntero** (`0x5CAD8D17A10` en todas): se quita de `[create]`.

### 8.15 Revisión de `c827729`

- **M1 (riesgo de crash).** Cambiar `r8` (U-a2 en `0x29113D4`) hacía que `FindUser` buscara al usuario 2.
  - Si no está «logueado y válido» (por ejemplo, un 2043 armado en su ventana de logout, o una pérdida de enlace sin
    2001), `FindUser` no encuentra a nadie.
  - Entonces `0x2911402 mov rbx,rdi` deja `rbx = 0` y `0x387EF0(&g, 0)` lee de una dirección nula. Un crash del juego
    se envía a Pearl Abyss.
  - **Ahora (variante robusta):** `r8` no se toca. `FindUser` resuelve al anfitrión como siempre y se sustituye `rbx` en
    `0x2911435`.
    - El parche son 7 bytes, `mov rdx,rbx ; lea rcx,[rbp-0x70]`, sin direccionamiento relativo a RIP. Ningún salto cae
      en `0x2911436–0x291143B` (barrido en crudo).
    - `rbx` solo se usa ahí: `0x2911442 mov ebx,1`.
    - `rbx := el gestor del usuario 2`, así que la guarda `0x387EF0` es la de su propio gestor.
    - Se cuenta cuántas veces el `FindUser` nativo no encontró a nadie.
- **B1.** `0x2911300` también lo llama `0x2913F60`, desde `cdmp_FieldStageManager_SyncStages`. Por eso U-a2 solo actúa
  cuando se cumple todo esto:
  - la dirección de retorno del marco es `0x28819D4` (la llamada desde `0x28818F0`);
  - el `rsi` del llamador (el gestor evaluado) es el del usuario 2, comprobado de nuevo (vtable, puntero de vuelta,
    estado 2);
  - el campo es del anfitrión;
  - estamos dentro de su propio Troc 2043.
  - Marco: 8 `push` y `sub rsp,0x1C2C8`. El `rsi` del llamador está en `[rsp+0x1C2F0]`, el retorno en
    `[rsp+0x1C308]` y el campo en `[rsp+0x1C318]`.
- **Menores:** fuera `g_gate2_other` y la rama muerta de vf40 en `user2_funnels.cpp` (solo cuenta).

**Política de `quest_per_user` (solo análisis; las inis no cambian).** Fuera del tick armado, con 1, el usuario 2 recibe
su propio `ServerQuestManager` en cuatro llamadores de vf40:

| Llamador | Qué es | Hoy con 1 | Con 0 |
|---|---|---|---|
| `0x2C4A36F` (`0x2C4A310`) | `MissionTriggerInOutReq` 2112: el cliente avisa de que entra o sale de la zona de una misión | La entrada/salida del invitado actualiza **sus** misiones (progreso propio) | La entrada/salida del invitado llega al gestor **del anfitrión**: dos personajes dando entradas/salidas de la misma zona; si el invitado sale mientras el anfitrión sigue dentro, puede romper el estado de la misión del anfitrión |
| `0x2C4A0AD` (`0x2C4A050`) | `UpdateTraceMissionReq` 2372: qué misión sigue el jugador | El seguimiento del invitado es suyo | El invitado **cambiaría la misión seguida del anfitrión**: mal |
| `0x2B03AA7` (`0x2B03A60`) | Posición de reaparición o segura de un personaje según la región de misión. La usan la muerte y el revivir (8 llamadores, entre ellos `ChangePlayerbleCharacter` y el guardado de `TransformSync`) | Las regiones de misión del invitado deciden dónde reaparece y la posición que guarda su partida | Las del **anfitrión**: lo que pide la regla. Riesgo bajo: la posición guardada del invitado puede ser un punto seguro de la región de misión del anfitrión. F4 no cambia: su vf48 es otra, y el revivir `0x2947F76` ya usa el del dueño |
| `0x2896347` (`0x2896120`) | El paso de misión al aplicar un viaje rápido (`2283`), solo si su primer u32 está en la tabla `exe+0x6D6C1F8` | Los dos clientes mandan 0: en la práctica no se ejecuta | Igual |

- **Marcadores de misión en el invitado:** salen de su cliente (M) y del gestor que recibe sus Req. Con 0, las Ack de
  2112/2372 saldrían del gestor del anfitrión. No está medido a quién se mandan.
- **Recomendación:** no pasar a 0 de golpe. Hacer una política por llamador:
  - `0x2B03AA7` → el del **anfitrión** (cumple la regla, riesgo bajo);
  - `2372` → no tocar el del anfitrión (o quitar ese Req del invitado);
  - `2112` → ni el del invitado ni el del anfitrión hasta medirlo: con `quest_per_user=probe` en la próxima sesión de
    Shadow, contar cuántos 2112/2372 manda el invitado y en qué zonas;
  - `0x2896347` → da igual.
  - Es un cambio de código: lo decide el usuario.
- **Aprobada (2026-10-13) e implementada** como clave nueva `quest_policy`: `politica-misiones.md`.

### 8.16 Las dos cadenas 2043 del anfitrión son nativas (solo medición)

Build 1.0.0.2976. La revisión notó que el gestor del anfitrión empieza cada partida con **dos** cadenas 2043: 12 por
minuto en vez de 6 (el periodo es de 10,1 s, medido en los ticks del usuario 2). La causa es que el anfitrión se
prepara dos veces al entrar. **Es nativo y no hace daño.** Solo se corrige cómo lo medimos.

- **Por qué hay dos.** El login del juego carga dos veces:
  - primero el campo de aparcamiento, en (0,1000,0); está documentado desde el 10-05, en `optD-mundo.md`;
  - después la partida. Entre las dos cargas, `LoginCharacterToGameReq 2601` pone `+0x6A0` a 0 **directamente**, sin
    pasar por el estado 1 (no hay ni L ni U).

  Cada `MoveFieldCompleteReq` llama a `0x287EF60` desde `MoveFieldCompleteCore` (`0x2BC84D4`). La rama lista arma y
  encola **siempre** un 2043 periódico nuevo (+0xB = 1). No comprueba si ya hay una cadena y todos los payloads son
  iguales. El tick `0x287DBB0` solo termina una cadena en tres casos:
  - no hay campo;
  - `+0x6A0 == 1`;
  - `+0x68A != 0`, o el usuario no está «valid logined» (`0x206AE30`).

  En el login no se da ninguno, así que sobreviven las dos. El 2045/2046 no se duplica porque tiene la bandera
  «pending»; el 2043 no tiene nada equivalente.
- **Pruebas.**
  - El mismo patrón en los 9 logs del anfitrión: S30 ×2 y los 7 `u1-local-*` del 13-10.
  - Siempre antes de que entre el invitado. En S30 `164744` hubo 12 por minuto durante 14 min sin invitado.
  - Una carga nativa cuya ventana en estado 1 dura un periodo o más lo arregla sola. En `u1-local-host-travel`, viaje
    2, la ventana fue de 10 s y después hubo 6 por minuto durante 13 min.
  - Con ventanas más cortas sobreviven cadenas: 7 s dio 17 → 12 → 9; 9 s (`mode1-e`) dio 20 → 17 → 13.
- **Coste.** `0x288B7A0` no sale antes si la celda no cambia, así que cada tick reevalúa (`0x28818F0`). La segunda
  cadena llega 0,5–0,9 s después de la otra y en la misma celda: una evaluación más cada ~10 s, sin churn de stages.
  Es el estado nativo de una partida de un jugador hasta su primera carga larga.
- **Lo que sí estaba mal era nuestra medición.**
  - La línea base de `[host cadence]` promediaba los minutos parciales del login (0, 1, 5, 9, 10) y quedaba en 7–8.
  - Con esa base, un 13 nativo daba un WRN falso: 8 en `c2`, 2 en `guardfix`, …
  - El «de 12 a 33» de A1 parte de una base nativa de 2 cadenas.
- **Cambio (solo log).**
  - La línea base solo usa minutos completos que empiezan ≥ 60 s después de la última preparación o carga del dueño
    del anfitrión (`baseline_minute_ok`).
  - El resumen y el WRN dicen `host chains N (user 2's M; period P ms; native login: 2 until its first long load)`.
  - N se calcula siguiendo la fase de los ticks del gestor del anfitrión que reprograman (r8b ≠ 0, estado 0/2,
    `+0x68A` 0) en los últimos max(60 s, 6 periodos). Un tick se une a la cadena cuyo siguiente tick esperado tiene
    más cerca. No puede unirse a una cuyo último tick sea de hace menos de `P − 100 ms`, así que dos cadenas separadas
    0,46 s no se juntan (`count_chains`).
  - Tests en `optd_user2_stages_tests.cpp`.
- **No implementado:** `host_chain_dedupe = 0 | probe | 1`, por si algún día hiciera falta. Se haría en el gancho T,
  en la entrada de `0x287DBB0`, antes de `movzx r13d, r8b` en `0x287DBD6`, y solo actuaría con todo esto a la vez:
  - el gestor es el del anfitrión;
  - estado 2 y `+0x68A` a 0;
  - r8b distinto de 0;
  - no es un tick del usuario 2;
  - el periodo leído en vivo es mayor que 0;
  - el tick anterior que reprogramó en ese gestor fue hace menos de P/2.

  Entonces `probe` contaría `would end` y `1` pondría r8b = 0. Ese tick evalúa igual pero no reprograma, que es el
  final nativo de un 2043 inmediato. Siempre sobrevive una cadena, y como máximo se termina una cada 2P. No se propone:
  el beneficio es nulo y va contra la regla de no poner guardas sin medir.

### 8.17 S31 con Shadow (14-10): U-1 funciona, el bucle de los guardias, U-e y el traspaso

Build 1.0.0.2976. Datos:
- anfitrión: `C:\Emulador CD\backups\p8-sesion31-host\` (paso 2: `cdmp_20261014-080229.log` y `cdmp_packets_20261014-080240.*`);
- Shadow: `cdmp_20261014-070240.log` y `cdmp_packets_20261014-070254.*`.

El reloj de Shadow va ~1 h por detrás. En las capturas, t del anfitrión ≈ t del invitado + 26,2 s.

**Lo que funcionó (paso 2) [V].**
- `[ARMED]` a las 08:04:08, a 62 celdas, y `[lists built]` a las 08:04:18. El gestor del usuario 2 creó 74 stages de
  horario (51 vivas al vaciar). El invitado las tuvo en su M (`GuestSchedRoute` census: M 51 stages, 252 NPC unidos).
- El usuario vio los NPC del pueblo con sus rutinas y sin duplicados (U-f2: 4 `[not created]`).
- El viaje rápido del anfitrión a otro sitio lejano no los quitó.
- Al llegar el anfitrión a Hernand: `[DRAIN requested]` 08:06:59 a 1 celda, `[DRAINED]` 08:07:00.

**El problema: guardias que andan, desaparecen y vuelven a su puesto, en bucle [V].**
- Las 1206 claves de NPC de las stages del usuario 2 (27 stages con NPC, en ~160 s) nacieron con **el anfitrión de
  controlador** (`+0x12 = 0x90100000`), por la rama del dueño del campo de `0x2792A70`.
- El cliente del anfitrión, a 3,3–4,1 km, **sí los simulaba**:
  - recibió sus 1206 `2005`;
  - mandó 2734 ×21 242, 2223 ×10 969, 2556 ×2 536 y 2405 ×1 332;
  - el invitado no mandó nada de ellos, solo recibió los relevos (2697 ×2 522, 2991 ×9 754).

  No es un *rubber-banding*: no hay correcciones de posición, cada «reaparición» es un actor nuevo.
- 828 murieron con 2516 de motivo 42.
  - En los 2 ms anteriores (828 de 828), el cliente del anfitrión mandó `StartAIFunctionReq` 2291 con: grafo `A17A8F3E`,
    paquete `1256388E`, función `40BC1B1E` = hashlittle(`logout`), evento `513FC4FF` = hashlittle(`onstateenterfunction`).
  - En el servidor: `Logout(42)` desde `0x2AC6EA9`, dentro de `0x2AC6C10` = `ServerAiActorComponent` vf69 (vtable
    `0x5B28988`, slot 69); la sonda O lo contó ×833.
- **El ciclo:**
  1. nace en un 2045 del usuario 2 (1119 de 1206 a < 50 ms de una línea `[timer] 2045 … (user 2)`);
  2. a +0,17 s entra en el paquete de IA `BA7DA7C7` y anda unos 3,6 m;
  3. ~5,2 s después pasa al paquete `1256388E` y su `logout`;
  4. 2516 de motivo 42;
  5. la stage lo repone (mediana 0,42 s, otra clave, en su punto).
  - Vida mediana 5,4 s. La stage `0x2000015C` (`cd_m11_m04_npc_city_00_0001_01`) gastó 685 claves.
- **Comparación nativa.** En las capturas de un jugador (200328, 112627, 105149), tras `BA7DA7C7` la IA sigue con
  `BA7DA7C7`, `5337ED67` o `653B6DD9`, y el `logout` sale 1–15 veces por sesión (NPC a 23–117 m). En la S31 sale en 828 de
  ~950 salidas. Control en la misma sesión: las mismas stage data recreadas por el anfitrión junto a él (t 248–303)
  dieron 170 NPC y **0** bajas de motivo 42.
- **Causa:** la IA corre en un cliente a más de 3 km de ellos. Que sea la distancia a su personaje o el mundo sin cargar
  allí es [H].
- **El cliente del invitado está listo para correrla:** los 1206 tienen el bloque de IA VALID y su unión a la stage en su
  propio M (`GuestStage … instance FOUND`).
- Reescribir el `+0x12` en el invitado, como `docked_gimmicks`, **no basta**: el servidor descarta en silencio las Req
  de quien no es el controlador (`MoveActorReq 0x2977E10`) y el cliente del anfitrión seguiría pidiendo el `logout`.

**El traspaso de las 08:06:59 [V].**
- El invitado recibe 2126 ×51 y 2516 ×246 (motivo 19) a 219,55–219,60.
- El anfitrión: su carga (L) empezó a 240,13 y su `[prepare]` llegó a 247,89 (hora de su captura). `[create]` de 32
  stages a 248,02–248,17 y de 6 más a 257,8. De las 51 stage data, 28 volvieron en el mismo sitio (< 5 m).
- El invitado: 2335 ×51 del anfitrión a 222,0 y 2005 ×165 a 222,5. **2,86 s sin NPC del pueblo.**
- El hueco se debe a que el vaciado por distancia (≤ 7 celdas durante 5 s) llegó ~2,1 s antes de la preparación del
  anfitrión.

#### Implementado (14-10): dos claves nuevas, solo en el anfitrión, que actúan con `user2_stages=1` armado

**`[session2] user2_npc_control = 0 | probe | 1` (U-e).**
- **Gancho:** la escritura del controlador `0x2792A7B` (`mov [r14+0x90],eax`), el `create_mid` de `docked_gimmicks`.
  - Hay un solo gancho y una sola instalación para las dos claves (`docked_gimmicks_ctl_write_for_user2`): lo instala la
    que llega primero, con un cerrojo.
  - Se comprueban la instrucción, la cabeza del bloque `0x2792A11` (r14 = actor, rsi = descriptor) y la lectura del
    dueño `0x2792A70` (r13 = campo).
  - La devolución de llamada pregunta primero a `user2_stages_npc_control`.
- **Condiciones (todas, `u2s::ctl_verdict`):**
  - sujeto del `ExecuteTroc` = personaje principal del usuario 2, con opcode 2043/2045/2046;
  - U-1 armado (modo 1, sin abortar);
  - `[rsi] == exe+0x5B04CB0` (`CreateServerActorDesc_NPCSchedule`);
  - `[r14+0x90] == 0`, `[rsi+0x40] == 0` y `[r14+0xA0] == 0`;
  - campo del anfitrión y `eax == [r13+0x180]`;
  - tipo 3 o 6;
  - `[pj2+0x90]` distinto de 0 e igual a la clave de usuario de la sesión 2.
- **Acción:** `eax := [pj2+0x90]` (solo el registro).
  - `probe`: `[would control]`.
  - Las bajas de motivo 42 de esos NPC se cuentan en el punto O (`reason-42 logouts`).
- **Seguimiento:** una tabla de claves (16 384 huecos) apunta, de cada NPC:
  - si el usuario 2 lo conduce (sus 2556/2223/2734/2405/2291/2828 en `OnRecvPacket`);
  - si el cliente del anfitrión manda Req de él (sniffer C>S);
  - el controlador de sus `2005` (a la sesión 2 en `take_frame`, y en el cliente del anfitrión);
  - su baja en el servidor (O).
- **Criterios para abortar (`u2s::ctl_abort`).** El primero que se cumple corta las asignaciones el resto de la
  partida; lo ya asignado se queda y los vaciados de U-1 siguen. Línea `User2Stages [CONTROL ABORT]`:
  - un `2005` de un NPC asignado con otro controlador;
  - más de 10 bajas de motivo 42 de asignados en el último minuto. Desde la S43 solo cuentan las que **repiten hueco**
    de stage (datos del horario, nodo de `[desc+0x4E8]`), es decir, las de un hueco que ya tuvo otra baja 42 en los
    últimos 60 s: el bucle de la S31. Un hueco desconocido cuenta como repetición. Una baja nativa de un grupo entero
    (muchos huecos una sola vez, como los 24 NPC de `0x3537D7CE` del 15-10) solo sale en el log;
  - el cliente del anfitrión manda una Req de un asignado;
  - un asignado sigue vivo 10 s después de un vaciado, después de la limpieza (además, `[control leftover]` WRN con las
    claves).
  - Desde la revisión, «de ≥ 8 asignados con 10–120 s de vida, menos de la mitad conducidos por el usuario 2» ya **no
    aborta**: es la línea WRN `[control undriven]`, como mucho una por minuto. Un guardia en su puesto puede pasar un
    rato sin mandar Req; se decidirá con las medidas de Shadow.
- **Resumen:** `U-e (user2_npc_control=…)`. Da los veredictos bajo el sujeto del usuario 2, los otros descriptores que
  vio bajo sus temporizadores armados (vtable y tipo: por si los miembros de grupo `0x5B04E00` hicieran falta) y los
  contadores.

**`[session2] user2_drain_at_host_prepare = 0 | probe | 1`.**
- **Dónde:** en P (`0x287EF60`, el gancho que ya existía), para el gestor del **anfitrión**.
- **Condiciones (`u2s::prep_drain_verdict`):**
  - gestor del usuario 2 armado, o con stages que dejó un vaciado asíncrono (revisión 8);
  - hilo del servidor;
  - no anidado en otro vaciado del módulo;
  - el llamador es uno de los tres de `0x287EF60`: el tick `0x287DCD5`, `0x289604E` o `MoveFieldCompleteCore 0x2BC84D9`;
  - mismo campo y el anfitrión a ≤ 7 celdas del usuario 2;
  - ningún Troc del usuario 2 en curso (revisión 1).
- **Acción (modo 1):** vaciado síncrono (`0x287DA50` + `0x287D4C0`, el mismo de sus cargas), `DRAINED now | the
  host's preparation…`, y la máquina vuelve a esperar los 10 s.
- **`probe`:** `[would drain at host prepare]` con el contexto: quién llama, hilo, sujeto/opcode, distancia, estados y
  listas de los dos gestores.
- **Riesgo [H]:** el vaciado corre dentro de la preparación del anfitrión. Por eso se pasa primero en `probe`, para ver
  qué llamador aparece de verdad.

**Otros cambios.**
- El diseño (`mundo-escenarios-propios.md` §5.3, §5.6): U-e escribe el `+0x90` del actor (aquí, `eax`), no
  `desc+0x90`, que es un byte que lee `0x2792456`.
- Tests: `optd_user2_stages_tests.cpp` cubre los veredictos en orden, la tabla de claves, las estadísticas y los
  criterios de abortar, el recorrido del 2005 y el vaciado en P.
- Inis:
  - `cdmp.p8-s33-host-paso1.ini` (las dos en `probe`) y `cdmp.p8-s33-host-paso2.ini` (las dos a 1), las dos sobre
    `cdmp.p8-u1-host-1.ini`;
  - `cdmp.p8-s33-local-probe.ini` y `cdmp.p8-s33-local-1.ini`, sobre `cdmp.p8-u1-local-1.ini`.

#### Prueba en un PC (anfitrión + `cdmp_peer`)

**Preparación.** Como en §8.14: copia de la partida, el guion de 15 min y el anfitrión en su partida, a 62 celdas del
peer.
```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script build\e2e\u1_idle.troc --var ACCOUNT=1 --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\u33-a.bin --start-after 3000 --linger 30000 --seconds 1200
```
1. **`cdmp.p8-s33-local-probe.ini`.** Tras `[ARMED]`:
   - `[would control] NPC 0xB… (type 3) … born in its 2045: controller 0x90100000 … would be 0x9010006E`, una por NPC;
   - en el resumen, `[assigned] N` (en probe: los que cumplen todo) y `reason-42 logouts … would M`. Con el anfitrión lejos se espera **M > 0**: es el
     bucle de la S31, ahora medido;
   - el anfitrión viaja a Hernand (≤ 7 celdas) y aparece `[would drain at host prepare] … called from … | a server
     thread … | distance N cells`. Anotar el llamador.
2. **`cdmp.p8-s33-local-1.ini`.**
   - `[control] … -> 0x…` del peer;
   - `2005s to user 2 ok N WRONG 0` y `at the host's client ok N WRONG 0`;
   - `the host client's Reqs for them 0`;
   - casi ninguna baja de motivo 42 (`assigned` cerca de 0);
   - nacimientos por stage bajos: no hay bucle.
   - **Con el peer los NPC asignados no se mueven** (el peer no tiene cliente). Es lo esperado, y también el WRN
     `[control undriven]`. Desde la revisión ya no aborta: las asignaciones siguen.
   - El anfitrión vuelve a Hernand:
     - `[drain at host prepare] … called from …`, y en el acto `DRAINED now | the host's preparation …`;
     - después, los `[create] … (host)`;
     - **ningún** `[control leftover]` (todo asignado retirado con motivo 19) y ningún `YES (a duplicate)`.

**Lo que un PC no prueba:** que el invitado mueva los NPC, su IA y el aspecto del traspaso. Eso es Shadow.

#### Prueba con Shadow (S33)

**Paso 1, `cdmp.p8-s33-host-paso1.ini`.** La misma ruta que la S31 paso 2: el anfitrión lejos y el invitado en Hernand.
- Se espera lo mismo que en la S31 (guardias en bucle).
- El log debe dar `[would control]` y `reason-42 logouts … would` > 0.
- Al acercarse el anfitrión, `[would drain at host prepare]` con su contexto.

**Paso 2, `cdmp.p8-s33-host-paso2.ini`** (las dos a 1), solo si el paso 1 no da sorpresas.
- Los guardias están quietos en su puesto, sin desaparecer.
- En el log del invitado, sus 2556/2223/2734 de esos NPC.
- En el resumen del anfitrión: `driven by user 2` cerca de `tracked`, `reason 42 … assigned` cerca de 0, 0 Req del
  cliente del anfitrión y ningún `[CONTROL ABORT]`.
- Al llegar el anfitrión: `drain at host prepare` y un hueco de menos de 0,5 s entre los 2126 del usuario 2 y los 2335
  del anfitrión en la captura del invitado.
- Medir el ancho de banda invitado → anfitrión del túnel. Estimación: ~40 KB/s para unos 170 NPC activos.

#### Revisión de `ae14ab6` / `351df3b` (14-10) y cambios

| Hallazgo | Cambio |
|---|---|
| **A1** (ALTA) El vaciado en P corre en el hilo del anfitrión mientras los Troc del usuario 2 corren en otros: su tick armado evalúa `mgr2` sin cerrojo, y están sus 2045/2046 y los `drain_now`/`arm_now` de `troc_enter`/`troc_exit`. | Un **cerrojo del módulo** (intento con espera corta, 20 ms) rodea `arm_now`, `drain_now` y la limpieza. Si está ocupado, la petición se vuelve a pedir (`kWhyBusy`). `user2_funnels` cuenta los Troc del usuario 2 en curso (`user2_funnels_trocs_in_flight`, seq_cst). En P: bandera `g_p_draining` y vuelta a contar. Con alguno en curso no se vacía (veredicto `a Troc of user 2 is running`). Los Troc que empiezan durante el vaciado esperan en su entrada (`user2_stages_troc_begin`, 200 ms como máximo). La línea de P dice `user 2's Trocs in flight N`. |
| **A2** (ALTA) En la S31 el vaciado por distancia llegó ~2,1 s antes de P: en P ya no estaba armado. | Con `user2_drain_at_host_prepare=1`, un vaciado **por distancia** espera mientras la carga del anfitrión está pendiente (su L después de su último P), 15 s como máximo (`u2s::hold_distance_drain`). Líneas: `[DRAIN held]` y `[DRAIN hold expired]`. Con `probe`: `[DRAIN would be held]` (una vez), y en P `would drain at host prepare … probe: mode 1 would have HELD the distance drain` si ese vaciado fue en los 30 s anteriores. Toda línea de P dice `the last drain of user 2's manager N ms before (causa)`. |
| **A3** (ALTA) Nada retiraba un NPC asignado que sobreviviera a un vaciado o a la salida del usuario 2: se quedaría congelado en el anfitrión, y una clave de usuario reutilizada podría adoptarlo. | `ctl_cleanup`, en un hilo del servidor: 5 s después de cada vaciado (a la salida de la siguiente Req del usuario 2), en la entrada de su 2001/2283/2362/2437 y en el `ServerActor::Logout` de su personaje. Para cada clave asignada aún viva: `SAM::FindActor 0x2A827B0([[exe+0x6D69698]], scope, key)`, comprobando la clave entera y `[+0x90]` = clave del usuario 2; dentro de ese scope, `ServerActor::Logout 0x2068340(actor, &out, 0x19, [exe+0x6CF76F8])`, la llamada del recolector; se suelta como lo hace `MoveActorReq` (vtable `ScopeAttacherBase 0x5725E48` y `0x1434880`). Bytes comprobados; si no coinciden, el modo 1 queda en `probe`. Línea `[control cleanup]`. La comprobación `[control leftover]` pasa a los 10 s. |
| M4 Escribía `eax` antes de saber si la tabla podía seguir el NPC; la tabla se llenaba en ~27 min del bucle en `probe`. | Primero `put()`: si no cabe, no se asigna (`not assigned (table full)`). El hueco de una clave dada de baja hace ≥ 30 s lo toma una clave nueva, sin dejar agujeros en las cadenas (`slots reused`). |
| M5 Una clave dada de baja que el juego reutiliza daba `Bad2005`/`HostDrives` falsos. | `ctl_check_2005` y el sniffer ignoran las claves con `kKeyRemoved`. |
| M6 Un NPC de una stage del **anfitrión** nacido bajo el sujeto del usuario 2 podía asignarse. | Hace falta además un `thread_local` por Troc: «el vf48 de este Troc devolvió su propio gestor» (lo marcan `user2_stages_route` y `user2_stages_timer_vf48`). Veredicto `its Troc's vf48 was not its own manager`. `[would control]`/`[control]` dicen ese indicador, el dato del horario del descriptor (`[desc+0x4E8]` → dato y nodo [H]) y la stage viva de ese dato en el gestor del usuario 2 y en el del anfitrión. |
| B7 `Undriven` contaba Req de cualquier momento, y un guardia quieto puede no mandar nada. | Solo WRN (`[control undriven]`) hasta medirlo con Shadow. |
| B8 P no cubría el caso A6. | P vacía también un gestor no armado con stages en el campo del anfitrión. |
| B9 `ctl_write_release` destruye el gancho con devoluciones de llamada en curso (ya pasaba antes). | Anotado en el código: solo lo llama el fallo de habilitación de `docked_gimmicks`; con `user2_npc_control` usándolo, el gancho nunca se quita. |

**Corrección tras la prueba de `1e20571` (09:22:52, pantalla de título):** `U-e's cleanup functions are not build
2976's … stays probe`. Los bytes de `Logout` (`0x2068340`) se comparaban **después** de que el punto O hubiera escrito
su salto encima. Además, el puntero del buscador de actores se leía antes de que existiera el mundo.
- Ahora los bytes de las tres funciones se comparan al principio de la instalación, antes de cualquier gancho, y la
  línea de error dice cuál falla.
- `[exe+0x6D69698]` y su SAM se leen en cada limpieza. Si alguno es 0, esa ronda se salta con
  `[control cleanup] … SKIPPED: …` (`skipped (no SAM)` en el resumen), sin bajar el modo.

Tests nuevos: `NotOwnTroc`, la reutilización de huecos (no antes de 30 s, sin agujeros), `ctl_undriven` sin abortar,
`Busy` y la revisión 8 del vaciado en P, y `hold_distance_drain`.

#### Siguiente prueba en un PC (este build)

Con el juego cerrado:
1. `.\scripts\build.ps1` y `.\scripts\deploy.ps1`; copia de la partida en `backups\`.
2. `bin64\cdmp.ini` = `cdmp.p8-s33-local-1.ini` (las dos claves a 1).
3. Anfitrión en su partida (ranura 1, a 62 celdas del peer). Peer:

```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script build\e2e\u1_idle.troc --var ACCOUNT=1 --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\u33-b.bin --start-after 3000 --linger 30000 --seconds 1200
```

**Se da por bueno si:**
- Tras `[ARMED]` salen `[control] NPC 0xB… … its vf48 gave its own manager: yes … -> 0x<clave del peer> … a live stage
  of it in user 2's manager 0x2000…`. La stage del anfitrión debe ser 0 para todos.
- `2005s to user 2 ok N WRONG 0`, `at the host's client ok N WRONG 0`, `the host client's Reqs for them 0`,
  `not assigned (table full) 0`, y casi ninguna baja de motivo 42 de asignados.
- El WRN `[control undriven]` es esperado con el peer. Ningún `[CONTROL ABORT]`.
- El anfitrión viaja a Hernand (≤ 7 celdas):
  - `[DRAIN held] … the host's load started …` (si el vaciado por distancia llega antes de P);
  - `[drain at host prepare] … user 2's Trocs in flight 0 …` y `DRAINED now | … at the host's preparation …`;
  - después, los `[create] … (host)`;
  - o bien `host prepare, no drain: a Troc of user 2 is running` seguido del vaciado por distancia (anotarlo: cuenta
    cuántas veces choca).
- 5 s después, como mucho un `[control cleanup] … 5 s after a drain …` con `0 logged out` (todo se fue con motivo 19);
  ningún `[control leftover]`; en el resumen, `module lock refusals` y `Trocs that waited` cerca de 0.
- El peer se despide (2001): `[control cleanup] … at its load / goodbye Troc's entry` o ninguno si no queda nada.
  Ningún fallo (`game faults 0`).

#### Cierre del anfitrión con `6c12c82` (14-10, un PC) y arreglo

**Qué pasó [V, análisis del volcado].**
- Con `cdmp.p8-s33-local-1.ini` el anfitrión se cerró en `exe+0x1831598` (c0000005, acción actual nula) unos 10 s
  después de las primeras asignaciones. Datos:
  - volcado `%LOCALAPPDATA%\CrashDumps\CrimsonDesert.exe(1).26132.dmp`;
  - log `C:\Emulador CD\backups\p8-sesion31-host\u33-local-crash.log`, líneas ~8106, 8333+ y 8415/8812.
- Es el mismo tipo de fallo que `pendientes/p8-crash-esc-1831598.md`. No hay ningún marco del mod en la pila.
- **La cadena:**
  1. Los NPC asignados al peer nunca tuvieron una acción iniciada (el peer no tiene cliente: ningún
     `StartActionReq`).
  2. Por eso `observer_hold` de `child_hold` los retuvo para la sesión del anfitrión.
  3. A los 10 s los soltó con restore 0 (`timeout: released with restore 0`).
  4. El cliente del anfitrión los creó sin acción actual, y una pieza acoplada (`0xB0100885`, probablemente un arma)
     leyó en `FUN_141831560` la acción nula de su padre.

**Arreglo (este commit).**
1. **`child_hold`** (`npc_release_step`):
   - Si el destinatario es la sesión del **anfitrión** y el controlador del NPC es el usuario 2, nunca se suelta con
     restore 0 por tiempo.
   - Espera a restore 1, a que el anfitrión pase a ser su controlador, o a una baja (otra época, desaparecido,
     observador fuera o fuera de alcance).
   - No hay baja por «antiguo» en este caso.
   - Línea `NPC 0x… withheld from the host … user 2 controls it … restore 0 for N ms`.
   - En el resumen del observer hold:
     - `withheld from the host … N`;
     - `after the timeout with restore 0 … (to the host with user 2 its controller M: must be 0)`.
   - `NpcReleaseReadings.user2_key` lo pone la pasada (`server_user2_user_key`).
   - Tests: `test_npc_withheld_from_host` en `optd_s3_tests.cpp`.
2. **U-e: la primera acción aceptada.**
   - El desvío de `StartActionReq::vf2` de `child_hold` avisa a `user2_stages_start_action_done(key, resultado)` cuando
     U-e está a 1. Resultado 0 = aceptada: indicador `kKeyAccepted`, y el retraso desde el nacimiento (media, máximo,
     ≥ 1 s).
   - **Criterio nuevo `Unaccepted`** (solo con el gancho de `child_hold` puesto): un NPC asignado, vivo, sin acción
     aceptada 5 s después de nacer → `[CONTROL ABORT]` (se dejan de asignar).
   - En la siguiente Req del usuario 2, `ctl_cleanup` saca con `Logout(0x19)` a los no aceptados (`[control cleanup] …
     no accepted StartActionReq 5 s after their birth`). Su stage los repone con el anfitrión de controlador: U-e ya
     está abortado.
   - En el cliente del anfitrión: `assigned 2005s with restore 0 at the host's client N` (con el byte de `restore_byte_of`).
     Tiene que ser 0.
3. **Solo tipo 3** hasta medir el 6 (veredicto `not type 3`).
4. **Red de seguridad en `0x1831594` (§5.3 de `p8-crash-esc-1831598.md`): no se implementa.**
   - El punto 1 cierra el camino conocido.
   - La contención salta una función del cliente que tiene 10 llamadores (minimapa incluido), en el hilo principal, y
     escondería el síntoma de cualquier otra causa.
   - Aquel documento la deja como red de prueba, nunca como arreglo.
   - Si vuelve a aparecer el fallo con el punto 1 puesto, la causa es otra y hay que buscarla.

**Prueba en un PC** (`cdmp.p8-s33-local-1.ini`; con el juego cerrado, `build.ps1` + `deploy.ps1` y copia de la
partida; el peer como en «Siguiente prueba en un PC»). Esperado con el peer:
- primero `[control] …` (solo tipo 3);
- unos 5 s después, `[CONTROL ABORT] … no accepted StartActionReq 5 s after its birth`;
- en la siguiente Req del peer, `[control cleanup] … no accepted StartActionReq … -> N logged out` con N > 0;
- `withheld from the host` > 0 y `to the host with user 2 its controller 0`;
- `assigned 2005s with restore 0 at the host's client 0`;
- `game faults 0` y **ningún cierre**;
- después, las stages reponen sus NPC con el anfitrión de controlador: ningún `[control]` nuevo, veredicto `aborted`.

**Shadow** (`cdmp.p8-s33-host-paso2.ini`):
- `first action accepted N` cerca de los asignados, con el retraso desde el nacimiento **muy por debajo de 5 s**
  (S16: 0,2–0,3 s para el IA local);
- `>= 1 s` pocos;
- ningún `[CONTROL ABORT]`;
- `withheld from the host` solo mientras el anfitrión está cerca de NPC del usuario 2 sin acción, y
  `to the host with user 2 its controller 0`.
