# S36 fase B: familias NPC enteras (`npc_family_control`)

Fecha: 14-10-2026. Rama `opcion-d-s36`, sobre `opcion-d-s35` (fase A y fase E ya incluidas). Build del juego
**1.0.0.2976**, timestamp **0x6AB28F00**, base **0x140000000**: todo RVA de este documento vale sólo para esa build.
**No se ha ejecutado el juego, ni el peer, ni Shadow para esta entrega**: sólo `scripts/build.ps1` con sus pruebas
(**69/69**: las 68 anteriores + `optd_npc_family`).

Implementa el punto 3 del orden de la auditoría ([AUDITORIA.md](s34-investigaciones/AUDITORIA.md) §8: «sondas y registro
completo de familias … activar sólo una cohorte medida de 3/6; tipo 7 según capacidad/dependencia demostrada») con los
«Cambios obligatorios» de §2 y las condiciones transversales de §6. El dictamen de detalle es
[AUDITORIA-2-FAMILIAS](s34-investigaciones/datos/auditoria-codex-20261014/a2-familias/AUDITORIA-2-FAMILIAS.md) (citado
abajo como **A2-n**, su lista «Cambios obligatorios al diseño»); la investigación original es
[2-familias-npc.md](s34-investigaciones/2-familias-npc.md) (**I2**). La fase A ([s35-fase-a.md](s35-fase-a.md)) es requisito:
evidencia H1b, contención con revalidación, degradación y `publication_gate`.

Todo lo nuevo va detrás de `[session2] npc_family_control = 0 | probe | 1`; **`0` es exactamente la fase A** (ningún
camino nuevo se ejecuta: el mid vuelve en la primera comprobación, `parent_birth` nunca se fija y `family_into` une como
antes; prueba `probe_changes_nothing`). `probe` mide y **no cambia ningún controlador** (U-e decide como en la fase A).

## 1. El problema y la invariante

S34 (AUDITORIA.md §1 y §2): una raíz tipo 3 dada al usuario 2 por U-e con hijos tipo 6 (perros, cabras, ovejas) que
nacían con el anfitrión. El anfitrión retenía esos hijos para el invitado, los soltaba con restore 0 o como huérfanos, la
raíz esperaba a sus hijos para arrancar (4/4 casos) y el invitado recibía actores incompletos: cierres `0x88F832`
(`B01004A5`, Greyhound) y `0x1831598` (`B01004BF`, cabra, consultada por jerarquía). Además `ctl_verdict` asignaba por
TIPO: un hijo tipo 3 de una raíz del anfitrión se iba al invitado (familia partida al revés).

**Invariante de la fase B (modo 1):** una unidad (raíz + todo su subárbol de descriptores) **nunca** queda repartida entre
controladores para los tipos de la cohorte. O nace entera con el usuario 2 (los tipo 7 con el anfitrión, protegidos por la
publicación segura de la fase A), o nace entera con el anfitrión. Si después de admitirla un miembro no puede seguir a su
raíz, la unidad queda marcada **partida** y se **retira entera, de hojas a raíz**; su nodo no vuelve a admitirse en esa
época/generación (renace entera con el anfitrión).

## 2. Qué cambia y por qué

Código: [`npc_family_logic.h`](../../../src/mod/session2/npc_family_logic.h) (pura, probada),
`user2_stages.cpp` (integración en el mid existente), `npc_activation_logic.h` y `user2_stages_logic.h` (reservas,
generación del padre, miembros), `config.*`, `session2.cpp`, `npc_probe_client.cpp` (el cliente acepta `1` como
«observar»).

### 2.1 Padre conocido en el mismo mid (AUDITORIA.md §2 tabla; A2-«Direcciones de enlace»)

Ningún gancho nuevo: el `create_mid` 0x2792A7B de `docked_gimmicks` (único dueño, `ctl_write_mid` →
`user2_stages_npc_control`). En él `rsi` = descriptor **interior**, `r13` = campo, `r14` = actor. Contrato verificado contra
los exports de Ghidra de la build 2976 (`D:\RE\CrimsonDesert_2976\exports\decomp\`):

| Hecho | Fuente |
|---|---|
| Constructor base `0x278EA70`: `+0x2F0` = 0 (`param_1[0x5e]`), vector `+0x2F8` {`T* data`, `u32 count` `+0x300`, `u32 cap` `+0x304`} = 0 | `0x278ea70.c:132-136` |
| `0x281DBD0`: envoltorio de 0x10 B `{vtable, inner}`, `[inner+0x2F0] = descriptor padre`, `push_back(padre+0x2F8, envoltorio)` con `0x43DB70` | `0x281dbd0.c:104-121` |
| `0x2820D80` (grupo nativo): mismo par de escrituras | `0x2820d80.c:107-123` |
| `0x43DB70`: vector de **punteros de 8 B** (`count` en `+8`, `cap` en `+0xC` del vector) | `0x43db70.c` |
| `0x281CEE0` construye el descriptor y llama a `0x281D970` y `0x281DBD0` con ese mismo descriptor: el subárbol estático está completo antes de que `ServerField::vf17 0x2AF9AE0` cree el lote | `0x281cee0.c:385-396`, `0x2af9ae0.c` |
| `0x2791CD0`: selector `0x2792A11..0x2792A7B` y escritura de `actor+0x90`; justo después `(*param_1->vf[1])(param_1, …, param_3, param_4)`: `rcx = rsi = param_1` = el descriptor | `0x2791cd0.c:625-672` y AOB de AUDITORIA.md §7 (`41 89 86 90 00 00 00 48 8B 06 4D 8B CE 4D 8B C5 48 8D 55 67 48 8B CE FF`) |
| vtables (RTTI leída del PE por la auditoría): `NPCSchedule 0x5B04CB0`, `InstantGimmick 0x5B04C68`, `_Simple 0x5B04E80`, `_CatchTarget 0x5B04E20`, `_AutoSpawnGimmick 0x5B04DA0`, `_CombinationGimmick 0x5B04D60`, `_SavedAttached 0x5B04DC0`, `_LinkedVehicle 0x5B04D80`, `_Docking 0x5B04DE0`, `_Rider 0x5B04E60`, `_NPCScheduleGroupActionMember 0x5B04E00` | `a2-familias/rtti.json` |

Clases de nodo (`npcfam::classify`): `_Simple`+`NPCSchedule` = **horario**; `_Simple`/`_AutoSpawnGimmick`/
`_CombinationGimmick` + `InstantGimmick` = **gimmick**; `_CatchTarget`, `_SavedAttached`, `_LinkedVehicle`, `_Docking` y
**`_Rider`** = **excluidos**; cualquier otra vtable = desconocida. Un excluido o desconocido deja la unidad **entera** con el
anfitrión. **`_NPCScheduleGroupActionMember`** era excluido (sin ruta demostrada); desde la prueba en dos copias 2 (§10.3)
es **horario** sólo con interior `NPCSchedule` bajo un padre `NPCSchedule` (el contrato de su constructor `0x100E70A0`);
con otro interior o padre sigue excluido.

### 2.2 Reserva de la unidad entera ANTES de cambiar EAX (A2-1; AUDITORIA.md §6.2)

En el mid de una raíz (`[desc+0x2F0] == 0` leído, no ilegible) que U-e asignaría (`ctl_verdict == Assign`):

1. **Preexamen** (`npcfam::scan_subtree`, BFS acotado): hasta 63 descendientes, 64 hijos directos, profundidad 4. Por nodo
   se leen el envoltorio, su vtable, el interior, su vtable, `[interior+0x2F0] == su padre` (coherencia) y
   `[interior+0x40] == 0` (ningún usuario explícito). Fallos: ilegible, demasiado grande/profundo, incoherente, `desc+0x40`,
   ciclo, excluido, desconocido → `ScanFailed`.
2. **Admisión** (`npcfam::admit_unit` / `reserve_unit`), por orden: preexamen; sin hijos (`Childless`: la raíz sigue la
   regla de U-e de siempre); hijo gimmick sin `publication_gate=1`; su nodo ya partió una unidad
   (`NodeSplitBefore`); un descendiente ya nació antes que la raíz (`ChildBornFirst`); marco; sitio en el marco
   (`1 + n` registros); **reserva** de `1 + horario (+ gimmick si 7 está en la cohorte)` claves en `g_ctl_keys` y de `1 + n`
   registros en `g_activation`. Si una capacidad falla se devuelven las anteriores y **nada** se asigna.
3. Modo 1, unidad no admitida: la raíz **tampoco** se asigna (veredicto nuevo `Ctl::Family`, «family unit not admitted»);
   toda la unidad nace con el anfitrión. Coherente aunque conserve el bucle de S31 para esa unidad (A2-«Pruebas» 7).
4. Las altas reservadas nunca fallan por capacidad: `KeyTable` admite altas con reserva hasta 7/8 (sin reserva, 3/4 menos lo
   reservado); `npcact::Table` igual con `max_count`. Lo no usado se devuelve al cerrar el marco. Contraejemplo «63 + 2»
   (`contraejemplos_diseno.json`, `capacidad_no_reservada`): rechazado entero (prueba `reservation_63_plus_2`).

### 2.3 Pila de marcos por Troc anidado (A2-2)

`TrocMark` (por hilo) lleva ahora `npcfam::Stack`: un **id de Troc** por profundidad (contador global en
`user2_stages_troc_begin`) y el índice de su marco en un conjunto **compartido** de 8 marcos (`npcfam::FramePool`, ~100 KB
en total; no TLS, para no multiplicarlo por cada hilo del juego). Un Troc anidado abre su propio marco; el exterior sigue
intacto y la búsqueda de un descriptor recorre la pila entera (un hijo de A nacido dentro de B encuentra a A). Al terminar
el Troc (`user2_stages_troc_end`) su marco se **cierra**: censo, reservas sin usar devueltas, marco liberado; ningún puntero
sobrevive al Troc. Si una excepción SEH salta el destructor de `troc_detour` tampoco baja la profundidad del hilo: ese
marco y sus reservas quedan perdidos (fail-closed: con los 8 marcos ocupados nada se admite, `marcos_agotados`); el cierre
de un índice viejo en `stack_begin` sólo cubre una salida perdida a la misma profundidad. Sin marco libre o profundidad
≥ 16: `NoFrame` (no se admite). Contraejemplo
`troc_anidado`: prueba `nested_frames`.

### 2.4 Generación del padre y token de unidad (A2-3; AUDITORIA.md §6.1)

Cada miembro registrado en su mid guarda `parent_birth` (nacimiento del registro de su padre en el marco) y la unidad
(`unit_key`, `unit_birth` = clave y nacimiento de su raíz). `npcact::Table::family_into` (`npc_activation_logic.h`) une un
hijo con `parent_birth` sólo a **esa** encarnación de su padre: un superviviente de otra encarnación de la misma clave queda
fuera (contraejemplo `familia_clave_reutilizada`, prueba `parent_generation`). Un vínculo visto sólo en la publicación
(`parent_birth` 0) no inventa generación: se une por clave como en la fase A (necesario para retirar de hojas a raíz) y
nunca recibe el controlador de la unidad.

### 2.5 «CC ausente demostrado» frente a «no puedo leerlo» (A2-4; AUDITORIA.md §6.3)

La fase A ya separó los tres estados del CC (`pubg::Cc`) en la publicación, y la fase B **depende** de ella: los miembros
tipo 7 que se quedan con el anfitrión sólo se admiten con `publication_gate=1` (`GimmickNeedsGate`), que nunca publica a un
observador que no lo controla un actor con CC `Unknown` o restore 0. No se usa NoCc como argumento de seguridad. El mismo
principio rige las lecturas nuevas: `npcfam::Link` = `Unknown` (lectura fallida o puntero implausible) / `Absent` (leído y
0) / `Present`; `read_matches` y `in_vector` devuelven 1 / 0 / −1. **Ilegible nunca es raíz ni ausencia**: un descriptor con
`+0x2F0` ilegible no usa su veredicto de U-e; un miembro con vínculo, padre o controlador ilegible deja la unidad partida.

### 2.6 Raíces no asignadas, salidas por unidad, hojas a raíz, coste (A2-5, A2-6, A2-7, A2-10)

- **Raíces sin hijos** (preexamen vacío) también llevan su marca de unidad (`kFamRoot`, sin reserva): un hijo que
  aparezca después fuera de todo preexamen se reconoce (modo 1: parte la unidad; probe: se cuenta).
- **Raíces no asignadas también se registran**: una raíz con hijos en el ámbito de U-e (su Troc, armado, sin abortar) cuyo
  veredicto no es `Assign`, o cuya unidad no se admitió, deja un registro `HostRoot`: sus hijos se reconocen como de una
  unidad del anfitrión (`HostMember`) y el censo la cuenta.
- **Un descriptor con padre nunca usa su propio veredicto** (modo 1): su controlador es el de su unidad si está admitida y él
  pasa todas las comprobaciones (`npcfam::member_verdict`: vínculo releído igual al del preexamen, padre vivo
  `[padre+0x60]` con el controlador de la unidad `[padre+0x90]`, `actor+0x90 == 0`, `desc+0x40 == 0`, `actor+0xA0 == 0`,
  campo del anfitrión, `eax` del dueño, clave del usuario 2, U-e sin abortar ni en pausa, y tipo de la cohorte para su
  clase). Corrige la clasificación por tipo de `ctl_verdict` (AUDITORIA.md §2 «`not type3` …» [F]).
- **Toda salida resuelve la unidad entera**: en la raíz (capacidad, preexamen, tipo de envoltorio) se rechaza entera; en un
  miembro (tipo inesperado — p. ej. un tipo 4/5 bajo `_Simple`+`NPCSchedule` —, lectura fallida, padre cambiado, U-e
  abortado, clave) la unidad queda **partida** (`family_split` en la raíz, `npcact::containment_due` → `Retire` sin plazo;
  su nodo entra en `g_fam_split_nodes`). Un miembro nunca vence solo (`family_member`): lo juzga su raíz.
- **Hijos tardíos** (A2-1 último párrafo): un hijo cuyo padre está en el marco pero que no estaba en el preexamen sólo se
  admite si su pertenencia al vector del padre se demuestra **ahora** y caben su registro y sus reservas
  (`npcfam::late_verdict`); si no, la unidad queda partida. Un hijo de una unidad que aparece por primera vez en la
  publicación (nació fuera de su marco) con otro controlador que su raíz (salvo un tipo 7 fuera de la cohorte) parte la
  unidad. Nunca «ya vendrá».
- **Hojas a raíz en todas las salidas**: la contención ya recorría la familia al revés (padre siempre antes que hijo en
  `family_into`; prueba con `B0100473 → 474 → 475`). Nuevo: `ctl_cleanup` (drenaje, logout, su Troc de carga/2001) elige las
  64 claves **más profundas** y la más profunda primero (`npcfam::LeafFirst`; con profundidad 0 en todas es el orden de
  antes), y una **retirada parcial** explícita (`npcfam::RetireGuard`): un miembro cuya baja no se confirma bloquea a su
  padre en esa pasada (y el bloqueado al suyo); lo bloqueado cuenta como fallo acotado (`kCleanBlocked`). Nunca un padre
  retirado con un hijo suyo vivo.
- **Coste y cerrojos**: el camino de familias del mid se cuenta (llamadas, µs total/máx.) y también cada `ActivationLock`
  que toma (la reserva de la raíz, un tardío, el registro del nacimiento, que `npc_observe_birth` ya tomaba). Ninguna
  llamada al juego, ningún CRT, ninguna copia agregada grande ni `memset` (el preexamen vive en ~1 KB de pila sin
  inicializar). No se sostiene ningún cerrojo del mod durante llamadas nativas; el orden ModLock → barrera → ActivationLock
  no cambia y `child_hold` sigue fuera de `ActivationLock`.

### 2.7 Cohorte: tipos 3 y 6; tipo 7 con el anfitrión (AUDITORIA.md §2 «Tipo7», §6 «Fronteras»)

La **raíz** sigue la regla de U-e (tipo 3, `NPCSchedule`). Los **miembros** admitidos son los de `npc_family_types`
(por defecto `3,6`). Un hijo gimmick tipo 7 (InstantGimmick) de una unidad admitida se queda con el anfitrión
(**host-kept**: no se escribe nada), sólo con `publication_gate=1`. `7` en la cohorte es configurable pero **no** está en
ningún ini de este kit: su contrato (SetOwnershipState `0x27A1430`, vf7) no está demostrado.

### 2.8 Frontera de autoridad (AUDITORIA.md §6 «Fronteras de autoridad»)

Sólo cambia el valor que `0x2792A7B` escribe en `actor+0x90` (simulación de IA). **Nunca** `actor+0xA0`, `desc+0x40`, la
partida del mundo ni el gestor de misiones. `quest_policy` no se toca: el crédito sigue siendo del anfitrión (y debe
comprobarse en Shadow, §5.3). Entre PC sólo viajan claves; descriptores y actores son punteros locales al Troc.

### 2.9 `probe`: sólo medir

Mismo preexamen, mismos marcos y la misma decisión por miembro, con `can_reserve` en lugar de `reserve`, sin escribir EAX
de ningún miembro, sin marcar unidades partidas y sin cambiar la contención ni `ctl_cleanup`. El camino de U-e de la fase
A sigue intacto (incluida la asignación de hijos tipo 3 por su propio veredicto, que es lo que se mide). Única marca
adicional: la raíz que «se admitiría» lleva `fam = kFamRoot` en la tabla de activación (sin efecto en la contención) para
contar los tardíos vistos en la publicación.

## 3. Claves `[session2]` (anfitrión)

| Clave | Por defecto | Valores |
|---|---|---|
| `npc_family_control` | `0` | `0` = fase A; `probe` = mide (§2.9); `1` = §2.2–2.8 |
| `npc_family_types` | `3,6` | lista de 3, 6, 7; otra cosa → WRN y `3,6` |

`npc_family_control=1` exige `user2_stages=1`, `user2_npc_control=1`, `npc_activation=1`, `npc_action_evidence=1`,
`npc_degradation=1` y `publication_gate=1` (éste a su vez `observer_hold=1` y `create_session=1`). Si falta alguno: una
línea `WRN NpcFamily: npc_family_control=1 necesita …: queda en probe` y se mide. En el invitado (Shadow) la clave sólo
observa (`npc_probe_client`); su ini no cambia.

Inis ([`docs/research/ini/`](../ini/)): `cdmp.p8-s36-local-paso1.ini` / `-paso2.ini` (= `cdmp.p8-s35-local-paso2.ini`,
fase A activa, + `npc_family_control=probe` / `1` y `npc_family_types=3,6`), `cdmp.p8-s36-host-paso1.ini` / `-paso2.ini`
(= `cdmp.p8-s35-host-paso2.ini`, fase A activa y `host_save=probe`, + lo mismo), `cdmp.p8-s36-shadow.ini` (= la S35, byte a
byte).

## 4. Líneas del log (anfitrión)

- Arranque: `NpcFamily S36: npc_family_control=<0|probe|1> (pedido …) npc_family_types=3,6 | unidad = raíz + subárbol de
  descriptores (+0x2F0/+0x2F8) en el mid 0x2792A7B; reserva entera antes de EAX (marco de Troc 255 registros, 8 marcos),
  … tipo 7 con el anfitrión (host-kept, puerta S35); nunca actor+0xA0 ni misiones`; o el `WRN … queda en probe`.
- Por raíz con hijos, al cerrar el marco de su Troc: `NpcFamily [censo] qpc=… T… | raíz=0x… nacimiento=… modo=<probe|1>
  unidad=<admitida|se admitiría|con el anfitrión> motivo=<admitida|preexamen fallido|…|veredicto de U-e> estado=<completa|
  PARTIDA|pendiente|con el anfitrión> nodos=N nacidos=N miembros(_que_asignaría)=N host-kept=N partidos=N tardíos=N
  pendientes=N profundidad=N controlador=0x… recurso=0x… nodo=N prof_troc=N registros_perdidos=N motivos_partido=[…]`.
- Por miembro partido: `WRN NpcFamily [partida] qpc=… T… | key=0x… nacimiento=… padre=0x… raíz=0x… nacimiento_raíz=…
  tipo=N clase=<horario|gimmick|…> profundidad=N tardío=<0|1> motivo=<…> modo=<probe|1> (…)`.
- Publicación: `NpcFamily [tardío en la publicación] … key=0x… padre=0x… controlador=0x… tipo=N raíz=0x… nacimiento_raíz=…
  decisión=<vínculo distinto del mid|tardío host-kept (tipo 7)|tardío PARTIDO (otro controlador)|tardío del mismo
  controlador> modo=<probe|1>`.
- Retirada: `WRN NpcFamily [bloqueo hoja-raíz] key=0x… nacimiento=… padre=0x… raíz=0x…: un descendiente suyo no se retiró
  en esta pasada; no se llama a Logout (fallo acotado, se reintenta)`.
- Diagnóstico: `NpcFamily [miembro sin acción] key=0x… … raíz=0x… tipo=N host-kept=… desde2005_encolado_us=…: su raíz
  progresó y él no (diagnóstico; no se retira por reloj …)` (una vez por miembro, ≥ 2 plazos sin progreso).
- Cada 60 s, tras `[resumen S35]`: `NpcFamily [resumen] modo=… cohorte=3,6 | unidades: admitidas=… sin_hijos=…
  preexamen_fallido=… (…) gimmick_sin_puerta=… nodo_partido_antes=… hijo_antes_que_raíz=… sin_marco=… marco_lleno=…
  claves_llenas=… activación_llena=… raíces_del_anfitrión=… | miembros: asignados|asignaría=… host-kept=…
  de_unidad_del_anfitrión=… partidos=… (motivos) | tardíos admitidos=… partidos=… | huérfanos_al_nacer=…
  vínculo_ilegible=… descriptor_renacido=… registros_perdidos=… marcos_agotados=… | censo: completas=… partidas=…
  pendientes=… con_el_anfitrión=… | publicación: tardíos=… partidas_marcadas=… vínculo_distinto=… | retirada:
  unidades_partidas=… bloqueos_hoja_raíz=… miembros_sin_acción=… | coste_mid: llamadas=… total_us=… medio_us=… máx_us=…
  cerrojos=… | reservas vivas: claves=… activación=…`.
- En el resumen de U-e (`User2Stages summary`), el veredicto nuevo `[family unit not admitted] N`.
- Las líneas de la fase A siguen igual; la retirada de una unidad partida es la `[retirada local]` de siempre (motivo 25),
  con sus miembros host-kept retirados con su propio controlador.

## 5. Criterios de aborto (parar, `deploy.ps1 -Remove`, devolver logs)

- Cualquier cierre del juego (invitado o anfitrión); con familias en modo 1, guardar dump, log y captura de los dos lados.
- Modo 1: `estado=PARTIDA` en `[censo]` o `[partida]` más de una o dos veces en 10 min, o `partidas_marcadas` creciendo:
  el modelo de descriptores no describe esas unidades; volver a probe y mirar sus motivos.
- Cualquier 2005 a Shadow con restore 0 de un actor que Shadow no controla (como en la fase A: debe ser 0), y en
  especial de un hijo de una unidad admitida.
- `[CONTROL ABORT]` (cualquiera: 2005 con otro controlador, cliente del anfitrión pidiendo para un miembro, motivo 42 >
  10/min que repiten hueco de stage (desde la S43: un hueco con otra baja 42 en los últimos 60 s; las bajas 42 en huecos
  distintos solo se registran), restos tras drenar). Un `HostDrives` sobre un perro/cabra asignado indica que el cliente del anfitrión aún lo
  simula.
- `reservas vivas` distintas de 0 de forma sostenida entre Trocs (fuga de reservas) o `marcos_agotados` > 0.
- `coste_mid máx_us` > 2000 o `medio_us` > 200: el mid se vuelve caro; parar.
- Progreso de misiones en la partida propia del invitado, o una acción del invitado sobre una familia que no acredite la
  misión del anfitrión.
- Un animal de una unidad admitida invisible en el cliente del invitado más de unos segundos, o duplicado.

## 6. Plan de prueba

### 6.1 Un PC con `cdmp_peer` (primero)

Lo que **puede** validar: el vínculo real de descriptores en el mid (preexamen), los marcos por Troc y su cierre, las
reservas y su devolución (`reservas vivas` vuelve a 0), el censo (completas/pendientes), el registro de miembros con su
`parent_birth`, la retirada de unidad de hojas a raíz (con el peer ningún NPC arranca: en paso 2 la contención pospone por
silencio y, al acercarse/drenar, `ctl_cleanup` debe ir de hojas a raíz), el coste del mid y que nada global se dispare.
Lo que **no**: la IA de los miembros, la primera acción de una raíz con sus animales, el seguimiento/combate, ni la
visibilidad en un segundo cliente (el peer no ejecuta cliente).

1. Juego cerrado. `.\scripts\build.ps1`, `.\scripts\deploy.ps1`. Copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en
   `backups\` (U-1/U-e escriben en la memoria del juego).
2. Guion del peer como en [u0-resultados-s28.md](u0-resultados-s28.md) §8 (`build\e2e\u1_idle.troc`).
3. **Paso 1:** `bin64\cdmp.ini` = `cdmp.p8-s36-local-paso1.ini`. Juego, Hernand, peer
   (`--mode replay --script build\e2e\u1_idle.troc --var ACCOUNT=1 --var SAVE_SLOT=108 --start-after 3000 --linger 30000
   --seconds 1200`), alejarse para armar U-1, 5–10 min. Esperado: `NpcFamily S36: npc_family_control=probe`, líneas
   `[censo]` para guardias con perro, pastores, porteadores (tipo 7); `preexamen_fallido` y `vínculo_ilegible` ≈ 0;
   `hijo_antes_que_raíz` = 0; `huérfanos_al_nacer` bajo; `pendientes` ≈ 0 (los hijos nacen en el mismo Troc que su raíz);
   `reservas vivas` 0; sin cierre.
4. **Paso 2:** reiniciar con `cdmp.p8-s36-local-paso2.ini`. Esperado: `npc_family_control=1` sin WRN de requisitos,
   `unidad=admitida` y `miembros=N` en `[censo]`, `[family unit not admitted]` sólo con motivo explicable, ningún
   `[partida]`, ningún `[CONTROL ABORT]`. Volver junto al peer: el drenaje y `ctl_cleanup` retiran hijos antes que padres
   (`[retirada local]`/limpieza en orden), `reservas vivas` 0, sin `[bloqueo hoja-raíz]` salvo Logout rechazado.

### 6.2 Shadow, paso 1 (probe)

Anfitrión `cdmp.p8-s36-host-paso1.ini`, Shadow `cdmp.p8-s36-shadow.ini` ([PRUEBA-DOS-PC.md](../../PRUEBA-DOS-PC.md)). Ruta
de S34 (Hernand, guardias con perro, niña con beagle, Strangers con cabras, pastor con ovejas, porteadores/buhoneros con
Game Mechanic), anfitrión lejos y cerca. Devolver logs, `cdmp_packets_*.bin` y dumps de los dos lados por la carpeta compartida.

**Criterios para pasar al paso 2** (sobre las raíces tipo 3 con hijos tipo 6): `unidad=se admitiría` en las familias
conocidas (`B01004A4`/`B01004A5` y equivalentes), `estado=completa` en ≥ 95 % de las admisibles, `partidos` sólo con
motivos explicables (y ninguno `vínculo +0x2F0 distinto`/`ilegible`), `pendientes` ≈ 0, `tardíos en la publicación` ≈ 0,
`hijo_antes_que_raíz` = 0, `claves_llenas`/`activación_llena`/`marco_lleno` = 0, coste del mid dentro de §5; y que lo
medido coincide con los 2005 de la captura de Shadow (cada hijo tipo 6 que «asignaría» llegó con controlador anfitrión, como
en S34). Si `pendientes` o `tardíos` son frecuentes, el preexamen no ve a los hijos en su Troc y el modo 1 retiraría esas
unidades: no pasar.

### 6.3 Shadow, paso 2 (activo, cohorte 3/6)

Anfitrión `cdmp.p8-s36-host-paso2.ini`, Shadow igual. Misma ruta y además:

1. **Guardia + perro** (Hernandian Guard + Greyhound) y **niña + beagle**: nacen juntos con el usuario 2
   (`[censo] … unidad=admitida estado=completa miembros=1`); en la captura de Shadow el 2005 del perro llega con controlador
   del usuario 2 y en la misma llamada que la raíz; cero restore 0 hacia Shadow de esos hijos; cero `RemoteUpdateGuard`
   sobre ellos.
2. **Pastor + animales** (Stranger + Goat/Baby Goat; Herder + Sheep): igual, con varios hijos; los Game Mechanic tipo 7 de
   buhoneros/porteadores quedan `host-kept` y se ven en el invitado (puerta S35) sin retrasar a su raíz.
3. **Primera acción**: la raíz emite su primera Req en 1–3 s (como las familias enteras de S34), no ~10 s; `evidencia=
   aplicada` para la raíz; ninguna `retirada local` de unidades con aplicación previa.
4. **Seguimiento / combate**: el perro sigue a su guardia; acercarse con el invitado y provocar combate; ver que el animal
   actúa en el cliente del invitado sin acciones duplicadas (`combat_follow`, `remote_interp`/`remote_motion` no corrigen al
   mismo actor: AUDITORIA.md §6.6).
5. **Visibilidad en los dos clientes**: la familia entera visible en Shadow; en el cliente del anfitrión, al acercarse, la
   familia aparece entera (o se drena entera) sin huérfanos.
6. **Transiciones**: anfitrión cerca/lejos (drenaje en su preparación: hijos antes que padres, `reservas vivas` 0), viaje
   rápido del anfitrión, salida y vuelta del invitado, carga del invitado: ningún resto de la generación anterior
   (`[control leftover]` 0), reaparición nativa entera.
7. **Misiones**: una acción del invitado con una de esas familias acredita la misión del anfitrión y ninguna propia.

Éxito: todo lo anterior, ningún cierre, ningún `[partida]` sin explicación, ningún `[CONTROL ABORT]`.

## 7. Pruebas puras (`tests/optd_npc_family_tests.cpp`, `optd_npc_family`)

Sobre una arena sintética leída con la misma política `read` del mod, y con el archivo de contraejemplos de la auditoría
como fuente de los datos (`contraejemplos_diseno.json`): `types_and_reads` (cohorte, tres estados del vínculo),
`scan_trees` (profundidad 2 `B0100473→474→475`, profundidad 3 y 4, 5 rechazada, ilegible a mitad, `_Rider`, desconocido,
incoherente, `desc+0x40`, demasiado grande, ciclo, sin hijos, pertenencia de un tardío), `reservation_63_plus_2` (el diseño
literal falla; la reserva entera se rechaza sin tocar nada; segunda capacidad fallida devuelve la primera; probe no
reserva; `KeyTable` y `npcact::Table` reales; orden de motivos de admisión), `nested_frames` (el `t_fam` único pierde la
raíz; la pila no; ids por profundidad; salida perdida; conjunto lleno), `parent_generation` (superviviente de otra
encarnación fuera; hijo y nieto de esta dentro; vínculo de publicación como antes), `leaf_to_root_orders` (contención,
`LeafFirst` con tanda acotada y empates, retirada parcial con `RetireGuard`, `kCleanBlocked`), `split_and_members`
(`family_split` → retirar; miembro nunca vence solo; huérfano como siempre; sin familia = S35; `ctl_stats` no cuenta
miembros como 'unaccepted'), `member_matrix` (3/6 asignados, 4/5/ilegible partidos, tipo 7 host-kept o asignable, cada
motivo, tardíos), `census_and_frame` (completa/pendiente/partida/del anfitrión, registros perdidos),
`probe_changes_nothing`. Tres mutaciones comprobadas a mano (quitar el `parent_birth` de `family_into`, reservar sólo la
raíz, no reemplazar en `LeafFirst`): cada una hace fallar su prueba.

**No factible con las costuras actuales** (como en la fase A): `npc_control_impl`/`family_child` de extremo a extremo
(leen el sujeto de `user2_funnels`, `g_u2_char` y el campo del juego). Su lógica de decisión está extraída a
`npc_family_logic.h` y probada ahí; el pegamento se valida en §6.1.

## 8. Lo que NO se hizo (y por qué), riesgos abiertos

- **Nada medido en vivo.** Que los hijos nazcan en el mismo Troc que su raíz (A2 «[P] Padre siempre primero, mismo hilo y
  Troc») es la hipótesis de la que depende el modo 1: si no, `pendientes`/`tardíos` serán altos y el modo 1 retiraría
  unidades. El paso 1 lo decide; no activar el paso 2 sin él.
- **IA tipo 6 con el invitado como controlador: [NV]** (AUDITORIA.md §2). El diseño sólo garantiza coherencia de autoridad;
  que perros y cabras actúen bien en Shadow es la prueba §6.3.
- **Raíces tipo 6** (sin padre) siguen con el anfitrión (regla de U-e); sus hijos también (unidad del anfitrión).
- **Hijos nacidos fuera del marco**: su generación no se conoce; se unen por clave sólo para retirarlos de hojas a raíz y
  parten la unidad. Un superviviente de otra encarnación nunca visto en un mid sigue siendo indistinguible (residual de
  AUDITORIA.md §6.1; sólo afecta a la retirada, nunca a una asignación).
- **Host-kept tras un drenaje**: los tipo 7 del anfitrión no están en `g_ctl_keys`; los quita el drenaje nativo de sus
  stages, no `ctl_cleanup`. No se añadió la pasada específica de I2 §3.8.
- **Desconexión sin siguiente Troc** (AUDITORIA.md §6.4): sin cambios en esta fase; una unidad partida pendiente de retirar
  cuando el invitado ya no manda Trocs sólo se resuelve por el drenaje/limpieza existentes.
- **Unidad partida que no se puede retirar** (stage no demostrada, rechazos agotados): se abandona como cualquier unidad
  (`[contención abandonada]`); queda partida y la protege sólo la puerta de publicación de la fase A.
- **Reutilización de direcciones de descriptor dentro de un Troc**: un descriptor liberado y reasignado en el mismo Troc
  podría coincidir con un registro reservado; lo mitigan la relectura de `+0x2F0`, el padre vivo y `descriptor_renacido`,
  pero no es una prueba de identidad.
- **Vínculos no enumerados**: lectores por otra capa/jerarquía distintos de `+0x2F0/+0x2F8` (AUDITORIA.md §1, «no hay
  prueba de punto común») no los ve el preexamen; la defensa sigue siendo la publicación segura de la fase A.
- **`ctl_cleanup` con retirada parcial**: un padre bloqueado se queda asignado; si era la limpieza tras un drenaje (una vez
  por drenaje) puede disparar el aborto `LeftAfterDrain` de U-e (fail-closed, sin retirar nada a ciegas).

## 9. Revisión independiente (14-10, sólo lectura) y correcciones

| # | Hallazgo | Hecho |
|---|---|---|
| 1 (A) | La marca de unidad partida dependía de poder registrar al miembro: un tardío `Split` (p. ej. `LateNoRoom` con la tabla de activación llena) o un `npc_add_member` fallido en la publicación salían antes de `fam_mark_split_locked` | La raíz se marca **antes** del registro en `npc_observe_birth`; en la publicación la comprobación del tardío va antes de registrarlo |
| 2 (M) | Un hijo nacido fuera de su marco cuyo padre ya se ve al nacer (`child_hold_actor_parent`) entraba con `fam=0` y la publicación ya no lo veía «nuevo» | Se compara con el controlador de su raíz en su nacimiento (modo 1) y parte la unidad si difiere (salvo tipo 7 fuera de la cohorte) |
| 3 (M-B) | Raíz sin hijos asignada sin marca: un hijo posterior pasaba en silencio | Marca `kFamRoot` sin reserva (modo 1) / de probe |
| 4 (B) | Huérfano sólo apuntado si ya había marco | `fam_frame(true)` |
| 5 (M-A) | Fuga: la clave de un tardío se reservaba aunque fallara la reserva de su registro | Sólo con la reserva del registro tomada |
| 6 (B) | El cierre de marcos de «salida perdida» no cubre el caso SEH real | Documentado (§2.3) |
| 7 (M) | En probe un descriptor hijo seguía el camino de U-e y, si U-e decía `Assign`, se registraba como raíz (censos duplicados y `pendientes` falsos) | El camino de raíz exige `+0x2F0` leído a 0 |
| 8 (B) | U-e degradado a probe en la instalación dejaba las familias en 1 | Las familias bajan también a probe, con WRN |
| 9 (M) | `ctl_cleanup`: un hijo sin llamada en la pasada no bloqueaba a su padre; los filtros `before_us` / no aceptadas eran por clave | El hijo bloquea a su padre; un miembro usa la hora de alta de su raíz; las claves de una unidad no entran en la limpieza de «no aceptadas» (red antigua; inalcanzable con los requisitos del modo 1) |
| 10 (B) | Un tardío podía llevar la unidad por encima de 64 (`family_into` desbordaría y la unidad partida no se podría retirar) | `room` exige `2 + nodos + tardíos ≤ 64` |

Sin cambios de comportamiento en modo 0; en probe ningún EAX cambia. `build.ps1`: 69/69.

## 10. Segunda revisión (15-10) y correcciones

Revisión independiente 2 (sólo lectura, `98491fc`; texto completo en `build/research/s36-revision2/fase-b.md`, no
versionado). Build del juego 1.0.0.2976 (PE `0x6AB28F00`). Veredicto: modo 0 idéntico a la fase A; probe no cambia
ningún controlador; ninguna ruta de cierre nueva en el modo 1; cuatro hallazgos «wrong».

| # | Hallazgo | Cambio |
|---|---|---|
| R1 (wrong) | `ctl_cleanup`: cualquier resultado de un hijo distinto de Done/Gone bloqueaba a su padre, también `kCleanOther` («ya no es nuestro», clave marcada Removed igualmente). La limpieza tras el drenaje y la de logout corrían una vez: un miembro renacido con el anfitrión y la misma clave dejaba a su raíz asignada → 10 s después `[CONTROL ABORT] LeftAfterDrain`, un aborto global (AUDITORIA §6 «Fronteras») | `cleanup_one` dice si el `kCleanOther` es «otra clave o controlador» (leído y distinto) y `npcact::cleanup_blocks_parent` no bloquea por él (un padre nativo distinto sí: el hijo sigue con el usuario 2). Con familias a 1, lo que una pasada deja vivo (bloqueo hoja-raíz, retirada fallida, sin llamada por su reintento) lleva `u2s::kKeyCleanBlocked`: `ctl_stats` lo cuenta en `drain_blocked`, nunca en `left_after_drain` → `WRN NpcFamily [control leftover blocked]`, nunca el aborto. Tras un drenaje, si quedó algo bloqueado (o la limpieza estaba ocupada / sin SAM), hasta `kAfterDrainReruns`=3 repeticiones en Trocs posteriores, cada `kCleanupRetryUs` (1 s). La marca se borra al reasignar la clave. Modo 0/probe: una sola pasada como antes, la marca nunca se pone |
| R2 (wrong) | `m.stopped` incluía `assignments_paused`: un miembro de una unidad ya admitida nacido durante la degradación salía partido, y la contención que retira las partidas está en pausa | `npcfam::member_stopped` = sólo el aborto de U-e; la pausa sigue bloqueando raíces nuevas (`root_stopped`: su veredicto de U-e sale «abortado» y no hay unidad) |
| R3 (crash potencial, heredado) | Las unidades mezcladas (host-kept tipo 7, partidas hasta su retirada) dependen de que la puerta de la fase A cubra todas sus salidas, y la fase A documenta salidas sin política (`character_hold`, `child_settle`, D-53; en prepare: controlador ilegible, fuera del SAM, `CtrlNotUser`, `partial`, `unlinked`, sesión ilegible). Un tipo 7 con CC y restore 0 lejos del anfitrión puede quedar retenido y tirarse sin publicar (invisible) | **Sin cambio de código (medir).** Paso 1 (probe): por cada clave host-kept del censo, el estado del CC, el restore y la decisión de la puerta (`RETAINED`/`WOULD RETAIN`/`DROPPED`), y si alguno sale por `partial`/`unlinked` u otra salida sin política. Hasta medirlo: el modo 1 sólo en un PC con dos copias; en Shadow, sólo con unidades sin hijos gimmick o con 7 fuera de las unidades admitidas |
| R4 (wrong) | En la publicación y en el nacimiento fuera del marco, cualquier hijo de JERARQUÍA (+0xAC / jerarquía nativa) de una raíz o miembro con otro controlador y tipo ≠ 7 partía la unidad: una montura o un accesorio del anfitrión retiraba a un NPC sano delante del invitado | Sólo la pertenencia de DESCRIPTOR parte: `npcfam::late_child` exige `+0x2F0` leído presente en el nacimiento del hijo (leído en su mid; en la publicación, `g_fam_lineage`, reescrito en cada nacimiento). Sin linaje (0 o ilegible): `decisión=tardío sólo de jerarquía (sin +0x2F0: no parte la unidad)` y `sin_linaje` en el resumen; nunca parte ni retira. La línea es ahora `[tardío en el nacimiento]` / `[tardío en la publicación]` con `linaje=<descriptor|+0x2F0=0|desconocido>` |
| R5 (minor) | `g_fam_seen` nunca se limpiaba: una clave reutilizada ocultaba a un tardío genuino | `npcfam::KeyMarks` (clave + época, atómico, acotado): cada nacimiento de la clave reescribe su marca (dentro del marco de su unidad: insert; si no: erase) |
| R6 (wrong, medir) | El padre de un miembro (modo 1) es la clave del descriptor padre; la validación de la retirada compara con el padre nativo: rechazos motivo 4 y `vínculo_distinto` posibles | **Sin cambio (medir)**: rechazos motivo 4 y `vínculo_distinto` en los pasos 1/2. Si el vínculo nativo llega tarde, retrasar la validación de las unidades partidas (no el plazo) |
| R7 (minor, medir) | En modo 1 todo descriptor con padre fuera de una unidad admitida pierde la asignación de U-e (huérfanos, grupos, hijos de raíces del anfitrión) | **Sin cambio (medir)**: `huérfanos_al_nacer` y las líneas `[huérfano al nacer]` (abajo) frente al total de U-e |
| R8 (minor) | probe tomaba `ActivationLock` exclusivo en el mid para `can_reserve` | probe usa `ActivationRead` (compartido). El resumen sigue en exclusivo: escribe `member_noted` |
| R10 (minor) | `LeafFirst` elige las 64 más profundas y logout corría una vez: raíces detrás de más de 64 miembros | Con familias a 1, `ctl_cleanup` repite la pasada en la misma llamada mientras la anterior estuvo llena y progresó (`kCleanupPasses`=4, sin volver a mirar lo ya mirado) |
| R9, R11–R13 | Documentados por la revisión | Sin cambio; R9: el resumen y las líneas nuevas `[huérfano…]` están acotadas (64) |

### 10.1 Prueba en un PC con dos copias (15-10, kit `98491fc`, probe): ninguna familia vista

Anfitrión lejos (62 celdas), `npc_family_control=probe`. El resumen nunca vio una familia:
`admitidas=0 sin_hijos=111 preexamen_fallido=15 (envoltorio excluido=15) … asignaría=0 … huérfanos_al_nacer=89 …
completas=0`. Pero sí hubo familias partidas: 00:45:31.97, U-e asignó las raíces `0xB01003A2` y `0xB01003A5` (tipo 3,
usuario 2 `0x90100065`) y sus perros `0xB01003A3` / `0xB01003A6` (tipo 6) nacieron ~0,4 ms después, en el mismo hilo,
con el anfitrión (`0x90100000`; `NpcActivation [familia]` 00:45:32.321); la contención retiró A5+A6 a las 00:45:40.

Lo que dicen los contadores:

- **En el mid de cada raíz, su vector `+0x2F8` tenía 0 hijos** (`sin_hijos`; la lectura es la correcta: `+0x300` es el
  `count` de `0x43DB70`, y los 15 `preexamen_fallido` por envoltorio excluido demuestran que el desplazamiento sirve
  para otras raíces).
- **Sus hijos sí tenían `+0x2F0`** (pasaron por `family_child`) pero su padre de descriptor no estaba en ningún marco
  (`huérfanos_al_nacer`). Como una raíz «sin hijos» no se registraba en el marco, no se puede saber con este log si ese
  padre ES la raíz (vector llenado o vaciado después de su mid) u otro descriptor.
- Ghidra (2976): `0x281DBD0` y `0x2799C30` enlazan hijos (`inner+0x2F0 = *param_8`, `push_back(*param_8+0x2F8)`) al
  construir el árbol; la clase `pa::CreateServerActorDesc_NPCSchedule` tiene su propio vf3 `0x27A6160`, que llama al
  base `0x2791CD0` (el mid) y no toca `+0x2F8`; quién crea los actores hijos (vf de `_Simple` `0x27A7110` → `0x1010EEC0`,
  zona sin descompilar) no se pudo seguir. **Causa no demostrada.** Hipótesis por orden: (a) el vector de la raíz se
  llena (o se vacía al aplanar la lista de creación) después del mid de la raíz y los hijos nombran a la raíz en
  `+0x2F0`; (b) los hijos nombran a otro descriptor (una copia, o un descriptor de grupo).

Cambios (medir primero, el modo 1 no se activa sin esto):

- **Las raíces «sin hijos» del ámbito de U-e se registran en su marco** como unidad de 0 nodos (`Admit::Childless`, sin
  reservas; modo 1: admitida; probe: «se admitiría»). Un hijo que nace después en el mismo Troc con `+0x2F0` = ese
  descriptor sigue la política de tardíos que ya existía: pertenencia releída en el vector de su padre (`in_vector`),
  las condiciones de su nacimiento, reserva propia; si no, la unidad queda partida y se retira entera. Sin hijos, ningún
  censo (como antes). Si (a) es cierta con el vector ya lleno en el mid del hijo, probe mostrará
  `hijos_de_raíz_sin_hijos>0` y miembros «asignaría»; si el vector está vacío también entonces, saldrán partidas
  `LateNotInVector` (en modo 1 la unidad se retira, igual que antes con el hijo huérfano).
- **Diagnóstico acotado (64 líneas por sesión)** `NpcFamily [huérfano al nacer]` / `[hijo de raíz sin hijos en su
  preexamen]`: vtable del descriptor del hijo y de su padre de descriptor, `+0x300` del padre ahora, si el padre contiene
  al hijo, el `+0x2F0` del padre, y si el padre es la última raíz preexaminada en ese hilo (con su `+0x300` en su mid).
  Con esto el siguiente probe decide entre (a) y (b).
- Resumen: `huérfanos_al_nacer=… hijos_de_raíz_sin_hijos=… (raíces_registradas=…)`.

### 10.2 Comprobación del binario (paso 0 de la revisión)

`dumpbin /disasm` (VS Build Tools 14.44, x64) de `build\bin\RelWithDebInfo\cdmp.asi` con `cdmp.pdb`: cierre de llamadas
por DIRECCIÓN desde `user2_stages_npc_control` (los nombres del espacio anónimo se repiten entre unidades de
compilación): `npc_control_impl`, `family_child`, `npc_observe_birth`, `fam_orphan_line`, `fam_split_event`,
`scan_subtree`, `in_vector`, `read_parent`, `KeyTable`, `EventRing::push`, `fast_read`… **ninguna llamada a
`memset`/`memcpy`/`memmove`/`__chkstk`** en el camino normal. La única rama que llega al CRT es la de excepción de un
`std::mutex` que ya existía en la fase A: `npc_observe_birth` → `child_hold_actor_born` → `observer_hold_acks_retired`
→ `std::_Throw_Cpp_error` → `system_error` (cadenas). Marcos: `npc_control_impl` 0x598 B, `family_child` 0xD0,
`npc_observe_birth` 0x100, `fam_orphan_line` 0xD0 (< 4 KB, sin sonda de pila). La limpieza (hilos del servidor, fuera
del mid): `ctl_cleanup` 0x498, `ctl_cleanup_pass` 0x630, también sin `__chkstk`.

### 10.3 Prueba en un PC con dos copias 2 (15-10 noche, kit `165c4aa`, probe): `_NPCScheduleGroupActionMember`

Log del anfitrión `backups\p8-2inst-noche2\cdmp.host-probeB2.log` (el del invitado al lado). Anfitrión lejos (62 celdas),
`npc_family_control=probe`, U-e a 1. Resumen 01:17:27/01:18:27: `admitidas=0 sin_hijos=133..136 preexamen_fallido=21
(envoltorio excluido=21) … host-kept=1 … tardíos admitidos=1 | huérfanos_al_nacer=108 hijos_de_raíz_sin_hijos=1
(raíces_registradas=88..91) … censo: completas=1`. Las 63 líneas `[huérfano al nacer]` (cupo 64), por la vtable del
**padre** de descriptor:

| Clase | Líneas | Qué dicen |
|---|---|---|
| Padre `exe+0x5B04BE8`: 30 hijos con descriptor `0x5B04BE8` (12 tipo 3, 17 tipo 6, 1 tipo 5) y 4 `InstantGimmick` tipo 7 | 34 | `0x5B04BE8` es `pa::ICreateServerActorDesc` (la clase base usada tal cual; la escribe el constructor base `0x278EA70` en `0x278EAE1`): viajeros de carretera (`0xB010029F`, tipo 6, con `0xB01002A0..2A3`). Raíz tipo 6 / otro descriptor: fuera de U-e, nunca asignada; hijos con el anfitrión, coherente. **Fase C.** |
| Padre `NPCSchedule` (`exe+0x5B04CB0`): hijos `NPCSchedule`, 25 tipo 3 y 4 tipo 6 | 29 | En las 29: el padre es raíz (`+0x2F0=0`), lo contiene (`contiene_al_hijo=sí`), y es «la última raíz preexaminada de este hilo: SÍ» con `+0x300` **en su mid** = sus hijos (1, 2 o 3). El vector ya estaba lleno en el mid de la raíz y aun así la unidad no existía. |

Raíces de la segunda clase (18 distintas en las líneas; 21 con el resto pasado el cupo): `0xB0100300` (3 hijos tipo 3),
`310`, `312`, `32D` (2), `330`, `332` (1 tipo 6), `334` (3 tipo 6), `359` (3), `35E`, `360` (2), `363` (2), `366`, `368`,
`36A` (2), `36D`, `36F`, `371`, `38C` (3). El caso peligroso: 01:13:07.57, U-e asignó `0xB0100332` (recurso `0xDE65F7C5`) y
`0xB0100334` (`0x3FC14330`) al usuario 2 (`User2Stages [control]`), y `0xB0100333` (padre 332) y `0xB0100335/336/337`
(padre 334), tipo 6, nacieron 0,05 ms después, mismo hilo T34556 y mismo Troc, con el anfitrión
(`NpcActivation [familia] … controlador=0x90100000`); la contención retiró 336/337 a las 01:13:15 (motivo 25). En probe
los hijos tipo 3 de los grupos (`0xB0100301..303` …) siguieron con U-e (su propio veredicto, fase A): esos grupos
quedaron enteros con el usuario 2 por casualidad; en modo 1 se habrían quedado enteros con el anfitrión
(`Ctl::Family`).

**Causa por clase.**

1. *Guardia/pastor (raíz tipo 3) + animales tipo 6, y grupos de tipo 3*: **el preexamen falla por envoltorio excluido**.
   Contadores: `admitidas=0` y todos los demás motivos de admisión 0, así que toda raíz con hijos en su preexamen salió
   `ScanFailed`; `preexamen_fallido=21 (envoltorio excluido=21)`. El log no imprimía qué envoltorio (sólo
   `Scan::bad_wrap` interno), pero el hijo único de `0xB0100332` (interior `NPCSchedule`, padre `NPCSchedule`) sólo puede
   ir en un envoltorio cuyo constructor acepte horario bajo horario. Desensamblado del PE (`D:\RE\…\bin\CrimsonDesert.exe`,
   sección `.xtls`, sin función en Ghidra; referencias RIP a las vtables buscadas en bruto): `_NPCScheduleGroupActionMember`
   (`0x5B04E00`) sólo se escribe en `0x100E7158` (constructor `0x100E70A0`) y `0x100C3204` (su destructor). El constructor
   comprueba `[padre+0xA] == 0xC` y `[hijo+0xA] == 0xC` (`0xC` es el tipo que el constructor de `NPCSchedule` `0x10102580`
   pasa en `r8b` al base `0x278EA70`, que lo guarda en `+0xA`, `0x278ea70.c:18`), crea `{vtable, inner}` de 0x10 B,
   `[inner+0x2F0] = padre` y `push_back(padre+0x2F8)` con `0x43DB70`: el mismo contrato que `0x281DBD0`. Los otros excluidos
   no encajan: `_Rider` (constructor `0x100E63BF`, `.xtls`) exige la clase 5 de vehículo en el padre (`[…+0x68] == 5`, la de
   `_LinkedVehicle` `0x2797510`) y asiento libre; `_LinkedVehicle`, `_Docking` (`0x288CFD0`), `_SavedAttached`
   (`0x279FCA0`, `ScopeAttacher`) y `_CatchTarget` (`0x2793CA0`, `0x2799C30`) son de vehículos/gimmicks. **La exclusión era
   demasiado amplia**: `_NPCScheduleGroupActionMember` es el vínculo de los grupos de horario (guardia + perro, pastor +
   rebaño, patrullas), no una montura ni un vehículo. (Su vf2 es el stub compartido `0x35EA90`, no el creador `0x27A7110` de
   `_Simple`: el envoltorio no crea al hijo; el hijo nace en el mismo Troc tras su raíz, como muestra el log.)
2. *Registro*: con el preexamen fallido la raíz **no se registraba** (el marco sólo se tomaba con el preexamen bien,
   aunque el comentario de `add_host_root` decía «o el preexamen falló»): sus hijos salían `huérfanos_al_nacer` en vez de
   `de_unidad_del_anfitrión`. Sin efecto en el controlador (en modo 1 la raíz tampoco se asignaba), pero tapaba la causa.
3. *Viajeros* (`0x5B04BE8`): no son raíces de U-e; correctos con el anfitrión. Fase C; sólo ensuciaban el contador.
4. El único `[hijo de raíz sin hijos en su preexamen]` (`0xB010033C`, tipo 7, `InstantGimmick` bajo `0xB010033B` con
   `+0x300` 0 en su mid y 1 después) confirma que los gimmicks pueden enlazarse después del mid: entró como tardío
   host-kept (`completas=1`, la única), la política de tardíos funciona.

**Cambios** (`npc_family_logic.h`, `user2_stages.cpp`):

- `npcfam::classify(envoltorio, interior, padre)`: `_NPCScheduleGroupActionMember` = horario sólo con interior y padre
  `NPCSchedule`; el preexamen lee la vtable de cada padre con hijos (lectura protegida; ilegible = `Unreadable`); el tardío
  la del descriptor de su padre. `_Rider`, `_LinkedVehicle`, `_Docking`, `_CatchTarget`, `_SavedAttached` siguen excluidos
  (prueba). En modo 1, guardia + perro y pastor + ovejas se admiten como unidad y los hijos 3/6 reciben al usuario 2 por
  `member_verdict`; un miembro de tipo inesperado (4/5) en un grupo parte la unidad y se retira entera (nunca se asigna).
- La raíz con preexamen fallido se registra como unidad del anfitrión (`add_host_root`, hijos = `+0x300` en su mid): sus
  hijos cuentan en `de_unidad_del_anfitrión` y su `[censo]` sale `con el anfitrión motivo=preexamen fallido`.
- Diagnóstico por clase: `NpcFamily [preexamen fallido] … motivo=… nodo: envoltorio exe+0x… (<nombre>) interior … padre …
  profundidad=…` (8 líneas por envoltorio); `[huérfano al nacer] … clase=<viajero|horario|gimmick|otro padre>` con cupo
  por clase (16; viajeros 6; hijo de raíz sin hijos 16); en el resumen `huérfanos_al_nacer` ya **no** cuenta viajeros y
  sale `viajeros_fase_C=N`; línea nueva cada 60 s `NpcFamily [clases] preexamen_fallido por envoltorio: _Simple=… …
  _NPCScheduleGroupActionMember=… otro=… sin_envoltorio=… | huérfanos por padre: … | unidades con
  _NPCScheduleGroupActionMember: unidades=… nodos=…`.
- Prueba `group_action_members` (`optd_npc_family`): la clase y sus contrarios (padre sin leer / gimmick / base, interior
  gimmick, los cinco excluidos bajo horario), guardia + perro y pastor + tres ovejas admitidos, perro miembro y montura
  tipo 5 partida, tardío del grupo, grupo bajo gimmick y jinete debajo con el nodo que falló, vtable del padre ilegible,
  clases de huérfano y el cupo por clase.

**Esperado en el siguiente probe**: `[clases] … _NPCScheduleGroupActionMember=0` (o sólo bajo padres no horario),
`[censo] … unidad=se admitiría estado=completa miembros_que_asignaría=N` para `0xB0100332`-like y los grupos,
`unidades con _NPCScheduleGroupActionMember: unidades>0`, `huérfanos_al_nacer` ≈ 0 fuera de viajeros. Si `[preexamen
fallido]` muestra otro envoltorio, ésa es la clase siguiente. Sin el probe no se pasa al modo 1.

### 10.4 Prueba en un PC con dos copias 3 (15-10 noche, kit `ce5d47d`, probe): marcos llenos

Log `backups\p8-2inst-noche3\cdmp.host-probeB3.log`. Resumen 01:58:50: `admitidas=4 sin_hijos=125 preexamen_fallido=0 …
marco_lleno=14 … asignaría=7 … huérfanos_al_nacer=46 viajeros_fase_C=67 … completas=4 partidas=0`; `[clases]`:
`_NPCScheduleGroupActionMember=0` fallos, `unidades=4 nodos=7`; huérfanos por padre: viajero 67, horario 27, gimmick 19.
El arreglo de §10.3 funciona (cero fallos de preexamen, cuatro unidades completas); el siguiente límite es el marco.

**Por qué se llena.** Un marco por Troc (y profundidad): **todas** las raíces de U-e de ese Troc se registran en él.
Capacidad hasta `ce5d47d`: 255 registros y **64 unidades**, y cada raíz sin hijos ocupaba una unidad (unidad de 0 nodos,
§10.1). Al armar, U-e asignó **157 raíces en el mismo segundo** (`User2Stages [control]`, 01:54:14; 178 en toda la
sesión): las 64 primeras agotaron las unidades, las raíces sin hijos siguientes no se registraron (sin contador) y las 18
con hijos que llegaron después salieron `marco_lleno` (14) o admitidas (4, antes del límite). Sus hijos salieron huérfanos:
las 16 líneas de horario son de 01:54:14.915 (`0xB0100351..368`, padres raíz con `+0x300` 1–3 y que los contienen).
`raíces_registradas=76` = 64 del Troc del armado + 12 posteriores.

**Los 19 huérfanos de padre `InstantGimmick`** no tienen línea (sólo se cuentan: son «hijo de un huérfano», el padre
estaba en el marco como huérfano). Con las altas de `NpcProbe`: 01:54:08.765, `0xB01002AA..2BC` (19, tipo 7), hijos del
gimmick `0xB01002A9` (tipo 7, 19 hijos), hijo de `0xB01002A6` (tipo 5, carretera `0xFA1`, montura con jinete
`0xB01002A7`, `MountSync MountCommit`): la carga de una carreta de viajeros. Del anfitrión, esperados; son **viajeros**.

**Cambios** (`npc_family_logic.h`, `user2_stages.cpp`):

- Una raíz sin hijos en su preexamen ocupa **sólo un registro** (`add_childless_root`, `Kind::Root` sin unidad). Su unidad
  (`Admit::Childless`, admitida en 1 / «se admitiría» en probe, controlador = la clave del usuario 2) se crea con su primer
  hijo tardío (`promote_root`, en `family_child`); si su clave no se pudo registrar (`KeysFull`) la unidad es del
  anfitrión. Sin unidad libre en ese momento: en modo 1 el hijo nace con el anfitrión y la unidad queda **partida**
  (`LateNoRoom`, se retira entera); en probe se cuenta. Fail-closed como antes.
- Marcos más grandes: **1023 registros** (índice `u16`, centinela `kNoRec`) y **128 unidades** (raíces con hijos o del
  anfitrión). ~48 KB por marco, 8 marcos (~390 KB estáticos). Medido en el armado: 157 raíces + 18×(1+nodos) + ~113
  huérfanos ≈ 300 registros y 18 unidades; peor caso acotado por las constantes. Un tardío sólo necesita registro
  (`rec_room`), no unidad libre. La búsqueda sigue lineal (≤ 1023 comparaciones por marco; el coste del mid sigue en el
  resumen).
- Viajeros: un huérfano guarda la clase de su padre (`Rec::oc`) y sus descendientes la heredan: los 19 tipo 7 de la
  carreta cuentan en `viajeros_fase_C`, no en `huérfanos_al_nacer`. Nunca se admiten: sus raíces (tipo 5/6, descriptor
  base) no son raíces de U-e.
- `[clases]` añade `marcos: registros_máx=N/1023 unidades_máx=N/128 raíces_sin_hijos_sin_registro=N
  unidades_al_primer_tardío=N sin_unidad_libre=N`.
- Prueba `frame_burst`: el Troc del armado medido (157 raíces sin hijos + 18 con 3 nodos + 113 huérfanos) cabe con 18
  unidades; `promote_root` (probe, 1, del anfitrión, ya promovida, sin unidad libre); un tardío con unidades llenas;
  registros llenos.

**Esperado en el siguiente probe**: `marco_lleno=0`, `registros_máx` < 1023 y `unidades_máx` < 128, `huérfanos por
padre: horario` ≈ 0, `raíces_sin_hijos_sin_registro=0`. El modo 1 sigue apagado hasta verlo.
