# Estado del mod en la build 1.0.0.3527

Última actualización: 2026-11-05. **La build instalada es ahora 1.0.0.3549: ver su apartado.** Las pruebas son con dos copias del juego en un mismo PC
(`scripts/launch-2inst.ps1`) y la red por loopback. **La prueba entre dos PC por Steam sigue pendiente:** necesita el
segundo PC y otra cuenta. Lo que se puede probar de Steam en un PC está en la tabla.

Perfil de prueba: `tools/porting/configs/cdmp.3527-2inst-host.ini` y `cdmp.3527-2inst-guest.ini`.

## Build 1.0.0.3549 (actualización del 5-nov-2026)

Steam actualizó el juego a **1.0.0.3549** (PE `0x6AEB7D59`, imagen `0x13CFF000`, SHA-256 `25f24e06…92b9`) a las 08:53, 27
minutos después de la última prueba. El `.exe` de 3527 ya no está en disco.

**En vivo en 3549 (dos copias en un PC, loopback, sesión de 17 minutos el 5-nov):** las tablas de abajo siguen siendo de 3527.

| Qué | Cómo se comprobó |
|---|---|
| Unirse | `connection REMOTE (host's server)` y estados hasta `state 0x18 marker 3`, sin cierres ni dumps |
| El "waiting" | `fell to marker 1 in load state 0x11 ... put back to 2` en el invitado y `in load state 0x18 ... put back to 3` en el anfitrión |
| Verse los dos | Capturas en los dos lados |
| Moverse | Cada lado da las coordenadas exactas del otro, en los dos sentidos |
| Monturas | El invitado llama al wyvern (`CallVehicleMercenaryAck`), monta y desmonta. Misma posición en los dos clientes; el anfitrión refleja el desmontar a los 100 ms |
| Partida del invitado al entrar | `the host ACCEPTED the upload` y `LoadSlot ... from exe+0xE3CCAD4 (3093 character) ... REMAP 204`: esa dirección es una de las que cambiaron |
| Copia de escena | `ObserverSceneCopy`: `COPY x1`, en la cola del usuario 2, `LOST 0`. Es la primera vez que se ve en vivo |
| Filtro de golpes entre bandos | 934 descartados |

**Sin comprobar en 3549:** matar a un enemigo (solo hubo dos golpes del invitado y ninguna muerte), morir y revivir, el viaje
rápido, el progreso del invitado de vuelta y dos PC. En esa sesión el anfitrión salió primero y el túnel cayó, así que el
invitado no recibió ninguna generación (no hay `gen ... STORED`): **el invitado tiene que salir antes que el anfitrión.**

Kit: `dist/cdmp-coop-20261105-3549-v3.zip`. El de 3527 está en `backups/cdmp-coop-20261105-3527.zip`: en 3549 se desactiva solo
(`unsupported game build; native integration disabled`).

### Primera sesión real entre dos PC (5-nov, 21:20, por Steam, 3549)

**Funcionó:** la conexión (116 ms), la partida propia del invitado (slot 100, subida y verificada en menos de 1 s,
`REMAP 204`), su viaje rápido (de 672 m a 152 m del anfitrión). **Sin confirmar:** que llegaran a verse.

El invitado no encontró al anfitrión hasta poner `peer_steamid`: con *Game details* en privado Steam no lo muestra jugando, y
la detección automática depende de eso. El lado del anfitrión acepta a los amigos que **él** ve jugando, así que si el perfil
del invitado también fuera privado haría falta `peer_steamid` en los dos.

Problemas que salieron:

1. **Cierre del invitado [punto exacto confirmado con su dump; por qué falta el objetivo, sin confirmar].** Lectura de `0x8` en
   `exe+0x2205734`, en el hilo principal: el manejador de `pa::StageChart_Function_ChangePivot` lee el elemento 0 de sus objetivos sin
   mirar cuántos hay. `0x22038F0` devuelve 1 si el parámetro es válido, aunque no encuentre a nadie (`r14` y `[rsp+0x78]` a 0). Era la
   etapa `0x6` de su propio personaje `0xA0100A6C` en su `ClientSequencerStageManager`: no una de las copias de `observer_scene_copy`
   (`0x336`..`0x577`, la última 85 s antes). Los dos habían cambiado a Damiane casi a la vez (`ChangeFocusActorReq`, anfitrión a las
   21:26:14) y se cerró 0,7 s después de su `ReloadStageStartReq` hacia Hernand; el mismo viaje a las mismas coordenadas no falló a las
   21:22 ni a las 21:24, antes de cambiar. Es una correlación con tres muestras. El cambio de personaje no es 2369
   (`ChangePlayerbleCharacterReq`): su contador quedó a 0.
   **Guardia:** `session2/pivot_guard` (`change_pivot_guard=1`) manda ese caso a la salida propia de la función (`0x22059FF`), que ya
   admite un resultado vacío. La escena sigue sin cambiar su pivote. Instalada en el juego; **no se ha visto saltar en vivo**, y la
   escena podría fallar más adelante por el mismo actor ausente.
2. **El mod no sigue el cambio de personaje [confirmado; la parte visible, arreglada sin probar en vivo].** `Local: pos` se queda
   en el personaje inicial (hasta 1.660 m del global del juego). El panel oculta entonces distancia y marcador (`hud_tracks_local`).
   Segunda sesión (22:18, build 3549): el anfitrión pasa a Damiane (`0xB0100002`, tipo 4) y el invitado sigue viendo a Kliff quieto
   y nunca a ella, aunque el servidor ya le había anunciado a Damiane (`child settle: released`, 22:17:40). **La causa no es
   `child_hold`:** el clan de mercenarios oculta a todo personaje que no esté en control (bit `0x100000` de Status, alfa 0; leído
   en vivo en el cliente del anfitrión con Kliff en control: `0xB0100002` y `0xB0100003` con ese bit y alfa 0). La rutina nativa
   `0x23144B0(clan, viejo, nuevo)` oculta al viejo, muestra al nuevo y guarda la clave en `clan+0x118`, pero solo en el cliente del
   que cambia: ningún paquete lo cuenta al otro. Además `remote_visible` impedía ocultar al Kliff del otro. Lo arregla
   `session2/focus_follow` (estado de control 16, los dos sentidos); falta verlo en vivo, y comprobar que el servidor reenvía los
   Move de Damiane al invitado (línea `FocusFollow summary`). El panel, el marcador y `mount_sync` siguen leyendo al personaje
   principal.
3. **Reentrar tras un cierre: BROKEN para siempre [arreglado, sin probar en vivo].** La conexión nueva rompe el enlace, el enlace
   roto descarta la identidad del invitado, y solo `start_epoch` lo reinicia, que necesita esa identidad. Las reentradas probadas
   antes eran tras un adiós limpio, por otro camino. Ahora se reinicia en READY (`GuestLifecycle::link_reset_due`).
4. **El anfitrión pierde los NPC de la ciudad cuando el invitado sale [causa medida, arreglado sin probar en vivo].** Medido el
   2026-11-05 a las 22:44:58 (`ManagerProbe`, salida limpia): `0x2AD40B0` (vf18 del manager, la descarga) corre en el manager del
   anfitrión con retorno `0x23BAFDB` y en el mismo milisegundo en el del usuario 2 con retorno `0xE55E49F`; el del anfitrión pasa de
   estado 2 a 0 y no vuelve. `0x23BAFB0` es la vf34 de los componentes de secuenciador, que el campo llama al salir un actor
   (`0xE55E320`): descarga `field->vf48()`, el manager **del dueño del campo**, y luego el suyo. Con un jugador son el mismo. Es el
   "F5" que se sospechaba. `session2/field_leave_guard` (`field_leave_guard=1`, los dos PC) salta el del dueño solo si el actor es de
   otro usuario. El cliente tiene la misma forma (`ClientField` vf48 `0xAAA7A0`, manager vf18 `0x99B5750`). Falta verlo en vivo, y
   comprobar que el manager del anfitrión sin descargar no retiene nada del usuario 2 (entrar otra vez después de salir).
   Lo que se sabía antes: al salir
   el usuario 2 se descarga el `ServerSequencerStageManager` **del anfitrión**: los 58 de 58 `DeleteNPCScheduleStageAck` nombran a
   `0xA0100001`, más 184 `DeleteFactionSpawnStageAck`, 18 `DeleteSequencerAck` y 231 `RemoveActorAck` de razón 0x13. Después el manager
   deja de evaluar celdas. Mismo patrón en 3527 con salida limpia (32 de 32) y en más capturas, así que no depende del cierre ni de
   Damiane. No es el paquete 2001 como tal: en una captura la ráfaga llega 330 ms antes. Los 7 que se crean después no son una
   recuperación: son los mismos 7 de tras una descarga nativa. **No se sabe qué llamada nativa elige el manager del anfitrión** (se
   sospecha una búsqueda por dueño, como F3/F4 con vf48), ni en qué estado queda. Ninguna captura tiene una recarga del anfitrión
   después, así que tampoco está probado que una recarga lo arregle. `mundo-escenarios-propios.md` dice que ese estado solo lo pone
   el dueño al cargar, y esto lo contradice: el mapa estático tiene un hueco.
   **La medida ya está instalada:** `session2/manager_probe` (`manager_probe=1`, solo anfitrión, solo 3549, solo registra) anota cada
   cambio de `+0x6D0` y `+0x6BA` (los `+0x6A0` y `+0x68A` de 2976) de los dos managers, y quién llama a los que escriben ese estado
   (`0x2AD9C00` estado 1, `0x2ADABE0` y `0x2AD40B0` estado 0, `0x2ADB5E0` estado 2, `0x2ADB090` preparación). Las líneas `ManagerProbe` de
   la próxima salida de un invitado dirán qué pasó; aún no se ha visto ninguna con un invitado.
   Orden propuesto a partir de ahí: probar a repreparar con un `PreLoadSequencerReq` en la sesión del
   anfitrión (hoy `server_inject_ex` solo va a la sesión 2); y solo si la medida muestra un retorno de vf48, un "F5" como F3/F4.

**Tercera sesión entre dos PC (22:55-23:08, kit v5):**
- `focus_follow` **funciona en vivo**: los dos cambian a Damiane y cada uno ve al nuevo personaje del otro. En el anfitrión:
  `hidden 0xA01001E0` al cambiar el invitado, `shown`/`hidden` al volver; 3.824 Move de Damiane del anfitrión al usuario 2.
- `field_leave_guard` **funciona en vivo**: al salir el invitado (23:08:23) saltó en servidor y cliente, y el manager del
  anfitrión siguió en estado 2 hasta que el anfitrión cerró (23:09:34).
- Nuevo, visto por el usuario: al entrar, la cuenta de Steam del invitado recibió **todos los logros del anfitrión**. La única
  vía a `ISteamUserStats` es `PlatformAchievementManager_Steam::vf2` (`0x19827C0`); en el mundo del anfitrión el cliente
  del invitado tiene el progreso del anfitrión. `session2/achievement_guard` (invitado) la salta desde que juega en un anfitrión
  hasta reiniciar el juego. Sin probar en vivo. Los logros ya dados no se pueden quitar desde el juego.
- También: el invitado ve y usa los Nexos del Abismo descubiertos del anfitrión. Sin investigar si llegan a su partida
  propia al aplicar el progreso (`apply-guest-progress.ps1`); hasta saberlo, no aplicar el de esa sesión.
- El marcador y el panel siguen al personaje en control (`PlayerReader::set_follow_focus`, clave en `clan+0x118`). Sin probar.

**Noche del 5 al 6-nov (dos copias en un PC, kit v7):**
- **Clima, portado y en marcha.** Un subagente leyó en el exe de 3549 todo lo que faltaba (`docs/research/s25/clima-3549.md`):
  X = `[[0x70D9FC0]+0x60]+0xF70` (getter `0x7AA9B0`), vtable de X `0x611E330`, gancho de la actualización `0x40C2580`, bloques
  A/B/C y el formato nuevo (estado 12, versión 2, 69 palabras). En vivo: `probe` en los dos con `same, vtable ok`; luego
  `weather_sync=1` en los dos: el invitado en modo visor (`X+0x31 = 1`), `frames applied 2083`, `samples in 447 (bad 0)`,
  `faults 0`, y los mismos valores (nubes 0,70, viento 238,087°) en las dos copias. No se ha visto un clima distinto en
  pantalla: las dos copias tenían el mismo.
- **Movimiento más suave.** (1) `reliable.cpp`: reenvío rápido (un fragmento sin ack cuando ya hay 3 posteriores con ack y
  pasó un RTT). En la sesión real de las 22:55: 166 reenvíos, cada uno con el flujo ordenado parado ≥ 250 ms. (2)
  `remote_interp_players` (los dos): el personaje del otro jugador ya no usa los 200 ms fijos del juego, sino pico de
  retraso + 50 ms (200..450), con un pico que cae en 6,4 s. Medido en `probe`, con los mismos movimientos: tirones (el
  personaje parado mientras se movía) 277 con 200 ms frente a 3 con el retraso nuevo (el anfitrión viendo al invitado) y
  105 frente a 12 (el invitado viendo al anfitrión). En modo 1, tras ajustar (pico que cae en 25,6 s, margen 60 ms), con el
  mismo patrón de paseo: 2 tirones de 718 muestras (0,3 %) en el anfitrión y 12 de 555 (2,2 %) en el invitado, frente al
  44 % con los 200 ms fijos. El precio: el otro jugador se ve hasta 0,45 s por detrás (en dos copias la "tardanza" es casi
  toda desfase de relojes). (3) `docking_dedupe` (invitado; compara el opcode LÓGICO, el camino de envío ya decodificó):
  la misma petición de acople de equipo, una vez cada 2 s: 118 de 154 descartadas en 2,5 min.
- **Logros de Steam:** `achievement_guard` bloqueó en vivo `ExpertStoryteller` y `GameConqueror` al entrar el invitado: es
  la causa confirmada (el cliente del invitado tiene el progreso del anfitrión). Arreglo verificado.
- **Tenderos.** Encendida la cadena de escenas (`observer_seq_copy` + `observer_seq_copy_2322` en el anfitrión; en el
  invitado `guest_seq_route`, `guest_seq_foreign_player`, `guest_seq_play`, `guest_x_stage_veto`, `guest_x_ui_veto`,
  `guest_seq_foreign_present=1`; `guest_seq_foreign_timeline` en probe). Arreglado `guest_seq_foreign`: comprobaba el punto
  `0xD33D2E`, que es el gancho de `guest_seq_route`, y se negaba siempre; sin él una escena del anfitrión
  (`cd_seq_boss_rematch_matt`) movía al personaje del invitado (lo vimos con un papel en la mano). En Hernand las escenas
  `cd_seq_funcnpc_*` (smithy, grocery, general, clothes, pub, bank, butcher…) empiezan en el invitado, sus tenderos están
  en las dos copias en el mismo sitio y visibles, y **el invitado manda `SetQuestDialogReq` a todos los tenderos
  (`0xB01005F0`–`0xB01005FF`) y el servidor le contesta `SetQuestDialogAck`**, igual que al anfitrión. La sesión real con la
  cadena apagada no tuvo ninguno (la tabla de `p8-npc-tiendas-animales-escenas.md`: 0). **No se ha probado hablar ni
  comprar** con un tendero desde el invitado: las pruebas con teclas se atascaban en muros y puertas. Según S46, comprar
  puede fallar (la compra exige que la escena del anfitrión sea la actual del gestor del dueño del campo).
- **Estabilidad:** unos 75 min de dos copias con todo esto, cinco reinicios y viajes rápidos, sin cierres.

**Madrugada del 6-nov (dos copias en un PC, kit v8): puestos del mercado y tenderos.**
- **Por qué al invitado no le pasaba nada al pulsar.** Ve los avisos de los objetos del anfitrión ("Chair E Carry",
  "Empty Bottle E Buy"), pero la pulsación no manda nada: ni petición ni error. El cliente lo decide en la pulsación:
  manejador `0xBCCD50` → evaluación `0x22E62A0` (salto a la sección protegida `0xCAF4350`: vf168 y vf170 `0xABF190` del
  componente de interacción, modo 3; el aviso usa el modo 0) → `0x22E7360` → `0xABE7B0` → emisor del 2671 `0xABAE00`.
  No es ninguna de las comprobaciones de controlador conocidas (`0x9D05D0`/`0x9D0B00` no rechazan nada al pulsar). Con el
  `+0x90` del objeto escrito a mano con el usuario del invitado (solo prueba) la pulsación sí manda 2671 → éxito,
  2720 → éxito y `CatchActionResultAck`, y el servidor lo acepta.
- **`interact_gate` (nuevo, solo 3549).** En el invitado con 1, gancho en la entrada de `0xBCCD50`: durante esa llamada,
  un actor de los tipos listados (`interact_gate_types`, por defecto 7: objetos del mundo) que controla otro usuario
  tiene en `+0x90` el usuario del invitado; al volver se repone el valor del servidor. También sonda en `0x22E62A0`
  (resultado: código 2 = la rama de subinteracciones del llamador, tanto si el objeto es suyo como si no). **Comprobado
  en vivo:** en el puesto junto al almacén general de Hernand, "Storage Bowl" y "Rosemary" (hay que **mantener E**;
  una pulsación corta no llega a `0xBCCD50`): 2671 y 2720 aceptados, "Rosemary x1 acquired", dinero del invitado
  2115,33 → 2115,25. **Un cierre del invitado por un fallo mío** (04:15, `cdmp_crash.guest_20261106-041522_64956_1.dmp`,
  `exe+0xABB018` leyendo `0x30000000`): `0xBCCD50` tiene SEIS argumentos (todos sus llamadores escriben `[rsp+0x20]` y
  `[rsp+0x28]`; el primero llega a `0xABAE00` como 7.º y se lee en `0xABB018`) y el desvío solo pasaba cuatro, así que
  el original leía lo que hubiera en la pila. Las tres compras anteriores salieron bien por suerte. Corregido: el
  desvío recibe y pasa los seis.
- **Tenderos con NPC: aviso sí, tienda no.** C1 (`guest_x_interaction_stage`) portado a 3527/3549: punto A `0x22ED729`
  (`mov rdi,rax ; mov [rbp+0x8F8],rax`), búsqueda `0x2156CA0` con la tabla del gestor desplazada +0x28
  (`isl::kLayout3527`). R y F (solo lectura) se instalan solo en probe. Con 1 el invitado ve "R Shop" en los tenderos
  (39 000 consultas redirigidas a las escenas del anfitrión `0x2CF`, `0x2D0`, `0x2D1`, `0x2D2`, `0x2D4`). Con el
  intercambio de `interact_gate` también para el tipo 3, mantener R en el verdulero manda 2671/2720 con la interacción
  `0x000F4321` y el servidor los acepta, pero **la ventana de tienda no se abre**: la cámara se queda encuadrando al
  tendero, que se gira; el invitado puede moverse y abrir el menú. Encaja con S46: la tienda la abre la rama del cliente
  dueño de la escena (el anfitrión). En el kit: C1 = 0 y tipos = 7. No se sabe si el tendero queda "ocupado" en el
  servidor después de eso.
- **Tenderos con NPC, segunda parte (05:00–06:45).** Referencia nativa en el anfitrión (verdulero Delkin, escena
  `0x1AF`): tras `InteractionDoStepSuccessAck` (2916) su cliente pide `StageBranchStateReq` (rama 6); el servidor
  contesta `SetAvoidByCutSceneAck` 2561, `StageOnBranchEventAck` 2524 y `StageBranchStateAck` 2981 (21 B); el cliente
  dueño manda 4 × 2173, mueve la IA del tendero, informa del volumen `0x20000011` (el jugador dentro) y sale el menú
  "Hernand Grocer's Shop — Delkin: Buy / Sell / Leave". La ventana la abren eventos `UIControl` de esa escena
  (subagente: `StageChartUIControl`, tipo 9 con la pista del tendero; `0x6CCA40`, ventana `UIGamePlayControlRootNpcBuyStore2`).
  Hecho en el invitado:
  - `store_gate` (los dos): la comprobación de tienda `0x23C6600` (cliente y servidor) exige que la escena del
    tendero sea la actual del gestor; con 1 pasa para las compras del otro jugador (`0x23C676B` / `0x23C6A37`).
    Instalado, aún sin ninguna compra de tendero que lo use.
  - `guest_shop_branch` (invitado): tras su propio 2916 de tienda, le da a SU cliente la rama 6. Primero sobre la
    escena del anfitrión (X): sale la cámara de tienda, nada más (en el invitado esa escena no tiene jugador). Luego
    sobre una COPIA propia de la escena (el 2661 del anfitrión con su personaje como actor y dueño, id `0x1F000000 |
    escena`), entregada solo cuando la etapa ya arrancó (un primer intento a ciegas a los 1,5 s cerró el invitado en
    `exe+0xBA9DAA`, tabla de ramas aún nula; volcado `cdmp_crash.guest_20261106-060844_51204_1.dmp`). Con la copia: sin
    cierres, la cámara de tienda, la copia corre como escena propia (manda sus 2173 y volúmenes, retenidos), pero su
    volumen nunca informa del personaje del invitado y cada 2 s pide volver a la rama 0: **el menú no sale**.
  - En el kit: `guest_shop_branch=0`, `guest_x_interaction_stage=0`, `interact_gate_types=7`.
- **Memoria cercana para trampolines (invitado).** Hay muy poca a ±2 GB del exe. Con los tres puntos de C1 instalados al
  arrancar, los cuatro ganchos de `RemoteInterp` (que esperan al mundo) se quedaron sin memoria ("no slot within ±2 GB").
  Cada gancho nuevo del invitado compite con los que se instalan después: preferir pocos y solo los necesarios.
- **Un cierre del invitado al arrancar** (02:18, `sl.interposer.dll+0x29D8D`, lectura de 0x0 en el hilo principal, antes
  de cualquier gancho del mod, que instala el overlay al entrar en el mundo): Streamline, no el mod. El informe de
  fallos del juego intentó enviarlo. Se relanzó sin cambios y no se repitió.
- **Un cierre del ANFITRIÓN** (03:58:42, `cdmp.host_20261106-034720.log`): "[Crash] 덤프 보내는 중..." en el hilo
  principal mientras el invitado viajaba por el nexo desde el punto de inicio hasta Hernand (el anfitrión se había
  quedado en el inicio). Justo antes, `ObserverSeqCopy [bound actor removed]`: actores enlazados a escenas copiadas al
  invitado salían de su vista. Es el único cierre del anfitrión en todos los registros desde el 5-nov por la mañana, y
  el código del anfitrión es el de v7 salvo `interact_gate` (apagado en el anfitrión). **No hay volcado**: la captura
  de fallos del mod (`guest_s3`) solo existe en el invitado y el del juego se envía. Pendiente: captura de fallos
  también en el anfitrión. Las cuatro veces anteriores de esta noche, con la misma secuencia, no pasó.

**Mañana del 6-nov (kit v9, 07:00): regresión y tenderos, tercera parte.**
- **Kit v9** (`dist\cdmp-coop-20261106-3549-v9.zip`): el binario de las 06:50 (con `store_gate`, `guest_shop_branch` y
  `shop_ui_trace`, todo apagado por configuración) y los perfiles de v8 (`interact_gate=1`, tipos 7). Regresión con ese
  binario exacto: el invitado compra "Rosemary" en el puesto (mantener E), "Rosemary x1 acquired", 8 cobres menos, sin
  cierres, `RemoteInterp` instalado. 101/101 tests con los juegos cerrados (con los juegos abiertos fallan
  `launch_2inst_script` y `host_save_restore_script`, que se niegan a lanzar; lanzados desde pwsh 7 también fallan por el
  `PSModulePath` de pwsh: `Get-FileHash` no existe en 5.1. Desde bash: `env -u PSModulePath`).
- **Corrección de lo de las 06:45:** en la prueba de las 06:31 la copia SÍ informó del personaje del invitado dentro del
  volumen `0x20000011` (911,418 y 912,920 del registro de paquetes del invitado). La copia se comporta igual que la escena
  nativa: rama 6, los cuatro 2752 (`0x60000000..3` = 0, 0, 1, 1), los volúmenes y, a los 2 s, la petición de rama 0. La
  escena NATIVA también vuelve a la rama 0 a los 2 s **con el menú abierto** (anfitrión, 679,2 → 681,3): el menú no depende
  de seguir en la rama 6.
- **`shop_ui_trace` (nuevo, los dos, solo registro):** ganchos mid en la entrada de `0x6CCA40` (despacho de
  `StageChartUIControl`) y de `0xE8BD20` (la ventana de compra toma al tendero), 150 s después del mundo. **Nativo
  (anfitrión, Delkin, escena `0x2A3`):** el menú "Hernand Grocer's Shop — Delkin: Buy / Sell / Leave" son eventos
  UIControl sobre el JUGADOR (`0xA0100001`), no el tendero: ventana `ButtonListPanel`, control
  `selector-stagechart-self` tipo 14 (1 parámetro) y tipo 20 (4), luego `selector-stagechart-selection` tipo 5 (5: tres
  cadenas tag 4, los botones); ESC → tipo 15. **Invitado (copia `0x1F0002A3`):** rama 6, cámara de tienda, ni un solo
  UIControl. El contador de `guest_x_veto` lo confirma: el manejador UIControl del procesador (`0x6A1050`, gancho en
  `0x6A1065`) no se llamó ni una vez en toda la sesión, mientras que InputBlock se llamó 5 veces. La copia no está cortada
  por `guest_seq_present` ("not foreign" +1 en ese minuto). `StageChart_Function_UIControl::vf8` (`0x255C800`) salta sin
  condición al hueco `+0x160` del procesador: la función UIControl del gráfico no llega a ejecutarse en la copia.
- **Diferencia que queda:** en el nativo, justo tras la rama 6, el cliente dueño manda `StartAIFunctionReq` 2606 y
  `StartActionReq` 2202 **para el tendero** (el tendero se gira hacia el jugador) y después sale el menú. En el invitado el
  tendero lo controla el cliente del anfitrión (`+0x90` = `0x90100000`) y no se gira. El 2661 de la copia es idéntico
  al nativo salvo la clave del personaje y el id (enlaces `0x20000002` = jugador, `0x20000004` = tendero, tipo 5).
  Hipótesis: el gráfico espera al paso de IA del tendero antes del UIControl. Experimento `guest_shop_branch=2`: mientras
  la copia está en la rama 6 (12 s como mucho), el cliente del invitado es el controlador del tendero (`+0x90` := su
  usuario; se repone el del anfitrión al acabar) y sus peticiones para ese tendero se retienen (no llegan al servidor).
  **Resultado (07:37, tendero `0xB0100564`, copia `0x1F0001A0`): sin efecto.** El cambio de controlador entra y se repone
  sin cierres (solo se retuvieron cuatro 2857 de posición), pero el tendero no se gira, no hay 2606 / 2202 y tampoco hay
  UIControl (InputBlock otra vez 5). Ser el controlador no basta: el paso de IA lo lanza el propio gráfico y en la copia
  no se lanza. Descartado también el buff `0x128E7` del tendero (se pone y se quita solo cada pocos segundos).
- **Siguiente paso, si se retoma:** saber qué funciones del gráfico de la rama 6 se ejecutan en el nativo y en la copia.
  Una tabla de rastreo para el procesador de la escena (la que `guest_seq_present` ya sabe instalar, pero que registre y
  reenvíe cada hueco) en el anfitrión y en la copia, y comparar. La otra vía (abrir `UIGamePlayControlRootNpcBuyStore2`
  directamente con `0xE8BD20` + mostrar) necesita encontrar la ventana en el árbol de la UI del invitado: `0xE8BD20` no
  se llama nunca en el invitado.
- **Modo 3 (08:00–08:20): la copia antes de la pulsación.** Idea: en el nativo la escena recibe la interacción y pide la
  rama 6 ella misma; una rama metida desde fuera nunca ejecutó el UIControl. Con 3, la copia se crea antes (al ver una
  entrada con la clave de tienda, que resultó no estar en los primeros 0x40 bytes de la entrada, o tras una primera
  pulsación "Shop") y C1 en el punto A apunta `[entrada+0x20]` a la copia cuando la búsqueda nativa la encuentra en M
  (el id se lee después del punto A: `0x22ED766 mov r8,[r13+0x20]`). Comprobado en vivo que la copia `0x1F0001A0` está en
  la tabla de M (leída con `scratchpad\mscan.py`). **Resultado: peor.** En cuanto existe la copia, el cliente del invitado
  deja de evaluar avisos de interacción (las llamadas al punto A se paran del todo: 18 007 y ya no suben), las entradas
  de Delkin se rehacen (sus direcciones pasan a tener otros datos), no vuelve a salir "R Shop" ni con ningún otro, y el
  personaje del invitado aparece con un papel en la mano. La copia NO se comporta como la escena nativa en reposo:
  en el nativo el servidor también ejecuta la escena y mueve sus ramas según los informes de volumen del cliente (2620),
  y los de la copia no llegan al servidor. Emular esa parte del servidor en local es mucho más trabajo; el modo 3 deja al
  invitado sin interacciones y no debe usarse.
- **`guest_shop_window` (nuevo, invitado, 08:30–08:55, SIN PROBAR):** abrir la ventana de compra sin escena. Capturado en
  el anfitrión el "Buy" nativo: el menú se cierra (tipo 15) y la ventana `NpcStoreBuyView` / `selector-stagechart-self`
  recibe tipo 9 (lista tag 5 con la clave del tendero en decimal y un espacio: `'2954899684 '` = `0xB02030E4`), 20, 14
  (mostrar) y 21 (`PrefabTag(Store)`); el suscriptor llama a `0xE8BD20(W, {&h, 1, 5}, cmd)` desde `0xFFAAF2` con un
  asa de 0x20 B `{vtable exe+0x58FE3B0, actor*, válido}`. W = `UIGamePlayControlRootNpcBuyStore2` (vtable `0x59ECA30`;
  hueco +0x498 = `0xE8BD20`, +0x490 = `0xE8C020`, que llama al mostrar base `0xA370FF0`: con el tipo 0xE y sin parámetros
  pone `+0x128` = 0, `+0x130` = 1 y muestra el nodo). Hay tres objetos con esa vtable; el bueno tiene de cuerpo
  `[[W+8]+0x100]` = `NpcStoreBuyBody` (los otros: `NpcStoreTradeBuyBody`, `StableBuyBody`); tras usarse, `+0x2F0` = clave
  del tendero y `+0x2F4` = su CharacterKey. Con 1, la pulsación propia del invitado sobre un NPC (gancho de pulsación de
  `interact_gate`, hilo de fotogramas) hace `0x71C0E0(0, &h, clave)`, `vf+0x498`, suelta el asa (`vf0(h, 0)`) y, si la
  ventana tomó al NPC, `vf+0x490` con el tipo 0xE; todo con SEH. Las ventanas no existen 90 s después del puntero del
  mundo (primer intento: 0 encontradas), sí unos minutos después (escaneo externo `scratchpad\vtscan.py`: 3); el código
  ya reescanea cada 30 s. **Siguiente paso:** perfiles de `scratchpad\exp\` (`guest_shop_window=1`, `store_gate=1` en
  los dos, `interact_gate_types=3,4,7`, `guest_x_interaction_stage=1`), pulsar "Shop" en Delkin con el invitado y probar
  una compra.
- **`guest_shop_window` en vivo (10:25–11:40).** Las ventanas se crean unos minutos después del mundo: ahora se
  reescanea cada 30 s (encontrada a los ~3 min, 15–25 s de escaneo en segundo plano). Con la pulsación propia del invitado
  sobre Delkin (10:33 y 11:22): `0xE8BD20` toma al tendero (`+0x2F0` su clave, `+0x2F4` = `0xB7D`), `+0x130` = 1 y el
  elemento de UI queda marcado visible (`E+0x27D` bit 0), sin cierres; **pero no se dibuja nada**. Con
  `guest_shop_branch=1` a la vez, la copia entra en la rama 6 (cámara de tienda, sin HUD) y la ventana sigue sin verse.
  Hipótesis: se mostró antes de la fase de UI de la escena; ahora se oculta y se vuelve a mostrar desde el gancho de
  InputBlock de `guest_x_veto` para una escena del propio M (`guest_shop_window_on_scene`), una vez y en los 8 s
  siguientes a la pulsación. Sin probar aún. Los comandos 20 y 21 nativos: el 20 es la guía de teclas (manejador propio
  del suscriptor `0xFFACF1`), el 21 elige la pestaña (`PrefabTag(Store)`, `0xE8C020`).
- **Más intentos con la ventana (11:40–13:00), ninguno la dibuja:**
  - Volver a mostrar en la fase de UI de la escena (gancho InputBlock): sin efecto.
  - Comando 21 (`PrefabTag(Store)`) tras mostrar: sin efecto.
  - `guest_shop_window=2`: los mismos eventos `StageChartUIControl` (9, 14, 21) por el despachador `0x6CCA40`, con
    `staticstringA` propios (cuenta negativa = estáticos, `0x521740` no los cuenta). El suscriptor los procesa: la ventana
    toma al tendero (`+0x2F0`) y `+0x130` = 1, igual que en el nativo; tampoco se dibuja. Con el id de la copia como
    escena, igual. Y el `show` del menú (`ButtonListPanel` 14) tampoco sale: **en el invitado no se dibuja ninguna UI de
    escena**. Los antepasados del elemento de la ventana (`[el+0x38]`) no tienen componente de vista.
  - `guest_shop_branch=4` (modo X): la escena del anfitrión tal como la tiene el invitado recibe la rama 6, con su
    presentación desilenciada (`guest_seq_present_set_muted`) y sus manejadores de UI permitidos (`guest_shop_open_now`):
    cámara de tienda, InputBlock de X permitido, pero su UIControl tampoco se ejecuta. Ni la copia ni X ejecutan
    UIControl en el cliente del invitado; el del anfitrión, sí. El 2661 que reciben los dos es idéntico byte a byte.
  - Siguiente: la copia con las piezas de escena del invitado apagadas (`guest_seq_play`, `guest_seq_foreign_timeline`,
    `guest_seq_foreign_present`, `guest_x_stage_veto`, `guest_x_ui_veto`) para ver si alguna lo impide.
- **Disponibilidad de "R Shop":** el aviso sale gris y la pulsación ni llega a `0xBCCD50` cuando Delkin está de espaldas
  trabajando en sus estantes, o no está (el anfitrión lejos: sus PNJ los lleva el cliente del anfitrión). Funciona
  enseguida tras lanzar (hacia las 12:40 del juego) y con el anfitrión cerca. Las pruebas con teclas pierden mucho tiempo
  en esto; R cuando no hay interacción saca un papel.
- **Conclusión de la noche para los tenderos:** el menú de tienda del invitado necesita que una escena suya corra con el
  servidor detrás (como `user2_stages` de la opción U, que no está portada ni validada en 3527/3549), no una copia local.
- **Estado del kit:** v9 sin cambios (`guest_shop_branch=0`, `store_gate=0`, `shop_ui_trace=0`, `guest_x_interaction_stage=0`,
  `interact_gate_types=7`). En `bin64` está el `cdmp.asi` exacto de v9. El código fuente tiene además los modos 2 y 3 de
  `guest_shop_branch` (apagados; el 3 deja al invitado sin interacciones), compilado y con 101/101 tests.

**Tarde del 6-nov: la vida de las escenas (`guest_seq_play_live`) portada a 3527/3549.**
- **Por qué se intentó** (premisa que la traza de después desmintió, ver abajo): se creía que las funciones UIControl
  del gráfico (menú y ventanas de tienda) eran «temporizadas» (su vt `+0x28`, `0x3A9BF0`, devuelve 1, y el ejecutor
  `0x2202A60` deja esas funciones en `[nodo+0x70]`) y que en el invitado nadie hacía esa actualización para X, porque la
  fase 2 de `guest_seq_play` estaba negada en esta build.
- **Lectura con capstone sobre el PE de 3549** (detalle en el comentario de `guest_seq_play.cpp`):
  - la llamada nativa: `0x9A0060` (dt en `xmm6` desde `0x9A0085`) → `0x9A017C cmp byte [0x710F318],0` →
    `0x99C450` (= `0x8B2660` de 2976) → `0xBAEB20(M)`, `0xBAF680(M)`, **`0xBB0450(M, dt)`** (= `0xAA3E70`);
  - nuestro punto `0x99C389` está dentro de `0x99C2E0`, que no escribe `xmm6`: el dt del fotograma sigue ahí;
  - gancho de salto **`0xBB112F` → `0xBB12BA`**, con `rsi := exe+0x5AC37B0`. La misma estructura que 2976: asa del jugador
    local en `[rbp+0x18]` (sin otros usos antes), cerrojo de `[G+8]` tomado y soltado dentro del tramo, copia de la lista
    viva en `[rsp+0x38]` (`0xBB09AF`) destruida en la reanudación, `r13` recargado de `[rbp+0x4B0]` antes del bucle,
    sin `push`/`pop` entre el prólogo y el epílogo;
  - los campos de la compuerta de G se movieron +0x40 (`+0x2E4/+0x2C8/+0x2E8/+0x2F0/+0x2F4/+0x314/+0x30C`).
- Se verifican 13 tramos de bytes antes de instalar; si alguno no cuadra, `live=0`. El enrutado del 2524 sigue negado.
- **En vivo (15:00–15:40):** el byte `[0x710F318]` vale 1 en las dos copias, así que la vida nativa de M no corre desde
  el fotograma sino desde un trabajo (`0xBE02F0` → `0x99C450`), en hilos del grupo que cambian de una llamada a otra.
  Por eso la de X ya no se llama desde el gancho del fotograma: el fotograma la marca «debida» y un gancho intermedio en
  la **entrada de `0xBB0450`** la ejecuta justo antes de la de M, en el mismo hilo y con su dt (`xmm1`), tras repetir
  las comprobaciones de identidad y de la compuerta. Resultado: unas 8 000 llamadas por minuto (todas las de M),
  «local-player part skipped» = llamadas, «rsi set 0», «NOT skipped 0», **sin cierres**. Riesgo abierto, solo para
  pruebas: que el hilo del fotograma libere X durante la llamada (las comprobaciones previas estrechan la ventana, no la
  cierran). `guest_seq_play_live` sigue a 0 en todos los perfiles.
- **Pero el menú de tienda no dependía de eso.** Con la traza de nodos (`shop_ui_trace`, ganchos de solo registro en la
  entrada del ejecutor `0x2202A60`, del paso de nodo `0x21FDE10` y de `0x2BA1420`) y una pulsación nativa en el anfitrión
  (Delkin, escena `0x1AD`):
  - nodo 1 de la escena de tienda: `InputBlock` + `DeactivateSequencerCameraFadeIn` (RTTI de los tipos de función del
    gráfico: `StageChart_Function_*`, vtables `exe+0x5D43058…0x5D43538`, `0x5D444C8`);
  - **2,0 s después**, nodo 2: `ChangePhasePanelTag` + 3 × `UIControl` + `InputBlock`. Los UIControl van por el camino
    inmediato del ejecutor (`0x2202C61`), no por el diferido: la premisa «UIControl es temporizado» era falsa;
  - el paso al nodo 2 lo hace `0x2AF1300` → `0x2BA1420(stage, …)` → `0x21FDE10`, **en el hilo del servidor** del
    anfitrión (el mismo que registra «user 2's lobby loads» y `LoadSlot (UserLogin 2490)`). `0x2AF1300` devuelve códigos de
    error y solo se llama desde código protegido.
  - En el invitado (modo X), la copia de la misma escena entra en el nodo 1 (su InputBlock corre), está en la lista
    viva de X (índice 7, `+0x491` 0, `+0x2C3` 1) y la vida de X corre cada fotograma, pero **`0x2BA1420` no se llama
    nunca para ella** y no pasa al nodo 2.
  - **Conclusión:** el menú lo hace avanzar el servidor, y el servidor solo lleva la escena del anfitrión. Ni la copia ni
    X pueden mostrarlo en el invitado. Hace falta una escena de tienda **del propio usuario 2 que el servidor ejecute**
    (la vía de `user2_stages` / opción U, sin portar a 3527/3549), o reproducir en el cliente del invitado el paso que el
    servidor le mandaría (sin estudiar: qué paquete lleva ese paso a un cliente remoto).
- **Operativa de estas pruebas:** el anfitrión arranca en su punto guardado (Eastern Hills, (-5089, -4711)); para que
  Delkin aparezca hay que llevarlo a Hernand por el mapa (`scratchpad\host_to_hernand.sh`; las coordenadas de clic son
  píxeles reales 4K, las capturas se reducen a 1100 de ancho, ×3,49). Delkin solo ofrece «R Shop» en la primera hora
  de juego tras lanzar (hacia las 12:40–13:30 del juego); de espaldas trabajando o con el otro jugador delante del
  mostrador, la interacción es otra. El modo X deja al invitado atrapado en la cámara de tienda (InputBlock de X): solo se
  sale cerrando el juego.
- Al terminar: `bin64` con el `cdmp.asi` de v9 (`80a12dd3…`) y los ini de dos copias del repositorio; 102/102 tests con
  los juegos cerrados.

**Tarde-noche del 6-nov: LOS TENDEROS FUNCIONAN en el invitado (`guest_shop_branch=5`, 3549).**
- Clave (registro de paquetes del anfitrión, cinco sesiones nativas idénticas): 2 s después de la rama 6 el cliente
  pide la rama 0 y el servidor contesta 2524 0, 2561 off y un 2981 de 29 B `2 | 0 | 0x979C909D`. El primer u32 es el
  **nodo del gráfico** al que va la escena. Nodos del gráfico del verdulero, leídos en la memoria del invitado
  (definiciones en línea, en orden): 0 reposo, 1 InputBlock + DeactivateSequencerCameraFadeIn, 2 menú, 3
  NpcStoreBuyView, 4 NpcStoreSellView, 5 otra vista de compra, 6 fundido, 7 ChangePhasePanelTag (salir).
- Modo 5 = modo 1 (copia propia de la escena) + cada petición de rama de la copia contestada como el servidor
  (`gsb::native_answer`): rama 6 → `1|6`; rama 0 a los 2 s → `2|0|tag` (menú); en el menú: rama 0 (Comprar) → `3|0`,
  rama 1 (Vender) → `4|1` (`guest_shop_sell_node`, por defecto 4, se relee del ini en cada petición), rama 3 (Salir) →
  `7|3`, y la rama 1 de después → `0|1|tag`. Esc en una ventana pide rama 0: otra vez el menú.
- **En vivo (Delkin, 17:13–18:35, cuatro sesiones):** menú «Hernand Grocer's Shop — Delkin: Buy / Sell / Leave» en el
  invitado; Comprar abre la tienda con sus existencias; **compra real** (Water, 3 cobres: `BuyStockFromStoreReq` →
  `AddItemToInventoryAck` + `VaryItemTradeCountAck` del servidor del anfitrión; dinero y existencias cambian); Vender
  abre el inventario del invitado (no se vendió nada); Salir devuelve el control. También con los perfiles del kit.
  Requisitos: invitado `guest_shop_branch=5`, `store_gate=1`, `interact_gate_types=3,4,7`,
  `guest_x_interaction_stage=1`; anfitrión `store_gate=1`. Ya están en `tools/porting/configs` y `kit/coop` (con
  `housing_share=1` en el anfitrión). Sin probar: otro tendero, vender de verdad, y la compra en puestos con este binario.
- **Cierre del ANFITRIÓN** (18:20:34, al unirse el invitado, perfiles nuevos): `exe+0x39CC7C4`, lectura de 0x48, en un
  trabajo de render (`GenerateStaticMeshRenderDataTask` / MassPlacement); volcado
  `cdmp_crash.host_20261106-182034_15568_1.dmp`. El informe de fallos del juego intentó enviarlo. Una vez en ~13
  arranques del anfitrión con `housing_share=1` y `store_gate=1`; no se ve relación con el mod.
- **Steam actualizó el juego a 1.0.0.3576** (PE `0x6AED2E85`) a las 18:37 del 6-nov, durante la última prueba. Todo lo
  anterior es de 3549; el kit v10 no llegó a empaquetarse. Siguiente paso: `python tools/porting/build_delta.py`.
- **Port a 3576 (6-nov por la noche, estático, nada ejecutado aún):** detalle en `docs/research/port-3576.md`. Listos:
  `interact_gate`, `store_gate`, C1, `guest_seq_route`, `guest_seq_foreign`, `guest_x_veto`, `guest_seq_present`,
  `guest_seq_play`, `observer_seq_copy`, el transporte de `profile_s2.h` (el `ServerActorManager` se lee por la
  búsqueda de actores, `[[0x71C0608]]`, resuelto en estático) y los desplazamientos TLS. Después, el núcleo de la
  sesión, el perfil del lector (vtable de física `0x60CD660` y bloque de cámara `+0x430`, también en estático), el
  movimiento y el resto de módulos. `supported_build()` admite 3576. Compila, 102/102 pruebas; **nada ejecutado en el
  juego todavía**. Lo que debe mirar la primera prueba en vivo está al final de `docs/research/port-3576.md`.
- **En vivo en 3576 (20:05–20:26, dos copias, perfiles del kit): FUNCIONA.** Unión, tenderos (menú de Delkin y compra
  real en el invitado), `housing_share` a nivel de paquete, despedida y vuelta del invitado. Anfitrión 0 ERR, sin
  fallos. Detalle en `docs/research/port-3576.md`. `bin64` tiene el `cdmp.asi` de 3576.

**El invitado debe salir antes que el anfitrión:** si no, pierde el progreso de la sesión.

Comparación con lo guardado de 3527, repetible con `python tools/porting/build_delta.py` (resultado en
`tools/porting/data/build_delta_3549.json`):

| Qué | Resultado |
|---|---|
| Vtables | 15.717 de 15.717 en la misma dirección |
| Funciones en `.pdata` | 248.726 en las dos |
| Catálogo Troc | 1.175 clases: opcodes, tipos y tamaños idénticos, handlers idénticos (3.525 de 3.525) |
| Globales del lector | Las mismas direcciones, por las mismas referencias de código |
| Código real, instrucciones guardadas | 19.879 de 19.897 iguales en la misma dirección. Las 18 distintas son `jmp rel32` a la sección RWX |
| Spans con hash de `guest_x_veto` | Los 5 iguales |
| Réplicas nativas (`optd_milestone_native`, `optd_apply_playback`) | Pasan contra el `.exe` de 3549 con el mismo hash del código replicado |
| Sección RWX, instrucciones guardadas | 0 de 1.301 en su sitio |

Qué cambió:
- **La sección RWX protegida se recodificó y se reubicó.** Es el mismo código con otra codificación (`48 89 F1` por `48 8B CE`)
  y cada función se movió lo suyo, en los dos sentidos; se encogió `0x1B3400` bytes y lo que hay detrás se movió con ella.
  No se ha visto ningún cambio de lógica, pero no se puede descartar: del `.exe` de 3527 solo queda lo guardado.
- **El protector renombra las secciones en cada build.** El código real se llama `.rsrc` en 2976 y 3527 y `.xtext` en 3549;
  `.text` es la RWX en 3527 y datos en 3549. `gad::in_real_code` exigía el nombre `.rsrc`, así que casi todos los módulos se
  habrían negado a instalarse. Ahora se identifica por sus flags: ejecutable y no escribible (`gad::is_real_code`).

Cómo está en el código: `Build::v3527` significa esa distribución (las dos builds) y `game::is_3549()` las distingue donde hace
falta. El saludo entre los dos jugadores ahora compara el timestamp del PE, así que 3527 y 3549 no pueden jugar juntos.
Las herramientas `read_coop_state_3527.py` y `live_reader_check.py` aceptan las dos. Solo cambia lo que apunta a la sección RWX:

| Dónde | 3527 | 3549 |
|---|---|---|
| `guest_save_host.cpp`: thunk GetOrCreate `0x2548E40` | `0xD43ED30` | `0xD33BA90` |
| `guest_save_logic.h`: `kRets` (cuatro retornos) | `0xE48A0A3`, `0xB744239`, `0xE42468F`, `0xE7C098D` | `0xE3CCAD4`, `0xB7F138A`, `0xE37C236`, `0xE6FC3CD` |
| `profile_s2.h`: thunk del refresco de ranuras `0x19B2080` | `0xB7449C0` | `0xB7F20A0` |
| `user2_funnels.cpp`: stub del usuario dueño `0x22A2880` | `0xCA26A10` | `0xC9F6CE0` |
| `user2_funnels_logic.h`: retorno de vf40 del 2215 | `0xE500077` | `0xE44CF57` |
| `mount_sync.cpp`: thunks `0xB4D8D0` y `0x23FDCC0` | `0x997A230`, `0xCD8D560` | `0x989F950`, `0xCD40720` |
| `guest_seq_present.cpp`: CameraSelector y los tres de R-A | `0x1068DEB0`, `0x8DF2F60`, `0x8DF4480`, `0x8DF44E0` | `0x104B17F0`, `0x8DBACE0`, `0x8DBC940`, `0x8DBDAA0` |
| `seq_marker.cpp`: bind (solo el rastreo) | `0x9A7F860` | `0x99BADC0` |

**Con el siguiente parche:** ejecuta `build_delta.py`. Si las vtables y el código real siguen en su sitio, añade la identidad
del PE en `build_profile.h` y los valores que imprime. Luego busca constantes en el rango de la sección RWX y comprobaciones
de 5 bytes `E8`/`E9`. Guarda una copia del `.exe` antes de actualizar: permite comparar byte a byte.

## Comprobado en vivo

| Qué | Cómo se comprobó |
|---|---|
| Unirse: el invitado carga hasta el final | Estados de carga `0x11 → 0x18` en `cdmp.guest.log` |
| Verse los dos | Capturas en los dos lados; alpha 1.0 y flags 0 en memoria |
| Movimiento en los dos sentidos | Posición igual en las dos copias, a menos de 0,1 m en 60 m a la carrera |
| Hora del mundo | El mismo `FieldTime` en los dos clientes |
| Sacar el arma | Se ve en la otra copia |
| Invocar y llamar una montura (el cierre con el guiverno) | Sin cierre; el guiverno aparece en las dos copias |
| Montar y desmontar, en los dos sentidos | `MountBegin`, `MountCommit`, `MountApply: APPLIED` y captura del jinete en la silla desde la otra copia |
| Cabalgar | La montura y el jinete siguen al dueño en la otra copia |
| Viaje rápido del invitado | `user2_fast_travel` usa el gestor del invitado; carga en unos 14 s y el anfitrión lo ve llegar |
| Viaje rápido del anfitrión | Los dos quedan a 1 m en el destino, cada uno ve al otro |
| Golpear a NPC desde el invitado | `AttackByCollisionReq` y sus `PostProcessAttackResult`; `combat_follow` los sigue |
| Pelear con enemigos de verdad (Valvegard Garrison) | Los golpes del invitado llegan igual a las dos capturas (52 a `0xB0100586`, con 47 eventos de daño). La IA del anfitrión pasa a 8 enemigos al invitado (`CurrentTargetChangeReq`). Le hacen daño, lo matan, sale la pantalla de muerte y `E` lo revive en el sitio con la vida llena, sin cierres |
| El invitado mata a un enemigo | Vida del mecha `0xB0100580` de 160 a 0 (leída en memoria), `NotifyHandleDeadAck` con asesino `0xA0100066`, dos muertes y `User2Funnels dead … DeadActor goes to character 0xA0100066` |
| Steam: API, relevo, envío, recepción y saludo | Dos copias con `transport=steam` y el invitado apuntando a su propio SteamID: relay status 100, `connected`, más de 7.000 mensajes en cada sentido, 0 errores, rtt 0,3 ms. El invitado habla consigo mismo: no prueba NAT ni la ruta entre dos PC |
| Los menús no pausan el mundo | `UiPause`: el reloj del juego sigue a velocidad real con el menú abierto |
| Compresión del túnel | `in compressed batches` sube en el invitado |
| El invitado sale con Exit y el anfitrión sigue | Barrera C4 con las cuatro comprobaciones en `pass` y `logout complete` |
| El invitado vuelve a entrar tras cerrar el juego | Proceso nuevo, mismo anfitrión: `epoch 2`, los dos se ven, misma hora |
| El invitado vuelve al título y pulsa Jugar | Mismo proceso: `epoch 3` en la misma conexión |
| El invitado desaparece de golpe (proceso matado) | El anfitrión sigue y el invitado vuelve a entrar |
| El anfitrión sale con el invitado dentro | Sin volcado de cierre en ninguno; el invitado sigue en el mundo y puede salir |
| Escalar | `remote_climb`: mismas acciones y posición en las dos copias (`Enter calls 22, forced 22`) |
| El invitado muere y revive | Pantalla de muerte; `U` lo lleva al punto de control con la vida llena en unos 7 s |
| El anfitrión muere y revive con el invitado dentro | Pantalla de muerte; `U` (punto de control, vida llena) y `Q` (en el sitio, 30 %). Antes se quedaba muerto sin salida |
| El invitado juega con su propia partida | Sube su ranura al entrar (3,3 MB, SHA-256 comprobado) y el servidor la carga como ranura 204 (`REMAP 204`) |
| El progreso del invitado vuelve a él | Al salir: `SavePlayerToSlot` en la ranura 204, `gen1` enviada y guardada; `aplicar-progreso-invitado.ps1` la aplica y la sesión siguiente la sube como `base gen 1` |
| Sin fuego amigo entre jugadores | `AttackByCollisionReq` entre actores de dos usuarios se descarta en el servidor: la pelea de los mercenarios gemelos baja de cientos de proyectiles a 3 o 4 |
| Los enemigos dan permiso de ataque al invitado | `rank_permit=1`: `vf188(5)` del invitado en `0x23F2E51` pasa de 0 a 1, 18 veces en una pelea |
| Una muerte causada en una Req del invitado va al invitado | `kill_loot_per_user=1`: `User2Funnels dead … DeadActor goes to character 0xA0100066` |
| Gestor de misiones propio en las Req del invitado | `quest_per_user=1`: `vf40 … user 2's 4` de 20212 llamadas (las demás no pasan por él: ver abajo) |
| Niveles de escena compartidos | `scene_level_share=1`: `kept 4` niveles que el otro jugador seguía teniendo |
| Gimmicks acoplados | `docked_gimmicks=1`, invitado: 8 registros reescritos para que los controle su dueño |
| Estabilidad | 35 min seguidos con invocaciones y monturas, y otros 15 min de prueba larga, sin cierres |

## Comprobado solo en parte

- **Por Steam:** el modo Steam arranca (API resuelta, SteamID leído) con los inis del kit. Nunca se ha conectado
  un invitado por Steam en esta build: hace falta una segunda cuenta y otro PC.

## Implementado, sin comprobar en vivo

- Pelea en dos PC. Para repetirla, el nexo junto a Valvegard Garrison (-4710, -5348) deja a los dos en medio de ella.
  `SpawnCharacterCheatReq` no sirve para crear enemigos: echó al invitado del mundo.
- `observer_scene_copy` (el 2810 de las escenas propias del invitado): portado e instalado, pero no ha salido el caso.
  Examinar un documento del inventario ya no crea secuenciador en esta build, y no se ha encontrado un objeto que lo haga.
- `scripts/host_save_restore.ps1` sobre la carpeta real: solo con las pruebas de `tests/host_save_restore_tests.ps1`,
  que van contra `%TEMP%`. No se ha ejecutado contra la carpeta real.
- Que el contenido de la partida del invitado cambie de verdad: el fichero cambia y se carga bien, pero la posición no
  sirve de prueba porque entrar lo coloca junto al anfitrión.

- `catch_seq` (agarres y remates): portado e instalado, pero nadie ha hecho un agarre.
- Efecto en el juego de `scene_level_share` (abrir una puerta tras cargar primero el invitado) y de `docked_gimmicks`
  (usar el gancho): los módulos actúan, pero no se ha mirado el resultado en pantalla.
- Botín: `kill_loot_per_user` está activo. Los enemigos muertos en la prueba no soltaron objetos (`LootDropCharacterRewardListAck`
  vacío), así que no se ha visto ninguna línea `loot`.
- **`housing_share` (nuevo, anfitrión, 6-nov):** casas y muebles del anfitrión en el invitado. Al entrar, el servidor manda
  a cada cliente el juego de su personaje (visto en vivo en las dos copias, volcado entero en `bin64\cdmp_housing_<pid>.txt`
  por el sniffer): `SyncPlayerHouseDataListAck` (sus casas: casa `0xF4342` → escena de vivienda `0xF4416`),
  `AddHousingGimmickAck`, `AddHousingPlacedFieldGimmickSaveKeyListAck` (por casa, claves de guardado de los gimmicks puestos),
  `SetLoadedHousingObjectSyncDataIdMaxAck`, `SyncHousingPlacedSceneObjectListAck` (por escena, los objetos colocados:
  registros de 0xA0 B con celda de la cuadrícula, clave del objeto, posición local, giro y escala; en partes de hasta 32 KB)
  y `SyncHousingPlacedPathDataListAck`. El invitado es el usuario 2 con SU partida (ranura 204): recibe sus casas, nunca
  las del anfitrión. Con 1, en `take_frame` cada paquete de ese juego para el usuario 2 se descarta y en su lugar se
  encolan los últimos del cliente del anfitrión del mismo opcode (`server_push_extra`), con la clave del personaje del
  anfitrión cambiada por la del usuario 2; los cambios en vivo del anfitrión (colocar, quitar, vaciar...) también se le
  mandan. Lógica pura y tests en `housing_share_logic.h` / `optd_housing_share`. En dos copias del mismo PC los dos juegos
  son iguales (la partida del invitado es copia de la del anfitrión): la prueba de verdad es la de dos PC, o colocar algo
  con el anfitrión durante la sesión. Sin probar: qué hace el cliente del invitado si pide datos de vivienda al servidor
  (las respuestas de su juego se cambian por las del anfitrión) ni si colocar algo con el invitado en casa del anfitrión
  funciona.
- **`housing_share` en 3576 (6-nov, 20:05–20:45):** el anfitrión encola al usuario 2 los seis Acks de su entrada
  con la clave cambiada, y el volcado del invitado (`cdmp_housing_<pid>.txt`) muestra que su cliente los recibe tal
  cual con la clave `0xA0100066`, en la unión y en la vuelta. La prueba VISUAL en un solo PC es posible con un cambio
  en vivo (el anfitrión coloca o quita un mueble con el invitado mirando: la partida del invitado no lo tiene), pero
  falta saber dónde está la casa del anfitrión (casa `0xF4342`, escena `0xF4416`): el mapa no tiene filtro de casas,
  los menús no la muestran y las posiciones de los registros 2858 son locales a la casa (4–10 m). Siguiente paso:
  que el usuario diga dónde está su casa, o la prueba de dos PC con el kit v10.
- **Prueba con el usuario (6-nov, 20:45–20:56):** la casa es `KliffHouse_Pailoon_0001` (tabla `HouseInfoManager`,
  clave de casa `0x424A`), cerca de Delesyia, en (-4710, 385, -3976). El invitado entró (los seis Acks de la entrada
  re-encolados con su clave). Al abrir el anfitrión el modo de diseño de la casa, su cliente recibió
  `UpdateWaitingCustomHousingGimmickSyncDataListAck` (2500: los ids de sincronización de los objetos que su PROPIO
  cliente crea para el modo); reenviado al invitado, **el invitado se cerró 72 ms después** (`exe+0x3D7FB6`, liberar
  una referencia por un puntero basura; volcado `cdmp_crash.guest_20261106-205310_40916_1.dmp`). El invitado SÍ tenía
  la guarda de informes K1 (`exe+0x14F7E00`, desde el primer segundo), pero K1 solo corta los informes con archivos;
  los volcados de fallo van por Sentry/crashpad (K2, `scripts\crashpad.ps1 -Off`), que estaba puesto: el volcado
  quedó en `%LOCALAPPDATA%\Pearl Abyss\DumpCache
eports` (pendiente a las 21:00; crashpad lo sube si se vuelve a
  abrir el juego con él puesto). La pantalla del anfitrión se quedó
  negra en el modo de diseño hasta cerrarlo (no se sabe si por el mod o por la caída del invitado).
  Arreglo: `housing_share=1` da ya solo el juego de la entrada; los cambios en vivo solo con `housing_share=live`
  (inseguro). Kit `dist\cdmp-coop-20261106-3576-v11.zip`; el v10 renombrado `-DO-NOT-USE`. **El usuario confirmó
  (21:05) que el invitado VE en pantalla los muebles del anfitrión: `housing_share=1` funciona en 3576.**
  Registro de paquetes del anfitrión de esa sesión: el 2500 llega también al acercarse a la casa (215 s, con
  `SpawnPlacedHousingGimmickFromSyncDataReq` y `SpawnOrUnspawnHousingGimmickAck`), no solo al abrir el modo de diseño
  (575 s): con los cambios en vivo reenviados, el invitado se caería en cuanto el anfitrión cargase su casa. El
  servidor no vuelve a mandar el juego de la entrada en la sesión. Pendiente: K2 (`crashpad.ps1 -Off`) antes de las
  pruebas; qué cambios en vivo (colocar / quitar) son seguros, nunca el 2500; la pantalla negra del anfitrión en el
  modo de diseño (probarlo sin invitado).
  Manejadores de cliente en 3576 (ranura 2 de la vtable Troc, por RTTI): `PlaceHousingItemAck` `0xCBFE30`,
  `RemoveHousingGimmickAck` `0xCF7160`, `PushHousingObjectToInventoryAck` `0xCC1330`, `DiscardHousingGimmickAck`
  `0xCF5280`, `ClearHousingSyncDataAck` `0xCC0590`, `RemoveHousingPlacedFieldGimmickSaveKeyListAck` `0xCBCFD0`, 2500
  `0xCC08F0`, `SpawnOrUnspawnHousingGimmickAck` `0xCAFF20`. Todos son el mismo envoltorio (deserializar y mandar el
  trabajo al hilo del juego); lo propio de cada uno está en su función de trabajo (colocar `0x458690`, vaciar
  `0x458DA0`, 2500 `0x458F50`). Para los cambios en vivo hay que leer esas funciones (¿buscan objetos por id de
  sincronización que solo existen en el cliente del anfitrión, como el 2500?) y probar cada mensaje por separado con
  K2 puesto.
  Camino del fallo (volcado del invitado, pila): el tick `0x9EFCB0` llama a `0x462DD0` (rcx = `[obj+0x210]`, el
  contexto de vivienda; edx = un índice de entrada) y este, al poner un objeto nuevo en `entrada+0x38`, suelta el viejo
  con `0x3D7FA0`: el viejo era basura (`0xC5914EC1C57E2B4A`). El 2500 deja al invitado con entradas «en espera» que su
  cliente nunca creó (las crea `SpawnPlacedHousingGimmickFromSyncDataReq` en el cliente que pide): todo mensaje en vivo
  que nombre objetos por estos ids de cliente es igual de peligroso.
  Funciones de trabajo de cada manejador (3576): colocar `0x458690` (0x702 B: `0x460AA0`, `0x4A2890`, `0x451940`...),
  a inventario `0x4593D0`, vaciar `0x458DA0`, 2500 `0x458F50` (lista de espera: `0x4A2AC0`, `0x4A93A0`), quitar
  `0x99DD00` / `0xACEBD0`, descartar `0xACE7B0`, quitar claves de campo `0x544E20`, aparecer/desaparecer `0xA4C970`.
  Si colocar acaba en la misma lista de espera no se ve en estático: la prueba es en vivo, de un mensaje cada vez.
  **Preparado para esa prueba (sin juego, 6-nov por la noche):** `housing_share=live` reenvía solo los cambios de
  `housing_share_live_ops` (opcodes lógicos separados por comas; vacío = todos menos el 2500); el 2500 no se reenvía
  nunca, lo diga o no la lista (`hsh::forward_live`, `hsh::parse_live_ops`, con pruebas). Orden propuesto, en el
  `cdmp.host.ini` y con `scripts\crashpad.ps1 -Off` antes: `2623` (colocar), después `2859` (quitar), `3087` (a
  inventario), `3085` (descartar), `2398` (vaciar). Cada vez: invitado dentro de la casa, el anfitrión hace solo esa
  acción en el modo de diseño, y se mira la pantalla del invitado y su registro.
- **Casas, noche del 7-nov (3576, dos copias, K2 puesto: `crashpad.ps1 -Off`; cinco sesiones):**
  - Cómo llegar: la casa del anfitrión es «Old Bloomscent House» (junto al Delesyia Dyehouse). Nexo en
    (-4740, 368, -3836) y 140 m a pie hasta (-4710, -3976); en la casa, Esc abre el modo de construcción (icono de casa
    con «Esc»). Script de la sesión: `to_house.sh` (mapa a 4 muescas = 0,622 m/px, norte arriba; el pin de mapa,
    `CreateOrChangePinMarkerReq`, lleva x y z del mundo y sirve para calibrar).
  - **Pantalla negra:** el modo de construcción del anfitrión no se pone negro, ni solo ni con el invitado conectado
    (sesiones 2, 3 y 5). La del 6-nov fue consecuencia de la caída del invitado, no del modo.
  - **Construir no manda nada objeto a objeto:** al confirmar, el CLIENTE del anfitrión manda la lista entera del escenario
    editado, `SyncHousingPlacedSceneObjectListReq` (lógico 2408, partes de hasta 32 KB `[flags][escenario][registros]`), y
    el servidor solo contesta con Acks pequeños (coste, `RemoveHousingGimmickAck` 2859, `SyncAndConsumeCustomHouseSyncDataAck`
    2187). El Ack de la entrada 2733 es la misma lista con la clave del dueño tras el byte de flags. Los muebles (modo
    «Place Furniture», F) sí van uno a uno: `PlaceHousingItemReq` → `PlaceHousingItemAck` (2623), y al salir del modo el
    cliente vuelve a pedir sus gimmicks (2492/2446 → 2500 con un id más). Nuevo `housing_share_edits=1`: convierte cada
    2408 del anfitrión en el 2733 del usuario 2 y lo guarda para su próxima entrada (funciona: «2 of 2 part(s) QUEUED»).
  - **Por qué se caía el invitado:** con la clave cambiada (`rekey`) el cliente del invitado es DUEÑO de la casa del
    anfitrión y hace la parte del dueño sobre objetos que solo creó el cliente del anfitrión. Sesión 1: al acercarse pide
    él mismo los gimmicks (2492 + 2446), el servidor le da su lista de espera (2500, ids 0x80…) y cae ~20 s después a 80 m
    (`exe+0x462BDA`, función `0x462730`, entrada del id 0x84 vaciada al soltar el objeto que sustituye con `0x3D7FA0`).
    Sesión 2 (con 2492/2446 del invitado bloqueados): cae al abrir el anfitrión el modo de construcción, tras la ráfaga de
    `RemoveActorAck` de los gimmicks del anfitrión y su propio `UpdateHousingModeEnableReq` (`exe+0x4632EA`, función
    `0x462DD0`, id 0x80). Sesión 4 (re-clave solo de 35245 y 2733): ve la casa entera (muros, tejado, balcón) y cae igual
    (`exe+0x46313D`). El volcado de texto de paquetes del invitado se corta antes de la caída; el `.bin` está completo
    (`bintail.py` de la sesión).
  - **Lo que queda (por defecto desde esta noche):** `housing_share_rekey=0` (la casa sigue siendo del anfitrión) y
    `housing_share_guest_spawn=0` (se tiran los 2492/2446 del usuario 2 en `inject`). El invitado ve los gimmicks que
    genera el cliente del anfitrión y el servidor muestra a todos: puertas, muebles, adornos, mascotas; NO los muros ni
    suelos (las puertas flotan). Sin caídas al acercarse, al entrar y salir el anfitrión del modo de construcción (dos
    veces) ni al confirmar cambios. **Comprobado en pantalla: una diana colocada por el anfitrión apareció junto al
    invitado.** `housing_share_rekey=1` / lista y `housing_share_guest_spawn=1` siguen disponibles para investigar.
  - Para ver los muros sin caídas habría que entender la tabla de entradas del contexto de vivienda del cliente
    (`[ctx+0xC0]`, entradas de 0xC8, `+0x68/+0x70` lista de objetos) o dejar al invitado dueño pero sin que reaccione a los
    gimmicks del anfitrión. Sin hacer.
  - Kit `dist\cdmp-coop-20261107-3576-v12.zip` (perfiles regenerados, README de casas reescrito). `scripts\build.ps1` ya
    pasa las 102 pruebas también lanzado desde PowerShell 7 (las dos pruebas de script fallaban por heredar su
    PSModulePath, no por el juego abierto); `scripts\crashpad.ps1` ya no confunde el `crashpad_handler.exe` de otros
    programas (TeamViewer) con el del juego. Crashpad queda APAGADO; sigue pendiente el informe del 6-nov 20:53 en
    `DumpCache\reports`.
  - **Muros para el invitado (madrugada del 7-nov, sesiones 6 y 7):** los registros del 2733 son de 160 B
    `[id de sincronización u32]...`, con la categoría en `+0x25`: 0 = estructura (suelos, muros, tejado, caminos; nunca en
    una lista de espera), 4 = lo que el cliente genera como gimmick (puertas, muebles, contraventanas; los 11 ids del 2500
    del anfitrión eran todos de categoría 4), 2 = uno más; al final de la última parte, huecos con id `0xFFFFFFFF`. La
    lista 2408 del cliente y el 2733 de la entrada son la misma salvo redondeos de los giros (comprobado byte a byte).
    Nuevo `housing_share_drop_gimmicks = 0 | all | waiting`: el 2733 que recibe el usuario 2 sin los registros de
    categoría 4. Con `housing_share_rekey=35245,2733` + `drop_gimmicks=all` + `guest_spawn=0` el invitado **ve los
    muros, el balcón y los marcos de las ventanas** (dueño de la estructura, sin gimmicks propios; los del anfitrión le
    llegan como siempre) y **no se cae** al entrar el anfitrión en el modo de construcción (tres veces, sesión 6; una,
    sesión 7) ni al recibir los cambios (2733 en vivo, 43 868 B: 15 registros menos). Los cambios del anfitrión se ven al
    **volver a entrar** el invitado (dos vueltas al título: el hueco de un muro derribado y la losa colocada aparecen);
    en vivo el 2733 llega pero el cliente no reconstruye (ni alejándose 190 m, ni reenviando el
    `SyncAndConsumeCustomHouseSyncDataAck` 2187 con `housing_share=live` + `live_ops=2187` + clave cambiada). Un cierre del
    invitado sin explicar en la primera sesión 7 (`exe+0xB5C095`, función `0xB5C050`, lectura de `[nulo+0x115]`, en el
    nexo, 30 s tras entrar, sin ningún paquete de vivienda en los 30 s anteriores): por eso esta combinación queda como
    experimento y el kit sigue con `rekey=0`.
- **Tiendas en 3576 (7-nov, madrugada):** **compra en puesto del mercado comprobada en 3576** (el invitado, junto a la
  tienda de Delkin en Hernand: «Box», mantener E → `InteractGate PRESS SWAP`, `InteractionPreemptionStep` y `DoStep`
  aceptados, `RemoveItemToInventoryAck` 28 cobres, `AddItemToInventoryAck`; la caja desaparece del puesto). Vender a
  Delkin: sin probar todavía; Delkin solo atiende de 12:40 a 13:30 de juego (~5,6–9,9 min reales tras entrar el
  anfitrión en el mundo, reloj ×11,5) y el invitado tarda en unirse y llegar. El invitado reaparece en su ÚLTIMO nexo
  (se guarda al volver al título). `scratchpad map_tp.py`: teletransporte por el mapa calibrado con pines (varios
  puntos si cae en un icono; el mapa abierto se reconoce por la barra de teclas de abajo).
- **Kit v13 (7-nov, mañana): muros activados por defecto** (`housing_share_rekey=35245,2733` +
  `housing_share_drop_gimmicks=all` + `guest_spawn=0`), a petición del usuario: con v12 el invitado veía los muebles
  flotando. `dist\cdmp-coop-20261107-3576-v13.zip`, `cdmp.asi` `7916dd02…` (el mismo que `bin64`). La caída del
  invitado de las 05:41 (`exe+0xB5C095`) **no es código de vivienda**: función `0xB5C050`, llamada desde `0x9481E9`
  (zona de cámara/UI del jugador, junto a `0x948760`), lee `[[rsi+0x88]+0x10]+0x115` con ese puntero de actor nulo
  (compara al jugador con otro personaje; el otro argumento sí se comprueba). Las otras cuatro caídas del invitado de
  esa noche (`0x462BDA`, `0x4632EA`, `0x46313D`, `0x3D7FB6`) eran del código de vivienda con la casa entera como
  propia (`rekey=1` o sesión 4 sin `drop_gimmicks`). Vender a Delkin: sigue sin probar (el lanzamiento de las 06:00 se
  quedó esperando una tubería que heredaron los juegos; el de las 08:33 perdió al anfitrión, «종료 이벤트 발생» del
  propio juego 2 s después de arrancar el invitado, sin volcado).

## Problemas conocidos

- Las misiones pueden mezclarse. Con `quest_per_user=1`, los eventos de juego (`0x2B79B6B`) no son un llamador permitido
  de `vf40`, así que las muertes e interacciones del invitado siguen avanzando el gestor del anfitrión. Es el diseño de
  S34 y la lógica se comparte con 2976; no se ha cambiado.
- `UpdateOnDemandEquipDockingNak` llega unas 10 veces por segundo por personaje en co-op. Cada cliente vuelve a pedir
  los dispositivos del otro, `0x2B0BF80` no tiene nada que crear y responde `eErrNoFailedToSpawnDockingActor`. Es ruido de
  red, sin efecto visto, y ningún módulo portado lo arregla.

- Los mercenarios gemelos (invitado con una copia de la partida del anfitrión) aún disparan alguna vez al encontrarse,
  sin daño. No se sabe por qué se eligen: no hay `CurrentTargetChangeReq` entre ellos.
- `F4` deja en el registro una línea rara `Req 35907`. Es la misma acción; solo es cosmético.

## Portado, sin ejecutar nunca

Compila y pasa las pruebas, pero nadie lo ha lanzado: la única evidencia es la lectura del ejecutable. Todo sigue apagado
en los perfiles y **no está en el kit** `dist/cdmp-coop-20261105.zip`, que es de antes. Probar en dos copias, con copia de
las partidas, porque un cierre del juego se envía a Pearl Abyss.

**Cuerda y árboles** (`remote_rope=1`, `tree_bend=1`, los dos lados). Enganchar un árbol, tirar, soltar y volver a lanzar.
Salen `RemoteRope: hooked at their entries`, `TreeBend: hooked at their entries`, `remote thrower, handle TRANSLATED ->
WRITTEN`, `2471 SENT ToNear(thrower)` y `TreeBend #n applied`. No debe salir `FAULTED`. Vigilar `BUSY` en el resumen: un 2471
al lanzar puede cortar la cuerda en el aire. Ninguna captura de 3527 contiene mensajes de cuerda.

**Tenderos y NPC de escena en el invitado.** Hoy faltan o están quietos al entrar en una ciudad; las escenas del anfitrión
no pueden apoderarse del cliente del invitado solo porque esto está apagado. Encenderlo trae ese riesgo. Nunca
`guest_seq_route=1` sin `guest_x_stage_veto=1`. Por orden:

1. Anfitrión: `observer_seq_copy=1`, `observer_seq_copy_2322=1`.
2. Invitado, a la vez: `guest_seq_route=1`, `guest_seq_foreign_player=1`, `guest_seq_play=1`, `guest_x_stage_veto=1`,
   `guest_x_ui_veto=1`, `guest_seq_foreign_present=probe`, `guest_seq_foreign_timeline=probe`.
3. `guest_seq_foreign_present=1`.
4. `guest_seq_foreign_timeline=1`.

Al entrar en una ciudad: `[trip] OBSERVER` y `[copy] ... SENT` en el anfitrión; `GuestSeqRoute [2661 ...] ROUTED`,
`GuestSeqPlay [3]` y `GuestSeqForeign [foreign]` en el invitado. En una charla con un tendero o una escena del anfitrión:
`[R-A CUT]`, `[SKIPPED]` y `GuestXVeto [interface] VETOED`, y el invitado conserva su cámara y su menú.

**Parar si sale** `RESTORED`, `WRITE FAILED` o un `[R-A CUT]` con G = M.

**Dudas abiertas:**
- Que `[ctx+8]` sea `0x6042EF0`. Si no lo es, R-B no omite nada y la cámara del anfitrión se ve en el invitado.
- Que los ids `node+0x30` sean correctos.
- Que el cambio de vptr de R-A no choque con código que compare el vptr.
- `hold_scene_order_logic.h` aún supone el prefijo 91 (está apagado).
- `0xBA2A30` corre ahora en el tick de X y solo se ha leído en parte.

## Sin portar a esta build

Siguen desactivados en el perfil. Cada módulo se niega a instalarse si no reconoce la build, así que no
pueden cerrar el juego.

- Etapas propias del invitado (`user2_stages`).
- Control de NPC por el invitado (`user2_npc_control`, `npc_activation`).
- Tiendas y carros (`shop_*`, `user2_road_*`): las sondas y rutas de compra de S46 (la cadena de escenas sí está en marcha).
- Clima (`weather_sync`): **portado a 3549 el 6-nov** (ver arriba); 3527 sigue negándose. Lo de 3527 era: los bloques se movieron, hay un float nuevo sin ubicar y acumuladores
  nuevos que no están en el formato de red, y no se encontraron la ranura de M, la vtable de X ni las puertas del
  simulador. Lo encontrado está en `weather_sync_logic.h`.

## Cómo se guarda el progreso

Durante la partida nada escribe en la carpeta real: todo va a `cdmp_save_sandbox`.

- **Invitado** (`guest_own_save=1`): al salir por el menú, el anfitrión guarda su personaje en la ranura 204 y le
  devuelve el resultado. Queda en `%LOCALAPPDATA%\cdmp\guest_progress\<anfitrión>\incoming\gen<n>`. Con el juego
  cerrado, `tools/scripts/aplicar-progreso-invitado.ps1` lo aplica a su ranura y guarda antes una copia.
- **Anfitrión** (`host_save=probe`): al arrancar el juego, el sandbox anterior se exporta a `cdmp_save_exports`. Con el
  juego y Steam cerrados, `scripts/host_save_restore.ps1 -Generation <nombre> -Slot <n>` lo comprueba, y con `-Confirm`
  lo copia a la carpeta real, tras hacer copia de seguridad.
- **Riesgo sin medir** (auditoría S34, M2): una partida del anfitrión bien atribuida puede llevar estado global cambiado
  por la carga del invitado. Con `quest_per_user=0`, además, misiones y botín pueden mezclarse. Restaurar es decisión
  del usuario.

## Por qué se cerraba el invitado

Los dos cierres (`exe+0x22485D5` y `exe+0xA7ADDC`) eran copias remotas de NPC con la capa 0 vacía: el invitado
las recibía antes de su primera acción. Los *holds* (`child_hold`, `observer_hold`, `character_hold`) existen
justo para eso, pero en 3527 estaban apagados y `child_hold.cpp` se negaba a instalarse. Con los *holds*
encendidos, los contadores de las guardas de `horse_diag.cpp` se quedan en 0.

Se comprobó que los offsets de los que dependen siguen valiendo en 3527: padre en `+0xAC` y `hierarchy+0x1C`, sin
discrepancias en 682 actores vivos.

## Por qué el anfitrión no tenía pantalla de muerte

Con un invitado conectado, el anfitrión moría y se quedaba sin interfaz: ni `U` ni `Esc` respondían. Los mensajes
de red hasta ese momento eran iguales que en una muerte sin invitado, así que la causa estaba en el cliente.

El gestor de secuenciadores propio (`pa::ClientSequencerStageManager`) tiene un marcador de carga en `+0x259`
(`M+0x231` en las notas de 2976). Cada viaje de la máquina de carga (`P+0x15C`) lo lleva por 0 (`0x856B67`), 1
(`0x8571CE`), 2 (`PlayAllPreLoadSequencerAck`, `0xC9CB9B`) y 3 (`0x9A86A49`). Por debajo de 2, `0xBA2F70` se salta
el arranque de secuenciadores, y la pantalla de muerte cuelga de uno de ellos.

**La causa:** cuando termina de cargar el personaje de otro jugador en un cliente, se inicializan sus componentes
(`0xB4AD380`, `vf+0xB0` de cada uno). El de secuenciador (`0xCD03720`) enlaza el campo con dos gestores: primero el
que devuelve `field->vf48()`, que es el del jugador local, y luego el suyo. Ese enlace (`vf+0x20`, `0x9A7F860`, por
el `jmp` de `0xB9FD80`) guarda el `ClientField` en `+0x20` y deja el marcador en 1. El juego solo lo espera durante
la carga del propio personaje. Con dos jugadores, también le llega al jugador local cuando entra el otro. Se capturó
la pila con `seq_marker_trace=1`:

```
bind manager local, marcador 3, hilo de trabajo: exe+0xCD03758 exe+0xB4AD434 exe+0x99BEBF exe+0x15D5057 ...
bind manager del otro, marcador 0:               exe+0xCD03783 exe+0xB4AD434 exe+0x99BEBF exe+0x15D5057 ...
```

Por eso en el anfitrión el marcador cae de 3 a 1 con la carga parada, cuando entra el invitado, y en el invitado cae
de 2 a 1 en el estado `0x11`, cuando le llega el anfitrión: es lo que vio Codex como "la inicialización lo devuelve a
esperando". Escribir un 3 a mano en una sesión viva devolvió el evento (1,2 s tras morir), la pantalla y el revivir.

`session2/seq_marker.cpp` (`seq_marker_guard=1`, los dos lados) vuelve a poner el valor si cae con la carga parada o
esperando en `0x11`. Las reglas están en `seq_marker_logic.h`, con pruebas en `tests/optd_seq_marker_tests.cpp`. No
hay gancho: es un hilo que mira 20 veces por segundo y escribe solo ese byte.

También se vio que, mientras el marcador estaba caído, los eventos de otros secuenciadores del anfitrión dejaron de
enviarse hasta que el invitado se fue. Es probable que esto arregle también otras escenas del anfitrión, pero no se
ha comprobado una por una.

## Cómo portar otro módulo

Casi todos los módulos bloqueados se portan con una tabla de direcciones por build. Los inicios de función rara
vez cambian y los `jmp` a `.xtls` sí llevan otro destino. Cada dirección nueva necesita:

1. Pruebas en el desensamblado del ejecutable de 3527 o en memoria viva.
2. Comprobación de bytes antes de instalar el gancho (`verify`, `install_one`).
3. Prueba en vivo y una línea en este documento.

`tools/porting/data/signature_mapping_2976_3527.json` sirve de punto de partida, pero `unique_candidate` no es
una prueba. Los campos que más se han movido:

| Qué | 2976 | 3527 |
|---|---|---|
| Métodos del controlador de movimiento | `mc+0xF8` | `mc+0x140` |
| Método actual | `mc+0xF0` | `mc+0x13C` |
| Clave del caballo en `RideOff` | `+0xD4` | `+0xD0` |
| Búfer de posición | `mc+0x3D0` | `mc+0x430` |
| Búfer de rotación | `mc+0x488` | `mc+0x4E8` |
| `FieldMover` | `TS+0x4F0` | `TS+0x508` |
| Byte de recarga del gestor de escenas | `+0x6A0` | `+0x6D0` |
| Reloj de fotograma | `0x6C8CD68`, `0x6C8CD70` | `0x70D9E60`, `0x70D9E70` |
| Sesgo cliente-servidor | `0x6D69438` | `0x71BE198` |

## Cómo se prueba una montura

1. Inventario → Montura especial → clic en la fila. Eso ya la invoca; Espacio la vuelve a retirar.
2. Cerrar el menú y pulsar `H` para llamarla.
3. Acercarse hasta que salga `E Montar`. `R` desmonta.

Sin montura invocada, `H` no envía nada.

## Trampas al automatizar pruebas

- Los diálogos de Salir y Volver al título piden **mantener** Espacio unos 2,5 s. Teletransportarse basta con
  pulsarlo.
- En el mapa, Teletransportarse es doble clic en el punto y confirmar con Espacio.
- En la pantalla de muerte hay que **mantener** unos 3 s: `U` revive en el punto de control y `Q` o `E` en el sitio
  (gastan un objeto).
- Si lanzas el juego desde un script, no redirijas su salida a una tubería sin lector: el juego escribe mucho por
  consola y se bloquea al llenarla. Redirígela a un fichero.
- Un rejoin justo después de salir cae en una ventana de 60 s en la que el anfitrión espera al proceso anterior.
  Eso se cerraba en un punto muerto y está arreglado en `tunnel_tick_reconnect`.
