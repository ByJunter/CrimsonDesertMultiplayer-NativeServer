# S40 carros: los caballos con jinete y los carros del invitado los controla el invitado (`user2_road_carts`)

Fecha: 15-10-2026. Rama `opcion-d-carros` (worktree `C:\Emulador CD-carros`), sobre `ef5a6b5` (kit 37: fases A, E, B, C
y D activas, C sin carros). Build del juego **1.0.0.2976**, timestamp **0x6AB28F00**, base **0x140000000**: todo RVA de
este documento vale sólo para esa build. **No se ha ejecutado el juego para esta entrega** (las dos copias las prueba el
usuario): sólo `scripts/build.ps1` con sus pruebas y la lectura de los logs ya guardados.

## 1. El problema

Con la fase C (`user2_road_control=1`, [s38-fase-c.md](s38-fase-c.md)) los caminantes y las bandadas del invitado se
asignan al invitado y se mueven aunque el anfitrión esté lejos (noche 8: todos los vivos desplazados ≥ 1 m). Los
**caballos con jinete y los carros** siguen quietos: la revisión de la fase C (§3.2, §3.3) los dejó fuera del modo 1
(`user2_road_carts=0`) por tres riesgos: (1) la carga de una carreta (un `InstantGimmick` con muchos hijos tipo 7) partía
la unidad y en el modo 1 eso es un bucle nace → se retira → nace; (2) la capacidad de la unidad; (3) un jugador montado
en un caballo o carro de la caravana nunca debe recibir `Logout(25)`. Siguen con el anfitrión como controlador y el
cliente del anfitrión, lejos, no los mueve (la misma causa que los caminantes: [3-viajeros.md](s34-investigaciones/3-viajeros.md)).

## 2. Composición medida (logs + Ghidra)

Logs (`C:\Emulador CD\backups\`): `p8-2inst-noche7-faseCprobe2\cdmp.host-Cprobe2.log` (C probe, carros incluidos en la
medida), `p8-2inst-noche3\cdmp.host-probeB3.log` (fase B probe 3, la carreta grande), `p8-2inst-noche8-faseBC1` y
`p8-2inst-noche9-todo` (C=1, carros fuera).

### 2.1 Caballo con jinete (carretera `0x7D1`, «clases 3»)

`0xB01002BD`, `0xB01002D2`, `0xB01002D4`, `0xB01002D6` (C probe 2): **raíz tipo 5** (el caballo, descriptor de carretera
marcado `+0x3CC=1 +0x3CD=1`) + **jinete tipo 3** (acompañante marcado, word `0x100`, envoltorio `_Rider` 0x5B04E60 de
`0x100E63B0`). El jinete ya está en el vector `+0x2F8` de la raíz cuando ésta pasa por el mid (`nodos_preexamen=1`).
Nada más: `[viajero unidad] … registros=2 … t3=1 t5=1 estado=completa`. Sin carga ni vehículo enlazado.

### 2.2 Carro (carretera `0xFA1`, «clases 4»)

`0xB01002D8` (C probe 2, 12 registros) y `0xB01002A6` (fase B probe 3, 25 registros). Por `+0x2F0` (y las altas de
`NpcProbe [anfitrion familia]`):

| Clave | Tipo | Qué es | Descriptor / envoltorio | En el mid de la raíz |
|---|---|---|---|---|
| `D8` | 5 | raíz: el vehículo que monta el conductor (`MountSync MountBegin rider 0xB01002D9 horse 0xB01002D8 seat 0`) | base marcado, `sub 0x101` | — |
| `D9` | 3 | conductor | base marcado `0x100`, `_Rider` | **sí** (preexamen) |
| `DA` | 5 | **vehículo enlazado** | base **sin marca** (`sub 0x0`), solicitante y carretera copiados, `_LinkedVehicle` 0x5B04D80 | no (tardío) |
| `DB` | 7 | gimmick (la carga), 5 hijos | `InstantGimmick` 0x5B04C68 | no (tardío) |
| `DC`, `DE`..`E1` | 7 | carga (hijos de `DB`) | base sin marca, sin solicitante | no (tardíos) |
| `DD` | 7 | hija de `DC` (profundidad 3) | base sin marca | no |
| `E2`, `E3` | 7 | gimmicks de la raíz | `InstantGimmick` | no |

`0xB01002A6`: igual, con `A8` (vehículo enlazado), el gimmick `A9` con **19** de carga (`AA`..`BC`) y `BD`, `BE`.

**El vehículo enlazado (Ghidra `0x2797510`, exports `0x2797510.c`).** Sólo si la clase del descriptor padre (`+0x68` de
sus datos) es 5; un bucle de 2 (ranuras `+0x82`/`+0x84` de los datos del vehículo; `-1` = ninguna): descriptor de `0x4E8`
con el constructor base `0x278EA70`, `desc+0x490 = [actor padre+0x60]` (la **clave del actor padre**: la función recibe
el actor ya creado), los campos de carretera copiados de la información de aparición del actor padre
(`[[actor+0x68]+0x20]+0x278..+0x2C8` → `desc+0x3C8..+0x418`, sólo si `+0x278` y `+0x280`/`+0x298` no son 0) y **word
`+0x3CC = 0`** (sin marca); `+0x3B8` no se escribe. Envoltorio de `0x18` B: vtable `_LinkedVehicle` 0x5B04D80, `inner`,
ranura u8 en `+0x10`; `[inner+0x2F0] = descriptor padre` y `push_back(padre+0x2F8)` con `0x43DB70`. Lo crea la creación
del actor padre: **nace siempre después del mid de su padre** (tardío). `0x27EA160` sólo construye la raíz (`:1447`) y
los acompañantes (`:1675`); ni el vehículo enlazado ni los gimmicks salen de ahí.

### 2.3 Qué regla de la fase C deja hoy cada parte con el anfitrión

| Parte | `user2_road_carts=0` con `user2_road_control=1` (noches 8 y 9) | Con probe (C probe 2), lo que haría el 1 de la fase C |
|---|---|---|
| Raíz tipo 5 | `Verdict::CartRoot` («raíz tipo 5 fuera del modo 1»): 4 por sesión | se asignaría |
| Jinete / conductor | su raíz no abrió unidad: `[viajero sin unidad] … su padre no es un registro` (4 t3) | se asignaría (miembro del preexamen) |
| Vehículo enlazado | sin unidad abierta: sólo cuenta como «sin marca»; `paridad 1:1 … SIN registro t5=5` = 4 raíces + 1 enlazado | **parte la unidad**: `road_foreign_child`, «no es un viajero del mismo solicitante» |
| Gimmicks (`InstantGimmick`) | nativo (anfitrión) | host-kept (no parten) |
| Carga del gimmick | nativo (anfitrión) | **parte la unidad** (6 `[viajero partido]` tipo 7, descriptor base) |

Es decir: en la fase C los caballos con jinete ya formaban unidades completas; los carros se partían **siempre** (7
`[viajero partido]` por carreta) y en el modo 1 habrían entrado en el bucle nace/retira hasta que `SplitGuard` parara la
clase `0xFA1`.

## 3. Diseño

### 3.1 Clave

`[session2] user2_road_carts = 0 | probe | 1` (anfitrión; sólo tiene efecto con `user2_road_control=1`; por defecto `0`):

| Valor | Raíces tipo 5 en el modo 1 |
|---|---|
| `0` | Como antes: `CartRoot`, con el anfitrión |
| `probe` | Su unidad se abre y se mide **entera** con las reglas nuevas («se asignaría»: ningún controlador escrito, nunca se retira, su bucle de partidas va a un contador aparte que nunca para la pieza) mientras caminantes y bandadas se siguen asignando |
| `1` | Se asignan enteras al invitado |

Con `user2_road_control=probe` todo es probe (carros incluidos, con las reglas nuevas).

### 3.2 La unidad entera de un tipo 5

- **Raíz** tipo 5 (`road::root_writes`): escribe sólo con `user2_road_carts=1`. Además de todo el veredicto de la fase C,
  `Verdict::PlayerRides`: nunca se asigna un vehículo que un jugador ya monta (§3.4).
- **Jinete / conductor**: miembro marcado `_Rider` del preexamen, como en la fase C (sin cambios).
- **Vehículo enlazado** (`road::child_verdict`, `ChildKind::Linked`): un hijo sin marca cuyo `+0x2F0` es un registro de la
  unidad se acepta como miembro **sólo** con el contrato entero de `0x2797510` para ESE padre: envoltorio `_LinkedVehicle`
  en el vector `+0x2F8` del padre, padre tipo 5, `+0x3CD == 0`, `+0x3C8 ==` el solicitante de la unidad, `+0x3D0 ==` la
  carretera de la raíz, `+0x490 ==` la clave del actor padre, tipo 5, ningún jugador lo monta; y la cola común de un
  miembro (`road::assignable`, extraída de `member_verdict`): padre vivo con el controlador de la unidad, actor sin
  controlador ni dueño (`+0x90`, `+0xA0`), `desc+0x40` a 0, campo y rama del anfitrión, la clave del usuario 2 y registro
  libre. Si algo falla, la unidad se parte (como cualquier hijo ajeno). Toma el controlador de su unidad en el mismo mid
  (`mov [r14+0x90],eax`). **Por qué con la unidad y no host-kept**: lleva IA (el cliente del anfitrión, su controlador
  hoy, envió `2734` para él igual que para la raíz y el conductor) y está físicamente enlazado a la raíz; con otro
  controlador se quedaría quieto lejos del anfitrión mientras la raíz se mueve.
- **Gimmicks** (`InstantGimmick` tipo 7) **y su carga** (`ChildKind::Cargo`: hijo tipo 7, descriptor base o
  `InstantGimmick`, de un registro host-kept, a cualquier profundidad): **con el anfitrión, enteros**. Se registran en la
  unidad (host-kept) para retirarse con ella de hojas a raíz: la carga antes que su gimmick, el gimmick antes que la raíz
  (nunca un padre retirado con un hijo vivo). Un hijo de un gimmick que no sea tipo 7 parte la unidad
  (`Split::CargoType`). **Por qué host-kept y no asignados** (decidido con datos): los tipo 7 no llevan IA (S34, I3 §2.1:
  el cliente que no los controla sólo envía `2556` de posición), y los caminantes con gimmicks host-kept se movieron
  todos con la fase C (noche 8: 24 host-kept registrados, todos los vivos desplazados). Si la prueba muestra la carga
  quieta o separada del carro, el siguiente paso sería asignarla con la unidad (mismo registro, otra rama del veredicto).
- Un `_LinkedVehicle` ya presente en el vector al nacer la raíz (no ocurre en la build 2976) sigue rechazando la unidad
  (`envoltorio no admitido`): queda con el anfitrión, seguro, y se ve en `preexamen fallido por envoltorio`.

### 3.3 Capacidad y bucle

- **48 registros por unidad** (`road::kMaxUnit`; `pack_loc` pasa a 6 bits de registro y 10 de unidad). Medido: el
  máximo es la carreta `0xB01002A6` con **25** (raíz + conductor + enlazado + gimmick + 19 de carga + 2 gimmicks); la de
  C probe 2, 12 con profundidad 3. Un hijo que no cabe deja a su padre `incompleto` (nunca se retira, terminal y visible
  en `[viajero unidad] … INCOMPLETA`), como en la revisión §3.2.
- **Bucle** (`road::SplitGuard`, por clase de carretera `+0x3D0`): cubre los carros (`0xFA1`, `0x7D1`): 2 unidades
  **asignadas** partidas de una clase en 10 min la dejan de asignar; 6 en total paran la pieza. Cambio: las partidas de
  unidades **no asignadas** (probe, carros en probe con el 1) van a otro contador (`g_road_split_would`) que sólo dice
  «pararía»: nunca bloquean ni paran las asignaciones reales.

### 3.4 Jugadores montados (revalidación)

**Sustituido por §9.1** (revisión): más fuentes y un tipo 5 sólo se retira con prueba de que nadie lo monta.

`road_player_rides(clave)`: el padre serializado (`+0xAC`, jerarquía como respaldo; `child_hold_actor_parent`, lecturas
protegidas sin cerrojos) del personaje del anfitrión o del invitado es esa clave, **o** `mount_sync` la tiene como la
montura de uno de ellos (`mount_sync_rides`: el par armado de este PC o la última transacción montada del otro jugador;
atómicos, cualquier hilo). Se usa:
- **antes de asignar** una raíz tipo 5 (`Verdict::PlayerRides`) y un vehículo enlazado (`Split::PlayerRides`);
- **antes de cada `Logout`** (como la revisión §3.3): `Call::Occupied` → nunca `Logout`, reintento cada segundo,
  terminal a los ~120 s. La retirada va de hojas a raíz, así que un jugador montado en el vehículo enlazado bloquea
  también a la raíz (espera), y un conductor NPC sale antes que su caballo.

### 3.5 `mount_sync` y doble control

- `mount_sync` y `ride_filter` sólo actúan sobre claves de personaje (`0xA…`, `publishable`: `rider == local_key`): un
  jinete NPC (`0xB…`) sobre un caballo NPC no publica ni retiene nada. Hoy el cliente del anfitrión hace el `vf53` del
  conductor NPC como controlador (`MountSync MountBegin … rider 0xB01002D9 horse 0xB01002D8 … client`); con el carro
  asignado lo hará el cliente del invitado y el del anfitrión lo verá como observador (lo nativo, simétrico a como el
  invitado ve hoy los carros del anfitrión).
- Doble control: la parada de la fase C (`Req de controlador 2223/2734/2291/2405/2828` del cliente del anfitrión para
  un viajero asignado) cubre la raíz, el jinete y el vehículo enlazado. Los gimmicks y la carga siguen siendo del
  anfitrión: su `2556` no cuenta.
- Al llegar el anfitrión: nada cambia de controlador (fase C §2.3); la publicación hacia el anfitrión es la de los
  caminantes (`observer_hold` por controlador, `observer_hold_family` para los hijos, `publication_gate=1`).

### 3.6 Lo que no cambia

Caminantes, bandadas, U-e, familias, la contención y la limpieza estricta de la fase C. El camino del mid sigue sin CRT
(`dumpbin /disasm` del `cdmp.asi`: `road_root`, `road_member`, `road_foreign_child`, `road_close_depth` (con el libro
por clase en línea), `road_player_rides`, `compose`, `child_verdict`: ninguna llamada a `memset`/`memcpy`/`memmove`/
`__chkstk` ni `rep stos`/`rep movs`).

## 4. Código

`src/mod/session2/user2_road_logic.h` (pura, probada): `kVtWrapLinkedVehicle`, `kDesc_ParentActorKey`, `kMaxUnit = 48`,
`Carts` / `root_writes` / `carts_allowed`, `Verdict::PlayerRides`, `Split::{LinkedMismatch, PlayerRides, CargoType}`,
`assignable`, `ChildKind` / `ChildIn` / `child_verdict`, `in_vector(…, &wrap)`, `Rec::linked`, `Composition` /
`compose`, `CaravanBook`. `user2_stages.cpp`: `road_root` (escribe según el tipo y la clave, jinete jugador),
`road_member` (líneas y contadores con la asignación real de su unidad), `road_foreign_child` (nuevo veredicto, vehículo
enlazado asignado en el mid), `road_close_depth` (composición en el censo, libro por clase, bucle real / «pararía»),
`road_player_rides`, `user2_stages_set_road_carts` (0 | probe | 1), líneas. `mount_sync.{h,cpp}`: `mount_sync_rides`.

## 5. Líneas del log (anfitrión)

- Arranque: `User2Road S40: user2_road_carts=probe|1 | con user2_road_control=1, raíces tipo 5 …`.
- `[viajero: se asignaría]` / `[viajero asignado] … papel=vehículo enlazado` para el enlazado (con su `+0x3D0`).
- `[viajero unidad] … | carretera=0x… raíz_tipo=5 jinetes=N vehículos_enlazados=N (M asignados|que se asignarían)
  gimmicks=N carga=N ajenos=N profundidad=N [INCOMPLETA …]`.
- `WRN [viajero partido] … papel=hijo no viajero … clase_hijo=vehículo enlazado|carga|gimmick|ajeno envoltorio=exe+0x…`.
- Cada 60 s, `User2Road [resumen carros] user2_road_carts=… | raíces tipo 5: asignadas= que se asignarían (probe con
  el 1)= fuera del 1= montadas por un jugador= | miembros de carros en probe con el 1: t3= t5= | hijos sin marca
  (asignado/host-kept/partido): vehículo enlazado=a/b/c gimmick=… carga=… ajeno=… | vivos en unidades de raíz tipo 5
  (raíz/jinete/vehículo enlazado): vivos= con Req del invitado= desplazados>=1m= host-kept vivos= | jugador montado: al
  nacer= vehículo enlazado= antes de Logout= | bucle (sólo probe/carros en probe): pararía|no pararía | 0x7D1 raíz t5:
  unidades= asignadas= completas= partidas=(motivos) incompletas= registros_máx= carga_máx= prof_máx= jinetes=
  vehículos_enlazados= (asignados) gimmicks= carga= ajenos= | 0xFA1 …` (una entrada por clase de carretera, hasta 8).
- `[resumen clases] … user2_road_carts=0|probe|1` (antes `carros_fuera_del_1`); `[resumen vivos]` con el 1 cuenta sólo
  los asignados (un carro en probe no se mezcla con los caminantes asignados).

## 6. Prueba en dos copias

Juego cerrado: `.\scripts\build.ps1`, `.\scripts\deploy.ps1`, copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en
`backups\`; `launch-2inst.ps1 -HostIni docs\research\ini\cdmp.2inst-host-carros-probe.ini -GuestIni
docs\research\ini\cdmp.2inst-guest.ini` ([dos-instancias.md](../dos-instancias.md)). Inis: `cdmp.2inst-host-carros-probe.ini`
y `cdmp.2inst-host-carros-1.ini` = `cdmp.2inst-host-todo-D1.ini` + `user2_road_carts=probe` / `1`. Invitado sin cambios.

### 6.1 Paso 1: `user2_road_carts=probe` (anfitrión lejos, invitado quieto junto a carreteras, 7–10 min)

Mirar en `[resumen carros]` y en `[viajero unidad]` de raíz tipo 5:
- clases `0x7D1` y `0xFA1` con `unidades` > 0 y **`partidas=0`** (o sólo motivos explicables); `completas` ≈ `unidades`;
- en `0xFA1`: `vehículos_enlazados` ≈ unidades (`que se asignarían`), `gimmicks` y `carga` > 0, `ajenos=0`;
  `hijos sin marca … vehículo enlazado=a/0/0` (ninguno partido), `carga=0/b/0`;
- `registros_máx` ≤ ~30 y `incompletas=0`; `bucle … no pararía`;
- `paridad 1:1 … SIN registro t5=0` (antes 5) y `t3=0`;
- los caminantes siguen asignados y moviéndose como en la noche 9 (`[resumen vivos]` del 1 sin cambios);
- ningún `ERR`, `PARADA`, `[2005 discrepante]`;
- **`[resumen montura]`** (§9.1): `asientos del vehículo al nacer su jinete … legibles` > 0 e **`ILEGIBLES=0`**, y el
  vínculo de montura del servidor `legible` para el anfitrión y el invitado. Prueba de la fuente: que cada uno monte un
  momento su propio caballo y mirar la siguiente línea (debe decir `vehículo 0x…` = su caballo; a pie, `0x00000000`). Si
  algo sale `ILEGIBLE`, **no pasar al paso 2**: con el 1, los tipo 5 nunca se retirarían (`tipo 5 sin prueba` → FALLIDA
  tras ~120 s, visible y sin cierre, pero los carros quedarían con el usuario 2).

Si aparece `[viajero partido] … clase_hijo=…` en un carro: su motivo y su envoltorio dicen qué parte del contrato no se
cumple; no pasar al paso 2 con esa clase.

### 6.2 Paso 2: `user2_road_carts=1` (mismo guion)

Criterios para dar por bueno:
- **Movimiento**: `[resumen carros] vivos en unidades de raíz tipo 5 … desplazados>=1m` > 0 en raíz y jinete (y en
  vehículo enlazado si hay carros), con `con Req del invitado` ≈ vivos; visualmente, en la copia del invitado los
  caballos con jinete y los carros **avanzan ≥ 1 m** con el anfitrión lejos, el carro con su vehículo enlazado y su carga
  juntos.
- **0 bucle**: `partidas=0` en `0x7D1`/`0xFA1`, ningún `[viajeros: bucle de partidas]`, ningún carro que desaparece y
  reaparece.
- **0 `Logout` de un actor montado**: `[resumen retirada] montado por un jugador=` sólo crece si alguien monta, y nunca
  con un `Logout` después; `jugador montado: al nacer=0` normalmente.
- **0 `ERR` / `PARADA`**, `2005 … OTRO=0`, `restore0=0`, `doble_control=0`, ningún `[Req del cliente del anfitrión]`
  para claves de carros; en el log del anfitrión, ningún `MountSync MountBegin … rider 0xB…` para un conductor asignado
  después de su nacimiento (sería el cliente del anfitrión actuando como controlador).
- **El anfitrión se acerca**: ve llegar los carros ya en marcha, con carga, sin duplicados (`vistos por el cliente del
  anfitrión` crece).
- **Viaje rápido / salida del invitado**: `[viajero retirado]` con `fallidos=0 sin_confirmar=0`, `pendiente=false`; la
  carga se retira antes que su gimmick y éste antes que el carro (ningún `FALLIDA`).
- **Jugador montado** (opcional, el más arriesgado): el anfitrión monta un caballo de caravana asignado y el invitado
  hace viaje rápido: `un jugador lo monta` en `[resumen limpieza]` crece, el caballo **no** se retira mientras lo monta, y
  ningún cierre; `[resumen montura] … lo monta un jugador por … vínculo=` o `asiento=` > 0, y `[PARADA de carros] motivo=doble
  control` (sólo carros: los caminantes siguen).
- **El invitado montado en su propio caballo / carro de caravana** (revisión §4, el caso de riesgo): el invitado se sube
  a un caballo o carro de SU caravana asignada y, montado, (a) hace viaje rápido y, en otra ronda, (b) sale al título.
  Esperado: ese vehículo **no** recibe `Logout` mientras lo monta (`[resumen limpieza] un jugador lo monta` crece;
  `[resumen montura] … vínculo=` o `asiento=` > 0), el resto de su unidad (el conductor NPC, hojas) sí; ningún cierre de
  ninguna copia. Si `tipo 5 sin prueba` crece en vez de `vínculo`/`asiento`, la fuente no se lee: anotar y volver a
  `user2_road_carts=0`.

**Aborto** (parar, desinstalar y devolver los dos logs): cualquier cierre; `PARADA`; `[viajero: retirada FALLIDA]`;
`[viajero partido]` de carros repetido; carros que parpadean; un carro separado de su carga o de su vehículo enlazado.

## 7. Pruebas puras (`tests/optd_user2_road_tests.cpp`, `carts_s40`)

`root_writes` / `carts_allowed`; `CartRoot` y `PlayerRides` (sólo tipo 5); `child_verdict` (carga base / gimmick,
`CargoType`, gimmick tipo 5, ajeno con y sin vínculo, `_Simple` de tipo 5 ajeno, el vehículo enlazado con cada condición
del contrato y de la cola común); `in_vector` devuelve `_LinkedVehicle` y `scan_unit` rechaza uno ya presente; **la
carreta medida `0xB01002D8`** por la tabla en el orden de nacimiento (asignada y probe): 12 registros, completa, 1 jinete,
1 enlazado, 3 gimmicks, 6 de carga, profundidad 3, el libro por clase, la retirada (la carga antes que el gimmick, el
gimmick antes que la raíz) y el jugador montado; **la carreta de 25** (19 de carga) cabe y la carga sin sitio deja el
gimmick terminal; el caballo con jinete `0x7D1` y un hijo ajeno que lo parte (y se retira); el bucle por clase con el
contador «pararía» separado; el libro con más de 8 clases. `pack_loc` de 48 registros. Revisión (`carts_review`):
`read_seats` / `read_char_link` sobre una arena con el componente de vehículo (puntero de vuelta, recuento imposible,
conductor NPC, reserva del invitado), `ride_check` (cada fuente, prueba doble, nadie conectado, tipos no 5),
`RideUnknown` acotado, `KeptChanged` terminal con su padre bloqueado, y `child_verdict` con carros a 0 = la regla de
`ef5a6b5`. Resultado: `build.ps1` 73/75 (las
dos que fallan, `launch_2inst_script` y `host_save_restore_script`, se niegan porque el juego estaba abierto: «REFUSED:
CrimsonDesert is already running» / «the game is running»).

## 8. Lo que queda sin medir

- [NV] La IA del caballo y del carro con el invitado como controlador (lo decide el paso 2).
- [NV] La carga (host-kept) siguiendo al carro asignado: los gimmicks de los caminantes sí; la carga de una carreta es
  más profunda (profundidad 2–3). Si queda atrás, asignarla con la unidad.
- [H] La forma del componente de vehículo en el actor del SERVIDOR (asientos y vínculo de montura, §9.1): validada en
  cada lectura por el puntero de vuelta y medida por `[resumen montura]`; lo que no se lee es «desconocido», nunca
  «libre».
- Un `_LinkedVehicle` en el preexamen (rechazo seguro) o un hijo propio del vehículo enlazado: no vistos.

## 9. Revisión independiente de `fda4c62` y correcciones

Revisión de sólo lectura `build/research/carros-revision.md` (no versionada): nada de severidad crash en `0` ni en
`probe`; un crash sin verificar con `1` (el invitado montado en su carro al empezar su carga / Logout) y varios puntos
menores. Todo corregido aquí:

### 9.1 Jugador montado antes del Logout (§4, crash sin verificar con `user2_road_carts=1`)

Antes, para el invitado sólo quedaba `+0xAC` [H] (`mount_sync_rides` miraba el caballo de la transacción en curso, que
vuelve a 0 tras su `Commit`). Ahora (`road_ride`, `road::ride_check`, puras y probadas) una clave **monta** si lo dice
cualquiera de estas fuentes:
1. el padre serializado `+0xAC` de un personaje (como antes);
2. `mount_sync`: el par armado de este PC (`g_ctl_arm`) o **el padre de la última `StartActionAck` del otro jugador**
   (`g_remote_parent`, guardado en `on_dispatch`, donde ya se leía `ack.parent`; 0 a pie; se borra en `mount_sync_reset`);
3. **el vínculo de montura de cada personaje en el servidor** (`road::read_char_link`: `[[actor+0x68]+0x48]` con
   `[vc+0x08] == actor`, vínculo `[vc+0x18]`, vehículo `+0x10`, reservado `+0x08`: la forma que `mount_sync` lee en el
   cliente);
4. **los asientos y reservas del propio vehículo** (`road::read_seats`: `vc+0x48` / `vc+0x88`, recuento `vc+0xC8`
   ≤ 16, con el mismo puntero de vuelta): cualquier clave de personaje (`0xA…`).

Y un **tipo 5 sólo se retira con prueba doble**: los asientos del vehículo **y** los vínculos de los personajes presentes
leídos y validados, sin nombrarlo. Si falta cualquiera: `Call::RideUnknown` («tipo 5 sin prueba de que nadie lo monta»):
nunca `Logout`, reintento cada segundo como `Occupied`, terminal visible (FALLIDA) a los ~120 s. Nunca se supone libre.
Sólo con `user2_road_carts != 0` (con 0 los tipo 5 de las unidades de caminantes siguen la regla de antes). La
retirada va de hojas a raíz, así que el conductor NPC sale antes y su asiento deja de contar (sólo cuentan claves 0xA).

Medida (antes de pasar al 1): `User2Road [resumen montura]` cada 60 s, con el vínculo de cada personaje **ahora** (su
vehículo), si los asientos del vehículo se leen al nacer el jinete de un tipo 5 (`legibles` / `ILEGIBLES` / `con un
personaje`; sólo con `user2_road_carts != 0`), y antes de Logout: asientos legibles / ilegibles, quién lo monta por
fuente, `tipo 5 sin prueba`.

### 9.2 `user2_road_carts=0` exactamente como `ef5a6b5` (§1)

- `child_verdict` con `carts_rules=false` (carros a 0) es la regla de la fase C: un `InstantGimmick` tipo 7 host-kept y
  todo lo demás (la carga de un gimmick, el `_LinkedVehicle` de un miembro tipo 5) parte la unidad; sin lecturas nuevas en
  el mid (`maybe_linked` exige carros != 0).
- La parada y el bucle de una raíz se calculan con el modo (como antes), no con la escritura de esa raíz; las líneas de
  descarte llevan el modo.
- `KeptChanged`, `RideUnknown`, la sonda de asientos y el padre incompleto por hijos tardíos (§9.4) sólo con carros != 0.
- Lo único que cambia con 0: más fuentes **positivas** de «lo monta un jugador» antes de un Logout (estrictamente más
  seguro) y las líneas nuevas.

### 9.3 Libro por clase (§8)

Las clases de raíz tipo 5 tienen su propio libro (`g_road_book_carts`, 8 clases) y salen primero en `[resumen carros]`;
las de caminantes y bandadas no les quitan sitio.

### 9.4 Carga y retirada (§3)

- **`KeptChanged`**: un host-kept (gimmick / carga) que sigue en el SAM con su clave pero con otro controlador o dueño ya
  no cuenta como «fuera» (antes `NotOurs` → `Gone` desbloqueaba a su padre): queda terminal (FALLIDA) y su padre nunca se
  retira con él vivo. Tras NUESTRO Logout, `NotOurs` sigue siendo otra encarnación (fuera). Contado en `[resumen
  montura] host-kept vivo con otro controlador`.
- **Hijos nacidos fuera del Troc** (carga tardía de un gimmick, vista sólo por la jerarquía en la publicación): en una
  unidad **asignada de raíz tipo 5**, su padre queda `incompleto` (nunca se retira con él vivo; terminal y visible).
  Contado en `hijos tardíos de carros (padre sin retirar)`. Los caminantes siguen con la regla R4 de la fase B.

### 9.5 `__chkstk` en la limpieza (§8)

`RoadSnap` (~5 KB con 48 registros) sale de la pila: una copia estática usada sólo bajo la barrera `g_p_draining`
(exclusiva y nunca reentrante). `dumpbin /disasm`: `road_cleanup_run` pasa de un marco `0x1660` con `__chkstk` a `0x250`
sin él; `road_request` `0xE00`, `road_native`, `road_add_held`, `road_ride`, `read_seats`, `read_char_link`,
`road_root`, `road_member`, `road_foreign_child`, `road_close_depth`, `road_control_impl`, `road_family_observed`,
`road_check_2005`, `road_on_req` y `mount_sync_rides`: ninguna llamada a `memset`/`memcpy`/`memmove`/`__chkstk` ni
`rep stos`/`rep movs`.

### 9.6 Paradas acotadas a los carros (§4 minor, §6)

Un doble control (p. ej. el anfitrión monta un caballo de caravana asignado), un 2005 discrepante, un restore 0 al
anfitrión o el bucle de partidas de una unidad **de raíz tipo 5** ya no paran toda la pieza: `ERR User2Road [PARADA de
carros] motivo=…`: no se asignan raíces tipo 5 nuevas; caminantes y bandadas siguen. Las unidades de carros asignadas
tienen su propio contador de bucle (`g_road_split_carts`). Como con la parada global, nada cambia un controlador ya
escrito. Sigue siendo criterio de aborto del paso 2.

## Prueba en vivo con el usuario (15-10, dos copias en un PC)

| Paso | Resultado | Logs (`C:\Emulador CD\backups\`) |
|---|---|---|
| `carros-probe`, invitado junto a un camino | 3 jinetes (0x7D1) y 1 carro (0xFA1) completos: carro + conductor + vehículo enlazado + 3 gimmicks con 5 de carga; 0 partidas, 0 incompletas; asientos legibles 4, ILEGIBLES 0; 0 ERR/PARADA | `p8-2inst-carros-probe3` |
| `carros-1` | 7 raíces tipo 5 asignadas (6 jinetes, 1 carro), todas completas; vivos 2/2/1 (raíz/jinete/vehículo enlazado) **con Req del invitado y desplazados ≥ 1 m**; carga 19 piezas host-kept; 0 partidas, 0 restore 0, 0 doble control, 0 ERR/PARADA. El usuario vio un caballo y un caballo con carreta moverse bien, y la carreta lleva la carga sin problema | `p8-2inst-carros-1` |

Pendiente: un jugador montado en un caballo o carro de caravana y luego viaje rápido o salida (§6.2).
