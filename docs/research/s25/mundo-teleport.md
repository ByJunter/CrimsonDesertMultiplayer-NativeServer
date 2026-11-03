# S25, paso 1: el mundo del invitado con el anfitrión lejos y tras su viaje rápido (build 1.0.0.2976)

Investigación de solo lectura (2026-10-12): sin tocar código, sin compilar, sin abrir el juego. `.xtls` solo leído.
Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`), direcciones en RVA.

Marcas: **[V]** comprobado (log, captura, bytes o desensamblado); **[E]** leído en el código sin prueba en vivo;
**[H]** hipótesis.

Fuentes:
- Anfitrión: `C:\Emulador CD\backups\p8-sesion25-host\cdmp_20261012-063736.log` y
  `cdmp_packets_20261012-063747.bin` (cliente del anfitrión; recibe todo lo del campo, un solo sector).
- Invitado (Shadow): `cdmp_20261012-054004.log` + `cdmp_packets_20261012-054022.bin` (primera conexión) y
  `cdmp_20261012-060436.log` + `cdmp_packets_20261012-060451.bin` (segunda).
- Ini: `docs/research/ini/cdmp.p8-s7-host.ini` (`user2_world_anchor=1`).
- **Relojes [V]:** reloj del anfitrión = reloj del invitado **+ 1 h 00 min 16,4 s**. Medido con el mismo
  `DeleteNPCScheduleStageAck`/`RemoveActorAck` en las dos capturas: t del anfitrión 943,124 = t del invitado 771,750
  (los t de captura cuentan desde 06:37:47 en el anfitrión y desde 05:40:22 en el invitado). En el texto, «t» sin más
  es t de la captura del invitado.
- Scripts (scratchpad de la sesión, `s25tp\`, lectura en streaming de los `.bin`): `rmwin.py` (instantáneas de NPC
  vivos cerca de cada jugador y bajas por motivo), `neargone.py`, `births.py`, `stages.py`, `stagejoin.py`, `r19.py`,
  `rmpos.py`, `bracket.py`, `movers.py`, `owners.py`, `keyev.py`; `reasons.py`/`scan13.py`/`imm13.py`/`vf68.py`
  (capstone sobre el exe con `C:\Emulador CD\build\research\cdpe.py`).

---

## 0. Resumen

1. **El ancla hizo exactamente lo previsto y no la tocó nada [V].** Se puso a los 20 s del 2762 del invitado
   (06:41:51), se verificó a los 5 s y siguió en `{1, 0xA010034E}` hasta el `[clear] before 2001` (07:04:04). Mismo
   campo `0x391CED17A10`, misma generación `0x81020001`, también **a través del viaje rápido del anfitrión**: el
   campo no se reinició ni se cambió, la ranura no se perdió (`lost 0`), ni la ocupó otro (`busy 0`), ni falló el
   TryLock (`lock busy 0`, `not written 0`, `refused 0`, `errors 0`). La segunda conexión (0xA020280A) igual.
2. **Los NPC del invitado no los borró el recolector ni el ancla, sino el sistema de «stages» de horario del
   anfitrión [V].** El viaje rápido del anfitrión es `ReloadStageStartReq` 2283 + `GameLoadingStartReq` 2437. A los
   6 ms del 2437 el servidor **cerró las 36 stages de horario (`ServerNPCScheduleStage`) que quedaban vivas, todas
   del personaje del anfitrión `A0100001`**: 36 × `DeleteNPCScheduleStageAck` 2126 (ToNear) y `RemoveActorAck` 2516
   de **motivo 19** de cada NPC de esas stages, estuvieran donde estuvieran. 19 de ellos estaban a 6–49 m del
   invitado (los aldeanos que nacieron al entrar). Los animales del invitado sobrevivieron. Como el anfitrión viajó
   justo al lado del invitado, su gestor volvió a crear stages allí (76 × 2335 a 20–160 m del invitado) y los NPC
   reaparecieron a los ~6 s. Si hubiera viajado a otro sitio, no habrían vuelto.
3. **Los NPC nuevos no aparecen junto al invitado porque la población humana (horarios, tiendas, escenas) la crea el
   gestor de stages solo para el dueño del campo y solo alrededor de su personaje [V].**
   - Las **676/676** `CreateNPCScheduleStageAck` 2335 de las dos capturas tienen dueño `A0100001`; ninguna
     `A010034E`/`A020280A`.
   - Se crean a 70–240 m del anfitrión (mínimo por ráfaga 70–144 m). Con el anfitrión a 450–1000 m, ninguna cayó a
     menos de 435 m del invitado.
   - Código: `ServerSequencerStageManager` update `0x288B7A0`, con la **puerta de dueño** `0x288B87A`
     (`cmp [field+0x180], eax; jne salir`) y la celda de 64 m calculada con la posición del personaje de ese gestor.
     El temporizador de horarios que dispara el propio usuario 2 (2045) acaba también en el gestor **del anfitrión**,
     por `ServerField::vf48 0x2B00CE0` (el log de `User2Funnels` lo cuenta: `2045x73`).
   - Junto al invitado solo nace lo que pide su propio cliente (pools y caminos 2424/2550/2928): **439 animales y 17
     personas** en 345 s con el anfitrión lejos, frente a **288 personas en 57 s** cuando llegó el anfitrión.
   - El ancla solo alarga la vida de lo que ya existe: es el comportamiento que vio el usuario.
4. **Causas raíz y arreglo.** Todo cuelga del gestor de stages del dueño del campo, que es por personaje y se mueve
   con la posición del anfitrión:
   - (a) **creación** solo junto al anfitrión;
   - (b) **cierre** de las stages que salen de su ventana al alejarse (motivo 30, también junto al invitado);
   - (c) **cierre total en cada carga del anfitrión** (motivo 19).

   El ancla del recolector no puede arreglar nada de esto. Propuesta (§5), en este orden:
   - **medir primero**, con ganchos de solo registro en entradas de función;
   - después, el arreglo de fondo en el gestor de stages. Hay dos diseños:
     - **U:** el gestor propio del usuario 2 evalúa sus stages cuando está lejos del anfitrión, con histéresis;
     - **B:** una segunda ventana del usuario 2 en el gestor del anfitrión.

     Se recomienda U, porque también deja a salvo sus stages en la carga del anfitrión. Ninguno lleva guardas que
     rechacen valores sin medir.
5. **Hallazgos laterales:**
   - **El caballo del invitado** `0xB010034F` (hijo del usuario 2, tipo 6, a 1 m de él) recibió `RemoveActorAck`
     **motivo 9** en la carga del anfitrión y **no volvió** en toda la sesión [V].
   - **La sonda `[minute]` del ancla mide mal cuando el anfitrión va montado:** «host - user 2 now 11425 m» es la
     distancia del invitado al origen, porque la posición del anfitrión se leyó como (0, 0, 0). Por eso sus
     `beyond 150 m from the host` de 06:47–06:50 y 07:07–07:10 no valen [V].
   - **IA lejana (R3):** con el anfitrión a ~950 m, su cliente siguió moviendo 335 actores junto al invitado
     (2697 reenviados; 309 animales y 15 personas) [V]. Amplía el dato de `control-npc-lejos.md` (≤ 640 m).

---

## 1. P1: qué hizo el ancla

Líneas `WorldAnchor` del anfitrión [V]:

| Hora anfitrión | t invitado | Hecho |
|---|---|---|
| 06:41:30.9 | ~52 | `[leave] user 2's 2437 reaches the server: nothing set` (su carga de entrada) |
| 06:41:51.0 | ~72 | `[set] key 0xA010034E field 0x391CED17A10 gen 0x81020001 slot was 00/00000000 … the host's own field: the same` |
| 06:41:56.0 | ~77 | `[verify] slot 01/A010034E ok after 5000 ms` |
| 06:46:36 | 358 | El anfitrión monta su caballo (`InteractionDoStepReq` 2720 `A0100001 → B0100003, 0x190`) y se aleja |
| 06:46–06:52 | 360–700 | Separación real hasta **1012 m** (t = 630–640). Cada `[minute]`: `slot 01/A010034E`, `now: wanted`, `held: field 0x391CED17A10`, `lost 0 busy 0 … lock busy 0 not written 0 refused 0`, «(the host's own)» |
| 06:53:28 | 769,8 | Anfitrión: `ReloadStageStartReq` 2283, destino (−10626, 621, −3813), a ~100 m del invitado |
| 06:53:30 | 771,7 | Anfitrión: `GameLoadingStartReq` 2437 (§2) |
| 06:53:40 | 781,7 | Anfitrión: `LoadingCompleteAck` 2762 |
| 06:53:47 → 07:03:47 | — | Igual que antes: mismo campo y generación, ranura `01/A010034E`, `lost 0`, `busy 0`, «the host's own» |
| 07:04:04.9 | — | `[clear] before 2001` (salida del invitado al título) |
| 07:06:30.3 / 07:06:35.3 | — | Segunda conexión: `[set] key 0xA020280A` en el **mismo** campo y generación, y `[verify]` |
| 07:14:07.5 | — | `[clear] before 2001` |

Respuestas concretas:
- **El campo no se desmontó ni se recreó.** El viaje del anfitrión (2283 + 2437, con `TS+0x568` puesto) vuelve a
  entrar en el mismo `ServerField`: mismo puntero y misma generación antes y después, y «the host's own» en todas
  las líneas [V].
- **El juego no limpió ni sobrescribió la ranura.** El vigilante la leyó cada 5 s en ServerRunThread, con paquetes del
  invitado durante toda la carga del anfitrión, y nunca dio `[lost]` ni `[busy]` [V].
- **No se soltó el ancla:** el 2283/2437 del anfitrión no es una «salida» del usuario 2 (`on_leave` solo mira los
  del usuario 2), así que no se quitó ni hubo que rearmarla [V, código y log].
- **TryLock:** 0 fallos, 0 escrituras rechazadas, 0 errores (625 lecturas a las 07:02 y 1018 a las 07:13) [V].
- **El 2573 con la ranura puesta:** `slot off 0/0, on 0/0`. Nadie mandó `OutOfLevelBoundaryReq` [V].

---

## 2. P2: por qué desaparecieron los NPC del invitado con el viaje rápido del anfitrión

### 2.1 Secuencia en la captura del invitado [V]

| t invitado | Paquetes S>C al invitado | Efecto junto al invitado (< 150 m) |
|---|---|---|
| 770,1 | — | 63 NPC/animales (tipos 3/6) vivos, 360 actores en total |
| **771,750–771,768** | **36 × `DeleteNPCScheduleStageAck` 2126** (todos con dueño `01 00 10 A0` = `A0100001`), y en el mismo milisegundo: **46 × 2516 motivo 19**, 3 × motivo 30, 7 × motivo 9, 1 × motivo 44 y otros de gimmicks | 19 NPC de stage a 6–49 m del invitado (`B01000B4…B0100100`, nacidos en t = 59,2 al entrar, y `B0100926`), + el caballo del invitado `B010034F` (motivo 9) |
| 772,1 | — | 43 NPC/animales cerca (193 actores en total) |
| 773,0 | 73 × motivo 25 (pasada del recolector) | 0 cerca del invitado: mediana a 963 m de él, junto a donde **estaba** el anfitrión |
| 777–811 | 76 × `CreateNPCScheduleStageAck` 2335 (dueño `A0100001`) a 20–160 m del invitado | Nacen 288 NPC tipo 3 junto al invitado en 57 s; a t = 790, 372 cerca |

En la captura del anfitrión, lo mismo: 2283 en t = 941,141, 2437 en 943,118 y las 2126/2516 en 943,124 [V].

El log del invitado lo confirma: el libro del invitado tenía `received ledger: schedules 36` justo antes (05:53:17),
y el anfitrión cerró exactamente 36 [V].

### 2.2 Qué código cierra las stages [V/E]

- `pa::ServerNPCScheduleStage` (vtable `0x5B169B0`), **vf13 = `0x2817E80`** (`[vt+0x68]`, prólogo
  `4C 89 4C 24 20 44 88 44 24 18 55 53 …`, código real en `.rsrc`) [V]:
  - pone la stage en estado 4 (`+0x16C`);
  - recorre sus entradas de NPC (`+0x138`, número `+0x140`, paso 0x168) y, por cada actor vivo
    (`[entrada+0x28]`) que siga ligado a la stage (`.xtls 0x14DD9D1E0`), llama a
    **`ServerActor::Logout(actor, …, motivo = r8b del llamador, …)`** (`0x2068340`);
  - envía el 2126 por **ToNear** (`0x2A53EB0` con `ToNearSendTargetFinder`, el sitio `0x28182C5`), así que llega a
    los dos clientes;
  - pasa a estado 5 [E, decompilado].
- **El motivo 19 viene del llamador:** la carga del dueño cierra todas sus stages de golpe. No encontré el llamador
  con 19: ningún `mov r8b, 0x13` ni `call [reg+0x68]` cercano en `.rsrc`/`.xtls`, así que va por una variable o por
  `.xtls` ofuscado [V la búsqueda, H el sitio]. El gancho de medida A1 (§5.2) lo identifica por la dirección de
  retorno.
- Por qué afecta a NPC lejos del anfitrión: el cierre es **por stage**, no por distancia, y todas las stages de
  horario del campo son del gestor del anfitrión (§3). En un juego de un jugador es correcto: en una carga, todo lo
  del jugador se descarga.
- Lo que **no** fue la causa [V]:
  - el recolector: la pasada de 773,0 retiró cosas junto a la posición vieja del anfitrión;
  - el reinicio del campo o de la ranura (§1);
  - el mod del invitado: su log a esa hora solo tiene diagnósticos.
- **El viaje del anfitrión (2283 seguido de 2437) entra por el mismo manejador del 2437 que cualquier carga**
  (`TransformSync::GameLoadingStart 0x2BC9200` → `GameLoadingStart_Step 0x2BC9470` → `MoveFieldOnceTimer`). Por
  tanto cualquier pantalla de carga del anfitrión hace lo mismo: viaje, reaparición o recarga [E, por `optD-mundo.md`
  «server removes all NPCs with reason 19» y `pendiente-robustez.md` §2.1].

### 2.3 Lateral: el caballo del invitado [V/H]

- `0xB010034F` es el hijo del usuario 2: tipo 6, dueño `0x9010034D`, nacido en t = 48,8.
- Recibió **dos 2516 motivo 9** en t = 771,769, a la vez que los cierres por la carga del anfitrión, y no volvió a
  nacer (ningún 2005 hasta el final de la captura, t = 1410) [V].
- En el anfitrión: `AttachFollow … 0xB010034F detached (parent 0xA010034E)` a las 06:53:35 [V].
- Los otros motivo 9 fueron gimmicks del propio anfitrión (`B010314A/B/C`, `B0102EF1/2`, a 6 m de él).
- Hipótesis: la limpieza de «vehículos y hijos» del usuario que carga alcanza al hijo del usuario 2 porque su
  controlador es el dueño del campo, o por la entrega del hijo a la sesión del anfitrión (`child hold`/`child
  settle`) [H].
- Es otro defecto, aparte de este. Se investiga por su cuenta (§5.6).

---

## 3. P3: por qué no aparecen NPC nuevos junto al invitado con el anfitrión lejos

### 3.1 Lo que nace y dónde [V]

Captura del invitado, actores nuevos (primer 2005) por sitio y tipo:

| Tramo | Separación | Junto al invitado (< 150 m) | Junto al anfitrión | 2335 (stages de horario) |
|---|---|---|---|---|
| t 420–765 (345 s) | 450–1012 m | **tipo 3 (personas): 17**, tipo 6 (animales): 439, tipo 5: 6, tipo 7: 23 | (el resto) | 195, **todas** a ≥ 435 m del invitado y a 70–240 m del anfitrión |
| t 773–830 (57 s) | 2–90 m | **tipo 3: 288**, tipo 6: 193, tipo 7: 189 | — | 76, a 20–160 m del invitado |

- **Dueños de las stages:** en las dos capturas, las 676 `CreateNPCScheduleStageAck` y las 574
  `DeleteNPCScheduleStageAck` son de `A0100001`. Hay 0 del personaje del invitado [V].
- **Dónde nacen las stages:** en cada ráfaga, d(stage, anfitrión) tiene mínimo 70–144 m y mediana 100–240 m. Por
  ejemplo, t = 439: 16 stages, d(anfitrión) mín 93 / mediana 172, d(invitado) mín 506 / mediana 605 [V].
  - La única excepción, t = 777–778 (42 + 6 stages «a 850–1088 m del anfitrión»), es un efecto de medida: el
    invitado aún no tenía la nueva posición del anfitrión; las stages nacieron junto al punto de llegada.
- **El invitado sí pide spawns y el servidor los atiende** (2424/2550/2928/2221; unos 52 × 2550 cada 20 s). Pero
  eso da sobre todo animales y algún viajero de camino. **Las personas de los pueblos (horarios) y las escenas
  (tiendas, facciones) salen de las stages.**

### 3.2 Quién decide crear stages [V/E]

Cadena (en parte de `pendientes/p8-s12-tenderos-servidor.md` §2.1, comprobada aquí con capstone/Ghidra):

- `UpdateStageOnceTimer` 2043 → `.xtls 0x1082F900` → tick `0x287DBB0(mgr)` → **`0x288B7A0(mgr, campo, actor,
  actorPosición, …)`**, llamado en `0x287DE14` con `r8 = [mgr+0x18]` (el personaje del gestor) [V]. Dentro de
  `0x288B7A0`:
  1. **puerta de dueño `0x288B867–0x288B881`** [V bytes]:

     ```
     mov rax,[r13+0xA0] ; mov eax,[rax+0x60]
     cmp dword [r15+0x180], eax ; jne 0x288BDEE
     ```

     Si el personaje del gestor no es del dueño del campo, sale sin evaluar nada. El gestor del usuario 2 nunca pasa
     en el campo del anfitrión;
  2. posición = `TransformSync_GetWorldPosition([[actorPosición+0x68]+0x1A0])` (`sector*1000 + local`). La celda es
     `floor(pos/64)` en X, Y y Z [E, decompilado];
  3. si la celda cambió respecto de `mgr+0x680/+0x684`: **`0x28818F0`** (evalúa y arranca las stages de las celdas
     nuevas; dentro, `0x288A970` crea la stage de horario con `0x28078E0` → 2335) y **`0x287DFE0(mgr, campo,
     &celda)`** (revisa las stages `AutoSpawnOwnerData`, tipo `'\n'`, frente a la celda) [E];
  4. al final, `0x15025C310(mgr+0x148, …)`: el tick del `ServerNPCScheduleSpawnManger` [E].
- `UpdateScheduleStageOnceTimer` 2045 → `.xtls 0x108309B0`: campo del actor del temporizador → **`campo->vf48()`**
  → `0x2822D00([gestor+0x148])` [V decompilado]. En el servidor, `vf48 = 0x2B00CE0` devuelve **siempre el gestor del
  dueño del campo** (`p8-s12-tenderos-servidor.md`).
  - El log del anfitrión lo cuenta: `User2Funnels vf48 0x2B00CE0 … Req opcodes … 2045x73 | callers …
    exe+0x10830A0Fx73`, es decir, 73 ticks de horario **del usuario 2** que se aplicaron al gestor del anfitrión
    (07:14:21) [V].
- Conclusión: en el campo compartido, **toda la población por stages (horarios, tiendas, escenas) es del anfitrión y
  sigue la posición del anfitrión**. El ancla del recolector no toca nada de esto.

### 3.3 Lo que pasa con las stages que el invitado ya tenía al alejarse el anfitrión [V]

Hay dos efectos, los dos medidos en la captura del invitado:

- **Cierre por ventana (motivo 30).** Al moverse el anfitrión, su gestor cierra stages (2126 en ráfagas) y en el
  mismo milisegundo retira sus NPC, casi siempre con **motivo 30** (también 25 o 36 en alguna ráfaga;
  `stagejoin.py`). Junto al invitado: 12 NPC (t = 296), 1 (298), 12 (341), 6 (366), 5 (375), 3 (382). Son 39 antes
  del viaje del anfitrión, de 202 bajas por cierre de stage en toda la captura. No todas cierran por distancia: las
  de la entrada siguieron vivas hasta la carga del anfitrión.
- **Recolector con radio pequeño (motivo 25), con el ancla funcionando:**
  - con el invitado quieto y el anfitrión a 450–1000 m: **0 bajas de motivo 25 a menos de 150 m del invitado** en
    t 420–490 y 515–770. Entre t 100 y 770 hubo solo 3 bajas de motivo 25 a < 30 m del invitado, en t 237–253 y
    con el anfitrión al lado. **El ancla protege** [V];
  - cuando el invitado anduvo unos 60 m (t 492–503), el recolector retiró 57 NPC y animales nacidos al entrar (t
    59), a 63–96 m de su nueva posición. Algunos estaban a 7–58 m de donde había estado quieto, con el anfitrión a
    670–760 m.
  - En la pasada anterior (5 s antes) estaban a 4–63 m del invitado; en la suya, a 38–96 m. Es el corte por **radio de
    spawn** (`[[actor+0x68]+0x20]+0x468`) medido **desde el invitado** (ancla 2), con radios de **~40–65 m** para
    esos aldeanos y animales de granja, no los ~200 m de la fauna [V horquilla, E el campo concreto].
  - La rama de 50 m del dueño (`0x6D09488`) los habría matado mucho antes, con el anfitrión a 700 m, así que no es
    esa.
- El efecto conjunto: lo que rodea al invitado **se va gastando** (de 125 a 63 NPC y animales vivos entre t 400 y
  770) y solo se repone con lo que piden los pools de su cliente. Las personas no se reponen: **la stage que las
  volvería a crear sigue la posición del anfitrión**. En un jugador, volver al pueblo reevalúa sus celdas y las
  stages las recrean.

### 3.4 IA de lo que sobrevive (R3) [V]

En t 540–760 (anfitrión a 850–1000 m), de los 372 actores `0xB` con movimiento cerca del invitado:
- 335 lo recibieron solo por 2697 reenviado, es decir, simulados por el cliente del anfitrión (309 animales, 15
  personas, 6 tipo 5, 5 tipo 7);
- 4 los movió el propio cliente del invitado (2556);
- 33, los dos.

La IA lejana del anfitrión sigue a ~1 km. El dato de `control-npc-lejos.md` llegaba hasta 640 m.

---

## 4. Lo que los números del log del anfitrión no muestran bien [V]

`WorldAnchor [minute]` de 06:47:47–06:50:47 y 07:07:47–07:10:47: «host - user 2 now 11425 / 11376 / 11411 m».
- **11425 m = |posición del invitado|** (−10729, −3926). La posición del anfitrión se leyó como ~(0, 0, 0).
- El anfitrión iba **montado** desde 06:46:36: el 2720 `0x190` sobre `B0100003`, y a las 06:51:47 la medida vuelve a
  ser correcta (952 m, igual que en la captura del invitado).
- `world_anchor.cpp` `position()` usa `game::actor_world_position` del gestor de actores **del cliente**. Para un
  jinete, eso devuelve la posición local respecto del caballo [E].
- Consecuencias:
  - en esos minutos, `d(host)`, `beyond 150 m from the host` y `max with a removal` no valen;
  - `near user 2` sí vale: las 192 bajas de 06:47:47 son las del §3.3, en t 340–420, con el anfitrión de verdad a
    90–140 m de esos NPC;
  - las de 06:49:47 son las 57 del paseo del invitado, con el anfitrión de verdad a 670–760 m.

---

## 5. P4: causas raíz y arreglos propuestos (sin implementar)

### 5.1 Causas raíz

| # | Síntoma | Causa | Dónde |
|---|---|---|---|
| C1 | Los NPC del invitado desaparecen con el viaje rápido del anfitrión | La carga del dueño del campo cierra **todas** las stages de su gestor (motivo 19), también las que están junto al usuario 2 | Cierre: `ServerNPCScheduleStage::vf13 0x2817E80` (motivo del llamador); llamador sin identificar en `.xtls` |
| C2 | No aparecen NPC nuevos junto al invitado | Las stages solo las crea el gestor del dueño (puerta `0x288B87A`), en celdas de 64 m alrededor de **su** personaje (`0x288B7A0` → `0x28818F0`); los ticks de horario del usuario 2 (2045) van al gestor del dueño (`vf48 0x2B00CE0`) | `0x288B7A0`, `0x2B00CE0` |
| C3 | Lo que el invitado tenía se va gastando | Cierre de stages al alejarse el dueño (motivo 30) y radios de 40–65 m de los NPC de stage, sin nada que los reponga junto al invitado (C2) | `0x287DFE0` y el camino de motivo 30 (sin identificar); el recolector funciona bien con el ancla |
| C4 | Desaparece el caballo del invitado | Motivo 9 en la carga del dueño [H] | Sin localizar |

**El ancla de mundo está bien y hay que mantenerla**: sin ella, el recolector se lleva lo que rodea al invitado a
~250 m del anfitrión (`mundo-invitado.md`). Ahora el fallo está una capa más arriba: quién **crea y mantiene** la
población.

### 5.2 Paso 0: medir (solo registro, sin cambiar nada)

Todo con ganchos en **entradas de función** (`0x2817E80`, `0x288B7A0`, `0x28818F0`, `0x287DFE0` empiezan en código
real de `.rsrc`: prólogos comprobados, no son thunks). Lecturas protegidas con SEH, el registro en otro hilo y un
interruptor `probe`. Se puede hacer en un PC: el anfitrión se aleja de `cdmp_peer` y viaja.

- **A1, cierre de stage `0x2817E80`:**
  - registrar el motivo (r8b), la clave de la stage (`[stage+0x30]`), el actor dueño (`[stage+0x50]` [E]), el número de
    NPC vivos y su distancia mínima al anfitrión y al usuario 2;
  - y la **dirección de retorno**, que identifica el llamador del motivo 19 (carga del dueño) y del motivo 30
    (ventana).
  - Responde a: cuántas stages hay junto al usuario 2 cuando el anfitrión carga, y quién las cierra.
- **A2, `0x288B7A0` y `0x28818F0`:** gestor, personaje, celda nueva y vieja (`mgr+0x680/+0x684`), cuántas stages
  arranca cada evaluación. Si el gestor es el del usuario 2, cuántas veces corta la puerta `0x288B87A` y si su
  `mgr+0x6A0 ≥ 2` (estado listo).
- **A3, `0x287DFE0`:** qué stages revisa y cuáles cierra para una celda. Hace falta para saber si una segunda ventana
  (opción B) cerraría las de la primera.
- **A4, la sonda del ancla (corrección de medida, no de comportamiento):** usar la posición del actor **del
  servidor** (`TransformSync_GetWorldPosition` del personaje del servidor, que el ancla ya resuelve en
  ServerRunThread) o descartar la muestra si el actor tiene padre. Así `[minute]` deja de dar 11 km con el anfitrión
  montado.

### 5.3 Opción U (recomendada): el gestor propio del usuario 2 cuando está lejos del anfitrión

Es la idea del «ancla», llevada a la población: el usuario 2 ya tiene en el servidor su propio
`ServerSequencerStageManager` y su `ServerNPCScheduleSpawnManger`. Ahora no hacen nada por la puerta de dueño y por
`vf48`.

- **Dónde:**
  - `create_mid` en `0x288B87A` (el `cmp [r15+0x180], eax`). Si el gestor es el del personaje principal del usuario
    2, el usuario 2 está `Stable` en un campo del anfitrión y la separación supera `D_on`, poner `eax =
    [r15+0x180]`. Su gestor evalúa entonces sus propias celdas;
  - en el gancho de entrada que `user2_funnels` ya tiene en `ServerField::vf48 0x2B00CE0`: con el mismo estado,
    devolver el gestor del usuario 2 **solo** para el 2045 del usuario 2 (llamador `exe+0x10830A0F`). Así su
    `ServerNPCScheduleSpawnManger` también avanza;
  - por debajo de `D_off < D_on` (histéresis), volver al comportamiento nativo y cerrar las stages del gestor del
    usuario 2 con el cierre nativo que identifique A1.
- **Qué arregla:**
  - C2: stages, horarios, tiendas y escenas alrededor del invitado, creados por su gestor (2335 con dueño
    `A010034E`, que en el cliente del invitado va a su propio gestor M, el caso nativo);
  - C1: **la carga del anfitrión solo cierra las stages de su gestor**; las del usuario 2 siguen;
  - C3: la ventana del usuario 2 sigue al usuario 2.
- **Riesgos:**
  1. **Duplicados** donde se tocan las dos ventanas: el mismo aldeano con dos stages [H]. `D_on` tiene que ser mayor
     que la suma de las dos ventanas; lo medido sale **> ~500 m** (stages hasta 240 m del dueño) y A2 dará el valor
     real. Queda el traspaso cuando se acercan: parpadeo de los NPC al cerrar unas y crear otras.
  2. **Misiones del anfitrión evaluadas también desde el gestor del usuario 2** (`0x28818F0` usa el gestor de
     misiones del dueño, `0x2880BC0` → `vf40`). Es el mismo progreso, sin estado doble, pero las stages ligadas a
     misiones podrían arrancar dos veces [H] (S12 lo marcó como el motivo para no levantar la puerta sin más; con la
     distancia mínima solo ocurre lejos).
  3. **Control e IA:** el controlador de los actores de stage sigue siendo el dueño del campo (`0x2792A70`), así que
     el cliente del anfitrión los simula a ~1 km. Está medido con existentes (§3.4); con nacidos tan lejos, no
     (`control-npc-lejos.md`).
  4. **Escenas del usuario 2 en su cliente:** con 2661/2264 propios, la tienda se abriría por el camino nativo, pero
     el filtro R1 y las rutas de `guest_seq_route` dan por hecho que todo es del anfitrión. Hay que revisarlos.
  5. **Carga de CPU y red del servidor del anfitrión:** dos ventanas de población. R4 de `mundo-invitado.md`.
- **Pruebas:**
  - un PC, con `probe` («would evaluate») y el peer quieto mientras el anfitrión se aleja más de 600 m: ver 2335 con
    dueño del peer en la captura del anfitrión;
  - después, dos PC: el invitado se queda en un pueblo y el anfitrión se va y viaja.

### 5.4 Opción B: segunda ventana del usuario 2 en el gestor del anfitrión

- **Qué:** que `0x288B7A0` del gestor del anfitrión evalúe también la celda del usuario 2, con su propia «última
  celda» guardada en el mod (no en `mgr+0x680`), y que el cierre por ventana conserve las stages de la **unión** de
  las dos.
- **Pros:**
  - un solo dueño de todo: sin duplicados por diseño;
  - el mundo sigue siendo «el del anfitrión».
- **Contras:**
  - `0x288B7A0` guarda una sola celda: llamarlo dos veces con posiciones distintas haría que cada pasada cerrara lo
    de la otra en `0x287DFE0`. Hay que intervenir en la decisión por stage de `0x287DFE0` y en el cierre de motivo
    30 (sin localizar); es una cirugía más honda en `.rsrc`;
  - **no arregla C1**: la carga del anfitrión seguiría cerrando todo. Los NPC volverían tras su 2762 y la siguiente
    evaluación (parpadeo de ~10 s).
- Solo si U da duplicados que no se puedan evitar.

### 5.5 Sobre C1 en concreto

- Con U, C1 queda resuelto para lo que está lejos del anfitrión.
- Con el anfitrión cerca del invitado (separación < `D_off`), una carga del anfitrión sigue borrando también lo del
  invitado y lo recrea tras la carga, como en un jugador. Es aceptable: la llegada del anfitrión repuebla la zona.
- **No se propone** saltarse el cierre de motivo 19 en `0x2817E80` («no cerrar si está junto al usuario 2»):
  - el gestor puede vaciar su contenedor después del bucle, y la stage quedaría huérfana con NPC que nunca se
    cierran;
  - el cliente del anfitrión descarga su mundo igualmente: desincronía en el anfitrión;
  - además sería una guarda que rechaza sobre un valor no medido.

  Si A1 mostrara un llamador limpio (bucle sin efectos posteriores), se podría estudiar, siempre primero en `probe`.

### 5.6 C4, el caballo del invitado

- Medir, con el mismo gancho de solo registro que A1 pero en `ServerActor::Logout 0x2068340`: motivo, clave, dueño,
  controlador (`+0x90`) y dirección de retorno, filtrado a los hijos del usuario 2.
- Encontrar quién manda el motivo 9 en la carga del dueño, y decidir si:
  - el hijo debe quedarse, porque su dueño es el usuario 2;
  - o hay que volver a crearlo tras la carga del anfitrión, como hace el mod con la copia del invitado.
- Hoy el invitado se queda sin caballo hasta que recarga [V].

### 5.7 Lo que no conviene hacer

- **Ignorar en el cliente del invitado los 2516 de motivo 19/30:** el actor ya no existe en el servidor y quedarían
  fantasmas sin actualizaciones. Es lo mismo que se descartó para el motivo 25.
- **Subir el radio `+0x468`:** es global, no repone lo que no se crea (C2) y no evita C1.
- **Copiar las stages del anfitrión al invitado (`observer_sched_copy`) como sustituto:** solo refleja lo que crea el
  anfitrión junto a sí mismo.

---

## 6. Direcciones (build 1.0.0.2976)

| RVA | Qué es | Estado |
|---|---|---|
| `0x2817E80` | `ServerNPCScheduleStage` vf13 (`[vt+0x68]`, vtable `0x5B169B0`): cierra la stage, `Logout` de sus NPC con el motivo de r8b, 2126 ToNear (`0x28182C5`) | [V] decompilado y prólogo |
| `0x5B16968` / `0x5B169B0` | vtables `ServerNPCScheduleSpawnManger` / `ServerNPCScheduleStage` | [V] |
| `0x288B7A0` | `ServerSequencerStageManager` update: puerta de dueño `0x288B87A`, celda de 64 m de `actorPosición`, `0x28818F0` + `0x287DFE0` al cambiar de celda | [V] bytes de la puerta, [E] resto |
| `0x287DE14` | Llamada del tick `0x287DBB0` a `0x288B7A0` (`r8 = [mgr+0x18]`) | [V] |
| `0x28818F0` / `0x287DFE0` | Evaluar celdas nuevas / revisar stages `AutoSpawnOwnerData` frente a la celda | [E] |
| `0x2C33900` → `.xtls 0x108309B0` | `UpdateScheduleStageOnceTimer` 2045: `campo->vf48()` → `0x2822D00([gestor+0x148])` | [V] decompilado |
| `0x2B00CE0` | `ServerField::vf48`: siempre el gestor del dueño del campo (ya enganchado por `user2_funnels`) | [V] (S12 + log) |
| `0x29C59D0` → `0x2BC9200` | `GameLoadingStartReq` → `TransformSync::GameLoadingStart` (→ `0x2BC9470` / `0x2BCA3D0` / `0x2BCA9C0`) | [V] |
| `0x2068340` | `ServerActor::Logout(actor, out, motivo r8b, …)`; ningún llamador directo con `0x13`, `9` o `0x1E` inmediatos | [V] |
| `0x2C38E28` / `0x29C2915` | Lectura y escritura de la ranura del observador (sin cambios) | [V] |

## 7. Pruebas que necesita cada cosa

- **Un PC** (anfitrión + `cdmp_peer` quieto):
  - A1–A4;
  - el anfitrión se aleja más de 600 m y viaja. En la captura del anfitrión (recibe todo) se ven los 2126 con
    motivo 19/30 de stages junto al peer, y con U en `probe`, «would evaluate».
- **Dos PC** (Shadow):
  - con U activo, el invitado se queda en un pueblo y el anfitrión se va más de 600 m y viaja a otra región: no hay
    2516 de motivo 19 junto al invitado, hay 2335 con dueño del invitado y nacen personas junto a él;
  - **antes de activarlo, avisar al usuario de que hace falta Shadow.**
