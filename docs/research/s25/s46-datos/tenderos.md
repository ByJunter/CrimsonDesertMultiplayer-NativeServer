# S46 — Tenderos del invitado sobre la base S44/S45: estado, causas y diseño de raíz

Investigación de solo lectura (16-10). Build del juego 1.0.0.2976 (PE `0x6AB28F00`, base `0x140000000`); todas las
direcciones son RVA de esa build. Base: rama `opcion-d-2inst` `c84b2dd` (S44 + S45 unidas, kit 39). Trabajo de la otra IA:
rama `opcion-d-tenderos` `a294fac` (S41, S41R, S41 P2). No se ha compilado, desplegado ni abierto el juego.

Marcas: **[V]** leído en log, captura, bytes o decompilado legible; **[E]** inferencia fuerte; **[E-x]** tramo legible de
`.xtls` (sin ganchos ahí); **[H]** hipótesis por medir.

Abreviaturas: `mgrH` / `mgr2` = `ServerSequencerStageManager` del anfitrión / del usuario 2; M / X = gestor propio del
cliente del invitado / copia del gestor del anfitrión en ese cliente; «entrada» = entrada dinámica de interacción del NPC
en el servidor (`[[NPC+0x68]+0x98]`, vector `+0x88`, cuenta `+0x90`, 0x30 B por entrada, `+0x20` = id de escena).

Scripts y salidas de esta investigación (todo en `scratchpad\s46\`): `funcnames.py` → `funcnpc.json` (156 recursos
`cd_seq_funcnpc_*` del diccionario), `scan.py` → `scan_s45.json` (escenas de tienda y tráfico de interacción por captura),
`order.py` (orden 2005 ↔ 2661 de cada escena de tienda), `seqtl.py` (cronología de una escena), `d2369360.txt` y
`de404a10.txt` (decompilados de SetInteraction en el servidor y de su borrado).

---

## 0. Resumen

1. **Hoy, con S44/S45, el invitado no tiene ninguna escena de tienda propia, ni lejos ni cerca [V].** En las sesiones S45
   (pasos 2 y 3) recibió 22 + 22 escenas de tienda, **todas del anfitrión** (dueño `0xA0100001`), copiadas a X; 0 con dueño
   `0xA0100066/67`. No envió ningún 2671/2720 ni ninguna Req de escena de tienda. Su búsqueda de interacción rechazó las
   52 entradas que vio (`GuestXInter`: «in X, not in M → refused: no prompt»). Lejos, el kit 39 no deja preparar tiendas
   (`user2_stages_elements=schedule`, `user2_shop_route=0`).
2. **`+0x318` / `local_res8` no es lo que falta [E].** `comp+0x318` es el enlace del NPC a su stage de **horario**
   (`vf13 0x9ED290`). Los tenderos los genera la propia escena `funcnpc` (`comp+0x2E0` = id de secuenciador), así que `vf13`
   sale siempre en `local_res8 = 0xFFFFFFFF00000000`, también con escenas creadas bien (p1b `0x293`, resultado 0). Lo que
   falta es **una escena propia en M** (su `SetInteraction` de cliente da el aviso) y **una escena del invitado en el
   servidor** que conteste sus Req.
3. **Lejos, la causa del 2661 antes que el 2005 es del mod, no del juego [V].** `observer_hold` retuvo los 2005 de Renee y
   Rhett (tipo 4, controlador el anfitrión) 344 y 375 ms; el 2661 propio va ToTarget sin esperar. En nativo el 2005 llega
   0,1–0,9 ms **antes** del 2661 (34 de 34 escenas de tienda medidas), y las copias de `observer_seq_copy`, que ya esperan
   al 2005, se crean todas. **Arreglo de raíz: ordenar en el anfitrión** (la misma retención de `observer_hold_acks`),
   no en el cliente.
4. **Cerca, el servidor no crea la tienda del invitado [E fuerte, V en datos].** La evaluación de celdas `0x288B7A0` solo
   pasa para el gestor del **dueño del campo** (puerta `0x288B87A cmp [r15+0x180],eax`); `mgr2` solo evalúa porque U-1
   fuerza esa puerta (U-a), y solo armado a ≥ 10 celdas. Lo de «una tienda por jugador» es la *capacidad* (dueño = el
   personaje de la stage, 2661 ToTarget), no algo nativo.
5. **La gemela murió y rompió al vendedor por dos mecanismos separados [V + E-x]:**
   - `mgr2` en estado 1 **sigue reprogramando su 2043** cada ~100 ms y cada tick descarga todo (`reschedule 1`, p2b);
   - el `SetInteraction` del servidor (`0x2369360`, código común) **añade al final** (`.xtls 0xE402CC0`) y **borra por clave
     la primera coincidencia** (`.xtls 0xE404A10`, intercambio con la última), no por escena. Al borrarse la gemela, más
     nueva, se llevó la entrada del anfitrión y dejó la suya (`0x2DE`) huérfana. Eso explica los cinco 3000 del anfitrión.
6. **Diseño propuesto:** una sola entrada por (NPC, clave), siempre la del anfitrión si hay escena suya. Lejos: escena propia
   de `mgr2`, orden en el anfitrión, ruta U2. Cerca: `mgr2` armado también cerca, pero solo para tiendas (U-1 «modo
   tienda»); su escena no publica entrada en el servidor si ya hay una, y el invitado interactúa sobre la del anfitrión. En el
   cliente, la escena X del anfitrión no registra entradas de tienda. La gemela de la otra IA se descarta.

---

## 1. Estado real hoy (base `c84b2dd`, kit 39, S44 + S45)

### 1.1 Qué escenas de tienda llegan al invitado

| Caso | Escenas que llegan | Dónde se crean en el invitado | Aviso / interacción | Fuente |
|---|---|---|---|---|
| **Lejos** (U-1 armado, `mgr2`) | **Ninguna.** U-f salta todo `cd_seq_funcnpc_*` (`user2_stages_elements=schedule` por defecto; `user2_shop_route=0`) | — | No hay tendero: lo genera la propia escena de tienda, y no existe | [V] p1b: 11 `SKIPPED` (`host.log:24749`); ini S45/kit 39 sin las claves |
| **Cerca** (`mgrH`) | **Las del anfitrión**, copiadas por `observer_seq_copy`: S45 paso 2: 22 (13 recursos), paso 3: 22. **0 propias** | X (`guest_seq_route`), dueño neutralizado (S44), presentación cortada (R-A: `UIControl`, `InputBlock`…) y cámara de pista cortada (R-B) | `SetInteraction` de X **permitido** (`guest_x_ui_veto_allow=SetInteraction`) → entradas de cliente con id de X → la búsqueda en M las rechaza: 22/22 y 30/30 «refused: no prompt». Invitado: 0 × 2671/2720 | [V] `scan.py` sobre `cdmp_packets.guest_*` de `s45-paso2/3`; `guest.log` `GuestXInter summary` |

Detalle S45 [V, `scan_s45.json`]:
- Captura del invitado, paso 2: 52 × 2661 (22 de tienda, todas `owner A0100001`), 36 × 2810, 10 × 2173 C>S (ninguno de
  tienda), **0** × 2671/2663/2720/2916/3000. Paso 3: 60 × 2661 (22 de tienda, todas del anfitrión), igual de vacía.
- Anfitrión: 2 interacciones completas por paso (2671 → 2663 → 2720 → 2916) y, en el paso 3, una compra
  (`BuyStockFromStoreReq 2789` a 203396,745 s).
- `FuncnpcTwin [would create]` para 22 + 22 escenas del anfitrión (`funcnpc_twin=probe`): solo registro.

**Respuesta a «¿hubo escenas de tienda propias del invitado en S45?»: no [V].** Las únicas escenas de tienda con dueño
del invitado de todo el corpus son las de `tenderos-p2b`: `0x142`/`0x143` (las preparó `mgr2` armado lejos, con U-f
abierto por los inis de la otra IA) y la gemela `0x2DE`. Las tres son producto del mod, no del juego nativo.

### 1.2 Lo que hace falta en el cliente del invitado

**`+0x318` y `local_res8` (corrige la lectura de P1 de la otra IA) [E].**
- `comp+0x318` lo escribe `vf13 0x9ED290` (y `0x9F2FA0`): es el enlace del NPC a la **ClientNPCScheduleStage** que busca en
  `[X+0x148]` con la clave de `comp+0x2E0` (`guest_seq_route_logic.h` §horarios).
- En los tenderos, `comp+0x2E0` es el **id de la escena de tienda** (`0x293`, `0x143`, `0x142`): `GuestStage [NPC vf13]`
  en p1b `guest.log:8291` y p2b `:9570-9573`. Los genera el nodo `SpawnCharacter` de la escena `funcnpc`, no un horario.
- `local_res8` sale del nodo de IA del actor (`[[[[a+0x68]+0x58]+0x90]+8]` → `0x15EE200`), no del papel del cliente. Por
  eso sale `0xFFFFFFFF00000000` («no stage node of its own») también cuando la escena se creó bien: p1b `0x293`, resultado 0,
  `+0x3D8` 1 → 0 y `+0xCF` 1 a los 5 s (`guest.log:8552`, `:10471`). Es lo esperado en el anfitrión también [E: mismo
  código, mismo actor].
- **No hay que «arreglar» `+0x318`.** El enlace que importa para una escena de tienda es el de su 2661 (tipo 4/5 en
  `stage+0x120`, `0xA9CA40`).

**Lo que de verdad falta, en orden de la cadena 2671 → 2663 → 2720 → 2916:**

| # | Pieza | Lejos | Cerca | Evidencia |
|---|---|---|---|---|
| 1 | Una escena de tienda **propia** (dueño = invitado) creada en M | Falta: U-f la salta; con U-f abierto (p2b) el cliente la rechaza porque su NPC aún no está (`0x3B331F28`) | Falta: nadie la prepara | [V] §1.1, p2b `guest.log:9562-9569` |
| 2 | Su `SetInteraction` de **cliente** en M → entrada con id de M → aviso | Sale sola si existe la escena | Además, las entradas de X no deben tapar ni borrar la de M (§2.3) | [V] `GuestXInter`; [E-x] borrado por clave |
| 3 | En el **servidor**, la entrada que elige `0x2124060` (la primera con clave `0x000F4321`) tiene que validarse en `0x2126920` | Entrada de `mgr2` → hace falta U2-b (`RAX := mgr2`) | Entrada del anfitrión → se valida en `mgrH` sin ruta | [V] tabla §2 de `tiendas-u2.md`; S45 `ShopProbe [server check]` |
| 4 | Las Req de la escena propia (2436, 2173, 2767, 2591…) al gestor que la tiene (`mgr2`) | U2-a (ya hecho; necesita `user2_shop_route=1` y U-1 armado) | U2-a con `mgr2` armado también cerca | [V] tiendas-u2 §11 |
| 5 | El servidor contesta 2524 + 2981 a cada 2436: el cliente aplica la rama al recibirlos y luego manda sus 2173 | Necesita la escena viva en `mgr2` | Igual | [V] cronología del anfitrión, `seqtl.py`: 2436 → 2524/2981 en 4–5 ms → 2173 a los 15 ms |
| 6 | Compra/venta (2789 / 2366) con el actor que manda: dinero e inventario propios | Nativo | Nativo | [E] otra IA §6.3; [V] el 2789 del anfitrión lleva su clave y la del NPC |

Dato útil del paso 3 [V]: la compra del anfitrión (2789) llegó **8 s después** de que la escena pasara de la rama 6 a la 0,
así que la interfaz de compra no depende de la rama en curso [E].

---

## 2. Causas raíz de los fallos conocidos

### 2.1 Lejos: 2661 antes que 2005 → `0x3B331F28`

| Hecho | Marca | Fuente |
|---|---|---|
| `mgr2` prepara `0x142` (carnicería, Renee `B01002EB`) y `0x143` (vendedor de armas, Rhett `B01002EC`), resultado 0 | [V] | p2b `host.log:12537-12538` |
| `observer_hold` retiene los 2005 de los dos NPC (tipo 4, controlador `0x90100000`) para la sesión del usuario 2 y los suelta tras la primera acción del controlador: **344 ms** y **375 ms** | [V] | `host.log:12343-12344`, `:12513`, `:12517` |
| En el invitado, los 2005 llegan 339,6 y 387,6 ms **después** de los 2661 | [V] | `order.py` sobre `cdmp_packets.guest_20261015-144332.bin` |
| `0xA9CA40` no encuentra el actor enlazado (tipo 5) y descarta la escena: `0x3B331F28` | [V] | `guest.log:9562-9569` |
| **Nativo:** en el cliente del anfitrión, el 2005 del tendero llega 0,1–0,9 ms **antes** que su 2661, en las 12 escenas de p1b y las 22 de S45 paso 3 | [V] | `order.py` sobre las capturas del anfitrión |
| Las copias de `observer_seq_copy` ya esperan «al 2005 de sus actores enlazados»: en el invitado, el 2005 va siempre delante (0,0 ms). Se crearon todas las copias (52, 60, 28 y 29), y los **únicos 2 rechazos** de las cuatro sesiones son `0x142`/`0x143` | [V] | `order.py`; `grep "2661 result"` en los cuatro `guest.log` |

**Causa raíz [V]:** la retención de `observer_hold` (diseño del mod) se aplica a los 2005, pero no a los 2661 propios del
usuario 2 que enlazan esos NPC. El orden nativo (2005 → 2661 en el mismo envío) se rompe solo para las escenas propias.

**Sobre `guest_shop_order` (otra IA):** corrige el síntoma en el cliente con una cola delante de `OnRecv`, que espera a que
el actor exista y tenga las marcas de `0x8AF170`. Funciona, pero la raíz está en el anfitrión. Con el orden nativo
restaurado, el 2005 se procesa en el mismo despacho que el 2661 y basta: p1b, `vf13` del NPC a 139605,921710 y 2661
aceptado a 139605,921728 (18 µs) [V]. El argumento de la otra IA de que el 2005 se materializa de forma asíncrona es
[H]: en todas las sesiones medidas, «2005 delante en el mismo despacho» nunca falló.

### 2.2 Cerca: ¿crea el servidor la tienda del invitado?

**No [V en datos, E fuerte en código].**
- La evaluación de celdas del 2043 (`0x287DBB0` → `0x288B7A0`) tiene una **puerta de dueño** en `0x288B87A`
  (`cmp [r15+0x180],eax ; jne 0x288BDEE`): solo evalúa el gestor cuyo personaje es el dueño del campo. U-a (de `user2_stages`,
  o el punto G de ShopProbe) hace pasar a `mgr2` poniendo `eax := [r15+0x180]`, y solo con U-1 armado (`user2_stages.h`
  §U-a) [E fuerte].
- U-1 se arma a ≥ 10 celdas durante 10 s y se vacía a ≤ 7. Cerca, `mgr2` está en estado 1 o 0, sin evaluar: no prepara
  nada.
- En S45 (invitado en Hernand junto al anfitrión), 0 escenas de tienda propias [V]. En p2b, las propias salieron solo de
  `mgr2` armado lejos (U-f abierto) y de la gemela [V].
- **Corrige `PENDIENTES.md` («El servidor ya crea una tienda por jugador»):** nativamente solo el dueño del campo evalúa.
  Lo que es por jugador es el modelo: una stage de `mgr2` tiene como dueño a `pj2` y su 2661 va ToTarget a él
  (`tiendas-u2.md` §1.1). Para que exista hace falta que el mod haga evaluar a `mgr2`.

### 2.3 La gemela: por qué la borró el 2043 y por qué rompió al vendedor del anfitrión

**(a) Vida de la gemela: el 2043 de `mgr2` en estado 1 no termina [V].**
- p2b `host.log:34206-34207`: `2043 reached the manager … of 0xA0100066 (user 2) in state 1, reschedule 1` → `0x287D4C0`
  (lista vacía). La gemela `0x2DE` se prepara 2,5 ms después (`:34199`/`:34306`).
- `:34490-34491`: el siguiente 2043, 104 ms después, **también en estado 1 y reprogramado**, descarga «sequencers 1». El 2810
  sale 91 µs después, con motivo `0x0E` (`:34307`; payload `660010a0 de02… 10439341 0e`).
- **Esto contradice `tiendas-u2.md` §12.3** («en estado 1 no reprograma: la cadena termina»). Con `user2_timer_route=1`, la
  cadena del usuario 2 sigue en estado 1, y cada tick descarga todo lo que no es tipo 3 ni está en `[mgr+0xC8]`.
  **Ninguna escena puede vivir en `mgr2` desarmado** más de ~100 ms. El requisito «`mgr2` asentado» de la otra IA no lo
  evita.
- Además [V]: el primer vaciado de p2b (`:30581`, estado 2 → 1) **dejó** 2 secuenciadores y 52 horarios
  (`:32291 stages left in its manager after a drain`). Las escenas de tienda `0x142/0x143` cerraron en un segundo vaciado
  1 s después (`:33219`), con la escena del anfitrión `0x2A2` ya preparada. Hubo 1 s de solape de dos escenas sobre el mismo
  NPC también en el relevo normal, sin gemela.

**(b) El daño al anfitrión: el `SetInteraction` del servidor borra por clave, no por escena [V decompilado legible + E-x].**
- `0x2369360` es el `SetInteraction` común (lo comparten cliente y servidor; `guest_x_veto` ya lo engancha en el cliente en
  `0x236938A`). `*rdx` = la etapa; `[stage+0x30]` = su id; `[stage+0x58]` = el procesador. Para cada actor destino,
  `comp = [[actor+0x68]+0x98]` y:
  - **alta** (`r8b = 1`): `0x2124370` → `.xtls 0xE402CC0(comp, …, clave u16 [this+0x38], …, id de la etapa, …)`, que hace
    `push_back` en `comp+0x88` y escribe la **última** entrada (`+0x10` clave, `+0x20` id de etapa) [E-x, `d2369360.txt`];
  - **baja** (`r8b = 0`): `.xtls 0xE404A10(comp, clave)` recorre el vector y **borra la primera entrada cuya clave coincide**,
    moviendo la última a su hueco. **No mira el id de la etapa** [E-x, `de404a10.txt`].
- La reconstrucción de p2b cuadra exactamente:
  1. `0x143` (de `mgr2`, más antigua): `[E143]`.
  2. `0x2A2` (anfitrión): `[E143, E2A2]`.
  3. El vaciado borra `0x143`: su baja quita la primera, que es la suya → `[E2A2]`. Bien, por suerte de orden.
  4. La gemela `0x2DE`: `[E2A2, E2DE]`.
  5. La descarga borra la gemela: su baja quita la **primera**, que es **la del anfitrión** → `[E2DE]`.
  6. Los cinco 2671 del anfitrión toman `E2DE` → `stage id 0x2DE … NOT found` → 3000 (`host.log:36604`, `:36705`; cinco pares
     2671/3000 en la captura).
- **Causa raíz:** dos escenas vivas con `SetInteraction` de la misma clave sobre el mismo NPC solo son seguras si se borran en
  el orden en que se crearon (FIFO). Si se borra primero la más nueva (LIFO), se lleva la entrada de la otra y deja la suya
  huérfana. La entrada huérfana es **permanente** mientras viva el NPC: la toma `0x2124060` para todos los jugadores.
- Lo mismo vale **en el cliente**, porque es la misma función: una escena de X que se borra antes que la de M puede llevarse la
  entrada de M [E-x].
- La dirección `entry 0x1A1EFF868` de ShopProbe es la copia temporal que recibe `0x2126920`, como ya señaló la otra IA. No hizo
  falta ninguna sobrescritura: basta el borrado por clave.

**(c) Lo que la otra IA dejó bien:** la gemela pasó a probe a la fuerza. Bien hecho; el diseño de la gemela no se puede
reactivar.

---

## 3. Diseño de raíz

### 3.1 Principios

1. **Una sola entrada de servidor por (NPC, clave).** Si hay una escena del anfitrión sobre ese NPC, la entrada es la suya.
   El invitado interactúa sobre ella: `0x2126920` la valida en `mgrH` y no comprueba quién pide. Solo valida la escena, el
   gestor de misiones del anfitrión (la regla) y la distancia del que pide [E].
2. **La escena del invitado vive en `mgr2` armado (estado 2).** Nunca en estado 1/0: el 2043 la descarga (§2.3 a).
3. **El orden nativo se respeta en el anfitrión.** Un 2661 propio del usuario 2 va detrás de los 2005 de sus actores
   enlazados, como las copias.
4. **Coherente con S44/S45.** La escena propia va nativa a M: S44 la deja (`OwnScene`), R-A/R-B no la cortan (G = M). La
   escena del anfitrión en X sigue inerte, y además **no registra entradas de tienda en el cliente** (vía C, la misma regla
   de S45: de una escena ajena solo corre lo que es mundo).
5. **Comercio por jugador** (2789/2366 con el actor que manda) y **mundo/misiones del anfitrión** (vf40 sin tocar; U-a2 ya
   usa el gestor de misiones del anfitrión).

### 3.2 Piezas

#### T1 — Lejos: preparar las tiendas propias (existe; completar)

- U-f con `user2_stages_elements=schedule+funcnpc`, **solo con la lista blanca de comercio** (`smithy` = vendedor de armas de
  fuera, `butcher`).
- **Reutilizar** de la otra IA: `funcnpc_trade_policy.h` y el veredicto de `user2_shops_logic.h` con esa lista.
- Requisitos ya revisados (§11 de `tiendas-u2.md`): ruta capaz, ≥ 10 celdas, sin espera de vaciado.

#### T2 — Orden en el anfitrión (nuevo; sustituye a `guest_shop_order` como arreglo)

Clave **`observer_hold_scene_order = 0 | probe | 1`** (anfitrión; necesita `observer_hold_acks=1`, ya en el kit 39).
- En `take_frame`, para la sesión del usuario 2: un **2661 cuyo dueño (`@5`) es el personaje del usuario 2** y que enlaza
  (tipo 4/5, `@91 + 11n`) alguna clave que `observer_hold` le está reteniendo se guarda. También se guardan, en orden, los
  2981/2524/2810 posteriores de ese id.
- Se encola justo detrás del 2005 que anuncia la **última** de esas claves. Es el mismo patrón de `kHeldOps`, pero con varias
  claves.
- Si se cae la retención de una clave (despawn, época, salida), los paquetes guardados se sueltan en orden. El cliente
  rechazará la escena como hoy, sin daño. También hay topes (por escena, totales y 40 s) con la misma política.
- `probe`: línea `[would hold scene]` con el id, las claves pendientes y el retraso que habría tenido.
- **Reutilizar** de la otra IA (en `guest_shop_order_logic.h` y sus tests):
  - los formatos de 2981 (`21+8n`) y 2524 (`18+11n`);
  - las cancelaciones por 2810/2516 y las fronteras 2174/2643(append 0)/2756;
  - los fixtures reales de `0x142/0x143`.
  
  Se lleva la política al anfitrión.
- `guest_shop_order` se queda en **`probe`** en el invitado durante las pruebas, como comprobación independiente: con T2
  activo, no debería decir `retendria` nunca. Su modo 1 queda solo como alternativa si T2 no bastara.

#### T3 — Guardia de interacción en el servidor (nuevo; prerrequisito de todo lo de cerca)

Clave **`shop_interaction_guard = 0 | probe | 1`** (anfitrión).
- Un `create_mid` en el mismo punto verificado que usa el invitado: `0x236938A` dentro de `0x2369360`, tras el prólogo. En el
  proceso del anfitrión nadie lo engancha (`guest_x_veto` es solo del invitado).
- Solo actúa en llamadas **de servidor**: el vptr del procesador `[[rdx]+0x58]` es `0x5B1FE10`. Con el de cliente
  `0x558C080`, la llamada es del cliente del anfitrión y no se toca.
- Lecturas protegidas de: la etapa (`[stage+0x30]` id, dueño), el NPC (clave), `comp+0x88/+0x90` y las entradas
  (`+0x10`, `+0x20`).

Reglas:

| Regla | Cuándo | Qué hace |
|---|---|---|
| **G1** | Alta de una etapa de `mgr2` y el NPC ya tiene una entrada con esa clave | Salida nativa (la que `guest_x_veto` ya repite: «la búsqueda del actor falla» → `0x23698C6`). Se anota (id, NPC) como **suprimida** |
| **G2** | Baja de una etapa suprimida | Salida nativa (no tiene entrada que borrar) |
| **G3** | Alta de una etapa de `mgrH` y el NPC tiene una entrada viva de una etapa de `mgr2` | El alta pasa (el anfitrión siempre registra la suya). Se encola **borrar esa etapa de `mgr2`** a la salida del Troc más externo del mismo hilo |
| **G4** | Baja de una etapa de `mgrH` con etapas de `mgr2` suprimidas sobre ese NPC | La baja pasa. Se encolan los borrados de esas etapas suprimidas, para que `mgr2` las vuelva a preparar ya con entrada propia |
| **Aviso** | Baja, de quien sea, cuya primera coincidencia no es su propia entrada (el caso LIFO) | WRN `[would orphan]`; criterio de aborto |

Notas a las reglas:
- **G3:** el borrado de `mgr2` corre su baja, que quita la **primera** coincidencia. Esa es la suya, porque es la más antigua
  (§2.3 b), así que queda `[E_host]`. Es lo que pasó en p2b con `0x143`, pero ahora garantizado.
- **G4:** es [H] si `mgr2` vuelve a preparar un id borrado en una celda ya evaluada. Si no lo hace, el invitado se queda sin
  tienda hasta el siguiente armado.
- **Borrado:** `0x2879440(mgr2, campo, &id, 0x0E)`. Es el contrato corregido por la otra IA: `rdx` es el **campo**, no un
  `int*`. Retención con `0x1F6C540` y liberación `0x1434880`, con SEH, como U-c/U-d.

`probe` (**la sonda I, la primera que hay que medir**):
- sin escribir nada, una línea por alta y baja del servidor sobre NPC de tienda: id, gestor (H/U2), NPC, clave, la lista antes
  (n, y para cada entrada clave e id) y la posición de la primera coincidencia;
- más `[would suppress]`, `[would delete]` y `[would orphan]`.

Comprueba (a) que la descarga 0xE corre la baja, (b) el orden y (c) el solape del relevo, sin tocar nada.

#### T4 — Cerca: U-1 «modo tienda» (nuevo)

Clave **`user2_shops_near = 0 | probe | 1`** (anfitrión; necesita T3 en 1, `user2_shop_route=1` y U-1 en 1).
- En el campo del anfitrión, `mgr2` también se **arma cerca** (< 10 celdas), con el mismo armado nativo `0x287F450` + U-c2.
- En modo cercano, U-f deja pasar **solo** los elementos de la lista blanca de comercio. Los horarios y todos los demás
  secuenciadores se saltan: las stages de horario cerca son las del anfitrión.
- La escena del invitado la prepara la **evaluación nativa** de su celda (dueño `pj2`, 2661 ToTarget, el mismo NPC por el mapa
  del campo). No hay `Prepare` del mod, ni contrato de binding, ni copia de `playData`.
- Con T3: si el anfitrión ya tiene su escena, la del invitado nace **sin entrada en el servidor** (G1). El 2671 del invitado
  toma la entrada del anfitrión → `0x2126920` en `mgrH`: FOUND → 2663/2916 al invitado. Su cliente sigue con **su** escena de
  M: 2436/2173/2767 con el id propio → U2-a → `mgr2` → 2524/2981.
- Transiciones:
  - **lejos → cerca:** el vaciado de hoy (borra las escenas de `mgr2`; con G3, aunque se solapen, el orden es FIFO) y
    rearmado inmediato en modo cercano;
  - **cerca → lejos (≥ 10 celdas):** vaciado y rearmado lejos, porque las celdas evaluadas en modo cercano no tienen horarios;
  - **cargas y logout:** como hoy.
- Ajustes de U2:
  - «U-1 armado» (M4) se cumple también cerca;
  - la espera de vaciado con la tienda abierta (A1) se aplica igual;
  - el veredicto «< 10 celdas → no tienda» de U-f pasa a «< 10 celdas → solo tienda».
- **Alternativa B1-bis (si T4 da problemas):** armar `mgr2` cerca con la puerta U-a **cerrada** (estado 2, sin evaluar) y
  preparar la escena de forma explícita. Vuelve a necesitar los contratos de `Prepare` de la otra IA (§5 de `s41-tenderos.md`).
  Solo si T4 falla.

#### T5 — Cliente del invitado: X no registra entradas de tienda (nuevo, pequeño)

Clave **`guest_x_shop_interaction = 0 | probe | 1`** (invitado).
- Excepción por recurso a `guest_x_ui_veto_allow=SetInteraction`: para etapas de X cuyo recurso empieza por
  `cd_seq_funcnpc_`, `SetInteraction` se veta **en los dos sentidos** (alta y baja de la misma etapa).
- Se usa la salida forzada que ya existe en `0x236938A`, y el nombre de la etapa que `guest_x_veto` ya lee
  (`[[[proc+0x10]+0x48]+0x1B8]`).
- **Por qué:**
  - las entradas de X nunca dan aviso (52/52 rechazadas en S45 [V]);
  - pueden tapar la de M en la elección por primera coincidencia [H];
  - su baja puede **borrar la de M** (borrado por clave, §2.3 b [E-x]).
- Es la regla de S45 aplicada a la vía C: la entrada de tienda de una escena ajena es interacción del jugador local, no mundo.
- Las demás `SetInteraction` de X (diálogos, Renee/Delkin de la S11) siguen permitidas.

#### T6 — Diagnóstico y abortos (reutilizar de la otra IA)

- El parser de 3000 corregido (requester `@0`, NPC `@4`, interacción `@8`, error `@12`; fixture real
  `010010a0ec0210b021430f005d34c858ffffffff000100`) y ShopProbe sin «ACCEPTED» por el retorno exterior.
- El fin de la ventana de tienda con `2743 InteractionResetAck` coincidente, y la separación del aborto por origen
  (lejos / cerca).
- Abortos nuevos:
  - cualquier 3000 de `0x000F4321` **al anfitrión** (visible en su propio sniffer), sea cual sea el retorno exterior;
  - cualquier `[would orphan]` en modo 1.

### 3.3 Qué se reaprovecha y qué se descarta de `opcion-d-tenderos`

| Pieza | Decisión | Por qué |
|---|---|---|
| `funcnpc_trade_policy.h` (lista smithy/butcher) | **Reutilizar** | Acota U-f lejos y cerca |
| Parsers de 3000 + tests con el paquete real; texto de ShopProbe | **Reutilizar** | Arreglo verificado [V] |
| Separación lejos/cerca en los abortos de `user2_shops`; fin con 2743; `ArmRequest`/`DrainRequest` (`shop_troc_logic.h`) | **Reutilizar, revisando** | Útil para T4; hay que revisarlo con el modo cercano |
| Contratos estáticos: `Delete 0x2879440 (mgr, campo, &id, motivo)`, `Prepare 0x287C200`, liberación `0x38AD90`, firmas | **Reutilizar** | G3/G4 llaman a `Delete`; `Prepare` solo para B1-bis |
| Análisis de capturas (`s41p2-datos/capturas/analizar.py`, cronologías) | **Reutilizar** | Reproducible |
| `guest_shop_order` | **Probe como verificación**; política y tests llevados a T2 | La raíz está en el anfitrión (§2.1) |
| `funcnpc_twin` (Prepare en `mgr2` desarmado, un intento por proceso, gancho Delete, barrera ModLock, selector de recurso, 1 s de gracia) | **Descartar** | Muere en ≤ 100 ms (§2.3 a) y, con LIFO, rompe la entrada del anfitrión (§2.3 b). T3 + T4 lo sustituyen |
| Requisito «`mgr2` asentado» | **Descartar** | En estado 1 la cadena sigue y descarga |

### 3.4 Puntos verificados y por verificar

| Punto | Estado |
|---|---|
| `0x2369360` añade con `0x2124370 → .xtls 0xE402CC0` y borra con `.xtls 0xE404A10` por clave (primera coincidencia) | [V] decompilado legible del llamador; [E-x] los cuerpos `.xtls` |
| `0x236938A` (punto mid y salida forzada `0x23698C6`) | [V] ya verificado y en uso en el invitado (`guest_x_veto.cpp:177`); en el anfitrión hay que repetir la comprobación de bytes |
| Vptr del procesador de servidor `0x5B1FE10` / cliente `0x558C080` | [V] S45 §3.1 |
| `[stage+0x30]` = id, `*rdx` = etapa en `0x2369360` | [V] decompilado (`*(*param_2+0x30)` va a `+0x20` de la entrada) |
| Dueño de la etapa en el servidor (`[[stage+0x48]+0x60]` o `[stage+0x98]`) | [E] `[[stage+0x48]+0x60]` lo usa `0x288A970`; hay que confirmarlo en la sonda I |
| La descarga 0xE ejecuta la baja de `SetInteraction` | [E] (cuadra con p2b); lo dice la sonda I |
| El 2043 de `mgr2` en estado 1 se reprograma | [V] p2b |
| Puerta de dueño `0x288B87A` | [V] bytes (`user2_stages.cpp:91`) |
| El 2005 delante, en el mismo despacho, basta para `0xA9CA40` | [V] 169 copias sin rechazo, 18 µs en p1b |

### 3.5 Qué no está demostrado

- [H] Que un invitado que interactúa sobre la **entrada del anfitrión** (cerca) reciba 2663/2916 y que su escena M continúe.
  `0x2126920` no mira quién pide [E], pero `0x2120CC0` (distancia y ángulo) y el estado de interacción del NPC
  (preempción) con dos jugadores no se han medido.
- [H] Que el anfitrión pueda hablar con el mismo tendero **mientras** el invitado está en su tienda. Lo nativo de varios
  jugadores sería un «ocupado»; hay que medirlo (T5 de `tiendas-u2.md`).
- [H] Que 2789/2366 no comprueben la escena ni la entrada de interacción del NPC (solo el actor que pide y su componente).
- [H] Efectos del gráfico de servidor de la escena del invitado sobre el NPC compartido, fuera de `SetInteraction`
  (`BlockInteraction`, categoría de IA). `BlockInteraction` podría tener el mismo problema de clave; la sonda I debería
  incluirla.
- [H] Que `mgr2` en modo cercano vuelva a preparar una escena borrada por G3/G4 en una celda ya evaluada.
- [H] El coste y los efectos de tener `mgr2` armado cerca (evaluación duplicada con el gestor de misiones del anfitrión vía
  U-a2, U-e, el hueco de 4–9 celdas).
- [H] Qué hace el cliente del invitado si recibe el 2810 de su escena con la interfaz de tienda abierta (G3 al llegar el
  anfitrión, o el vaciado).
- [E-x] El borrado por clave se ha leído en `.xtls` (parcialmente ofuscado). La reconstrucción de p2b lo confirma, pero la
  sonda I es la prueba.

---

## 4. Plan de prueba con dos copias en un PC

Preparación (CLAUDE.md):
- copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups\`, con la partida del invitado separada;
- la segunda copia lanzada con `SteamAppId`/`SteamGameId=3321460` y `PlatformServiceType=Steam`;
- papel, ini y log propios por copia; los dos procesos reiniciados en cada paso;
- marcas Ctrl+F12 en las dos ventanas en cada acción;
- dinero e inventario de **cada** jugador apuntados antes y después.

Recurso: el vendedor de armas de fuera (`cd_seq_funcnpc_smithy`, `0xA7132561`) y la carnicería (`0x978345CA`), en Hernand.

### Paso 0 — Control del anfitrión (siempre primero)

- El anfitrión, solo, habla con el vendedor de armas y compra.
- Esperado: `ShopProbe [server check] … host … field owner's … FOUND | result 2` y 2671 → 2663 → 2720 → 2916 en su captura.
- Si falla, parar: no atribuirlo al mod nuevo.

### Paso 1 — Sondas (todo `probe`; lejos y relevo)

- **Anfitrión:** `user2_stages_elements=schedule+funcnpc`, `user2_shop_route=probe`, `observer_hold_scene_order=probe`,
  `shop_interaction_guard=probe`, `user2_shops_near=0`, `funcnpc_twin=0`, `shop_probe=1`.
- **Invitado:** `guest_shop_order=probe`, `guest_x_shop_interaction=probe`, `guest_x_interaction_stage=probe`.
- Ojo: `schedule+funcnpc` **prepara** escenas lejos, igual que en p2b; no es puramente observacional.
- **Recorrido:**
  1. el invitado lejos (≥ 11 celdas) va al vendedor de armas;
  2. el anfitrión llega a Hernand;
  3. el anfitrión habla con el vendedor;
  4. el anfitrión se va.
- **Mirar:**
  - `[would hold scene]` para el 2661 propio con las claves retenidas y ~340 ms;
  - `GuestShopOrder [retendria]` en el invitado (hoy sí; con T2 en 1, nunca);
  - en la sonda I:
    - las altas y bajas, con la lista;
    - en el relevo, `[E_u2, E_host]` y la baja de la de `mgr2` en primera posición;
    - **si la descarga 0xE produce baja**;
  - ningún `[would orphan]` fuera del caso esperado.

### Paso 2 — Lejos activo

- **Cambios:** `observer_hold_scene_order=1`, `user2_shop_route=1`, `shop_interaction_guard=1` (G3 protege el relevo).
- **El invitado** (≥ 11 celdas, `[ARMED]`):
  - habla con el vendedor de armas: el aviso sale de M (`GuestXInter … in M (native)`);
  - abre, compra un objeto barato, vende otro y cierra;
  - repite con la carnicería.
- **Pasa si:**
  - el 2661 propio llega **detrás** del 2005 y se crea con resultado 0;
  - `[server check] for USER 2 … ROUTED by U2-b: FOUND`, 2663/2916 al invitado, `[route] … ROUTED` de 2436/2173 y `[shop
    followed]`;
  - aparecen 2789/2366 del invitado sin Nak;
  - **el dinero y el inventario cambian solo en el invitado**;
  - ningún 3000, `ABORT` ni ERR.

### Paso 3 — Cerca activo

- **Cambios:** `user2_shops_near=1`, `guest_x_shop_interaction=1`.
- **Los dos juntos en el vendedor de armas:**
  1. `[ARMED near]` y la escena de tienda del invitado preparada por `mgr2`, con `[suppressed]` (G1): el anfitrión ya tenía la
     suya;
  2. el invitado ve el aviso (M), habla, compra y vende;
  3. **después, el anfitrión habla y compra**: `[server check] … host … FOUND` (con su propio id), 2663/2916, la compra hecha y
     **ningún 3000**;
  4. el invitado vuelve a usar la tienda.
- **Mirar en las dos ventanas:**
  - el anfitrión no ve nada de la tienda del invitado;
  - el tendero sigue animado en las dos;
  - la tienda del anfitrión sigue funcionando.

### Paso 4 — Relevos con la tienda abierta y cerrada

1. **Lejos → cerca, tienda del invitado abierta:**
   - el anfitrión hace un viaje rápido a Hernand;
   - esperado: G3 borra la escena de `mgr2` con FIFO;
   - el invitado ve cerrarse su tienda (apuntar cómo) y luego reabre en modo cercano;
   - el anfitrión compra después.
2. **Cerca → lejos, tienda del invitado abierta:**
   - el anfitrión se va;
   - esperado: G4 y luego el vaciado y rearmado lejos;
   - el invitado reabre con su escena propia (lejos).
3. **El invitado se aleja de un tendero mientras el anfitrión sigue en él:** la tienda del anfitrión sigue funcionando.
4. **Al final:** el anfitrión hace una carga limpia y vuelve a comprar al mismo vendedor.

**Criterios de aborto en todos los pasos:**
- un 3000 de tienda al anfitrión;
- `[would orphan]` / `[orphan]`;
- `ShopProbe [server check] … host … NOT found`;
- el dinero o el inventario cambiados en el jugador equivocado;
- pérdida de cámara o control;
- un cierre.

Después, Shadow (red real) con los pasos 2–4.

---

## 5. Archivos

- Este informe: `scratchpad\s46\tenderos.md`.
- Scripts: `scratchpad\s46\{funcnames,scan,order,seqtl}.py`; datos: `funcnpc.json`, `scan_s45.json`.
- Decompilados nuevos: `scratchpad\s46\d2369360.txt`, `de404a10.txt`, también guardados por Ghidra en
  `D:\RE\CrimsonDesert_2976\exports\decomp\0x2369360.c` y `0xe404a10.c`.
- Fuentes principales:
  - `C:\Emulador CD\backups\p8-2inst-tenderos-p2b\host.log`: 12343-12538, 29635-34491, 36604/36705;
  - `…\guest.log`: 9555-9575, 19676-19679;
  - `…\p1b\guest.log`: 8291-8295, 8552, 10471;
  - `…\p8-2inst-s45-paso2\` y `…\paso3\`: `host.log` y capturas;
  - `C:\Emulador CD-2inst\src\mod\session2\{user2_stages.h, observer_hold_acks*.h, observer_seq_copy.h,
    guest_x_veto*.{h,cpp}, guest_seq_route_logic.h}`.
