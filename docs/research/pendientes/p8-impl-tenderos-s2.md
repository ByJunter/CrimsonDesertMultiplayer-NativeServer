# P8 sesión 14: tenderos — resultado de la sesión 13, C1 en 1 y la gemela S2 (solo sonda)

Build del juego 1.0.0.2976 (base `0x140000000`, direcciones en RVA). Fecha: 2026-10-11. Rama `p8-shop-s2` (desde
`opcion-d` 1d3e728). Sigue a `p8-impl-tenderos-c1.md`, `p8-s12-tenderos.md` y `p8-s12-tenderos-servidor.md` (§5.2 y
§7: la gemela). Sin juego; compilado y probado con tests (42/42).

Convenciones: **[V]** comprobado (registro o código leído), **[E]** inferencia fundada, **[H]** sin demostrar.

## 1. Sesión 13: qué dijeron las sondas

Registros: invitado `backups\p8-sesion13-shadow\cdmp_20261010-233225.log`, anfitrión
`backups\p8-sesion13-host\cdmp_20261011-003019.log`. El Kliff del invitado era `A0100318` (usuario `0x90100317`).

### 1.1 C1 en probe (invitado) [V]

- `GuestXInter summary`: 922 llamadas en A, **las 922 «in X, not in M», 922 «would redirect»**, y en todas la búsqueda
  nativa usó M, no encontró nada y `0x2126920` devolvió **0** (sin aviso). X conocido: `0x5CB3F0B8400`
  (`A0100001`), comprobación «ok» 922 veces. Ninguna conclusión de otro tipo.
- Etapas vistas (nombre leído en `[[inst+0x48]+0x1B8]`): `cd_seq_funcnpc_pub`, `_smithy`, `_grocery`, `_clothes`,
  `_craftsmit`, `cd_seq_node_her_bluemont` y `cd_minigame_armwrest_bas`. En todas, `inst+0x30` = el id de la entrada:
  la instancia que devuelve `0x1F6C190` **es** la etapa cuya clave copia `E402CC0` en `entrada+0x20` [V].
- Es la causa de `p8-s12-tenderos.md` §4.2, vista en vivo: el HUD de interacción («Listen to Request E / Shop R») no
  aparece porque todas las entradas dinámicas del tendero se rechazan en esta comprobación.

### 1.2 Anfitrión (`shop_probe`) [V]

- 13 escenas `cd_seq_funcnpc_*` preparadas, todas con dueño `A0100001` y del gestor de `A0100001`.
- **«user 2: updates 3, gate 2, owner 2»** no es una fuga de la puerta: ocurrió en t≈163–173, **en el campo propio del
  usuario 2 durante su entrada** (su `MoveFieldAck` #1 con dueño `0x90100317`, «NOT an observer trip»; el
  `MoveFieldCompleteCore` de t=163,43 con dueño `0x90100317`). El campo tenía la misma dirección (`0x2872ED17700`) que
  uno anterior del anfitrión: memoria reutilizada. Después de pasar al campo del anfitrión, el gestor del usuario 2 **no
  se vuelve a actualizar nunca** (las cuentas se quedan en 3/2/2 hasta el final): en el campo compartido no hay ni puerta
  que cruzar.
- Los dos 2671 del usuario 2 eran de una interacción estática (`B010031A`, interacción `0x190`, sin id de etapa):
  aceptados (resultado 0, 2663 y 2916 encolados). Por eso `[server check]` del servidor para el usuario 2 no corrió nunca
  (las entradas sin id de etapa salen antes de A).
- El anfitrión sí habló con el tendero (`B0100159`, grocery `0x19D`): `[server check] ... FOUND ... result 2` y 2671
  aceptado; al invitado le llegaron cuatro 2524 y cuatro 2981 de esa escena (ToNear): por eso **el invitado ve la
  escena sin menú** cuando habla el anfitrión.
- El único 2436 del usuario 2 (`0x1C9`) no era de tienda; pasó R1.

## 2. ¿Con C1 en 1 aparecería el HUD? [V código, E resultado]

Con la etapa encontrada en X, lo que queda dentro de `0x2126920` (§4.2 de `p8-s12-tenderos.md`):

| Paso | Qué mira | En el invitado con C1 |
|---|---|---|
| `[inst+0x160] == 0` | lista `inst+0x158` vacía | igual que en el anfitrión [E] |
| `!0x1F6FC40(X, inst)` | estado de la instancia en su gestor | X es el gestor que la tiene [E] |
| `0x23505A0(info [inst+0x70], tipo de personaje)` | personaje admitido | el invitado también es Kliff [E] |
| `0x1AC10F0(misiones locales, [inst+0x70])` | **[V]** busca el índice en la tabla del gestor de misiones del campo local (`+0x48` cubetas, `+0x58`, `+0x60`); si **no está → 0 (pasa)**; si está → el byte `+0x2E` de esa entrada decide | usa las misiones **del invitado**: si la tiene marcada, rechazo [H] |
| condición `[entrada+8]` | debe existir (nula → 0); su resultado da 1 ó 2, los dos aceptan | la misma entrada que en el anfitrión, que dio 2 [E] |

Después de `0x2126920`, en `0x211FCB0` (`vf188(2)` del NPC) y en `0x9B94B0` (`0x17EE020(J)` y la bandera
`[[h+0x68]+0x120]+0x18`; `0x509C70` y `0x9B4790` sobre la clave de la entrada; el tipo de NPC `[[comp+0x88]+1]`)
**ninguna de esas llamadas recibe el gestor de escenas ni el id de etapa** [V lectura de los argumentos; qué comprueba
cada una por dentro, [E]].
Conclusión: con C1 en 1 el HUD debería mostrar «Shop» [E]; el único rechazo plausible es la tabla de misiones del
invitado [H], que se vería como `[result] ... (X) and found instance ... | returned 0`.

Después de pulsar: el servidor busca la entrada del NPC por la clave de interacción y la comprueba con el gestor del
dueño del campo (`ServerField::vf48`): encuentra la etapa del anfitrión → 2663/2916/2525 [E fuerte, §3.1 del documento
del servidor; el 2671 del anfitrión sobre `0x19D` dio exactamente eso]. La tienda no se abre: el cambio de rama lo pide
el cliente que reproduce la escena (2436) y, en el invitado, esa escena está en X (su 2436 lo corta R1, y lo que X hiciera
de interfaz está vetado). **C1 mide el camino del servidor; no abre la tienda.**

## 3. S2, la gemela: qué está resuelto y qué no

| Pregunta | Respuesta |
|---|---|
| ¿Los enlaces del NPC dependen del gestor del anfitrión? (T1) | No [E fuerte]: `p8-s12-tenderos-servidor.md` §7.1–7.2 (por el campo y la tabla global) |
| ¿Reentrada del cerrojo al llamar a `0x287C200` desde `0x288AAEF`? (T2) | **Cerrado [E fuerte]** (lectura con capstone, §3.1): ningún cerrojo de gestor está tomado en `0x288AAEF`, así que llamar a `0x287C200` para el gestor del usuario 2 no reentra. `funcnpc_twin=1` todavía no llama a nada en esta versión |
| ¿El 2661 de la gemela llega al invitado de forma nativa? | Sí [E fuerte]: `0x2A4DEF0` lo manda ToTarget al dueño de `playData` (`A0100318`); `guest_seq_route` deja los 2661 del personaje propio en M (como la `0x19D` propia de la sesión 12) |
| ¿SetInteraction de la gemela da entradas con ids de M? | Sí [E fuerte]: la gemela corre en M (gestor propio, sin vetos); `E402CC0` copia `[etapa+0x30]` = id de la gemela, que está en M. **Esas entradas no necesitan C1.** Las de X siguen ahí (el recolector devuelve todas) |
| ¿Doble productor del NPC? | En el servidor una instancia no mueve al NPC: lo mueve su controlador, el cliente del anfitrión (`0x2792A70`) [V]; el servidor guarda la rama y la difunde. El NPC quedaría enlazado a las dos instancias (T3, §7.3) [E]. Qué hace el cliente del **invitado** con un NPC enlazado a su gemela (si manda algo por él) está sin medir [H] |
| ¿Qué pasa con el 2436 del usuario 2 sobre su gemela? | Se pierde [E fuerte]: el manejador del 2436 busca el id con `ServerField::vf48` = gestor del **anfitrión**; la gemela está en `mgr(A0100318)`. Igual que los 12 × 2591 y el 2436 de la `0x19D` en la sesión 12. Sin la pieza 2 (enrutar por id de gemela), no hay rama de tienda |
| ¿Cámara / interfaz solo en el invitado? | Sí [E]: la gemela solo existe en M del invitado; al anfitrión le llegarían 2981 de un id desconocido (su M no lo tiene: nada, como los 2981 ajenos nativos) |
| ¿El 2671 del invitado sobre la entrada de la gemela? | El servidor busca la entrada del NPC por la clave de interacción (`0xF4321`); como el servidor no ejecuta el gráfico de la gemela (sus 2591 se pierden), solo tiene la entrada del anfitrión → la aceptaría contra la escena del anfitrión, como C1 [E] |

### 3.1 T2: los cerrojos de la cadena de etapas (2026-10-11, capstone, build 2976)

- **[V]** El guardián de cerrojo de la cadena es `0x393DF0(&guard, lock)`: un giro con `lock cmpxchg [lock+8]`
  y luego `lock->vf18(1)`. Se suelta con `lock->vf20(1)` (`mov rcx,[guard] ; mov rax,[rcx] ; mov dl,1 ;
  call [rax+0x20]`).
- **[V]** `0x287C200(mgr, …)` toma `[mgr+8]` en `0x287C25A` y lo suelta en `0x287C283` o `0x287C294`, justo después
  de `0x287D1D0`. El resto de la función no tiene guardián de gestor. Lo que bloquea después son pruebas sobre el actor
  (`0x206AE30` → `vfC0(4,0x10)`, que no espera) y refcounts atómicos.
- **[V]** Los guardianes de la cadena son tramos cortos que se sueltan antes de la llamada siguiente:
  - `0x288B7A0`: `0x288B7F2` → `0x288B81B` y `0x288B846` → `0x288B85C`; los tres de `0x288B8F6`… se sueltan antes de
    `call 0x28818F0` (`0x288BBC6`);
  - `0x28818F0`: `0x2881CBF` → `0x2881CFA`, antes de las llamadas a `0x2882C70` (`0x28828D7`…);
  - `0x2885BD0`: seis guardianes, cada uno con su `vf20` pocas instrucciones después, todos antes de
    `call 0x288A970` (`0x288760C`).
- **[E fuerte]** En `0x288AAEF` (tras `call 0x287C200` dentro de `0x288A970`) no hay ningún guardián de gestor tomado.
  Una segunda llamada a `0x287C200` con el gestor del usuario 2 toma `[mgr2+8]`, un cerrojo distinto que nadie tiene
  (la actualización de etapas del usuario 2 no pasa la puerta de dueño). No hay reentrada ni espera circular en el
  hilo lógico del servidor.
- **[H]** Lo que queda abierto es semántico: las piezas 2 a 4 de abajo.

**Veredicto:** S2 es viable en principio pero necesita tres cosas antes de ponerla en 1: (1) cerrar T2 (o crear la
gemela fuera de la pila de `0x288A970`, en un punto del hilo del servidor sin cerrojos de escenas tomados); (2) la pieza
2: con el usuario 2 como sujeto (`user2_funnels` ya tiene el gancho de `ExecuteTroc` y cuenta `ServerField::vf48` bajo
sujeto), que `vf48` devuelva `mgr(A0100318)` **solo** para los Req de escena cuyo id está en el libro de gemelas; (3) el
borrado con `0x2879440` (§7.5). Sin (2), la gemela daría el HUD sin C1 pero tampoco abriría la tienda.

## 4. Lo implementado en esta rama

- **C1** (`guest_x_interaction_stage=1`): sin cambios de código respecto a la sesión 13; listo. Las inis
  `cdmp.p8-s7-shadow.ini` y `cdmp.p8-s7-shadow-fase2.ini` pasan a `1`; `cdmp.p8-s12-shadow-seguro.ini` (plan B) sigue en
  `probe`.
- **S2 en sonda**: `[session2] funcnpc_twin = 0 | probe | 1` (anfitrión, `create_session=1`), en el módulo `shop_probe`
  (mismo punto P `0x288AAEF`; si `shop_probe=0` solo se instala P). **`1` no se aplica**: queda en probe y lo dice con un
  `WRN`. El hilo del módulo lee cada segundo el personaje del usuario 2 (`[usuario+0xD0]`, clave comprobada) y su
  `ServerSequencerStageManager` (`[[[pj+0x68]+0xA8]+0x18]`, vtable `0x5B17A58`). En P, para cada `cd_seq_funcnpc_*`:
  - `FuncnpcTwin [would create] ... | host scene id 0x197 (owner 0xA0100001): WOULD CREATE a twin | user 2 character
    0xA0100318, manager 0x... (state +0x6A0 = n) | the twin's playData would be this one with +0 = 0xA0100318 and +8 =
    0: <64 bytes> | probe: nothing called, nothing written` — una vez por id de escena del anfitrión.
  - `FuncnpcTwin [no twin] ...` con el motivo: «the host's preparation failed», «not a scene of the field owner's
    character», «user 2 not known», «user 2's manager not readable».
  - `FuncnpcTwin summary` cada 60 s.
  - Para qué sirve: confirma en vivo las entradas de la pieza 1 (playData real, estado `+0x6A0` del gestor del usuario 2,
    que en la sesión 13 no se actualizaba) sin tocar nada.
- Inis del anfitrión (`cdmp.p8-s7-host.ini`, `cdmp.p8-noche-local.ini`): `shop_probe=1` y `funcnpc_twin=probe`.
- Tests: `optd_shop_c1` amplía con la clave y la decisión de la gemela.

## 5. Sesión 14: valores recomendados y prueba

| Ini | Clave | Valor |
|---|---|---|
| anfitrión | `shop_probe` | `1` |
| anfitrión | `funcnpc_twin` | `probe` |
| Shadow (`cdmp.p8-s7-shadow.ini`) | `guest_x_interaction_stage` | **`1`** |

Prueba: el jugador de Shadow, cerca del tendero de comestibles o del herrero, pulsa el botón lateral (HUD de
interacción) y anota la hora; si aparece «Shop R», pulsa R. Luego el anfitrión habla con el mismo tendero.

Qué esperar:
- Invitado: `GuestXInter [entry] ... REDIRECTED`, `[result] ... (X) and found instance ... | returned 1 (accepted)` o
  `2`; **aparece el HUD**. Si sale `returned 0` con `found instance`: la tabla de misiones del invitado lo rechaza (§2).
- Anfitrión: `ShopProbe [2671] USER 2 asked ... interaction 0x000F4321 ... ACCEPTED`, `[server check] ... for USER 2
  ... FOUND ... result 1/2`, `[to user2] 2663`, `2916`, `2525`; quizá `[user2 req] 2436 ... funcnpc ... DROPPED`.
- La tienda **no** debería abrirse en ningún lado. `FuncnpcTwin [would create]` para cada tienda cercana.

Siguiente paso según el resultado:
- HUD visible y servidor acepta → C1 cumplió; seguir con S2 (T2 y pieza 2, §3).
- HUD visible pero `returned 0` → anotar el índice de misión (añadir sonda de `0x1AC10F0`); S2 lo evitaría solo si la
  gemela tiene otra info.
- HUD no visible con `returned 1/2` → hay otra comprobación de la interfaz fuera de `0x211FCB0`/`0x9B94B0`; volver a leer
  `0x9BA050` y la UI `0xAC25D0`.

## 6. Abortar

Volver a `guest_x_interaction_stage=probe` (y apuntar la hora) si:
- el juego se cierra o se congela al abrir el HUD o al pulsar sobre un tendero;
- el **anfitrión** ve abrirse su diálogo o su tienda, o pierde cámara / control, cuando habla el invitado;
- el invitado pierde interacciones que antes tenía (NPC propios, objetos);
- algún `REDIRECTED` con `X ... character` igual al Kliff propio;
- el tendero se queda bloqueado (no atiende al anfitrión después) o se mueve raro en el anfitrión.

`funcnpc_twin=probe` y `shop_probe=1` solo leen: no se abortan por el juego; si el registro crece demasiado, a 0.
