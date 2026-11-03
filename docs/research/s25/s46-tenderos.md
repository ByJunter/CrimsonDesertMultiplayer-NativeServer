# S46 — El invitado comercia con los tenderos: causas, diseño por fases y fase 1 (sondas)

Rama `opcion-d-tiendas` (worktree `C:\Emulador CD-tiendas`), desde `c84b2dd` (kit 39). Build del juego 1.0.0.2976
(PE `0x6AB28F00`, base `0x140000000`): todas las direcciones son RVA de esa build. Encargo:
`s46-ENCARGO-tenderos.md`. Datos y scripts: `s46-datos/` (`tenderos.md`, `nativo.md`, `revision-diseno.md`, los
decompilados de `0x2369360` y `.xtls 0xE404A10`, los `.asm` y los scripts de `nativo/`).

Marcas: **[V]** leído en log, captura, bytes o decompilado legible; **[V-x]** decompilado legible de `.xtls`;
**[E]** inferencia fuerte; **[H]** hipótesis por medir.

Abreviaturas: `mgrH` / `mgr2` = `ServerSequencerStageManager` del anfitrión / del usuario 2; M / X = gestor propio del
cliente del invitado / copia del gestor del anfitrión en ese cliente; «entrada» = entrada dinámica de interacción del
NPC en el servidor (`[[NPC+0x68]+0x98]`, vector `+0x88`, cuenta `+0x90`, 0x30 B, `+0x10` índice u16, `+0x20` id de
escena).

**Estado:** fase 1 implementada (solo sondas y modos probe; nada escribe en el juego). Fase 2 implementada (§8): R0
activo (`shop_interaction_own_delete`) y U2-c activo (`user2_shop_trade_route=1`), las dos detrás de claves que por
defecto valen 0. Fase 3 implementada (§9): T1 activo (`user2_shop_trade_list`, la lista de comercio por hash exacto en
U-f), T2 activo (`observer_hold_scene_order=1`, la escena propia del invitado retenida detrás de los 2005 de sus NPC),
U2-c y la ruta U2 a 1 en su ini; todo detrás de claves que por defecto valen 0. Las tres fases están compiladas y
comprobadas fuera del juego. **No se ha abierto el juego ni desplegado.** La prueba en vivo de la fase 1 está en §6; la
de la fase 2, en §8.7; la de la fase 3, en §9.7.

---

## 1. Causa por caso

### 1.1 Lejos del anfitrión (U-1 armado)

| Hecho | Marca | Fuente |
|---|---|---|
| Con el kit 39 (`user2_stages_elements=schedule`), U-f salta toda escena `cd_seq_funcnpc_*` de `mgr2`: el invitado lejos **no tiene tienda** | [V] | p1b `host.log:24749`, `tenderos.md` §1.1 |
| Con U-f abierto (p2b, inis de la otra IA), `mgr2` prepara `0x142` (carnicería) y `0x143` (armas); el cliente las rechaza con `0x3B331F28` porque el 2661 llega **antes** que el 2005 del tendero | [V] | p2b `guest.log:9562-9569` |
| El 2005 llega tarde porque **`observer_hold` del mod** lo retiene 344 / 375 ms, y el 2661 propio va ToTarget sin esperar. En nativo el 2005 va 0,1–0,9 ms **antes** (34 de 34 escenas medidas) | [V] | `tenderos.md` §2.1, `order.py` |
| Aunque la escena se creara, **la compra fallaría**: 2789/2366 pasan por `0x21F2780`, que exige que la etapa enlazada al NPC sea la «actual» del gestor que da `field.vf48` (`mgrH`), y la escena del invitado está en `mgr2` → `eErrNoInvalidStageInfo` | [V código] / [E efecto: ninguna compra del invitado medida] | `nativo.md` §1.3 |

**Causa:** dos piezas del mod (U-f cerrado y el orden de `observer_hold`) y una comprobación nativa que nadie enruta
(U2-c). Ninguna es un fallo del juego.

### 1.2 Cerca del anfitrión

| Hecho | Marca | Fuente |
|---|---|---|
| El servidor solo evalúa celdas para el gestor del **dueño del campo** (puerta `0x288B87A`); `mgr2` solo evalúa con U-a, y U-a solo con U-1 armado (≥ 10 celdas). Cerca no hay escena del invitado | [V bytes] / [V datos S45: 0 escenas propias] | `nativo.md` §4, `tenderos.md` §2.2 |
| Al invitado le llegan las escenas del anfitrión (22 + 22 en S45), copiadas a X; su búsqueda de interacción rechaza las entradas de X («in X, not in M») | [V] | `scan_s45.json`, `GuestXInter summary` |
| Interactuar sobre la escena del anfitrión no sirve: el 2671/2720 se aceptarían, pero la tienda la abre la rama del cliente dueño (2436) y la compra exige que la escena del anfitrión sea la actual de `mgrH` (moverla cambia la etapa actual del anfitrión) | [V código] / [E] | `nativo.md` §1.4 |

**Causa:** el modelo nativo es «una instancia de la escena de tienda por campo, la del dueño». Lo que es por jugador
es el dueño de la etapa, el 2661 ToTarget, la tienda, el stock y el dinero (todo cuelga del
`ServerStoreActorComponent` de quien pide) [V/E].

### 1.3 La gemela de la otra IA rompió al vendedor del anfitrión

| Hecho | Marca | Fuente |
|---|---|---|
| `mgr2` en estado 1 sigue reprogramando su 2043 cada ~100 ms y descarga todo: la gemela vive ≤ 100 ms | [V] | p2b `host.log:34206-34491` |
| El borrado nativo de una entrada (`.xtls 0xE404A10`, su único llamador es `0x2369867`) quita la **primera** entrada con la clave y mueve la última a su hueco: no mira el id de escena | [V-x] | `de404a10.txt`, `nativo.md` §2 |
| Reconstrucción: `[E2A2, E2DE]` → la baja de la gemela `0x2DE` quita `E2A2` (la del anfitrión) → queda `E2DE` huérfana → los cinco 3000 del anfitrión | [V datos] | `tenderos.md` §2.3 b; reproducido en `optd_shop_trade` (`test_r0_p2b`) |
| En el servidor, `vf[0x120]`/`vf[0x128]` de la etapa son `ret`: no hay otro registro por etapa; nadie guarda índices; todo pasa bajo el cerrojo del actor | [V] | `nativo.md` §2.1, §2.5 |

**Causa:** dos escenas vivas con la misma clave sobre el mismo NPC solo son seguras si se borran en orden de
creación. El relevo normal (sin gemela) ya junta dos escenas durante ~1 s (p2b) [V].

### 1.4 Lo que no está demostrado (causas)

- [E] Que la compra del invitado falle con `eErrNoInvalidStageInfo` lejos: es lectura de código; ninguna compra suya
  se ha medido. P1 lo mide.
- [H] Qué etapa considera «actual» `0x1F5C940` para un NPC con dos escenas enlazadas (cerca).
- [H] El modo de «ocupado» de la clave `0x000F4321` (`InteractionInfo +0x13/+0xBC`). P5 lo mide.
- [E] El tramo «parada de la etapa → salida de `SetInteraction`» (H1). P2 lo mide.

---

## 2. Diseño por fases

Cada pieza con clave propia `0 | probe | 1`, sondas antes, y prueba en vivo de cada fase antes de la siguiente.

| Fase | Pieza | Qué hace | Dónde |
|---|---|---|---|
| 1 | Sondas | P1, P1b, P2, P2a, P5, P6, P3, libro; T2 y U2-c en probe; T1 solo veredicto | **hecha** (§3) |
| 2 | **R0** | En la baja de `SetInteraction` (`0x2369862`, cerrojo tomado): si la primera entrada con la clave no es la de esta escena, intercambiar los dos bloques de 0x30 B; si esta escena no tiene entrada, nativo (la revisión F1 quitó el centinela). Activo **sin** tiendas del invitado, como regresión. **Hecha** (§8), con U2-c activo detrás de su clave | anfitrión (servidor) e invitado (X frente a M; en probe) |
| 3 | **Lejos**: T1 + T2 + U2-a/U2-b + **U2-c** | T1: U-f deja pasar solo `smithy`/`butcher` (lista exacta, ≥ 10 celdas). T2: el 2661 propio y todo lo de su id detrás de los 2005 retenidos. U2-c: en `0x21F28A1`, `rax := mgr2` para el comercio del usuario 2 cuando su escena es la actual de `mgr2`. **Hecha** (§9) | anfitrión |
| 4 | **T5** | En el cliente del invitado, las escenas de X no registran entradas de tienda (`SetInteraction` vetado en los dos sentidos para `cd_seq_funcnpc_*`), decidido **por etapa** con un libro de altas y bajas | invitado |
| 5 | **Carril de tienda cerca** | `g_shop_lane` con su propio latch de aborto (no `g_armed`): `mgr2` armado cerca solo para la lista de comercio y solo con la escena del anfitrión del mismo recurso viva; consultan U-a, U-a2, la vf48 del tick, U-f, U2-a, U2-b, M4; no U-e, U-f2, D, el vaciado en P, `check_abort` ni familias/carreteras. Plan B sin riesgo: sin tiendas cerca (alejarse ≥ 10 celdas) | anfitrión |
| — | **R1 condicional** | Elección de entrada por solicitante sobre la copia de `0x2126A1F`. Solo si P1 muestra que con dos escenas el NPC no toma como actual la del invitado (H4b) | anfitrión |
| 6 | Shadow | Pasos 3–5 entre dos PC | — |

Puertas de diseño (revisión): **H1** (si el borrado lo hiciera el destructor de la etapa, R0 se rediseña: P2 lo
confirma o lo desmiente), **H5** (almacén por usuario: [V/E] `nativo.md` §1.3; si fuera compartido, la compra del
invitado se bloquea), **H6** (si el NPC muriera con la escena que lo generó, la regla «escena del anfitrión viva» sería
obligatoria también lejos; p2b dice que no muere [V]).

### Descartado

| Pieza | Por qué |
|---|---|
| `funcnpc_twin` (Prepare en `mgr2` desarmado) | Muere en ≤ 100 ms (estado 1 descarga) y, con el borrado por primera clave, se lleva la entrada del anfitrión (§1.3). Sigue a la fuerza en probe |
| T3 G1–G4 (guardia que suprime altas / borra etapas de `mgr2`) | Ataca el síntoma; G3 razona FIFO y el borrado nativo no conserva el orden (F1); saltarse el alta (G1) también salta registros que no se conocen; puede reproducir el daño de p2b. R0 lo sustituye en un solo punto |
| Reutilizar `g_armed` para «cerca» (T4) | Cambia el significado de unos 10 consumidores validados de U-1. Lo sustituye el carril con estado propio |
| (a) Interactuar sobre la escena del anfitrión | La compra exige que la escena del anfitrión sea la actual de `mgrH`; moverla altera al anfitrión (§1.2) |
| Arreglar `comp+0x318` / `local_res8` | Es el enlace de horario; los tenderos los genera la escena `funcnpc` (`tenderos.md` §1.2) |

---

## 3. Fase 1 implementada

Tres claves nuevas, **solo anfitrión, por defecto 0**. Ninguna escribe en el juego, llama a una función del juego,
cambia un registro o retiene, cambia o descarta un paquete.

| Clave | Valores | Qué instala |
|---|---|---|
| `shop_trade_probe` | `0` (sin ganchos) \| `1` | P1, P1b, P2, P2a, P5, P6, P3, el libro y el grifo de 3000/Nak al usuario 2 |
| `user2_shop_trade_route` | `0` \| `probe` (`1` se queda en probe con WRN) | U2-c (gancho propio; también sin `shop_trade_probe`) |
| `observer_hold_scene_order` | `0` \| `probe` (`1` se queda en probe con WRN) | T2 en `take_frame` (sin ganchos); necesita `observer_hold_acks=1` |

### 3.1 Sondas y su verificación

Verificación de cada punto antes de instalar (`shop_trade_probe.cpp` `install`):
1. base `0x140000000` y `TimeDateStamp 0x6AB28F00`;
2. la entrada **exacta** de `.pdata` de la función (inicio y fin);
3. la firma de 40 B en el punto, dentro de `.rsrc`, byte a byte (única en todo el exe: comprobado fuera,
   `s46-datos/nativo/verify_f1.py` → `verify_f1.out`);
4. el código del que dependen las lecturas (prólogo con el `rbp` del marco, ranuras de pila, cadenas de punteros);
5. `p8_install_mid`: dentro de `.rsrc`, los bytes, y safetyhook reubica **exactamente** los bytes verificados.

Fuera del juego (`verify_f1.out`): instrucciones enteras; ningún salto rel32/rel8 ni puntero absoluto dentro del
parche salvo su primer byte (un único positivo de la búsqueda en bruto en P2, `0x236981D`, es el byte `0x78` del
desplazamiento de `mov rdi,[rsp+0x78]`: falso positivo; el desensamblado de la función no tiene saltos ahí); el único
relativo parcheado es el `je rel8` de P6, que safetyhook 0.7 reubica (`jcc` corto → largo en el trampolín). Si un
punto falla, **no se instala** y sale `WRN ShopTradeProbe: <punto> NOT installed: <motivo>`; los demás siguen.

| Id | Punto (función, `.pdata`) | Bytes parcheados | Qué registra |
|---|---|---|---|
| **P1** | `0x21F2C45` (`0x21F2780`–`0x21F2F2E`) | `48 39 5C 24 28` `cmp [rsp+0x28],rbx` (5) | Cada comprobación de tienda: etapa enlazada al NPC (`rbx`, y si está en `mgrH`/`mgr2`) frente a la actual (`[rsp+0x28]`) del gestor `[rsp+0x38]` (`mgrH`, `mgr2` u otro; servidor o cliente por su vtable), quién pide (`r13`→`[+8]`→personaje y usuario), el NPC (`rsi`), el llamador (retorno en `[rbp+0x5F]`: 2789, 2366, otras 5 operaciones, la interfaz del cliente), el veredicto, las actuales de `mgrH` y `mgr2` y, para el usuario 2, si pasaría contra la actual de `mgr2` (lo que daría U2-c) |
| P1b | `0x21F2917` (ídem) | `41 F7 C4 FF FF FF 1F` (7) | El gestor sin etapa actual (`r12 & 0x1FFFFFFF == 0` → `eErrNoInvalidStageInfo`); solo esas llamadas |
| **P2** | `0x2369862` (`0x2369360`–`0x23698E7`) | `8B 10 49 8B CC` `mov edx,[rax] ; mov rcx,r12` (5) | Cada baja de `SetInteraction` **del servidor** (procesador `0x5B1FE10`; las del cliente del anfitrión `0x558C080` solo se cuentan): la lista de entradas del NPC **antes** del borrado (índice, clave, id), `i_first`, `i_own`, el veredicto de R0 `[would noop]` / `[would swap]` / `[would sentinel]` y **`[would orphan]`** (WRN) si el borrado nativo se llevaría la entrada de otra escena; el dueño de la etapa `[[stage+0x48]+0x60]` y si está en `mgrH`/`mgr2` |
| P2a | `0x2369695` (ídem) | `45 0F B7 C6 49 8B CC` `movzx r8d,r14w ; mov rcx,r12` (7) | Cada alta del servidor con la lista antes del `push_back` |
| **P5** | `0x2B36FB3` (`0x2B36EF0`–`0x2B37366`) | `48 8B F8 48 8B 45 CF` (7) | La `InteractionInfo` del 2671 (`rax`): clave, `+0x12`, `+0x13` (modo de «ocupado»), `+0xBC` (jugadores exentos), y su lectura (los dos a la vez / uno a uno / exclusivo por clave). Pocas líneas por clave |
| P6 | `0x2B3708C` (ídem) | `41 80 3F 02 74 29` `cmp byte [r15],2 ; je` (6) | El resultado y el error (con nombre) de `vf170 0x211FCB0` en cada 2671; los rechazos siempre, los pases unos pocos por NPC |
| P3 | sin gancho | — | Las 1346 variables `eErrNo` (`eerrno_names.h`, generado de los registros `0x21FA720`/`0x2209C20` por `allerr.py`), leídas al instalar y releídas hasta tenerlas casi todas: nombra el error de los 3000 y de los Nak 1013 al usuario 2 y el de P6 |
| **U2-c** | `0x21F28A1` (`0x21F2780`–`0x21F2F2E`) | `48 8B D8 48 8D 50 08` (7) | Para la petición del usuario 2: `[would route to mgr2]` con el gestor que dio `vf48`, `mgrH`, `mgr2` y su etapa actual (viva o no en `mgr2`); los demás casos se cuentan. **RAX no cambia** |

**Desviación del encargo (P2 y P2a):** el encargo ponía P2 en `0x2369867` (`call 0x21245B0`) y P2a en `0x236969C`
(`call 0x2124370`). Un `create_mid` sobre una llamada deja, mientras corre el borrado o el alta, una dirección de
retorno dentro del trampolín de safetyhook, que no tiene datos de desenrollado: una excepción que cruzara ese marco
no se podría desenrollar. Por eso los puntos van **en las instrucciones enteras inmediatamente anteriores**
(`0x2369862` ya era la alternativa de `nativo.md` §2.5), con los mismos registros: en P2, `rax` = la
`InteractionInfo` (`[rax]` = la clave que compara el borrado), `r12` = componente del NPC, `[rbp+0x858]` = el contexto
(`[ctx]` = la etapa), `[rbp+0x850]` = la función, `[[rbp-0x60]+8]` = el actor; en P2a, `r14w` = índice, `rsi` = id de
la etapa. El marco se verifica con el prólogo (`rbp` = `rsp` de entrada − 0x848) y las instrucciones que escriben el
cursor. R0 (fase 2) irá en el mismo punto que P2.

**El libro de interacciones del servidor** (con `shop_trade_probe=1`): las NPC cuyas altas o bajas tocan la clave
`0x000F4321` entran en el libro (por clave del NPC; la dirección solo sirve en este proceso). Cada segundo se relee el
vector vivo (lecturas SEH, sin cerrojo; si cambia durante la lectura se reintenta y, si no, no se decide nada) y cada
`+0x20` se busca en `mgrH` y en `mgr2` (réplica de solo lectura de `0x1F6C190`). Una entrada cuya escena no está en
ninguno durante **dos** comprobaciones seguidas es `[orphan]` (WRN, una vez). Si el NPC desaparece o su componente
cambia, sale del libro. El resumen cuenta «live != expected» (la lista viva frente a la que dan las operaciones
vistas). **Limitación (revisión B1):** el libro solo conoce `mgrH` y `mgr2` (los gestores de los dos personajes, de
`user2_stages`); una escena de un tercer gestor sale «in neither». Para no contar un falso huérfano en un cambio de
campo, la vuelta en la que `mgrH` o `mgr2` cambian de puntero se da por «desconocida» y no suma. Por eso `[orphan]`
solo es aborto con la comprobación de §6.

Las lecturas de los ganchos y del libro usan `game::fast_read` (revisión M1: rearma una página de guarda tocada por
un puntero obsoleto). Las líneas de P2/P2a de los tenderos tienen su propio limitador (64 por 5 s; `[would orphan]`
32 por 5 s, aparte): los eventos llegan siempre al libro y lo que no se escribe se cuenta en `limited` (revisión B4).

### 3.2 T2 en probe (`observer_hold_scene_order`)

En `take_frame`, para lo que se encola al usuario 2 (`hold_scene_order_logic.h`):
- un 2661 **propio** del usuario 2 (dueño `@5` = su personaje) de una escena de la **lista blanca por hash `@27`**
  (`0xA7132561` smithy, `0x978345CA` butcher; diccionario del juego, `funcnpc.json`) que enlaza (tipos 4/5) alguna
  clave que `observer_hold_acks` retiene ahora → `[would hold scene]` con esas claves;
- después, todo opcode con ese id (2981 `21+8n`, 2524 `18+11n`, 2810 `17`; formatos portados de la otra IA) se apunta
  en orden;
- los 2005 se leen **tal como se encolan** (después de la compactación de `observer_hold_acks.cpp:227`, todos los
  registros de un lote, también los que suelta `publication_gate`): cuando sale el de la última clave pendiente,
  `[would hold scene] ... the last of its NPCs' 2005s went N ms after its 2661: T2 would have released, in order, …`;
- terminan también: 2810 de la escena (`would cancel`), un 2516 de una clave pendiente, una frontera de campo del
  usuario 2 (2174 / 2643 sin append / 2756) y 40 s.

**Hoy no habrá líneas `[would hold scene]`**: el invitado no tiene escenas de tienda propias con
`user2_stages_elements=schedule`. Queda instalado y probado con tests (el caso lejos de p2b, 344 / 375 ms).

### 3.3 T1 solo veredicto

U-f ya ve, en su propio gancho y con U-1 armado, cada elemento de tienda de `mgr2` que hoy salta (`SkipFuncnpc` con
`elements=schedule`; `SkipNoRoute` si la ruta no puede preparar). Para esos, la línea `User2Stages [element]` dice ahora
`[T1 would pass]` (en la lista exacta y a ≥ 10 celdas) o `[T1 would skip]` (fuera de la lista o cerca), y si la ruta
podría prepararla; el resumen los cuenta. **El elemento se sigue saltando igual: no se prepara nada.** No hace falta
`user2_stages_elements=schedule+funcnpc` (que **sí** prepara escenas), así que no se activa. La lista es por **nombre**
exacto en U-f (el hash del recurso no está disponible antes de preparar: sale en `playData+0x20` después); nombre y
hash son la misma lista (`funcnpc_trade_policy.h`).

**Formato de log (revisión B2):** el veredicto de T1 y su recuento en el resumen U-1 solo salen con
`shop_trade_probe=1`. Con las claves de S46 a 0, las líneas `User2Stages [element]` y el resumen U-1 son idénticos a
los del kit 39. Con `shop_trade_probe=1`, la línea `[element]` de un elemento de tienda saltado termina en
`| S46 T1 (verdict only, nothing prepared): [T1 would pass|T1 would skip] …` y el resumen añade `S46 T1 (verdict
only) …`: los scripts que analicen esas líneas tienen que admitir ese sufijo.

### 3.4 Lo portado a mano de `opcion-d-tenderos`

La rama no se puede fusionar (parte de `ef5a6b5` y borraría S42–S45). Solo cabeceras puras, con sus tests:
- el formato del **3000** (solicitante `@0`, NPC `@4`, interacción `@8`, error `@12`; fixture real de p2b
  `…EC0210B0 21430F00 5D34C858…`) en `u2sh::parse_step_ack` y `shp::parse_interaction_ack`. **Arreglo colateral:**
  `user2_shops` leía el 3000 con el formato de éxito, así que su criterio `Fail3000` nunca veía un 3000 de tienda;
  ahora lo ve (solo afecta a `user2_shop_route=1`, que no está en ningún kit);
- `shop_troc_logic.h` (sin uso todavía; para el carril);
- los formatos 2981/2524 (en `hold_scene_order_logic.h`) y `funcnpc_trade_policy.h` con los hashes.

### 3.5 Archivos

- `src/mod/session2/shop_trade_probe.{h,cpp}`, `shop_trade_probe_logic.h`, `eerrno_names.h`;
- `src/mod/session2/observer_hold_scene_order.{h,cpp}`, `hold_scene_order_logic.h`;
- `src/mod/session2/funcnpc_trade_policy.h`, `shop_troc_logic.h`; cambios en `user2_shops_logic.h`,
  `shop_probe_logic.h`, `user2_shops.cpp`, `user2_stages.cpp` (T1), `server.cpp` (grifos), `session2.cpp`,
  `config.{h,cpp}`, `mod.cpp` (línea `Config: S46 …`);
- tests: `optd_shop_trade`, `optd_hold_scene_order`, `optd_shop_troc`, y añadidos en `optd_user2_shops`;
- ini: `docs/research/ini/cdmp.2inst-s46-probe-host.ini` (= `cdmp.2inst-s43-activo-host.ini` + las tres claves); el
  invitado usa `cdmp.2inst-s45-activo2-guest.ini` sin cambios.

---

## 4. Comprobaciones fuera del juego

Ver §7.

---

## 5. Lo que la fase 1 tiene que responder

| Pregunta | Sonda | Respuesta esperada [E] |
|---|---|---|
| ¿El anfitrión compra con P1 «PASS» (su escena = la actual de `mgrH`)? | P1 | Sí, en 2789/2366 con la tienda abierta |
| ¿Qué modo de «ocupado» tiene `0x000F4321`? | P5 | Desconocido: decide si los dos pueden hablar a la vez |
| ¿La descarga y el vaciado ejecutan la baja de `SetInteraction`? ¿Siempre `noop` hoy? | P2 | Sí y sí (con una sola escena por NPC) |
| ¿Hay entradas huérfanas hoy sin tiendas del invitado? | libro | Ninguna |
| ¿Qué errores llevan los 3000 / Nak al invitado? | P3 + grifo | Nombres legibles |
| ¿Lejos, U-f vería las tiendas de la lista? | T1 | `[T1 would pass]` para smithy / butcher en Hernand lejos |
| ¿T2 / U2-c callados? | T2, U2-c | Sí (no hay escenas ni compras propias del invitado) |

---

## 6. Prueba en vivo de la fase 1 (dos copias en un PC)

**Preparación** (CLAUDE.md): copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups\` con la partida del invitado
separada; anfitrión con `cdmp.2inst-s46-probe-host.ini`, invitado con `cdmp.2inst-s45-activo2-guest.ini`; la segunda
copia con `SteamAppId`/`SteamGameId=3321460` y `PlatformServiceType=Steam`; papel, ini y log propios; Ctrl+F12 en las
dos ventanas en cada acción; dinero e inventario de los dos apuntados antes y después.

**Al arrancar (anfitrión), mirar:**
- `Config: S46 shopkeepers … shop_trade_probe 1 user2_shop_trade_route 'probe' observer_hold_scene_order 'probe'`;
- `ShopTradeProbe (shop_trade_probe=1, user2_shop_trade_route=probe; READ ONLY …) | hooked: P1 … P6 …; U2-c … |
  refused: none | … eErrNo names: N of 1346` — **cualquier `NOT installed` se apunta** (el resto sigue);
- `HoldSceneOrder: observer_hold_scene_order=probe (READ ONLY)`.

**Pasos:**
0. **Control del anfitrión, solo**: habla con el vendedor de armas de Hernand (`smithy`), compra algo barato y vende.
   Mirar: `ShopTradeProbe [P2a add]` del NPC con la clave `0x000F4321` y la escena del anfitrión (`in mgrH`); `[P5]`
   con `+0x12/+0x13/+0xBC` de `0x000F4321`; `[P6] … result 2`; `[P1] SERVER … for host … 2789 … -> PASS`; al salir de
   la escena `[P2 erase] … [would noop]`. Ningún 3000.
1. **El invitado cerca** (junto al anfitrión, en Hernand): intenta hablar con el tendero (hoy no puede). El anfitrión
   compra otra vez. Mirar: P1 del anfitrión `PASS`; ningún `[would orphan]`; el libro con el NPC y sin `[orphan]`.
2. **El invitado lejos** (≥ 11 celdas, `[ARMED]`), por el vendedor de armas o la carnicería de fuera: mirar
   `User2Stages [element] … cd_seq_funcnpc_smithy … SKIPPED … | S46 T1 …: [T1 would pass]` (o `would skip` si está
   cerca). T2 y U2-c deben estar callados.
3. **Relevos**: el anfitrión viaja a Hernand y se va (dos veces). Mirar cada `[P2 erase]` de tenderos: `would noop`;
   `ShopTradeProbe summary` con `swap 0` y `orphans 0`; un `[would orphan]` en un relevo se apunta (criterio en
   la lista de abortos).
4. **Salida del invitado** y, al final, el anfitrión vuelve a comprar al mismo vendedor (P1 `PASS`, sin 3000).

**Qué entregar:** `host.log` y `guest.log`, las capturas, y los resúmenes `ShopTradeProbe summary`,
`HoldSceneOrder summary` y `User2Stages` del final.

**Criterios de aborto (cualquiera para la prueba):**
- **`[would orphan]`** (WRN de P2): aborto si sale **fuera de una ventana de relevo**. Ventana de relevo = desde que
  uno de los dos jugadores empieza un viaje, una carga o un cambio de U-1 (`[ARMED]` / vaciado) hasta 5 s después de
  terminar. Dentro de esa ventana se apunta (id de escena, NPC, víctima) y se sigue: el relevo normal ya junta dos
  escenas sobre el mismo NPC durante ~1 s (§1.3), y es justo lo que R0 tiene que arreglar en la fase 2;
- **`[orphan]`** (WRN del libro): aborto **solo si** el id de escena de la entrada no aparece vivo en ningún gestor
  del log (buscarlo en las líneas `SceneDiag`/`ObserverStage`/`ShopProbe [funcnpc]`/`User2Stages` posteriores a su
  creación y sin su 2810). Si aparece vivo en otro gestor, es un posible falso positivo: el libro solo busca en `mgrH`
  y `mgr2`;
- un 3000 de tienda al anfitrión, o `ShopProbe [server check] … host … NOT found`;
- `[P1] SERVER … for host … FAIL` en una compra del anfitrión con la tienda abierta;
- dinero, inventario, stock o recompra cambiados en el jugador equivocado; un cambio de progreso del anfitrión;
- desaparece el tendero del anfitrión;
- cualquier `ABORT` de U-1 o U2; un bucle de 2661/2810; el atasco S14; un cierre o pérdida de cámara o control;
- cualquier desviación de los resúmenes validados (U-1, observer_hold_acks, S43–S45) frente a la línea base del kit 39.

---

## 7. Resultado fuera del juego (16-10)

- **Compilación limpia** (`build\` borrado) con `.\scripts\build.ps1`: verde, **84/84 tests** (nuevos:
  `optd_shop_trade`, `optd_hold_scene_order`, `optd_shop_troc`; ampliados: `optd_user2_shops`). Ningún aviso en el código
  nuevo (el único aviso de esos tests, C4127 en `optd_user2_shops_tests.cpp:145`, es anterior).
- `cdmp.asi` SHA-256 `b7ece6b21299bc0fa26012badc13cc57796eea1227901bd5b03e89a28541653d` (12 297 216 B), compilación limpia tras la revisión de la fase 1 (M1, B1, B2, B4).
- `cdmp_host` con ese asi y `cdmp.2inst-s46-probe-host.ini` como `cdmp.ini`, sin `CDMP_ROLE`, en
  `C:\Emulador CD\build\s46b\`, 4 s: `Loaded mod … Mod stopped and unloaded cleanly`, código 0. Línea:
  `Config: S46 shopkeepers (host, phase 1, read only): shop_trade_probe 1 user2_shop_trade_route 'probe'
  observer_hold_scene_order 'probe' | related: user2_shop_route '0' user2_stages_elements 'schedule' observer_hold_acks 1`.
  Fuera del juego no se llega a instalar ningún gancho (eso pasa al arrancar la sesión 2 en el juego).

---

## 8. Fase 2: R0 activo (regresión) y U2-c activo

Commits `dceabea` (código y tests) y `f684b7b` (inis), desde `f5afbee`; la revisión de la fase 2 (F1–F12,
`scratchpad\s46\revision-f2.md`) está aplicada después (§8.10). Todo va detrás de claves que por defecto valen 0: con
ellas a 0, ningún gancho nuevo se instala y el comportamiento es el de la fase 1.

| Clave | Valores | Dónde | Qué hace con `1` |
|---|---|---|---|
| `shop_interaction_own_delete` | `0` \| `probe` \| `1` | anfitrión (servidor) e invitado (cliente) | R0 en P2 (§8.1) |
| `user2_shop_trade_route` | `0` \| `probe` \| `1` | anfitrión | U2-c: `RAX := mgr2` en `0x21F28A1` (§8.4). En la fase 1, `1` se quedaba en probe; ahora se aplica |

R0 instala P2 aunque `shop_trade_probe=0`. En el invitado, R0 instala **solo** P2, con las mismas verificaciones que en
el anfitrión (§3.1) y dos bloques de contexto más: `0x2369845` (29 B, el cerrojo y el getter) y `0x2369867` (21 B, la
llamada al borrado y el desbloqueo). Los 5 bytes de P2 se dejan fuera de los dos bloques, para que P2a pueda seguir
comprobando su contexto con P2 ya puesto.

### 8.1 R0: diseño

**Punto:** P2, `0x2369862` (`mov edx,[rax] ; mov rcx,r12`, 5 B). Es el mismo gancho que en la fase 1: cerrojo
exclusivo del actor tomado y justo antes de `call 0x21245B0` (`jmp .xtls 0xE404A10`).

**Decisión** (`stp::plan_r0`, pura y probada):
1. `i_first` es la primera entrada con la clave del borrado; `i_own`, la primera con la clave y con `+0x20 ==` id de
   esta etapa (`[stage+0x30]`).
2. Casos sin cambios:

   | Caso | Resultado | Razón en el registro |
   |---|---|---|
   | Ninguna entrada con la clave | nativo | `NoMatch` |
   | `i_own == i_first` | `noop` | `OwnFirst` |
   | El vector tiene más de 32 entradas | nativo | `Incomplete` |
   | La clave de alguna entrada no está cargada (el borrado compara claves) | nativo | `KeyUnknown` |
   | El gestor de esta etapa no está demostrado (no está, está en los dos o falla una lectura) | nativo | `OwnUnknown` |
   | Otra entrada con la clave está en los dos gestores o no se puede leer | nativo | `Ambiguous` |
   | Ninguna entrada con la clave es del **otro** gestor | nativo: un solape dentro de un mismo gestor es nativo | `SameManager` |
   | Esta etapa **no tiene** entrada con la clave (`i_own < 0`) | nativo: el borrado quita la primera, como siempre (F1) | `NoOwn` |
   | La clave no es la de tienda `0x000F4321` | nativo (F2: el libro solo vigila tenderos) | `NotShopKey` |

3. Si ninguno de esos casos se da (`Cross`): **swap**. Se intercambian los bloques `i_first` e `i_own` (6 × u64 cada
   uno) y el borrado nativo quita la entrada propia. Es la única acción de R0: ningún registro cambia.

**Por qué no hay centinela (revisión F1).** Que una etapa no tenga entrada propia suele significar que una baja anterior
(nativa: `SameManager`, `OwnUnknown`, un tercer gestor…) se la llevó; la entrada que esa baja dejó atrás es justo la que
hay que quitar ahora, y el borrado nativo lo compensa solo. Un centinela («no quitar nada») la dejaba huérfana para
siempre. Contraejemplo: `[S1, S2]` de `mgrH`; sale S2 (nativo, quita la de S1); entra U de `mgr2`; sale S1 sin entrada
propia → con centinela queda la de S2, ya salida, en cabeza (el síntoma de p2b). La propiedad sobre secuencias
aleatorias (§8.10) lo mide.

**Restricción «entre gestores».** R0 solo actúa si alguna entrada con esa clave pertenece al otro gestor de este lado.
Con una escena por NPC, o con varias del mismo gestor, R0 no toca nada. Cada lado tiene dos gestores:

| Lado | A | B | Etapas que mira |
|---|---|---|---|
| Anfitrión | `mgrH` | `mgr2` | solo las del procesador de servidor `0x5B1FE10`; las del cliente del anfitrión `0x558C080` solo se cuentan |
| Invitado | las X anotadas por `guest_x_interaction` | M (fresca, `guest_x_veto`, ≤ 2 s) | solo las del procesador de cliente: vtable `0x558C080` o la copia R-A del mod, con el COL del cliente en `[-1]`; y solo con `guest_remote_live` |

Pertenencia a un gestor: `isl::find_stage`, con la instancia viva o marcada como muerta (`+0x15`). Una etapa que se está
borrando sigue siendo de su gestor. En el anfitrión, el gestor de esta etapa se busca igual por su id. En el invitado,
es el `[proc+8]` del procesador (el G de `guest_x_veto`) comparado con M y con las X.

**Cuándo se escribe** (`1`), además del plan:
- `[comp+8] == actor`: el cerrojo que lleva la baja (`[actor+8]`) es el mismo objeto que bloquean los lectores
  (`[[comp+8]+8]`);
- la vtable de `[actor+8]` es una de las 11 `pa::WindowsRWLock` de la build (F5; se registra en la línea P2);
- el vector y la cuenta no han cambiado desde la lectura, y el bloque `i_own` sigue llevando el id de esta etapa;
- `VirtualQuery`: todo el vector `[vec, vec + n × 0x30)` es memoria `MEM_COMMIT` de lectura y escritura (F8).

Si falla una comprobación, no se escribe (`a check refused`, F7). Las 12 escrituras van con SEH; si una falla
(`WRITE FAULTS`), se restaura lo que se pueda y R0 queda en nativo el resto de la partida (`latched native YES`).
Con `shop_interaction_own_delete=1` y `shop_trade_probe=0` sale un WRN: R0 activo sin libro (F12).

**Probe:** solo lo que ya registra P2. La línea `[P2 erase]` añade
`| R0 (shop_interaction_own_delete=probe): probe: would swap …` o `noop|native (<razón>)`, la vtable del cerrojo, y cada entrada de
la lista lleva su gestor (`in mgrH`/`in mgr2`, o `in X`/`in M` en el invitado). Con `1` dice `APPLIED …` y
`[orphan avoided]`.

**Resumen nuevo** cada 60 s: `ShopTradeProbe R0 / U2-c summary (host|guest)`. Incluye:
- los planes `noop`/`native`/`swap` y sus razones;
- lo aplicado y lo no aplicado (`latched`, `lock not proven`, `a check refused`, `WRITE FAULTS`);
- `latched native`;
- `[would orphan] exits N: avoided by R0 A, left L (of them with another manager's entry C)`. `C` se calcula aparte
  (`stp::cross_of`, F6), sin las salidas tempranas del plan: cuenta también el caso H1 (la etapa ya fuera de su gestor);
- `guest overlaps`;
- U2-c decididos y aplicados.

El libro (con `shop_trade_probe=1`) reproduce la operación con el plan aplicado: swap y borrado.

### 8.2 Demostraciones (desensamblado, build 2976)

Fuera del juego: `s46-datos/nativo/verify_f2.py` → `verify_f2.out` (capstone sobre el exe).

**D1. En `0x2369862`, la clave y el componente están en registros, y cambiarlos solo afecta a esa llamada [V].**

```
0x236985D call 0x3888B0              ; rax = InteractionInfo de [func+0x38] (fila de la tabla estática compartida)
0x2369862 mov edx,[rax]              ; <- P2: edx = la clave que compara el borrado
0x2369864 mov rcx,r12                ; rcx = componente de interacción del NPC
0x2369867 call 0x21245B0             ; = jmp .xtls 0xE404A10(rcx, edx): sus dos únicos parámetros (de404a10.txt)
0x236986C nop
0x236986D mov rsi,[rbp-0x38]         ; el objeto del cerrojo
0x2369871 mov rax,[rsi]              ; rax se reescribe
0x2369874 xor edx,edx                ; rdx se reescribe
0x2369879 call [rax+0x20]            ; desbloqueo (dl = 0)
```

- En el punto, la clave todavía no está en `edx`: la carga la propia instrucción parcheada desde `[rax]`. «El registro
  que corresponde» es, por tanto, **`rax`**.
- `[rax]` es la fila **compartida** de la tabla `InteractionInfo`: nunca se escribe. Desde la revisión F1, R0 ya no
  cambia `rax` (no hay centinela): P2 solo lee registros.
- Después del punto, `rax` solo se lee en ese `mov`, y `rdx` solo lo lee el borrado. Los dos se reescriben antes de
  volver a usarse (`0x2369871`, `0x2369874`), y además son volátiles en la ABI x64.
- La vuelta del bucle (`jne 0x23697C3`) recarga `rax`, `rsi`, `r14` y `r13` desde el marco.
- R0 no toca `r12` ni `rcx`.
- El único salto que entra en el bloque es `0x23695AE je 0x2369845`, que cae en su primer byte. Ninguno entra en
  `0x2369846..0x236987B`.

**D2. Intercambiar dos entradas de 0x30 B es seguro.**

- **Sin punteros a sí mismas [V-x].** El borrado nativo (`de404a10.txt`) mueve la última entrada al hueco **copiando
  bits**:
  - las cadenas de `+0x00` y `+0x18`, que son identificadores de 8 B: copia el valor y deja la cadena vacía estática
    en el origen;
  - el puntero con cuenta de referencias de `+0x08`: copia el valor y pone 0 en el origen;
  - los campos simples `+0x10`, `+0x14`, `+0x20`, `+0x28`, `+0x2C` y `+0x2D`.

  Después destruye el origen, que ya está vacío. Si un objeto apuntara a sí mismo, el propio juego lo rompería al
  moverlo. Un intercambio son dos de esos movimientos sin vaciar ni destruir: cada identificador sigue teniendo un
  solo dueño y ninguna cuenta de referencias cambia (test `test_r0_apply`: 18 valores, cada uno una vez).
- **Con el cerrojo tomado [V código / E semántica]:**
  - la baja toma `0x393650(&[rbp-0x38], [actor+8], r8b = 1)` en `0x2369853`. Esa función llama a la `vf18` del
    objeto con **`dl = 0`** y lo suelta en `0x2369879` con `vf20(dl = 0)`;
  - el alta toma el mismo cerrojo en `0x23695C2`;
  - el lector `0x2124060` toma `0x393DF0([[comp+8]+8])` con **`dl = 1`** y lo suelta con `vf20(dl = 1)`;
  - `dl = 0` es exclusivo y `dl = 1` compartido [V, revisión]: las 11 `pa::WindowsRWLock` comparten `vf18 = 0x1371AD0`
    (`dl ≠ 0` → `AcquireSRWLockShared`; `dl = 0` → `AcquireSRWLockExclusive`, recursivo) y `vf20 = 0x1371B70`
    (comprobado otra vez en el exe). En vivo queda por ver que `[actor+8]` sea uno de ellos: R0 lo exige y lo registra;
  - R0 exige `[comp+8] == actor`: el objeto que bloquean los lectores es el mismo que tiene tomado la baja.
- **Los lectores trabajan sobre copias [V]:**
  - `0x2124060` copia el vector con `0x2123710` a un arreglo local (`rsp+0x30`, capacidad `0x40`) bajo el cerrojo, lo
    suelta y recorre la copia;
  - nadie guarda índices ni punteros a las entradas (`nativo.md` §2.5, H3);
  - `0xE411950`, que se ejecuta tras cada borrado, recalcula `comp+0x128`/`+0x129` con «alguna entrada…», que no depende
    del orden, y sube la versión. En el caso swap, el borrado nativo la sube igualmente.
- **El borrado del swap quita la propia:** las entradas anteriores a `i_first` no llevan la clave, y tras el
  intercambio `i_first` contiene la propia, que sí la lleva.
- **El orden cambia (revisión F11).** La entrada `i_first` acaba en `i_own` (y, si `i_own` era la última, vuelve al hueco
  con el movimiento nativo). La entrada que queda primera con la clave, la que toma el 2671 por `0x2123710`, puede no
  ser la que dejaría un borrado «ideal» de la propia. No es un error: en la propiedad sobre secuencias, los pasos con la
  entrada de una etapa ya salida en cabeza bajan de 26 525 (nativo) a ~7 674 (R0).

**D3. U2-c: el gestor que devuelve `vf48` solo se usa para su cerrojo de lectura y su etapa actual [V].**

`0x21F28A1`:
- `mov rbx,rax`;
- `lea rdx,[rax+8]` y `0x393DF0` (`dl = 1`), con desbloqueo en `0x21F2914`;
- `[rsp+0x38] = rbx`, que se escribe y no se vuelve a leer en la función (`verify_f2.out`);
- `+0xA0`/`+0x90` → `r12` y `[rsp+0x28]`.

Después, `rbx` se reescribe (`0x21F29AA`, `0x21F2A36`, `0x21F2C2C`…) antes de cualquier otro uso. Con `mgr2`, el juego
toma el cerrojo de lectura de `mgr2` y compara su etapa actual. No se escribe nada en ningún gestor.

**D4. Las etapas del cliente del anfitrión nunca:** filtro por la vtable del procesador (`[[stage+0x58]]`), igual que
en la fase 1 [V RTTI].

### 8.3 ¿R0 en el invitado? Decisión: probe

| Hecho | Marca |
|---|---|
| En el cliente del invitado, `SetInteraction` de X se ejecuta de forma nativa (`guest_x_ui_veto_allow=SetInteraction`): los tenderos llevan entradas con ids de X | [V] S45 `GuestXInter` |
| S45: 22 + 22 escenas de tienda en el cliente, todas de X; **0 propias** (M). Ninguna tienda tiene hoy entradas de M | [V] `scan_s45.json`, §1.1 |
| Con solo entradas de X sobre un tendero, el plan es siempre `noop`/`SameManager`: R0 = 1 en el invitado no haría nada en las tiendas | [V] lógica (`test_r0_plan`) |
| En otros NPC (diálogos de X y de M sobre el mismo NPC) no hay **ninguna** medida de solapes X/M | — |
| El cliente tiene un registro por etapa (`ClientSequencerStage vf120 = 0x605A40`, `vf128 = 0x606030`), sin seguir | [H] |

**Decisión:** `cdmp.2inst-s46-p2-guest.ini` = `cdmp.2inst-s45-activo2-guest.ini` + `shop_interaction_own_delete=probe`.
- Documenta qué entradas hay en el cliente (cada una con `in X`/`in M`) en cada baja de un tendero o con dos escenas
  sobre la misma clave.
- El resumen da `guest overlaps`.
- No escribe nada.
- Pasará a `1` solo si la prueba mide solapes X/M reales; con T5 (fase 4), las tiendas de X dejarán de tener entradas
  en el cliente.

Si se quiere el invitado exactamente como en la fase 1, `cdmp.2inst-s45-activo2-guest.ini` sigue valiendo.

### 8.4 U2-c activo

En `0x21F28A1`, `RAX := mgr2` (`stp::decide_u2c` → `WouldRoute`) **solo** si se cumple todo esto:
- el gestor de `vf48` es un `ServerSequencerStageManager` y es `mgrH`;
- el componente de tienda es del usuario 2 (`shp::who`) **y** el Troc de este hilo es suyo (`user2_funnels_subject_now`;
  no hay subject si el gancho no está);
- el campo `[rbp-0x51]` es del anfitrión (`user2_stages_host_field`);
- `mgr2` es conocido (`user2_stages_user2_manager`, vtable comprobada);
- U-1 está armado (`user2_stages_armed`), `[mgr2+0x6A0] == 2` y el usuario 2 no se está yendo
  (`server_logout_requested`);
- el subject del Troc es **el mismo actor** que el del componente de tienda (F9);
- una entrada de tienda del NPC (`0x000F4321`, con id de escena) está **viva en `mgr2`**, y **ninguna** viva en `mgrH`
  (F3: si no, `NpcAlsoInMgrH`, porque la etapa actual del NPC puede ser la del anfitrión y el enrutado convertiría un
  PASS nativo en `eErrNoInvalidStageInfo`). Las entradas se leen sin el cerrojo del NPC y con SEH; si la lectura no es
  completa, no se enruta.

Nunca se enruta la petición del anfitrión, ni un `mgr2` desconocido o desarmado. El carril (fase 5) podrá abrir el caso
de cerca. En `probe` se registra `[would route to mgr2]` con todas las condiciones; con `1`, `[ROUTED to mgr2]`.
`cdmp.2inst-s46-p2-host.ini` lo deja en **probe**: todavía no hay escenas de tienda del invitado (fase 3).

### 8.5 Criterios de aborto (prueba de la fase 2)

Cualquiera de estos detiene la prueba:
- cualquier `[orphan]` del libro (con la salvedad de §6: buscar antes el id en un tercer gestor);
- en el resumen R0, `left … (of them with another manager's entry C)` con **C > 0**: un borrado entre gestores que
  R0 no evitó. Si su razón es `NoOwn`, es el caso F1 (nativo a propósito): se apunta y se mira si a continuación sale un
  `[orphan]`; con otra razón, aborto;
- un `APPLIED swap` en un NPC cuya lista **no** tenga a la vez entradas `in mgrH` e `in mgr2` (R0 fuera de un solape);
- `WRITE FAULTS` > 0 o `latched native YES`;
- `lock not proven` > 0, o una línea P2 con `NOT a WindowsRWLock` (R0 no escribe, pero la suposición del cerrojo falla:
  parar y revisar);
- `[P1] SERVER … for host … FAIL`, o un 3000 de tienda al anfitrión, en una compra suya con la tienda abierta;
- el tendero del anfitrión desaparece, o le cambian dinero, inventario o stock al jugador equivocado;
- cualquier `ABORT` de U-1 o U2, un bucle de 2661/2810, el atasco S14, un cierre o un cuelgue;
- cualquier desviación de los resúmenes validados (U-1, `observer_hold_acks`, S43–S45) frente a la fase 1.

APAGAR: `shop_interaction_own_delete=probe` (o `0`).

### 8.6 Lo esperado [E]

- Sin escenas de tienda del invitado, casi todas las bajas son `noop` y `native (NoMatch/SameManager)`.
- R0 solo puede actuar en un relevo: el anfitrión llega a celdas donde `mgr2` armado tenía escenas sobre los mismos NPC
  con la misma clave (diálogo o tienda) durante ~1 s.
- Si ese solape no ocurre, R0 no hace nada en toda la prueba. Eso es válido como regresión: se apunta `swap 0`.

### 8.7 Prueba en vivo (dos copias en un PC)

**Preparación (§6):**
- anfitrión con `cdmp.2inst-s46-p2-host.ini` e invitado con `cdmp.2inst-s46-p2-guest.ini`;
- copia de las partidas, con la del invitado separada;
- Ctrl+F12 en cada acción;
- dinero e inventario de los dos jugadores apuntados.

**Al arrancar, mirar:**
- `Config: S46 shopkeepers (phase 2): … shop_interaction_own_delete '1' …` (en el invitado, `'probe'`);
- en el anfitrión, `ShopTradeProbe (host server; … shop_interaction_own_delete=1; ACTIVE: R0 swaps …) | hooked: P1 …
  P2 0x2369862 (5 B); …` y `refused: none`;
- en el invitado, `ShopTradeProbe (GUEST client, R0 only; … shop_interaction_own_delete=probe; READ ONLY …) | hooked:
  P2 0x2369862 (5 B);`;
- cualquier `NOT installed` se apunta. Sin P2, R0 no hace nada y lo dice con un WRN.

**Pasos (regresión: el anfitrión compra con el invitado presente):**
0. **Control:** el anfitrión, solo, compra y vende en el vendedor de armas de Hernand. Mirar:
   - `[P1] … host … PASS`;
   - al salir, `[P2 erase] … R0 (shop_interaction_own_delete=1): noop (its own entry is the first)`.
1. **Invitado cerca** (junto al anfitrión): el anfitrión compra. Mirar P1 `PASS`, R0 `noop`/`native` y ningún
   `[would orphan]`.
2. **Invitado lejos** (≥ 11 celdas, `[ARMED]`): el anfitrión compra en Hernand. Mirar lo mismo, y U2-c callado (no hay
   compras del invitado).
3. **Relevo:** con el invitado armado lejos, el anfitrión viaja hacia las celdas del invitado y vuelve, dos veces. Cada
   `[P2 erase]` entre gestores tiene que decir `APPLIED swap … [orphan avoided]` (o `native (no own entry …)`), con la lista mostrando las
   dos entradas (`in mgrH`, `in mgr2`). Ningún `[orphan]` en los 2 s siguientes. Después del relevo, el anfitrión
   vuelve a comprar: P1 `PASS`, sin 3000.
4. **Final:** el invitado sale; el anfitrión compra otra vez. Entregar `ShopTradeProbe summary`, `ShopTradeProbe R0 /
   U2-c summary` (de los dos) y los resúmenes U-1 y S43–S45.

**Qué entregar:** `host.log`, `guest.log`, las capturas, los dos resúmenes R0 y los apuntes de dinero e inventario.

### 8.8 Resultado fuera del juego (16-10)

- **Tras la revisión (§8.10), compilación limpia** (con `build\` borrado) con `.\scripts\build.ps1`: verde, **84/84
  tests** de ctest. `optd_shop_trade` tiene 186 comprobaciones (100 en la fase 1, 173 antes de la revisión); las de la
  revisión son `NoOwn`, `NotShopKey`, `cross_of`, las vtables del cerrojo, el contraejemplo de F1, `NpcAlsoInMgrH` y
  `test_r0_sequences`.
- `cdmp.asi` tras la revisión: SHA-256 `245b3d2f71dee336fd20f0d9f01ff7d7fa5bf39060288c098d5b59dc705135ef`
  (12 326 400 B). `cdmp_host` con los dos inis p2 (`build\e2e\p2h`, `build\e2e\p2g`), 4 s: carga y descarga limpias,
  código 0, la misma línea `Config: S46 …`. `dumpbin`: `on_si`, `on_u2c`, `swap_entries`, `writable`, `write_swap`,
  `plan_r0` siguen sin `memset`/`memcpy`/`__chkstk`/`ymm`/`__report_rangecheckfailure`; `writable` llama a
  `VirtualQuery`.
- Avisos (antes y después de la revisión): el código nuevo no da ningún aviso; los que salen (C4127 en `optd_user2_shops_tests.cpp:145` y
  `optd_fast_travel_tests.cpp:205`, C4996, C4244 de la STL y C4005 de safetyhook) son anteriores.
- `cdmp.asi` antes de la revisión (commit `f684b7b`): SHA-256 `239dc2cc22b7c6b155f21d59cbdbaa07b845ba71c4d5d8e438a9f531587ae01e`
  (12 325 376 B).
- `dumpbin /disasm` de `shop_trade_probe.obj`:
  - desde `on_si`, `on_u2c`, `swap_entries`, `write_swap`, `side_now`, `where_in` y `own_where_guest` solo se llama a:
    - lecturas SEH y atómicos;
    - `find_stage`, `plan_r0` y `decide_r0`, que son puros;
    - los accesores atómicos de `user2_stages`, `user2_funnels`, `guest_x_veto`, `guest_x_interaction` y `server`;
    - `WindowLimiter::take`, `p8_now_us`, `GetCurrentThreadId` y `__security_check_cookie`;
  - no hay `memset`, `memcpy`, `__chkstk`, `ymm` ni `__report_rangecheckfailure`; los índices de `ew` se enmascaran
    para que /GS no emita esa comprobación;
  - el marco de `on_si` es de 0x468 B.
- `verify_f2.py` → `verify_f2.out`: los 13 bloques de contexto coinciden con el exe y ninguno se solapa con un parche.
  También da los registros de D1, los saltos que entran en el bloque de P2 y los usos del gestor en U2-c (D3).
- `cdmp_host` con ese asi, sin `CDMP_ROLE`, en `build\e2e\p2h` (`cdmp.2inst-s46-p2-host.ini` como `cdmp.ini`) y en
  `build\e2e\p2g` (`cdmp.2inst-s46-p2-guest.ini`), 4 s cada uno: `Loaded mod … Mod stopped and unloaded cleanly`, código
  0. Líneas:
  - `Config: S46 shopkeepers (phase 2): shop_trade_probe 1 (host, read only) user2_shop_trade_route 'probe' (host,
    U2-c) observer_hold_scene_order 'probe' (host, read only) shop_interaction_own_delete '1' (R0, host and guest) |
    related: user2_shop_route '0' user2_stages_elements 'schedule' observer_hold_acks 1`;
  - en el invitado, `… shop_interaction_own_delete 'probe' …`.

  Fuera del juego no se instala ningún gancho: eso ocurre al arrancar la sesión 2 en el juego.
- **Archivos:**
  - `shop_trade_probe.{h,cpp}` y `shop_trade_probe_logic.h`, con `plan_r0`, `apply_r0`, `swap_blocks`, `U2cIn`/`decide_u2c`
    y `u2c_npc`;
  - `guest_x_veto.{h,cpp}` (`guest_x_current_m`) y `guest_x_interaction.{h,cpp}` (`guest_x_interaction_known_x`);
  - `guest.cpp` (arranque en el invitado), `session2.cpp`, `config.{h,cpp}` y `mod.cpp`;
  - `tests/optd_shop_trade_tests.cpp`;
  - los inis `cdmp.2inst-s46-p2-host.ini` y `cdmp.2inst-s46-p2-guest.ini`;
  - `s46-datos/nativo/verify_f2.{py,out}`.

### 8.9 Lo que no está demostrado (fase 2)

- **Nada de la fase 2 ha corrido en el juego.** R0 con `1` escribe en la memoria del juego (el vector de entradas del
  NPC): hace falta la copia de las partidas antes de la prueba (CLAUDE.md).
- [H] Que `[actor+8]` de un tendero sea un `pa::WindowsRWLock` (la semántica de esas 11 vtables ya es [V]). R0 lo exige
  y lo registra.
- [E] Que la etapa que se para siga en su gestor (viva o muerta) cuando su `SetInteraction` sale (H1, «parada → salida»
  sin seguir). Si no sigue: `OwnUnknown` → nativo. R0 no haría nada, sin riesgo; el resumen lo cuenta.
- [E] `[comp+8] == actor` (el cerrojo de los lectores). Si no se cumple, R0 no escribe (`lock not proven`) y el
  resumen lo dice.
- La gemela cuya alta falló (condición inválida) ya no está cubierta: sin entrada propia, R0 es nativo (F1). Es raro y
  el juego lo avisa con `DynamicInteraction Condition … 비정상`.
- [H] Que los ids de etapa no coincidan entre `mgrH` y `mgr2` (ni entre X y M). Si un id está en los dos: `Ambiguous` o
  `OwnUnknown` → nativo.
- [H] Que en esta prueba haya algún solape entre gestores. Sin tiendas del invitado puede no haber ninguno, y entonces
  R0 no se ejercita en el juego: solo en los tests (p2b reconstruido).
- [H] El cliente del invitado: si un solape X/M existe en otros NPC y si el registro de interfaz por etapa del cliente
  (`vf120`/`vf128`) depende del orden de las entradas. Por eso R0 va en probe en el invitado.
- U2-c con `1` no se ha ejercitado. Hace falta una compra del invitado con escena propia (fase 3). Las entradas del
  NPC se leen sin su cerrojo (F10: el riesgo es solo de decisión; con F3 el caso malo desaparece).
- La propiedad sobre secuencias es un modelo (un NPC, una clave, cada etapa con un alta y una baja, el gestor de cada
  entrada conocido): no cubre `OwnUnknown`, `Ambiguous` ni lecturas fallidas, que son nativos.

### 8.10 Revisión de la fase 2 aplicada

Informe: `scratchpad\s46\revision-f2.md` (y su `sim.cpp`). Aplicado en el commit de la revisión (§8.8):

| # | Hallazgo | Cambio |
|---|---|---|
| F1 (P1) | El centinela deja huérfanas que el nativo compensa (729 sobrantes frente a 0) | Quitado. `i_own < 0` → nativo (`NoOwn`). R0 ya no cambia ningún registro |
| F2 | R0 actuaba sobre cualquier clave; el libro solo vigila tenderos | R0 solo con `0x000F4321` (`NotShopKey` para las demás) |
| F3 | U2-c enrutaba con una entrada viva de `mgrH` también | `NpcAlsoInMgrH`: no enruta. Test corregido |
| F4 | `1` se aceptaba en el invitado | En el invitado `1` queda en probe, con un WRN |
| F5 | El tipo de cerrojo no se comprobaba | `lock_ok` exige una de las 11 vtables `pa::WindowsRWLock` (`vf18 = 0x1371AD0`); la línea P2 la registra |
| F6 | `C` contaba de menos | `stp::cross_of`, sin salidas tempranas |
| F7 | Comprobación y escritura se contaban igual | `a check refused` (no bloquea R0) frente a `WRITE FAULTS` (lo bloquea) |
| F8 | Sin comprobar la memoria antes de escribir | `VirtualQuery` sobre todo el vector |
| F9 | El subject no se comparaba con el actor de la tienda | `subject == owner` |
| F11 | El intercambio cambia el orden | Anotado en D2 |
| F12 | R0 = 1 sin libro | WRN al instalar |

**Propiedad sobre secuencias** (`test_r0_sequences`, como `sim.cpp`): 20 000 intercalados aleatorios de altas y bajas
de 1–3 etapas de `mgrH` y 1–2 de `mgr2` sobre un NPC, nativo frente a R0 (solo intercambio), con la etapa viva o muerta
al salir. Se exige que R0 nunca deje más entradas sobrantes que el nativo, que deje 0, y que no tenga más pasos con la
entrada de una etapa ya salida en cabeza. Resultado (las mismas cifras que `sim.cpp`):

| Etapa al salir | Sobrantes nativo | Sobrantes R0 | Pasos con una entrada ya salida en cabeza: nativo / R0 | Intercambios |
|---|---|---|---|---|
| viva | 0 | **0** | 26 525 / 7 674 | 19 189 |
| muerta | 0 | **0** | 26 525 / 7 676 | 18 625 |

Con el centinela, la misma simulación de la revisión daba 729 sobrantes.

## 9. Fase 3: el invitado lejos comercia con su propia tienda (T1 + T2 + U2-c + U2)

Commits `2f20070` (código y tests) y `0bbde91` (inis), desde `4d26882`. Todo va detrás de claves que por defecto valen
0: con ellas a 0 (o en probe) no cambia nada respecto a la fase 2. **No se ha abierto el juego ni desplegado.**

| Clave | Valores | Dónde | Qué hace con `1` |
|---|---|---|---|
| `user2_shop_trade_list` (nueva) | `0` \| `probe` \| `1` | anfitrión, con `user2_stages` | T1 (§9.1) |
| `observer_hold_scene_order` | `0` \| `probe` \| `1` | anfitrión, con `observer_hold_acks=1` | T2 (§9.3); en las fases 1 y 2, `1` quedaba en probe |
| `user2_shop_trade_route` | `0` \| `probe` \| `1` | anfitrión | U2-c (§8.4), sin cambios |
| `user2_shop_route` | `0` \| `probe` \| `1` | anfitrión, con `shop_probe=1` | U2-a / U2-b / espera de vaciado (`tiendas-u2.md` §11), sin cambios |

### 9.1 T1: la lista de comercio por hash exacto

**Qué hace** (`user2_stages.cpp` `elem_detour`, `u2sh::element_verdict`):
- con U-1 armado, U-f lee el nombre base del recurso del elemento, como antes, y calcula su **hash del diccionario**
  (`u2trade::dict_hash`);
- un `cd_seq_funcnpc_*` cuyo hash está en la lista (`0xA7132561` smithy, `0x978345CA` butcher) pasa (`LET THROUGH`) si
  la ruta puede preparar (`user2_shops_may_prepare`) y el usuario 2 está a ≥ 10 celdas sin la espera de vaciado;
- cualquier otro `funcnpc` se salta como hoy, con el veredicto nuevo `SkipNotTrade`;
- los horarios y los demás secuenciadores no cambian (el salto propio de U-1).

**El hash es exacto [V].**
- El hash del diccionario es `lookup3 hashlittle(nombre, 0x000C5EDE)`. Coincide en los **156 de 156** nombres de
  `s46-datos/funcnpc.json` (comprobado fuera del juego con Python).
- El código lo calcula en tiempo de compilación para los dos nombres (`static_assert`); el test comprueba además otros
  dos nombres del diccionario.
- Solo se usa un nombre leído **entero** (no cortado a 40 B).
- U-f no ve el hash del recurso: está en `playData+0x20` y sale después de preparar. Por eso se calcula del nombre.

**T1 frente a `schedule+funcnpc`. Decisión: T1 lo sustituye.**
- `schedule+funcnpc` deja pasar **todas** las `funcnpc` de la celda. En Hernand, la evaluación del anfitrión prepara 11
  [V, p1b `host.log:18881-18891`]: `bank`, `butcher`, `contribution`, `craftsmith`, `goblinbroker`, `grocery`,
  `leather`, `smithy`, `general`, `clothes` y `pub` (más `church`, `:1664`). Nueve de ellas no tienen ninguna traza de
  comercio (`funcnpc_trade_policy.h`: «never widen this list»).
- En p2b, la rama de la otra IA tenía su propia lista por nombre: «SKIPPED (W4: resource is not on the exact trade
  allowlist) 9» frente a «LET THROUGH 2» [V, p2b `host.log`, el resumen U-1].
- Con `user2_shop_trade_list=1`, la clave `user2_stages_elements` **ya no decide** para las `funcnpc`: la lista manda.
  - Con `schedule` (la de los inis), T1 abre exactamente smithy y butcher.
  - Con `schedule+funcnpc`, T1 la **estrecha** a la lista (lo dice la línea de arranque).
  - No hace falta poner `schedule+funcnpc`, y el ini de la fase 3 no lo pone. Test `test_t1_active`.

**Aborto de T1.** En el punto P de ShopProbe (`user2_shops_prepared`), si `mgr2` prepara una `funcnpc` cuyo hash de
`playData` **no** está en la lista:
- T1 se bloquea para toda la partida (todas las `funcnpc`, `SkipNotTrade`, sea cual sea `user2_stages_elements`);
- la ruta U2 pasa a probe (`User2Shops [ABORT] … S46 T1 …`, ERR; aborto `NotTradeList`).

**Probe.** Las líneas `User2Stages [element]` de las tiendas saltadas dicen `[T1 would pass|would skip] … (hash 0x…)`,
como en la fase 1, pero ahora también con `user2_shop_trade_list=probe`, sin necesidad de `shop_trade_probe`. No se
prepara nada. Con `1`, cada veredicto de `funcnpc` lleva `S46 T1 (ACTIVE, by the exact hash 0x…)`, y el resumen U-1
cuenta `S46 T1 (ACTIVE …)`.

### 9.2 El tendero de la escena del invitado: ¿lo adoptan U-e o las familias B?

**Respuesta: hoy no [V]. Por seguridad, con T1 se le excluye por su escena de origen.**

| Pieza | Selección | El tendero de `0x143` (p2b) |
|---|---|---|
| U-e (`npc_control_impl`, en el mid `0x2792A7B` de docked_gimmicks) | La primera condición es el descriptor `CreateServerActorDesc_NPCSchedule` (`exe+0x5B04CB0`); luego `type == 3` y el resto (`u2s::ctl_verdict`, `user2_stages_logic.h:712-728`) | Descriptor base `exe+0x5B04BE8`, tipo 4 → `OtherDesc`. El resumen U-1 lo cuenta: «other descriptors under its armed timers: … [vtable exe+0x5B04BE8 type 4] x2» [V, `host.log:14665`, igual hasta `:35887`]. Aunque fuera NPCSchedule, pararía en `Type` |
| Familias B (S36, `npc_family_control`) | Raíz con subárbol de descriptores (`+0x2F8`/`+0x300`); `fam_host_root` solo registra raíces con hijos; miembros de las cohortes 3 y 6 | Raíz sin hijos («padre 0x00000000 hijos 0», `host.log:12504-12505`) → no se registra ni escribe nada |
| Carreteras (S38) | Descriptor base, **marca de carretera** `+0x3CD == 1` con solicitante `0xA…`; tipos 3/5/6 | «solicitante 0x00000000 carretera 0x0» → `NotMarked`; además, tipo 4 está fuera de la cohorte |

- En p2b, ninguna de las 297 líneas `User2Stages [control]` es de `B01002EB`/`B01002EC`, y ninguna línea NpcFamily,
  NpcActivation ni User2Road los nombra.
- Su controlador es el anfitrión en todo momento: al nacer (`:12504-12505`), en H1 (`:12596`, `:12599`), en P8Probe
  (`:13801-13804`), y hasta la baja con motivo 25 (`:38748`, `:38753`).
- Nacen con controlador anfitrión **porque así lo escribe el juego**: la rama del dueño del campo, `[field+0x180]`.
- **El riesgo que queda:** un tipo 4 que apareciera como hijo de descriptores de una unidad admitida la partiría
  (`npc_family_logic.h:516`). No es el caso del tendero, pero no hay garantía para lo que genere otra escena de tienda.

**Exclusión por la escena de origen** (`elem_detour` y `user2_stages_npc_control`):
- mientras corre el original de `0x288A970` de un elemento `PassFuncnpc` (con `user2_shop_trade_list` en probe o 1),
  una marca de hilo guarda el id estático de esa etapa;
- todo nacimiento en el mid `0x2792A7B` con esa marca **no pasa** por U-e, las familias ni las carreteras: se queda con
  el controlador que escribió el juego;
- sí pasa por `npc_observe_birth` (lápidas, registro de activación);
- nunca se decide por el nombre del NPC ni por su clave;
- en probe solo se cuenta: `births inside a trade scene's prepare N (U-e / families / roads changed M)`.

**[E] Que el nacimiento sea síncrono dentro de `0x288A970`, en el mismo hilo.** En p2b, el qpc del nacimiento de
`B01002EB` (`142518.577967`) es 117 µs anterior al punto P de la preparación de `0x142` (`.578084`, `T32788`). Si no
fuera síncrono, la marca no estaría puesta, la exclusión no actuaría (el resumen diría `births … 0` con tiendas
preparadas) y quedaría lo de la tabla: U-e y B no lo adoptan.

### 9.3 T2: retener la escena del invitado o liberar antes el 2005. Decisión: retener

**Por qué no se libera antes el 2005 del tendero [V].**
- El 2005 lo retiene `observer_hold` (child_hold), no `publication_gate`: el tendero nace con el byte de restauración a
  0 (`[[CC+0x40]+0x30]`) y su controlador es el anfitrión, así que se le oculta al usuario 2 hasta que el cliente del
  anfitrión le da su primera acción (StartActionReq).
- En p2b, la puerta de publicación **aprobó** las dos liberaciones: «restore 1 (after StartActionReq)», 344 y 375 ms
  (`host.log:12513`, `:12517`).
- La puerta solo retendría una liberación por tiempo (10 s) que siguiera en restauración 0 (`Restore0`; en p2b, 549 de
  1789 en el resumen del minuto 50, `:35776`).
- Liberarlo antes es exactamente la publicación `Restore0` que prohíbe el invariante de S35 (`s35-fase-a.md` §1.4). El
  cliente del invitado construiría el NPC sin acción actual: el 2723 está vetado en un cliente remoto y los lectores
  `0x232D5FF`, `0x88F832` y `0x1831598` desreferencian la acción nula (`p8-raiz-invitado.md` §5, `AUDITORIA.md:67-70`).
  Fueron cierres reales.
- No existe ninguna vía de liberación anticipada, y saltarse la retención en `prepare` dejaría fuera a
  `publication_gate` por completo.
- **Descartado.**

**Por qué retener es seguro en ese punto** (`observer_hold_acks_route`, la ruta de `take_frame` a la sesión 2 viva):
- **Sin llamadas al juego:** solo paquetes ya serializados. Nada de memoria del juego ni ganchos nuevos.
- **Cerrojo:**
  - es el de `observer_hold_acks` (`g_mu`, hoja: dentro solo se toma el de la cola de salida), con el mismo orden
    atómico que sus `Pushed`;
  - el 2661 se retiene bajo `g_mu`, y el 2005 que lo suelta encola, bajo el mismo `g_mu`, el 2005, los paquetes del
    propio `observer_hold_acks` y los de la escena, **fusionados por su secuencia original** (un solo contador,
    `AckHold::next_seq`);
  - ningún paquete de esas claves ni de ese id puede colarse entre medias.
- **Los observadores que guardan estado** (revisados uno a uno en `server.cpp` `take_frame`):
  - `observer_scene_copy`: su libro anota el 2661 y el 2810. Para una escena **de `mgr2`**, el 2810 nativo ya va al
    usuario 2 (`AlreadyToUser2`, que se comprueba **antes** que el libro: `observer_scene_copy_logic.h:218-221`). Un
    2661 que llega tarde no cambia ninguna copia.
  - `observer_seq_copy`: solo apunta los ids propios (`on_user2_own`) y la última rama (`branch_seen_`, que importa para
    las escenas del anfitrión). Ninguno de los dos depende del orden.
  - `shop_probe`: solo cuenta.
  - Aun así, los paquetes que suelta un 2005 se enseñan a esos tres observadores **después** de la ruta, fuera de los
    cerrojos (`AckRouteOut::scene_released`) y en orden.
  - Los demás observadores no miran 2661/2981/2524/2111/2810: `user2_shops` mira 2916/3000/Nak; `user2_stages`,
    2005/2322/2335; `child_hold`, 2005/2516/2174; los de movimiento, 2697/2991.
- **El cliente del invitado** recibe el 2005 y, en el mismo envío, el 2661 detrás: el orden nativo. Las 169 copias de
  `observer_seq_copy` se crearon detrás de su 2005 sin un solo rechazo [V, `tenderos.md` §2.1].
- **El servidor no espera nada del cliente** para esa escena hasta que el cliente la tiene: las Req de la escena (2436,
  2173, 2767…) solo las manda un cliente que la conoce.

**Qué retiene T2** (`hso::SceneHold`, `hold_scene_order_logic.h`):
- el 2661 **propio** del usuario 2 (dueño `@5` = su personaje) de una escena de la lista (hash `@27`) que enlaza (tipos
  4/5) alguna clave cuyo 2005 **aún tiene que pasar** por `take_frame`: esas son las claves pendientes.
  `AckHold::withheld` (revisión M1): la clave existe y su estado es `Holding` o `Spilled` (sus paquetes desbordaron y
  pasan, pero su 2005 aún no ha salido), **no** `Orphan` (su NotifyAppear ya volvió sin que se viera el 2005: puede ir
  dentro de un 2042 y el NPC estar ya en el cliente);
- después, **todo paquete al usuario 2 con ese id de escena**:
  - 2981 `@5`;
  - 2524 `@5`;
  - **2111** `@5`, 22 B (`UpdateStageChartEventAck`, ToNear desde `0x2936BF3`) [V p2b: en las capturas, los ids
    `0x115…0x117` van en `@5`, los mismos que en el 2981 `@5` y el 2661 `@9`];
  - 2810 `@4`.
- Son los S>C con id de secuenciador del gestor: el 2661 y el 2810 van ToTarget al dueño; el 2981, el 2524 y el 2111,
  ToNear (`p8-arquitectura-observador.md:101-107`). El 2322 no lleva id y no se toca.

**Las claves de los 2005 y todas las salidas de `publication_gate`:**

| Salida | Lo que ve T2 |
|---|---|
| Publicar (`Restore1`/`ByControl`, familias, «released anyway», el olvido del motor) | Un 2005 nativo **tal como se encola**: después de la compactación de los retirados (`observer_hold_acks.cpp` vuelve a leer el `payload` del paquete compactado) y **en lote**: todas las claves de sus registros, también varias de un mismo 2005. La escena sale cuando se anuncia la **última** clave pendiente |
| Retener (`Restore0`, `CcUnknown`, `Dependency`…) | La retención de `observer_hold_acks` sigue y la escena espera (máx. 60 s de la puerta, menos que los 100 s de T2) |
| Descartar (`DropExpired`/`DropNoRoom`), despawn, fuera de rango, época, salida del usuario 2, retirada S33, caducidad de `observer_hold_acks` | `observer_hold_acks_drop` / `retire` / `clear` / `expire` → `key_gone`: la escena se **descarta** |
| Ocultar sin entrada (tabla llena con la puerta en 1) | No hay retención de `observer_hold_acks`: T2 no retiene, el 2661 sale como hoy y el cliente lo rechaza (`[no hold]`). Es raro: hace falta la tabla de 1024 llena |

**Los finales:**

| Caso | Qué pasa |
|---|---|
| Llega el 2005 de la última clave pendiente | Se encolan, justo detrás de él y en su orden, el 2661 y los paquetes retenidos: `[RELEASED scene] … N ms …` |
| **Su 2810** | Se descarta junto con la escena: el 2661, lo que retenía y el propio 2810 (el cliente nunca la tuvo): `[DISCARDED scene] … its 2810` |
| **Tope**: 32 escenas, 64 paquetes o 64 KB por escena, 1 MB en total | Se **rechaza y se descarta** junto: el 2661 que no cabe, o la escena entera con el paquete que no cabe |
| La retención de una clave pendiente cae sin 2005 | Descartada |
| Un 2516 de una clave pendiente | Descartada (el 2516 sale) |
| Una frontera de campo del usuario 2 (2174, 2643 sin append, 2756) | Se descartan todas (la frontera sale) |
| 100 s sin el 2005 (= el tope de `observer_hold_acks`) | Descartada |
| El usuario 2 se va, o época nueva | Se vacía |

- Una escena descartada deja una **lápida** con su id: sus paquetes posteriores también se descartan, hasta su 2810
  (que se descarta y cierra la lápida), 120 s o una frontera. Un **2661** con el id de una lápida no se descarta: la
  lápida se levanta y se decide como uno nuevo (revisión B4).
- El 2810 se reconoce por su id `@4` desde 12 B (revisión B2): uno de otra longitud que lleve el id de una escena
  retenida o con lápida también la descarta (se cuenta como `2810 not 17 B`). Hoy todos miden 17 B.
- Descartar equivale a lo que haría el cliente sin T2 (rechazar la escena, `0x3B331F28`), pero sin mandarle paquetes de
  un id que no conoce. En p2b, un 2810 de un id desconocido no le hizo nada al cliente (`guest.log:19241-19242`).

**Probe** (`observer_hold_scene_order=probe`): exactamente la fase 1 (`[would hold scene]`). Con `1`, el seguimiento en
probe se apaga y las líneas son `HoldSceneOrder [HELD|RELEASED|DISCARDED scene]` y, cada 60 s, `HoldSceneOrder summary
(T2 ACTIVE)`.

### 9.4 U2-c y la ruta U2 con T1

- U2-c (`user2_shop_trade_route=1`) ya estaba (§8.4). Lejos y con T1, la única entrada de tienda del NPC es la de
  `mgr2`: se cumplen «viva en `mgr2`» y «ninguna en `mgrH`» (F3). Con `RAX := mgr2`, la compra y la venta se validan
  contra la etapa actual de `mgr2`.
- U2-a, U2-b (`user2_shop_route=1`), la ventana B3, la espera A1 y los abortos B5 **no dependen** de qué clave dejó
  pasar la escena:
  - `user2_shops_may_prepare` no mira `user2_stages_elements`;
  - las rutas deciden por el id en `mgr2`;
  - B5 (`NotFuncnpc`) solo mira si es una `funcnpc`.
- Lo único que cambia es el aborto nuevo `NotTradeList` (§9.1). Revisado en `user2_shops.cpp`; los tests de la ruta
  siguen igual.

### 9.5 Inis

- `docs/research/ini/cdmp.2inst-s46-p3-host.ini` = `cdmp.2inst-s46-p2-host.ini` con `user2_shop_trade_list=1`,
  `observer_hold_scene_order=1`, `user2_shop_trade_route=1` y `user2_shop_route=1`. `user2_stages_elements` no se pone
  (`schedule`).
- `docs/research/ini/cdmp.2inst-s46-p3-probe-host.ini`: lo mismo, con las cuatro claves en `probe`. No prepara ninguna
  escena de tienda del invitado: con T1 en probe sigue valiendo `schedule`.
- Invitado: `cdmp.2inst-s46-p2-guest.ini`, sin cambios.

### 9.6 Criterios de aborto (fase 3)

Cualquiera de estos detiene la prueba. Para apagar por piezas, las cuatro claves a `probe`.
- cualquier `[orphan]` del libro (con la salvedad de §6: buscar antes el id en un tercer gestor);
- un 3000 de tienda **al anfitrión**, o `ShopProbe [server check] … host … NOT found`;
- `[P1] SERVER … for host … FAIL` en una compra del anfitrión con la tienda abierta;
- **dinero, inventario, stock o recompra que cambian en el jugador equivocado.** En cada 2789/2366, la sonda P1 dice
  quién pide (`for host` / `for USER 2`), contra qué gestor y con qué veredicto. El cambio en sí se comprueba **a mano**
  (abajo);
- `User2Shops [ABORT]` (incluido `NotTradeList`) o un `ABORT` de U-1;
- en el invitado, `REJECTED 0x3B331F28` de una escena propia de tienda: T2 no ordenó (apuntar si antes salió
  `[DISCARDED scene]` o `[no hold]`);
- el tendero del anfitrión desaparece;
- los de la fase 2 (§8.5), un bucle de 2661/2810, el atasco S14, un cierre o un cuelgue.

**No son aborto, pero se apuntan:**
- `[DISCARDED scene]`: el invitado se queda sin tienda hasta el siguiente armado;
- `[no hold]` con un 2005 retenido (la tabla llena).

**Dinero e inventario: comprobación a mano.** No se añade una sonda de dinero. Leerlo exige seguir `0x25CA080` y el
inventario del dueño del `storeComp` (`nativo.md` §1.3), que no están estudiados, y cada lectura nueva de memoria del
juego necesita su propia verificación. El método:
1. Antes de cada compra o venta, captura (Ctrl+F12) en **las dos** ventanas: el dinero (plata) y el objeto en el
   inventario de cada jugador.
2. Después, otra captura de las dos ventanas.
3. Pasa si solo cambian el dinero y el inventario de **quien comerció**, y en el otro no cambia nada.
4. Con la hora de cada captura, se cruza con la línea `[P1] SERVER … for USER 2 … 2789|2366 … -> PASS` y la ausencia
   de `Nak` y de 3000.
5. El stock: después, el anfitrión abre su propia tienda y mira que el objeto que compró el invitado conserva su
   cantidad (el stock es por usuario, `nativo.md` §6 H5 [V/E]).

### 9.7 Prueba en vivo (dos copias en un PC; después, Shadow)

**Preparación (§6):**
- copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups\`, con la partida del invitado separada;
- la segunda copia, con `SteamAppId`/`SteamGameId=3321460` y `PlatformServiceType=Steam`; papel, ini y log propios;
- **el anfitrión necesita una partida con las tiendas de Hernand desbloqueadas** (`tiendas-u2.md` §11.4, paso A: la
  evaluación solo acepta lo que permite el progreso del anfitrión);
- el anfitrión, primero con `cdmp.2inst-s46-p3-probe-host.ini` (paso previo) y luego con `cdmp.2inst-s46-p3-host.ini`;
  el invitado, con `cdmp.2inst-s46-p2-guest.ini`.

**Al arrancar (anfitrión), mirar:**
- `Config: S46 shopkeepers (phase 3): … observer_hold_scene_order '1' (host, T2) user2_shop_trade_list '1' (host,
  T1) …`;
- `User2Stages: user2_shop_trade_list=1 (S46 phase 3, T1 ACTIVE) …`;
- `HoldSceneOrder: observer_hold_scene_order=1 (S46 phase 3, T2 ACTIVE) …`;
- `User2Shops (user2_shop_route=1): U2-a at the vf48 call of 15 of 15 scene handlers`, sin `left out` ni `CRITICAL`;
- `ShopTradeProbe … U2-c …` sin `NOT installed`.

**Paso previo (probe).** Con el invitado lejos, en Hernand: `User2Stages [element] … 'cd_seq_funcnpc_smithy' ->
SKIPPED … | S46 T1 (verdict only…): [T1 would pass] … (hash 0xA7132561)`, y lo mismo para butcher. Las demás `funcnpc`
dicen `[T1 would skip] not on the trade list`. Nada más cambia.

**Pasos (fase 3):**
0. **Control:** el anfitrión, solo, compra en el vendedor de armas de Hernand. Mirar: P1 `PASS` y ningún 3000.
1. **El invitado, a ≥ 10 celdas del anfitrión**, en Hernand, junto al vendedor de armas (smithy) o el carnicero
   (butcher), con `[ARMED]`. Mirar, en orden:
   - `User2Stages [element] … 'cd_seq_funcnpc_smithy' -> LET THROUGH … | S46 T1 (ACTIVE, by the exact hash 0xA7132561
     …): on the trade list, >= 10 cells`;
   - `User2Shops [prepared] … (resource hash 0xA7132561): … booked`;
   - `HoldSceneOrder [HELD scene] … scene 0x… (cd_seq_funcnpc_smithy …) … B01…` y, unos 150–400 ms después,
     `[RELEASED scene] … 2661 2981 …`;
   - en el invitado, el 2661 de esa escena **aceptado** (sin `0x3B331F28`) y el aviso de interacción del tendero
     (`GuestXInter … in M (native)`).
2. **Hablar, comprar y vender**, con capturas antes y después (§9.6). Mirar:
   - `ShopProbe [server check] … for USER 2 … ROUTED by U2-b: FOUND`;
   - el 2663/2916 al invitado y `User2Shops [shop followed]`;
   - `User2Shops [route] 2436 … ROUTED`;
   - `ShopTradeProbe [ROUTED to mgr2]` y después `[P1] SERVER … for USER 2 … 2789 … -> PASS`; lo mismo con 2366;
   - cambian el dinero y el inventario del **invitado**, y los del anfitrión no.
3. **Después, el anfitrión compra en su partida**, en su propio vendedor o viajando a Hernand cuando el invitado ya
   salió de la tienda. Mirar:
   - P1 `for host … PASS` y ningún 3000;
   - su tienda intacta: el stock de lo que compró el invitado no ha cambiado.
4. **Relevo con la tienda abierta:** con el invitado dentro de su tienda, el anfitrión viaja hacia Hernand. Mirar:
   - `[drain held]` (> 5 celdas) o `[drain NOT held]` (≤ 5) y el vaciado;
   - las `[P2 erase]` entre gestores, con `APPLIED swap … [orphan avoided]` o `native (no own entry)`;
   - ningún `[orphan]` en los 2 s siguientes;
   - qué ve el invitado cuando le llega el 2810 de su tienda abierta (apuntarlo: H11);
   - el anfitrión compra después: P1 `PASS` y ningún 3000.
5. **Final:** sale el invitado y el anfitrión compra otra vez.

**Qué entregar:**
- `host.log` y `guest.log`;
- las capturas de dinero e inventario;
- los resúmenes `HoldSceneOrder summary (T2 ACTIVE)`, `User2Shops summary`, `ShopTradeProbe R0 / U2-c summary` y el
  de U-1 (`S46 T1 (ACTIVE …)`).

### 9.8 Resultado fuera del juego (16-10)

- **Compilación limpia** (con `build\` borrado) con `.\scripts\build.ps1`: verde, **84/84 tests** de ctest. Los nuevos
  van en `optd_hold_scene_order` (T2 activo: retener, soltar en orden mezclado con `observer_hold_acks`, dos claves, un
  2005 compactado, el tope de escenas, de paquetes y de bytes, el 2810, la lápida y los demás finales) y en
  `optd_user2_shops` (`test_t1_active`: el hash de cuatro nombres del diccionario, las 11 tiendas de Hernand fuera de
  la lista, los veredictos con las dos claves de elementos, el bloqueo y el aborto).
- Avisos: el código nuevo no da ninguno. Los que salen (C4127 en `optd_user2_shops_tests.cpp:145` y
  `optd_fast_travel_tests.cpp:205`, C4996, C4244 de la STL y C4005 de safetyhook) son los mismos que en la fase 2.
- `cdmp.asi` tras la revisión (§9.11), compilación limpia, 84/84: SHA-256
  `9f001312312f8c9b3a5f4f5612a39fabb738a895d6519d2efb87617fc81c88f2` (12 387 840 B). `optd_hold_scene_order` añade
  `test_t2_review` (los casos 1 a 4 de la simulación de la revisión: `Orphan`, `Spilled`, el 2810 de 18 B y la lápida
  con un 2661 reenviado, y el motivo de `KeyGone`).
- `cdmp.asi` antes de la revisión (commit `7c3b489`): SHA-256
  `f3a7b44ca4b02c5543ac3ef6e4919e210121c1d10eb4e7807541b28df2791120` (12 384 256 B).
- `cdmp_host` con ese asi, sin `CDMP_ROLE`, en `build\e2e\p3h` (`cdmp.2inst-s46-p3-host.ini` como `cdmp.ini`) y en
  `build\e2e\p3p` (`cdmp.2inst-s46-p3-probe-host.ini`), 4 s cada uno: `Loaded mod … Mod stopped and unloaded cleanly`,
  código 0. Líneas:
  - `Config: S46 shopkeepers (phase 3): shop_trade_probe 1 (host, read only) user2_shop_trade_route '1' (host, U2-c)
    observer_hold_scene_order '1' (host, T2) user2_shop_trade_list '1' (host, T1) shop_interaction_own_delete '1' (R0,
    host and guest) | related: user2_shop_route '1' user2_stages_elements 'schedule' observer_hold_acks 1`;
  - en el probe, lo mismo con `'probe'` en las cuatro claves.

  Fuera del juego no se instala nada de la sesión 2 (ni ganchos ni las líneas `User2Stages:` / `HoldSceneOrder:` de
  arranque): eso ocurre al arrancar la sesión 2 en el juego.

### 9.9 Archivos

- `funcnpc_trade_policy.h`: `dict_hash`, `is_trade_name_by_hash`;
- `user2_shops_logic.h`: `element_verdict` con la lista, `SkipNotTrade`, `t1_list_on`, `t1_breach`, `Abort::NotTradeList`;
- `user2_stages.{h,cpp}`: T1 en U-f, la exclusión por escena de origen, `user2_stages_set_trade_list` y el resumen;
- `user2_shops.{h,cpp}`: el aborto de T1;
- `hold_scene_order_logic.h`: `Mode::On`, `scene_id_t2` (2111), `SceneHold`, `merge_release`, `Kind::KeyGone`;
- `observer_hold_acks.{h,cpp}` (T2 en la ruta, bajo `g_mu`) y `observer_hold_acks_logic.h` (`AckHold::next_seq`);
- `observer_hold_scene_order.{h,cpp}`: el modo 1, sus líneas y el resumen;
- `server.cpp`: los paquetes soltados se enseñan a los observadores;
- `config.{h,cpp}`, `mod.cpp` y `session2.cpp`: la clave nueva y la línea `Config: S46 … (phase 3)`;
- tests `optd_hold_scene_order` (T2) y `optd_user2_shops` (T1);
- los inis `cdmp.2inst-s46-p3-host.ini` y `cdmp.2inst-s46-p3-probe-host.ini`.

### 9.10 Lo que no está demostrado (fase 3)

- **Nada de la fase 3 ha corrido en el juego.**
- [E] Que el tendero nazca en el mismo hilo, dentro de `0x288A970` (la exclusión por escena de origen). Si no, la
  exclusión no actúa, y U-e y B tampoco lo adoptan (§9.2).
- [H] Que, con el orden nativo restaurado, el cliente del invitado acepte el 2661. Las 169 copias de
  `observer_seq_copy` sí se aceptaron, pero no se ha medido ninguna escena **propia** de tienda en esas condiciones.
- [H] La compra del invitado lejos con U2-c: no hay ninguna medida (P1 la mide).
- [H] Qué hace el cliente con el 2810 de su escena cuando tiene la tienda abierta (H11; paso 4).
- [H] Que ningún otro S>C, aparte de los de §9.3, lleve el id de la escena (por ejemplo el 2196 o el 2561, de formato
  desconocido). Si alguno existe y llega antes que el 2661, el cliente lo ignora, como hoy.
- [E] Que el stock y la recompra sean por usuario (`nativo.md` H5): el paso 3 lo comprueba.
- El encargo pedía «§2 T1/T2 y sus modos de fallo, y §6.4» de `s46-datos/revision-diseno.md`, pero ese archivo es un
  resumen y no tiene esas secciones. Se usaron su veredicto, sus condiciones previas («T2 debe retener todo lo que lleve
  el id… leer los 2005 compactados o en lote») y sus abortos.
- [H] **Ids que coincidan entre `mgrH` y `mgr2`** (ya [H] en la fase 2, §8.9). T2 solo comprueba el dueño en el 2661
  (`@5` = el personaje del usuario 2). En el 2981, el 2524, el 2111 y el 2810 decide **solo por el id**: si una escena del
  anfitrión tuviera el mismo id que una escena retenida del usuario 2, T2 retendría (o descartaría con ella) los paquetes
  de la escena del anfitrión que van al usuario 2. Los ids parecen salir de un contador global y creciente [E]
  (`InBoth` es un aborto de la ruta U2). En vivo: ningún id de `[HELD scene]` puede aparecer como escena del anfitrión
  (las líneas `ShopProbe [funcnpc]` / P del anfitrión).
- [H] Un 2661 que se **reenvíe** con el mismo id: es raro (se envía una vez, al crear la escena). Con la revisión B4 ya no
  se descarta por la lápida; si en vivo sale `a 2661 of a discarded id decided again` > 0, se apunta.
- Los observadores ven los paquetes soltados fuera de los cerrojos (revisión B6): un 2810 o 2981 de la misma escena en
  otro hilo podría llegar al libro de `observer_scene_copy` antes que su 2661. Es la misma carrera que ya existe entre
  cualquier `push` y su `on_out`; para una escena de `mgr2` no cambia ninguna copia (`AlreadyToUser2`).

### 9.11 Revisión de la fase 3 aplicada

Informe: `scratchpad\s46\revision-f3.md` (y su `sim.cpp`, cuyos casos 1 a 4 son ahora `test_t2_review`).

| # | Hallazgo | Cambio |
|---|---|---|
| M1 | T2 decidía las claves pendientes con `holding()`: incluía `Orphan` (escena descartada 2 s después aunque el NPC estuviera en el cliente) y excluía `Spilled` (el 2661 salía antes del 2005) | `AckHold::withheld` (existe y no es `Orphan`, `Spilled` incluido) en `offer`. `KeyGone` lleva el motivo de `observer_hold_acks` (`the hold was dropped`, `held too long`, `released, but no 2005 …`) |
| M2 | `NotTradeList` se disparaba con cualquier `funcnpc` de `mgr2` fuera de la lista, también preparada fuera de U-f | Solo cuenta si el punto P ocurre con la marca de T1 puesta en el hilo (`user2_stages_in_trade_prepare`); si no, solo se cuenta (`outside a T1 pass`) |
| B1 | Una excepción tras mover el paquete devolvía `Pass` (paquete vacío encolado) | `catch (...) { return packet.empty() ? Held : Pass; }` |
| B2 | Un 2810 de otra longitud no descartaba la escena | Id `@4` desde 12 B en `scene_id_t2`; contador `2810 not 17 B` |
| B3 | La frontera de campo salía rotulada como 2516 | `keys` vacío en ese descarte (la línea dice «user 2's field changed») |
| B4 | La lápida descartaba un 2661 reenviado aunque su NPC ya estuviera en el cliente | Un 2661 con el id de una lápida la levanta y se decide como nuevo (contador `decided again`) |
| B5 | `~Mark` ponía 0 en vez del valor anterior | Restaura el anterior; además, el final del Troc más externo del hilo borra la marca (`user2_funnels` `~Restore` → `user2_stages_clear_trade_mark`). Con `/EHsc`, una excepción SEH que cruce el marco se salta `~Mark`, pero los manejadores del mod siguen la búsqueda (el cierre es del juego) |
| B6 | Los observadores ven los paquetes soltados fuera de los cerrojos | Sin cambio: documentado (§9.10) |
| B7 | `own 2661` y `[no hold]` solo se contaban con retenciones vivas | Con T2 en 1, todo 2661 al usuario 2 pasa por T2 (se cuenta siempre) |
| [H] | T2 no comprueba el dueño en 2981/2524/2111/2810 | Documentado (§9.10) |

### 9.12 Datos en vivo de las fases 1 y 2: arreglos antes de la fase 3

Datos: `C:\Emulador CD\backups\p8-2inst-s46-f1\` (análisis `scratchpad\s46\vivo1.md`) y `p8-2inst-s46-f2\` (R0 = 1:
`noop` 13, `swap` 0, 0 fallos; la tienda del anfitrión funcionó).

**A. P1 clasificaba mal la compra del servidor [V].**
- `[rsp+0x38]` (el gestor, escrito en `0x21F28BD`) lo pisa `lea rdx,[rsp+0x30] ; call 0x4F8220` (`0x21F2A56` /
  `0x21F2AC5`) antes de `0x21F2C45`. En vivo P1 leía `0xFFFDFFF6C4541564` (una posición), así que `server` salía siempre
  falso.
- **Arreglo:** P1b (`0x21F2917`, la misma llamada a `0x21F2780`) sella por hilo `{rsp, rbx}`. En ese punto `rbx` es el
  gestor: lo escribe `0x21F28A1 mov rbx,rax` (después de U2-c, así que es el gestor **efectivo**) y nada lo reescribe
  hasta `0x21F29AA` [V `f21f2780.asm`]. P1 lo toma solo si su `rsp` es el mismo (entre los dos puntos `rsp` solo se mueve
  en llamadas equilibradas: revisión F1). Si no está sellado, la línea dice «a manager P1b did not seal» y el resumen lo
  cuenta (`P1 without P1b's manager`).

**B. Durante el diálogo de tienda el NPC no tiene entradas [V]: U2-c decide ahora por la etapa del 2671 aceptado.**
- Cronología del tendero `0xB010053D` (smithy, escena `0x298` de `mgrH`), en `p8-2inst-s46-f1\host.log`:

  | qpc (s) | Qué | Línea |
  |---|---|---|
  | 235281.961 | altas de `0x000F4321` y `0x000F42F6` de `0x298` | `:15463-15464` |
  | 235343.414952 | 2671 del anfitrión: P6 `result 2` (y, en la misma llamada, ShopProbe `[server check]`: la entrada es de `0x298`, FOUND en `mgrH`) | `:30029`, `:29917` |
  | 235343.468 | la 2720 da de baja `0x000F42CF` y `0x000F4321` (y `0x000F42F6`, que no se registraba) | `:30030-30031` |
  | 235364.644779 | **2789** con el vector **vacío**: P1 `linked to stage 0x298` contra la actual `0x298` → PASS | `:31262` |
  | 235371.761 | al cerrar la tienda, las altas otra vez | `:31712-31713` |

- La etapa de la entrada que usó el 2671 que pasó `vf170` (`0x298`) es la etapa enlazada que compara la compra 21 s después,
  con el vector vacío. Lo mismo en la compra 2 (2671 a las 17:18:12.469, 2789 a las 17:18:25).
- **Criterio nuevo (`stp::AcceptedBook`, P6):**
  - en P6 (`0x2B3708C`, después de `vf170`), con resultado 2 y la clave de tienda, se apunta por NPC la etapa de la
    copia de la entrada que eligió `0x2124060`. La copia está en `[rbp-0x61]` y su `+0x20` en `[rbp-0x41]`. Se aclara en
    `0x2B36F90`, la llena `0x2124060` (`r9` en `0x2B36FCD`) y se le pasa a `vf170` (`r8` en `0x2B3707C`). Entre
    `0x2B36FE3` y P6 solo la tocan las salidas de error, que abandonan la función [V capstone]. Las tres instrucciones
    entran en la verificación de contexto de P5/P6;
  - U2-c, para la compra del usuario 2, toma ese registro si es **del usuario 2** y tiene ≤ 120 s (la ventana de
    tienda), y busca la etapa en `mgrH` y en `mgr2`: enruta solo si está viva en `mgr2` y **no** en `mgrH` (F3). Si está
    en `mgrH` → `StageInMgrH`; si no está en ninguno → `StageNotLive`;
  - F3 se mantiene **antes**: una entrada de tienda viva de `mgrH` sobre el NPC no enruta nunca (`NpcAlsoInMgrH`);
  - sin registro decide, como en la fase 2, por las entradas del NPC;
  - un 2671 posterior del anfitrión sobre el mismo NPC reemplaza el registro, así que U2-c ya no enruta.
- P6 se instala también solo con `user2_shop_trade_route` (sin `shop_trade_probe`; entonces no escribe líneas).
- **Por qué no las otras dos opciones:**
  - `0x1F5C940` → `[[r+0x10]+0x30]` llama a métodos virtuales (`[rax+8]`, `0x5D8880`): replicarlo es llamar al juego;
  - `comp+0x2E0` / `+0x2D8` son del componente del **cliente** (`scene_points.h`), no del servidor.
- **Lo que no está demostrado:** [E] que `vf170` no cambie la copia (solo la lee: es la validación común) y que la
  etapa enlazada siga siendo la del 2671 hasta la compra. En vivo, la línea U2-c dice `user 2's accepted 2671 used stage
  0x… (in mgr2)` y P1 `for USER 2 … linked to stage 0x…`: los dos ids tienen que coincidir.

**Menores:**
- el libro recibe también las operaciones **sin** la clave de tienda de los NPC que ya sigue (`other keys of the ledger's
  NPCs`): `live!=expected` pasa a significar un cambio que nadie vio;
- los resúmenes `ShopTradeProbe` y `HoldSceneOrder` se escriben también en cuanto el usuario 2 se va (el log del
  anfitrión puede acabar con el juego, sin `Mod stopped`);
- las líneas `[element]` de los elementos de la lista de comercio tienen limitador propio (16 por 5 s) y ya no se las
  come el presupuesto de su clase (`trade-list lines over their limiter`).

Fuera del juego: compilación limpia, **84/84**; `optd_shop_trade` añade `test_live_f1` (el diálogo con el vector
vacío, los tres veredictos nuevos, F3 antes del registro, el libro de 2671 aceptados y el sello de P1). `cdmp.asi`
SHA-256 `36150bde9f139f6678edc241eda215b048cf5a7a0bfdb7a68c468ff9864afa99` (12 393 984 B). `cdmp_host` con los inis
p3 (activo y probe): carga y descarga limpias, código 0.

**Revisión de A y B aplicada** (`scratchpad\s46\revision-ab.md`; veredicto: el p3 activo es seguro):

| # | Hallazgo | Cambio |
|---|---|---|
| M1 | `stp::where` daba `User2` con la lectura de `mgrH` fallida: «viva en `mgr2` y no en `mgrH`» se cumplía por omisión | `stp::where_proven`: `User2` solo si `mgrH` se leyó y no la tiene y `mgr2` se leyó y la tiene; si una lectura falla, `Unknown`. En el camino del registro, `Unknown` da `StageNotLive`. En `u2c_npc`, una entrada de tienda `Unknown` deja la lectura sin decidir (`NpcUnreadable`). Test con la lectura de `mgrH` fallida. `stp::where` (R0, las líneas) no cambia |
| B1 | P6 no es «aceptado»: después de `vf170` aún pueden rechazarlo `0x211E2A0` (`0x2B370D9`) y `0x2125660` (`0x2B371CD`) | Texto corregido: «el 2671 que pasó `vf170`». Un 2671 rechazado después no abre diálogo ni trae 2789. La confirmación en la cola de éxito (`0x2B37313`) exigiría un punto de enganche nuevo, con su firma única y la verificación del marco; no se añade con el usuario probando en vivo |
| B2 | Ventana fija de 120 s: un diálogo más largo perdía el registro | El registro se cierra con el diálogo: P2a vuelve a dar de alta en ese NPC la entrada `0x000F4321` de **esa** etapa, o llega al usuario 2 un 2743 (`InteractionResetAck`) de ese NPC. Tope: 10 minutos. Las dos vías necesitan `shop_trade_probe=1` (P2a y el grifo de `take_frame`); solo con `user2_shop_trade_route` queda el tope. El resumen cuenta `closed with their dialogue` |
| B3 | Con P6 rechazado y U2-c instalado, B se degradaba en silencio | WRN en `install()`: «U2-c without P6: no route during the shop dialogue» |

Fuera del juego, tras esta revisión: compilación limpia, **84/84**. `cdmp.asi` SHA-256
`f6908918f798ab997236d189704b46442bf21597f14ef52bd87f9572b4400eb3` (la compilación no es reproducible byte a byte). La
prueba en vivo en curso usa el binario anterior, `36150bde…` (`a3c8aa4`). `cdmp_host` con el ini p3 activo: carga y
descarga limpias, código 0. No desplegado.

## 10. Lo que no está demostrado (fase 1)

- **Nada de la fase 1 ha corrido en el juego.** Los puntos están verificados en el exe en disco y se vuelven a
  verificar al instalar; que los registros lleven lo que se lee es [V] por desensamblado y [E] en ejecución.
- [E] `[storeComp+8]` es el actor del jugador que pide (`+0xA0` su usuario): lo usa la propia `0x21F2780`; el personaje
  se lee como `[actor+0x60]`. Si no lo fuera, P1/U2-c dirían «unknown» y no «USER 2».
- [E] El dueño de la etapa `[[stage+0x48]+0x60]` en P2/P2a (lo usa `0x288A970`); solo se registra.
- [E] La clave de cada entrada se resuelve con la tabla de `InteractionInfo` (`[exe+0x6D69AB8]`); si una fila no está
  cargada se compara el índice (la misma fila de la misma tabla). El borrado nativo compara claves.
- [H] Que la descarga 0xE ejecute la baja de `SetInteraction` (P2 lo dice).
- [H] Que P1 en el cliente del anfitrión (las comprobaciones previas de la interfaz) use el gestor del cliente: se
  marca «client» y no se cuenta como servidor.
- [H] T2 en vivo: hoy no hay escenas propias del invitado; solo hay tests.
- [H] U2-c en vivo: hace falta una compra del invitado (fase 3).
- Las preguntas abiertas de `nativo.md` §9 y `tenderos.md` §3.5 siguen abiertas.
