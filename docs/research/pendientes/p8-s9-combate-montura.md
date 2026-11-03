# P8 sesión 9: combate, mascota del anfitrión, escalada y montar/desmontar

10 de octubre de 2026. Build **1.0.0.2976**, base `0x140000000`, direcciones en RVA. Solo lectura: logs y capturas
de la sesión 9. No se tocó `src/`, no se hizo commit y no se lanzó el juego.

- **[V]** = comprobado, con la evidencia al lado (captura, log o código ya verificado).
- **[E]** = deducción fuerte que falta medir en vivo.
- **[H]** = hipótesis.

## Fuentes y reloj

| Id | Fichero | Qué |
|---|---|---|
| GL | `backups\p8-sesion9-shadow\cdmp_packets_20261010-151331\cdmp.log` | invitado, prueba 1 (15:13–15:28) |
| G1 | `…\151331\cdmp_packets_20261010-151331.bin` | invitado, prueba 1 |
| G2 | `…\153446\cdmp_packets_20261010-153446.bin` | invitado, prueba 2 (**sin `cdmp.log`**: no se copió) |
| HL1 / HL2 | `backups\p8-sesion9-host\cdmp_fase1.log` / `cdmp_fase2.log` | anfitrión (16:10–16:29 / 16:30–16:43) |
| H1 / H2 | `…\cdmp_packets_20261010-161029.bin` / `-163017.bin` | anfitrión |

- **Reloj [V]:** la `t` de la captura es el QPC en µs, el mismo que el `qpc` de los logs. Invitado: pared =
  QPC + 15:05:58,374; anfitrión: pared = QPC + 15:59:26,287. La pared del anfitrión va **1 h + ~1,4 s** por delante
  (el `CatchActionResultAck` de G1 778,646 coincide con el error `Gimmick Catch` de GL 15:18:57,032; el mismo paquete
  llega a H1 en 1172,098).
- **Claves:**
  - fase 1: anfitrión `0xA0100001`, su mascota `0xB0100002`; invitado `0xA0100377` (usuario `0x90100376`), su
    mascota `0xB0100378`;
  - fase 2: invitado `0xA0100423`, su mascota `0xB0100424`, su caballo `0xB0100425`; caballo del anfitrión
    `0xB0100003`.
- **Scripts** (scratchpad de la sesión, no se conservan): `ext.py` (extrae del `.bin` los opcodes de interés),
  `a2`–`a8.py` (combate, cadencias, agarres, padres, 2005), `a7.py` (error de posición en el golpe), `sim.py`
  (políticas de retardo).

---

## 0. Resumen

1. **Combate, «lag» y teletransportes [V].** Los enemigos los controla el cliente del anfitrión. En combate
   mandan unas 1,5 muestras/s (Move + StartAction; huecos p50 500–580 ms, p90 ~1 s, máx. 2–2,4 s) y llegan al
   invitado 1:1 (0 perdidas en 10 enemigos).
   - `remote_interp` les pone `D = max(3 huecos) + 150`: en el minuto del combate el **77 %** de los frames de `0xB`
     está por encima de 1000 ms y el 7 % por encima de 1600. El enemigo `0xB0101A87` llega al tope de 1800 ms.
   - **La acción se aplica al recibirla, la posición sale del búfer con `D` de retraso** (`movimiento-remoto-cliente.md`
     §3). La reacción a un golpe del invitado llega en ~90 ms (golpe → `AttackTargetAck` 57 ms, → `StartActionAck`
     del enemigo 32 ms más). Sin embargo, su desplazamiento se ve ~1,2 s después: la animación va por delante del
     cuerpo, que luego se desliza 2–5 m (172 tramos de > 2 m en 90 s).
   - En el golpe, el invitado ve al enemigo donde estaba hace **~1,27 s** (p50; error p50 0,44 m, p90 1,77 m,
     máximo 4,45 m).
   - **Bajar el tope no arregla nada sin más muestras:** simulado, `cap 700` deja al enemigo parado el 50 % del
     tiempo que se mueve, frente al 34 % de hoy.
2. **Remates y agarres [V].** Son frame events de tipo *catch*. El servidor responde `CatchActionResultAck` 2156
   (ToNear): 15/15 llegan a los dos clientes.
   - En un jugador, el **controlador de la víctima** manda la `StartActionReq` de la víctima con padre = quien
     agarra.
   - Aquí la víctima la controla el cliente del anfitrión y **nunca** lo hace: 0 `StartActionReq` de enemigos con
     padre = invitado. Solo manda su reacción de IA con padre −1.
   - **No es la bandera `0x2D3`** (solo `0x837487D4`/`0x35DD392F` la llevan, de secuenciador). **Sí necesita que el
     atacante controle a la víctima** (o que su controlador ejecute el enganche).
3. **Mascota del anfitrión [V].** `child_settle` funcionó: retenida y soltada a los 1500 ms (HL1 16:15:16,322 →
   16:15:17,823; G1 recibe el 2005 propio +1,493 s después del personaje).
   - Aun así, el invitado falla la creación **18 ms después** (GL 15:15:16,465, contador `owned actor creation
     failed 1`). Lo mismo en la fase 2 (+1,497 s; sin log).
   - La mascota del invitado se crea en el anfitrión a +2,77 y +4,31 s: **1,5 s no basta; ≥ 3 s sí.**
4. **Escalada y salientes [V].** `PointClimb::Enter` del invitado forzado 7/7 en el anfitrión (`y` sube de 618,3
   a 624,4 sin saltos); WallUp con `D+0xEE0` 1→2 y `up` válido; 2851→2893 reenviados 14/14 y 4/4.
5. **Montar.**
   - **El que observa nunca recibe el vínculo [V]** (`link 0x0` en todas las sondas). **`RideOn` con `Enter` 0
     [V]:** congelado en el suelo 2,33 s durante la animación de montar; después, `method 0` en la silla.
   - **«Enterrarse» [E]:** la animación de montar está anclada al asiento, pero el transform se queda en el suelo.
     No lo causan los Move con padre: llevan la posición de mundo de la silla (617,72) y se aplican bien.
   - **`horse_follow` funcionó (anfitrión → invitado) [V]:** 679 sintéticos, 0 rechazados (720 Acks del caballo en
     G2 = `+0x94` del jinete). **En el otro sentido no hay nada [V]:** 575 Req del jinete frente a 19 del caballo.
6. **Desmontar [V]:** el jinete remoto entra en `RideOff` (método 7) con `Enter` 0 y **se queda bloqueado para
   siempre** (83,6 s, hasta que el invitado se fue).
   - `update NO` todo el tiempo y las capas de acción fijas en `c9356ceb/7b6aff57`, aunque llegaron 53
     `StartActionAck` nuevas y 675 Move en el suelo.
   - Se queda en la silla (618,09 frente a 616,5; divergencia hasta 6,86 m): las acciones siguientes **no se
     aplican** (cierra la duda de la sesión 7).

**Arreglos, por prioridad (§6):**
- D1: quitar `RideOn`/`RideOff` de los `StartActionAck` de jugadores remotos en el cliente que observa. Si no, D2,
  recargar al jinete.
- P1: `child_settle_ms` ≥ 3000 más un reintento.
- C1: Move sintéticos de enemigos en combate desde la memoria del cliente del anfitrión (como `horse_follow`).
- C2: investigar el traspaso de control de la víctima (2168).
- M1: caballo del invitado → anfitrión con una Req 2556 sintética.

---

## 1. Combate

### 1.1 Qué llega y con qué cadencia [V]

Ventana G1 1100–1170 s (combate del invitado; H1 = G1 + 393,452 s), Move + StartAction con `ts` distinta:

| Enemigo | Rx invitado (Move/Start) | Huecos p50 / p90 / máx. (ms) | Tx anfitrión | Perdidas |
|---|---|---|---|---|
| `B0101A7E` | 34 / 57 | 581 / 1013 / 2019 | 34 / 57 | 0 |
| `B0101A7F` | 40 / 86 | 504 / 1015 / 2017 | 40 / 86 | 0 |
| `B0101A62` | 35 / 91 | 565 / 1008 / 2086 | 35 / 91 | 0 |
| `B0101A87` | 7 / 42 | 530 / 1595 / 16056 | 7 / 42 | 0 |
| `B0101A2D` | 14 / 35 | 584 / 2385 / 2410 | 14 / 35 | 0 |
| `B010190C` | 38 / 47 | 998 / 1158 / 3007 | 38 / 47 | 0 |

- **M6 respondido:** en combate la IA manda más que caminando (p50 ~550 frente a ~1100 ms), sobre todo
  `StartAction`, pero sigue siendo ~1,5–2 Hz.
- **`remote_interp` en el combate** (GL, diferencia de histogramas 15:24:33 → 15:25:33): ≤ 1000 ms 23 %, ≤ 1300
  52 %, ≤ 1600 18 %, > 1600 7 %. En `largest delays` aparecen `0xB0101A87 1800 ms (gaps 1001/770/2793)` y
  `0xB0101A88 1800`.
  - Vaciados de `0xB` en ese minuto: 706, frente a 5008 con el umbral del juego.
  - `owned actors (cap 900 ms): controllers 0` en el invitado. Es coherente: la mascota del anfitrión no existe.
- **Latencia de la acción frente a la posición:**
  - golpe del invitado (`DoFrameEventReq`) → `AttackTargetAck`: p50 57 ms, p90 74 ms;
  - `AttackTargetAck` → primer `StartActionAck` del objetivo (reacción, generada por el cliente del anfitrión en
    ~9 ms): p50 32 ms.
  - Las acciones se aplican al recibirse (`0x48AD40` → `0x863530`). La posición entra en el búfer y se muestra en
    `now − D`. **Desfase animación/cuerpo = `D` ≈ 1,1–1,8 s [V código + log].**
- **Error en el golpe** (`a7.py`, 115 objetivos `0xB` en los `DoFrameEventReq` del invitado, comparados con las
  muestras de H1 con la misma `ts`):
  - error p50 0,44 m, p90 1,77 m, máximo 4,45 m;
  - la posición que vio el invitado es la del enemigo **1275 ms antes** (p50; p10 776 ms; 98 casos a < 0,5 m de la
    trayectoria real).
- **«Teletransportes raros» [V/E]:**
  - 172 tramos de > 2 m entre muestras seguidas (2–5 m en 0,4–1,2 s: embestidas y esquivas que el anfitrión hizo en
    ~0,3 s). En el invitado se reparten linealmente a lo largo del hueco, 1,2 s tarde, mientras la animación ya
    acabó.
  - Huecos > 2500 ms (`B0101A87` 16 s, `B0101A88` 5,5 s) vacían el búfer: salto.
  - Al cambiar `D`, el actor se reproduce a 0,5× o a 1,25×.
- Daño y aggro funcionan: 110 `AttackTargetAck` del invitado sobre `0xB`, 11 de enemigos sobre el invitado y 5
  muertes por el invitado. Es lo que dice el usuario.

### 1.2 Remates y agarres [V]

Secuencia (G1 1115,4–1116,8, `B0101A7E`):
1. El invitado manda `StartActionReq` `0xEE2A2E46` (objetivo el enemigo, `0x2D3 = 0`) y `DoFrameEventReq`
   (accessor `…462e2aee… idx 3`).
2. `CatchActionResultAck` (`+0` `A0100377`, `+4` `B0101A7E`, `"B_EnemyCatch_00"`), más `AttackTargetAck`
   `0x7DC30C07`, a los dos clientes.
3. El cliente del anfitrión (H1 1509,27) manda `EchoActionSessionIDReq` + `CurrentTargetChangeReq` + la
   `StartActionReq` `0x5BD8B2C2` del enemigo con **padre −1**. Después siguen `0xAA935CF3`, `0x9B68944F` y
   `0x2E6FEA39`, todas con padre −1.
4. El invitado también manda `EchoActionSessionIDReq` del enemigo (10 por enemigo): su cliente procesa el golpe
   como si lo controlara **[H]**. Sus `StartActionReq` solo son de su personaje y de su mascota.

- **Contraste [V]:** `interacciones-mundo.md` §2.2 (la perdiz). En un jugador, el cliente que controla a la víctima
  manda su `StartActionReq` **con padre = quien agarra** y ningún Move.
  - En H1/H2: 0 `StartActionReq` de `0xB` con padre `0xA0100377`/`0xA0100423`.
  - Las únicas con padre `0xA0100001` son de la mascota del anfitrión (hombro) y de un objeto (`0x0FE3CE6C`,
    `0x5953D471`).
- **Paso de interacción:** solo el primer agarre usó `InteractionDoStep` (id `0x35`). Los otros 14 son solo frame
  event. Los Ack de interacción son ToTarget: H1 no recibe ninguno del invitado.
- **Conclusión:** la animación emparejada de la víctima depende de su controlador. El del anfitrión no la ejecuta
  para un agarrador remoto. En el invitado, la víctima (modo 2) sigue las muestras sin padre del anfitrión, 1,2 s
  tarde, y no se engancha a la mano. **[E]**
- **Pista [H]:** H2 trae 18 × `OverriedOwnershipStateTypeyAck` 2168 (`u32 actor, u32 actor, u8 4`) en el
  instante en que se va el invitado (2636,4). Parece el aviso nativo de cambio de controlador del MMO y es el
  candidato para dar al invitado el control de su víctima o de sus enemigos.

---

## 2. Mascota del anfitrión [V]

| Hecho | Evidencia |
|---|---|
| Retenida y soltada a 1500 ms | HL1: `child settle: held 0xB0100002 … goes in the same NotifyAppear` (16:15:16,322), `released … after 1500 ms, settled` (16:15:17,823). HL2: lo mismo a las 16:36:11,643 → 16:36:13,143 |
| Llega sola al invitado | G1: 2005 `[A0100001]` a 556,580; `[U90100000][B0100002]` a **558,073** (+1,493 s). G2: 1811,932 → 1813,429 (+1,497 s) |
| Y falla igual | GL 15:15:16,465 = G1 558,091: `Actor 생성에 실패했습니다` `0x90100000B0100002` desde `exe+0x8BDF7C`. Minuto: `owned actor creation failed 1` |
| Con más margen se crea | H1: mascota del invitado `B0100378` a +4,311 s de `A0100377`; H2: `B0100424` a +2,773 s. Ningún error en HL. Sesión 8: +3,1 s |

- **Causa [E fuerte]:** la c5 de `c1-actor-rechazado.md` §1.4, `[pj+0x5E] & 0x10`. El personaje del anfitrión
  todavía carga en el invitado 1,5 s después. El formato `[U][hijo]` es el mismo en los casos buenos y en los
  malos, y solo cambia el tiempo.
- Nadie la reanuncia después: hay un solo intento.
- **«Con lag»:**
  - En el anfitrión, la mascota del invitado ya va con tope 900 (`largest delays: 0xB0100378 (owned) 900 ms`,
    `0xB0100424 (owned) 587–900`).
  - En el invitado, su propia mascota es local (modo 1, manda 871 `ChangeAIStateReq`): `remote_interp` no la toca.
    Si el «lag» es de la propia mascota, la causa es otra **[H]** y hay que precisarlo con el usuario.
- **`attach_follow` (probe) valida la hipótesis del Path local en vivo [V]:**
  - `parent x local` ≤ 50 mm en 20/24 y en 28/33;
  - `previous local applied later` ≤ 200 mm en 10/14 (fase 1) y en 11/11 (fase 2, sentido anfitrión).
  - Se puede pasar a `1` cuando exista la mascota.

## 3. Escalada y subidas (confirmación) [V]

- **HL1, `RemoteMotion summary`:** `Enter calls 7, original ok 0, forced 7 (last 0xA0100377)`.
  - Episodio #3: tramos `method 18 PointClimb enter 1` con la `y` 618,30 → 619,31 → 621,00 → 622,56 → 623,70 →
    624,36, sin saltos.
  - Después, `method 5 WallUp enter 1 | wall D+0xEE0 1→2 … up (-10701.79, 626.36, -3907.59)`, subiendo hasta 626,37.
- **GL** (el anfitrión saltando un obstáculo), episodio #1: `WallUp enter 1`, `D+0xEE0 1→2`, `up` correcto.
- **Paquetes:**
  - G1 C>S 2851 = 14 → H1 S>C 2893 = 14;
  - H1 C>S 2851 = 4 → G1 S>C 2893 = 4;
  - G2 2851 = 4 → H2 2893 = 4.
  - No es uno por frame, como suponía `p8-impl-escalada.md` §4.1, pero basta.
- `wallup_send`: `game resets 0` en los tres logs.

## 4. Montar y desmontar

### 4.1 El invitado monta, visto por el anfitrión (HL2, sonda #2, t0 ≈ 16:41:42) [V]

```
[-574..-30]   method 1 enter 1   act 26107d3c/ce35ca5d  y 616.48..616.51
[+0..+2329]   method 6 RideOn enter 0 | act 91605616/26107d3c | link 0x0 | moved 0.00 m, y 616.51   <- congelado en el suelo
[+2360..+3296] method 0 enter 1  | act f79c7424/375d7006 | y 617.96..618.04                          <- ya en la silla (Move con padre)
[+3326..]     method 0 enter 1   | act f79c7424/2314ebb4 | moved 4.83 m
```

- Mientras cabalga, el jinete remoto está en **método 0**, no en 10 (Vehicle).
- El caballo `0xB0100425` manda **19** Req propias frente a 575 del jinete (HorseFollow, `this direction … NOT
  followed`). `remote_interp` le aplica 700–900 ms (owned).
  - El jinete va a 200 ms y a ~36 Hz; el caballo, a ~1 Hz y 0,7–0,9 s detrás: «jinete delante, caballo a saltos».

### 4.2 El anfitrión monta, visto por el invitado (G2; sin log del invitado)

- G2 2176,567: `StartActionAck A0100001 0x872A6750/26107D3C`, objetivo el caballo, `y` 616,28.
- **2,09 s sin ninguna muestra del jinete** (la animación de montar).
- 2178,656: `0xF79C7424/0x375D7006` con **padre = `B0100003`** e `y` 617,71. A continuación, Move con padre a ~36 Hz:
  `+0x04` es la posición de mundo de la silla (617,72) y `+0x94` la del caballo (616,14).
- **`horse_follow` [V]:** HL2 `made 679 (push refused 0, build failed 0)`, `rider Ack cadence <=40 615`. En G2, 720
  `MoveActorAck` del caballo idénticos al `+0x94` del jinete con la misma `ts`. Por eso el anfitrión montado se ve
  bien.
- **«Se entierra y luego aparece en la silla» [E]:** es la misma ruta que en §4.1.
  - El método `RideOn` (6) arranca con la acción de montar. Su `Enter` (`0x681C80`) exige el vínculo (`[[V+0x18]+0x10]`),
    que no existe en el que observa, y devuelve 0.
  - Con `+0x44 = 0` no se interpola ni se hace `SetPosition`: el transform se queda en el suelo.
  - La animación, pensada para un transform colocado en el asiento por `Enter` (`vf40 0xA453F0`, `0x877F90`),
    pone la malla por debajo **[H: que esté anclada al asiento]**.
  - Al llegar la primera acción con padre, el método pasa a 0 y el actor sube 1,4 m en ~200 ms.
  - **No lo causan** los Move del jinete con padre: se aplican como posición de mundo y dejan al jinete en la silla
    (sonda: `y` 617,96–618,04).

### 4.3 Desmontar: el bloqueo definitivo [V]

HL2, sonda #3 (el invitado desmonta, t0 ≈ 16:41:52):

```
[-242..-30]  method 4 enter 1           | act 667e3b35/7b6aff57 | y 618.06..618.09
[+0..+5018]  method 7 RideOff enter 0   | act c9356ceb/7b6aff57 | moved 0.00 m, y 618.09
```

- **Divergencia #1** (51 líneas en el resumen, 81 en total; `END … after 83601 ms`):
  - `memory (-10677.80, 618.09, -3853.81)` fija, frente al canal en el suelo (616,5), hasta 6,86 m;
  - `method 7 RideOff enter 0 | act c9356ceb/7b6aff57` en todas;
  - `update NO for 2540 … 84601 ms (shown ts 672438)`, es decir, el update del modo 2 no corre;
  - el búfer `mc+0x3D0` sigue recibiendo (count 30 → 2 → 13: los set funcionan).
- **H2 en esos 70 s:** 675 `MoveActorAck` del invitado con padre −1 en el suelo y 53 `StartActionAck` con acciones
  nuevas (`E21887A3`, `841A7C99`, `25DC7FB9`, `B5713BEA`…). **Ninguna cambia las capas** que lee la sonda
  (`CC+0x88/+0x90`).
  - Mientras el método 7 tenga `Enter` 0, las acciones nuevas no entran y el método no cambia: un bloqueo que no
    se resuelve solo.
  - Concuerda con `main`: forzar `RideOff::Enter` también dejaba la copia atascada (1c434b2), y lo que funcionó fue
    recargarla.
- **El anfitrión desmonta, visto por el invitado (G2 2197,321):** `0x412C2C2B/0x7B6AFF57` con padre caballo; los
  Move con padre −1 bajan de 617,44 a 616,03 en 0,8 s.
  - Es la misma familia (`A1 = 0x7B6AFF57`); por simetría, **[E]** el mismo bloqueo en `RideOff`.
  - Falta el `cdmp.log` del invitado de la prueba 2 para verlo con la sonda.

### 4.4 Qué falta para desmontar
El cliente que observa no puede pasar por `RideOn`/`RideOff` sin el vínculo, y ningún paquete se lo da:
- 2663/2916 son ToTarget;
- `ChangeRideSeatAck` es un stub;
- `AttachLinkVehicleAck` 2230 no aparece en ninguna captura.

O se evita que entre en esos métodos (D1), o se le recarga (D2), o se le da el vínculo (D3, experimental).

## 5. Hallazgo lateral: Move de un jugador con padre «gimmick» → Ack a cero [V]

Cada `MoveActorReq` de un personaje con padre `0xB` que **no** es su caballo (objetos de escalada o de salto:
`B010136A`, `B01007D4`, `B010141B`…) llega al otro cliente como un `MoveActorAck` **a cero**: posición (0,0,0),
quat identidad, padre −1, `ts` 0. Las cuentas son 1:1:
- invitado → anfitrión: 64/64;
- anfitrión → invitado: 30/30 y 45/45.

Con el caballo llega intacto (679/679).

El búfer lo descarta (`ts ≤` última), pero `0x48BC60` escribe antes `TS+0x534/+0x540` («última posición de red»)
**[E]**. Hoy no se ve ningún síntoma (las subidas pequeñas ya funcionan). Hay que tenerlo en cuenta si aparece un
salto a (0,0,0) o un fallo al subirse a objetos. El origen está en el escritor del Ack del servidor **[H]**.

---

## 6. Arreglos propuestos y riesgos

### Combate
- **C1. `combat_follow` (anfitrión, recomendado) [E].**
  - Un hilo del mod, a 15–20 Hz, lee en el **cliente del anfitrión** la posición y el quat (TransformSync `+0xB4`,
    sector `+0xC0`, quat `+0xA4`; la misma lectura con `fast_read` que `dismount_probe`).
  - Lo hace para los enemigos en combate con el usuario 2: los que tienen un `StartActionAck` o un
    `CurrentTargetChange` hacia su personaje, o un `AttackTargetAck` suyo en los últimos N s.
  - Empuja 2697 sintéticos (la receta de `horse_follow`, `ts` = reloj del cliente) con `server_push_extra`.
  - **Efecto:** `D` baja sola a 200–300 y la animación y el cuerpo quedan casi en fase.
  - **Riesgos:**
    - la `ts` tiene que ser del mismo reloj que los Move reales (si no, se descartan o se reordenan);
    - leer memoria del juego desde otro hilo (SEH; como mucho, un frame a medias);
    - ~25 KB/s por cada 5 enemigos;
    - solo en el sentido anfitrión → invitado (los enemigos los controla el anfitrión).
- **C1-bis (barato, parcial).** Tope de `D` por clase «en combate» de 700–900 ms con extrapolación (F1-bis,
  `Path+0x28`). La simulación con las muestras reales empeora las paradas (34 % → 41–50 %). **No se recomienda
  solo.**
- **C2. Remates [H, investigar].**
  - Opción (a): traspasar el control de la víctima al que agarra mientras dure el agarre, y quizá el de los enemigos
    que pelean con el invitado (es el modelo del MMO: cada NPC lo controla el cliente que pelea con él). Primero hay
    que leer el manejador de `OverriedOwnershipStateTypeyAck` 2168 (`0xBB1640`) y su escritor en el servidor.
  - Opción (b), solo visual y en el invitado: entre `CatchActionResultAck` (agarrador = local) y
    `ReleaseCatchResultAck`/muerte, ignorar los Move de red de la víctima (bit 0 de `[[CC+0x60]+0x10]`) para que la
    sujete su propio enganche (`0x935DD0`).
    - Riesgos: es una escritura en memoria del juego; el anfitrión sigue viendo a la víctima sin emparejar; hay que
      volver al modo 2 sin salto.

### Mascota
- **P1 [E fuerte].** `child_settle_ms=4000` (los casos buenos van de 2,8 a 4,3 s), más una **red de seguridad**:
  1. el invitado detecta el error `exe+0x8BDF7C` con la clave (`game_errors` ya la guarda);
  2. manda por el túnel «reanuncia K»;
  3. el anfitrión llama a `NotifyAppear([sesión 2],[K])` en su hilo nativo, con espera creciente (2, 4, 8 s) y
     como mucho 3 intentos.
  - El fallo no deja ningún actor a medias (`c1-actor-rechazado.md` §3): el reanuncio no duplica.
  - **Riesgo:** la mascota aparece ~4 s tarde.
  - **Medir antes:** el diagnóstico mínimo de c5 (`0x206B5DA`, `u16 [[r14+8]+0x5E]`) en el invitado, para cerrar
    la causa.
- Después de P1, `attach_follow=1` (la hipótesis ya está validada).

### Montar y desmontar
- **D1. `ride_filter` en el cliente que observa (los dos PCs) [E].**
  - En el `OnRecvPacket` que ya enganchan los dos clientes (`troc_sniffer.cpp`; en el invitado vale también
    `guest_deliver`).
  - Para cada `StartActionAck` de un personaje `0xA01` remoto (`actor+0x90 ≠ 0`, no el local) cuya acción entra en
    `RideOn`/`RideOff`, se sustituyen los hashes A0/A1 por una acción neutra de locomoción (`0x26107D3C`) y se
    conservan posición, padre y `ts`.
    - Montar: `0x91605616` y `0x872A6750`, con objetivo = caballo.
    - Desmontar: A1 = `0x7B6AFF57` con padre = caballo (`0x667E3B35`, `0xC9356CEB`, `0x412C2C2B`).
  - **Efecto:** nunca entra en el método 6/7. Sin bloqueo y sin enterrarse; el cambio de altura lo hacen los Move
    (~200 ms).
  - **Coste:** no se ve la animación de montar ni la de desmontar.
  - **Riesgos:**
    - la lista de hashes no es completa (desmontar en carrera, en combate, la caída): mejor, decidirlo por el método
      que elige la acción, si se encuentra la tabla, y si no, ampliar la lista con la sonda;
    - modificar paquetes en `OnRecvPacket` antes del juego.
  - **Validar primero con `probe`:** contar cuántas veces se aplicaría y ver que la sonda no vuelve a registrar
    `RideOn`/`RideOff enter 0`.
- **D2. Recargar al jinete (respaldo, robusto como en `main`) [E].**
  - El servidor del anfitrión detecta el desmontar: la Req o el Ack del jinete con padre = caballo y después un
    Move con padre −1.
  - Hace `NotifyDisappear` + `NotifyAppear` del personaje **solo hacia el observador**, con el camino de
    `character_hold`.
  - **Riesgos:** un parpadeo de ~1–2 s; 11,5 KB de 2005; `child_settle` y D-53 se vuelven a activar con sus hijos;
    hay que probar en ambos sentidos (el del invitado como observador pasa por la sesión 2; el del anfitrión, por la
    sesión 1).
- **D3. Vínculo real (experimental):** inyectar en el observador `InteractionDoStepSuccessAck`/`RideOffByServerAck`
  (`caballo-coop.md` §5 fase 2). El paso de interacción `0x9B6560` está mutado. Solo si D1/D2 no bastan.
- **M1. Caballo del invitado visto por el anfitrión [E].**
  - Por cada `MoveActorReq` del invitado con padre = su caballo, `server.cpp inject` encola una `MoveActorReq` 2556
    sintética del caballo (`+0x94/+0xA0` del jinete, que coincide con el caballo con error 0,000 m). El servidor la
    valida (dueño = usuario 2) y la reenvía de forma nativa al anfitrión.
  - `attach_follow` ya construye esa Req en modo de medida.
  - **Riesgos:** cambia la posición del caballo en el servidor a ~36 Hz (es la real); duplica tráfico de Req
    mientras se cabalga; falta saber qué hace el servidor con un historial a cero.

## 7. Qué medir en la sesión 10
1. **Copiar el `cdmp.log` del invitado de cada prueba** (el de la prueba 2 falta). Así se ve la sonda en el
   desmontar del anfitrión.
2. **Combate con C1:** en GL, `largest delays` de los enemigos ≤ 300 ms. En `a7.py`, el retraso del golpe debería
   bajar de ~1275 ms a < 300 ms.
3. **Agarre:** con la sonda, el método y la acción de la víctima en el invitado durante un remate. Leer
   `0xBB1640` (2168).
4. **Mascota:** HL `released 0xB0100002 … after 4000 ms` y GL sin `Actor 생성에 실패`. Si falla, el diagnóstico de
   c5.
5. **D1 en `probe`:** contar las sustituciones por hash y repetir la secuencia de montar y desmontar en los dos
   sentidos.
