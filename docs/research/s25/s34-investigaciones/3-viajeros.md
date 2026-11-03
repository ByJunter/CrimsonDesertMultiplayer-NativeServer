# S34: viajeros 2550 del invitado — qué muestra la sonda y diseño de `user2_road_control=1`

Fecha: 14-10-2026. **Sólo lectura**: no se ha editado código, compilado, desplegado ni abierto el juego. Juego **1.0.0.2976**, PE timestamp **0x6AB28F00** (comprobado en el exe), base **0x140000000**; todas las direcciones son RVA de esta build. Worktree `C:\Emulador CD-npc-s33-fix`, HEAD `d835806`.

Etiquetas: **[V]** medido en captura/log; **[E]** leído en código/desensamblado; **[H]** hipótesis pendiente de medida.

Datos y scripts reproducibles en `build/research/s34/viajeros-datos/` (Python de investigación `C:\Emulador CD\build\research\venv\Scripts\python.exe -I -B`, ejecutar desde esa carpeta): `s34road.py` (cohorte por clave), `guestsplit.py` (vista del invitado separada por solicitante), `windows.py` (ventanas lejos/cerca/lejos), `guestctl*.py` (NPC controlados por el invitado), `peek.py` (una clave en ambos PC), `dis.py` (desensamblado con capstone), `cohort1b.json` / `cohort2b.json` (resultados).

## 0. Resumen

1. **[V] Confirmado en S34, ahora con un control interno de la misma sesión.** Los 327 actores nacidos de AutoSpawnRoad 2550 del invitado (paso1b 245, paso2b 82; tipos 3, 5 y 6, más 34 «Game Mechanic» tipo 7 hijos suyos) nacen **todos** con controlador `0x90100000` (anfitrión), aunque el solicitante del 2005 es el personaje del invitado. El cliente del anfitrión los crea y ejecuta su IA a los ~140 ms (2223/2734/2291/2405), pero **no los desplaza mientras está lejos**: 139/140 (paso1b) y 81/82 (paso2b) con máximo X/Z ≤ 1 m. La excepción es el «Wagon Driver», que llega a 2,9 m, igual que en S33. Al mismo tiempo, **el mismo cliente del anfitrión sí mueve sus propios viajeros**, los que él pidió junto a su personaje (paso2b: 12/12 tipo 3, 61/64 tipo 6). En paso1b, al acercarse el anfitrión, los 46/46 viajeros que seguían vivos empiezan a moverse con la misma clave y el mismo controlador. Al volver a alejarse, unos 9 de 19 (tipos 3 y 5) se paran en unos 20–40 s y las caravanas siguen su ruta. **La hipótesis «el cliente del anfitrión no puede iniciar locomoción lejos de su personaje» queda reforzada**. El predicado nativo exacto (navegación/streaming) sigue siendo [H].
2. **[V] El resumen del invitado `viajeros activos N desplazados M` engaña.** Cuenta cualquier 2005 con metadatos de carretera, sea cual sea el solicitante. Los «desplazados» son viajeros **pedidos por el anfitrión** junto a su personaje que también llegan al invitado. Si se separan por solicitante, los del invitado lejos del anfitrión dan **0/82 desplazados** (paso2b).
3. **[E] Diseño:** asignar el controlador en el **único** mid hook existente `0x2792A7B` mediante la **procedencia del descriptor**. `0x27EA160` escribe en cada descriptor de raíz y de acompañante la clave del solicitante (`+0x3C8`), su puntero (`+0x3B8`) y una marca `+0x3CD = 1`. El constructor `0x278EA70` los deja a cero y fija la vtable `0x5B04BE8`. No hace falta ningún hook nuevo ni correlación por hilo/TLS. Tabla propia, independiente de U-1/U-e. Limpieza sólo en la carga y el Logout del usuario 2. **No se devuelven al anfitrión cuando éste se acerca.**
4. **[V] El cliente del invitado sí desplaza los NPC que controla, con el anfitrión lejos:** en paso1b, NPC de horario asignados por U-e («Stranger», «Wandering Freesword», «Aspirant», guardia) recorren 22–39 m (trayectoria de 24–53 m) entre H123 y H207, con el anfitrión a 61 celdas. Son las mismas plantillas que, como viajeros con controlador anfitrión, se quedan quietas en esa misma ventana.

## 1. Datos S34 utilizados

| Alias | Fichero | Notas |
|---|---|---|
| H1b | `C:\Emulador CD\backups\p8-sesion34\paso1b\cdmp_20261014-184959.log`, captura `cdmp_packets_20261014-185010.bin` | 3572 líneas `[viajero]`: 3525 entradas 2550 de `A01001D5` y 47 altas tipo 3 |
| H2b | `...\paso2b\cdmp_20261014-190232.log`, captura `cdmp_packets_20261014-190242.bin` | 1133 líneas `[viajero]`: 1110 entradas 2550 de `A010024A` y 23 altas tipo 3 |
| G1b | `...\shadow-p1b-p2b\cdmp_20261014-175039.log`, captura `cdmp_packets_20261014-175057.bin` | Usuario `0x901001D4`, personaje `A01001D5` |
| G2b | `...\shadow-p1b-p2b\cdmp_20261014-180502.log`, captura `cdmp_packets_20261014-180517.bin` | Usuario `0x90100249`, personaje `A010024A`; **cierre `exe+0x1831598` a las 18:07:10** (§6) |

Alineación de capturas por clave 2005 común (mediana): H1b = G1b + 60,797 s (n=4418) y H2b = G2b + 167,170 s (n=976). Estas cifras **no** son latencias. Las duraciones se calculan siempre en un solo PC.

Fases del anfitrión según `User2Stages [state]/[owner load]/[prepare]` de H1b:
- **Lejos:** 59–62 celdas hasta t≈260.
- **Carga del gestor del anfitrión:** t=261.952745 (línea 35716).
- **Preparación:** t=271.126643 (36424), con `DRAINED now` del gestor de user 2 en 271.34.
- **Cerca:** 0–1 celdas entre t≈265 y 325.
- **Vuelve lejos:** carga en 325.397111 (43697), preparación en 332.174 y 61 celdas desde 330.
- **Logout de user 2:** 377.77.

En H2b el anfitrión estuvo **lejos todo el tiempo** (60–61 celdas, t 212–258): es una prueba limpia de «anfitrión lejos».

## 2. Qué muestra la sonda (pregunta 1)

### 2.1. Solicitante frente a controlador al nacer

**[V] Sonda del anfitrión (`npc_observe_2005`, user2_stages.cpp:3590–3612).** Las 47 (H1b) y 23 (H2b) líneas `NpcActivation [viajero]` tipo 3 tienen `controlador=0x90100000` y `adicional` = solicitante = personaje del invitado (2685403605 = `0xA01001D5`; 2685403722 = `0xA010024A`). La sonda sólo registra el 2005 encolado al invitado y sólo el tipo 3. **No lee el descriptor al nacer**: el S34 no mide todavía `desc+0x3C8` en `0x2792A7B` (véase §4.7, paso de sonda previo).

**[V] Capturas, cohorte completa por metadatos.** Criterio: registro 2005 con `fin+88` = personaje del invitado y ID de carretera ≠ 0, de cualquier tipo. Además, sus hijos anunciados sin metadatos.

| | paso1b | paso2b |
|---|---:|---:|
| Entradas 2550 del invitado | 3550 | 1110 |
| Actores con metadatos del invitado (tipo 3 / 5 / 6) | 245 (47 / 10 / 188) | 82 (23 / 4 / 55) |
| Su ID de carretera aparece en sus 2550 | 242 (3 tipo 6 con carretera `0x8`, no) | 81 |
| Hijos sin metadatos (todos tipo 7 «Game Mechanic») | 21 | 13 |
| Controlador en el 2005 del invitado / del anfitrión | **`90100000` en todos** | **`90100000` en todos** |
| 2005 recibido también por el cliente del anfitrión | 245/245 | 82/82 |
| Latencia alta→primera Req del cliente anfitrión (mediana / máx.) | 146 ms / 1,11 s | 139 ms / 274 ms |
| Req del invitado para esas claves | 0 (sólo 2556 para 7 «Game Mechanic» tipo 7) | 0 (ídem) |
| Baja | motivo 25 en todos; 44 en 2 carros | motivo 25; 44 en 1 |
| Vida mediana / máx. | 44 s / 253 s | 39 s / 54 s |

Carreteras: `0x3FD`/`0x3FE`/`0x7D1`/`0xFA1` para caminantes, carros y caballos; `0x1F5` para bandadas (loros, cuervos, palomas, halcón). Familias: 24 de 47 tipo 3 anuncian hijos (cabras, ovejas, carro + caballo). 118 tipo 6 tienen padre.

### 2.2. ¿Se mueven? Posiciones de 2223 y 2556 en origen

El X/Z máximo se mide respecto a la primera posición real 2223/2556 del cliente controlador, el anfitrión. Se consideran válidos los actores con posición y nacidos ≥15 s antes de la carga del anfitrión.

| | Válidos lejos | ≤1 m lejos | >1 m lejos | Req del cliente anfitrión mientras lejos |
|---|---:|---:|---:|---|
| paso1b | 140 | **139** | 1 (Wagon Driver 2,9 m) | 2223×8127, 2734×5594, 2291×868, 2405×214, **2556×67** (8 claves, ajustes de altura) |
| paso2b | 82 | **81** | 1 (Wagon Driver 2,9 m) | 2223×2793, 2734×1985, 2291×229, 2405×43, **2556×22** (3 claves) |

paso1b, al acercarse el anfitrión. De los 139 quietos, 93 se retiraron (motivo 25) antes de la preparación. **Los 46 vivos en la preparación se movieron todos (46/46)**: 9/9 tipo 3, 1/1 tipo 5 y 36/36 tipo 6. Ventana completa cerca: 17 tipo 3, 4 tipo 5 y 87 tipo 6 desplazados, con la misma clave y el mismo controlador.

paso1b, al volver a alejarse el anfitrión (t ≥ 333,4, `windows.py`). De los tipo 3/5 que se movían cerca y siguen vivos (19), 9 se paran o no vuelven a moverse en unos 20–40 s. Por ejemplo, `B01003D1` hace 2,1 m y luego 0, y `B0100415` hace 12,2 m y luego 0. Las 10 caravanas siguen hasta el final de la captura: `B0100BB5` «Narrow Grain Wagon», 65 m y 41 2556; `B01003F8/F9` «Herspia» + «Wandering Freesword», 92 m. **[H]** Encaja con «una ruta ya calculada cerca se completa; lejos no se inicia una nueva». No distingue entre navmesh, streaming de caminos y caché.

**[V] Control decisivo (`guestsplit.py`).** El invitado recibe también viajeros pedidos por el **anfitrión** (`fin+88 = A0100001`) junto al personaje del anfitrión (x −6878…−6603, z −3177…−2932), a ~4 km. En paso2b llegan en los primeros segundos de G63–85. Esos sí se mueven, porque su controlador está cerca:

| Vista del invitado (S>C 2697/2991) | paso1b n / >1 m | paso2b n / >1 m |
|---|---|---|
| Pedidos por el anfitrión, tipo 3 | 38 / 38 | 12 / **12** |
| Pedidos por el anfitrión, tipo 6 | 292 / 282 | 64 / **61** |
| Pedidos por el invitado, tipo 3 | 47 / 17 (sólo tras acercarse el host) | 23 / **0** |
| Pedidos por el invitado, tipos 5 y 6 | 10 / 4; 188 / 126 (ídem) | 4 / **0**; 55 / **0** |

El mismo cliente, a la misma hora y con el mismo controlador `90100000`, mueve los viajeros cercanos a su personaje y no los lejanos. **Confirmado (con el matiz anterior): el cliente del anfitrión no pone en marcha la locomoción de NPC lejos de su personaje.** Las claves no cambian y no se recrean, así que la diferencia está en el cliente controlador.

**[V] Por qué en vivo se ven todos quietos y el log del invitado dice otra cosa.** `npc_probe_client.cpp:301` cuenta como viajero cualquier `a.road` (`npc_probe_client_logic.h:40`: solicitante 0xA… y carretera ≠ 0) **sin mirar el solicitante ni el controlador**. Ejemplos: G2b 18:06:53 `activos=88 desplazados=50` y G1b 17:52:30 `B0100294:78.1m`, que es un «Grey Parrot» pedido por `A0100001` en (−6655, −3071). Los «desplazados» son viajeros del anfitrión. Los del invitado siguen quietos, como se observa en vivo.

## 3. ¿Moverá el cliente del invitado a sus viajeros? (pregunta 3)

- **[V] U-e en S34, con el anfitrión lejos.** En G1b, 228 NPC tipo 3 con controlador `0x901001D4` reciben 3836 2556 del invitado. Los que recorren distancia son las mismas plantillas que los viajeros: `B0100336` «Stranger», 38,7 m (trayectoria 52,8 m, 29 2556); `B0100331` «Wandering Freesword», 30,6 m; `B010032E` «Hernandian Guard», 30,8 m… Ocurre en G62–146, es decir H123–207, con el anfitrión a 61 celdas (`guestctl2.py`). La mayoría restante está en su puesto (comportamiento de horario), así que el contraste es el par de plantillas, no el porcentaje.
- **[V] S33 §3.2:** 1811 2556 de Shadow para los 286 asignados por U-e.
- **[E] Contrato nativo.** En un jugador solo, el dueño del campo es el propio jugador, así que el solicitante de 2550 es también su controlador (selector `0x2792A70`: `field+0x180`). Asignar `controlador = usuario del solicitante` reproduce exactamente el contrato de un jugador.
- **[E] El invitado no necesita cambios para ejecutarlos.** El 2005 llega con `+0x12 = su usuario`. `remote_motion`, `remote_interp`, `guest_remote_update_guard` (`gad::contain_remote_update`: sólo controlador ≠ local) y `docked_gimmicks` (sólo tipo 7 acoplado a personajes) no actúan sobre actores controlados localmente.
- **[H] Lo que falta demostrar:** carros y caballos (tipo 5), bandadas 0x1F5 y el «Game Mechanic» tipo 7 de las caravanas con controlador invitado. También que su IA no exija algo que sólo el anfitrión tiene (por ejemplo, gimmicks con otro dueño, §5).

## 4. Diseño de `user2_road_control=1` (pregunta 2)

### 4.1. Principio

Una autoridad por unidad de spawn, decidida **al nacer y antes del primer 2005**, sin cambiar `+0x90` de actores vivos. La unidad es todo descriptor que `0x27EA160` construye para una petición 2550 del invitado: raíz y acompañantes. El mundo y las misiones siguen siendo del anfitrión. Independiente de U-1 (armado, timers) y de U-e (su aborto global, su vigilante «unaccepted» y sus drenajes).

### 4.2. Procedencia: descriptor, no hilo ni dirección remota

**[E] `0x27EA160`** (`D:\RE\CrimsonDesert_2976\exports\decomp\0x27ea160.c`):
- `:238` `iVar36 = *(int*)(requester + 0x60)` (clave del actor solicitante; `kActor_Key = 0x60`); `:253` `local_20cc = iVar36`; `:1324`/`:1614` lo restauran.
- Raíz (`:1433–1478`): reserva 0x4E8, `0x278EA70`, `[+0x3B8] = solicitante` si su tipo es 1 (jugador, `:1462–1464`), `[+0x3C8] = clave` (`:1466`), `+0x3CC = (primero del lote)`, **`+0x3CD = 1`** (`:1478–1479`).
- Acompañantes (`:1660–1713`): mismo constructor, `+0x3B8`, `+0x3C8` (`:1698`) y `+0x3CC:u16 = 0x100` (`:1713`), es decir **`+0x3CD = 1`**. Se cuelgan de la raíz (`thunk 0x1500E63B0`, `:1720`).
- El lote va a `thunk 0x1501C9D20` (`:1886`) y después se destruyen los descriptores. **[H]** No se sabe si la creación es síncrona: no importa para este diseño.

**[E] `0x278EA70`** pone `pa::ICreateServerActorDesc::vftable` = **`0x5B04BE8`** (capstone: `0x278EADE lea rax,[rip+0x3376103]`). Pone a 0 `+0x3B8`, `+0x3C8`, `+0x3CC:u16` y `+0x40`.

**[E] Selector `0x2792A11–0x2792A7B`** (capstone):

| Orden | Fuente del controlador | Dirección |
|---|---|---|
| 1 | `[r14+0x90]`, si ya existe | salta a `0x2792A7B` |
| 2 | `[[rsi+0x40]+0x60]` | |
| 3 | `[[r14+0xA0]+0x60]` | |
| 4 | `[r13+0x180]`, el dueño del campo | `0x2792A70` |

Después, `test eax,eax / je 0x2792A82`; el mid escribe `[r14+0x90]` en **`0x2792A7B`**. En el hook: `rsi` = descriptor, `r14` = actor nuevo, `r13` = campo.

**Identidad de procedencia:**
- `[desc] == base+0x5B04BE8`.
- `[desc+0x3CD] == 1`.
- `[desc+0x3C8] == g_u2_key` (clave de servidor del personaje del invitado, `A01…`).
- `[desc+0x3B8] == g_u2_char` (puntero local del mismo proceso, sólo como segunda comprobación).

En la tabla, cada actor se registra por **clave completa de servidor `B01…` + época de sesión + número de nacimiento**. Ninguna dirección de memoria se compara entre PC. El invitado reconoce el resultado sólo por el controlador del 2005, su clave de usuario.

**[H] Unicidad.** Entre los 827 exports sólo `0x27EA160` escribe `+0x3CD=1`. `0x288F0D0` también escribe `+0x3B8` (otra ruta). La sonda (§4.7) debe demostrar en vivo que la marca coincide 1:1 con los 2005 cuyos metadatos dicen «2550 del invitado». Si no, no se activa.

### 4.3. Veredicto puro (nuevo, `user2_stages_logic.h`, junto a `Ctl`, líneas 669–760)

```cpp
inline constexpr uint32_t kVtDescServerActor = 0x5B04BE8;            // pa::ICreateServerActorDesc (ctor 0x278EA70)
inline constexpr uint32_t kDesc_ReqActor = 0x3B8, kDesc_ReqKey = 0x3C8, kDesc_RoadMark = 0x3CD;
enum class Road : uint8_t { Assign, NotRoad, HostRequester, OtherRequester, PtrMismatch, HasController,
                            DescUser, HasParent, NotHostField, NotOwnerBranch, Type, NoKey, KeyMismatch, Stopped, Count };
struct RoadIn {
    bool vt_generic; uint8_t mark; uint32_t req_key; bool req_ptr_ok;      // descriptor
    uint32_t u2_char_key, host_char_key;                                    // g_u2_key / g_host_key
    uint32_t old_ctl; uint64_t desc_user, parent; uint32_t eax, field_owner; bool host_field;
    uint8_t type; uint32_t key2, session_key2; bool stopped;
};
inline Road road_verdict(const RoadIn& r) {
    if (!r.vt_generic || r.mark != 1 || (r.req_key >> 28) != 0xA) return Road::NotRoad;  // salida barata
    if (r.req_key == r.host_char_key) return Road::HostRequester;   // los del anfitrión: nunca
    if (!r.u2_char_key || r.req_key != r.u2_char_key) return Road::OtherRequester;
    if (!r.req_ptr_ok) return Road::PtrMismatch;
    if (r.stopped) return Road::Stopped;
    if (r.old_ctl) return Road::HasController;
    if (r.desc_user) return Road::DescUser;
    if (r.parent) return Road::HasParent;                           // hijo de un usuario (montura/mascota)
    if (!r.host_field) return Road::NotHostField;
    if (!r.eax || r.eax != r.field_owner) return Road::NotOwnerBranch;
    if (r.type != 3 && r.type != 5 && r.type != 6) return Road::Type; // 4/7 sólo contados (§4.5)
    if (!r.key2) return Road::NoKey;
    if (r.session_key2 && r.session_key2 != r.key2) return Road::KeyMismatch;
    return Road::Assign;
}
```

Todas las comprobaciones de U-e para la rama del dueño del campo se mantienen. Sólo cambia el origen: descriptor marcado en lugar de timer + `0x5B04CB0`. Ambas son excluyentes por vtable.

### 4.4. Ubicación exacta en el código

| Sitio | Cambio |
|---|---|
| `docked_gimmicks.cpp:395–405` `ctl_write_mid` | Sigue siendo el único hook. Pasa además `t_dock.parent` (leído antes de la llamada; es `thread_local`, sin coste) a `user2_stages_npc_control`. No hay otro `create_mid`. |
| `user2_stages.h:108`, `user2_stages.cpp:3336–3340` `user2_stages_npc_control` | Orden: `npc_control_impl` (U-e) → **`road_control_impl(rax, desc, field, actor, dock_parent)` sólo si U-e no cambió `rax`** → `npc_observe_birth`, que ve el controlador final. |
| `user2_stages.cpp` (nuevo, junto a `npc_control_impl` :1134–1190) `road_control_impl` | Lecturas `rdv`/`rdp` protegidas. Coste por creación ajena: un atómico de modo, la vtable, un byte y un u32. Sin CRT, sin locks y sin copias agregadas, igual que U-e. Con `Assign`: en modo 1 `eax := key2` y `g_road_keys.put(key, kKeyAssigned)`; en probe, `kKeyWould`. Contadores por veredicto y evento en el anillo (clave, tipo, solicitante, `desc+0x3CC`, campos `+0x3D0..+0x3E8` para cotejar carretera/sub, opcode de `subject_user2(&op)` si lo hay). |
| `user2_stages.cpp:282–340` | Estado nuevo e independiente: `g_road_mode`, `g_road_keys` (`u2s::KeyTable<4096>`), `g_road_stopped`, contadores. **No** se usan `g_ctl_keys`, `g_ctl_aborted` ni `ctl_stats`. |
| `user2_stages.cpp:3062–3075` | Pedir el hook también con `g_road_mode != 0`. Hoy ya lo pide con `npc_probes()`, que incluye `g_road_probe`. |
| `user2_stages.cpp:3176–3195` `user2_stages_set_npc_probes` | `parse_mode(road, true)` en el anfitrión. El modo 1 exige `g_cleanup_ok` (bytes de `0x2A827B0`/`0x1434880`/`0x2068340`, como U-e en :3014–3019, :3064–3069). Si no, queda en probe. No exige `user2_stages=1` ni U-e. |
| `npc_probe_client.cpp:364–369` | El invitado acepta `1` y lo trata como probe (sólo observa). |
| `user2_stages.cpp:972–988` `ctl_on_logout` | Marcar también `g_road_keys` como `kKeyRemoved` (motivo y momento) para baja natural, recolector o 42. |
| `user2_stages.cpp:3524–3541, 3590–3612` | La sonda 2005: línea por clave de **todos** los tipos de la cohorte (hoy sólo tipo 3), con controlador esperado (`key2`) y real. `road_check_2005` análogo a `ctl_check_2005` (:3622–3640), llamado desde `user2_stages_on_out(2005)` (`server.cpp:643–646`). |
| `user2_stages.cpp:3542–3551` (Req de user 2) y `npc_observe_req` (:3576) | Marcar `kKeyDriven` en `g_road_keys`. Las Req del **cliente anfitrión** para una clave road asignada indican doble control: `g_road_stopped = true`. |

### 4.5. Familia y caravana

- **[E] Raíz y acompañantes** llevan la marca y el solicitante (§4.2): el mismo veredicto asigna toda la caravana (conductor, carro, caballo, cabras) sin conocer el vínculo padre/hijo al nacer. El «Wagon Driver» (3), el «Desert Freight Wagon»/«Priden Wagon Horse» (5) y los animales tipo 6 tienen metadatos del invitado en el 2005.
- **[V/H] Los hijos tipo 7 «Game Mechanic»** (21 y 13) **no** tienen metadatos y nacen con controlador anfitrión. Según `docked_gimmicks_logic.h:7–11`, el cliente sólo ejecuta un evento de gimmick si `[[jugador+0xA0]+0x60] == [gimmick+0x90]`. Si el viajero lo controla el invitado y su gimmick el anfitrión, la caravana queda partida (el caso del pastor en S33).
  - Propuesta: asignar también `type 7` cuando `dock_parent` (el `t_dock.parent` de `0x28AD170/0x28ADE40`) es una clave viva de `g_road_keys` asignada al invitado, con las mismas condiciones `old_ctl/desc_user/parent/eax==field_owner`.
  - **Primero en probe:** medir si esos tipo 7 nacen dentro del contexto dock o por otra ruta. En ese caso, alternativa: `child_hold_actor_parent(actor)` al nacer.
  - Mientras no se mida, `type 7` = `Road::Type` (sólo contado). Es la única pieza de la familia que puede quedar partida en la primera activación.
- **[H] Tipo 6 sin primera acción.** El caso del ringneck, `child_hold.cpp:620–635`: `child_hold` **retiene para siempre del anfitrión** un NPC controlado por el invitado con restore 0 (`WithheldUser2`, genérico por controlador). Si las bandadas o los animales siguen en restore 0 bajo el invitado, el anfitrión no los verá al acercarse: es seguro (evita `0x1831598`), pero visible. Medir `WithheldUser2` por clave road. Si se confirma, decidir entre aceptar esa invisibilidad o excluir las raíces tipo 6 de bandada. Las de bandada se reconocen por la carretera en el descriptor, que primero hay que cotejar (campos `+0x3D0..+0x3E8`). **No excluir tipo 6 de una caravana con raíz tipo 3/5**: partiría la familia.

### 4.6. Ciclo de vida: acercamiento, carga y Logout

**Qué pasa nativamente cuando llega el anfitrión ([V] H1b).** Nada con el viajero. Conserva clave y controlador. El cliente controlador, ahora cerca, empieza a moverlo. Después lo retira el recolector (motivo 25; 44 en carros), con vida mediana de ~40 s. El drenaje `user2_drain_at_host_prepare` (H1b 271.34, `DRAINED now`) sólo descarga el gestor de horarios del invitado. Los viajeros no son de stage y no les afecta (46/46 siguieron vivos).

**Diseño:**

| Evento | Viajeros de user 2 | Por qué |
|---|---|---|
| Anfitrión se acerca o prepara (`[drain at host prepare]`, `kCleanAfterDrain`) | **Nada**: siguen con el invitado | Ambos clientes están cerca. El controlador, el invitado, es competente. No hay duplicación de gestores como con las stages. No hay traspaso en vida demostrado (§5.4 del informe S33), y devolverlos exigiría una retirada visible sin beneficio. El anfitrión los recibe como observador. Si un tipo 6 llega con restore 0, se retiene (`child_hold.cpp:629–635`). |
| Cliente del anfitrión pide sus propios viajeros | Se quedan con el anfitrión (`HostRequester`) | Población separada por solicitante. |
| Carga del invitado: teletransporte, viaje rápido, cambio de campo (`user2_stages_troc_enter`, `is_load` / `drains_at_entry`) | `road_cleanup(kRoadItsLoad)`: Logout motivo 25 | Nadie competente queda junto a ellos. En un jugador los recogería el recolector. Si el anfitrión está cerca, mejor desaparecer que quedarse congelados. **Colocar antes** del `return` de `mode_now() != On` en :3465, para no depender de `user2_stages=1`. |
| Logout del personaje del invitado (`on_logout` :992–1004, `ctx.rcx == g_u2_char`) | `road_cleanup(kRoadItsLogout)` tras `logout_cleanup` | Igual que U-e: hilo del servidor, personaje aún entero. |
| Reconexión / nueva época | Tabla vaciada por época; claves reutilizadas empiezan sin marca (`KeyTable::put`) | Como `g_ctl_keys`. |
| Invitado se aleja cabalgando sin cargar | **[H] Sin acción en la v1.** Medir la vida y la distancia de sus viajeros. Si el recolector no los retira y el anfitrión los ve congelados, añadir después una retirada por distancia con umbral **medido** | No inventar radio. |

`road_cleanup` reutiliza tal cual `cleanup_one` (`user2_stages.cpp:1630–1672`):
- `SAM::FindActor 0x2A827B0` dentro de un `ScopeAttacher`, destructor `0x1434880`.
- Comprueba la clave completa y `+0x90 == key2`. Si otro dueño o clave ya no es nuestro, no lo toca.
- Llama `ServerActor::Logout 0x2068340(actor,&out,0x19,[exe+0x6CF76F8])`; «ya se está yendo» (`[exe+0x6CDB934]`) cuenta como hecho.
- Bajo `ModLock`, con 64 por llamada como máximo. Si queda resto, se repite en el siguiente Troc de user 2.

Es una función propia con su propia lista de motivos. **Nunca la llaman** `kCleanAfterDrain`, `kCleanUnaccepted` ni la contención de `npc_activation`. Si la limpieza falla (SEH), `g_road_stopped = true`: no hay más asignaciones y el resto de módulos no cambia.

### 4.7. Modos, parada y sondas

- **`0`:** nada.
- **`probe`:** veredicto completo, línea `NpcActivation [viajero nacimiento] ... veredicto=... solicitante=... marca=... vt=... opcode=... 3CC=... 3D0..3E8=...` por clave, con tope; resumen por veredicto. Incluye `HostRequester`, que valida el lector en un solo PC con los viajeros del anfitrión. Sin escritura.
- **`1`:** escritura sólo con `Assign`.

**Parada local** (`g_road_stopped`):
- 2005 de una clave road que llegue a user 2 con otro controlador.
- Req del cliente anfitrión para una clave road asignada.
- 2005 al anfitrión con restore 0 de una clave road; debe ser 0 porque `child_hold` lo retiene.
- Fallo SEH en lectura o limpieza.

La parada no cambia la autoridad de los ya asignados; se limpian en la carga o el Logout. No hay red global ni vigilante de «primera acción en 5 s».

**Diagnóstico por clave** (anillo y resumen, sin decisiones): nacimiento, 2005 encolado, primera Req del invitado (latencia), primer 2556, X/Z en origen, baja y motivo.

**Sonda del invitado:**
- Separar el resumen en `propios/locales` (solicitante = personaje local y controlador = usuario local), `propios/remotos` y `ajenos`.
- Hasta entonces, `viajeros activos/desplazados` no sirve como criterio (§2.2).

### 4.8. Riesgos

| Riesgo | Valoración y mitigación |
|---|---|
| Doble control | Un único `+0x90` escrito antes del 2005; no hay cambios en vivo. El cliente del anfitrión recibe el actor como remoto. El servidor comprueba controlador == sesión en `0x2A78CA0` (S33 estático). Parada ante cualquier Req del anfitrión para esas claves. |
| Anfitrión se acerca | Ver §4.6: nada. El anfitrión ve sus 2697/2991. Riesgo de cierre `0x1831598` en el anfitrión cubierto por la retención por controlador de `child_hold`; vigilar `g_ctl_host_2005_r0`-equivalente = 0. Coste: tipo 6 en restore 0 invisible en el anfitrión. |
| Viajeros propios del anfitrión | Excluidos por `req_key`. Se cuentan como `HostRequester`. |
| NPC de misión en carreteras | Nacen de stages/misiones con otros descriptores: `0x5B04CB0` u otros, sin `+0x3CD=1` ni `+0x3C8` = user 2. Quedan como `NotRoad`. En probe, registrar descriptores con marca y vtable inesperada (no asignar). `quest_policy` no cambia: el progreso sigue en el anfitrión. |
| Viajeros hostiles / combate | **[H]** No hay ninguno en la muestra («Stranger», «Freesword», «Fisherman», «Herbalist», animales). Si 2550 genera hostiles, su IA de combate correría en el invitado. Auditar `combat_follow` (modo cliente +C8) antes de ampliar y probar combate contra un viajero. |
| Familia partida por tipo 7 | §4.5: medir y luego asignar por `dock_parent`. Hasta entonces los gimmicks de las caravanas pueden rechazar eventos en el invitado. |
| Densidad | Sin cambio: ya hoy cada cliente pide sus propios viajeros. |
| Coste en el hook | Una lectura de vtable por creación ajena (miles por minuto). Mismo patrón y salida temprana que U-e. |
| Cierre del invitado `0x1831598` (G2b) | `r13 = B01004BF`, una cabra tipo 6 de stage, controlador anfitrión y sin metadatos de carretera: **no es un viajero**. Es el sitio aún sin cubrir de `s33-npc-implementacion.md` §«Cierre de Shadow». **[H]** Con más actores controlados localmente en el invitado bajaría la exposición a actores remotos sin acción, pero esta pieza no lo arregla. |

### 4.9. Pruebas

**Tests puros** (en `optd_user2_stages` o un `optd_user2_road` nuevo):
1. `road_verdict`, tabla de verdad completa:
   - Sin marca, vtable distinta o solicitante no 0xA… → `NotRoad`.
   - Anfitrión → `HostRequester`, nunca `Assign`.
   - Otro jugador → `OtherRequester`.
   - Puntero distinto → `PtrMismatch`.
   - `old_ctl`, `desc+0x40`, `actor+0xA0`, campo ajeno, `eax ≠ field+0x180`, tipos 4/7, `key2=0`, `session_key2` distinto o `stopped` → su veredicto.
   - Tipos 3/5/6 completos → `Assign`.
2. Excluyente con U-e: un descriptor `0x5B04CB0` bajo timer nunca da `Assign` de carretera, y uno `0x5B04BE8` marcado nunca da `Ctl::Assign`.
3. Lector de descriptor sobre memoria sintética: offsets `0x3B8/0x3C8/0x3CD`, vtable relativa a base y lecturas fallidas → `NotRoad`, sin escritura.
4. Selección de limpieza:
   - `kRoadItsLoad` y `kRoadItsLogout` eligen las claves road vivas.
   - `kCleanAfterDrain`, `kCleanUnaccepted` y el drenaje en la preparación del host **nunca** las eligen.
   - `ctl_stats` nunca las cuenta como «unaccepted» y no pueden disparar `CtlAbort::Unaccepted`.
5. `road_check_2005`:
   - Al invitado con `key2` → ok.
   - Con otro controlador → `g_road_stopped`.
   - Al anfitrión con restore 0 → contador y parada.
6. Familia: raíz + 3 acompañantes marcados → 4 `Assign`. Tipo 7 con `dock_parent` asignado → `Assign` sólo cuando se habilite; con `dock_parent` no road → sin cambio.
7. `parse_mode(road, true)`: el anfitrión acepta `1`; el invitado lo degrada a probe.
8. `npc_probe_client`: el resumen separa propios/ajenos y locales/remotos. Un viajero del anfitrión que se mueve no cuenta como «propio desplazado».
9. Reutilización de clave y nueva época empiezan sin marca. Tabla llena → no se asigna (contado).

**Un PC + `cdmp_peer`** (`user2_road_control=probe`), sin Shadow:
- Valida el lector con los **viajeros del propio anfitrión**: `HostRequester` frente a 2005 con `fin+88 = A0100001`, 1:1 por clave, incluidos acompañantes.
- Cuenta descriptores con marca y vtable inesperada (debe ser 0).
- Coexistencia con `docked_gimmicks` y U-e en el hook.
- El peer no ejecuta IA: no sirve para locomoción.

**Shadow:**
1. **Probe, misma zona que S34 paso2b, anfitrión quieto a ≥60 celdas durante ≥60 s.**
   - `Assign` («would») = cohorte 2005 del invitado por clave, todos los tipos, 0 discrepancias.
   - Cotejar `+0x3D0..+0x3E8` con carretera/sub del 2005.
   - Medir tipo 7 (`dock_parent`) y opcode.
2. **Modo 1, anfitrión lejos ≥60 s.** Copia de partidas en `backups/` antes (CLAUDE.md).
   - El invitado envía Req para ≥90 % de las claves road en ≤3 s desde su 2005.
   - X/Z >1 m en la captura del invitado (`d==1` 2223/2556) para los tipos 3/5 que caminan, frente a S34 paso2b (0/27).
   - En el anfitrión: 0 Req del cliente anfitrión para esas claves, 0 2005 con otro controlador, 0 restore 0 publicados al anfitrión, sin cierre.
3. **Acercamiento del anfitrión.**
   - El controlador no cambia; el anfitrión recibe 2697 con la misma trayectoria.
   - Contar `WithheldUser2` de las claves road.
4. **Viaje rápido del invitado:** `road_cleanup` las retira (motivo 25) sin restos en SAM.
5. **Logout del invitado:** ídem; luego, reconexión.

Sin repetir la prueba en el mismo arranque cambiando dos claves a la vez.

## 5. Límites de este informe

- La procedencia del descriptor es **[E]** (decompilado y desensamblado). Su correspondencia 1:1 con la cohorte del 2005 no se ha medido en vivo: la sonda S34 no lee el descriptor. Es la primera medida.
- La condición nativa que impide al cliente lejano iniciar la locomoción sigue sin aislar ([H] navegación/streaming). El diseño no depende de ella: pone la autoridad donde la zona está cargada.
- No se ha verificado si `thunk 0x1501C9D20` crea los actores síncronamente. El diseño no lo necesita, pero el registro `opcode` de la sonda lo dirá.
- Tipos 5, 6 y 7 con controlador invitado: sin evidencia todavía.
