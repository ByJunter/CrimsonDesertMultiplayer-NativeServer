# S38 fase C: los viajeros del AutoSpawnRoadReq 2550 del invitado los controla el invitado (`user2_road_control`)

Fecha: 15-10-2026. Rama `opcion-d-faseC`, sobre `ce5d47d` (fase B con `_NPCScheduleGroupActionMember`). Build del juego
**1.0.0.2976**, timestamp **0x6AB28F00**, base **0x140000000**: todo RVA de este documento vale sólo para esa build.
**No se ha ejecutado el juego, ni el peer, ni Shadow para esta entrega**: sólo `scripts/build.ps1` con sus pruebas
(**72/74**: las dos que fallan, `launch_2inst_script` y `host_save_restore_script`, se niegan porque el juego estaba
abierto en el PC: «REFUSED: … is already running / the game is running»; no son de esta fase).

Implementa el punto 4 del orden de la auditoría ([AUDITORIA.md](s34-investigaciones/AUDITORIA.md) §8: «procedencia y ciclo
de vida de viajeros … distinguir 2550 actual de reconstrucción `28981B0`; negativos obligatorios. Limpieza estricta y
continuable tras desconexión, publicación cancelada y unidades completas»). Diseño de partida:
[3-viajeros.md](s34-investigaciones/3-viajeros.md) §4 (**I3**); cambios obligatorios: AUDITORIA.md §3 y
[INFORME-A3](s34-investigaciones/datos/auditoria-codex-20261014/a3-viajeros/informe-a3.md) §3–§4 (**A3-n**); familias:
[s36-fase-b.md](s36-fase-b.md); estado y plan: [s35-relevo.md](s35-relevo.md) §6 «Fase C».

Todo va detrás de `[session2] user2_road_control = 0 | probe | 1` (anfitrión). **`0` es exactamente lo de antes**: el
mid sale en la primera comprobación (un atómico), no se registra nada, no hay líneas nuevas y la continuación en los
Trocs ajenos es una carga atómica. `probe` hace todo el veredicto, la unidad, la tabla y las líneas **sin escribir ningún
controlador ni retirar nada** (la sonda S33/S34 que ya existía con ese nombre sigue igual y se amplía). `1` asigna.

## 1. El problema

S34 (I3 §2, AUDITORIA §3 [V]): los 327 actores que nacen del 2550 del invitado (raíz y acompañantes, tipos 3/5/6)
nacen **todos** con el anfitrión como controlador (`0x90100000`). Lejos de su personaje, el cliente del anfitrión no
pone en marcha su locomoción: 139/140 y 81/82 quietos (≤ 1 m). El invitado, que está al lado, sí movería NPC que
controla (los de U-e caminan 22–39 m en la misma ventana). En un jugador solo el solicitante de 2550 es también el dueño
del campo y su controlador: asignar `controlador = usuario del solicitante` reproduce ese contrato.

## 2. Qué cambia y por qué

Código: [`user2_road_logic.h`](../../../src/mod/session2/user2_road_logic.h) (pura, probada), `user2_stages.cpp` (sección
«S38 fase C», en el mid existente, la limpieza y las líneas), `user2_stages.h` / `user2_funnels.cpp`
(`user2_stages_any_troc_exit`), `npc_probe_client(_logic)` (el invitado acepta `1` y separa su resumen), `config.h`.

### 2.1 Un único gancho y la procedencia (A3-1, A3-2; AUDITORIA §7)

Ningún gancho nuevo: el `create_mid` 0x2792A7B de `docked_gimmicks` (`ctl_write_mid` → `user2_stages_npc_control`). Orden
en el mid: U-e y familias (`npc_control_impl`) → **viajeros (`road_control_impl`) sólo si eax no cambió** →
`npc_observe_birth`. Son excluyentes por vtable: U-e sólo asigna `CreateServerActorDesc_NPCSchedule` (0x5B04CB0) y sólo
en 2043/2045/2046; la carretera sólo `ICreateServerActorDesc` (0x5B04BE8) y sólo en el Troc 2550 (prueba
`provenance_reader`).

| Hecho (build 2976) | Fuente |
|---|---|
| 2550 → `0x29C4930` → `0x2C41E50` → .xtls `0x10880460` → `0x27E8C90` (AutoSpawnRoad) → `0x27EA160:564` | exports `0x29c4930.c`, `0x27e8c90.c` |
| `0x27EA160` lo llama también **ActivateSpawningPool `0x27EE3E0`** (cuatro llamadas) | `0x27ee3e0.c:1531/2006/2119/2233` |
| Raíz: `0x278EA70` (vtable 0x5B04BE8), `+0x3B8` = solicitante si es jugador, `+0x3C8` = su clave, `+0x3CC` = primero del lote, **`+0x3CD = 1`**, `+0x3D0..+0x3E8` carretera | `0x27ea160.c:1433-1480` |
| Acompañantes: mismos campos, word `+0x3CC = 0x100`, y `thunk 0x1500E63B0(raíz, &acompañante)` | `0x27ea160.c:1660-1720` |
| .xtls `0x100E63B0`: envoltorio con vtable **`_Rider` 0x5B04E60** (`0x100E64D8 lea rax,[rip-0xA5E167F]`), `[inner+0x2F0] = raíz` (`0x100E64D1`), `push_back(raíz+0x2F8)` con `0x43DB70` (`0x100E650B`); exige `[padre…+0x68] == 5` y asiento libre | capstone sobre el PE (este trabajo) |
| `0x28981B0` (contraejemplo A3): el MISMO descriptor, `+0x3B8`, `+0x3C8` desde AutoSpawnOwnerData guardado, word `+0x3CC = 0x101` (`0x28995E9`) | `a3-viajeros/ghidra-0x28981b0.c:347-888` |

**Procedencia positiva** (raíz, `road::root_verdict`): vtable 0x5B04BE8, `+0x3CD == 1`, `+0x3C8 ==` clave del personaje
del usuario 2, `+0x3B8 ==` su puntero (local, sólo se compara) **y el nacimiento ocurre DENTRO del Troc 2550 del usuario
2** (`subject_user2(&op)`, `op == 2550`). La marca sola no basta (0x28981B0 y ActivateSpawningPool escriben el mismo
descriptor): el Troc es lo que separa «la petición 2550 actual» de la reconstrucción guardada y de los pools. Si la
creación del lote (`thunk 0x1501C9D20`) resultara diferida fuera del Troc, todas las raíces salen `fuera del Troc 2550`
y el modo 1 no asigna nada (seguro; probe lo mide primero). Los campos de datos guardados de 0x28981B0 (`+0x280`,
`+0x298`) se registran en cada línea (`datos_guardados=`) **sin decidir** ([H], sin medida: no es una guarda).

Orden del veredicto: sin marca → **pedida por el anfitrión (nunca)** → otro solicitante (o clave del invitado
desconocida) → `+0x3B8` distinto → fuera del Troc 2550 → parada → `actor+0x90` puesto o ilegible → `desc+0x40` → hijo de
usuario (`actor+0xA0`: montura, mascota, compañero) → campo no del anfitrión → eax no del dueño del campo → tipo fuera
de 3/5/6 → clave del usuario 2 desconocida → **sesión desconocida (nunca cuenta como coincidencia, A3-2)** → clave
distinta de la sesión → asignada.

### 2.2 La caravana entera (A3-1; fase B §2.2)

En el mid de la raíz, antes de cambiar EAX, `road::scan_unit` recorre su subárbol (`+0x2F8`, BFS acotado: 15
descendientes, 16 hijos directos, profundidad 4). Cada nodo debe ser un envoltorio **`_Rider` o `_Simple`** (§9)
cuyo interior es un descriptor de carretera del **mismo solicitante** (clave y puntero), con `[interior+0x2F0] ==` su
padre y `[interior+0x40] == 0`, o un **gimmick** (`InstantGimmick` bajo `_Simple` / `_AutoSpawnGimmick` /
`_CombinationGimmick`): nodo **host-kept** (fase B §2.7) cuyo subárbol (la carga de un carro) no se recorre. Cualquier
otra cosa (otro envoltorio, otro solicitante, sin marca, ilegible, ciclo, demasiado grande/profundo, o un miembro que
**ya nació antes que su raíz** en este lote: mismo descriptor y mismo padre) deja la unidad **entera con el anfitrión**
(`unidad no demostrable`).

**Registro antes de autoridad** (A3-2): la unidad entera (raíz + nodos del preexamen) entra en la tabla propia
`road::Table<256>` (época, token de nacimiento propio, solicitante, controlador) **antes** de escribir eax; sin sitio
(tabla o marco del hilo) la raíz no se asigna (`sin registro libre`).

**Miembros** (`road::member_verdict`, en su propio mid): su `+0x2F0` nombra un descriptor de una unidad abierta en este
hilo; se comprueba de nuevo la procedencia (mismo solicitante), padre vivo con el controlador de la unidad, `actor+0x90`
/ `desc+0x40` / `actor+0xA0` a 0, campo del anfitrión, eax del dueño, tipo 3/5/6 y la clave del usuario 2. Un **tardío**
(no estaba en el preexamen) sólo entra con pertenencia demostrada ahora en el vector de su padre, envoltorio de viajero
(`_Rider` / `_Simple`) y registro libre. Si un miembro no puede seguir a su raíz, nace con el anfitrión y la unidad queda **PARTIDA**.

**Hijos que no son viajeros**: un gimmick tipo 7 (`InstantGimmick`) cuyo `+0x2F0` es un registro de una unidad
abierta se queda con el anfitrión (**host-kept**, fase B §2.7: `publication_gate=1`, que el modo 1 exige) y se registra
si cabe para retirarse con ella; nunca la parte. Cualquier otro descriptor la parte (`hijos_no_viajeros`) y queda
registrado con su controlador nativo. Un miembro del preexamen que **no nació en el Troc de su raíz** también la parte (`NotBorn`: nunca «ya
vendrá»). Hijos sólo de **jerarquía** vistos después en la publicación (`user2_stages_npc_family_observed`): se cuentan
(`hijos_de_jerarquía_tardíos`, «con otro controlador») y nunca parten ni retiran (regla R4 de la fase B).

**Modo 1, unidad partida** → al cerrarse su Troc pasa a retirarse **entera**, de hojas a raíz (§2.4). La cohorte es la
caravana que documenta I3 §4.5: conductor/personas (3), carro y caballos de tiro (5) y animales/bandadas (6), **sólo**
como unidad de carretera de 0x27EA160 en el Troc 2550 del invitado; nunca la montura o mascota de nadie (`+0xA0`,
`desc+0x40`), nunca tipo 4/7, nunca un NPC de misión (otros descriptores / otros Trocs).

### 2.3 Tabla y ciclo de vida propios, separados de U-e (A3-3..5; AUDITORIA §6 «Fronteras»)

Los viajeros **no** entran en `g_ctl_keys` (U-e) ni en `g_activation` (contención de `npc_activation`): ni el vigilante
de U-e («unaccepted», `LeftAfterDrain`, `HostDrives`, sus drenajes) ni la contención los ven. Decisión con evidencia: un
viajero no es un NPC de stage, ningún stage lo hace renacer con el anfitrión, y retirarlo por «sin progreso» sólo lo haría
desaparecer y el cliente pediría otro (el bucle de S31); su IA con el invitado no está medida (H1b no se registra para
ellos). Su vida la deciden su carga y su Logout:

| Evento | Viajeros del usuario 2 |
|---|---|
| El anfitrión se acerca o prepara (drenaje de U-1) | **Nada**: siguen con el invitado (I3 §4.6, A3 «Ciclo de vida» [P]). El cliente del anfitrión los recibe como observador; con restore 0 los retiene `child_hold` (abajo) |
| Su carga (2283 / 2362 / 2437: viaje rápido, teletransporte, cambio de campo) o su 2001 | `road_request(su carga / su despedida)` a la entrada de su Troc, **con cualquier `user2_stages`**: las unidades asignadas vivas pasan a retirarse, y se retiran ya |
| Logout de su personaje (punto O, hilo del servidor, personaje aún entero) | `road_request(su Logout)` tras `logout_drain` / `logout_cleanup` de U-e |
| Época nueva (reconexión) | las asignadas vivas de épocas anteriores se retiran (revalidando clave y controlador) |
| Se aleja sin cargar | **sin acción en la v1** (A3: medir, no inventar radio) |

**Retirada estricta** (`road_cleanup_run`; A3-3, A3-4, A3-5):
- **Su propio estado de fallo**: el filtro SEH (`road_fault`) para sólo esta pieza (`PARADA`, sin tocar
  `g_activation_fault` / `g_ctl_aborted`) y **no oculta** la excepción del motor (`game_fault_continue`:
  `EXCEPTION_CONTINUE_SEARCH`, como toda llamada al juego del mod).
- **Resultado estricto**: `Logout` 0 o «ya saliendo» → *saliendo*; cualquier otro código → rechazo con reintento a 1 s,
  tres rechazos → **FALLIDA** (terminal, visible, sigue con el usuario 2); nunca «hecho» por un código desconocido
  (`npcact::logout_result(…, strict=false)` no se reutiliza).
- **Desaparición confirmada**: tras *saliendo*, `SAM::FindActor` (sin Logout) debe dejar de encontrarlo con nuestra clave
  y controlador; si sigue 30 s → **SIN CONFIRMAR** (terminal, plazo experimental). «Otra clave o controlador» = ya no es
  nuestro, no se toca.
- **Publicación cancelada antes**: las claves vivas de la unidad (validadas en scopes nativos) pasan por
  `child_hold_retire_family` (tombstones, colas de `observer_hold_acks`, tablas de retención, cola saliente) **antes** de
  cualquier Logout; si se rechaza, se reintenta (10 veces y la unidad queda FALLIDA). Un 2005 viejo no resucita una baja.
  En otra época no hay publicación que cancelar.
- **Hojas a raíz** (`road::leaves_first`, `road::block_state`): un registro con un descendiente vivo, pendiente o saliendo
  espera a otra pasada; con uno terminal, él tampoco se retira y queda terminal (nunca un padre retirado con un hijo vivo).
- **Barreras**: `ModLock`, la barrera de Trocs del usuario 2 (`g_p_draining`, la de la contención) y ningún otro Troc suyo
  en vuelo; sin SAM o con el cerrojo ocupado no se finge nada (se reintenta).
- **Continuación aunque el invitado ya no envíe nada** (A3-5): como mucho 32 llamadas por pasada; lo pendiente sigue en
  la salida de **cualquier Troc del servidor** (los del anfitrión también: `user2_stages_any_troc_exit` desde
  `user2_funnels`, cada 100 ms como mucho) y en los del invitado. Prueba `continuation_without_guest_troc`: 65 / 91 / 128
  unidades pendientes sin ningún Troc del invitado se retiran todas, con el cerrojo a veces ocupado.

### 2.4 Paradas de esta pieza (sin aborto global)

Paran las asignaciones **nuevas** (raíces `parada`); las unidades ya asignadas siguen con su controlador (y sus miembros
siguen a su raíz) y su retirada en carga / Logout continúa. U-e, la contención y las familias no cambian.

| Parada | Cómo se detecta |
|---|---|
| **2005 con otro controlador** | un 2005 de un viajero asignado, al invitado o capturado en el cliente del anfitrión, con controlador distinto del escrito |
| **Restore 0 al anfitrión** | el cliente del anfitrión recibe (captura S>C) un 2005 de un viajero asignado con restore 0 (debe ser 0: lo impiden `child_hold`/`WithheldUser2` y `publication_gate`) |
| **Doble control** | el cliente del anfitrión envía una Req de IA **de controlador** (2223, 2734, 2291, 2405, 2828) para un viajero asignado. Evidencia S34 (I3 §2.1): el cliente que no controla a un viajero 3/5/6 no envió ninguna de éstas (sólo 2556 para los «Game Mechanic» tipo 7), así que una es su IA corriendo también allí. Un 2556 del anfitrión sólo se cuenta (indicio, A3-6) |
| **Excepción SEH** | en la limpieza nativa (`road_fault`) |

**Publicación hacia el anfitrión**: un viajero del usuario 2 con restore 0 nunca llega al cliente del anfitrión:
`observer_hold` lo retiene por controlador (`npc_controller_who` = invitado, restore 0 → `Hold`; pasado el plazo
`WithheldUser2`, nunca `Timeout`) y `publication_gate=1` cubre las salidas `Native` (lectura caducada, otra encarnación,
tabla llena). Por eso el modo 1 **exige** `observer_hold=1` y `publication_gate=1`; y `npc_activation=1` (la barrera de
retirada que cancela la publicación). Si falta alguno: `WRN User2Road: user2_road_control=1 necesita …: queda en probe`.
Las funciones de su limpieza (`0x2A827B0`, `0x1434880`, `0x2068340`) se comprueban como las de U-e; si no son las de la
build: `ERR …: queda en probe`. No exige `user2_stages=1` ni U-e.

### 2.5 El invitado

`npc_probe_client`: acepta `user2_road_control=1` y lo trata como `probe` (sólo observa). Nuevo:
`NpcProbe [invitado viajeros-por-solicitante] … | solicitante 0x… controlador 0x…: activos N desplazados M
con-Req-propia K | …` cada 10 s, junto a `movimiento-resumen`. El antiguo «viajeros activos/desplazados» mezclaba los del
anfitrión (que se mueven) con los suyos (I3 §2.2): ahora se separan por quién los pidió y quién los controla.

## 3. Clave e inis

| Clave `[session2]` (anfitrión) | Valores |
|---|---|
| `user2_road_control` | `0` (por defecto) \| `probe` \| `1` (con `npc_activation=1`, `observer_hold=1`, `publication_gate=1`) |

Inis ([`docs/research/ini/`](../ini/)), derivados de `cdmp.2inst-host.ini` (dos copias en un PC) cambiando **sólo** esa
clave y su comentario: `cdmp.2inst-host-faseC.ini` (`probe`) y `cdmp.2inst-host-faseC-1.ini` (`1`). Familias siguen en
`probe` (no se activan dos políticas nuevas a la vez). Invitado: `cdmp.2inst-guest.ini` sin cambios.

## 4. Líneas del log (anfitrión)

- Arranque: `User2Road S38: user2_road_control=<m> (pedido …) | viajeros del AutoSpawnRoadReq 2550 del invitado:
  procedencia = …` o el `WRN … queda en probe`.
- Por nacimiento (cupos 3000 en 1 / 1500 en probe por sesión): `User2Road [viajero asignado]` / `[viajero: se
  asignaría] qpc=… T… | key=0x… tipo=N clase=t3|t5|t6 papel=raíz|miembro|tardío raíz=0x… solicitante=0x…
  controlador_escrito|que se escribiría=0x… (nativo 0x…) op=2550 +0x3CC=… +0x3CD=1 carretera +0x3D0=… +0x3D8=… +0x3E0=…
  +0x3E8=… datos_guardados(+0x280/+0x298)=a/b nacimiento=… nodos_preexamen=N | n.º_en_su_carretera=K`. Desde el §11
  se **muestrea** por carretera (las primeras 40, luego 1 de cada 25; las de unidades tipo 5, todas); los cupos quedan
  como tope.
- Descartes con algo que medir (8 por veredicto): `[viajero descartado] … veredicto=<pedida por el anfitrión | fuera del
  Troc 2550 del usuario 2 | unidad no demostrable | …> preexamen=<…> envoltorio=exe+0x… interior=exe+0x… | op=…
  sujeto_usuario2=… …`.
- `WRN [viajero partido] … motivo=…` (64), `[viajero sin unidad]` (16: miembro de una raíz no abierta), `[viajero:
  hijo de jerarquía fuera de su unidad]` (32).
- Por unidad al cerrarse su Troc (500): `[viajero unidad] raíz=0x… unidad=asignada|se asignaría estado=completa|PARTIDA
  (motivo)|pendiente registros=… preexamen=… nacidos=… tardíos=… miembros_…=… | t3= t5= t6= otro= | solicitante
  controlador [-> se retira entera]`.
- Retirada: `[viajeros: retirada pedida] motivo=… unidades_asignadas=N unidades_que_se_retirarían(probe)=M op=…`;
  `[viajero retirado] … nacidos= fuera= fallidos= sin_confirmar= intentos_publicación= desde_la_petición_ms=`; `WRN
  [viajero: retirada FALLIDA] …`.
- `ERR User2Road [PARADA] motivo=… clave=0x…: no se asignan viajeros nuevos; …`; `WRN [2005 discrepante] …`; `WRN [Req
  del cliente del anfitrión] … doble control | indicio`.
- Cada 60 s, tres líneas: `User2Road [resumen]` (raíces asignadas/que se asignarían por clase, descartes por veredicto,
  preexámenes fallidos por motivo, miembros y partidos por motivo, sin unidad, hijos no viajeros, tabla por estado,
  coste del mid), `[resumen vivos]` (por clase: vivos, con Req del invitado, **desplazados ≥ 1 m según sus propias Req
  de posición**, vistos por el cliente del anfitrión; latencia 2005→primera Req; 2005 al invitado / al anfitrión;
  **paridad 1:1**: 2005 al invitado con metadatos de carretera de su personaje *registrados* / *SIN registro*, y
  registrados sin 2005 tras 10 s; Req del invitado y del anfitrión; bajas nativas 25/42/44/otro) y `[resumen limpieza]`
  (peticiones, pasadas, ocupada, sin SAM, llamadas por resultado, retiradas, FALLIDAS, publicación rechazada,
  excepciones, pendiente).

## 5. Criterios de aborto (parar, desinstalar, devolver logs y capturas de los dos lados)

- Cualquier cierre de cualquiera de las dos copias.
- `User2Road [PARADA]` (cualquiera), `[2005 discrepante]`, `[viajero: retirada FALLIDA]`, `restore0` del anfitrión > 0.
- Modo 1: `[viajero partido]` más de una o dos veces en 10 min (el modelo de unidad no describe esas caravanas: volver a
  probe y mirar sus motivos); caravanas que desaparecen y reaparecen (bucle).
- Progreso de misiones en la partida propia del invitado; viajeros duplicados o congelados en el invitado.
- Coste del mid: `medio_us` > 50 o `máx_us` > 2000.

## 6. Plan de prueba

### 6.1 Un PC con `cdmp_peer` (opcional, sólo el lector)

Con `user2_road_control=probe` y el peer como invitado: el peer no pide 2550 ni ejecuta IA, pero los viajeros del
**propio anfitrión** deben salir `[viajero descartado] veredicto=pedida por el anfitrión` (lector y offsets correctos) y
ningún descriptor marcado de otro tipo inesperado. No valida locomoción.

### 6.2 Dos copias en un PC, paso 1 (`probe`)

Juego cerrado: `.\scripts\build.ps1`, `.\scripts\deploy.ps1`, copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en
`backups\`. `.\scripts\launch-2inst.ps1 -GuestSlot slot108 -HostIni docs\research\ini\cdmp.2inst-host-faseC.ini -GuestIni
docs\research\ini\cdmp.2inst-guest.ini -BackupDir 'C:\Emulador CD\backups'` ([dos-instancias.md](../dos-instancias.md)).

1. **Anfitrión lejos** (≥ 60 celdas, quieto ≥ 60 s, como S34 paso 2b), **invitado junto a carreteras** (Hernand) 5–10
   min. Esperado: `[viajero: se asignaría]` para sus viajeros (personas, carros, caballos, cabras, bandadas);
   `[viajero unidad] estado=completa`; los del anfitrión `pedida por el anfitrión`.
2. **El anfitrión se acerca** al invitado y se queda 1–2 min: ninguna `retirada pedida`; `vistos por el cliente del
   anfitrión` crece.
3. **Viaje rápido del invitado**: `[viajeros: retirada pedida] motivo=su carga … unidades_que_se_retirarían(probe)=M`.
4. **Logout del invitado**: ídem con `motivo=su Logout`.

**Criterios para pasar al paso 2:**
- `fuera del Troc 2550 del usuario 2` ≈ 0 para su solicitante (si domina, el lote se crea fuera del Troc: el modo 1 no
  asignaría nada; rediseñar con esa medida). `datos_guardados=1/1` ≈ 0 en sus asignables.
- **Paridad 1:1**: `SIN registro` ≈ 0 en t3/t5/t6 y `registrados sin 2005 tras 10 s` ≈ 0.
- `[viajero unidad] estado=completa` en ≥ 95 % de las unidades; `unidad no demostrable` sólo con motivos explicables
  (anotar cuántas por `envoltorio no _Rider` / `interior no es un descriptor de carretera`: ahí están los «Game Mechanic»);
  `hijos_no_viajeros` y `hijos_de_jerarquía_tardíos` medidos (si son frecuentes, el modo 1 retiraría esas caravanas:
  decidir antes de activarlo).
- `sin registro libre` = 0, `vínculo_ilegible` ≈ 0, coste del mid dentro de §5.

### 6.3 Dos copias en un PC, paso 2 (`1`)

Mismo guion con `cdmp.2inst-host-faseC-1.ini` (sin ningún `WRN … queda en probe` al arrancar).
1. **Anfitrión lejos, invitado en carretera**: `[viajero asignado]`; en el log del anfitrión `[resumen vivos]` con
   `desplazados>=1m` > 0 en t3 (y t5 para carros) — en S34 paso 2b era 0/27 —, `con Req del invitado` ≈ todos los
   vivos, latencia 2005→Req de pocos segundos; en el invitado, `viajeros-por-solicitante` con su solicitante y **su**
   controlador, `desplazados` > 0. Visualmente: los viajeros caminan en la copia del invitado. 2005 `OTRO=0`, sin
   `PARADA`.
2. **El anfitrión se acerca**: el controlador no cambia (sin `retirada pedida`); el anfitrión los ve llegar cuando ya
   actúan (`vistos por el cliente del anfitrión` crece, `restore0=0`, `doble_control=0`, ningún `[Req del cliente del
   anfitrión]`); se mueven también en la copia del anfitrión (como observador).
3. **Viaje rápido del invitado**: `[viajeros: retirada pedida] motivo=su carga unidades_asignadas=N` y N `[viajero
   retirado]` con `fallidos=0 sin_confirmar=0`, `[resumen limpieza] pendiente=false` en segundos; en el destino, sus
   viajeros nuevos vuelven a asignarse.
4. **Logout del invitado**: ídem con `motivo=su Logout`; ninguna caravana congelada en la copia del anfitrión.
5. Una interacción del invitado con un viajero (hablar / combate): ningún crédito en su partida propia (`quest_policy`
   sigue en `probe`).

## 7. Pruebas puras (`tests/optd_user2_road_tests.cpp`, `optd_user2_road`)

Sobre una arena sintética con la política `read` del mod: `root_truth_table` (cada salida del veredicto, tipos 3/5/6 y
4/7, el anfitrión nunca, sesión desconocida nunca), `provenance_reader` (offsets, palabra 0x100/0x101, 0x28981B0 marcado
pero fuera del Troc, NPCSchedule nunca, exclusión con `ctl_verdict` de U-e), `caravan_scan` (carro + conductor +
caballo + cabras a profundidad 2; gimmick con `_AutoSpawnGimmick` o bajo `_Rider`; otro solicitante; sin marca;
incoherente; usuario; ciclo; ilegible; 16 descendientes; 17 hijos; profundidad 5), `member_matrix` (cada motivo,
tardíos), `table_lifecycle` (registro, cierre sin punteros, carga, baja nativa, retirada hecha, probe nunca se retira,
`NotBorn`, petición con la unidad abierta, unidad posterior a la petición, otra época, unidad abierta huérfana cerrada
por el borrado), `table_capacity_and_reuse` (sin sitio no asigna, clave reutilizada, índice reconstruido),
`strict_cleanup` (resultado estricto, confirmación, plazo, hojas a raíz, bloqueo terminal),
`continuation_without_guest_troc` (65/91/128 unidades sin Troc del invitado, cerrojo a veces ocupado; Logout siempre
rechazado → FALLIDAS y se conservan mientras vivan), `stops_and_checks` (2005, Req de controlador, posición),
`modes_and_guest_summary`, `separate_from_ue`. **No factible con las costuras actuales**: el pegamento del mid y la
limpieza nativa de extremo a extremo (leen el sujeto de `user2_funnels`, el SAM y el campo del juego); su lógica está en
`user2_road_logic.h` y se prueba ahí. `dumpbin /disasm` (VS Build Tools 14.44) del `cdmp.asi`: el camino del mid
(`road_control_impl`, `road_root`, `road_member`, `road_foreign_child`, `scan_unit`, `read_prov`, `in_vector`,
`Table::open/add_late`, `Index::put`) no llama a `memset`/`memcpy`/`memmove`/`__chkstk`; sólo `fast_read`, atómicos,
`subject_user2` y el SRWLOCK de la tabla.

## 8. Lo que NO se hizo y lo que queda por medir

(Actualizado tras la prueba de §9: `_Simple` ya se admite; los gimmicks tipo 7 son host-kept.)

- **Nada medido en vivo.** Los criterios de §6.2 deciden si el modo 1 tiene sentido.
- **¿Nace el lote dentro del Troc 2550?** (`thunk 0x1501C9D20`, I3 §5). Si no, el modo 1 no asigna nada (seguro).
- **¿Todas las caravanas usan `_Rider`?** Y dónde están los «Game Mechanic» tipo 7 (21/13 en S34): en el vector al nacer
  la raíz (unidad con el anfitrión), tardíos en el Troc (unidad partida → retirada) o sólo de jerarquía después
  (contados). Política conservadora: el tipo 7 nunca se asigna y nunca se deja una raíz del invitado con un hijo de
  descriptor del anfitrión. Si el paso 1 los muestra en casi todas las caravanas, el modo 1 dejaría esas caravanas con el
  anfitrión: decidir con datos (p. ej. un contrato demostrado del tipo 7, AUDITORIA §2 «Tipo7»).
- **`+0x280/+0x298` como marca de 0x28981B0** [H]: sólo registrado.
- **IA de tipos 5/6 y bandadas con el invitado como controlador** [NV]; bandadas tipo 6 en restore 0 quedarían
  invisibles para el anfitrión (`WithheldUser2`): seguro, pero visible; medir con `vistos por el cliente del anfitrión`.
- **Plazo de confirmación** (30 s) y reintentos (3 × 1 s): experimentales.
- **Alejarse sin cargar**: sin acción (A3).
- **Combate** con viajeros (`combat_follow`, `remote_interp` en modo local) y **crédito de misiones** del anfitrión: por
  medir (§6.3.5).
- **Una retirada FALLIDA** deja actores del usuario 2 con tombstone (invisibles a los clientes) hasta su baja nativa:
  terminal y visible en el log, como en la contención.

## 9. Prueba en dos copias, probe (15-10 noche) y correcciones

Kit `28863db` (a3ce30e + correcciones de la fase B `1a177f6`), dos copias en un PC, anfitrión lejos, fase B activa,
`user2_road_control=probe`. Log `C:\Emulador CD\backups\p8-2inst-noche6-faseCprobe\cdmp.host-Cprobe.log` (el del
invitado al lado). 0 ERR, 0 `PARADA`, 0 doble control, ningún aborto. Pero la cobertura era baja:
`[resumen vivos] … se asignaría t5=3 t6=14` (17 unidades, todas `completa` con `preexamen=0 nacidos=1`) frente a
`paridad 1:1 … SIN registro t3=24 t5=2 t6=48`.

**Lo que dice el log** (todas las líneas del invitado con `op=2550 sujeto_usuario2=1 prof_troc=1`, todas las raíces con
`+0x3CC=1`; ningún descarte `fuera del Troc 2550`: **el lote nace dentro del Troc 2550**, sincrónico, en el hilo de su
Troc; la procedencia por el Troc sirve tal cual):

1. **`_Simple`, no sólo `_Rider`**: 20 de 21 preexámenes fallidos eran `envoltorio no _Rider envoltorio=exe+0x5B04E80
   interior=exe+0x5B04BE8` (bandadas 0x1F5: `0xB0100275` y sus 4 aves `0xB0100276..279`; caminantes 0x3FD tipo 3:
   `0xB0100282/285/288` con sus acompañantes). `0x5B04E80` es `_Simple`, el mismo contrato que 0x281DBD0 (fase B §2.1).
   `_Rider` (0x100E63B0) sólo para los conductores de vehículo (raíz tipo 5, carreteras 0x7D1 / 0xFA1). Cada raíz
   rechazada dejaba a sus hijos `sin unidad` (52).
2. **El descriptor de la raíz no se guardaba** (`Table::open` registraba la clave de la raíz pero no su descriptor):
   ningún hijo DIRECTO podía encontrar su unidad. El carro `0xB010028E` (tipo 5, preexamen=1) quedó `PARTIDA (un
   miembro del preexamen no nació…)` porque su conductor `0xB010028F` (tipo 3, `+0x2F0` = la raíz) salió `sin unidad`;
   la publicación lo vio después como «hijo de jerarquía» del carro (`0xB010028F`, `0xB0100290` tipo 5, y tres tipo 7).
   Los mismos 3 `PARTIDA` (`0xB010028E`, `0xB0100392`, `0xB0100394`).
3. **«Un miembro nació antes que su raíz»** (`0xB010028C`) era falso: cada entrada 2550 es su propia llamada a
   0x27EA160 y sus descriptores se destruyen al terminarla (:1886); el siguiente lote reutiliza direcciones y la lista de
   huérfanos del hilo comparaba sólo el descriptor.

**Correcciones** (`user2_road_logic.h`, `user2_stages.cpp`):
- `scan_unit` / `in_vector`: envoltorio de viajero = `_Rider` **o `_Simple`** (interior de carretera del mismo
  solicitante, como antes). Gimmicks (`InstantGimmick` bajo `_Simple`/`_AutoSpawnGimmick`/`_CombinationGimmick`):
  nodos host-kept, sin recorrer su carga; otro envoltorio sigue rechazando la unidad entera.
- `Table::open` guarda el descriptor de la raíz (`r[0].desc`).
- **Sellado por lote**: la primera raíz de un lote (`+0x3CC = 1`, el «primero del lote» de :1478) cierra las unidades
  abiertas del lote anterior en este hilo (censo, punteros olvidados): un descriptor nunca se confunde con uno ya
  liberado. `BornFirst` exige el mismo descriptor **y** el mismo padre de descriptor.
- Un hijo gimmick tipo 7 (preexamen o tardío) es host-kept y no parte la unidad (`host-kept=` en `[viajero unidad]`); un
  gimmick del preexamen que no nace en el Troc no es `NotBorn`.
- Diagnóstico por clase: `[viajero sin unidad]` dice la vtable, la marca, el solicitante y el `+0x2F0` de su padre de
  descriptor y cuántas unidades tenía abiertas el hilo (cupo 48); descartes 24 por veredicto; nueva línea cada 60 s
  `User2Road [resumen clases] sin_unidad por clase: t3= t5= t6= | por su padre de descriptor: carretera= gimmick=
  horario= otro= | preexamen fallido por envoltorio: … | host-kept (gimmicks tipo 7): registrados= sin_registro= |
  lotes sellados=`.
- Pruebas: bandada con 4 `_Simple`; `_Simple` de otro solicitante fuera; gimmick con su carga de 19 (host-kept, no
  recorrida); gimmick bajo `_Simple`; `_Docking` fuera; la raíz encontrada por su descriptor; gimmick del preexamen no
  nacido sin partir; `gimmick_verdict`. `dumpbin`: el camino del mid (ahora con el sellado) sigue sin CRT.

**Esperado en el siguiente probe**: `paridad 1:1 … SIN registro` ≈ 0 en t3/t5/t6 (lo que quede, con su clase en
`[resumen clases]` y su motivo en `[viajero sin unidad]` / `[viajero descartado]`); `preexamen fallido por envoltorio`
≈ 0; `[viajero unidad]` de carros con `miembros_que se asignarían` (conductor) y `host-kept` (su carga); ningún
`PARTIDA` por `NotBorn` en carros; `lotes sellados` ≈ entradas 2550 del invitado. Si un carro sigue partido por su
caballo (`0xB0100290`, tipo 5, sólo visto como hijo de jerarquía), su línea `[viajero sin unidad]` /
`[viajero partido]` dirá con qué envoltorio y padre nace: siguiente clase a decidir con esa medida.

## 10. Revisión independiente (15-10) y correcciones

Revisión de sólo lectura de `a3ce30e` (`build/research/fase-c-revision.md`, no versionado). Veredicto: nada de
severidad crash en `0` ni en `probe`; en modo 1, tres puntos **wrong** y varios menores. Todos corregidos aquí salvo lo
marcado «medir».

| # | Hallazgo | Cambio |
|---|---|---|
| §4.1 (wrong) | `child_hold_retire_family` se niega de forma atómica (publicación en vuelo; hijo retenido en `observer_hold_family` fuera de la lista: los «Game Mechanic» sólo de jerarquía de un viajero retenido al anfitrión) y 10 negativas (~1 s) dejaban la unidad FALLIDA sin ningún Logout | (a) espaciado creciente 1, 2, 4, 5 s… y FALLIDA sólo tras **60 s** (`road::pub_refused`); (b) antes de cancelar, los hijos que `observer_hold_family` retiene de la unidad (`child_hold_held_children`, sólo lectura, try-lock) entran en ella como host-kept, validados en el SAM, y en la lista; sin sitio, su padre queda incompleto; (c) en probe, en cada carga / Logout del invitado, `unidades cuya cancelación se negaría (hijos retenidos)` en `[resumen retirada]` |
| §3.2 (wrong) | Caravanas que se parten siempre (la carreta con carga) → bucle nace/retira en cada 2550; 16 registros no caben la carga (19) | Parada automática local (`road::SplitGuard`): una clase de carretera (`+0x3D0`) con 2 unidades partidas en 10 min deja de asignarse (`su carretera ya partió unidades`), 6 en total paran la pieza (`PARADA … bucle de partidas`); probe lo cuenta («pararía»). **Raíces tipo 5 (carros, caballos con jinete o guía) fuera del modo 1** con la clave nueva `user2_road_carts=0` (por defecto; probe las sigue midiendo). 32 registros por unidad; un hijo sin registro deja a su padre **incompleto**: nunca se retira (terminal), así nunca un gimmick con su carga viva |
| §3.3 (wrong, posible crash) | La retirada sólo revalidaba clave y controlador | Antes de Logout: `actor+0xA0 == 0` (ya es de un usuario → no es nuestro, no se toca), **ningún jugador lo monta** (el padre serializado +0xAC / jerarquía del personaje del anfitrión o del invitado es este actor → nunca Logout, reintento cada segundo, terminal a los 120) y el mismo registro (clave y **nacimiento**) en la tabla justo antes de la llamada. [H] el vínculo de montura del servidor: medir con un jugador montado |
| §2 (medir → hecho) | Procedencia sólo por exclusión (Troc 2550) | **Positiva**: a la entrada del Troc 2550 del invitado (`user2_funnels`, TrocParams +0x18 / +0x10) se leen las clases de sus entradas (`road::parse_request`, el formato de 0x29C4930) y una raíz sólo se asigna si su `+0x3D0` está entre ellas (`su carretera (+0x3D0) no está en la 2550 en curso`; desconocida tampoco). Los otros llamadores de 0x27EA160 (0x27E81E0, 0x27E9B40, MeshGroupAutoSpawn 0x27F29D0) quedan fuera salvo que corran sincrónicos dentro de esa 2550 con la misma clase |
| §2 (wrong/minor) | Un hijo con vtable base SIN marca enlazado a una unidad salía `sin marca` y no partía | Pasa por `road_foreign_child` (parte la unidad) |
| §4.2 (minor) | Modo 1 con el punto O `REFUSED` | Queda en probe con un ERR |
| §4.4 (minor) | `order[8]` por índice; `leaves_first` sólo hasta profundidad 5; `ModLock` hasta 20 ms | Las 8 unidades menos recientemente atendidas (y sin esperar su siguiente intento de cancelación); la profundidad real; `ModLock(2 ms)` |
| §6 (minor) | Cerrojo exclusivo para los miembros de viajeros del anfitrión; índice que sólo se reconstruía al abrir | Sin unidad abierta en el hilo, sin cerrojo; el índice se reconstruye también al registrar un miembro |
| §4.3, §5 (minor) | `Gone` en la entrada de un Logout nativo luego rechazado; falsos positivos de doble control | Sin cambio (patrón de U-e; la parada sólo frena asignaciones nuevas) |
| §4.5 | Tipo 6 en restore 0 invisibles para el anfitrión | Sin cambio: medir (`vistos por el cliente del anfitrión`) |

**Ciclo de vida, revisado** (tabla de §2.3 con lo nuevo):

| Evento | Viajeros del usuario 2 |
|---|---|
| El anfitrión se acerca o prepara | Nada (sin cambio) |
| Su carga / su 2001 / su Logout / época nueva | Petición de retirada (sin cambio); la cancelación de publicación incluye a sus hijos retenidos y se reintenta hasta 60 s; cada Logout revalida clave, controlador, dueño (+0xA0), jinete jugador y nacimiento |
| Un jugador monta un caballo / carro de la caravana | Nunca Logout mientras lo monte (reintento; terminal visible a los ~120 s); si pasa a ser suyo (+0xA0), queda fuera de la unidad |
| Una clase de carretera se parte una y otra vez | Deja de asignarse (2 en 10 min); 6 en total paran la pieza |
| Raíz tipo 5 | Fuera del modo 1 mientras `user2_road_control=1` y `user2_road_carts=0` |

**Líneas nuevas**: `WRN [viajeros: bucle de partidas] carretera=0x… …`; en `[viajero descartado]`,
`en_la_2550=sí|NO|desconocida (clases N)`; cada 60 s `User2Road [resumen retirada] cancelación de publicación:
negativas= ocupada= hijos retenidos añadidos= sin sitio= | probe: unidades cuya cancelación se negaría= (hijos
retenidos=, child_hold ocupado=) | revalidación antes de Logout: dueño (+0xA0)= montado por un jugador= nacimiento
cambiado=`, y en `[resumen clases]` `2550 leídas= ilegibles= | partidas: asignadas= que se asignarían= | registros con
un hijo sin sitio= carros_fuera_del_1=`.

**Criterios añadidos para pasar al paso 2**: `2550 leídas` > 0 e `ilegibles` = 0; ningún descarte `su carretera
(+0x3D0) no está en la 2550 en curso` para su solicitante (si aparecen, el formato o el desplazamiento no son los
supuestos: no activar); `unidades cuya cancelación se negaría` ≈ 0 (si no, mirar qué hijos retiene
`observer_hold_family`); `partidas … que se asignarían` por clase (las que se parten siempre quedarán paradas por el
bucle en el 1: decidir antes); con `user2_road_carts=0` los carros siguen con el anfitrión en el 1.

Pruebas nuevas (`review_fixes`): clases de la 2550, procedencia desconocida, bucle por clase / ventana / global,
espaciado y presupuesto de la cancelación, jinete (Occupied), registro incompleto terminal, hojas a raíz con
profundidad 7, 32 registros por unidad en el índice; veredictos `NotInRequest` / `SplitLoop` / `CartRoot`. `dumpbin`:
el camino del mid sigue sin CRT.

**Continuación**: los caballos con jinete y los carros (raíces tipo 5) con `user2_road_carts=probe|1`, vehículo enlazado
`_LinkedVehicle`, carga host-kept entera, 48 registros por unidad: [s40-carros.md](s40-carros.md).

## 11. Rotación nativa de los viajeros con el 1 (15-10): higiene del log

Investigación: `build/research/churn-viajeros/informe.md` (sólo lectura, build 1.0.0.2976). Con `user2_road_control=1`
las bandadas 0x1F5 (tipo 6, aves) del invitado nacen a **~80/min**, viven **~29 s de mediana** (p10/p90 20/40 s),
recorren ~80 m y las retira el recolector nativo con **motivo 25**: los mismos números que las bandadas del propio
anfitrión en las mismas sesiones. Con `probe` (o sin fase C) se quedaban quietas junto al invitado (0,0 m), protegidas
por el ancla 2, y nunca se recogían: el cupo de su clase quedaba lleno. **No es un fallo**: es la paridad con un jugador
solo que buscaba el §1. Sin cambio de comportamiento; sólo log (informe §3):

1. **Volumen**: `[viajero asignado]` / `[viajero: se asignaría]` agotaba su cupo de 3000 en ~35 min (2435 en 30 min).
   Ahora se muestrea por carretera +0x3D0 y modo (asignado / se asignaría): las primeras 40, luego 1 de cada 25
   (`n.º_en_su_carretera=K` en la línea), y **todas** las de raíces tipo 5 y unidades de carros. Las líneas de partida,
   `[PARADA]`, `[PARADA de carros]` y retirada FALLIDA no se muestrean (sus cupos de siempre). Las líneas
   `Session2 observer hold: held/released` por actor ya tienen cupo propio de 48 cada una por proceso: sin cambio.
2. **Paridad** (cada 60 s, tras `[resumen vivos]`): `User2Road [resumen paridad] ventana_s=N | invitado 0x1F5 t6:
   altas/min=81.2 vida_mediana_s=29.3 (n=64) altas= bajas= (25=) sin_baja= -> frente a anfitrión 0x1F5 t6: ok | anfitrión
   0x1F5 t6: … | seguidos= (tabla llena, clases llenas, repetidos, olvidados ahora sin baja) | bajas por rama: invitado
   caducidad= distancia= dentro_de_un_ancla= desconocida= | anfitrión … | líneas por nacimiento: admitidas=
   fuera_del_muestreo= | líneas de baja: …`. Se siguen las altas de todo descriptor de carretera marcado (raíz o
   miembro) cuyo solicitante es el personaje del invitado o el del anfitrión (mid 0x2792A7B, sólo con la pieza en probe o
   1) y su Logout en el servidor (punto O); la vida mediana es la de las últimas 64 bajas nativas de cada clase (lado,
   carretera, tipo); nuestros propios Logout se olvidan sin muestra; 20 min sin baja se olvida (`sin_baja`). La clase
   del anfitrión con la que se compara es la de la misma carretera y tipo o, si no hay (caminantes 0x3FD del invitado
   frente a 0x3FE del anfitrión), la del mismo tipo con más bajas (`(mismo tipo)`). **`WRN [paridad: rotación anómala]`**
   (30 como mucho) sólo con ≥ 20 bajas del invitado en la clase y vida mediana < 10 s, o < 1/3 de la del anfitrión con
   ≥ 20 bajas suyas: eso sí sería anómalo (p. ej. un ancla 2 perdida).
3. **Posición en la baja** (punto O, hilo del servidor, lecturas protegidas, sin CRT): `User2Road [viajero: baja]
   invitado|anfitrión key=0x… tipo= carretera=0x… motivo= vida_s= | servidor=(x, y, z) d(user 2)= m d(anfitrión)= m
   radio(+0x468)= m caducidad(+0x428)= ahora(_time64)= (resta N s) -> rama=caducidad (+0x428) | distancia (fuera del
   radio de las dos anclas) | dentro del radio de un ancla (otra rama) | desconocida`; muestreada por lado y carretera
   (las primeras 30, luego 1 de cada 20), contada siempre en el resumen. Posición: TransformSync del actor en el servidor
   (lo que mide 0x28F9A20); radio `[[actor+0x68]+0x20]+0x468` (float); caducidad `+0x428` (i64) frente a `_time64`
   (0x2C37340:340-343: `!= 0 && < ahora` → Logout motivo 25). Distancias 3D (0x28F9A20 usa d² con y). Un actor con caja
   orientada no usa el radio: su rama sale por el radio igualmente (diagnóstico). Separa la rama de distancia de la de
   caducidad, que el informe §2.2 dejó en [H].

Qué mirar en la próxima prueba: `[resumen paridad]` del invitado 0x1F5 ≈ el del anfitrión (sin `WRN`); `bajas por rama`
del invitado: si domina `distancia`, `d(user 2)` ≥ radio en las líneas; si domina `caducidad`, la IA escribe +0x428 al
terminar el vuelo. Pruebas puras: `churn_hygiene` en `optd_user2_road` (muestreo y desbordamiento de ranuras, altas /
bajas / mediana / ventana / contraparte / tabla llena / olvido / borrado con desplazamiento, veredicto, rama).
