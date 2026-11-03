# Opción D: diario de trabajo

Diario de la sesión autónoma (noche del 2026-10-05 al 06). Lo más reciente de cada sección va
arriba. Build del juego: 1.0.0.2976.

## S33 — sondas y contención local autorizadas (2026-10-14)

Trabajo aislado en `opcion-d-npc-s33`, desde `c7ae132`, sin alterar el checkout compartido. Implementadas `npc_activation`, `npc_family_control` y `user2_road_control`, todas predeterminadas a `0`. Sólo `npc_activation=1` retira una unidad sin progreso con su familia, limita reposiciones por nodo/generación y evita su aborto global por falta de acción. Familia y viajeros permanecen en sonda; B no se da por resuelto. Revisión independiente y `scripts/build.ps1`: 65/65 tests sin juego; ninguna prueba en vivo/despliegue.

Evidencia y límites: [causas S33](s25/s33-npc-causas-y-diseno.md). Implementación, hooks y criterios: [sondas y contención](s25/s33-npc-implementacion.md). INI completos de dos pasos enlazados allí (1: tres sondas; 2: sólo contención activa). Pendiente: medir primero plan seco con Shadow, luego retirada local; confirmar scopes nativos, latencias y límite heredado de espera de 200 ms.

## Para leer al despertar (2026-10-10)

- **Estado:** con el 2322 nativo, el invitado real ya entra en el mundo (sesión 6). Esta noche:
  - arreglada la corrupción del heap del anfitrión;
  - salida limpia del invitado;
  - cierre de sus escenas propias;
  - primeros paquetes de los NPC en orden;
  - retención del personaje;
  - túnel comprimido (×9);
  - menús sin pausa mientras hay invitado.
  
  Todo está probado en un PC y pasó una revisión independiente. Kit de la sesión 7 listo (`cdmp.asi` `90C1D238…`).
- **Necesita Shadow (sesión 7):**
  - comprobar en vivo lo anterior;
  - medir S1 (sondas H1–H3/G1–G4);
  - repetir el ESC del anfitrión y del invitado con el invitado dentro;
  - salir, volver a entrar y reconectar.
- **Investigado para la etapa siguiente** (`pendientes/p8-sistemas-juego.md`):
  - inventario, equipo, tienda, dinero, diálogo y habilidades son por usuario de forma nativa;
  - misiones, escenas, botín, muerte y la hora pasan por tres "embudos" del dueño del campo (el anfitrión);
  - plan de pruebas G1–G10 y riesgos R1–R16.
- **Necesita decisión tuya:** S1, opción A de `pendientes/p8-s1-escenas-npc.md`. Consiste en duplicar hacia el usuario 2 los paquetes de escena del campo entero y colocarlos en el gestor del Kliff del anfitrión en el invitado. Recomiendo decidir después de las medidas de la sesión 7.

### Kit 35: arreglos de la auditoría S34, fases A y E (14-10)

Rama `opcion-d-s35` (worktree `C:\Emulador CD-s35`), sobre `opcion-d-npc-s33-fix` + la auditoría
(`s25/s34-investigaciones/AUDITORIA.md`). Plan de cinco fases (A cierres/contención, B familias, C viajeros, D rearmado,
E guardado) y cuatro kits (35: A+E, 36: B, 37: C, 38: D), cada uno con prueba en un PC y dos pasos en Shadow.
- **Fase A** (`s25/s35-fase-a.md`): evidencia H1b sin el emisor mal leído, contención que revalida y no retira por
  silencio, degradación sin el aborto global de 5 s y `publication_gate` (nunca restore 0 a quien no controla).
  Revisión independiente: GO con arreglos, los 15 hallazgos tratados (3 y 15 parciales/documentados).
- **Fase E** (`s25/s35-fase-e-guardado.md`): `host_save=probe` archiva la caja anterior, verificada, en
  `C:\Emulador CD\backups\host_save_exports` antes de rehacerla; `real` sigue bloqueado; restauración manual con
  `scripts\host_save_restore.ps1`. Revisión independiente: GO con arreglos, todos aplicados.
- 68/68 tests. Kit `dist/cdmp-p8-shadow-s35.zip` (`cdmp.asi` `82AA0BEC3BE9…`), copiado a la carpeta compartida; Shadow lleva el
  mismo `cdmp.ini` de la 34. En el anfitrión: asi instalado, `bin64\cdmp.ini` = `cdmp.p8-s35-host-paso1.ini` (el paso 2,
  `cdmp.p8-s35-host-paso2.ini`, queda al lado). Copias: partidas en `backups\saves-antes-s35-*`, ini anterior en
  `backups\cdmp.ini.antes-s35-*`.
- **Falta:** el arranque en un PC para ver que todos los ganchos se instalan (necesita pulsar Jugar), y Shadow.

### Para mañana (14-10): orden de pruebas

Shadow no funcionó el 13-10 por la noche.
1. **Kit 31 con Shadow** (NPC de pueblo alrededor del invitado, U-1). Anfitrión: paso 1 `cdmp.p8-s31-host-paso1.ini` (ya en bin64), paso 2 `cdmp.p8-u1-host-1.ini`.
2. **Kit 32** (política de misiones `quest_policy`, `a160ebe`, revisada). Anfitrión: `cdmp.p8-s32-host-paso1/2.ini`. Shadow: el mismo `cdmp.ini` estable. Hay que reconstruir el `cdmp.asi` con HEAD.
3. **Tiendas, caso A, en un PC** (`96dada0` + revisión `6b17447`, `tiendas-u2.md` §11.4): pasos A, A2 y B con el peer andando a la herrería de Hernand. Hace falta que el usuario pulse Play en cada reinicio. Después, Shadow (§11.5).
4. **Caso B** (gemela cerca del anfitrión): plan en `tiendas-u2.md` §12, sin implementar hasta que el caso A pase en vivo.

Hecho por la noche (13-10), sin juego:
- tiendas, caso A (`96dada0`), revisión (`6b17447`) y el 2054 como sitio 15 (`2cba3b0`: los borrados diferidos de escenas del invitado se perdían en el gestor del anfitrión);
- las dos cadenas 2043 del anfitrión tras el login son **nativas**, por la doble preparación del login. Solo se corrigió la medición (`8ae4f6e`, §8.16).

El `cdmp.asi` de HEAD no se ha probado en el juego: antes del kit 32 hace falta un arranque en un PC (instalación de ganchos).

### Opción U-1 en un PC y kit de la sesión 31 (13-10)

- **Regla del usuario:** el invitado juega la partida del anfitrión. No tiene misiones propias, y todo lo que depende del avance usa el del anfitrión (memoria `guest-follows-host-progress`).
- **U-1 (`user2_stages`)**, del `2a058cf` al `92c0686`, con cuatro revisiones independientes. El anfitrión carga ahora una partida nueva en la ranura 1 a 62 celdas del peer (Hernand); copia de la anterior en `backups\host-slot1-antes-20261013-1821`. Cada pasada en un PC desveló una pieza:
  - la puerta de dueño `0x288B87A`;
  - la segunda puerta del recolector `0x29113CB`, con el gestor de misiones del dueño en el contexto;
  - la lista de ids de la preparación nativa (`0x2896C60` + `0x287FA30`);
  - los duplicados de largo alcance (U-f2, por posición);
  - el null de `FindUser` (`92c0686`: cerrojo del propio gestor en `0x2911435`).
- **Resultado en un PC:**
  - armado a 62 celdas, unas 43 stages de horario del usuario 2 creadas y enviadas (2335 propios);
  - el anfitrión viajó a Hernand: vaciado a 2 celdas y relevo nativo (el anfitrión creó las suyas en el sitio). Al irse, rearmado;
  - despedida armada: vaciado completo;
  - sin errores, sin duplicados y sin tocar al anfitrión.
- **`user2_timer_route`** arregla un fallo que ya existía: la cadena 2043 del usuario 2 se sumaba a la del anfitrión, que pasaba de 12 a 33 por minuto.
- **Kit 31** (`cdmp-p8-shadow-s31.zip`, `cdmp.asi` `F525ED30…`): en Shadow, la configuración estable. Anfitrión: paso 1 `cdmp.p8-s31-host-paso1.ini` (U-1 en sonda), paso 2 `cdmp.p8-u1-host-1.ini`.
- **Pendiente (aprobado por el usuario):** política de `quest_per_user` por llamador.
  - Punto de reaparición con el avance del anfitrión.
  - La misión seguida del invitado nunca toca la del anfitrión.
  - Las zonas de misión 2112 solo medidas.

### Sesión 30 en Shadow: árboles doblados y reentrada (13-10)

- **Paso 1:**
  - Shadow se unió con el anfitrión en el título, salió y volvió a entrar: check c estructural `pass`, época 2 IN-BAND.
  - Revivir con F4: «F4 ACTED» y los NPC del anfitrión siguieron saliendo tras la muerte y un viaje rápido (lo vio el usuario).
  - Árbol en sonda: FIGHTS 0 en los dos PCs (contando la copia en línea de `0x8A30C0`); would apply 1086 en el invitado y 1650 en el anfitrión.
- **Paso 2 (`tree_bend=1`):**
  - Los árboles se doblan en la pantalla del otro (lo vio el usuario): applied 802 en el invitado y 1168 en el anfitrión, FIGHTS 0, ningún FAULTED.
  - Reentrada ×2 (épocas 2 y 3 IN-BAND).
- **Configuración estable:** `ini/cdmp.p8-estable-host.ini` y `cdmp.p8-estable-shadow.ini` (= paso 2 de la S30).
- **Siguiente:** opción U, paso U-1 (NPC con horario alrededor del invitado lejos del anfitrión).

### Sesión 29 en Shadow y kit de la sesión 30 (13-10)

- **S29, funciona y queda en 1:**
  - F4: el revivir del usuario 2 ya no congela las stages del anfitrión. En el paso 1 (sonda) la puerta se quedó en 284; en el paso 2, «F4 ACTED» ×2 y la puerta subió de 154 a 243.
  - `docked_gimmicks_other`: el invitado ve la cuerda del anfitrión. TOTAL 0/0 y el contador M1 no crece.
  - `gimmick_event_dedup`: 0 duplicados creados.
  - Informe completo: `s25/s29-resultados.md`.
- **S29, el fallo:** volver a entrar quedó en cuarentena porque Shadow se unió con el anfitrión en el título, sin baseline del campo. Arreglo: el check c pasa a ser estructural (`967c6ca`), con al menos una entrada (`bd627d2`, revisión). En un PC, el peer entró con el anfitrión en el título: 2 de 2 rondas y `c field pass` sin baseline.
- **Árbol:** el criterio B1, tal como estaba escrito, falló. El SetBend nativo corre fuera del tick y no siempre en el hilo principal, y hay una copia en línea en `0x8A30C0`. Para pasar a 1, guardas (`6c9e192`, `0fb9d3c`):
  - solo en el hilo principal;
  - no si el jugador local dobla el mismo árbol, o su copia en línea corrió hace < 150 ms;
  - SEH con apagado al primer fallo;
  - FIGHTS incluye la copia en línea.
- **Kit 30** (`cdmp-p8-shadow-s30.zip`, `cdmp.asi` `5F613E46…`):
  - paso 1: todo lo probado activo y `tree_bend=probe`, más la reentrada con el anfitrión en el título;
  - paso 2: `tree_bend=1` y la reentrada en el orden normal.

  Todos los hooks se instalan en un PC con `tree_bend=1`.

### Sesión 28 en Shadow y kit de la sesión 29 (13-10)

- **S28 funcionó:**
  - revivir (sin pantalla negra, cerca y lejos);
  - volver a entrar sin cerrar el juego (1 reentrada; el aviso no salía, `e327c9e`);
  - la cuerda propia del invitado (`docked_gimmicks=1`: contador del anfitrión 0, sin errores nuevos);
  - la sonda del 2779 (18 de 18, uno a uno).
- **S28 encontró:**
  - **Cuerda del anfitrión invisible en el invitado.** Es el reflejo del caso propio: el dueño del campo controla los dos lanzadores. Arreglo: `docked_gimmicks_other` (`d5fdf82`), con un contador M1 de la revisión (`39ad269`).
  - **Árbol doblado.** §12.3 queda refutado: el doblado solo lo calcula quien tira, desde su mando (`0x1190FD0` → `SetBend 0x349EA10`), y nunca va por la red. Arreglo: `tree_bend` (`b1d30f7`, revisión `3c3baf9`), por el estado de control 15 del túnel.
  - **El revivir del usuario 2 dejaba al anfitrión sin stages.** La causa es el vf48 del fijador `0x2BC4950`. Arreglo: F4 (`17c376b`). Tras la revisión decide por el campo, no por el opcode (`aaaa9db`): un 2601 al hacer login también llegaba. En un PC, «F4 ACTED on another Req».
  - **U-0:** confirma la opción U con 5 cambios (`u0-resultados-s28.md`). Los viajeros salen de `AutoSpawnRoadReq 2550`, por solicitante. La sonda U-0b va en `78b60f7`.
- **Kit 29** (`cdmp-p8-shadow-s29.zip`, `cdmp.asi` `2FC9D306…`), en TRES pasos, porque la revisión pidió no activar a la vez lo que escribe por primera vez:
  - paso 1: todo en sonda;
  - paso 2: `docked_gimmicks_other=1` y `gimmick_event_dedup=1`;
  - paso 3: `tree_bend=1`, solo si los pasos 1 y 2 muestran el `SetBend` nativo en el mismo contexto y `FIGHTS 0`.

  Todos los hooks se instalan en un PC.

### Sesión 27 en Shadow y noche del 12 al 13 (13-10)

- **Sesión 27 (dos pasos):**
  - Caballos bien en los dos PCs. `mount_relink` en sonda midió 0 reenlaces necesarios.
  - Cuerda mal en el paso 1, como se esperaba (`remote_rope` en sonda), y bien en el paso 2.
  - Detalles que quedan:
    - el invitado no ve **su propia** cuerda (el anfitrión sí la ve);
    - el árbol doblado no se ve en el otro PC.
  - Grave: al revivir, la pantalla del invitado se queda en negro y el guardado final falla con «cargando para moverse».
- **Revivir** (`reaparicion.md`, `d09254d` + revisión `08f0250`).
  - Causa: revivir es la misma recarga en el mismo campo que un viaje rápido, pero el observador solo abría viaje con un 2283. El 2873 del revivir pasaba nativo, el 2322 iba al personaje del anfitrión y la máquina de carga del invitado esperaba para siempre en 0x11.
  - Arreglo: `observer_milestone_reload_any` (0|probe|1) abre el viaje en cualquier 2437 del usuario 2.
  - La revisión encontró un aprobado falso en un PC (la copia del 2322 del anfitrión) y `expect` ahora comprueba el contenido.
  - Probado en un PC. **Falta Shadow.**
- **Volver a entrar sin reiniciar** (título → Continuar; `reentrada.md`, `c4367f8` + revisión `330b9a8`).
  - Conversación nueva dentro de la misma conexión del túnel (estados de control 13/14, caps 0x08).
  - En un PC con `cdmp_peer --rejoin 2`: 3 de 3 rondas y épocas 2 y 3 dentro de la misma conexión del túnel, también tras la revisión. **Falta Shadow.**
- **Cuerda propia invisible en el invitado** (`gimmicks-dueno.md`, `920b375`, fusionado en `2888da9`).
  - Causa: el servidor hace controlador al dueño del campo de todos los gimmicks, incluido el dispositivo acoplado al personaje del usuario 2, y un cliente solo mueve un gimmick si es su controlador.
  - Fase 1 (`docked_gimmicks`): el invitado se pone como controlador de sus propios acoplados al recibir el 2005 y retiene sus peticiones de estado.
  - Revisión independiente: el criterio de aborto «0 en la captura del invitado» no se puede cumplir porque el sniffer captura antes de la retención; hay que medir en el anfitrión. Arreglos en curso.
- **Árbol doblado:** `rope_probe` (`f8d8bee`, `66bac6f`), sondas de solo log en las dos funciones del árbol y el dispositivo. El arreglo espera sus datos.
- **NPC alrededor del invitado** (opción U, `mundo-escenarios-propios.md`). U-0 (`c9d2e68`) es una sonda de solo lectura de los gestores de escenas de los dos jugadores. Revisión sin bloqueantes; arreglos de fiabilidad en curso.
- **Proyectiles duplicados:**
  - 2399/2202/2699/2612/2999/2387 salen de los dos clientes, pero son inofensivos: el servidor solo acepta la copia del controlador (`+0x90`) y los 114 2751 de S27-2 no repiten ninguno.
  - El **2779** no tiene esa comprobación y creó 10 proyectiles de más en S27. `gimmick_event_dedup` (`6fa0aa0`) lo mide en los dos PC (en sonda en el kit 28); la retención en el invitado queda para cuando los datos cuadren 1:1 (`gimmicks-dueno.md` §7).
- **Revisiones aplicadas:** U-0 `37a23df`, gimmicks `7230c29`. La prueba de los gimmicks se juzga ahora con un contador del anfitrión.
- **Kit 28 listo** (`dist/cdmp-p8-shadow-s28.zip`, en la carpeta compartida; `cdmp.asi` `AD20B20D…`). Con `6fa0aa0` todos los hooks se instalan en el juego en un PC. En bin64 queda `cdmp.p8-s28-host-paso1.ini`; el paso 2 del anfitrión es `cdmp.p8-s28-host-paso2.ini`.

### Sesión 26 en Shadow y kit de la sesión 27 (12-10)

- **Partida propia (líneas) en vivo:** paso 1, la ranura 0 avanzada de Shadow cargada como línea nueva (las gen1-5
  sin raíz ignoradas); paso 2, la misma ranura continuada desde su gen6; gen6 y gen7 entregadas.
- **Remates y eliminaciones sigilosas:** `catch_seq=1` reescribió 8 de 8 agarres del invitado (el usuario: "funcionan
  perfectos"); los agarres locales siguen coincidiendo.
- **Cuerda mágica** (`gancho-cuerda.md`, revisión independiente x2): el observador usaba el identificador de cuerpo
  físico del lanzador (solo válido en su PC) y el servidor nunca manda la liberación para árboles (ni al soltar ni al
  salir volando). `remote_rope` (F1 traducción por id de escena, F2 2471 nativo en el servidor, F3 hueco atascado):
  `c365fae`, `cabea6a`, `a86e2fa`. En el anfitrión solo (sonda): 4 árboles con el identificador coincidente.
- **Caballos** (`montura-regresion.md`): no era la clase de combate. (A) el primer gancho en línea del exe se rechazó
  una vez (safetyhook no encontraba memoria cercana: bajo el exe no hay hueco de 64 KB en 2 GB y arriba el primero está
  a +850-1005 MB); asignador parcheado + reintentos + aviso (`bf255bb`, `86c3905`), 5/5 arranques bien. (B) carrera en
  el observador: una acción justo después del MountCommit deshizo el enlace; reenlace `mount_relink` (sonda/1).
- **Kit 27:** agarres activos desde el paso 1; cuerda y reenlace en sonda en el paso 1 y activos en el paso 2
  (`cdmp.p8-s27-host-paso1.ini`, `cdmp.p8-s25-host-activo.ini`; invitado `cdmp.p8-s26-shadow.ini` / `-paso2.ini`).

### Sesión 25 en Shadow: resultados y arreglos (12-10)

Dos pasos (paso 1: `cdmp.p8-s7-*`, sondas; paso 2: `cdmp.p8-s25-*-activo`), seis entradas de Shadow. Informes de los
cuatro agentes en `docs/research/s25/`.

- **Partida propia: fallo GRAVE mío, sin pérdida de datos.** En la segunda entrada del paso 2, Shadow subió su ranura 0
  avanzada (1,9 MB); el buzón del anfitrión (§3.5.6, «una reconexión nunca vuelve atrás») tenía gen2 de la partida de
  pruebas y la cargó EN LUGAR de la subida, porque solo miraba el id del invitado y su base_gen. Desde entonces
  cualquier ranura acababa en la de pruebas, y gen3-5 volvieron marcadas «para la ranura 0/1» (aplicarlas habría
  pisado las avanzadas). Los ficheros reales de Shadow no se tocaron (mismo SHA-256 en cada arranque).
  Arreglo `12f8dfb`: **líneas** (cada generación lleva su raíz, el SHA-256 del save.save del que salió;
  `gsw::find_continuation`; el buzón solo continúa la MISMA partida), SAVE_RESULT ver 2 con la raíz, ranura y línea
  del par vivo (no del preparado), y el script de aplicar solo escribe una generación sobre su propia línea (o una
  ranura vacía) y nunca una sin raíz. Probado en un PC con `cdmp_peer` (4 vueltas, el caso de S25 incluido) y el
  script sobre los resultados reales; carpeta real intacta.
- **«No nos veíamos»** (`invisibles.md`): en mi servidor se veían en todas las entradas. Lo de S25 fue tras salir al
  título y pulsar Continuar en el mismo arranque: el mod del invitado manda esa conexión a su propio servidor (una
  conexión remota por arranque), con su partida real, mientras los dos HUD seguían dibujando al otro por el canal del
  mod. Arreglo `a48ee13`: marcador y distancia solo en el mismo mundo, y aviso «Juegas EN SOLITARIO» en el invitado.
  Volver a entrar en el mismo arranque queda pendiente (diseño propio).
- **Hora y clima** (`hora-clima-verificacion.md`): hora idéntica (t0 igual en las 6 entradas, 12/12 empujes T1),
  clima del paso 2 en modo visor convergido sin fallos. Arreglos `a48ee13`: `server_user2_in_world()` falso tras la
  despedida (mandaba clima ~3 min a un invitado ido), edad de muestra sin desbordar, bytes del gancho en el log. Sin
  probar: lluvia/nieve, T2, radio, corte del enlace.
- **Combate:** bien en el paso 1 salvo **finisher y eliminación sigilosa** (`finisher-sigilo.md`): son agarres; el
  servidor los acepta, pero el cliente del anfitrión (que controla al enemigo) los descarta en `0x935DD0` al comparar
  el byte de secuencia del agarre, que para el personaje del invitado nunca coincide. Arreglo `catch_seq` (sonda en
  el paso 1, activo en el paso 2 de la sesión 26).
- **NPC** (`mundo-teleport.md`): el ancla funcionó como se diseñó. Los NPC de pueblo salen de escenarios de horario
  que pertenecen al personaje del anfitrión: nacen cerca de él y su carga (viaje rápido) los cierra todos (motivo 19),
  también los que están junto al invitado. Arreglo de fondo (escenarios propios del usuario 2 a distancia) para más
  adelante; corregida la medida del ancla al montar (`b747346`).

### Noche del 11 al 12: guardado desviado y los cuatro temas (sesión 24-25, un PC)

- **Guardado desviado** (`save_guard=redirect`, `pendientes/p8-guardado-desviado.md`): copia de `save` por arranque en
  `cdmp_save_sandbox` y todas las API de ficheros desviadas a ella. Traza real de un autoguardado (72 operaciones) antes
  de diseñarlo. Verificado: autoguardado del ESC y el periódico de 16 min en la caja, sin "Failed to save data", carpeta
  real intacta toda la noche.
- **Investigación** (cuatro agentes con Ghidra y las capturas de S10-S23; informes en `docs/research/noche/`):
  - combate (`combate-retardo.md`): el retardo es de pantalla (`D` de interpolación de los enemigos 0,2-1,3 s), no de
    red (golpe ida y vuelta ~64 ms);
  - mundo (`mundo-invitado.md`): el recolector de NPC del servidor solo mide desde el anfitrión y desde la ranura
    "observador de mundo" del campo, libre;
  - hora y clima (`clima-hora.md`, contrastado con el informe de la otra IA): lo de S16 era el clima; la hora del
    invitado se desfasa ~3,5 min por su carga; el clima se simula en cada cliente y no es determinista
    (`clima-sincronizar.md`: modo visor `X+0x31`);
  - partida propia (`progreso-invitado.md`): el cifrado no depende de cuenta ni PC; el mapa de ranuras se hace al
    arrancar; la 207 es de memoria (se usa la 204); la salida no guarda (3037 tampoco: clase 6), el 2910 sí;
  - NPC lejos (`control-npc-lejos.md`): el cliente del anfitrión los simula hasta 643 m; el control nunca se reasigna.
- **Implementado** (cada uno con revisión independiente y sus hallazgos corregidos):
  - `combat_follow_v2` + `remote_interp_combat` (probe en las inis);
  - `user2_clock_push` (2816 nativo tras cada 2762 y al cambiar el reloj), `block_guest_weather`, `clock_probe`;
  - `user2_world_anchor` (escritura directa de la ranura bajo el cerrojo del campo);
  - `guest_own_save` fases A y B (ranura 204, túnel estados 6-11, guardado final 2910, script de aplicar);
  - `weather_sync` (modo visor; en revisión).
- **Pruebas en un PC con `cdmp_peer`:** ancla del mundo puesta, verificada y quitada; T1 tras cada 2762; partida propia:
  primer intento rechazado de forma segura (lectura errónea de los ficheros de la ranura; corregido), luego carga
  verificada, y la **ida y vuelta completa** dos veces (sube, juega su partida, guarda con 2910, vuelve gen1; la gen1
  devuelta vuelve a cargar y produce gen2). Sin fallos del juego.
- **Error mío:** un merge se confirmó con marcas de conflicto (`87b89c8`); corregido en `4c9bae1` antes de compilar.
- **Falta (necesita Shadow):** la distancia real del ancla (400-600 m), el clima en modo visor en el invitado, el
  combate con enemigos reales, la partida propia con Shadow y su script de aplicar.

### Sesión 23 en Shadow: monturas resueltas (10-11)

- **Resultado:** funciona en vivo: montar, cabalgar y desmontar en los dos sentidos, sin
  desplazamiento en la silla ni parpadeo al subir.
- **Anfitrión (`cdmp_20261011-214457.log`):** `seat [seated 2, released 2, stuck 0, NOT seated 0, vetoed 0]`,
  `attach type 4 -> 9` al desmontar (una por la retirada del observador, otra 31 ms tras su `RideOff::Enter`),
  `game faults 0`. Faltan los logs de Shadow para su lado.
- **Siguiente:** el guardado desviado (el aviso en bucle bloquea las pruebas largas) y después hora y clima.

### Sesión 22 en Shadow y kit de la sesión 23: la barrera `+0x94` (10-11)

- **Resultado S22:** "sigue exactamente igual". `REFUSED (rider+0x94 != 0)` en las 4 monturas y `RideOn::Enter …
  attach type 9 | NOT SEATED`: la causa de §14 se confirma; la guarda `+0x94 == 0`, añadida sin medir el valor en un
  jugador remoto (la revisión lo marcó), impidió el asiento. Error mío: una guarda nueva sin dato en vivo.
- **Lo que decide [V Ghidra + capstone]:** el vf178 de todo actor de juego del cliente es `0x4FE260`
  (`+0xA0 == usuario local && +0x94`); el remoto tiene `+0x94 = 1` pero otro usuario. Autoprueba en vivo sobre el
  personaje local: `vf178 exe+0x4FE260, +0xA0 IS the local user, +0x94 1 -> local user (ok)`.
- **Cambios:** `msl::seat_local_user` (falla cerrado: ranura desconocida o lecturas a 0 = local), `Controlled`
  (C8 de jinete o caballo != 0 o ilegible), veto en `0xA45AC0` solo con `mount_seat=1`, autoprueba positiva en el
  resumen. Revisión: 1 fallo (la autoprueba podía dar "ok" con lecturas fallidas) y 3 riesgos, corregidos.
- **Regresión en un PC (`regresion-montura-s23-20261011-214457.log`):** autoprueba ok, veto instalado, NPC
  `9 -> 4` (3), `game faults 0`, 0 ERR.
- **Kit:** `dist/cdmp-p8-shadow-s23.zip` (asi `A3D0DDB536E0`).

### Sesión 21 en Shadow y kit de la sesión 22: el tipo 9 y una sola causa (10-11)

- **Resultado S21:** pequeña mejora; siguen el desplazamiento en la silla y el parpadeo, en los dos PCs. `MountSeat`
  dio `REFUSED (attach type 9 …)` en todas las monturas: vf54 no se llamó nunca. `hold` rellenó 358/78 frames sin
  efecto visible.
- **Investigación con Ghidra (dos agentes en paralelo, servidor de consultas):**
  - el tipo de enganche **9 es "ninguno"** (`0x9E990E0` escribe 9; el 0 no lo escribe nadie);
  - el parpadeo es el warp de `RideOn::Enter`, que sale de `[[CC+0x100]+0x24]` (la identidad, el origen, si el jinete
    no está sentado), más `RideOn::Exit` al origen sin padre; la premisa de `ride_hold_pos` era falsa (la salida ya
    llevaba la pose actual).
  - Una sola causa para los dos defectos: el jinete remoto no se sentaba (`p8-impl-monturas.md` §14).
- **Cambios:** `kAttachNone = 9`; guardas nuevas del juez (padre 0, `CC+0x228 == 1`, `rider+0x94 == 0`, caballo con
  CC, clave local conocida); tipo antes → después en cada vf54 y en `RideOn::Enter`; contador `NOT seated`;
  `ride_hold_pos=probe`. Revisión: 5 hallazgos menores, corregidos.
- **Regresión en un PC (`regresion-montura-tipo9-20261011-205747.log`):** los asientos nativos de NPC dan
  `attach type 9 -> 4` (3), `game faults 0`, 0 ERR.
- **Kit:** `dist/cdmp-p8-shadow-s22.zip` (asi `757EEC82142E`).

### Sesión 20 en Shadow y kit de la sesión 21: jinete en la silla y parpadeo al subir (10-11)

- **Resultado S20:** el desmontar funciona en los dos sentidos (invitado: `APPLIED 4`; anfitrión: `UNLOCKED 3`;
  `game faults 0`). Quedan dos defectos, vistos en las dos pantallas: el jinete se ve desplazado de la silla y
  parpadea unos milisegundos al subir.
- **Parpadeo [V]:** `RideOn::Enter` vacía en el observador el búfer de interpolación del jinete
  (`0x681DBC` → `0x88E520`, `[mc+0x3D0]=1`). El controlador no manda Move durante la animación, `0x88E6A0` sale
  sin escribir y se aplica la pose a cero del llamador: el jinete pasa por (0,0,0), sector (0,0). La sonda de
  RemoteMotion lo vio en las 7 monturas de la sesión, y nunca con `mount_sync=probe`.
- **Desplazamiento [V]:** el juego sienta al jinete con ClientVehicle vf54 `0xA45760` (tipo de anclaje 4 en el
  hueso de la silla; `0x88EB70` deja de interpolarlo). El mod solo llamaba a vf53.
- **Cambios** (`p8-impl-monturas.md` §13): `mount_seat` (vf54 tras el vf53 del observador, vigilancia de la
  liberación) y `ride_hold_pos` (gancho en la entrada de `0x88E6A0`: con el búfer vacío, la pose actual del
  jugador remoto en vez de la pose a cero). Revisión: 6 hallazgos menores, corregidos (carrera de la bandera,
  lectura rota de la pose, clave local, sello viejo de RideOff, acción a pie sin retirada, retirada solo con
  `ride_dismount=native`).
- **Regresión en un PC (`regresion-montura-silla-20261011-193842.log`):** ganchos instalados, `game faults 0`.
- **Kit:** `dist/cdmp-p8-shadow-s21.zip` (asi `FDDA9DC1E92E`).

### Kit de la sesión 20: monturas en los dos sentidos (10-11)

- **Fase B** (`p8-impl-monturas.md` §12): el invitado pasa a `mount_sync=1` y `ride_dismount=native` para ver al
  jinete del anfitrión.
  - Autocomprobación de la 2223 en `guest_send`.
  - La clave del anfitrión solo vale mientras la conexión remota está viva.
  - La autocomprobación degrada el arreglo B sin apagarlo; el Detach reinicia la marca a pie; tope de 30 s.
- **Revisión:** GO vigilado, con los hallazgos corregidos.
- **Regresión en un PC (`regresion-montura-invitado-20261011-183016.log`):** FAULTED 0, huérfanos 0.
- **Kit:** `dist/cdmp-p8-shadow-s20.zip` (asi `2B35432AA45C`).

### Sesión 19 en Shadow: desmontar resuelto (10-11)

- **Resultado:** funciona en vivo. El invitado, visto en el anfitrión, hace la animación de
  bajarse completa y queda de pie.
- **Log del anfitrión** (`backups/p8-sesion19-host`): `gate detour hooked [... UNLOCKED 5]`, con DEGRADED 0, OFF 0
  y FAULTED 0.
- Las sondas confirman la causa: la acción actual es `C9356CEB` / `412C2C2B` con `flags 0x50`, así que el bit
  `0x40` está puesto.

### Sesión 18 en Shadow y kit de la sesión 19: desmontar (10-11)

- **Sesión 18:** con `mount_sync=1` mejora el montar, pero al desmontar la copia del invitado en el anfitrión se
  queda atascada en RideOff (método 7).
- **Causa** (`pendientes/p8-s18-desmontar.md` y `p8-s18-desmontar-nativo.md`): toda acción de red lleva el bit 1
  de `+0x50`. `.xtls 0xEE58A40` descarta la petición si la acción actual o la pendiente tiene `def+0xA & 0x40`
  («bajarse»). El controlador sale de RideOff por su chart local, que va sin ese bit; la copia remota nunca
  ejecuta su chart.
- **Arreglo B** (`ride_dismount=native`, `p8-impl-monturas.md` §11): desvío del thunk `0x2329A90`. Solo para el
  jinete remoto atascado y solo en la ruta de retorno `0x8639CD`, llama al original con una copia de las entradas
  sin los bits de red. Retirada nativa (vf89 → `0x397F40`).
  - Respaldos: `partial` y `rewrite`.
  - Dos revisiones: GO, con las correcciones aplicadas.
- **Regresión en un PC (`regresion-desmontar2-20261011-174220.log`):** desvío instalado, 2960 llamadas intactas,
  FAULTED 0, huérfanos 0.
- **Kit:** `dist/cdmp-p8-shadow-s19.zip` (asi `3300FE274EA7`).

### Sesión 17 en Shadow y kit de la sesión 18: enlace de montura en el anfitrión (10-11)

- **Sesión 17:** el jinete va bien al paso pero se separa al galope; vídeo en `backups/p8-sesion17-host`.
- **Análisis de las sondas** (`pendientes/p8-impl-monturas.md` §10):
  - vf53 corre antes del 2223, con asiento 0 y quinto argumento 0.
  - Al desmontar, el jugador no pasa por 0x2228E40; ahora hay un Detach observado.
  - En el galope los datos son coherentes, pero el jinete remoto va suelto (Link 0).
- **Revisión:** GO condicionado; corregido el riesgo de crash en `do_detach` y otros 7 hallazgos (§10.7).
- **Regresión en un PC (`regresion-montura1-20261011-160515.log`):** `mount_sync=1` instalado, FAULTED 0.
- **Kit:** `dist/cdmp-p8-shadow-s18.zip` (asi `8D1CFF4377B1`). El anfitrión lleva `mount_sync=1` y el invitado
  `probe`.

### Sesión 16 en Shadow y kit de la sesión 17: monturas (10-11)

- **Sesión 16:** el viaje rápido del invitado funciona y los viajeros se ven.
  - Nuevo pendiente en `ROADMAP.md`: la hora y el clima del invitado se quedan en los del momento en que el
    anfitrión cargó la partida.
  - Guardé los logs de los dos lados en `backups/p8-sesion16-*`.
- **Monturas:** el informe externo está en `mounts-20261011-work`.
  - `mount_sync=probe` (`pendientes/p8-impl-monturas.md`): 9 ganchos de sonda y los eventos MountBegin, Commit y
    Detach por el túnel. El modo 1, el commit nativo de vf53 en el observador, está listo pero no se activa.
  - `horse_follow_reverse=1` y `remote_interp_pair=1` (`p8-impl-monturas-cabalgata.md`): caballo denso G→H y un
    mismo reloj para el jinete y el caballo.
  - Revisión: 6 hallazgos, corregidos.
- **Regresión en un PC (`regresion-monturas2-20261011-145101.log`):** ganchos instalados, FAULTED 0, huérfanos 0.
- **Kit:** `dist/cdmp-p8-shadow-s17.zip` (asi `E5DB7F36C680`).

### Sesión 15 en Shadow y kit de la sesión 16 (10-11)

- **Sesión 15:**
  - El viaje rápido pasa la mitad del servidor, pero el invitado se queda en el túnel en el estado 0x11. Su 2873
    hace que el servidor mande el 2322 al anfitrión (`pendientes/p8-s15-viaje-tunel.md`).
  - Alejarse más de 300 m: sin cierre.
- **Hecho:**
  - `observer_milestone_reload=1`: suelta el 2873 del usuario 2 y le emite su 2322. Se aplican las correcciones de la
    revisión.
  - `guest_reload_wait_fix=1`: el invitado saca de la espera de 0x12 a los jefes que ya estaban atascados (tope
    128).
  - `observer_hold_family=1`: nunca se entrega a un hijo antes que a su padre. El padre se lee del `+0xAC` del actor
    del servidor. Arregla a la vez a los jefes de grupo atascados y a los viajeros invisibles.
- **Regresión en un PC (`regresion-s16-20261011-132250.log`):**
  - Huérfanos hacia el usuario 2: 62 → 1 (17 retenidos y soltados detrás de su padre).
  - Viaje: PREPARED, 2873 descartado, 2322 emitido al usuario 2, anfitrión limpio, sin 2679.
- **Kit:** `dist/cdmp-p8-shadow-s16.zip` (asi `BDABB0B4F80F`).

### Sesión 14 en Shadow y kit de la sesión 15 (10-11)

- **Sesión 14:**
  - Todos los NPC se mueven y animan; `observer_load_defer` resolvió la mitad paralizada.
  - Tenderos: con C1=1 aparece el HUD, pero R deja al invitado atascado en la escena del anfitrión. C1 vuelve a probe.
  - Teletransporte del invitado: lo expulsa el error `eErrNoInvalidFieldInfoKey` (`p8-s14-teleport-cierre.md`).
  - Viajeros invisibles: sus piezas llegan antes que el NPC que las lleva (`p8-s14-viajeros.md`).
- **Hecho:**
  - Viaje rápido: F1/F2/F3 más la segunda vf48 `0x2BC4C53`, alias del dueño en el 2643 y sondas Travel*
    (`p8-impl-viaje-rapido.md`), contrastado con el informe externo.
  - `observer_hold_pieces=probe`.
- **Prueba del viaje en un PC (`cdmp_20261011-093242.log`, guion `login_e3lc00_travel_s14.troc`):**
  - Los dos TravelContext salen «user 2's own (ok)».
  - PREPARED: TS_user2 +0x550=1, +0x569=1 y +0x591=1; TS_host sin cambios; +0x6A0 del anfitrión 2→2.
  - El 2437 se responde con 2643 (copia con dueño = usuario 2) y 3066, sin 2679. La mitad del servidor funciona.
  - Piezas: 65 vistas, UNLINKED 0.
- **Kit:** `dist/cdmp-p8-shadow-s15.zip` (asi `947234C5991A`). Anfitrión con `cdmp.p8-s7-host.ini`
  (`user2_fast_travel=1`); invitado con `guest_fast_travel_veto=probe`.

### Sesión 13 en Shadow y kit de la sesión 14 (10-11)

- **Sesión 13:**
  - El reenlace dejó 177 NPC enlazados y los ficheros retenidos dieron MISSING 0. Animaciones especiales bien
    (barrer, agacharse, bailar) y todo bien fuera de la ciudad.
  - La ciudad se partió en dos mitades: la «foto» (2005 recibido durante la carga, antes de que el dueño
    `0xA0100001` fuera válido en el invitado) quedó paralizada, y los NPC vivos se vieron bien. El 2723 no es la
    causa: en el invitado no hace nada (`vf188(5)` es falso). Análisis en `pendientes/p8-s13-npc-familias.md` y
    `p8-impl-spawn-action.md`.
  - Tenderos: C1 en sonda dio 922 «in X, not in M». T2 de S2 cerrado (`p8-impl-tenderos-s2.md` §3.1).
- **Hecho para la sesión 14:**
  - `observer_load_defer=1`: los NPC se retienen del 2549 al 2762 y se anuncian de forma nativa después; la puerta
    de carga tiene un tope de 60 s, el 2322 espera a la instantánea y los descartes se cuentan por motivo.
  - Presupuesto de `guest_schedule_load` por tiempo: las claves del propio paquete van siempre antes de entregarlo,
    con un tope de 500 ms por segundo y la sonda automática tras 4 cargas lentas.
  - `guest_x_interaction_stage=1` (C1 activo).
  - Dos revisiones independientes, todos los hallazgos corregidos.
- **Regresión en un PC (`cdmp_20261011-020050.log`):** 332 NPC retenidos y 326 soltados tras el 2762; descartes:
  6, todos `despawned`, ninguno OTHER; sin fallos.
- **Kit:** `dist/cdmp-p8-shadow-s14.zip` (asi `9BAACFC0E711`, el mismo en el anfitrión con `cdmp.p8-s7-host.ini`).

### Sesión 12 en Shadow y kit de la sesión 13 (10-11)

- **Sesión 12:**
  - Horarios en X: 33 etapas y unos 118 NPC enlazados. Bien la taberna y la mujer de la cesta.
  - Siguen mal la plaza en pose A, el agachado, el rebaño y los tenderos.
  - Clasificación en `pendientes/p8-s12-npc-familias.md`; tenderos en `p8-s12-tenderos.md` y `p8-s12-tenderos-servidor.md`.
  - El PC se reinició por un fallo de `vjoy.sys+4bc3` (7 pantallazos azules desde el 10-01). No tiene que ver con el mod.
- **Hecho para la sesión 13:**
  - `guest_sched_bind`: cola y reintento desde `0x8B6144` cuando NPC y dueño tienen `+0x5E & 0x10` y `M+0x231 == 3`.
  - `guest_schedule_load`: retiene el fichero de horario hasta que corre su 2335.
  - `guest_seq_bodyanim_diag` y `guest_seq_route_2524=probe`: medida de los secuenciadores.
  - `shop_probe` en el anfitrión y `guest_x_interaction_stage=probe` en el invitado: tenderos, C1.
  - Revisión: 6 hallazgos, corregidos.
- **Regresión en un PC (`cdmp_20261011-002142.log`):** 46 etapas enviadas, ShopProbe con todos los ganchos dentro, sin fallos.
- **Kit:** `dist/cdmp-p8-shadow-s13.zip` (asi `65E4616BC53B`, el mismo en el anfitrión con `cdmp.p8-s7-host.ini`).

### Horarios de NPC en X, instantánea y reenlace; kit de la sesión 12 (10-10 noche)

- **Origen:** el informe externo de causa raíz de la sesión 11 (`npc-raiz-s11-20261010-work`). La fase 2 nunca
  ejecutó su `0xAA3E70(X)` (live calls 0), así que la regresión vino solo de encaminar el 2524. Se abandona la fase 2.
  Los 2335 creaban las etapas de horario en M, pero los NPC las buscan en X (`0x9ED290`). Faltaban 42/46 etapas
  previas a la entrada y casi todos los 2126/2958/2909.
- **Hecho:**
  - `guest_sched_route`: los 2335/2126/2958/2909 van a X (`p8-impl-horarios-x.md`). Rechaza duplicados y 2335 sin
    etapa. Guarda contra el destructor de etapas tras morir X (`0x43B0B6`).
  - `observer_sched_copy`: instantánea de los 2335 vivos detrás del 2005 del Kliff del anfitrión, más los
    2126/2958/2909 en vivo (`p8-impl-horarios-snapshot.md`).
  - `guest_sched_bind`: `0x9F2FA0` para los NPC con `comp+0x318 == 0` (`p8-impl-horarios-enlace.md`).
  - `guest_x_ui_veto_allow=SetInteraction` (`p8-impl-veto-interaccion.md`).
  - Revisión independiente: 9 hallazgos, todos corregidos o documentados.
- **Regresión en un PC (22:39, `cdmp_20261010-223932.log`):** 48 etapas en el libro del anfitrión, las 48 enviadas
  0,03 s después del 2549 del peer, con solo 3 de 785 registros de NPC por delante. Sin fallos.
- **Kit:** `dist/cdmp-p8-shadow-s12.zip` (asi `DB5601907A9D`, el mismo en el anfitrión con `cdmp.p8-s7-host.ini`).
  Plan B: `cdmp.p8-s12-shadow-seguro.ini` (sin `guest_sched_route` ni `guest_sched_bind`).

### SetInteraction ya no se veta en X (10-10, implementado, sin probar en el juego)

Detalle: `pendientes/p8-impl-veto-interaccion.md`. En la sesión 11 el invitado no podía abrir la tienda de Renee ni
la de Delkin (2857 sí, 2720 no). `SetInteraction` (`0x2369360`) da de alta y de baja las interacciones de los NPC de
la escena: es estado del mundo, y `guest_x_ui_veto=1` se las quitaba al invitado.
- Clave nueva del invitado `guest_x_ui_veto_allow` (por defecto `SetInteraction`): esos manejadores corren nativos
  para X, alta y baja; se cuentan y se registran como `ALLOWED`. InputBlock, UIControl, UIShowMinimap, UIStageGuide y
  TimelineEvent siguen vetados.
- Necesita Shadow: el invitado habla con un tendero (debe mandar 2720 y ver la tienda) y el anfitrión habla con otro
  con el invitado delante (sin menú en el invitado). Abortar: `guest_x_ui_veto_allow=` (vacío).

### Reproducción de las escenas copiadas en el invitado (10-10, implementado, sin probar en el juego)

Detalle: `pendientes/p8-impl-escenas-play.md`. Claves del invitado `guest_seq_play` (`0 | probe | 1`) y
`guest_seq_play_live` (`0 | 1`). `cdmp.p8-s7-shadow.ini` lleva `guest_seq_play=1` y `guest_seq_play_live=0`: la
prueba siguiente es solo la fase 1, porque `0xAA3E70` toca al jugador local, la cámara y la transición de capítulo
(§13 del documento).
- Gancho en `0x8B6144` (hilo principal, justo antes del tick nativo de M): `0xA953C0(X, Q, modo)` para el gestor X
  del Kliff del anfitrión, y `X+0x231 = 3` una vez por cada 2322 enrutado. Con `_live`, además `0xAA3E70(X, dt)` y el
  2524 enrutado a X (quinto punto de `guest_seq_route`, `0xC08178`).
- El anfitrión descarta los 2276/2105 del invitado sobre sus secuenciadores.
- G5 mide `comp+0x3D8`/`+0x320`, el padre `+0xAC` y las escenas vivas de X.
- Necesita Shadow. Abortar: `guest_seq_play=0`.

### Opción A de S1, tramo de secuenciadores (10-10, implementado, sin probar en el juego)

Detalle: `pendientes/p8-impl-opcion-a.md`. Claves `observer_seq_copy` (anfitrión) y `guest_seq_route` (invitado), las
dos `0 | probe | 1`; inis `cdmp.p8-s7-host.ini`, `cdmp.p8-noche-local.ini` (`observer_seq_copy=1`,
`observer_seq_copy_only=1AB,1AD,1A9,19E`) y `cdmp.p8-s7-shadow.ini` (`guest_seq_route=1`).
- Anfitrión: el libro sale de la captura del cliente propio (sniffer). Al invitado van, byte a byte, los 2661 (con su
  2981 si no lo tiene) de los secuenciadores del Kliff del anfitrión, cada uno después del 2005 de todos sus actores
  enlazados y del 2322 propio del usuario 2; los nuevos durante la estancia; el 2810 de los copiados; y una vez por
  viaje el 2322 del anfitrión (el gestor X del invitado está en el estado 1, de precarga [E]). Se descartan los C>S
  del invitado sobre secuenciadores del anfitrión (sus 2100, solo con observer_seq_copy_2100=1; apagado tras la
  revisión, que también corrigió el 2810 de un secuenciador preparado dos veces, el 2981 por estado, cotas, la puerta
  del invitado sin clave propia y el 2322 sin recorrido propio de la tabla de actores).
- Invitado: el cliente enruta los Ack de escena al gestor del dueño del campo, que por el 2174 reescrito es el
  **suyo** (M), no X [V, sesión 7]. `guest_seq_route` cambia RAX a X tras `vf48` en los manejadores de 2661/2981/2810/2322
  cuando el paquete nombra a otro personaje; comparte gancho con `guest_stage_diag` y `P8Probe`. Una puerta descarta
  esas copias si el enrutado no está activo.
- Reproducción de la sesión 7 con la lógica: 0x19E al abrirse la puerta (t=198), 0x1A9/0x1AB/0x1AD 0,18–0,20 s tras
  el 2661 del anfitrión. 31/31 pruebas.

## Para leer al despertar (2026-10-09)

- **Hecho en el juego:** A1, A2, A3 (Shadow), A4 con la partida de capítulo 2, RPT-1, B1, B2 y B3
  pasan. **C1 (E3b):** por primera vez el usuario 2 entra en tu `ServerField` (`users 2`), pero tu
  juego se cerró 2 s después al animar a su Kliff sin acción.
- **Esta noche (sin abrir el juego):**
  - C1 analizado ([data/c1-actor-rechazado.md](data/c1-actor-rechazado.md)): el error de "crear actor"
    era el mercenario (se descarta limpio); el cierre, el Kliff sin acción. Contención **D-44**: la
    guarda de capas del cooperativo también protege al personaje del usuario 2 (revisada; ajustes de
    concurrencia hechos). Variante del guion con su primera acción (`login_e3lc00act_112627.troc`).
  - **Ghidra** revisado por segunda vez (conectar con condiciones), ajusté su parada y queda en el
    repositorio (D-43). Lo uso con sus `.ps1`; no está registrado como MCP (eso lo apruebas tú).
  - Salida del usuario 2 (C4): el logout está en `.xtls`; el siguiente paso necesita el juego.
  - En curso al escribir esto: análisis de la identidad repetida (C2, `characterNo`), con Ghidra.
- **Actualización 09-10 por la mañana:** C1b–C1f (ver abajo) acabaron en cierres por la misma causa,
  ya encontrada de raíz (D-48): el guion no daba al Kliff del usuario 2 su primera acción durante la
  primera carga. Arreglado en el guion. **Prueba siguiente: C1g** ([opcion-d-pruebas.md](opcion-d-pruebas.md)).
- **Estado de la carpeta del juego:** configuración ligera (sonda, sin bloqueo de guardado);
  `crashpad_handler.exe` sigue apartado (K2) y no hay nada pendiente de envío.

## Resultados en el juego

### P8 con Shadow, sesión 9 — 2026-10-10 16:10–16:43 (anfitrión), y preparación de la sesión 10

Logs: `backups/p8-sesion9-host/` (fase1 y fase2), `backups/p8-sesion9-shadow/` (las dos pruebas), fotos y vídeos en
`backups/p8-sesion9-visual/`. El usuario cerró los dos juegos (16:29 y 16:43).
- [V] Escalada y subidas a muros/cajas ya funcionan; NPC mucho mejor; tenderos visibles (tienda sólo por la mesa, sin
  menú); enemigos atacan al invitado (rango); NPC de secuenciador con `+0xCF 1` tras el 3.
- [V] Fallos y causas (`p8-s9-*.md`): la cinemática de liberar un campamento (secuenciador `0x213`, rama 2) hizo que el
  cliente del invitado pidiera recargar el campo → logout del servidor → título (`p8-s9-cinematica.md`); los horarios
  sí se cargaron (68 ficheros) pero la comprobación del mod los daba por ausentes (`p8-s9-horarios.md`); la fase 2
  nunca corrió (compuerta `+0x2A8` mal planteada) y el 2524 se leía desplazado (`p8-s9-npc.md`); combate a saltos
  por el retardo de interpolación, jinete atascado en RideOn/RideOff, mascota del anfitrión fallando a 1,5 s
  (`p8-s9-combate-montura.md`).
- Arreglos (todos revisados y corregidos): presencia de horarios; compuerta y 2524; filtro 2888; lista de recursos
  denegados; veto de la recarga del campo para gestores remotos (`guest_x_veto`, V1 en `0x5F2A3A`); cierre limpio
  si una carga local se atasca tras una sesión remota; `ride_filter=1`; `combat_follow=1`; `child_settle_ms=4000` +
  reanuncio pedido por el invitado. 39 tests; regresión en un PC limpia
  (`backups/logs/cdmp_20261010-1807_s10final.log`). Kit `dist/cdmp-p8-shadow.zip` (asi 854C2A2FBAE5).

### Preparación de la sesión 9 — 2026-10-10 tarde (trabajo autónomo, regresión en un PC)

Tras la sesión 8 (NPC caminando bien; congelación mutua durante diálogos del anfitrión; caída por el candado del log
en Shadow; escenas copiadas sin reproducir; mascota del anfitrión nunca creada en el invitado; ver `p8-s8-*.md`):
- Log y grabación asíncronos (`p8-impl-log-async.md`), detector de divergencia de posición.
- Mascotas: `child_settle_ms`, tope de interpolación para actores de usuario, `attach_follow` en probe
  (`p8-impl-mascotas.md`).
- Escenas: lectura de `.xtls` en Ghidra (`p8-xtls-lectura.md`); `guest_seq_play` (tick de X + paso a 3; fase 2 de
  avance con salto de la parte del jugador local, apagada en `cdmp.ini` y encendida en `cdmp.ini.fase2`;
  `p8-impl-escenas-play.md`); filtro 2276/2105; `seq_actor_restore` (`p8-impl-seq-restore.md`).
- Arquitectura (`p8-arquitectura-observador.md`, contrastada con un análisis independiente): un punto central del
  servidor `ToTarget::vf1 0x2A757F0` (sonda `owner_fanout=probe`); los ficheros de horario no se cargan nunca en el
  cliente del invitado → `guest_schedule_load=1` (`p8-impl-horarios.md`). Autocomprobación en el anfitrión [V]:
  4210/4210 nombres, mismo gestor cliente/servidor, 16/25 de los recursos que faltaron en S8 cargados por su servidor.
- Todo revisado por revisores independientes y corregido; 37 tests; regresión en un PC (anfitrión + `cdmp_peer`) sin
  WRN/ERR ni REFUSED (`backups/logs/cdmp_20261010-1526_s9final.log`). Kit `dist/cdmp-p8-shadow.zip` (asi 78711CC0F104).

### P8 con Shadow, sesión 7 — 2026-10-10 06:02–06:31 (anfitrión), **juego cooperativo estable: entra, reentra, menús sin pausa, ropa y caballos visibles**

Logs: `C:\Emulador CD\backups\p8-sesion7-host\` (dos partidas: 06:02–06:18 con dos entradas del invitado, 06:25–06:31 con
una tercera) y `p8-sesion7-shadow\` (05:04–05:18 hora del invitado = anfitrión −1 h; falta la tercera entrada).

Comprobado [V]: tres entradas del invitado sin atascos ni cierres; salidas normales ("Terminate 시작.") en los dos
lados; túnel comprimido sobre Steam ×5,6 (182 MB → 33 MB), 0 descartes; ESC "ver objeto" del invitado sin quedarse
quieto; menús (mapa, inventario, ajustes) sin pausa en los dos lados (`UiPause … BLOCKED` con `MainMenu`); cambio de
ropa del invitado visible en el anfitrión; el caballo de cada uno visible para el otro; `observer_hold` 1748/1748
liberados, child hold 2/2; ObserverSceneCopy 1 copia, 0 perdidas. La pantalla inicial de recompensas
(`ExtraReward`) pausa al anfitrión porque aún no hay sesión (decisión del usuario: se queda así).

Fallos del usuario y causa raíz (informes en `pendientes/`):
- NPC/animales/caballo remotos a saltos de ~1 m (`p8-movimiento-remoto.md`): el cliente controlador manda
  `MoveActorReq` de actores 0xB… cada ~1,1 s; el que observa (modo 2) interpola con 200 ms (`exe+0x6CB9548`) y vacía
  el búfer si el hueco supera 1000 ms (`0x11B2073`/`0x11B2775`) [V]. Jinete por delante del caballo: el caballo a ~1 Hz,
  el jinete a ~36 Hz y sin vínculo de vehículo en modo 2 [E, de `main`].
- Escalada en el sitio y desaparición en cajas/muros (`p8-escalada-desmontar.md`): `PointClimb::Enter 0x691400` falla
  en el actor remoto [E fuerte]; 2851/2893 nunca se envían porque `exe+0x6D76D5C` = 0 [V]. Pose al desmontar: el
  vínculo jinete–caballo (2663/2916) sólo llega al que monta [V], resto [S].
- Tenderos ausentes y escenas (barrer, charlar) (`p8-npc-tiendas-animales-escenas.md`): los tenderos con nombre son
  NPC de secuenciador propio del Kliff del anfitrión cuyo 2661 sólo va al dueño; el invitado abrió 0/40 diálogos con
  ellos frente a 419/422 con NPC de horario [V]. H1 confirma el filtro `0x2D3`; H2 tumba §2.3 de `p8-s1-escenas-npc.md`.
  Requiere la opción A (pendiente de aprobación), con el 2661 copiado DESPUÉS de los 2005 de sus NPC.

Aprobado por el usuario tras la sesión: permiso de rango y embudos vf40/DeadActor/botín (implementado en `a7d043c`,
`pendientes/p8-impl-rango-embudos.md`). En curso: interpolación remota adaptativa, caballo sintético, escalada remota
y envío de wall-up.

### Noche del 10-10 (trabajo autónomo): arreglos tras la sesión 6, probados en un PC

- **Corrupción del heap del anfitrión** (`c64b13a`) [V con los dos volcados]: la CRITICAL_SECTION del heap global estaba tomada por el hilo de red del mod con `RecursionCount 40`.
  - Ahora las Req del invitado las asigna y empuja el NetworkThread del cliente (mid-hook `0x35BAD4`, heap propio del hilo).
  - Una pausa o un servidor ocupado retiene las Req, nunca saca al invitado; una excepción pone la época en cuarentena; el 2001 va a un ritmo máximo de uno cada 250 ms.
  - **Prueba (01:13–01:20):** peer dentro y menú ESC del anfitrión abierto más de 1 min: sin expulsión, `exceptions 0`, el servidor siguió enviando y el logout fue limpio.
- **Cierre `0x1831598` (00:56)**: lo provocó el guion obsoleto del peer `…act…`, que deja el personaje sin primera acción; no el ESC. Ver `pendientes/p8-crash-esc-1831598.md`.
- **Auditoría de hilos** (`7ccc885`): el invitado ya hacía todo el trabajo con el juego en el NetworkThread. Se añadieron medidas, un tope por vuelta y el salto de `game_errors` ahora se reescribe con los hilos congelados.
- **Salida del invitado** (`d9c542e`):
  - con un campo cargado y el anfitrión perdido, el proceso termina sin despedida local (que era lo que causaba `0x2045A6A`);
  - tras un volcado sin crashpad, el proceso termina en vez de colgarse en WER.
- **Escenas del usuario 2 y primeros paquetes de los NPC** (`c573fb7`):
  - `observer_scene_copy`: una copia nativa del 2810 para las escenas que abre el usuario 2 (UseItemReq).
  - `observer_hold_acks`: los primeros paquetes de un NPC retenido salen detrás de su 2005.
  - **Prueba (01:30–01:36):** 60 NPC retenidos; sus 238 paquetes (2723 ×60, 2404 ×178) salieron después de su 2005, con una espera media de 19 ms. La grabación da `keys with a 2723 BEFORE their 2005: 0`. Logout limpio y 0 ERR.
- **Menús sin pausa (`ui_pause_block`, rama del worktree `4171047`, pendiente de fusionar), probado en un PC (01:42–01:53)**:
  - **T0, `probe`:** el aviso ExtraReward pone `timeScale` 0 y el reloj de juego se congela (+0 ms en 5 s).
  - **T1, `force`:** con el aviso abierto el reloj avanza +5016 ms en 5015 ms. El menú principal (inventario: `UIGamePlayControl_Root_MainMenu`, `timeScale` 0) queda bloqueado y el mundo sigue. 0 ERR.
  - El primer ESC lanza un autoguardado (rechazado por el guardián: "Failed to save data") y el menú sale con el segundo ESC.
  - En un primer arranque con `force` el juego se quedó sin presentar fotogramas más de 2 min. No se reprodujo en el segundo arranque; queda vigilado.
- **Fusión de la noche** (`737a0d7`, `5ba103e`, `5e444d4`): `character_hold`, lecturas SEH en C4, `tunnel_compress`, `ui_pause_block` y sondas S1 (H1–H3, G1–G4). Total: 26/26 pruebas.
- **Regresión de un PC con todo activo (02:04–02:07, `cdmp.p8-noche-local.ini`, guion `act` obsoleto, aviso ExtraReward abierto)**:
  - **El anfitrión no cae.** Con el mismo guion se cerró en `0x1831598` a las 00:56; esta vez el personaje llegó con restauración 1, así que el `character_hold` no llegó a actuar y queda sin ejercitar.
  - **`ui_pause_block=1`:** sin usuario 2 la pausa del aviso actúa como siempre (+0 ms). Con el usuario 2 dentro queda bloqueada y el reloj avanza a tiempo real. Al salir el usuario 2 vuelve la pausa.
  - **Túnel:** con compresión, 13,9 MB pasan como 1,59 MB (×8,8).
  - **Hilo del túnel:** 753 ticks, el máximo de 0,1 ms; ninguno por encima de 50 ms (antes, 260–920 ms).
  - **Acks:** 58 NPC y 227 paquetes después de su 2005.
  - Logout limpio, `exceptions 0`.
  - Log: `backups/logs/cdmp_20261010-0204_noche-regresion.log`.
- **Revisión independiente de la noche** (`p8-revision-noche.md`): 0 bloqueantes y 5 importantes, corregidos en `41c0128`, con 27/27 pruebas.
  - I1: no se tragan excepciones dentro del código del juego.
  - I2: se vuelve a poner `PAGE_GUARD`.
  - I3: `notify_thread_rule` tiene clave propia.
  - I4: `ui_pause` se instala ya dentro del mundo, con un parche propio de safetyhook que no reintenta.
  - I5: cerradas las ventanas de los acks.
- **Regresión tras la revisión (03:02 y 03:10)**:
  - **Con `notify_thread_rule=1`** (log `cdmp_20261010-0302_rule1.log`): las liberaciones de NPC se retrasan, con una media de 13,9 s, un máximo de 40 s y 463 de 633. La revisión lo había previsto (I3). **Queda a 0 en todos los ini.**
  - **Con `=0`** (log `cdmp_20261010-0311_rule0.log`): 647/647 liberados en 157 ms de media; acks 2589 paquetes detrás de su 2005 con 19 ms de media; el tick del túnel, como mucho 0,6 ms; inject `exceptions 0`; 0 ERR/WRN; logout limpio.
- **Kit de la sesión 7 con Shadow:** `dist/cdmp-p8-shadow.zip`, `cdmp.asi` `90C1D238…`. En el anfitrión `cdmp.p8-s7-host.ini` (con `notify_thread_rule=0`), en Shadow `cdmp.p8-s7-shadow.ini`.
- **Investigaciones**:
  - `p8-s1-escenas-npc.md`: el nodo de escena llega en el propio 2005 del NPC, pero el invitado no tiene cargado el fichero de horario o secuenciador (lo cargan los 2335/2661 que solo recibe el dueño). El servidor no reenvía las acciones con el byte `0x2D3=1` de `StartActionReq`.
  - `p8-pausa-esc.md`: cada raíz de UI lleva su `timeScale` y `0x35CD990` elige uno en cada fotograma; el servidor sigue recibiendo. Se puede quitar la pausa con un solo gancho en `.rsrc`.

### P8 con Shadow, sesión 6 (2322 del observador, dos tramos) — 2026-10-09 23:37–23:53, **el invitado ENTRA EN EL MUNDO**

Mismo binario en los dos PC: SHA `68A47585…`.
- **Tramo A (solo medida):** igual que la sesión 5. Llegan el 2144 y el 3066 y no llega 2322; `M+0x231 = 1` y la carga queda atascada. La decisión del anfitrión dice "WOULD EMIT".
  - La conexión aguanta: rtt 38 ms, sin cortes. El arreglo del hilo de red funciona.
  - Logs: `backups/p8-sesion6A-shadow/` y `backups/logs/cdmp_20261009-2337_p8-6A-host.log`.
- **Tramo B (2322 nativo):** el anfitrión emite un solo 2322 después del 2144 y el 3066 (`IS in user 2's queue (x1)`).
  - **16,2 s después el cliente real envía su LoadingCompleteReq 2445** → 2762.
  - **El invitado entra en el mundo, se mueve y ve moverse al anfitrión.**
  - Logs: `backups/p8-sesion6B-shadow/` (con dump) y `backups/logs/cdmp_20261009-2345_p8-6B-host.log`.
- **Problemas nuevos (dato aportado por el usuario):**
  - Los NPC se ven mal: repiten una animación, la dejan a medias o se quedan en A-pose.
  - En el invitado, ESC obliga a "ver un objeto"; tras esa animación el personaje se queda inmóvil.
  - En el anfitrión, ESC (menú): a las 23:52:35 "the server could not take Req 2556", el túnel se para, empieza el logout y los 2001 se rechazan en bucle. A las 23:52:45 el anfitrión se cierra en `exe+0x47EFE20`.
  - Después el invitado se cierra en `0x2045A6A` (ya conocido); esta vez **la captura propia escribió el dump**.
- El análisis está en curso (`pendientes/p8-sesion6.md`).

### P8 con Shadow, sesión 5 (S1/S2/S3 completos) — 2026-10-09 20:38–20:48

Logs: invitado `backups/p8-sesion5-shadow/` (hora de Shadow = anfitrión − 1 h); anfitrión `backups/logs/cdmp_20261009-2038_p8g-host.log`.

**S3: resuelto en esta sesión** [V]
- `observer_hold` en el anfitrión: el invitado recibe 412 registros 2005, 409 de ellos con restauración 1.
- Llegan 2 577 `StartActionAck` 2991 y se instala la acción en las 2 214 que pasaron por el consumidor.
- Ninguna acción nula llega a `0x232D5C0`; la guarda no actúa ni una vez. **Sin cierres en juego.**

**S2: predicado identificado** [V]
- En el segundo viaje (al campo del anfitrión), el estado `0x11` se bloquea en `M+0x231 == 1`: es el mismo gestor del primer viaje, que llegó a 2 con su 2322 y que el viaje devolvió a 1. Las listas están vacías.
- Duró 250 s.
- No llega 2322 porque el anfitrión toma la rama de no dueño en `0x2BC8470`.

**S1: misma raíz probable** [V]
- Fallan los mismos 15 NPC.
- En el anfitrión corre la preparación BodyAnimation `0xA79770` para ellos: 15 llamadas, con `CC+0x70/+0x78` puestos.
- El binding de secuenciadores corre 241 veces en el anfitrión (`0x9ED431`/`0x9ED47F`). En el invitado corre 0 veces, y `0xA79770` solo 4, ninguna de ellas para esos NPC.
- [E fuerte] Al observador le falta el estado de escenas y secuenciadores del campo, el que el servidor sincroniza solo para el dueño (`0x2BC7885` → `0x2910DE0`, `0x2BC8470` → `0x287EF60`/2322).

**Corte de conexión (otro problema)**
- El hilo de red del mod en el invitado se retrasa ~1,5 s por ciclo con el diagnóstico activo mientras el anfitrión envía 400–900 KB/s.
- El rtt del mod sube a 1,47 s y hacia las 20:43:11–22 dejan de llegar paquetes en los dos sentidos. Después hay reconexiones en bucle.

**Cierre al salir con Alt+F4**
- `exe+0x2045A6A` en el invitado (WER), sin `.dmp`.

**En curso**
- Sacar el diagnóstico del hilo de red y medir ese hilo; revisar el cierre de salida.
- Investigar e implementar la incorporación del observador a las escenas y secuenciadores (`observer_stage_sync`).

### P8 S1/S2/S3 en un PC (anfitrión + `cdmp_peer`) — 2026-10-09 19:24–19:40

Ini `cdmp.p8-local.ini` (C4r + reloj + C2 + D-53 + `observer_hold` + `load_diag` + `s1_diag`). Logs:
`backups/logs/cdmp_20261009-1924_p8local1.log` y `cdmp_20261009-1933_p8local2.log`.

**Instalación**
- Los 10 puntos del anfitrión instalan sin rechazos (`REFUSED: none`).

**S2: rama de dueño (`0x2BC8470`)** [V en vivo]
- El primer viaje del usuario 2 (a su campo propio, dueño él) toma la rama de dueño y emite el 2322 hacia `A010023B`.
- El segundo viaje (al campo del anfitrión, dueño `0x90100000`) toma la rama de no dueño: no hay 2322.
- Confirma lo que el informe dedujo leyendo el código.

**S2: aviso de recompensas**
- Con el aviso abierto, el peer entra y sale limpio (época 1).
- La sonda `0xE919C0` ve eventos 9/4 de la UI ExtraReward, pero no el cierre: queda sin correlación.

**S3: `observer_hold`, primera pasada**
- Al cerrar el aviso empieza la ráfaga de NPC nuevos con restauración 0.
- Retuvo 104, pero **no liberó ni descartó ninguno**: `g_npc_size` no se actualizaba al añadir, así que el barrido veía la tabla vacía. Hallazgo bloqueante B1 de la revisión, arreglado.
- La tabla se llenó (256) y 372 NPC se anunciaron de forma nativa.

**S3: segunda pasada, ya arreglado**
- 20 liberados tras su `StartActionReq` (media 2,6 s, máx. 6,9 s). La salida del usuario 2 vació la tabla.
- Muchos NPC de tipo 6 (criaturas: faisán "Ringneck" `B01002A7`, que recibió 14 `StartActionReq` del cliente del anfitrión) **siguen con restauración 0**. "Restauración 1" no es un criterio universal.
- Antes se descartaban a los 10 s y quedaban invisibles para el observador. Ahora se anuncian de forma nativa con restauración 0 (registrado como TIMEOUT).

**S1**
- `ActionGraph_Find` del anfitrión encuentra `seq_bodyanim_action_xxx` en gráficos de 2 entradas (los dinámicos de BodyAnimation).
- En los gráficos grandes (27 173 o 4 645 entradas) no aparece. Es la comparación que falta hacer en el invitado.

**Revisión independiente**
- B1 arreglado.
- Arreglados también (`6b0ae94`): coste del barrido (I1), NPC descartados invisibles (I2), volcado de cierre más ligero (I3), conjuntos llenos (I4) y coste de `0x24314C0` (I5).

**Tercera pasada (20:11–20:18), con los arreglos de la revisión** — log `cdmp_20261009-2011_p8local3.log`
- `observer_hold`: **730 retenidos y 730 liberados** con restauración 1, media 214 ms y máximo 375 ms. **0 vencimientos.** Coste por pasada: 10 µs de media y 276 µs de máximo. Logout limpio, 0 ERR.
- **Corrección de la pasada anterior:** lo de "las criaturas de tipo 6 no llegan a restauración 1" venía del barrido defectuoso, no del motor. El anuncio nativo por vencimiento se queda solo como red.

**Lado invitado sin red, modo observe (20:19–20:23)** — log `cdmp_20261009-2019_p8guestlocal.log`
- Todas las piezas del invitado se instalan sin rechazos.
- Línea base de S2 en un jugador: cada viaje recibe 2322, `M+0x231` pasa a 2 y `0x11` sale en 0–0,5 s.
- La captura de cierres no da falsos positivos: 0 registros y 0 dumps.

Kit P8 completo listo (`cdmp.p8-host-full.ini` / `cdmp.p8-shadow-full.ini`). Ver `pendientes/p8-implementacion.md` y `p8-validacion.md`.

### Salida a media carga (cortes cut9/cut8/cut3) — 2026-10-09 16:54–16:57, **PASA las tres**

Log: `backups/logs/cdmp_20261009-1654_cortes.log` (`cdmp.c4r.ini`, ranura 108, una sola partida).
- **Causa (confirmada antes con el peer):** `GameLoadingStartReq` pone `FM+0x58`; solo
  `MoveFieldComplete` lo borra, y con él puesto el logout nativo no puede salir del campo.
- **Arreglo:** el servidor sigue la fase de movimiento del usuario 2 (3093/2437/2174/2549/2144…). Si
  se va en la ventana A (carga abierta), primero entrega `MoveFieldCompleteReq 2549 00` por su carril,
  espera su `MoveFieldCompleteAck 2144` (3 s como máximo) y después la 2001. En la ventana B, la 2001
  directa.
- **cut9** (ventana B): "no open move to close, the 2001 directly"; logout limpio, época 2 lista.
- **cut8** (ventana A): diagnóstico antes `FM+0x58 0x3`, `TS+0x3DC 1`; 2144 a los **953 ms**; después
  `0/0`; logout limpio, época 3 lista.
- **cut3** (ventana A, más temprano): 2144 a los **219 ms**; logout limpio, época 4 lista.
- Guardado: 0 rechazos (no hubo autoguardado). 0 líneas ERR aparte de los cortes provocados. El juego
  siguió respondiendo.
- **También en este commit:** el invitado tiene ahora su propia guarda de informes (K1). Queda
  pendiente la siguiente sesión P8 con Shadow.

### P8 con Shadow, segunda sesión — 2026-10-09 16:17–16:20, **entra en el mundo; cae Steam al final de la carga**

Log del anfitrión: `backups/logs/cdmp_20261009-1614_p8d.log`.
- **El título funciona:** Shadow pulsa "Jugar"; su 3093 llega con `7FFF…FFFF` y se traduce a F002. Luego
  lobby/Status F002 con `invariant ok`, aparcamiento (16:17:52) y **entrada en el campo del anfitrión
  (16:19:53, `users 2`)**. Reloj del mundo intacto.
- **Steam se satura:**
  - `SendMessageToUser` devuelve **EResult 25 (LimitExceeded) 29 156 veces**, con 519 KB pendientes,
    cola de ~2 s y rtt de 1–1,7 s (Shadow vio más de 2000 ms).
  - Nuestro transporte manda cada paquete suelto, en no fiable; los rechazos los reenvía la capa
    fiable, y eso amplifica la carga.
  - La sesión cae (16:20:04, `left`) y **el juego de Shadow se cierra** casi al final de la carga.
    Falta su log o el offset del cierre.
- **Salida a media carga, nuevo:** a los 15 s el C4 entrega la 2001, pero **la sesión no se suelta**
  (`user … still attached (+0x90)`). A los 10 s la barrera entra en cuarentena. En las pruebas con el
  peer la salida siempre fue tras cargar.
- Dos agentes en marcha: el transporte de Steam (límites de envío y contrapresión) y la salida a media
  carga, con una reproducción en un PC con `cdmp_peer` →
  [pendientes/salida-en-carga.md](pendientes/salida-en-carga.md).

### Arreglos tras la primera sesión P8 y P8-T en un PC — 2026-10-09 16:10, **P8-T PASA**

Análisis: [pendientes/p8-titulo.md](pendientes/p8-titulo.md). Los botones "Jugar"/"Cargar" del título
(`UIGamePlayControlRootTitle`, predicado `0x65EAD0`) dependen de la tabla de ranuras del gestor de
guardado local (`[[0x6D69190]+0x88]`). Esa tabla sólo la rehace `0x1870C20`, que llama el servidor local
al procesar el 2490. En P8 ese 2490 va al anfitrión, así que la tabla queda vacía.

Tres arreglos (commits `9333be3` y siguiente):
- **Título (invitado):** con la conexión remota, tras salir su 2490, el mod del invitado ejecuta la
  secuencia nativa de `SaveDataSlotRefreshReq` (`0x2A22380`, sin su Ack) en el NetworkThread: candado
  `WindowsLock<3>`, byte de ocupado `+0x60`, `0x1870C20(g)`. Comprueba bytes, candado y que el gestor
  sea el mismo que el del servidor. No hay login local. Según el análisis, no escribe partidas.
- **Cierre colgado (invitado):** si su 2001 no puede llegar (enlace roto) o el anfitrión calla 5 s, el
  mod entrega al cliente las 2003+2002 nativas con su propio `[exe+0x6A65564]`.
- **"Jugar" real (anfitrión):** el cliente real siempre manda `7FFF…FFFF` en su 3093 (`0x65DB90`). El
  servidor del anfitrión lo traduce al F002 del usuario 2 antes de la comprobación de identidad.

**P8-T** (ini [ini/cdmp.p8t.ini](ini/cdmp.p8t.ini), modo observe, un PC, log
`backups/logs/cdmp_20261009-1609_p8t.log`):
- `title slots refreshed locally … 2 slots [1, 108] | before 0 | 79 ms | (server's the same) | busy byte
  now 0`, antes del login local.
- El título queda con "Jugar"/"Cargar" activos; el login nativo posterior funciona, sin errores ni
  "ocupado".
- Primer intento: el diagnóstico se negaba porque el NetworkThread sólo gira cuando el cliente ya
  conectó. Ahora la condición es "antes de su 2490".

Kit de Shadow reconstruido con la misma build (`cdmp.asi` A6F907A0…).

### P8 con Shadow, primera sesión — 2026-10-09 15:02–15:36, **bloqueada en el título del invitado**

Kit `dist/cdmp-p8-shadow.zip` (misma build en los dos lados), ini `cdmp.p8-host.ini` / `cdmp.p8-shadow.ini`,
Steam (Shadow `Junter96` → anfitrión `ZeroSummer`). Logs del anfitrión
`backups/logs/cdmp_20261009-1502_p8a.log`, `…1514_p8b_noid.log`, `…1536_p8c.log`; de Shadow
`backups/logs/shadow_20261009-1431_p8a.log`, `…1440_p8c.log`, `shadow_…_packets.log`.
- **Funciona:**
  - Steam conecta con un rtt de unos 30 ms. En Shadow: `connection (epoch 0x1) -> REMOTE: the host is
    ready`; su 2319/2490 llegan a mi servidor, que crea su usuario; lobby normalizado a F002.
  - **La salida real de Shadow** al cerrar el juego: `out 2001 D1 07 FF`, `in 2003/2002 … 04 00 00 00 |
    index 4 vs own 4 -> equal`. En el anfitrión: `trigger=bye`, la barrera pasa en 0,6 s y
    `reconnect epoch=2 ready`. Es la primera validación real de C4 y del índice de la 2002.
- **Bloqueo:** en el título de Shadow, **"Jugar" y "Cargar" salen en gris**; sólo se puede "Nueva
  partida". La red es byte a byte la misma secuencia que recibe el propio anfitrión en su título
  (112627: 2840, 2188, 2646, 2869, 2716). Descartado:
  - la partida: es de otra cuenta, bajada de internet, y carga bien en local;
  - el arreglo de identidad: igual con `identity_fix=0`;
  - la ranura del lobby: el 2869 dice ranura 1, pero añadir `slot1` en los dos PC no cambia nada;
  - Steam Cloud.
- **Hipótesis:** el título decide con estado local que en un jugador llena el servidor interno del
  propio cliente al procesar el 2490, y que en remoto nunca se llena. Análisis con Ghidra encargado →
  [pendientes/p8-titulo.md](pendientes/p8-titulo.md). Sesión aparcada hasta tener el arreglo.
- En el anfitrión queda `slot1` como copia de la ranura de laboratorio (la ranura 1 real del usuario
  sigue en la copia de seguridad).

### Caballo del anfitrión con el usuario 2 dentro (G1 en vivo) — 2026-10-09 14:41 y 14:50, **PASA tras arreglar la barrera**

Ini [ini/cdmp.c4r.ini](ini/cdmp.c4r.ini), guion de 5 min. El caballo se llama con la tecla `1`; el usuario
lo llamó a mano. Logs `backups/logs/cdmp_20261009-1439_horse.log` y `…1448_horse2.log`.
- **14:41:** el caballo se retuvo y se liberó (`held 2 (host's 1, user 2's 1)`, `released 2`), pero al
  salir el usuario 2 la barrera entró en **cuarentena**: `userActors 0x3/0x2`. El caballo del anfitrión
  es un actor de usuario más, y la comprobación c lo comparaba con la base tomada antes del login. Era
  un falso fallo; el anfitrión siguió bien.
- **Arreglo:** la comprobación c ya no compara el total con la base. Ahora exige que el usuario que se
  fue no esté en la lista del campo y que el total sea igual a la suma de los contadores por usuario de
  la lista (`field+0x2A0`, que AddActor y RemoveActor mantienen). Se siguen exigiendo el mismo campo,
  dueño, generación y número de usuarios. Tests actualizados.
- **14:50:**
  - `held 0xB0100003 (type 5, owner host) from session (user 2)` y
    `released … after 390 ms, restore 1`: el caballo del anfitrión no llega al usuario 2 sin acción.
  - Al salir: `check c field pass (userActors 0x3/0x2 … listed actors 0x3)`, `logout complete` y
    `reconnect epoch=2 ready`.
  - Ningún evento 1000.

### Regresión tras generalizar D-53 a los hijos de todos los usuarios — 2026-10-09 14:27, **PASA**

Ini [ini/cdmp.c4r.ini](ini/cdmp.c4r.ini), guion de D-53r. Log `backups/logs/cdmp_20261009-1427_reg.log`.
- El hijo del usuario 2 se retiene y se libera a los 610 ms con restore 1, igual que antes. `held 1
  (host's 0, user 2's 1)`: los hijos del anfitrión, que ya tienen acción, no se retienen.
- `invariant ok` F002, `reconnect epoch=2 ready`, ningún evento 1000.
- **Pendiente con un solo PC:** que el anfitrión invoque su caballo o su mascota con el usuario 2 dentro.
  Se espera `held … owner host … from session (user 2)` y, en menos de 1 s, `released … restore 1`. Falta
  saber la tecla de invocar. Lo demás requiere P8 (Shadow): ver
  [p8-preparacion.md](p8-preparacion.md).

### Estabilidad 5 min con todo activo — 2026-10-09 13:59–14:04, **PASA**

Ini [ini/cdmp.c4r.ini](ini/cdmp.c4r.ini). Guion nuevo `login_e3lc00soak_child_112627.troc`, generado:
el de D-53r con 300 s de estancia antes de la despedida. Log `backups/logs/cdmp_20261009-1359_soak.log`.
- Usuario 2 dentro: `invariant ok` F002; su hijo retenido y **liberado a los 641 ms con restore 1**.
  Captura: los dos Kliff juntos.
- El anfitrión, movido por mí con Computer use, camina, ataca y baja a otra zona (Hernand Town) en el
  mismo campo.
- 5 min: **ningún `ERR`/`WRN`, ningún error del juego, ningún evento 1000**. P5 relevó 90 413 acciones
  sin errores. El reloj sigue en día 6 13:37:30, escala 12, sin publicaciones.
- Despedida a los 5 min y barrera limpia en 0,9 s (103 claves); `reconnect epoch=2 ready`. Ranura 108
  intacta.
- Nota del usuario: el pájaro blanco trae misiones; **las mascotas son perros y gatos**. La de esta
  partida (`cd_m0002_00_hatch`, tipo 6) sería una de ellas.

### D-53r (liberación del hijo con su primera acción) — 2026-10-09 13:57, **PASA**

Ini [ini/cdmp.d53.ini](ini/cdmp.d53.ini), guion nuevo `login_e3lc00bye_child_112627.troc`: el peer
aprende la clave del hijo con `learn_child` en los 2005 y le manda la 2223 de un hijo tipo 6 de 112627,
con su clave, el hash `0x866C7489` y la posición del usuario 2. Logs `backups/logs/cdmp_20261009-1351_d53r.log`
(primer intento) y `…1356_d53r.log`.
- **Primer intento (13:51), sin liberación:** la acción se envió, pero las 4 comprobaciones de liberación
  tras `StartActionReq` se rechazaron por el hilo. El servidor usa **varios hilos de ámbito servidor**, y
  el último que llamó a `NotifyAppear` no es el único legítimo. **Arreglo:** la liberación se acepta en el
  hilo del manejador `StartActionReq` con la comprobación TLS, porque el propio juego llega a
  `NotifyAppear` desde ahí cuando una acción cambia de sector (`0x2AF8090`). El respaldo sigue con la
  prueba estricta. El primer intento acabó en un resultado seguro: retenido, vaciado al salir y sin
  cierre.
- **Con el arreglo (13:56):**
  - `held 0xB0100247` y luego `released … after 641 ms, restore 1 (after StartActionReq)`: el servidor
    aceptó la primera acción del hijo.
  - El cliente del anfitrión lo crea (`CompCreate`) con **byte 1**, y su restauración arranca la capa 0
    (`cur 1 play 1`). Desde ahí se actualiza normal, sin contención (`HorseDiag update real … cur/play
    true`).
  - Guion 28/28 con despedida, ningún evento 1000 y juego vivo 40 s después. Ranura 108 intacta.
- **Con esto, D-53 queda completo**: el hijo no se publica sin acción y, en cuanto su controlador le da
  la primera, aparece en el mundo del anfitrión con una acción válida. Lo visual (la mascota moviéndose
  en pantalla) y su IA real necesitan un cliente real (P8).

### Prueba de conjunto (C4r + C2 + D-53 + reloj) — 2026-10-09 13:32, **PASA**

Ini [ini/cdmp.c4r.ini](ini/cdmp.c4r.ini), con todo lo nuevo activo por defecto y sin sondas. Dos ciclos,
uno con despedida y otro con corte. Log `backups/logs/cdmp_20261009-1331_all.log`.
- Cada época: `C2 identity: invariant ok … F002` (en la época 2 también, con su lobby nuevo),
  `child hold: held 0xB01… restore 0` y, al salir, `child hold: user 2 is leaving/gone; 1 held entry
  dropped` (el vaciado desde el tick de red funciona). Barrera limpia (102–103 claves) y `reconnect
  epoch=N ready`.
- Ningún evento 1000 de Windows. Reloj del mundo intacto (día 6 13:37, escala 12). Ranura 108 intacta.
- Sin sondas, el hijo ya no llega nunca al cliente del anfitrión: no hay "Actor 생성", porque D-53 lo
  retiene antes.

### C2 (identidad propia del usuario 2, D-54) — 2026-10-09 13:28, **PASA**

Ini [ini/cdmp.c2.ini](ini/cdmp.c2.ini) (d53 + `identity_fix=1`), guion con despedida, ranura 108. Log
`backups/logs/cdmp_20261009-1328_c2.log`. Hecho por mí.
- **Primer intento (13:23):** el fallo seguro actuó. El juego siguió vivo y el usuario 2 recibió la salida
  nativa, con el motivo "lobby loaded off the server thread". Causa: `server_thread_id()` es el hilo que
  ejecuta `OnRecvPacket` y construyó la sesión 2, pero los manejadores y las cargas corren en **otro
  hilo** (T29232) del ámbito servidor. Arreglo:
  - `identity_fix` comprueba el byte TLS del juego (`+0x1EC`, el que pone `0x27733A6`).
  - La liberación de `child_hold` sólo corre en el hilo donde el juego llama a `NotifyAppear`.
  - Log `cdmp_20261009-1322_c2_thread.log`.
- **Con el arreglo (13:28):**
  - El lobby del usuario 2 se normaliza dos veces, en el 2490 y en la recarga del 3093:
    `7FFF…FFFF → 7FFF…F002`, entrada 1 de 1, número único.
  - El Status de su Kliff `0xA0100227` pasa a F002.
  - `invariant ok: lobby = Status = F002` antes de las fases.
  - Su 2358 se publica con **F002** y el del anfitrión sigue en `7FFF…FFFF`.
  - Guion 25/25: el servidor aceptó su 3093 con el número nuevo, que el peer aprende del 2869.
  - Ningún cierre; el hijo sigue retenido (D-53).
- La tabla de D-53 seguía con la entrada tras la salida: el vaciado sólo corría en el hilo de
  `NotifyAppear`. Ahora lo hace el tick de red del mod (no llama al juego). Falta verlo en vivo.
- Límites: hay lectores internos sin instrumentar entre los 16 componentes posteriores a Status; los
  personajes intercambiables sólo normalizan el de ese login; P9 (versión de restauración y dueños de
  objetos) sigue aparte.

### D-53 (retención del hijo en el servidor) + C2-0 limpia — 2026-10-09 13:02, **PASA: ningún cierre**

Ini [ini/cdmp.d53.ini](ini/cdmp.d53.ini): comp + `child_hold=1`, con las sondas de identidad y compañeros,
que hacían crear al hijo. Log `backups/logs/cdmp_20261009-1300_d53.log`. Hecho por mí.
- `child hold: held 0xB0100228 (type 6, owner user 0x90100226, restore 0) from session …(user
  0x90100000)`: sólo lo recibe su dueño. El cliente del anfitrión **no lo crea** (no hay `CompCreate`
  del hijo).
- A los 5,3 s, `CompHold timeout`: sigue retenido, porque el peer no le da acción. Nunca se libera con 0.
- NotifyAppear se llamó 912 veces y sólo se partió una.
- Guion 25/25 con despedida. **Ningún evento 1000 de Windows**: ni al aparecer ni a los ~1 s de la
  salida (`0x17F328F`). Juego vivo 30 s después. Ranura 108 intacta.
- Pendiente menor: tras la salida la tabla conserva la entrada (`still held 1`, `user 2 gone 0`). No es
  peligroso, porque se revalida por clave y época, pero hay que vaciarla al irse el usuario 2.
- Pendiente: la liberación (`released`) sólo se probará cuando alguien dé la primera acción al hijo,
  sea el guion del peer (Req2223 del hijo, ver
  [pendientes/companeros-publicacion.md](pendientes/companeros-publicacion.md) §4) o un P8 real.

**Identidad (C2-0), primeras lecturas válidas** (la sonda corregida, en la misma partida):
- Carga del personaje del usuario 2 (`0x20688A0`, 18 componentes): Status es el #1 y deja
  `characterNo = 7FFF…` desde el componente #1. Cargan **16 componentes más** después, y **`0x2AE1010`
  no se llama dentro de la carga**.
- `0x2941020` (fases) y el 2358 (`0x28F31F0`, desde `exe+0x10430A4A`) llegan **después** de volver de
  la carga, con `7FFF…`. El empaquetador `0x17908C0` también. Los hijos llevan `-1`.
- El lobby carga en su propio `0x20688A0` (un componente, vf50 `0x2B7F4D0`).
- Versión de restauración 5 (anfitrión) → 5 (usuario 2), la misma partida.
- **Conclusión:** normalizar el `characterNo` del usuario 2 **al volver de `0x20688A0`**, antes de
  `0x2941020`, va por delante de todas las publicaciones observadas. Es el candidato de identidad.md
  §5.2, que pasa de condicionado a respaldado. Queda el límite de que puede haber lectores internos no
  instrumentados entre los 16 componentes.

### Compañeros (sonda + barrera por dueño, D-52) — 2026-10-09 11:50–12:01

Ini [ini/cdmp.comp.ini](ini/cdmp.comp.ini): reproduce el cierre, porque con las sondas el compañero se
crea. Logs `backups/logs/cdmp_20261009-1150_comp.log`, `…1156_comp_guard1.log`, `…1159_comp_guard2.log`.

**1. Lo que mide la sonda (11:51).** La partida tiene **un solo hijo**: `B0100002`, tipo 6, ranura 1,
info estática `03 06…`. El usuario 2, con la misma partida, trae el mismo.
- 11:51:56.291: el servidor crea al hijo **después** del viaje compartido. En el aparcamiento no existe,
  así que no se le puede dar acción antes, como a Kliff en D-48.
- +13 ms: el cliente del anfitrión lo crea. Rama completa: búsqueda ok, fábrica ok, principal adquirido
  con `+5E 0x10`, enganche 0, mapa 0, `CREATED`.
- +121 ms: restauración con **byte 0**; L0 tiene chart y ni acción ni reproducción, y la restauración
  no cambia nada.
- +73 ms: `CompCrash` en `0x232D5C0`, capa 0: `[r8+0x10]` es la **acción actual** (r8 copia la capa
  `+0..+0x18`), no la reproducción.
- `0x232D5C0` cuelga de `0x232D2A0`, que se llama desde `0x85FC94` y `0x860110`, ambos dentro de la
  actualización del personaje `0x85FBA0`.
- Por qué los compañeros propios no fallan: el cliente que los controla pone su acción localmente. Un
  actor remoto sólo la recibe de fuera (su controlador, vía P5), y el motor no espera.

**2. Barrera de activación por dueño (D-52).** La generalización que propone companeros.md §C, acotada a
actores remotos de la sesión cooperativa: las dos contenciones (capas `0x2329700`, actualización
`0x85FBA0`/`0x860D30`) cubren ahora todo actor cuyo dueño (`actor+0x90` en el cliente) es el usuario 2.
Su clave sale de su `MainUserDataStartAck` 2646. La guarda de capas considera también la acción actual
`+0x10`.
- Primer intento (11:57): la retención de capas actuó, pero el cierre llegó por `0x85FBA0`.
  `on_cc_update` volvía antes de la contención para claves no vigiladas. Arreglado.
- Segundo intento (12:01): **ya no hay cierre al aparecer**. El hijo `B0100244` se crea y la contención
  salta su actualización (82, luego 105 veces) hasta la despedida. Guion 25/25.
- **Nuevo cierre a las 12:01:50**, unos 5 s después de la salida (2001 a las 12:01:41), en
  `exe+0xAAFC1D`: `mov rbx,[rdi+0x210]` con `this` inválido, en `0xAAFC00` (función genérica, unos 180
  llamadores). Nunca pasó en las salidas en que el hijo se rechazaba.
- **Repetición con la sonda `CompBadThis` (12:06, log `…1204_comp_badthis.log`).** Cierre 10 s tras la
  2001. `this` es **nulo**, no liberado. La llamada viene de `0x86987D`, dentro de `0x869630`, que a su
  vez llama `.xtls 0x984A800`:

  ```
  r14 = [rdi+0xF8]
  ebx = clave de [[r14]+8]
  rcx = [[0x6D691B8]]
  rax = [rcx+0x40]
  rcx = rax ? rax-0x28 : 0
  call 0xAAFC00
  ```

  El código prevé el nulo con un `cmove`, pero luego no lo comprueba. Análisis con Ghidra encargado →
  [pendientes/companeros-cierre-salida.md](pendientes/companeros-cierre-salida.md).
- **Resultado del análisis: ese cierre es secundario.** Windows anota **dos fallos por proceso**; yo
  sólo había leído uno.
  - El primero es **`0x17F328F`, ~1 s después de la 2001**. `0x17F3240` lee la reproducción de la capa
    del hijo (nula) al aplicar `CharacterDataDefinedStatAck` 3095 (`0xB74AB0` → `0x630BF0`). Es un
    manejador de red, que D-52 no cubre.
  - El segundo es la destrucción del juego (`0xACD400`): apaga el `CameraManager` (global `0x6D691B8`,
    `+0x40` = PlayerCamera) y luego la retirada del CC con control local (`+0x1E9`) llama a `0x869630`
    con la cámara ya nula.
- **Conclusión: contener lector por lector no tiene fin.** Ya son seis lectores nativos que fallan con
  este estado: `0x232D5FF`, `0x88F832`, `0x1F63FE6`, `0x1831598`, `0x17F328F` y el de C1b. La raíz es
  que el actor remoto se publica sin acción.
- **Arreglo de raíz (D-53, a implementar):**
  - El servidor **no publica el hijo remoto a los demás usuarios hasta aceptar su primera acción**
    (2223→2723), de modo que sale con restauración 1, como Kliff desde D-48. Falta localizar el punto
    de emisión.
  - El controlador da esa primera acción: el guion del peer, a partir de la Req2223 de un hijo
    capturada en 112627; en P8, la IA del cliente real.
  - D-52 se queda como red de seguridad.
- Con `cdmp_peer` el hijo nunca recibe acción, porque el peer no tiene IA de compañeros. En P8 la
  recibiría a los 0,3–0,4 s (mediana de los hijos propios en 112627).

> **Nota del usuario (2026-10-09):** los "mercenarios" (claves `B01…`, hijos del usuario) serían la
> **mascota** que acompaña a cada jugador y recoge objetos de los enemigos muertos, y el **caballo**.
> Encaja con 112627: tres hijos `B0100002/3/4`, ranuras 1/2/3, tipos de registro 4/5/6. La sonda de
> compañeros registra ranura y tipo para distinguirlos. Además, el juego tiene **otros dos personajes**
> que ayudan al jugador y entre los que puede **cambiar** para jugar con ellos. Uno de los hijos podría
> ser uno de ellos. Riesgo a vigilar en identidad (C2) y P8: el personaje principal de un usuario puede
> cambiar dentro de una partida.

### C4r (reconexión, D-51) — 2026-10-09 11:31, **PASA: cuatro entradas del usuario 2 en la misma partida**

Ini [ini/cdmp.c4r.ini](ini/cdmp.c4r.ini), ranura 108. Hecho por mí con Computer use. Log
`backups/logs/cdmp_20261009-1129_c4r.log`, grabaciones `D:/cdmp-grabaciones/c4r_r*.bin`.
- **Primer intento (11:26), fallo de diseño:** la época 2 nunca construía su sesión. La sesión de cada
  época se construye en el hilo del servidor dentro de `on_server_recv`, es decir, cuando el servidor
  recibe algún paquete. Con el anfitrión quieto no recibía ninguno, y la primera Req del invitado
  esperaba a que la sesión existiera: bloqueo mutuo (log `cdmp_20261009-1124_c4r_epoch2_stuck.log`).
  **Arreglo de raíz:** `server_accepts_guest()`. Con una época nueva pendiente, la primera Req entra en
  la cola nativa, y al tomarla el propio hilo del servidor construye la sesión antes de enrutar por la
  etiqueta de época. También se aplicó el hallazgo 3 de la revisión: `g_session2` se publica antes que
  su época.
- **Con el arreglo (11:31–11:34), cuatro ciclos:**

  | Época | Salida | Guion | Barrera |
  |---|---|---|---|
  | 1 | despedida | 25/25 | completa en 0,5 s |
  | 2 | despedida | 25/25 | completa en 0,9 s |
  | 3 | corte del enlace (15 s) | 21/21 | completa |
  | 4 | despedida | 25/25 | completa |

  Cada época tiene su sesión nueva y `reconnect epoch=5 ready` al final.
- La barrera, cada vez: la sesión se desprende (`+0x90 0`); **102 claves** del usuario 2 dejan de
  resolver en el SAM; el campo del anfitrión vuelve a la base (`users 0x1 userActors 0x2`, mismo
  dueño, generación y abierto); colas vacías. La primera lectura, a 0,25 s, todavía falla (7–8 claves,
  cola de salida); la siguiente ya pasa.
- El reloj del mundo no se movió en ocho viajes del usuario 2 (día 6 13:37:40, escala 12). El
  mercenario fue rechazado sin consecuencias en cada entrada. Kliff camina y ataca al final. Sin `ERR`
  salvo el esperado del corte. Ranura 108 intacta.
- Revisión independiente de `f15596e`: nada bloqueante. Quedan dos hallazgos importantes que no
  afectan a esta prueba: el espejo E4 (`mirror_host`) puede impedir la barrera, y un login que falla
  antes de crear al usuario lleva a cuarentena a los 60 s. Pendientes.

### C2-0 (sonda de identidad, solo lectura) — 2026-10-09 11:07, **cierre por el mercenario del usuario 2**

Ini [ini/cdmp.c2probe.ini](ini/cdmp.c2probe.ini), guion `login_e3lc00bye_112627.troc`, ranura 108. Log
`backups/logs/cdmp_20261009-1105_c20_crash.log`.
- **Cierre a las 11:07:33 en `exe+0x232D5FF`**: `movzx eax, word [rax+0xE4]` con `rax=[r8+0x10]` nulo,
  dentro de `0x232D5C0`. Es el punto conocido "capa de acción con chart y sin playback" (03-10, ver
  `kActionLayersAdvance` en `offsets.h`). No es ningún punto de la sonda.
- **Causa:** el compañero del usuario 2 (`0xB0100234`) **sí se creó** en el cliente del anfitrión. En
  R1/C4a/C4b el cliente lo rechazaba ("Actor 생성에 실패했습니다" con `…B01002xx`) y no pasaba nada;
  aquí no aparece ese error. Llegó sin acción (restauración 0, [pendientes/companeros.md](pendientes/companeros.md)).
  La guarda de capas sólo cubre la clave del personaje del usuario 2, no la de su compañero. Que se
  cree o no parece una carrera, y puede que la sonda haya cambiado los tiempos. **Es el problema de
  compañeros, no una contención que falte:** pasa a ser lo siguiente.
- La sonda tenía un fallo: `hex_or(leer(x), x)` imprimía 0 porque MSVC evalúa los argumentos de derecha
  a izquierda. Arreglado; los ceros de `characterNo now`, `phases` y `2358` no valen.
- Datos válidos (lecturas no afectadas):
  - En la carga de componentes del personaje del usuario 2 (`0x20688A0`, 18 componentes, 1 stub),
    Status es el componente **#1**, y **16 componentes reales cargan después de él**.
  - `characterNo` pasa de `-1` a la entrada a `7FFF…` al volver.
  - **`0x2AE1010` (dueño de objetos con el `characterNo`) no se llama dentro de la carga** (0 veces).
  - La versión de restauración (`0x6C8CD98`) pasa de 0 a 5 con la carga del anfitrión. El usuario 2
    escribe 5 sobre 5: es la misma partida, así que aquí no se ve el problema de P9.
  - El 2358 del usuario 2 sale por `0x28F31F0` llamado desde `exe+0x10430A4A`.

### R1 (P-RELOJ, D-50): el reloj del mundo sigue siendo del anfitrión — 2026-10-09 10:34, **PASA**

Ini [ini/cdmp.c1f.ini](ini/cdmp.c1f.ini) (`clock_gate=1` por defecto), guion `login_e3lc00bye_112627.troc`,
ranura 108. Hecho entero por mí con Computer use. Log `backups/logs/cdmp_20261009-1032_r1.log`,
grabación `D:/cdmp-grabaciones/r1.bin`.
- Vigilante, durante la carga del anfitrión: `día 1 12:00 → día 1 00:00 escala 0 → día 0 24:00 escala 0 →
  día 6 13:30:17 escala 12 → día 6 13:37:35 escala 12` (10:33:41). **La carga del propio anfitrión
  también pasa por el aparcamiento**, y su viaje al campo real reinstala la hora de la partida. Eso es lo
  nativo.
- Usuario 2:
  - 10:34:39, viaje al aparcamiento (campo `…7700`, dueño `0x9010023F`). Su personaje toma la hora fija
    (`día 0 24:00 escala 0`), `nothing published`, reloj del mundo sin cambios.
  - 10:34:40, viaje al campo del anfitrión (`…7A10`). Toma `día 6 13:37:41 escala 12`; nada publicado.
  - Dos `LoadingComplete` con flag 0; reloj sin cambios.
- Desde 10:33:41 hasta el cierre **no hubo ni una publicación más**, ni durante la entrada, ni la estancia,
  ni la salida (C4a de nuevo: 2001 entregada, `users 0x1`). La sonda da los mismos 32 bytes, escala 12,
  antes y después.
- Juego estable, sin `ERR`. Cerrado a la fuerza; ranura 108 intacta.
- **Queda:** el cielo del cliente del anfitrión (no se registra) y P8, cuando un cliente real mande
  `LoadingCompleteReq 01` (el flag pasará a 0 y su campo privado quedará en pausa; ver la revisión).

### C4a (T10, salida con despedida) — 2026-10-09 10:08, **PASA** (primera prueba hecha por mí con Computer use)

Ini [ini/cdmp.c1f.ini](ini/cdmp.c1f.ini), guion `login_e3lc00bye_112627.troc`, ranura 108. Abrí el juego
desde Steam, cargué la ranura 108 y lancé `cdmp_peer` yo mismo. Log
`backups/logs/cdmp_20261009-1008_c4a.log`, grabación `D:/cdmp-grabaciones/c4a.bin`.
- Guion completo (25/25): las dos últimas esperas son 2003 y 2002, así que llegaron las dos respuestas
  de 7 bytes (la lista de `cdmp_peer` sólo muestra las 40 más frecuentes y no salen).
- 10:08:39 usuario 2 dentro (`users 0x2`, campo `0x24920D17A10`, generación `0x81020001`);
  10:08:56 `logout asked (goodbye from user 2)` y `2001 handed to the server`; servidor: `logout Acks
  out 2`, `Req refused after it 0`.
- Sonda `user 2 gone` a los 3 s: **mismo campo y misma generación, `users 0x1`**; sesión 2 sin usuario.
- En pantalla, sólo queda el Kliff del anfitrión. El juego siguió respondiendo ~70 s sin errores ni
  avisos; lo cerré a la fuerza porque el teclado de Windows mandaba combinaciones (ver abajo). La
  ranura 108 no se tocó (última escritura el 08-10 22:36) y no hay volcados.
- `user actors 0x2` tras la salida: **es lo normal**. La base de C4b, con el anfitrión solo en el
  mundo, también da `0x2`.
- Reloj: la sonda de las 10:07:49 (`01…00 01…`) se tomó durante la carga del anfitrión, y no hubo
  ninguna con el mundo ya cargado antes del login. No sirve para culpar a nadie; C4b sí lo aclara.
- Nota operativa: en esta sesión Windows actuaba como si la tecla Windows estuviera pulsada (`e` abrió
  el Explorador y `w` el panel de widgets). El usuario lo arregló antes de C4b.

### C4b (T10, salida por pérdida del enlace) — 2026-10-09 10:16, **PASA**

Misma configuración, guion de C1g (`login_e3lc00_112627.troc`, sin despedida); al acabar, `cdmp_peer`
se cierra y el enlace se pierde. Log `backups/logs/cdmp_20261009-1016_c4b.log`, grabación
`D:/cdmp-grabaciones/c4b.bin`. Hecho entero por mí con Computer use.
- Base a las 10:15:51, el anfitrión solo en el mundo: `users 0x1 user actors 0x2`, campo
  `0x206C6D17A10`, generación `0x81020001`.
- 10:16:51 usuario 2 dentro (21/21, `users 0x2`); P5 relevó 7523 acciones. `cdmp_peer` terminó a las
  10:17:06.
- 10:17:21, 15 s después: `the guest has been gone for 15 s`, el túnel se para, `logout asked (link
  lost …)` y `2001 handed to the server`; servidor `logout Acks out 2`.
- Sonda `user 2 gone`: **igual que la base** (`users 0x1 user actors 0x2`, mismo campo y generación).
- Después moví a Kliff (adelante y atrás) y ataqué: la posición cambia en el log y no hay ni un
  `ERR`/`WRN` aparte del aviso esperado del túnel. Juego cerrado a la fuerza (bloqueo de guardado activo;
  la ranura 108 no se tocó).
- **Reloj, la prueba que faltaba:** `server FieldTime` con valores reales a las 10:15:51 (`06…0D…25…24…
  AC02…3A5F01`) y **a ceros (`01…01…`) en cuanto entra el usuario 2** (10:16:51), y así se queda tras su
  salida. Confirma [pendientes/reloj.md](pendientes/reloj.md): lo rompe la carga del usuario 2 y no se
  recupera al irse.
- Nota operativa: el aviso del juego "Extra Rewards List" sale en cada carga (la ranura no guarda) y se
  cierra **manteniendo** la barra espaciadora.

### C1g (T07, E3b + P5) con la primera acción en su sitio — 2026-10-09 07:40, **PASA**

Ini [ini/cdmp.c1f.ini](ini/cdmp.c1f.ini), guion `login_e3lc00_112627.troc` con el paso 4b (D-48).
Log `backups/logs/cdmp_20261009-0733_c1g.log`, grabación `D:/cdmp-grabaciones/c1g.bin`.
- Guion completo (21/21). H1/H2: el usuario 2 en el campo del anfitrión (`users 0x2`).
- `HorseDiag ... RESTORE ... USER 2 0xA010052C ... restoreByte=1` y 50 ms después las dos capas con
  `cur=true play=true`: el cliente del anfitrión creó al segundo Kliff **con su acción**.
- **64 s después, el juego sigue abierto**; contenciones a 0 (`animation skipped 0`, `character update
  skipped 0`). P5: `finders control ... 14093 relayed, 0 errors`. Primera vez que dos jugadores
  comparten mundo de forma estable.
- Sigue saliendo el mercenario rechazado (inofensivo) y `server FieldTime` a ceros tras entrar el
  usuario 2 (pendiente de investigar).
- **Lo que vio el usuario** (captura en `backups/logs/c1g_dos_kliff.png`): el segundo Kliff de pie a
  su lado, con su mismo equipo, quieto; la ropa reacciona al viento y parece respirar (acción de
  reposo). Cerré el juego a la fuerza a petición del usuario (desde el teléfono, sin teclado); ranura 2
  idéntica a la copia, nada pendiente de envío.

### C1e (T07, E3b) con la contención de `horse_diag` — 2026-10-09 06:49, **se ve al segundo Kliff; cierre a los 7 s**

Log `backups/logs/cdmp_20261009-0647_c1e.log`. **Primera vez que el anfitrión ve al segundo Kliff**
(captura del usuario: con escudo redondo, en el camino junto a los guardias). La sonda del anfitrión:
`field ... users 0x2 user actors 0x4`. `HorseDiag ... USER 2 0xA01002BE`: capas con gráfico y sin
acción; la contención saltó su actualización 12 veces en 5 s.
- Cierre a las 06:49:27 en `exe+0x1831598`, `FUN_141831560` (10 llamadores, entre ellos la decisión
  del icono del minimapa): lee `[[[actor+0x68]+0x40] capa +0x10]+0x10B`, la acción actual, nula. Misma
  causa en un tercer sitio: **contener no basta, hace falta que tenga acción.**
- `server FieldTime` pasó a ceros al entrar el usuario 2 (`01000000…`): vigilar (riesgo R1 de E3).
- **D-47:** probable razón de que nunca llegue su acción: el relevo "excepto el que controla" está
  vaciado en esta build (A2), así que el `StartActionAck` del usuario 2 no llega a nadie. P5
  (`relay_fix`) existe pero nunca se probó: prueba **C1f** con [ini/cdmp.c1f.ini](ini/cdmp.c1f.ini)
  (`relay_fix=1`) y el guion [data/login_e3lc00actfast_112627.troc](data/login_e3lc00actfast_112627.troc)
  (sin la espera de 5 s antes de `LoadingCompleteReq`, y la acción al final).

### C1d (T07, E3b) con la acción temprana — 2026-10-09 06:44, **la acción se ignora; mismo cierre**

Log `backups/logs/cdmp_20261009-0642_c1d.log`, grabación `D:/cdmp-grabaciones/c1d.bin`.
- El `StartActionReq` salió a los 0,6 s (con la clave del jugador del usuario 2) y el servidor **no
  contestó nada** (ni `StartActionAck` ni `StartActionNak`); el Kliff llegó al cliente del anfitrión
  sin acción (`has no action yet`) y el juego se cerró otra vez en `0x88F832`.
- **D-46:** el movimiento que cae (`MovementController_FrameUpdate` `0x88EB70`) solo lo llama
  `CharacterControl::PostUpdate` `0x860D30` (Ghidra). La contención de `horse_diag` del cooperativo
  salta justo `0x85FBA0` y `0x860D30` de una copia sin acción: se instala con `share_field` y cubre
  también la clave del jugador del usuario 2 (`HorseDiag ... USER 2` en el log). Prueba C1e.
- **Identidad (C2)**, [data/c2-identidad.md](data/c2-identidad.md): el `characterNo` no está en la
  partida, el servidor lo fija a `0x7FFF…` al cargar el lobby (`0x2B7F66C`); el cliente y el servidor
  tienen tablas por `characterNo` (caché de equipo) que devuelven la primera coincidencia. Arreglo
  propuesto, sin hacer aún: hook `.rsrc` en `0x2B7F4D0` que, para el usuario 2, cambie el `characterNo`
  de su lobby a `0x7FFFFFFFFFFFF0nn`; el guion ya lo toma de 2869 (`CHARACTER_NO`).

### C1b (T07, E3b) con la guarda D-44 — 2026-10-09 06:38, **la guarda actúa; el juego se cae en otro sitio**

Log `backups/logs/cdmp_20261009-0637_c1b.log`, `cdmp_peer` en `backups/logs/cdmp_peer_c1b.txt`.
- H1/H2 como en C1 (`users 2 user actors 3`); el mercenario, rechazado igual (inofensivo).
- La guarda saltó la animación del jugador del usuario 2 (`copy 0xA01002BE has no action yet; ... skipped
  1 times`): **ya no se cae en `0x1F63FE6`**.
- Cierre 1,5 s después en `exe+0x88F832`, dentro de `MovementController_UpdateByMode` (`0x88F4E0`,
  Ghidra): `mov rcx,[[r14+0x10]]` y luego `[rcx+0x38]` con `rcx` nulo. Misma causa: el actor remoto
  **sin acción actual** (ahora en el movimiento, no en la animación). Parchear función a función no
  acaba: hay que darle una acción **antes** de que llegue al cliente del anfitrión.
- **D-45:** variante del guion [data/login_e3lc00actearly_112627.troc](data/login_e3lc00actearly_112627.troc)
  que manda el `StartActionReq` justo después de `LoginCharacterToFieldServerReq` (cuando el usuario 2
  aún está en su propio campo), para que al compartir el campo su `ActorLoadData` ya traiga acción.
  Prueba C1d. Si falla, alternativa: no mandar al cliente del anfitrión los registros del usuario 2
  hasta que tenga acción (filtro en `0x35CA00`), o del todo (opción (c) de `c1-actor-rechazado.md`).

### C1 (T07, E3b), el usuario 2 en el campo del anfitrión — 2026-10-09 03:34, **H1/H2 funcionan; el juego se cae**

Ini [ini/cdmp.e3b.ini](ini/cdmp.e3b.ini), ranura 108. `cdmp_peer` en `backups/logs/cdmp_peer_e3b.txt`,
log en `backups/logs/cdmp_20261009-0332_e3b.log`.
- `Session2 E3b ON`. Primer viaje del usuario 2 a `FieldInfoKey 0x3` (el nivel de la captura):
  `own field as before (another level than the host's)`. Segundo viaje, a `FieldInfoKey 0x1` (el
  del anfitrión): **`given the host's field 0x...7A10`**; H2: `users 2 user actors 3`. Primera vez
  que dos usuarios están en el mismo `ServerField`, sin `eErrNoHasntEnterFieldPermision`.
- 60 ms después, el **cliente del anfitrión** registra un error, `Actor 생성에 실패했습니다` ("falló la
  creación del actor", `exe+0x8BDF7C`). `비정상적인 공개정보입니다` no es un segundo error: es la
  descripción del código (`eErrNoInvalidPackPublicInformation`, `0xA5C2E5FE`). Según
  [data/c1-actor-rechazado.md](data/c1-actor-rechazado.md), el actor rechazado es el **mercenario**
  del usuario 2 (`0xB01002C1`, dueño `0x901002BF`), que llega 35 ms después del jugador, cuando este
  aún carga [S]; el fallo lo destruye limpiamente. **No** es por el `characterNo` repetido. El jugador
  del usuario 2 (`0xA01002C0`) sí se creó.
- **Cierre por excepción** a las 03:34:19: `0xC0000005` en `exe+0x1F63FE6`, dentro de
  `ActionLayer::AdvancePlayback` (`0x1F63F70`, nombre de Ghidra): `mov rdx,[rax+0x10]` con
  `rax = [rcx]` nulo. Es el mismo cierre que la copia del caballo del cooperativo: un actor remoto
  con gráfico de acciones y **sin acción**. El único actor remoto es el jugador del usuario 2, y
  `cdmp_peer` no le mandó nunca un `StartActionReq` (la grabación no tiene `StartActionAck` suyo).
- Sin informe a Pearl Abyss (K2: Sentry no arrancó; `DumpCache` sin nada). Windows Error Reporting sí
  registró el cierre (`APPCRASH`, solo firma: módulo y desplazamiento). Ranura 2 intacta.
- **Siguiente:** D-44 (guarda de capas de acción para el jugador del usuario 2 y variante del guion
  con una primera acción). El `characterNo` repetido (C2) sigue pendiente para el caché de equipo.

### B3 (T05, E3), personaje del usuario 2 — 2026-10-09 03:08, **pasa: cada usuario en su campo**

Ini [ini/cdmp.b3.ini](ini/cdmp.b3.ini), ranura 108, `--var SAVE_SLOT=108`. `cdmp_peer` en
`backups/logs/cdmp_peer_b3.txt`, grabación `D:/cdmp-grabaciones/e3.bin`, log del juego en
`backups/logs/cdmp_20261009-0308_b3_full.log`.
- Guion `login_e3lc00` completo (19/19, los dos ciclos de carga). El servidor mandó **38 089
  paquetes** a la sesión 2 (36 006 de la 2264, 807 de la 2146…): el mundo como lo vería el cliente.
- Usuario 2 `0x901002D4` con **personaje `0xA01002D5`** cargado de la ranura 108.
- **Campos distintos**, como predijo `opcion-d-e3-riesgos.md`: anfitrión `field ...7A10 owner
  0x90100000 users 0x1 generation 0x81020001`; usuario 2 `field ...7700 owner 0x901002D4 users 0x1
  generation 0x83020000`. El usuario no vio un segundo Kliff ni notó nada. **Siguiente pieza: E3b**
  (bloque C, `opcion-d-campo-compartido.md`).
- `FieldTime`: solo cambia el contador final; sin salto de hora.
- Claves: libres + actores = 15 501 también aquí (`0x38BF` + `0x3CE`): no hay reserva aparte de 101
  por usuario; el presupuesto de claves es el de los actores.
- Sin errores del juego, sin guardados rechazados durante el guion; al salir, el guardado de cierre
  rechazado de siempre. Ranura 2 idéntica a la copia. Cierre normal.

### B2 repetida (T04, E2), con id 0 — 2026-10-09 03:01, **pasa**

Ini [ini/cdmp.b2.ini](ini/cdmp.b2.ini), ranura 108, K1 + K2. `cdmp_peer` en
`backups/logs/cdmp_peer_b2b.txt`, grabación `D:/cdmp-grabaciones/e2b.bin`, log del juego en
`backups/logs/cdmp_20261009-0300_b2b.log`.
- **Guion completo** (`script done at step 4/4`): 2840 → 2646 → 2869 → 2716, y además 3
  `ServerTimeInfoAck` 2188 (cada ~5 s, como predijo D-40) y 2 `GlobalGameEventUpdateAck` 2093.
- **Usuario 2 creado**: `session 2 ... id 0x0 | user 0x... key 0x901002BE` (no `...0001`: índice libre
  de la SAM, como dijo el análisis). El anfitrión sigue con `0x90100000` y su sesión 1 intacta.
- Lecturas del guardián 13 → 24: `UserLogin` carga una partida para el usuario 2 (leer está
  permitido). Ningún error del juego durante el login; el usuario no notó nada.
- **Claves:** no bajan 101 aparte. Libres + actores = 15 501 en todas las líneas (al login:
  `0x39AE` + `0x2DF`). El usuario 2 no reserva claves en el login; a mirar en B3.
- Al salir, el juego **guarda solo** dos veces (03:03:42 y 03:03:55) en la **ranura 2** (la del
  guardado a mano de B1) con la clave del anfitrión (`ActorKey 2416967680` = `0x90100000`): el
  guardián las rechazó (aviso "Failed to save data") y K1 cortó 1 informe. La ranura 2 sigue
  idéntica a la copia.
- **Cierre normal, sin cuelgue** (`Terminate` a las 03:03:56). El usuario cerró el juego del todo.

### B2 (T04, E2), login del usuario 2 con `cdmp_peer` — 2026-10-08 22:58, **se para en el paso 1**

Ini [ini/cdmp.b2.ini](ini/cdmp.b2.ini), ranura 108, K1 + K2. `cdmp_peer --mode replay --script
login_e2_112627.troc` (salida en `backups/logs/cdmp_peer_b2.txt`, grabación `D:/cdmp-grabaciones/e2.bin`).
- El túnel conecta; la sesión 2 estaba `ready`. `ConnectCryptogramToServerReq` 2319 inyectada y
  **consumida por el servidor** (`injected 1 ... seen by the server 1`).
- **No sale ningún `ConnectCryptogramToServerAck` 2840**: `out: 0 packets` (la `Send` de la sesión
  2 no se llama nunca). `cdmp_peer`: `no Ack 2840 within 30000 ms`, guion `FAILED` en el paso 1/4.
- El anfitrión, sin efectos: siguió jugando; ningún error del juego en el log.
- Hipótesis: el Ack se dirige con `ToTargetSendTargetFinder` (`0x2A1F680`) y el destino no sale de
  la sesión 2 tal como la construimos (usuario nulo, id). Análisis estático en curso
  (`data/b2-ack2840.md`).
- **Al salir al título el juego se queda colgado** en la pantalla de carga (sigue pintando y el mod
  sigue escribiendo el log, ~1 min sin avanzar). Lo cerré a la fuerza a las 23:01:50: no se pierde
  nada (el guardián bloqueaba todo). Sospecha: el cierre del servidor espera a la sesión 2, que
  quedó a medio abrir tras la 2319. Hasta entenderlo, después de una prueba con `cdmp_peer` hay que
  cerrar el juego del todo, no salir al título. Log en `backups/logs/cdmp_20261008-2255_b2.log`.

### B1 (T03, E1), sesión 2 dormida — 2026-10-08 22:43, **pasa** (con un hueco del guardián, arreglado)

Ini [ini/cdmp.b1.ini](ini/cdmp.b1.ini), ranura 108 (capítulo 2), K2 activo. Log copiado en
`backups/logs/cdmp_20261008-2243_b1.log`.
- Arranque: `report guard ON` (K1) **antes** que `save guard ON`; `487 server Req handlers ... 49
  never fed from the tunnel`; `hooked ServerNet::OnRecvPacket`. `session 2 pending` → **`ready`** a
  los ~14 s, y la sonda la ve (`session 2 0x...`, sin usuario). `out: 0 packets`. 9 minutos de juego
  sin cierres ni tirones; cierre normal.
- **Guardado a mano en la ranura 2** (22:47:10, y el juego reintentó a las :20 y :27): 4 `refused
  move from ...slot2` por intento (el juego rota copias antes de escribir: `Backup2 생성 실패`).
  **K1 cortó 3 informes** `[INFO] Savedata Save Fail Event` con 3 adjuntos: `slot2/lobby.save`,
  `slot2/save.save` y el `Launcher_*.log`, exactamente lo previsto en RPT-0. El 4.º intento ya no
  pidió informe: **el tope de 3 por partida** (`0x6D9CCE8`) se confirma. `Sentry was off` en los
  tres (K2). La ranura 2 quedó **idéntica** a la copia.
- **Hueco:** el autoguardado de las 22:52:24 en la ranura 107 creó `slot107_new` y
  `slot107_backup` (vacías): el guardián no tenía hook de `CreateDirectory*` aunque la política ya
  tenía `Op::CreateDir`. Luego `Open 실패`, `writeSaveData 실패`, `복구 실패` (recuperación
  fallida) sin más efectos. Arreglado (D-38); las dos carpetas vacías, borradas.
- Marcas del usuario: 1 (22:47:24), 2 (22:47:29), 3 (22:52:26), en los momentos en que salió el
  aviso. **Lo que ve el jugador** al guardar a mano y en cada autoguardado rechazado: una ventana
  "Failed to save data. / Failed to save game data." con un botón Confirm; tras confirmar, el juego
  sigue normal. Para el multijugador (P9 inverso, persistencia del servidor) esto es un pendiente:
  con el guardián activo saldrá en cada autoguardado. Habrá que sustituir el guardado del anfitrión
  por el del servidor, no solo bloquearlo.

### A4 + RPT-1, partida de capítulo 2 sin crashpad — 2026-10-08 22:37, **RPT-1 pasa; A4 a medias**

Ini [ini/cdmp.rpt1-a4.ini](ini/cdmp.rpt1-a4.ini); `crashpad_handler.exe` apartado (K2).
- **RPT-1 pasa.** El juego arranca, carga y juega normal y cierra normal. No hay proceso
  `crashpad_handler` y **no se creó carpeta `<uuid>.run` nueva** en `DumpCache` (la única es la de
  la partida de las 22:11): Sentry ni siquiera arrancó. `crashpad.ps1`: `nothing pending`.
  `Launcher_*.log` sin avisos (solo el de idioma de siempre).
- **La partida de otra cuenta carga:** `Start/End Load SaveSlot108` dos veces, sin errores, y el
  personaje es `0xA0100001` como en las tuyas. Una partida de otra persona sirve de plantilla tal
  cual (la clave de cifrado no depende de la cuenta, como decía el análisis).
- Ninguna ranura se escribió (fechas sin cambios; sin autoguardado en 3 minutos).
- Marcas: `MARK 1` a las 22:39:22 (12 s después de `in world`), `MARK 2` a las 22:41:04. Faltan las
  notas del usuario de qué pasaba en cada una.
- Sonda: 800 actores en esa zona (`0x320`), claves `0x396D` = 15 501 − 800. Mismo esquema de campo
  (primer campo y luego el definitivo, generación `0x81020000` → `0x81020001`).
- Grabación de la carga de la plantilla (76 MB) y log guardados en
  `C:\Emulador CD\backups\capturas\` (`cdmp_packets_20261008-223758.bin`).

### A3 (T06, G0) en Shadow, modo observe — 2026-10-08, **pasa**

Kit `dist/cdmp-a3-shadow.zip` (build `58A42167` + el cambio de `9309309`), ini
[ini/cdmp.a3-shadow.ini](ini/cdmp.a3-shadow.ini). Juego 1.0.0.2976, PE `0x6AB28F00`.
- Hooks del invitado instalados 11,5 s tras arrancar y **5 s antes** de que el resto del mod
  empiece (`Late init`): **sin `TOO LATE`**.
- `connections remote 0 local 1 (first packet of the last one: 2319)`: la primera Req es
  `ConnectCryptogramToServerReq`, como dice el análisis (D-16). **Una sola conexión** para todo
  el login, aunque haya dos ciclos de carga: la decisión por época de P8 se toma una vez.
- `local packets` 2 → 3 → 6 175 → 30 085 → 48 902 por minuto; partida normal y cierre normal.
- `out 0 / in 0`: en modo observe no sale ni entra nada del túnel, como debe.

### A2 repetida — 2026-10-08 21:50, **pasa**

Build con el arreglo (`87154DB8` + cambios locales, que son el commit `dccf39e`). En los tres
minutos: `finders control 0/263`, `0/13074`, `0/24594 nonempty`; `self 0/0`, `0/5`, `0/9`; 0
errores. Unas 190 llamadas por segundo durante el combate, y **ninguna devuelve nada**. Confirmado
en vivo que el relevo "excepto el que controla" está vaciado en esta build: P5 (`relay_fix`) es
necesario. Las claves siguen cuadrando (minuto 3: `0x3B72` = 15 218 con `0x11B` = 283).

Siguiente: A4 en este PC, ya instalada ([ini/cdmp.a4.ini](ini/cdmp.a4.ini), con grabación de
paquetes). Copia de las partidas en `C:\Emulador CD\backups\saves-20261008-215415-antes-a4`. Las
ranuras 0 y 1 se escribieron durante las pruebas A2 (autoguardado del juego: el guardián estaba
apagado, como debe). A3 necesita Shadow.

### A2 (T01, E0b), buscadores — 2026-10-08 21:42, **sin dato: repetir** (fallo mío)

`cdmp.ini`: [ini/cdmp.a2.ini](ini/cdmp.a2.ini). El hook se instaló (`hooked
ToNearExceptOneControl/Self::vf1`), pero los contadores solo salían en `describe()`, que escribe el
bucle de red, y el ini lleva `[net] enabled=0`. Arreglado: la sonda los escribe cada minuto
(`Session2 probe (minute N): finders ...`); compilado, tests pasan, instalado. Hay que repetir A2
con el mismo ini.

Lo que sí da esta partida (6 min, combate), y confirma A1:
- Sesión, colas y usuario igual que en A1; ningún `DIFFERENT`.
- Claves libres = 15 501 − actores en todos los minutos (p. ej. minuto 4: `0x3B61` = 15 201 con
  `0x12C` = 300; minuto 6: `0x3ACC` = 15 052 con `0x1C1` = 449). Entre los minutos 3 y 4 bajan los
  actores y **vuelven las claves**: se reciclan.
- En el minuto 1 el personaje ya está en un primer campo (`...7700`, `user actors 0x1`, generación
  `0x81020000`) y en el minuto 2 en otro (`...7A10`, `0x4`, `0x81020001`): los **dos ciclos de
  carga** del login (`opcion-d-login.md`), vistos en vivo.
- `FieldTime` vuelve a quedarse fijo desde que termina la carga (minutos 2 a 6, con combate).
  Confirmado: es una instantánea de la carga, no un reloj.

### A1 (T01, E0), sonda de solo lectura — 2026-10-08 21:33, **pasa**

Build del mod `55E828E9`, protocolo 8; juego 1.0.0.2976, PE `0x6AB28F00`. `cdmp.ini`:
[ini/cdmp.a1.ini](ini/cdmp.a1.ini). Partida del usuario, ~4 min con combate; cerrado sin guardar.

- Sesión 1 con vtable `exe+0x57260C0`; `out queue (ClientNet+0x18 same)`, `Q2/Q3 (ClientNet+0x20/+0x28
  same)`; usuario `vt exe+0x5B257A8 key 0x90100000 +0x1F8 == session`; `crypto +0x24 0x0`.
  Ningún `DIFFERENT`. Los `?` de la línea `host character` solo salen en la sonda de instalación
  (todavía en el menú); desde el minuto 1 hay datos.
- **Claves de actor libres:** `0x3C8D` = **15 501** al instalar (coincide con la reserva común de
  D0), `0x3AFA` = 15 098 en el minuto 4 con `0x193` = 403 actores: 15 501 − 403 = 15 098 exacto.
  **E2 compara contra este par.**
- Personaje `0xA0100001`, campo con `owner 0x90100000 users 0x1 user actors 0x4 generation
  0x81020001`, el mismo los 4 minutos.
- **Observación:** `server FieldTime` (32 B en `exe+0x6A6D238`) cambia entre la instalación y el
  minuto 1 y entre los minutos 1 y 2, y luego queda **igual en los minutos 2, 3 y 4**, con combate
  en el minuto 3. No es un reloj que avance de forma continua: será una instantánea que se
  actualiza por eventos. No bloquea nada; antes de usarlo como hora del servidor (vision A3)
  hay que ver qué lo actualiza.
- `save guard true` en la línea `Config` es la opción; el guardián **no se instaló** (no hay
  etapa que escriba), como manda D-13. Cierre normal (`Terminate 시작.`).

## Actualización de la visión persistente — 2026-10-08

Investigación terminada en [vision-sintesis.md](vision-sintesis.md), con ocho informes de área
y nueve revisiones independientes en total, incluida la síntesis. Se contrastaron diez
afirmaciones seleccionadas por informe; las correcciones y límites están documentados.
No se ejecutó el juego, compiló, desplegó ni modificó código del mod durante esta investigación.

El objetivo sigue siendo ocho personas y el techo diez, **sin aforo cooperativo demostrado**.
Primero H=2/U=2 con anfitrión participante; una sesión técnica adicional consume otra plaza.
Persistencia, IA remota, identidades, contenido y creación completa conservan incógnitas
explícitas. Los informes y datos quedan versionados; este diario se mantiene actualizado fuera
de los commits por la restricción expresa a `vision-*.md` y `docs/research/data/`.

**Para esta visión prevalece el registro T00–T21 de la síntesis sobre la secuencia histórica
que sigue abajo.** En particular, RPT-0 debe preceder al error deliberado de guardado de E1:
P6 no aísla las lecturas/adjuntos de Sentry. Ninguna prueba futura se ha ejecutado aquí.

## Para leer al despertar

- **Rama:** `opcion-d`, en un **worktree aparte**: `C:\Emulador CD-opcion-d` (decisión D-1).
  `C:\Emulador CD` sigue en `main` con tus cambios sin commitear intactos.
- **Las cuatro tareas de la lista están hechas**, compiladas, con tests (4 ejecutables, todos
  pasan) y revisadas por agentes revisores (código del servidor dos veces, P8 y la rama entera).
  Además he adelantado **P8** (el invitado juega en tu servidor, programado y revisado) y los
  diseños de **E3b** (meter al usuario 2 en tu campo) y **P9** (servir la partida del invitado).
  Nada se ha probado en el juego: ni lo he abierto ni he ejecutado `deploy.ps1`.
- **Lo más importante de la noche:**
  - **D0:** la cuota de usuarios es N = 1, **pero no limita nada**; no hay que parchear nada
    (`opcion-d-d0.md`).
  - **Login:** hay **dos ciclos de carga**; el canal de la trama debe coincidir; el primer paso
    (`ConnectCryptogramToServerReq`) es un handler "interno" que el anfitrión tiene que aceptar
    aparte (D-16, arreglado) (`opcion-d-login.md`).
  - **E3, la expectativa cambia:** el servidor da a cada usuario **su propia instancia de campo**;
    lo más probable es que no veas al usuario 2 en E3 aunque todo funcione. El sondeo dirá si
    hace falta E3b (`opcion-d-e3-riesgos.md`, `opcion-d-campo-compartido.md`).
  - **Seguridad:** el guardián bloquea toda escritura en la carpeta de partidas en cuanto una
    etapa escribe en el juego (no podrás guardar); el túnel nunca pasa al servidor Req de
    guardado, de trucos ni de desarrollo, ni la ranura 3 (CrossSave) o la 206.
  - **Protocolo 8:** esta rama no conecta con un kit de `main` (v7); misma build en los dos PC
    (D-21).
- **Qué necesito de ti:** las pruebas de `docs/research/opcion-d-pruebas.md`, **en orden**
  (resumen abajo). Antes, coordina con el otro proyecto quién instala en la carpeta del juego.

## Pruebas en el juego que necesito que hagas (en orden)

Guía completa, reorganizada el 2026-10-08 según la secuencia T00-T21 de
[vision-sintesis.md](vision-sintesis.md) y el aforo de 4 (D-33):
[opcion-d-pruebas.md](opcion-d-pruebas.md).

- **Bloque A, ya:** A1 sonda E0; A2 buscadores E0b; A3 G0 en Shadow (observe); A4 marcar en una
  partida nueva dónde empieza la exploración libre (IN1). Ninguna activa el guardián.
- **Bloque B, tras RPT-0:** E1, E2, E3 y G2. Activan el guardián, y un guardado rechazado (también
  un autoguardado) puede pedir adjuntar los `*.save` a un informe de error para Pearl Abyss
  (`vision-a7-transversal.md` §4). Antes hay que aislar esos informes y trabajar solo con partidas
  de laboratorio (D-34).
- **Bloque C, tras trabajo nuevo:** E3b → E4, identidad, IA lejos del anfitrión, salida y
  reconexión, persistencia, plantilla, contenido, permisos, escala 2 → 4 y
  duración (H=4 durante 24 h).

## Hecho

- **2026-10-08, síntesis cerrada y verificada independientemente; commit final `0298987`.**
  [vision-sintesis.md](vision-sintesis.md) reúne los seis objetivos, arquitectura, aforo H/U,
  presupuestos condicionados, hoja de ruta, registro único T00–T21, cinco riesgos y esfuerzo.
  Los ocho informes A0–A7 ya están verificados. La auditoría de coherencia añadió RPT-0 a
  las áreas anteriores, corrigió que 1.182.846 B mide sólo `save.save` y dejó pendiente la
  disponibilidad de barbería justo en la frontera de exploración libre. La síntesis contiene
  además las decisiones D-22–D-32 para su versionado dentro de las rutas autorizadas.
  El último revisor confirmó diez afirmaciones en su alcance local y reprodujo la aritmética;
  no convirtió los escenarios de capacidad en mediciones multijugador. Control documental:
  nueve informes, 36 archivos de datos y 159 enlaces locales comprobados, sin errores de
  estructura JSON/CSV ni enlaces ausentes (`data/vision-control-entrega.json`).

- **2026-10-08, visión A7 cerrada y verificada; commit `8b95cdb`.**
  [vision-a7-transversal.md](vision-a7-transversal.md) contrasta las 1.115 correspondencias
  nombre/opcode de la build, inventaría dependencias fijas y diseña el banco de hasta diez
  sesiones sin implementarlo. Un replay no certifica IA, física ni interfaz de clientes reales.
  El hallazgo de reportes de `*.save` condiciona E1 mediante RPT-0 (D-28). Se documentan por
  separado CrossSave, Crashpad/Sentry y las restricciones contractuales del EULA específico
  de Crimson Desert y Steam; no se observó transmisión ni se distribuyó nada.
  El revisor reprodujo las 1.115 correspondencias y confirmó nueve afirmaciones. Corrigió
  una falsedad local de punteros: el FileHandler se obtiene del gestor recibido por RCX,
  mientras la ranura llega por R8. El contador limita invocaciones al reportero, no subidas
  efectivas. La condición RPT-0 queda confirmada para el orden de pruebas.

- **2026-10-08, visión A6 cerrada y verificada; commit `931da11`.**
  [vision-a6-personaje.md](vision-a6-personaje.md) separa alta, reedición estética semanal y
  recreación completa: esta última no queda acreditada. Req2676 consume 271 B y Ack2622 279 B;
  el envío explícito tras editar es ToTarget, mientras ToNear pertenece a otra ruta protegida.
  La apariencia se conserva en Status+0x388; faltan prueba de editor y difusión durante una
  edición real. El túnel actual bloquea esta Req por el prefijo Save. La autorización semanal
  necesita una excepción específica, nunca retirar en conjunto el bloqueo de guardados.
  Diez afirmaciones contrastadas por otro agente; tres AOB únicas y ocho capturas reproducidas.
  ToNear exige también éxito de aplicación. Ninguna captura contiene Req2676, por lo que
  sigue sin observarse una edición real y su difusión; el icono actual sólo sigue un actor.

- **2026-10-08, visión A5 cerrada y verificada; commit `3074a18`.**
  [vision-a5-inicio.md](vision-a5-inicio.md) cruza tablas introductorias, lectores del exe y
  capturas. Hay quests/stages candidatas y un requisito de experiencia ligado al tutorial del
  Abismo; ninguno certifica el instante de exploración libre. `NewGameStartReq` lleva payload
  vacío y no es una API demostrada para saltar el prólogo. A5-IN1 debe fijar el punto real y
  distinguir primer control libre de primer guardado recargable; inventario y coordenadas
  iniciales siguen pendientes de esa prueba. A5-IN2–5 comprueban plantilla, identidad y
  compatibilidad narrativa con un mundo avanzado.
  El revisor confirmó diez afirmaciones locales y reprodujo once tablas/160 registros.
  Precisó conversiones de tamaños, guardas condicionales del gestor y que una búsqueda de
  actor no siempre impide procesar si no lo encuentra. Ninguno de esos contrastes certifica
  la frontera libre ni una plantilla clonable sin adaptación.

- **2026-10-08, visión A4 cerrada y verificada; commit `01b284f`.**
  [vision-a4-identidad.md](vision-a4-identidad.md) distingue cuenta recibida de identidad
  autenticada y documenta una colisión concreta de caché por `characterNo`. La retención de
  cuenta en la ruta nativa sigue sin demostrarse; tampoco está localizado el generador de
  `itemNo`. `_reserveItemNo` es una referencia de `UseItemReserveSlotElementSaveData`, no
  evidencia de un contador. P9 invertido exige preparar lobby antes de `LoginUser`; treinta
  registrados en disco no garantizan rotación continua sin invalidación de las ranuras.
  Otro agente confirmó diez afirmaciones con alcance local. Precisó que la ruta de borrado
  de 206 es condicional y que la discrepancia de relogin no siempre detiene la operación:
  depende también del código global de error. Ninguna de estas rutas acredita efectos en vivo.

- **2026-10-08, visión A2 cerrada y verificada; commit `011bbf6`.**
  [vision-a2-servidor.md](vision-a2-servidor.md) mide tráfico real separando título,
  cargas y juego. El modelo añade los relevos vaciados que no aparecen en S→C nativo:
  ocho personas y ancla técnica dan escenarios de 24,2–36,6 Mbit/s juntos, con estrés de
  67,0 Mbit/s; son hipótesis de dimensionamiento, no capacidad observada. Diez personas
  incluyendo al anfitrión: 27,8–42,4 Mbit/s. Se documentan logout/Close, ventanas del túnel
  y un contraejemplo al supuesto de validación universal de Req (`VaryStatByAccInputReq`).
  Diez afirmaciones contrastadas por otro agente y dos capturas releídas. La validación de
  `DoFrameEvent` puede descartar un evento devolviendo 0: las pruebas deben comprobar efectos,
  no sólo el código de retorno. El modelo de red es aritméticamente consistente y sigue siendo
  un escenario, sin cota garantizada ni ensayo multijugador.

- **2026-10-08, visión A1 cerrada y verificada; commit `a5a96b8`.**
  [vision-a1-mundo.md](vision-a1-mundo.md) mide ocho capturas y diferencia altas acumuladas
  de visibilidad simultánea: en 200328 son 7.655 claves distintas y pico visible de 1.006;
  no es el total del SAM. Regiones superpuestas y radio heurístico no equivalen a zonas
  independientes. El Ack 2508 sólo lleva clave del actor, no nuevo controlador ni época;
  la ruta de grupo revisada exige el mismo controlador. Quedan abiertas IA, duplicados,
  contenido y reparto humano; no se promete que el protocolo MMO los resuelva solo.
  El revisor reproduce el pico de 1.006 con un lector independiente y confirma nueve
  afirmaciones. Corrige una formulación falsa: `0x2792A7B` también reescribe el controlador
  existente; sólo la elección de uno nuevo requiere que el valor anterior sea cero.

- **2026-10-08, visión A3 cerrada y verificada; commit `d30c3c2`.**
  [vision-a3-guardado-mundo.md](vision-a3-guardado-mundo.md) y su catálogo de componentes
  separan datos privados, mundiales y mixtos. `lobby.save`/`save.save` no son una separación
  mundo/jugador. El productor de datos de guardado usa `vf(0xD0)` y los archivos se tramitan
  mediante llamadas separadas. No garantizan dos commits efectivos ni durabilidad:
  `0x2355700 → .xtls 0xEF51850` puede terminar sin escritura según los pendientes.
  Las llamadas indirectas y protegidas impiden afirmar aislamiento global completo.
  A4 adopta el contrato de ranuras/cachés de su §4. El segundo agente confirmó nueve
  afirmaciones y corrigió una dudosa: las llamadas de guardado no prueban commits efectivos.

- **2026-10-08, visión A0 cerrada y verificada; commit `c6cf1c2`.**
  [vision-a0-base.md](vision-a0-base.md) distingue las piezas programadas de las diseñadas,
  fija las puertas E0–E4/G0–G3 e inventaría las familias Denuvo relevantes. El contraste del
  exe confirma la sesión fija (`0x27735C7`) y el rechazo de segundo usuario de campo
  (`.xtls 0x1064C806`). Corrección de precisión: `0x2792A11` lee el controlador de IA;
  `0x2792A7B` lo escribe. Diez afirmaciones contrastadas por un segundo agente: confirmadas
  dentro de su alcance, con cuatro precisiones; la barrera de guardado no tiene timeout visible
  y la carga ocupada devuelve error en vez de esperar.
  A1 (mundo/capturas) y A3 (guardado) han empezado en paralelo desde esta base.

Por orden de la lista:

1. **D0 en estático** — `docs/research/opcion-d-d0.md` (agente). N = 1 sin límite efectivo;
   `sesión+0x90` = usuario, `+0xC8` = cola de salida (las dos lecturas eran correctas);
   las cuatro colas Q0-Q3 y su ciclo de vida; layout del `PacketBuffer` C→S y dónde poner una
   marca; byte TLS `+0x1EC`; asignador `0x47EF9B0`/`0x47EFA78` (eran VA, no RVA);
   `ChangeAiControlAck` solo sale del cheat; el controlador de un NPC (`actor+0x90`) se fija al
   crearlo; prólogos de todas las funciones a enganchar (ninguno con RIP-relativo).
2. **Login de las capturas 112627 y 105149** — `docs/research/opcion-d-login.md` y
   `docs/research/data/login_script_112627.json` (agente). Dos ciclos de carga; Req casi
   constantes (solo cambian la clave RSA y la ranura); qué reescribir (cuenta, `CHARACTER_NO`,
   `SAVE_SLOT`, `PLAYER_KEY` en las opcionales); varias correcciones a `segunda-sesion.md`.
3. **Piezas P1-P7** (todo detrás de `[session2] enabled=0`):
   - **P7 túnel** (`src/common/troc_frame`, `troc_link`): lotes `TrocFrame` en mensajes fiables
     (**protocolo 8**), cola entre hilos, emisor que no desordena ni pierde. Tests: 400 paquetes
     con 15 % de pérdida y desorden; dos sesiones conectadas en memoria con reconexión.
   - **P6 guardado** (`src/common/save_guard`, `src/mod/session2/save_hooks`): hooks en la
     entrada de las funciones de fichero de KernelBase; rechaza escrituras, borrados, mover,
     copiar y atributos bajo `...\CD\save`; deja leer. Test **con los hooks reales** contra una
     carpeta falsa en `%TEMP%`.
   - **P1 sesión 2, P2 entrada, P3 salida** (`src/mod/session2/server.cpp`): la sesión 2 se
     construye en el hilo del servidor igual que la del juego, con una **vtable privada** cuya
     `Send` es nuestra (decisión D-7); las Req del invitado entran en la cola Q0 en un búfer
     marcado y un mid-hook en la entrada de `OnRecvPacket` cambia la sesión (opción B de D0).
   - **P4 login:** guiones generados desde la captura (`tools/scripts/make_troc_script.py` →
     `docs/research/data/login_e2_112627.troc`, `login_e3_112627.troc`,
     `login_e3opt_112627.troc`) y un reproductor de guiones (`src/common/troc_script`, con
     tests). El mod reescribe siempre la cuenta del login a `[session2] account` (≠ 0).
   - **P5 relevo:** hooks en las dos `vf1` vaciadas: con el usuario 2 conectado, recogen lo
     mismo que `ToNear` menos la sesión del usuario que controla el actor (D-8).
   - Extras: E0 sondeo de solo lectura, E0b recuento de los buscadores, E4 **modo espejo**
     (el usuario 2 repite tus movimientos para probar P5 con un PC), registro de la ranura de
     guardado del anfitrión.
4. **`cdmp_peer --mode replay`** (`tools/peer_sim/replay.cpp`): con `--script` (espera cada Ack)
   o `--capture` (las Req de una captura a su ritmo, con `--from/--to/--only/--skip`), `--var`,
   `--record` (graba las dos direcciones en un `.bin`). En modo captura exige la build 2976,
   reescribe la cuenta y nunca manda Req de guardado. Test de punta a punta por UDP contra un
   anfitrión falso.

Otros: worktree con su `build\`; `[session2]` en `cdmp.ini`; el mod sigue cargando y
conectando en la prueba sin juego (`cdmp_peer` + `cdmp_host`). Remote Control activado a
petición tuya (23:50).

## Falta

- Revisión: queda sin hacer el hallazgo 8 (bajo): el SEH que envuelve llamadas al juego que
  toman locks (cola, constructor, `ToNear`) se tragaría un fallo con el lock tomado. Lo dejo así
  porque la alternativa (dejar que la excepción tumbe el juego) es peor; si aparece algún
  `errors` en los contadores, hay que mirarlo.
- **P8 (cliente fino del invitado):** **programado** detrás de `[session2] guest=1`
  (`src/mod/session2/guest.cpp`), sin probar; un agente lo está revisando. Diseño
  (`docs/research/opcion-d-p8.md`), en resumen:
  - salida: hook en la entrada de `NwVirtualSyncSession::Send` `0x14380B0`, que copia la trama al
    túnel y devuelve el búfer a Q3;
  - entrada: un mid-hook en el bucle del NetworkThread (`0x35BAD4`) que llama a
    `ClientNet::OnRecvPacket`;
  - el connect `1001`/`1002` sigue siendo local;
  - el login sale solo al arrancar, así que el hook tiene que estar puesto **antes** del `1001`
    (antes de lo que hoy hace el late init);
  - etapas G0-G5.
- **Campo compartido (E3b):** diseñado (`opcion-d-campo-compartido.md`, decisión D-17); se
  programa cuando E3 confirme que el usuario 2 cae en otro campo.
- **P9 (la partida del invitado en el servidor del anfitrión):** diseñado, sin programar
  (`opcion-d-p9.md`, D-20). Depende de P8 (G2) y de E3b.
- `block_save_trocs`: de momento solo cuenta las Req de guardado (D-5).
- Guion de E5 (combate del usuario 2) y el control de NPC con dos clientes (P10).

## Decisiones

- **D-50 (2026-10-09): P-RELOJ, el reloj del mundo es del anfitrión** (diseño de
  [pendientes/reloj.md](pendientes/reloj.md) §5, causa confirmada en vivo en C4b). Nuevo
  `src/mod/session2/clock_gate.{h,cpp}`, con dos enganches en entradas `.rsrc`, como una sola unidad:
  - `0x2BC0A30` (`SyncFieldTimeOnMove`): para el personaje del usuario 2, su hora es
    `GetCurrentFieldTime(campo)` (`0x20E55A0`, sólo lectura), copiada en `TS+0x450`. El original nunca
    se ejecuta para él (ni la rama de dueño ni el setter que publica).
  - `0x219D7F0` (`LoadingComplete`): el original con el flag a 0.

  Fallo seguro, tras una revisión independiente sin bloqueantes:
  - Con la clave del anfitrión conocida, cualquier usuario que no sea él cuenta como invitado.
  - Si el enganche no se instala, la opción D no arranca.
  - `SetFieldTimeReq` 2125 del invitado se descarta.
  - Un vigilante lee el reloj cada segundo y registra cada publicación, marcando las de escala 0.

  `[session2] clock_gate=1` por defecto. Prueba **R1**: la de C4b; pasa si el reloj sigue con escala 12
  tras entrar el usuario 2.
- **D-49 (2026-10-09): C4, salida nativa del usuario 2 (primera parte).** Diseño:
  [pendientes/salida.md](pendientes/salida.md) (análisis con Ghidra y revisión cruzada). La salida
  normal del cliente es `LogoutSessionToServerReq` 2001, **3 bytes** (`D1 07 FF`, sin campo de
  longitud), y las respuestas 2003/2002 son de **7 bytes**; el túnel y el sniffer suponían 5 bytes
  y las tiraban. Hecho: el túnel lleva exactamente esos tres mensajes internos, cada uno en su
  dirección (`parse_wire`), nada más de ese tipo (ni el apagado global 2790/2038 ni 2004); el
  servidor acepta la 2001 por nombre y opcode exactos; `logout_session` en los guiones; el sniffer
  graba la 2001. Disparadores: despedida del usuario 2, o pérdida del enlace (túnel roto o 15 s sin
  conexión), que generan la misma 2001 en el servidor del anfitrión. Desde que se pide, nada más del
  usuario 2 entra en el servidor (filtro central en `server_inject`, también el espejo E4); se
  entrega una sola vez por sesión 2 y, si el login aún no había creado al usuario, en cuanto
  exista (hasta 60 s). Revisión independiente: dos fallos (latch antes de comprobar el usuario;
  el espejo seguía inyectando) y varios riesgos, arreglados. **Pendiente:** la barrera de limpieza
  antes de reconectar (comprobar SAM/campo/sesión), las sondas de solo lectura de cada fase, el
  índice u32 de la 2002 para un cliente P8 real (dónde lo toma `0x1051F190`), si el servidor manda
  1004 a la sesión 2, y comprobar que el cierre de la sesión 2 no la libera. Prueba **C4a**.
- **D-48 (2026-10-09): causa raíz de C1–C1f y arreglo de raíz** ([opcion-d-accion-usuario2.md](opcion-d-accion-usuario2.md)).
  El cliente del anfitrión recibe la acción actual de cada actor dentro de `ActorLoadDataAck`, con un
  **byte de restauración** que el servidor pone a 1 cuando el actor ejecuta de verdad una acción
  (`0x2328894`); `0x8646D0` solo restaura la acción si vale 1. En las grabaciones: el anfitrión, 0 en su
  primera carga y 1 en la segunda; 354 NPC y animales, 1; **el personaje del usuario 2, 0 siempre**.
  El cliente real manda su primera `StartActionReq` ~10 ms tras el primer `MoveFieldCompleteAck` (escena
  de aparcamiento); nuestro guion no la mandaba (C1d: antes de tiempo; C1f: después de que el anfitrión
  ya lo hubiera creado). La carga era correcta (idéntica a la aceptada al anfitrión). **Arreglo:** paso
  4b en `login_script_112627.json` y guiones E3 regenerados (`make_troc_script.py`); corregida la tabla
  de `opcion-d-login.md`. P8 no necesita cambios (el cliente real ya lo hace); P5 (`relay_fix`) sigue
  siendo necesario para las acciones siguientes. Las contenciones D-44/D-46 se quedan como red de
  seguridad; más adelante, una regla general (actor remoto sin acción, con tiempo límite) para la
  carrera del caballo, el mercenario y los NPC que aparecen con los dos jugadores. Prueba **C1g**.
- **D-44 (2026-10-09, madrugada): contención del cierre de C1.** La guarda del cooperativo
  (`remote_puppet`: `0x2329700` salta la actualización de animación de una copia con gráfico y sin
  acción; `0x1F63F70` no avanza una capa vacía, que de todos modos cerraría el juego) se instala con
  `share_field` y protege además la clave del jugador del usuario 2, que el servidor conoce por
  `MainPlayerDataStartAck`. Solo cambia algo en el estado que hoy cierra el juego. Contadores en la
  línea de la sonda (`layer guards ... animation skipped N`). Y una variante del guion,
  [data/login_e3lc00act_112627.troc](data/login_e3lc00act_112627.troc), que tras el login manda un
  `StartActionReq` (el primero de la captura 112627, con la clave del jugador del usuario 2), como
  haría un cliente real. Pruebas: C1b (guion de siempre, debe aguantar) y C1c (con la acción, debería
  verse al segundo Kliff animado).
- **Nota (2026-10-09, C4, salida del usuario 2):** con Ghidra, los dos manejadores de logout son
  *thunks* a `.xtls` (`LogoutCharacterToFieldServerReq` 2564 → `0x1048DF70`;
  `LogoutSessionToServerReq` 2001, interno → `0x1089A360`): no se pueden leer. En las capturas, la
  única salida (105149, a los 1163,6 s) muestra solo lo que manda el servidor
  (`LogoutCharacterToFieldServerAck` 2679 → `LogoutUserToServerAck` 2003 → `LogoutSessionToServerAck`
  2002) y **ninguna Req del cliente**: va por un camino interno que el sniffer no ve. Siguiente paso,
  con el juego: registrar qué manda el cliente al salir al título (hook de `FlushSendQueue`/`Send`
  para los tipos internos) y repetirlo desde `cdmp_peer` al acabar el guion. Hasta entonces, cada
  prueba con `cdmp_peer` termina cerrando el juego.
- **D-43 (2026-10-09, madrugada): Ghidra en uso, sin registrar como MCP.** Tras corregir los tres
  bloqueantes, una segunda revisión independiente da **conectar con condiciones** (sin ejecución
  remota, sin escrituras en el proyecto, solo `127.0.0.1`). Ajusté la parada (F1 parcial, F2, F5;
  `ghidra/SETUP.md` §12.5). La sesión principal lo usa con los `.ps1` (`server-start`, `decomp`,
  `xrefs`…): comprobado en vivo (`decomp 0x1317D40` en 0,15 s, igual que el análisis a mano;
  `xrefs 0x143A580` con sus 8 llamadas). **No** se registra como servidor MCP en la configuración de
  Claude: es una integración permanente que debe aprobar el usuario. La carpeta
  `docs/research/ghidra/` queda en el repositorio (`a6821d6`); los scripts son copia exacta de
  `D:\RE\CrimsonDesert_2976\scripts`. Al terminar cada sesión: `server-stop.ps1` y comprobar que no
  queda `java.exe`.
- **D-42 (2026-10-09): E3b programado** (`field_share.{h,cpp}`, `[session2] share_field=1`, commit
  `0ffffed`), siguiendo `opcion-d-campo-compartido.md` §3 con lo comprobado a mano en el exe: H1 en
  la entrada de `AcquireField` `0x2B07790` replica su final (`0x2B079DD..0x2B07A47`: `TryAddRef`,
  `out->vf(0x10)`, `out+8/+0x10/+0x18`) pero con `out+0x20 = 0`; H2 en `EnterField` `0x2BC04F0`
  mete `{usuario 2, 0}` en `field+0x2A0` con los dos locks en el orden de `AddActor` (`+0x18`,
  luego `[+8]`; `vt+8` lock, `vt+0x10` unlock; push `0x40B5B0`) y los suelta **antes** del
  original; si el original falla y la entrada sigue a 0, la quita intercambiándola con la última.
  Solo actúan si el viajero es exactamente el usuario 2 (`server_users`, validado por vtable y
  puntero de vuelta). H2 antes que H1; si algo no cuadra, sin compartir (como E3) y la opción D
  sigue. Test `optd_field` con memoria falsa. Revisión independiente: sin fallos (argumentos,
  retorno de `res`, nivel, copia de `out`, locks sin retener durante el original, `enter_undo`,
  compuertas). Aplicado: ninguna excepción sale de los hooks (si algo falla, el original); el nivel
  se compara por la **entrada de nivel exacta** (búsqueda propia en el hash del gestor, como
  `0x2B077BC..0x2B07840`, con `out+0x24`), no por el índice u16; compuerta de H2 y búsqueda con
  test. Riesgo de carrera C3 asumido: con dos usuarios, un campo reabierto solo puede ser el nuevo
  del anfitrión. **Pendiente (C4):** si la sesión 2 muere sin `RemoveActor` estando en el campo del
  anfitrión, su entrada se queda y el campo no cerraría nunca; antes de E4/P8 hay que forzar la
  salida nativa (`0x2BBEAB0`). En C1 no aplica (el guion acaba y el juego se cierra). Lista C1.
- **D-41 (2026-10-09): Ghidra, revisado y todavía sin conectar.** La revisión independiente
  (`ghidra/REVISION.md`) da el análisis por fiable (reproduce las comprobaciones hechas a mano;
  `.rsrc` bien delimitada salvo casos aislados; descargas oficiales por SHA-256; copia del exe
  idéntica), pero **NO CONECTAR** la capa MCP hasta corregir límites, cierre de procesos y el paso
  de argumentos por cmd, y hacer una prueba operativa. Los cambios de `cdmp.asi`/`.pdb` de las 23:30
  del 8/10 que vio la revisión son la instalación de D-40 (esta sesión). Dos reglas de entrada del
  cortafuegos para el Java de `D:\tools` (red privada, cualquier puerto) las borró el usuario;
  comprobado que ya no están. Hasta conectar, solo se leen las exportaciones (`exports\`).
- **D-40 (2026-10-09, noche): el resto del login del usuario 2, revisado en estático**
  ([data/b2-login-pasos.md](data/b2-login-pasos.md)). Con id 0 la 2319 y la 2490 llegan a su
  manejador por la cola común; no hay tope de un login ni globales que pisen al anfitrión; todos los
  Ack hasta `MainUserDataEndAck` 2716 van por ToTarget a la sesión 2 (nuestra `Send`). Tras el
  login llegará un `ServerTimeInfoAck` 2188 cada ~5 s a la sesión 2. Cambios: (1) la cuenta **todo
  unos** (la única que coincide con el usuario del anfitrión en la rama de relogin) queda prohibida
  en el `cdmp.ini`, en la inyección y en `cdmp_peer`; (2) `cdmp_peer` escribe cada `Nak 1013` con
  su carga (código de error): si el gestor de partidas está ocupado (un autoguardado del
  anfitrión), `UserLogin` responde Nak en vez de 2716. Pendiente (no para B2): antes de que el
  anfitrión salga del mundo, dejar de inyectar y soltar la sesión 2; quizá un logout nativo con
  `LogoutSessionToServerReq` 2001 [S]. El análisis no encuentra en el servidor ninguna espera
  infinita por la sesión 2: el cuelgue de B2 se achaca a la lectura fuera de límites (D-39) [S].
- **D-39 (2026-10-08, B2): el id de la sesión 2 (`*(u32*)[s+0x88]`) es 0, no 2.** Causa de B2
  ([data/b2-ack2840.md](data/b2-ack2840.md), comprobado a mano): sin usuario, `OnRecvPacket` encola
  la Req diferida con esa clave (`0x277407E..87`); `0x143A580` manda la clave 0 a la cola común
  (`0x143A5A4`) y con otra indexa una tabla de 1 entrada sin comprobar límites (`0x143A5CA..D8`).
  Con 2 leyó fuera de la tabla, la cola devolvió error y `OnRecvPacket` tiró la 2319 sin avisar:
  ni manejador ni Ack. La sesión 1 tiene 0 (ctor, y la sonda lo lee en vivo). El trabajador de la
  cola exige `*(s+0x88) == item+0x54` (`0x27748D0`): 0 = 0 pasa. La lectura fuera de límites es
  además la sospechosa del cuelgue al salir al título. La sonda escribe ahora el id de la sesión 2.
  Corregidos `OPCION-D-EMULADOR.md` §P1 y `opcion-d-d0.md`, que recomendaban un id ≠ 0 [S].
- **D-38 (2026-10-08, B1): el guardián también bloquea crear carpetas.** Hooks de
  `CreateDirectoryW` (obligatorio), `CreateDirectoryA` y `CreateDirectoryExW` con `Op::CreateDir`.
  Motivo: en B1 un autoguardado rechazado dejó dos carpetas vacías en la carpeta de partidas. Las
  borré a mano (vacías, no estaban en la copia de antes de B1). Test en `optd_hooks`.
- **D-37 (2026-10-08, decisión del usuario): RPT-0 con K1 + K2.** Auditoría en
  [opcion-d-rpt0.md](opcion-d-rpt0.md). K1: hook en `0x1317D40` (reportero de Sentry del juego, en
  `.rsrc`) que sale sin enviar mientras una etapa escribe, instalado antes del guardián; si la
  cabecera no coincide, la opción D no arranca (`47741a3`, test `optd_report`). K2:
  `crashpad_handler.exe` apartado con `scripts/crashpad.ps1 -Off` (SHA-256 `222EC1F0…FE6EC`) en
  `C:\Emulador CD\backups\crashpad`; sin él `sentry_init` falla y no sale nada. Falta RPT-1
  (que el juego vaya normal así). Hallazgo: 6 volcados de cierres del cooperativo de `main`
  enviados el 3-4 de octubre. Partida plantilla del usuario ("Chapter 2 Normal", Nexus 2863) en
  `C:\Emulador CD\backups\plantillas\`, instalada como ranura 108 para A4.
  Revisión de K1 (agente): arreglado que el log distinga si Sentry estaba activo (el juego lo
  habría enviado) o no, que reinstalar sea inocuo, que el test compare cabecera y no enganche un
  thunk de enlace incremental (`/INCREMENTAL:NO`). Descartado, comprobado a mano: las rutas son
  UTF-8 `char*` (`0x44FD30` las mide byte a byte en `0x44FDB3` y las convierte en `0x44FDC2`), no
  `wchar_t*`; y safetyhook congela los hilos al activar (`trap_threads`).
- **D-36 (2026-10-08, decisión del usuario): fuera la personalización semanal.** Se retiran el
  cambio de aspecto una vez por semana, el «derecho semanal» y la semana del servidor (D-30
  queda sin efecto, y T18 sale de la guía de pruebas). Era un objetivo pensado para un proyecto
  mucho mayor (~90 jugadores); con aforo 4 (D-33) no compensa. Sigue en pie dar a cada jugador
  una apariencia al entrar por primera vez (alta inicial, plantilla T16). La Req 2676
  (`SaveCharacterCustomizationReq`) sigue bloqueada en el túnel por el prefijo Save, sin excepción.
  Los informes `vision-*.md` no se reescriben: llevan una nota que remite aquí.
- **D-35 (2026-10-08, decisión del usuario): la carpeta del juego pasa a esta rama.** El otro
  proyecto ya no la comparte; instalo y cambio los `cdmp.ini` de cada prueba yo, sin abrir el
  juego. Copias antes de instalar: partidas en `C:\Emulador CD\backups\saves-20261008-213024-
  antes-opcion-d`, ficheros del cooperativo en `...\juego-main-20261008-213024`, y el ini del
  cooperativo como `bin64\cdmp.ini.main-coop`. `winmm.dll` es el mismo cargador (hash igual).
  Los ini de cada prueba quedan versionados en `docs/research/ini/`.
- **D-34 (2026-10-08): la guía de pruebas sigue la secuencia de la síntesis.** Todo lo que activa
  el guardián (E1, E2, E3 y G2 en el anfitrión) espera a RPT-0, no solo el guardado manual de E1:
  cualquier autoguardado rechazado puede llegar a la ruta de informes con adjuntos. La propuesta
  práctica de contención es que, durante esas pruebas, en la carpeta de partidas solo haya
  partidas de laboratorio y las reales estén movidas a `backups\`; qué hacer con los informes que
  el juego pueda enviar lo decide el usuario. Ya se pueden hacer E0, E0b, G0 (observe) y la
  observación IN1 de la frontera de exploración libre. E3 ya no se repite con `LoadingComplete 01`
  si `00` se atasca (puede restaurar la hora de otra partida). E4 pasa detrás de E3b, porque con
  campos distintos su resultado no se puede interpretar.

- **D-33 (2026-10-08, decisión del usuario): el aforo baja a 4 jugadores.** Objetivo y techo son
  **4 personas a la vez**; las etapas de 8 y 10 de `vision-sintesis.md` quedan fuera de alcance.
  Sustituye la parte de aforo de D-22 y D-32. Con anfitrión jugando: U = 4 sesiones nativas
  (3 remotas); con personaje ancla: U = 5 (4 remotas). Consecuencias, con las cifras de la propia
  síntesis (§3):
  - **Claves de actor:** 15.501 − 101·U deja 15.097 (U=4) o 14.996 (U=5); con el margen de 3.100,
    unas 11.900 para el mundo. Cuatro o cinco zonas separadas caben aunque cada una costara
    ~2.400 claves (las capturas vieron 136-1.006). Deja de ser el límite probable, pero se sigue
    midiendo `SAM+0xD0` en vivo.
  - **Subida del anfitrión:** 8,1-11,9 Mbit/s juntos con el anfitrión jugando; 11,1-16,3 con ancla;
    estrés 23-31. Una conexión de **30 Mbit/s de subida** basta; con 10 no.
  - **Relevo de movimiento:** U(U−1) = 12 entregas por ronda (frente a 56-90 con 8-10).
  - **Ranuras:** hacen falta 3-4 del grupo `100-108`; no depende de certificar la 207.
  - **Hoja de ruta:** acaba en la etapa «Cuatro personas» (2-4 semanas tras un prototipo de dos
    estable). Duración de prueba para dar el servicio por estable: **H=4 durante 24 h** (la tabla
    original pedía 8 h para 4 y reservaba 24 h para 8). El banco sintético de T20 se dimensiona
    a 4-5 sesiones.
  - **Sin cambios:** la generalización de «sesión 2» a una tabla de sesiones sigue haciendo falta
    (de 1 a 3-4 extra); los riesgos 1-3 y 5 de la síntesis (escena/IA lejos del anfitrión,
    separar mundo y cuentas, historia y personaje, reportes y permisos) no dependen del número.
    Las ~30 cuentas registradas que rotan siguen siendo posibles con 4 a la vez.

- **D-32 (2026-10-08, avance por evidencia): etapas humanas y duración explícitas.** La
  secuencia consolidada de [vision-sintesis.md](vision-sintesis.md) prevalece sobre guías
  históricas que carezcan de las nuevas puertas. Objetivos iniciales de ensayo: H=2 durante
  2 h, H=4 durante 8 h, H=8 durante 24 h y H=10 durante 48 h, después de superar carga,
  identidad y recuperación en cada aforo. El primer prototipo usa H=2/U=2 con anfitrión
  participante; dos personas externas más ancla necesitan U=3 y generalización previa.
  Un login de prueba que exceda el aforo espera fuera del motor. Registrar U por separado: ancla adicional implica
  U=H+1, y H=10 con esa ancla queda fuera de U≤10. Ampliar a 72 h fortalece la evidencia sin
  probar estabilidad indefinida. Son criterios propuestos, no capacidad observada ni un
  máximo nativo. Motivo: no confundir replays con clientes completos ni avanzar porque
  coinciden medias de tráfico mientras quedan fallos de integridad o ciclo de vida.

- **D-31 (2026-10-08, continuidad y distribución): perfil de build cerrado y alcance privado.**
  Una build no certificada bloquea la activación de las piezas del motor y la admisión; hash,
  estructuras, mensajes y firmas se revisan juntos. Un nombre RTTI igual o AOB única no
  demuestra contrato compatible. El banco futuro distingue diez sesiones sintéticas frente
  a anfitrión falso de un juego integrado con ancla, donde sólo caben nueve adicionales bajo
  U≤10. No publicar ni operar para desconocidos como extensión automática de un prototipo:
  quedan permisos contractuales, revisión de autorizaciones y manejo de reportes/partidas.
  Motivo: prevenir corrupción por parches y promesas de seguridad o permiso que las APIs de
  red no proporcionan. Fuente: [vision-a7-transversal.md](vision-a7-transversal.md), §§2–5.

- **D-30 (2026-10-08, derecho semanal): siete días desde la última confirmación durable.**
  **Sin efecto desde D-36** (fuera la personalización semanal).
  Propuesta explícita: 604.800 segundos del reloj del servidor por SteamID, no FieldTime,
  fecha del cliente ni número de ranura. La elección estética inicial exige comprobar que
  el editor o presets validados respetan el punto exacto de inicio libre; no adelantar la
  plantilla si la barbería requiere progreso posterior. Reedición sin borrar progreso como
  primera modalidad. Recreación completa/cambio libre de cuerpo sigue
  pendiente y no se presenta como equivalente. Un identificador de operación y recibo durable
  evitan gastar dos veces o recuperar el derecho por rollback del mundo; cancelar/fallar no
  lo consume. Retrocesos y saltos sospechosos de reloj suspenden sólo confirmación estética
  hasta reconciliar hora; no se promete resistencia frente al administrador del propio PC.
  Exceptuar únicamente Req2676 tras autorización, actor/campos validados y respuesta coherente,
  manteniendo bloqueadas las demás Save*. Probar difusión y entrada tardía antes de habilitar
  la edición compartida: ToTarget de `0x294B21B–0x294B24F` no demuestra actualización vecinal.
  Fuente: [vision-a6-personaje.md](vision-a6-personaje.md), §§3–5.

- **D-29 (2026-10-08, incorporación de jugadores): plantilla nativa certificada.** No elegir
  una misión por su nombre ni dar por hecho que `Mission_Intro_Abyss_Tutorial` define la
  frontera libre: sólo está contrastada su relación con una condición de progreso. Fijar
  mediante A5-IN1 primera exploración libre y primer estado recargable; documentar cualquier
  diferencia. Conservar un progreso introductorio coherente, adaptar identidades según A4
  y excluir la autoridad mundial del donante según A3. No adelantar experiencia, misiones o
  stages para ocultar una incompatibilidad. Motivo: evitar plantillas que parezcan jugables
  pero bloqueen progreso, dupliquen objetos o reviertan el mundo al entrar el novato.
  Fuente: [vision-a5-inicio.md](vision-a5-inicio.md), §§1–5.

- **D-28 (2026-10-08, reportes y pruebas destructivas simuladas): puerta RPT-0 antes de E1.**
  El bloqueo P6 impide escrituras, pero no lecturas ni adjuntos de reportes. El análisis A7
  identifica `0x2355700 → .xtls 0xEF51850 → 0x2356270`: ante determinados fallos de carga,
  validación, guardado o borrado enumera `*.save` de la ranura y prepara adjuntos para Sentry
  (`0x2356434 → 0x1317D40`). Hay condiciones y un contador global; no se observó envío.
  Por prudencia, la futura E1 que provoca un error, y los ensayos de disco lleno/corte, quedan
  precedidos por auditoría del disparador y aislamiento verificable de enumeración, archivos
  legibles y adjuntos. La raíz propia debe cubrir esas rutas y cualquier lector auxiliar.
  No basta redirigir CreateFile ni apagar CrossSave. Esta decisión no ejecuta ni configura
  ninguna prueba y no convierte la contención de reportes en permiso contractual.
  Fuente: [vision-a7-transversal.md](vision-a7-transversal.md), §8, contraste independiente completado.

- **D-27 (2026-10-08, identidad durable): catálogo autenticado y aislamiento antes del botín.**
  SteamID64 autenticado gobierna los archivos y el derecho semanal; el u64 de una Req no
  autentica ni demuestra registro durable en el servidor nativo. Dar identidad de personaje
  coherente a cada miembro, porque la caché cliente compara `characterNo`/serie y resuelve el
  primer actor coincidente (`0xAA8E88`, `0xAA7F41 → 0x8AEB30`). No separar rangos modificando
  `_reserveItemNo`: pertenece a una ranura de uso (`0x189D7D0`, RTTI `0x6B43290`). Bloquear
  intercambio en el prototipo hasta localizar el productor real y superar ID-2. Preparar el
  arrendamiento antes de `LoginUser` (`0x2C17881 → 0x186F570`) y no reutilizarlo sin demostrar
  invalidación A→B→A. Motivo: evita atribuir una partida o una instancia de objeto a otro
  miembro aunque el login y el movimiento parezcan correctos. [vision-a4-identidad.md](vision-a4-identidad.md).

- **D-26 (2026-10-08, operación doméstica): admisión y red con presupuesto.** Una carga
  de entrada/recarga admitida cada vez al principio; colas y presupuestos por conexión y
  globales, sin prometer capacidad por el ancho de banda nominal de la casa. Medir con RTT,
  pérdida y reenvío P5 activo: las capturas de un jugador omiten esos Ack vaciados. Mantener
  acceso de personas conocidas hasta auditar permisos de todas las Req admitidas; validar
  dueño en algunas acciones no hace confiable a todo cliente. Cerrar B5, IA y salida antes
  de escalar a cuatro. Motivo: [vision-a2-servidor.md](vision-a2-servidor.md), D-19 y A1/P10.

- **D-25 (2026-10-08, mundo e IA): no ampliar claves ni fingir un traspaso con 2508.**
  Mantener inicialmente la reserva de 15.501 y medir población SAM, actores ocultos y margen
  antes de tocar su inicialización. Los máximos visibles del cable son una cota parcial.
  `ChangeAiControlAck` no contiene nuevo dueño ni época (`0x297B430`); no tratarlo como una
  API de migración. El diseño de dueño con época necesita transferencia explícita de estado,
  aceptación del nuevo cliente y rechazo de mensajes antiguos; si no se demuestra, limitar
  dispersión/contenido en el prototipo. El motivo es que ni aumentar la reserva ni emitir
  un Ack resuelve quién simula el mundo. Fuente: [vision-a1-mundo.md](vision-a1-mundo.md).

- **D-24 (2026-10-08, persistencia coherente): generaciones completas.** Conservar los blobs
  nativos mientras no exista un lector/escritor propio validado. Guardar mundo y jugadores bajo
  una raíz del servidor ajena a las partidas reales; publicar un manifiesto de generación solo
  tras completar y verificar los archivos necesarios. La pareja lobby/save no es atómica por
  existir `_new`/`_backup`: `0x186E606` y `0x186E678` llaman por separado a `0x2069640`.
  No reutilizar una ranura para otro SteamID mientras no se haya demostrado invalidación de
  todas sus cachés. Fuente: [vision-a3-guardado-mundo.md](vision-a3-guardado-mundo.md) §§2–4.

- **D-23 (2026-10-08, gestor único): exclusión y pausa de guardado.** Diseñar una cola global
  de operaciones que comparten gestor y banderas: guardar, cargar y transiciones de recarga.
  Mantener la barrera TLS `+0x4460`; eliminarla para ocultar pausas permitiría serializar un
  estado cambiante. Las operaciones nativas ocupadas se rechazan, no esperan en esa cola.
  No habilitar persistencia levantando P6 hasta demostrar aislamiento y recuperación. La
  separación fina de mundo/jugador queda bloqueada donde el componente mezcla ambos.
  Motivo/evidencia: A0-C9/C10 revisadas y [vision-a3-guardado-mundo.md](vision-a3-guardado-mundo.md).

- **D-22 (2026-10-08, visión persistente): puertas de evidencia y aforo.** Esta investigación
  no ejecuta el juego, despliega, compila ni modifica el mod. P1–P8 siguen sin validación en
  vivo; E3b y P9 son diseños. Ocho jugadores es el objetivo y diez el techo, no una capacidad
  demostrada. Separar el número de personas del de sesiones del motor: un anfitrión técnico
  aparcado también consume una sesión y sus claves. No diseñar once sesiones para prometer
  diez invitados. Base: [vision-a0-base.md](vision-a0-base.md), D0 y D-17–D-21. La huella del
  exe y las firmas se conservan en `data/vision-manifiesto.json` y
  `data/vision-firmas-2976.json`. El motivo es evitar convertir supervivencia del protocolo MMO,
  pruebas sintéticas o aritmética de claves en una promesa de mundo persistente jugable.

- **D-21 (03:30) Revisión de toda la rama contra `main`:** con `[session2]` ausente o
  `enabled=0` (y con `[coop] enabled=1`), el mod y `cdmp_peer` en relay/ghost/bot se comportan
  como en `main`, salvo el número de protocolo y una línea nueva en el log. `CMakeLists.txt` es el
  único fichero en común con lo que tienes sin commitear en `main`, y se fusiona sin conflicto.
  - **El protocolo pasa a 8 y lo mantengo:** es la regla del proyecto (`CLAUDE.md`) y lo pedía el
    traspaso. Consecuencia: **un kit de esta rama no conecta con uno de `main` (v7)**, ni con un
    `cdmp_peer` viejo; hay que instalar la misma build en los dos PC. Al fusionar con `main`, si
    allí el formato también cambia, una de las dos ramas tiene que pasar a 9.
  - Arreglos menores: `cdmp_peer` rechaza otra vez las opciones de replay fuera de
    `--mode replay`; los tests aguantan rutas con acentos y dos ejecuciones a la vez (puerto
    distinto por proceso).

- **D-20 (03:10) P9 diseñado (`opcion-d-p9.md`) y una barrera nueva en el anfitrión.** El análisis
  de la carga de partidas dice:
  - la clave de cifrado **no** depende del SteamID (solo la ruta de la carpeta). Queda una duda
    pequeña, una clave por cuenta, que se resuelve en vivo (G3a);
  - lo prudente es servir los bytes cifrados tal cual, desde una ranura reservada para el
    invitado (207), redirigida a una carpeta de intercambio;
  - el progreso puede volver por el mismo camino (`0x186E450`, legible).

  Lo que cambia ya: **la ranura 3 es CrossSave** (guardarla sube los ficheros a Pearl Abyss con la
  cuenta del anfitrión) y **la 206 se borra tras cargarla**. El anfitrión rechaza ahora cualquier
  `LoginCharacterToFieldServerReq` del túnel con la ranura 3 o 206, y se para el túnel. La guía
  de pruebas pide usar una ranura normal (0-2) en E3.

- **D-19 (02:50) Capacidad del túnel, medida en la captura 112627:** lo que el servidor manda al
  cliente durante el login tiene un pico de **5,4 MB en un segundo** (carga del personaje) y unos
  5 MB en la ráfaga de la carga de zona. Los topes del túnel están por encima: 16 MB de cola en el
  emisor (`TunnelSender`) y 32 MB en la cola del anfitrión. El túnel aguanta mientras el enlace
  drene a más de ~1 MB/s; si no, se para (no pierde paquetes en silencio). No cambio nada.

- **D-18 (02:40) Arreglos de la revisión de P8** (12 hallazgos):
  - **alto:** al reconectar (salir al título) el invitado habría recibido Ack del mundo anterior.
    Ahora, si una conexión remota termina, se para la entrega, se vacía la cola, y las
    conexiones siguientes de esa partida quedan locales (no existe todavía un reinicio pactado
    con el anfitrión). La bomba solo entrega en la conexión (época) a la que pertenecen los
    paquetes;
  - **alto:** ventana durante la instalación en la que `ConnectCryptogram` podía ir al servidor
    local y `LoginUser` al anfitrión. Ahora una conexión solo va al anfitrión si su **primer**
    paquete es `ConnectCryptogramToServerReq`; si el cliente ya conectó, los hooks ni se ponen
    ("TOO LATE");
  - el invitado solo pasa a remoto si **el anfitrión ha dicho "sesión 2 lista"** en los últimos
    3 s: paquete de control del túnel (opcode 1, que no es un mensaje del juego) que el anfitrión
    manda cada segundo, o "túnel parado" si lo mató;
  - el primer fallo de entrega o de envío corta la conexión remota (nunca se vuelve a llamar a
    `OnRecvPacket` tras una excepción); Q3 se valida con `VirtualQuery` antes de usarla;
  - **modo `observe` por defecto** (`guest_mode=observe`): ganchos puestos, todo local, solo se
    cuenta (etapa G0). Para jugar contra el anfitrión hace falta `guest_mode=remote`;
  - con `guest=1` se apagan las herramientas de desarrollo que inyectan paquetes (clon F9,
    pruebas de combate, pieza 4, aggro, relevo de enemigos, sonda de ataques).

- **D-17 (02:05) Campo compartido: diseñado, no programado** (`opcion-d-campo-compartido.md`).
  - Lo que hay: el `ServerField` del juego guarda una lista de usuarios (`+0x2A0`) pensada para
    varios, pero `AddActor` (en `.xtls`) rechaza a un segundo usuario
    (`eErrNoHasntEnterFieldPermision`). No hay ninguna rama que reutilice un campo.
  - La propuesta son dos hooks en entradas de `.rsrc`: **H1** en `0x2B07790` da al usuario 2 el
    campo del anfitrión; **H2** en `0x2BC04F0` mete `{usuario 2, 0}` en la lista bajo los dos locks
    del campo, para que `AddActor` lo deje pasar.
  - **No lo programo esta noche:** H2 escribe en una estructura del juego bajo sus locks, desde un
    hook, y nada de eso se ha visto todavía en vivo. Lo prudente es que antes E3 confirme en el
    log que el usuario 2 cae en otro campo. El sondeo registra ya, para cada usuario, su campo, el
    dueño (`+0x180`), los usuarios (`+0x2A8`) y los actores de usuario (`+0x2B0`). Si E3 lo
    confirma, H1/H2 es la etapa siguiente (E3b, con los criterios del informe).

- **D-16 (01:45) Fallo encontrado por el análisis de P8 y corregido:** `ConnectCryptogramToServerReq`
  (2319, el primer paso del login) no es un handler de servidor (`kind 4`) sino "interno"
  (`0x7FFFFFFE`), y el servidor lo encuentra por máscara de bits. El anfitrión lo descartaba y
  **E2 habría fallado en el paso 1**. Ahora se acepta por nombre. Las demás Req internas
  (temporizadores y el logout nativo `LogoutSessionToServerReq` 2001) **no** pasan por el túnel:
  el logout lo usará el invitado al salir al título (P8, G5) y no está probado; hasta entonces,
  dejar que el invitado cierre la sesión 2 es un riesgo sin necesidad. El test de replay no lo
  veía porque el anfitrión falso acepta cualquier opcode.

- **D-15 (01:30) Riesgos de E3 (análisis estático, `opcion-d-e3-riesgos.md`).** El mundo del
  anfitrión no se reinicia: la carga solo escribe en los componentes del usuario 2, y cada
  `MoveField` le da al usuario 2 **su propia instancia de `ServerField`**. Por eso lo más probable
  es que el anfitrión **no vea** al usuario 2 en E3, aunque todo funcione. Decido:
  - el sondeo registra el personaje, el `TS` y el sector de los dos usuarios y los 32 B de la
    hora del mundo del servidor (`exe+0x6A6D238`), para decidir en E3 si hace falta la pieza
    nueva (gancho en `0x2B07790` que meta al usuario 2 en el campo del anfitrión);
  - la prueba E3 usa por defecto `LoadingCompleteReq` con `00`
    (`login_e3lc00_112627.troc`, `make_troc_script.py --loading-complete 00`): según el
    desensamblado, con `00` el servidor no copia la hora guardada del usuario 2 en la hora del
    mundo, y el resto del handler y su Ack no cambian. Si se atasca, la variante `01` es la
    captura tal cual;
  - no restauro la hora en memoria (eso sería escribir en el juego sin necesidad todavía): solo
    se registra;
  - la guía de pruebas pide estar quieto durante E3 (gestor de guardado único: R3) y avisa de
    que puede salir un aviso de guardado fallido (autoguardado del usuario 2 bloqueado: R2).

- **D-14 (01:15) Segunda pasada del revisor sobre los arreglos.** Aplicado:
  - el reinicio del servidor se detecta también por el **hilo** (`OnRecvPacket` solo lo llama
    `ServerRunThread`; un servidor nuevo trae un hilo nuevo), porque el asignador puede devolver
    las mismas direcciones a `ServerNet` y a la sesión 1; y el usuario de una sesión solo se usa
    si sigue siendo un `ServerUserActor` cuyo `+0x1F8` apunta a esa sesión;
  - la lista de Req que el túnel nunca pasa al servidor cubre ahora **todas** las de dominio
    cheat/dev de la tabla (comprobado contra `troc_messages_2976.csv`), más `DeleteCharacterReq` y
    `ReportFromViewerReq`; ninguna del login ni del movimiento queda bloqueada;
  - un paquete del invitado que no es una Req del servidor (p. ej. su trama 1001) se descarta y
    se cuenta, no para el túnel;
  - `ExceptOneSelf` excluye la sesión propia del actor (`vf(0xE8)`, como el juego); si el actor
    no tiene (un NPC), se deja como hoy.

- **D-13 (01:00) Arreglos de la revisión del código** (agente revisor, 17 hallazgos):
  - **alto:** los hooks de los buscadores usaban `call()` de safetyhook, que mantiene el mutex
    del hook durante el original; con el lock del actor dentro, dos hilos del servidor podían
    interbloquearse (ya con `finder_log=1`). Ahora `unsafe_call` (no se desenganchan nunca en
    caliente). Lo mismo en los hooks de KernelBase (`unsafe_stdcall`), que si no serializaban
    todo `CreateFileW` del proceso;
  - **alto:** si el servidor del juego se reemplaza (volver al título, otra partida), la sesión
    2 apuntaba a un usuario liberado. Ahora la sesión 2 queda atada a `ServerNet` y su sesión
    1; si cambian, se deja la vieja (sin liberar) y se construye otra; una conversación que ya
    había empezado se da por perdida;
  - hooks creados desactivados y activados ya asignados; ninguna excepción C++ sale de un hook
    (sin `thread_local` de 32 KB); `*res = 1` si no se puede devolver el búfer (lo libera
    `SendPacket`); el túnel **nunca** pasa al servidor Req de guardado, de borrado de partidas,
    de trucos ni de desarrollo (por nombre, al instalar); cualquier otro rechazo para el túnel
    en vez de perderse en silencio; P5 lee el controlador como el juego (`[TS+8]+0x90`);
  - **el guardián del guardado solo se activa si alguna opción escribe en el juego**: la sonda
    (prueba 1) y el recuento de buscadores (prueba 2) no tocan el guardado. La prueba de guardar
    con el guardián pasa a la prueba 3;
  - `OPEN_ALWAYS` de solo lectura sobre una partida que existe se permite (es una lectura; antes
    se bloqueaba y podía romper la carga de la ranura en E3).

- **D-1 (23:40) Worktree en vez de cambiar de rama en `C:\Emulador CD`.** En `main` había 6
  ficheros modificados y 3 nuevos sin commitear (reloj del juego: `game_clock`, `inject_clock`,
  `clock_mapper`, `local_capture`, `remote_puppet`), tocados a las 22:58. No son míos y podrían
  ser de otra sesión en marcha. Hacer `git checkout -b opcion-d` allí se los habría llevado a la
  rama nueva (y `CMakeLists.txt` choca con lo que voy a tocar). Con `git worktree add` la rama
  vive en `C:\Emulador CD-opcion-d` con su propio `build\`, y `main` no se toca. Las
  herramientas de análisis (`build/research`, `captures/`, `reference/`) no están en git: las
  uso por ruta absoluta desde `C:\Emulador CD\`.
- **D-2 (23:40) Ni juego ni `deploy.ps1`.** Leer `CrimsonDesert.exe` del disco para el análisis
  estático sí (solo lectura, no se ejecuta).
- **D-3 Tests de la opción D en ejecutables aparte** (`cdmp_tests_optd`, `_hooks`, `_replay`) y no
  dentro de `tests/tests.cpp`: así la rama choca lo menos posible con `main`. Por lo mismo, las
  direcciones de la opción D van en `src/mod/session2/offsets_s2.h` y no en `offsets.h` (que
  tiene cambios sin commitear en `main`).
- **D-4 P6 bloquea en KernelBase, no en la tabla de imports del exe.** El exe importa
  `CreateFileW/A`, `CreateFile2`, `DeleteFileW`, `CopyFileW`, `MoveFileExW`,
  `SetFileAttributesW/A`, `RemoveDirectoryW` (`build/research/optd/imports.py`), pero la CRT
  (`_wfopen`…) llama desde `ucrtbase` y no pasaría por esos imports. Los hooks van en la
  **entrada** de las exportaciones de KernelBase (permitido por la regla de hooks). El guardián
  se activa al arrancar y dura toda la partida: ni el anfitrión puede guardar mientras
  `[session2] enabled=1`.
- **D-5 P6, capa 2 (filtrar `SaveAutoReq` 3037, `SaveToFileCurrentPlayerReq` 2910 y los
  temporizadores 2077/2011/2070) solo cuenta por ahora.** Saltarse el `Process` de un
  temporizador o de una Req puede dejar al cliente esperando un Ack o romper el re-armado del
  temporizador. El bloqueo de ficheros (capa 1) ya impide escribir.
- **D-6 `sesión2+0xC8` apunta a Q2** (la cola en la que el servidor recoge los búferes S→C para
  liberarlos). Nadie la lee, porque la `Send` de la sesión 2 es nuestra. Pero si algo llamara a
  la `Send` original, el búfer volvería a donde debe: ni fuga, ni paquete del invitado en el
  cliente del anfitrión (lo que pasaría con Q1), ni puntero nulo (cierre).
- **D-7 P3 con vtable privada y no con inline hook en `0x1437740`** (idea de D0 §9). La sesión 1
  (el anfitrión) no pasa nunca por nuestro código, y la ruta más caliente del servidor queda
  intacta. `Send` no tiene llamadas directas (xrefs = 0), solo por la vtable.
- **D-8 P5 excluye por controlador, no por "dueño de la sesión".** Con `actor+0x90` (clave del
  usuario que controla el actor) y `usuario+0x60`: si controla el anfitrión, el relevo va solo
  al invitado; si controla el usuario 2, solo al anfitrión. Si el controlador no es ninguno de
  los dos, se deja como hoy (vacío). Sin usuario 2 conectado no cambia nada. Se llama a la
  `vf1` de `ToNear` (`0x2A75860`: `TS = [[finder+0x10]+0x68]+0x1A0; jmp 0x2BC51E0`) con el
  mismo `finder`, en vez de llamar a `0x2BC4F70` con argumentos supuestos.
- **D-9 La sesión 2 se construye en el hilo del servidor** (dentro del mid-hook de
  `OnRecvPacket`), no en un hilo del mod: así el constructor y sus sub-objetos salen de los
  asignadores del juego en un hilo del juego, como la sesión 1.
- **D-10 Modo espejo (E4) con claves de clase `0xA…`.** En la captura, el jugador es
  `0xA0100001` y todos los NPC son `0xB…`; todas las Req de movimiento llevan slot 0. El espejo
  copia las Req de movimiento de la sesión 1 cuyo actor es de clase `0xA` y no es del usuario 2.
- **D-11 El canal de cada Req inyectada sale de la tabla de handlers del juego** (`Troc+0x14`) y
  no viaja por el túnel: el servidor compara contra esa misma tabla.
- **D-12 `cdmp_peer --capture` reescribe la cuenta y no manda guardados**: reproducir el login
  de una captura con la cuenta 0 sería un relogin del anfitrión (lo echaría).
