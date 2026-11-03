# P8 sesión 8: mascotas (retraso y «flotar» de la del invitado; la del anfitrión no aparece)

10 de octubre de 2026. Build **1.0.0.2976**, base `0x140000000`, direcciones en RVA. Solo lectura: capturas y
logs de la sesión 8 y capstone sobre el exe. No se tocó `src/`, no se hizo commit y no se lanzó el juego.

- **[V]** = comprobado, con la evidencia al lado (captura, log o desensamblado).
- **[E]** = deducción fuerte que falta medir en vivo.
- **[H]** = hipótesis.

## Fuentes y reloj

| Id | Fichero |
|---|---|
| HC | `C:\Emulador CD\backups\p8-sesion8-host\cdmp_packets_20261010-103235.bin`: cliente del anfitrión |
| HL | `…\p8-sesion8-host\cdmp.log` (10:32–10:51) |
| GC | `C:\Emulador CD\backups\p8-sesion8-shadow\cdmp_packets_20261010-093514.bin`: cliente del invitado |
| GL | `…\p8-sesion8-shadow\cdmp.log` (09:34–09:51, reloj del anfitrión − 1 h) |

**Alineación [V]:**
- HC `t0` = 10:32:48,30. Ancla: `ObserverSeqCopy` de HL, `capture t=13.875772` a ~10:33:02,172. No es 10:32:35:
  el nombre del fichero es la hora de apertura, no la del primer paquete.
- GC `t0` = 09:35:43,52 (= 10:35:43,52 del anfitrión). Ancla: `GuestDiag loading transition`, `capture t=4.343505`
  a ~09:35:47,862.
- Desfase HC − GC = **174,787 s**. Es la mediana de 8080 `MoveActorReq` del personaje del invitado emparejados
  por `ts` de juego con sus `MoveActorAck` en HC; p10–p90 = ±15 ms.

**Claves de esta sesión [V]** (`Comp probe: CompCreate` de HL y registros 2005):

| Clave | Qué es |
|---|---|
| `0xA0100001` | personaje del anfitrión |
| `0xB0100002` | **mascota del anfitrión** (tipo 6, ranura 1; la misma clave que en la sesión 7) |
| `0xB0100003` | caballo del anfitrión |
| `0xA0100333` | personaje del invitado |
| `0xB0100334` | **mascota del invitado** (tipo 6, ranura 1) |

**Scripts** (scratchpad de la sesión, no se conservan):
- `pets.py`: extrae del `.bin` los 2005 y los paquetes con clave en `+0`;
- `ana.py`: conteos y cadencias;
- `sa.py`/`win.py`: padre de StartAction y ventanas «en el hombro»;
- `perch.py`: mascota frente a personaje;
- `lat.py`: latencia;
- `sim.py`/`sim2.py`: simulación de políticas de retardo.

Formato del registro (`<QBBHHHII>`) y de los 2005 (`[u16 len][u8 tipo][u32 clave]…`): como en
`tools/scripts/analyze_capture.py`.

---

## 0. Resumen

1. **(2) La mascota del anfitrión no existe en el cliente del invitado [V].**
   - GC recibe **un solo** 2005 con `0xB0100002`, a `t` = 50,959. Es el mismo paquete que el personaje del
     anfitrión: `[7 B010006C][0 U90100000][1 A0100001][0 U90100000][6 B0100002]`.
   - 23 ms después (09:36:34,503), GL registra `Game error: "Actor 생성에 실패했습니다." (… 0x90100000B0100002 …,
     from exe+0x8BDF7C)`: falló `CreateOwnedActor` `0x8AF580`, rama `0x8BDC4A`.
   - Nunca se vuelve a anunciar. Los **277** `MoveActorAck`, **599** `StartActionAck` y **914** `ChangeAIStateAck`
     de esa clave que llegan después van a un actor que no existe.
   - Ninguna retención la tocó: HL da `held 1 (host's 0, user 2's 1)` y `character hold: held 0`.
   - Es exactamente el caso (A) de `p8-actores-anfitrion.md` §1.2. Sale **en las 7 sesiones con Shadow** (3, 4,
     5, 6A, 6B, 7 y 8: misma línea en cada `cdmp.log` del invitado). Hasta hoy nunca se arregló.
2. **(1) Mascota del invitado en el anfitrión: no es la red ni una retención. Son dos causas que se suman
   [V datos / E código].**
   - **(1a) Retardo:** `RemoteInterp` le aplica **1,1–1,8 s**, frente a los 200 ms de su dueño. Con su cadencia
     (mediana 668 ms, p90 1669 ms), `max(últimos 3 huecos) + 150` casi siempre supera 1,3 s.
   - **(1b) «Flotar»:** subirse al hombro es un **enganche por `StartAction` (`+0x116` = personaje)**, nunca por
     Move.
     - Mientras está en el hombro, la mascota **no manda ningún `MoveActorReq`**: solo una `StartActionReq`
       cada **4,17 s**.
     - En modo 2 el padre del Path no se aplica, así que el anfitrión ve la mascota a 1,58 m de altura en la
       última posición recibida mientras el personaje anda.
     - Esos huecos de 4,17 s llevan además el retardo al tope de 1800 ms. Tras bajarse, la mascota sigue
       mostrándose «arriba» ~1,8 s.
3. **La interpolación remota funciona para los NPC [V]** (GL, último resumen):
   - vaciados del búfer de `0xB`: **13 201**, frente a **80 438** con el umbral del juego (−84 %);
   - el 71 % de los huecos en ≤ 1500 ms;
   - 0 sin resolver y 1 075 598 sustituciones.

---

## 1. Mascota del anfitrión (`0xB0100002`) en el invitado

### 1.1 Lo que llega y lo que falla [V]

| `t` GC | Hora | Suceso |
|---|---|---|
| 50,869 | 09:36:34,387 | Estado de carga `0x0C → 0x0D → 0x0E` (GL) |
| **50,959** | 09:36:34,48 | 2005 de 5 registros: gimmick `B010006C`, `U(90100000)`, **`A0100001`** (11 582 B), `U(90100000)`, **`B0100002`** (9244 B, tipo 6, dueño `90100000`, ranura 1) |
| — | **09:36:34,503** | `Actor 생성에 실패했습니다` con `0x90100000_B0100002` = `[rsp+0x70]` de `0x8BD530` (clave abajo, dueño arriba) |
| 51,8–773,9 | | 914 × 2404, 599 × 2991, 277 × 2697 (desde 61,0), 8 × 2506/3007 de `B0100002`: **sin actor** |
| 50,959–954 | | ningún otro 2005, ni `RemoveActorAck`, con `B0100002` |

**Código [V], capstone de `0x8BD530`:**
- `0x8BDC3E call 0x8AF580`;
- `0x8BDC44 cmp byte [rbp-0x48],0`;
- `0x8BDC4A lea rax,[0x55B6510]`, que apunta a la cadena «Actor 생성에 실패했습니다.»;
- `… 0x8BDF77 call 0x35D8D0`, cuyo retorno es `0x8BDF7C`, el `from` de la línea.

**Por qué falla.** Lo más probable es la condición **c5** de `data/c1-actor-rechazado.md` §1.4 **[E fuerte]**:
- el hijo se engancha al personaje principal de su dueño y exige `[pj+0x5E] & 0x10`;
- ese personaje se ha creado en el mismo paquete y sigue cargando (estado 8).

**El contraste lo confirma [V]:**

| Caso | ¿Mismo 2005 que el personaje principal? | Resultado |
|---|---|---|
| `B0100002` en el **cliente del anfitrión** (HC) | no: 2005 a 55,396 y 55,485 (**+89 ms**) | se crea |
| `B0100334` (mascota del invitado) en el **cliente del anfitrión** (HC) | no: 225,673 y 228,797 (+3,1 s, por D-53) | se crea |
| `B0100334` en el **cliente del invitado** (GC, su propio login) | no: 51,345 y 51,759 (+414 ms) | se crea |
| `B0100002` en el **cliente del invitado** (GC) | **sí**: el mismo paquete, detrás de `A0100001` | **falla**, en 7 de 7 sesiones |

### 1.2 Por qué ninguna retención la detiene [V]

- **D-53 (`child_hold`)** solo retiene con byte de restauración 0. La mascota del anfitrión lleva desde 55,8 s
  mandando acciones (872 `StartActionReq` en HC), así que su byte es 1.
  - HL, `Session2 probe`: `held 1 (host's 0, user 2's 1 …)`. La única retenida fue `0xB0100334`, liberada a
    los 3062 ms «after StartActionReq».
- **`character_hold`:** `held 0`. `A0100001` ya está restaurado.
- **`observer_hold`:** solo NPC sin dueño.
- **`observer_hold_acks`:** solo actúa sobre claves retenidas.
- Ninguna línea `held 0xB0100002` en HL.

**Conclusión:** el problema **no es** una retención que no suelta. Es lo contrario: el hijo se entrega
**demasiado pronto**, pegado a su personaje principal.

## 2. Mascota del invitado (`0xB0100334`) en el anfitrión

### 2.1 Cadencia y reenvío [V]

| Medida | Invitado (GC, Req) | Anfitrión (HC, Ack) |
|---|---|---|
| `MoveActorReq`/`Ack` | 254 | 244 (96 %) |
| `StartActionReq`/`Ack` | 613 | 594 (97 %) |
| Huecos del Move solo | p50 1014, p90 9081 ms | ídem |
| Muestras de posición combinadas (Move + StartAction, `ts` distintas) | 854; p50 666, p90 1669 ms | 826; p50 668, p90 1669 ms; 270 > 1 s; 32 > 2,5 s |
| Latencia GC→HC (desfase − 174,787 s) | — | igual que la del personaje: p10–p90 ±15 ms y máximo +34 ms |
| `+0x8C` del Move | −1 en 254/254 | −1 en 244/244 |

**La red no añade retraso:** la mascota llega con la misma latencia que su dueño.

Mascota del anfitrión, para comparar (HC): 378 Move, p50 1022, p90 7385 ms; 872 StartAction; `+0x8C` = −1 en
378/378.

### 2.2 El hombro es una acción con padre, no un Move con padre [V]

- `StartActionReq +0x116` (Ack `+0x117`) = `0xA0100333` en **71/613** Req (GC) y **70/594** Ack (HC).
- La mascota del anfitrión hace lo mismo con `0xA0100001`: 63/872.
- En todas, `+0x16C` es el dueño (objetivo actual).
- **15 subidas** en HC, cada una de **8–11,7 s**:
  - hash de subir `0xBC77AF45`/`0xA497102C`, hash de bajar `0x8276C0C5`;
  - **0 `MoveActorReq`** dentro de cada ventana: solo 4–5 `StartAction`, cada **4,15–4,20 s**.
- Posición de esas muestras (Ack `+0x8F`): **1,58 m por encima** del personaje, a < 0,15 m en horizontal. Es el
  hombro, en coordenadas de mundo.
- En 7 de las 15 ventanas el personaje **anda** con la mascota encima: 3,3, 13,7, 10,1, 11,8, 7,3 m…, 50,2 m en
  total.

Ejemplo, HC 380,1–389,7: el personaje recorre 13,7 m y la mascota manda 4 muestras.

**Qué hace el cliente que observa [E, código de `movimiento-remoto-cliente.md` §3]:**
- `StartActionAck` inserta su posición (`+0x8F`, `ts +0x160`) en el búfer como un Move.
- El Path (`+0xD7`, padre en `+0x117`) **se guarda y no se aplica** en el modo 2.

**Resultado:** el anfitrión ve la mascota a la altura del hombro en la posición de hace hasta 4,2 s, mientras el
personaje (muestras cada 17 ms, retardo de 200 ms) ya se ha ido. Es el «se queda encima, en el aire».

### 2.3 Lo que aplicó `RemoteInterp` [V, HL]

- `controllers seen in the last minute 2 (0xB 1, of them read by the mode-2 update 1)`. El único `0xB` en modo 2
  del anfitrión es esta mascota.
- **`largest delays: 0xB0100334`**, por minuto:

  | Minuto | 10:37 | 10:38 | 10:39 | 10:40 | 10:41 | 10:42 | 10:43 | 10:44 | 10:45 | 10:46 | 10:47 | 10:48 | 10:49 |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | Retardo (ms) | 1360 | 1150 | 1163 | 1165 | 1138 | **1800** | 1163 | 1553 | 1158 | 1393 | **1800** | 1200 | **1800** |

  - **10:42:** `(gaps 2501/734/100)`.
  - **10:49:** `(gaps 4175/572/4179)`, los huecos de la subida al hombro.
- **Histograma por frame, acumulado** (18 372 updates): ≤ 1000: 3456 | ≤ 1300: 6682 | ≤ 1600: 2790 | > 1600:
  5258 | < 700: 186.
  - El **44 %** de los frames está por encima de 1,3 s y el **29 %** por encima de 1,6 s.
- **Inserciones:** `0xB: buffer emptied 119 (with the game's 1000 ms: 309)` de 658 huecos.
  - Bucket `<=5000`: **45** = 15 subidas × 3 huecos de ~4,17 s.
  - Cada `StartAction` en el hombro supera el umbral de 2500 ms: **vaciado**, y la mascota salta al siguiente
    punto.

### 2.4 Cómo interactúa el retardo adaptativo con subir y bajar [V log + E]

1. **Subida:** 3 huecos seguidos de ~4,17 s. El objetivo pasa a 1800 (tope) y `D` sube a 0,5 ms/ms.
   - Cada muestra llega con hueco > 2500 ms, y el búfer se vacía cada vez: tramo sintético desde `now − D`.
   - La mascota «se desliza» 1,8 s detrás y a +1,58 m, sin seguir al personaje entre muestras.
2. **Bajada:** el `StartAction` de bajar trae `+0x8F` todavía a la altura del hombro y los siguientes ya en el
   suelo (HC 336,46 → 336,98: dy 1,54 → −0,21).
   - Con `D` = 1800, el anfitrión los muestra 1,8 s tarde: la mascota sigue arriba mientras el personaje se
     aleja.
   - **Simulación** (`sim.py`, sobre las 826 muestras reales y con la ley de `remote_interp_logic.h`): `D`
     tarda ~4 s en volver a ~1,1 s, porque los 3 huecos de 4,17 s siguen siendo «los últimos 3» hasta que
     entran 3 nuevos, y se baja como mucho 0,25 ms/ms.
3. **Fuera del hombro**, la cadencia irregular de la IA (ráfagas de `StartAction` < 100 ms y huecos de 1–2 s)
   mantiene `max(3) + 150` en 1,1–1,4 s. La mascota sigue a su dueño, que va a 200 ms: siempre ~1 s detrás.

## 3. NPC: la interpolación remota sí funcionó [V, GL 09:50:52]

- `RemoteInterp ON (1, guest)`, sin ninguna línea `ERR RemoteInterp`.
- **Updates:** 1 559 257 llamadas, 0 sin resolver, **1 075 598 sustituidas**; `SEVERAL threads` (5 hilos de
  trabajo + el principal).
- **`0xB`:**
  - 89 126 huecos registrados, `<=1500` 63 605 (71 %);
  - **vaciados 13 201 frente a 80 438 con el umbral de 1000 ms** (14,8 % frente a 90 %).
- **Retardo por frame:**

  | Retardo | ≤ 200 | ≤ 1300 | ≤ 1600 | > 1600 |
  |---|---|---|---|---|
  | Frames | 450 964 (29 %; primer contacto y quietos) | 624 749 (41 %) | 324 695 (21 %) | 81 704 (5 %) |

- Los «largest delays» de 1800 son NPC con huecos de 2–4,6 s; el caso habitual son `gaps 1133/…` de ~1,1 s.
- **Coincide con lo observado en vivo** (ya caminan bien). El precio es la latencia de ~1,3 s, ya prevista
  en `p8-impl-interp-remota.md` §6.1.

## 4. Arreglos propuestos

### 4.1 (2) Hijos ajenos con byte 1: no anunciarlos en la misma tanda que su personaje principal (anfitrión, D-53)

En `child_hold.cpp` (el desvío de `NotifyAppear 0x2B000C0` ya existe), un caso nuevo con clave propia, por
ejemplo `[session2] child_settle_ms=1500` (0 = como hoy). Si:
- una clave `0xB…` con dueño usuario (tipo 4/5/6; **cualquier byte de restauración**) va a una sesión que no es
  la de su dueño;
- y el **personaje principal de ese dueño** va en la misma llamada, o se anunció a esa sesión hace menos de
  `child_settle_ms`;

entonces se reparte como D-53:
- el resto, en el acto;
- el hijo, anotado `(clave, observador)`;
- `observer_hold_acks_begin(clave)`, para que sus 2697/2991/2404 esperen.

Se **libera por tiempo**: re-anuncio nativo `NotifyAppear([observador],[clave])` en un hilo nativo, al vencer
`child_settle_ms`. El 2005 nuevo se re-serializa con byte 1. Limpieza: la de D-53 (época, salida, despawn, fuera
de alcance).

**Justificación del plazo [V]:** funciona a +89 ms (cliente del anfitrión), a +414 ms (cliente del invitado) y
a +3 s (prueba C del caballo); falla a +0 ms en 7 de 7 sesiones. 1500 ms deja margen para un invitado cargando
por el túnel.

**Riesgos:**
- la mascota aparece ~1,5 s después que el anfitrión;
- si el cliente sigue sin crearla (otra condición de `0x8AF580`), el fallo es igual que hoy y visible en GL;
- `observer_hold_acks` derrama con 32 paquetes (unos 1,3 paquetes/s de esta clave: no llega).

**Red de seguridad opcional [E]:** el invitado ya registra el error con `0x9010…_B01…` (`game_errors`). Puede
mandar por el túnel «re-anuncia K» y el anfitrión llama a `NotifyAppear([sesión 2],[K])` en su hilo nativo.

Lo descartado en `p8-actores-anfitrion.md` §4.3 sigue descartado: reescribir dueños o controladores.

### 4.2 (1a) Retardo distinto para hijos de usuario (`remote_interp`, los dos lados)

En la lógica pura (`remote_interp_logic.h`), para actores con dueño. En el hook ya se tiene `actor =
[mc+0x68]`; **`[actor+0xA0]` ≠ 0** es el dueño en el cliente [V: `0x8BDA00–0x8BDA10` y `0x8BDBF7–0x8BDC07` comparan
`[[actor+0xA0]+0x60]` con el dueño del registro]:
- **tope 900 ms** en vez de 1800 (clave nueva, por ejemplo `remote_interp_child_cap=900`);
- **no registrar huecos de muestras con padre**. Se puede hacer sin leer el Path: no contar un hueco > 2500 ms
  si la muestra anterior y la nueva tienen la misma posición ± 2 m en horizontal y Δy > 1 m respecto a lo que
  se muestra. Lo más limpio es la opción 4.3, que elimina esos huecos.

**Simulación [E]** (`sim2.py`, las muestras reales de HC; «parado» = el instante mostrado ya pasó la última
muestra mientras la mascota se mueve > 0,3 m):

| Política | `D` p50 / p90 | Parado en movimiento |
|---|---|---|
| hoy (`max3 + 150`, tope 1800) | 1330 / 1800 ms | 4,8 % |
| tope 900 y huecos del hombro ignorados | **900 / 900 ms** | 7,4 % |
| mediana de 5 + 150, tope 900 | 713 / 900 ms | 9,6 % |
| juego (200) | 200 | 22,3 % |

**Recomendación:** tope 900 con `max3`. Quita 0,4–0,9 s de retraso a cambio de ~2,6 puntos más de micro-paradas.

**Riesgo:** caballo sin jinete y mercenarios también tienen dueño y pasan a 900. Con F2 (`horse_follow`) el
caballo montado del anfitrión ya baja a 200.

### 4.3 (1b) Seguir al padre mientras está en el hombro: `attach_follow`, generalizando `horse_follow`

Estado por mascota `P`: «enganchada a `K`», con el Path local (Ack `+0xD7`: pos 12 B, quat 16 B):
- se activa con un `StartAction` de `P` cuyo padre (`Req +0x116` / `Ack +0x117`) es la clave `0xA…` `K`;
- se apaga con un `StartAction` o Move de `P` con padre −1, o tras 15 s sin nada.

Por cada Move de `K`, a 1:3 (~20 Hz), se fabrica un Move de `P` con:
- pos = `K.pos + R(K.quat)·local`;
- quat = `K.quat ⊗ local_q`;
- la `ts` de `K`;
- `+0x8C` = −1;
- el resto, la receta de `horse_follow` §4.1.

**Dónde:**
- **Mascota del anfitrión → invitado:** en `take_frame` (sesión 2), igual que `horse_follow`, con 2991/2697 que
  ya pasan por ahí. Solo sirve una vez arreglado 4.1.
- **Mascota del invitado → anfitrión** (el síntoma 1): la sesión 1 es nativa (`horse_follow` §4.3). La vía es una
  `MoveActorReq` 2556 sintética de `P` al encolar cada Req del invitado en `server.cpp` (el mismo punto de la
  sonda de `horse_follow`). El servidor la valida (dueño = usuario 2) y la difunde al anfitrión de forma nativa
  **[E]**.

**Efecto:** durante los 8–12 s en el hombro, el búfer de la mascota recibe ~20 Hz:
- `D` baja a 200 (huecos < 100 ms);
- no hay vaciados;
- al bajarse, `D` arranca bajo.

Combinado con 4.2, el «quedarse arriba» pasa de ~1,8 s a ≤ 0,2–0,9 s.

**Riesgos:**
- cambia la posición de `P` en el servidor durante el enganche (es la del hombro: inocua **[E]**);
- +~2 KB/s solo durante el enganche (15 subidas en 15 min);
- otros enganches por acción (presa agarrada, `interacciones-mundo.md`) tienen otro padre: hay que filtrar
  **solo `0xA…` y solo hijos de usuario** (`[actor+0xA0]` ≠ 0);
- si el Path local no fuera el del hombro (por verificar: **[H]** `Ack+0xD7` = pos local), la mascota iría
  desplazada. **Validar primero con `probe`:** `|pos_sintética − pos real del siguiente StartAction| < 0,2 m`.

**Alternativa más barata y parcial:** solo 4.2. El retardo baja, pero en el hombro la mascota sigue saltando
cada 4,17 s mientras el dueño anda (7 de 15 subidas).

## 5. Qué medir en la próxima sesión

1. **4.1:** en GL, **ninguna** línea `Actor 생성에 실패 … B0100002`. En HL, `child settle: held 0xB0100002 …
   released after 1500 ms`. En GC, un 2005 propio `[U][B0100002]` ≥ 1,5 s después del de `A0100001`, y
   2697/2991 de esa clave detrás.
2. **4.2:** `largest delays: 0xB0100334 ≤ 900` y el histograma de HL sin > 1300.
3. **4.3 en `probe`:** el error de la posición sintética frente a la del siguiente `StartAction` en el hombro;
   después, en `1`, los `<=5000` de HL ≈ 0.
4. **Visual:** la mascota del invitado sube al hombro y el invitado corre 10 s. En el anfitrión debe ir pegada;
   al bajarse, debe caer a sus pies en < 1 s.
