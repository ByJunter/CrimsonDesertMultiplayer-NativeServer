# S25, paso 2: «no nos vemos físicamente» (build 1.0.0.2976)

Investigación de solo lectura (sin tocar código, sin abrir el juego). Reloj: anfitrión = invitado + 1 h
(06:18:58.654 SAVE_OFFER del invitado = 07:18:57.643 del anfitrión). Etiquetas: **[V]** visto en log, captura o
código; **[E]** deducción.

Fuentes:
- Invitado (Shadow): `cdmp_20261012-{054004,060436}.log` (paso 1) y `-{061831,062315,062751,062954}.log`
  (paso 2), con sus `cdmp_packets_*.bin`.
- Anfitrión: paso 1 `backups/p8-sesion25-host/cdmp_20261012-063736.log` y `cdmp_packets_20261012-063747.bin`;
  paso 2 `bin64/cdmp_20261012-071438.log` y `cdmp_packets_20261012-071449.bin`.
- Las capturas se leyeron en streaming. Scripts en el scratchpad de la sesión: `keyscan.py` (paquetes que nombran
  una clave, por opcode y desplazamiento), `rec2005.py` (registros de `ActorLoadDataAck`), `recdiff.py` (diff byte a
  byte de un registro 2005 entre capturas) y `parentdump.py` (`MoveActor*` con padre en `+0x8C`).

## Resumen

1. **El único intervalo en el que el HUD mostraba al otro jugador sin cuerpo está en la sesión 06:23. Va de 06:25:21
   a 06:27:43 del invitado (07:25:07 a 07:27:43 del anfitrión). Durante ese intervalo el invitado no estaba en el
   mundo del anfitrión:** había vuelto al título y cargado **su propia partida en su servidor local**. Los datos
   coinciden: HP 1125 en (-10819, 578, -4156). Los dos juegos eran mundos distintos, pero el canal del mod siguió
   enviando posiciones y los dos HUD dibujaban al otro. El invitado caminó unos 250 m hasta el marcador del
   anfitrión, el anfitrión se acercó al del invitado y acabaron a **2,8 m** sin verse. Esto encaja punto por punto
   con lo observado en vivo: NPC, mundo, hora y clima cargados; el HUD dice dónde está el otro; no se ven.
2. **En las cuatro fases remotas** (06:18, los primeros 20 s de 06:23, 06:27 y 06:29), **cada cliente tenía el
   personaje del otro creado y movido por la red, en la posición del HUD (±0,05–0,30 m), en las dos direcciones.**
   Los paquetes de aparición, acción y movimiento son los del paso 1, en el que sí se veían. El registro 2005 del
   personaje solo cambia en claves y marcas de tiempo.
3. **Causa:** ninguna función nueva del paso 2 rompe la replicación. Hay dos piezas del mod que llevan a la
   situación:
   - el invitado solo admite **una conexión remota por arranque del juego** (`guest.cpp` `g_had_remote`/`decide()`),
     así que «salir al título → Continuar» carga la partida en su servidor local;
   - el HUD dibuja al otro jugador solo con el canal del mod, **sin comprobar que los dos estén en el mismo mundo**.

   El paso 2 lo provocó **de forma indirecta**. Con `guest_own_save=1` y el fallo del buzón (ya conocido), el
   invitado apareció con el personaje del laboratorio (HP 300) en vez de con su progreso. El usuario volvió a
   entrar desde el título y obtuvo su progreso real, pero en solitario.
4. **Arreglo:**
   - (a) HUD honesto: marcador y «Otro jugador …» solo si los dos están en el mismo mundo; si no, un aviso claro;
   - (b) aviso en el invitado al pasar a local tras una sesión remota;
   - (c) a medio plazo, que el invitado pueda volver a entrar al anfitrión en el mismo arranque (el anfitrión ya
     tiene C4 `reconnect` listo);
   - (d) el arreglo del buzón, que quita el motivo de volver a entrar.

## 1. Qué sesiones y cuándo

| Sesión del invitado | Fase | Invitado (su reloj) | ¿En el mundo del anfitrión? | Personaje del otro en cada cliente |
|---|---|---|---|---|
| 06:18:31 | remota | en el mundo 06:20:08,9 → 2001 06:20:55,9 | sí (`epoch 0x1 -> REMOTE`) | **sí, en las dos direcciones** |
| 06:18:31 | (local) | 06:21:10,2 `epoch 0x3 -> local: this game run already had a remote connection`; cerrado 06:21:13,1 | no | no hubo partida local: el juego se cerró a los 3 s |
| 06:23:15 | remota | en el mundo 06:24:47,0 → 2001 06:25:07,2 | sí | **sí, en las dos direcciones** |
| 06:23:15 | **local** | 06:25:21,5 `-> local`; 3093 local 06:25:39; 2549 local 06:25:49; en el mundo 06:26:15,7 → `Terminate` 06:27:43,7 | **no** | **ninguno: dos mundos distintos; el HUD sí dibujaba al otro** |
| 06:27:51 | remota | en el mundo 06:29:21,5 → 2001 06:29:46,8 → `Terminate` | sí | **sí, en las dos direcciones** |
| 06:29:54 | remota | en el mundo 06:31:40,5 → 2001 06:31:57,7 → `Terminate` | sí | **sí** (el seguimiento empieza; sin resumen de minuto: 17 s) |

Pruebas de la fase local (06:23) [V]:
- **Invitado:**
  - 06:25:21.541 `Session2 guest: connection (epoch 0x3) -> local: this game run already had a remote connection
    (restart the game to play on the host again)`.
  - 06:25:39 `a save is loading on this PC's own server after a session on the host's server (3093, local
    connection epoch 0x3)`, y luego `the local load sent its MoveFieldCompleteReq 2549 after 11.5 s: not stuck`.
  - En su captura, su propio personaje recibe en su servidor local la clave **0xA0100001** (2005 de 14881 B en
    t=110,6 s y de 14744 B en t=139,3 s, con más inventario que el del laboratorio).
  - `RemoteMotion summary` de 06:26:08 y 06:27:08: `tracked 0 (0x00000000)`, `compared 601` congelado, mientras
    `channel positions` sube de 1634 a 2783 y a 3935. Es decir, el canal (HUD) recibía al anfitrión, pero no había
    personaje del anfitrión en su mundo.
  - Los contadores de `RemoteInterp` se quedan congelados desde 06:25:08 (2235/29).
- **Anfitrión:**
  - 07:25:07.007 `C4: logout begin epoch=2`, 07:25:07.083 `UiPause: P8 session over (no other player)` y
    07:25:10 `Session2 probe (user 2 gone) … user 2 character ?`.
  - `RemoteMotion summary` de 07:25:22 a 07:29:22: `tracked 0`, `compared 2058` congelado, mientras
    `channel positions` sube de 1340 a 3049.
  - `RemoteInterp` 0xA congelado en 342.
  - En su captura no hay ningún 2005 de un personaje del usuario 2 entre t=600 s y t=856 s.
  - 07:25:07.769 `C4: reconnect epoch=3 ready (a NEW guest connection starts it)`: el anfitrión **sí** esperaba
    una reconexión.
- **Lo que mostraban los HUD** (posiciones del canal; el HUD dibuja exactamente esto):

  | Hora del anfitrión | Invitado, visto en el anfitrión | Anfitrión, visto en el invitado |
  |---|---|---|
  | 07:26:18 | (-10819,46, 577,77, -4156,28) **HP 1125** (su partida real; en las fases remotas, HP 300) | (-10725,71, 620,07, -3917,60) |
  | 07:27:13 | (-10750,85, 605,67, -4006,07) | |
  | 07:27:28 | (-10727,90, 617,56, -3924,10) | (-10723,37, 620,07, -3917,50) |
  | 07:27:38 | (-10727,52, 617,52, -3924,22) | (-10724,89, 617,31, -3925,22) |

  A las 07:27:38 estaban a **2,8 m** según los dos HUD. A las 06:27:43 el invitado cerró el juego.

**Respuesta a la pregunta 1:**
- **Invisibles de verdad:** solo en esa ventana de unos 2,4 min dentro de 06:23, en las dos direcciones y durante
  toda la ventana. No hay ningún personaje que ver: cada uno estaba en un servidor distinto.
- **Fases remotas:** los logs no muestran invisibilidad. Los dos personajes existen, se mueven, tienen acción y
  colisión (ver §2), igual que en el paso 1.
- **Sesión 06:18:** solo tuvo fase remota, y sus datos son prácticamente iguales a los del paso 1 (§2). Según
  los logs **no se vio afectada**.
- **Límite:** los logs no miden si algo se dibuja. Si el usuario asegura que tampoco se veían cuando el HUD decía
  «Otro jugador: HP 300», hay que aplicar la medida de §3.4.

## 2. ¿Replicó el servidor del anfitrión a cada jugador hacia el otro? Paso 1 frente a paso 2

### 2.1 El anfitrión (0xA0100001) en el cliente del invitado [V capturas del invitado]

| Fase | 2005 del anfitrión | 2991 con su clave en +0 | MoveActorAck en +0 | Sus piezas con padre = anfitrión (`MoveActorReq` +0x8C) | RemoteMotion (seguimiento; divergencia memoria-HUD) | RemoteInterp 0xA (1.er resumen) |
|---|---|---|---|---|---|---|
| Paso 1, 05:40 (45 s tras su 2005) | 1 × 11582 B | 3 | 196 | 88 | 05:41:41.9; 554 muestras, máx. 0,05 m | 197 |
| Paso 2, 06:18 | 1 × 11582 B | 3 | 189 | 132 | 06:20:11.8; 653 muestras, máx. 0,05 m | 190 |
| Paso 2, 06:23 (remota) | 1 × 11582 B | 9 | 20 | 46 | 06:24:50.0; 601 muestras, máx. 0,05 m | 29 |
| Paso 2, 06:27 | 1 × 11582 B | 10 | 42 | 42 | 06:29:24.1; 586 muestras, máx. 0,05 m | 51 |
| Paso 2, 06:29 | 1 × 11582 B | 1 | **0** | 12 | 06:31:43.3 (sin resumen) | — |

Además, en todas las fases remotas el invitado registra:
- `GuestDiag restore … actor key 0xA0100001 … result 0x00000000 (ok) -> restored` (acción restaurada);
- `MountSync gate probe … remote 0xA0100001 2991 A0 0xECF9B7D2 A1 0x26107D3C` (acción a pie aplicada);
- `GuestSeqPlay [3]` sobre el X del anfitrión;

y lo mismo en el paso 1 (05:41:44, 05:41:51, 05:41:59).

### 2.2 El usuario 2 en el cliente del anfitrión [V captura y log del anfitrión]

| Fase | Clave | 2005 | 2723 | 2991 en +0 | MoveActorAck | Piezas con padre = invitado | RemoteMotion | RemoteInterp 0xA |
|---|---|---|---|---|---|---|---|---|
| Paso 1, 05:40 (t 210–265 s) | 0xA010034E | 1 × 11554 B | 1 | 54 | 756 | 292 | 06:41:41.0; 882 muestras, máx. 0,16 m | 238 |
| Paso 2, 06:18 | 0xA01003FB | 1 × 11554 B | 1 | 31 | 211 | 264 | 07:20:11.6; 350 muestras, máx. 0,16 m | 242 |
| Paso 2, 06:23 | 0xA0100749 | 1 × 11348 B | 1 | 16 | 84 | 77 | 07:24:48.96 | +100 |
| Paso 2, 06:27 | 0xA0100B10 | 1 × 11348 B | 1 | 23 | 230 | 120 | 07:29:23.5; máx. 0,30 m | +253 |
| Paso 2, 06:29 | 0xA0100D11 | 1 × 11348 B | 1 | 4 | 1 | 52 | 07:31:41.8 | — |

Cada fase remota termina con un `RemoveActorAck` 2516 del personaje cuando sale el invitado. Los 11348 B de
06:23 en adelante son la generación del buzón (el fallo ya conocido); es el mismo personaje de laboratorio.

### 2.3 El registro 2005 es el mismo [V `recdiff.py`]

- **Anfitrión en el invitado (05:40 frente a 06:18):** 11582 B en los dos. Las 145 diferencias son marcas de
  tiempo, contadores y claves de objetos de servidor. La apariencia es la misma
  (`character/appearance/1_pc/1_phm/cd_phm_macduff/cd_phm_macduff_00000.app_xml`).
- **Usuario 2 en el anfitrión (paso 1 frente a 06:18):** 11554 B en los dos. Las 262 diferencias son claves
  (0xA010034E → 0xA01003FB), marcas de tiempo y un u32 en +0x228D (0 → 0x37D). Ese u32 vale 0x391 en la sesión
  06:04 del paso 1, así que no distingue un paso del otro.

### 2.4 Lo que esto prueba [V/E]

- Las piezas con padre en `+0x8C` (`kMoveParent` de `combat_follow_logic.h`) son actores 0xB que cada cliente
  mueve con el personaje del otro como padre. Por ejemplo, el anfitrión mueve 0xB0100521–23 con padre
  0xA01003FB y el invitado mueve 0xB0100156–15B con padre 0xA0100001. Que existan en cada fase remota indica
  [E] que el personaje del otro está instanciado como actor con transform en ese mundo, no solo como registro.
- La divergencia ≤ 0,05–0,30 m dice que su transform en memoria (`TS+0xB4` + sector) está donde dibuja el HUD.
- Así que en las fases remotas del paso 2 **el servidor replicó a los dos jugadores en las dos direcciones, igual
  que en el paso 1.**

### 2.5 La pista de «pocas muestras del 0xA0100001»

No es un fallo:
- **Decompilado `0x11B1FD0` `setInterpolatePositionData` [V Ghidra]:** devuelve sin insertar si `ts ≤ último`,
  vacía el búfer si el hueco supera 1000 ms y **no inserta si se movió ≤ 0,0001 m**. El controlador solo manda
  `MoveActorReq` cuando el personaje se mueve (`combate-retardo.md` §3).
- **El anfitrión estaba quieto:** según el HUD del invitado, su posición no cambió en toda la fase remota de 06:29
  ((-10724,79, 617,31, -3924,95)), por eso hay **0** `MoveActorAck`. En 06:23 se movió antes de que entrara el
  invitado y en 06:27 solo un poco, a las 06:29:31.
- **Sesión 06:18:** dio 189–190, como el paso 1 (196–197).
- En 06:23 los contadores se congelan desde 06:25:08 porque el invitado ya no estaba en el servidor del
  anfitrión.
- En el paso 1 también pasó: el primer resumen de 06:04 tuvo **1** muestra 0xA, y se veían.

## 3. Causa raíz

### 3.1 Los dos mundos (probado)

- **Una sola conexión remota por arranque en el invitado [V].**
  - `guest.cpp:90` dice `g_had_remote … one remote connection per game run (no reset with the host yet)`.
  - `decide()` (`guest.cpp:331-335`) devuelve Local con `this game run already had a remote connection`.
  - El cambio de época (`guest.cpp:706-714`) llama a `end_remote(...)` y la conexión siguiente queda local. Viene
    de D-18 (diario: «no existe todavía un reinicio pactado con el anfitrión»).
  - Desde entonces el anfitrión ya tiene C4 `reconnect` («`reconnect epoch=3 ready (a NEW guest connection starts
    it)`»), pero el invitado sigue negándose.
- **El HUD no sabe en qué mundo está el otro [V].**
  - `mod.cpp:443-456` rellena `HudData.remote = session.remote_state()` solo con el canal del mod.
  - `hud.cpp:165-187` escribe «Otro jugador: HP %.0f/%.0f a %.0f m» y dibuja el marcador sin mirar el estado de
    session2.
  - Con el invitado en su partida local, los dos HUD seguían mostrando al otro con HP, distancia y marcador.
- **`guest_local_after_remote=stuck` no lo impide [V ini del invitado].** Solo cierra una carga local que se
  atasca. La de 06:25:39 avanzó (`not stuck`) y se jugó.

### 3.2 Por qué pasó en el paso 2 y no en el paso 1 [V/E]

- **Paso 1:** ninguna sesión pasó a local: 05:40 terminó por la conexión y 06:04 cerrando el juego.
- **Paso 2:** el invitado entró cuatro veces y en todas apareció con el personaje del laboratorio (HP 300, en
  (-10717,65, 620,22, -3914,63)). Es el fallo del buzón ya conocido (`SAVE_OFFER … slot0 … save 1942478 B`, pero
  el anfitrión cargó la generación de 737 KB).
- **El paso a local en 06:23 [E]:** tras salir (2001 a las 06:25:07), el usuario pulsó Continuar en la misma
  partida y obtuvo su progreso real (HP 1125) en su servidor local, con el HUD aún «conectado» al anfitrión.
  Parecía «la partida propia funciona, pero no nos vemos».
- **Resto de sesiones:** en 06:18, 06:27 y 06:29 el juego se cerró tras salir. Encaja con aplicar los resultados
  con `aplicar-progreso-invitado.ps1`, que exige el juego cerrado.

### 3.3 Las funciones del paso 2, descartadas una a una [V]

| Cambio | Por qué no es la causa |
|---|---|
| `guest_own_save=1`, carga de la ranura 204 | `LoadSlot … REMAP 204`, `lobby VERIFIED` y `save VERIFIED`. El 2005 del usuario 2 que recibe el anfitrión es igual al del paso 1, salvo claves y tiempo (§2.3). Hay 2723, 2991 y movimiento en cada fase. |
| Retención de 2490/3093 hasta `SAVE_ACK` | En 06:18 el 2490 salió 81 ms después de abrirse la conexión (06:19:10.363 → .444). La secuencia del campo (`E3b H1` campo propio → 2437 → `H2 … users 2 user actors 3`, 2174/2144/2762, `WorldAnchor set`/`verify ok`) coincide línea a línea con el paso 1 (06:40:54 → 06:41:30 → 06:41:51). |
| `characterNo` e identidad | `C2 identity` da 0x7FFFFFFFFFFFF002 en los dos pasos, con `invariant ok` y `number unique`. |
| Registro del usuario 2 en el campo | Igual que en el paso 1 (fila anterior). La salida y el C4 dan `pass` en las cuatro fases. |
| `combat_follow_v2=1` (anfitrión) | Solo crea `MoveActorAck` sintéticos de claves **0xB** (`combat_follow_logic.h` `is_b`; resumen `last 0xB0100002`/`0xB0100091`) y solo hacia la sesión 2. No puede ocultar al invitado en la pantalla del anfitrión. |
| `weather_sync=1` | En el anfitrión es solo lectura y envío (`weather_sync.h`). En el invitado solo escribe X y el holder en modo visor (06:20:22–06:20:59). Mientras tanto, el anfitrión en el invitado siguió a 0,05 m de su posición del HUD. |
| `remote_interp_combat=1` (invitado) | Solo afecta a claves 0xB (`remote_interp_logic.h:15-17`; en la clase 0xB0100090/91). Los 0xA siguen con los 200 ms del juego. |

### 3.4 Lo que los logs no deciden y cómo medirlo

Ningún log mide si un personaje se dibuja. Para cerrar del todo las fases remotas:

1. **Pregunta al usuario, sin código:** cuando no se veían, ¿qué HP mostraba el HUD para el otro y para sí mismo?
   - HP 1125 del invitado: era la fase local. Queda resuelto.
   - HP 300: era una fase remota, y entonces hay que medir.
2. **Prueba en un PC con `cdmp_peer`.** Decide la dirección «el anfitrión ve al usuario 2» y el efecto de los
   cambios del anfitrión.
   - Arrancar `cdmp_peer --script docs/research/data/login_e3lc00act_112627.troc --guest-save <copia del
     laboratorio> --save-before-bye`.
   - Con `cdmp.p8-s25-host-activo.ini` (guest_own_save=1, combat_follow_v2=1, weather_sync=1), lanzar el juego.
     Cuando `RemoteMotion probe: tracking … 0xA01…` aparezca en el log, hacer una captura con `System.Drawing
     CopyFromScreen` (2560×1440) y recortar la zona del usuario 2.
   - Repetir con `cdmp.p8-s7-host.ini`, la configuración del paso 1.
   - Repetir con `weather_sync=selftest` en el anfitrión, con el usuario 2 delante, durante los 30 s de modo visor
     (`X+0x31=1`). Sirve para comprobar que ese modo no oculta personajes. Es lo único del invitado que escribe en
     el juego y se puede reproducir en un PC.
   - Si el usuario 2 se dibuja en los tres casos, ningún cambio del paso 2 lo oculta.
3. **Lo que solo Shadow puede medir:** la dirección «el invitado ve al anfitrión» con el cliente real del
   invitado, porque no caben dos copias del juego en un PC. Los paquetes que recibe ya están comprobados (§2.1).
   - El invitado entra y **no sale al título**.
   - Con «Otro jugador: HP 300» y a menos de 10 m, los dos hacen una captura.
   - Si hace falta una medida sin capturas, una sonda de solo lectura del estado de dibujo del personaje remoto,
     comparado con el del propio personaje: nodo de escena visible, alfa o LOD, malla cargada. Antes hay que
     localizar esos campos con Ghidra. Debe ser **solo lectura**, sin guardas.

## 4. Arreglo propuesto (sin implementar)

### (a) HUD honesto, en los dos PC. Riesgo bajo; es lo primero.

- **Dónde:** `mod.cpp`, donde se rellena `HudData` (líneas 443-456), y `render/hud.cpp`, en el panel y los
  marcadores (líneas 165-187).
- **Qué:** un campo nuevo `remote_world` (mismo mundo / no está en tu mundo / cargando) que cada lado calcula con
  lo que ya sabe:
  - **Invitado:** el modo de su conexión session2 (`g_mode == Remote`, `guest.cpp`) y su personaje en el mundo.
  - **Anfitrión:** el usuario 2 conectado y en el campo, la misma señal que `UiPause` ya registra en vivo
    («`P8 session LIVE`» / «`session over`», S25 07:19:09 / 07:20:55 / …).
  - Solo con «mismo mundo» se dibuja el marcador y «Otro jugador: HP … a N m».
  - Si no, en lugar de esa línea:
    - en el invitado, «Estás en TU partida (servidor local): el anfitrión no te ve. Cierra el juego y vuelve a
      entrar para jugar con él»;
    - en el anfitrión, «El invitado no está en tu mundo (título o su partida local)».
- **Sin cambio de protocolo.** Opcional: mandar el modo del invitado en `PlayerState` para que el anfitrión diga
  el motivo exacto. Eso sí cambia el protocolo y hay que subir `kVersion`.
- **Riesgos:**
  - Ocultar el marcador durante una carga legítima: mostrar «cargando» en vez de esconderlo.
  - Lectura entre hilos: solo atómicos, leídos en el hilo del mod que arma `HudData`, nunca en el de Present.
  - No escribe en el juego.
  - No es una guarda que rechace nada: es presentación, basada en estados medidos en vivo.

### (b) Aviso en el invitado al pasar a local tras una sesión remota. Riesgo bajo.

- **Dónde:** `guest.cpp`, en `decide()`, cuando devuelve Local por `had`, y en el 3093 por la conexión local que
  ya marca B1.
- **Qué:** `render::hud_notice(Notice::LocalAfterRemote, …)`, un aviso nuevo y largo, además de las líneas que ya
  se registran.
- La condición se midió en vivo en S25 (06:21:10, 06:25:21 y 06:25:39), así que no es una guarda sin medir.
- **Sin rechazo por defecto.** Un modo que lo impida (cerrar el juego o bloquear el 3093 local) iría como opción
  aparte, porque puede dejar al usuario atascado en el título.

### (c) Volver a entrar al anfitrión en el mismo arranque. Riesgo medio-alto; pieza propia.

- **Qué:** emparejar el C4 del anfitrión, que ya está activo, con un «reinicio pactado» en el invitado. Una
  conexión nueva podría ir a Remote si el anfitrión anuncia una época nueva lista tras una barrera limpia.
- **Hay que reiniciar por conexión todo el estado del invitado:**
  - gestores X y secuenciadores enrutados;
  - retenciones de `GuestSchedLoad` (06:25:14: `DROPPED (another connection …)`);
  - MountSync;
  - registros de X del clima;
  - retención del guardado;
  - cola de entrega.
- La sesión 9 dejó una carga local atascada tras una sesión remota. Una vuelta a remoto nunca se ha probado.
- Necesita diseño, revisión, prueba en un PC con `cdmp_peer` (solo cubre el lado del anfitrión) y Shadow.
- Mientras no exista, la instrucción de prueba es: **tras salir al título, cerrar el juego del invitado.**

### (d) El arreglo del buzón (ya en curso)

Si el invitado recibe su progreso en el mundo del anfitrión, desaparece el motivo para Continuar en local, que fue
el desencadenante en S25.

**Regla del proyecto:** nada de lo propuesto añade guardas que rechacen valores no medidos. (a) y (b) usan
señales registradas en vivo en S25. (c) se deja como diseño aparte.
