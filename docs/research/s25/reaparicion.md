# Reaparición del invitado: pantalla negra para siempre (S27, paso 1)

12 de octubre de 2026, rama `opcion-d` (HEAD `0732911`). Build del juego **1.0.0.2976** (PE `0x6AB28F00`, base
`0x140000000`), direcciones en RVA. Investigación de solo lectura: sin tocar `src/`, sin compilar y sin abrir el juego.

- **[V]** comprobado (captura, log del mod, o desensamblado/decompilado de esta build, citado al lado).
- **[E]** deducción fuerte que falta medir en vivo.
- **[H]** hipótesis.

## Fuentes y relojes

| Id | Fichero | Qué |
|---|---|---|
| G | `…\scratchpad\s27-shadow\cdmp_packets_20261012-210724\` (`cdmp_20261012-210705.log` + `cdmp_packets_20261012-210724.{bin,log}`) | invitado, paso 1 (Kliff `0xA010033E`, usuario `0x9010033D`). **Aquí están la muerte y la reaparición** |
| G2 | `…\scratchpad\s27-shadow\cdmp_packets_20261012-213205\` | invitado, paso 2 (`0xA0100377`). **No hay muerte del invitado**: los 14 `NotifyHandleDeadAck` tienen víctima `0xB…` y el invitado (o el anfitrión) como asesino. Su guardado final funcionó (`gen8 … STORED`) |
| H | `C:\Emulador CD\backups\p8-sesion27-host\cdmp_20261012-220452.log` + `cdmp_packets_20261012-220503.{bin,log}` | anfitrión, paso 1 (ini `cdmp.p8-s27-host-paso1.ini`: `observer_milestone_reload=1`, `user2_fast_travel=1`, `user2_travel_probe=1`, `observer_hold_family=1`) |
| N | `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\cdmp_packets_20261004-080253.log` | referencia nativa: el anfitrión solo, muere y reaparece (t=119,7 y 130,6) |

- Invitado: ini `cdmp.p8-s26-shadow.ini` (`guest_reload_wait_fix=1`, `guest_fast_travel_veto=probe`).
- **Relojes [V]:** t=0 de G = 21:07:54,882 (reloj del invitado); t=0 de H = 22:05:18,597 (reloj del anfitrión), sacados de
  las líneas `(~hh:mm:ss) capture t=`. **t(H) = t(G) + 155,06 s** (2437: 1206,758 ↔ 1361,835; 2873: 1206,922 ↔
  1361,976). Reloj del anfitrión = reloj del invitado + 59 min 58,8 s. En el texto, «t» sin más es t de G.
- Scripts (Python `-I`) en `…\scratchpad\respawn27\`: `win.py` (ventana de una captura `.log` en streaming),
  `kstr.py`/`off2rva.py` (cadena del error), `d2947e00.c`/`a2947e00.asm` (decompilado y desensamblado del 2362).

---

## 0. Resumen

1. **Dónde se para [V].** La máquina de carga del cliente del invitado (`0x7797F0`, `[P+0x15C]`) se queda en el
   **estado 0x11** desde las 21:28:02,6 hasta que el usuario cierra el juego 44 s después (`GuestReloadWait [still] …
   0x11 for 40.070 s … own M+0x231 1`). 0x11 espera `[M+0x231] ≥ 2` en el gestor de secuenciadores de **su propio**
   Kliff (`0x779E48 cmp byte [rax+0x231],2 ; jc`), y ese 2 solo lo escribe el manejador del
   **`PlayAllPreLoadSequencerAck` 2322** (`0xB87A67`). **El invitado no recibió nunca un 2322 con `3E 03 10 A0`
   tras su `PreLoadSequencerReq` 2873.** Sin salir de 0x11 no llega a 0x17, así que no manda el
   `LoadingCompleteReq` 2445: pantalla negra para siempre.
2. **Quién se llevó el 2322 [V].** El 2873 del invitado llegó al servidor del anfitrión. Su manejador
   (`0x2A5F2B0` → `.xtls 0x104D7F20`) toma el gestor del **dueño del campo** (vf48, retorno `0x104D801E`), vuelve a
   preparar los secuenciadores **del anfitrión** y le manda el 2322 a **su** Kliff. Lo confirman el log y la captura del
   anfitrión: `P8Probe [2322 path 0x287F1CC] … target actor 0xA0100001`, y su cliente recibe 12 × 2661 + 12 × 2981
   y el 2322 `01 00 10 A0` sin haber mandado ningún 2873. Es **el mismo fallo de la S15** (viaje rápido, «túnel»).
3. **Por qué el arreglo de la S16 no actuó [V].** `observer_milestone_reload=1` (descarta el 2873 y emite el 2322
   al usuario 2) **solo abre un viaje con un `ReloadStageStartReq` 2283 aceptado por F3**. La reaparición empieza con
   `ReviveLoadingReq` 2362, no con un 2283. El 2873 se juzgó contra el viaje anterior (R1, el viaje rápido de las
   21:09, ya terminado) y pasó al servidor: `trip R1: user 2's PreLoadSequencerReq 2873 #2 (loaded (2762): over …)
   -> … passes (native) | handed to the server`. La revisión de la S16 dejó pasar a propósito «una reaparición u
   otro camino sin 2283 nuevo» (`observer_milestone_reload_logic.h` líneas 25-27), para no tragarse el 2873 sin
   2322. Pero el camino nativo también deja al invitado en 0x11, porque su 2322 va al anfitrión.
4. **El servidor hizo bien la reaparición [V].** La 2362 armó el `TransformSync` del usuario 2
   (`TS_user2 before [+0x550 0x1 +0x568 0 +0x569 1 +0x591 1]`). El 2437 se respondió con la rama de recarga en el
   mismo campo `0x2BCA3D0` (2643 + 3066, ningún 2174), igual que un viaje rápido. El destino (−10751,78, 610,80,
   −3955,23) está a unos 19 m del sitio de la muerte (−10734,38, 610,92, −3948,56), en el mismo campo. Por eso el
   anfitrión ve el cuerpo allí. El propio cliente del invitado también lo tiene ya en el destino y con la vida llena
   (`Local: pos (-10751.78, 610.88, -3955.23) … hp 1125.0/1125.0` desde las 21:28:06): solo la carga está parada.
5. **El guardado [V].** Al cerrar, el mod del invitado mandó su 2910 final. **El servidor del anfitrión** lo rechazó:
   `SaveToFileCurrentPlayerAck 3016 … NOT saved (result 0x896C24B6)`. El texto del exe para ese error es
   «플레이어가 이동을 위해 로딩 상태입니다.» (`.xdata`, offset de fichero `0x5921C80`). El servidor seguía viendo al
   usuario 2 entre su 2437 y un 2445 que nunca llegó (`MOVE_ASKED (… 2437 3 … 2445 3 …)`). **Se perdieron los
   ~21 min del paso 1:** el paso 2 continuó desde `gen7`.
6. **Arreglo (solo anfitrión, sin ganchos nuevos):** que un viaje de recarga del usuario 2 se abra en **su 2437**,
   venga de donde venga (2283, 2362 u otro camino), y no solo con el 2283 aceptado por F3. Se clasifica como hoy
   (2643/3066 sin 2174 = recarga). Clave nueva `[session2] observer_milestone_reload_any = 0 | probe | 1` (§5). Se
   prueba en un PC con un guion de `cdmp_peer` que muere (`KillCheatReq`) y reaparece (§6). Shadow, al final (§7).

## 1. Cronología

### 1.1 La reaparición (G y H)

| t (G) | Invitado | t (H) | Hecho | Marca |
|---|---|---|---|---|
| 1155,450 | 21:27:10,33 | 1310,449 | S>C `NotifyHandleDeadAck` 2530 `3E 03 10 A0 │ 3E 03 10 A0 │ 07 …` (víctima = asesino = el Kliff del invitado, motivo 7) a los dos clientes | V |
| 1205,690 | 21:28:00,57 | — | La máquina de carga sale de 0x18: `trip 4 begins (state left 0x18)`, 0x01 → 0x02. La lista de espera, vacía | V |
| 1206,700 | 21:28:01,58 | 1361,752 | C>S **2362 `ReviveLoadingReq` `02 00`**. Anfitrión: `User2Funnels vf40 … Req 2362 of user 2 … returned user 2's ServerQuestManager … from exe+0x2B03AA7` | V |
| 1206,754 | | | S>C 2993 `ReviveLoadingAck` `3E 03 10 A0 02 00 00 00 00` | V |
| 1206,758 | 21:28:01,64 | 1361,835 | C>S **2437**. Anfitrión: `WorldAnchor [clear] before 2437`; `TravelConsume trip 1 … TS_user2 before [+0x550 0x1 +0x568 0 +0x569 1 +0x591 1] … 0x2BCA3D0 (reload in the same field: 2643 + 3066 expected) \| ExecuteTroc result 0x0` | V |
| 1206,834 | | 1361,835 | S>C **2643** (68 B, FieldInfoKey 1, destino −10751,78/610,80/−3955,23; el alias del mod cambia el dueño `0x90100000` → `0x9010033D`) + **3066** | V |
| 1206,841–,857 | | | 0x06 → 0x07 (3066 visto, `P+0x290 01`) → 0x08 → 0x09 → 0x0A: `own M+0x231` 3 → 0 → **1** (lo escribe `0x77B0CE` en 0x09) | V |
| 1206,857 | | | C>S 2988 `ReviveReq` `3E 03 10 A0 02 00 00 00` | V |
| 1206,914 | | 1361,919 | S>C 3053 `NotifyHandleReviveAck` + 2475 `ReviveAck` (también al cliente del anfitrión) | V |
| 1206,922 | 21:28:01,80 | 1361,976 | C>S **2873 `PreLoadSequencerReq`** (0 B), 0x0D → 0x0E → 0x0F. Anfitrión: **`ObserverMilestone [reload] … trip R1: user 2's PreLoadSequencerReq 2873 #2 (loaded (2762): over; user 2 in the host's field (shared)) -> its reload trip is over (decided, loaded or > 45 s after its 2437): passes (native) \| handed to the server`** | V |
| — | | 1362,147–,374 | Al **cliente del anfitrión**: 2661 `PrepareSequencerAck` y 2981 de su Kliff `0xA0100001` (secuenciadores nuevos `0x22A…0x3B3`). Su cliente no había mandado ningún 2873 | V |
| — | | 1362,411 | Al **cliente del anfitrión**: **2322 `01 00 10 A0`**. `P8Probe [2322 path 0x287F1CC] … (0x287F1D4 call 0x2999D80): target actor 0xA0100001`; `ObserverStage [2322] … -> actor 0xA0100001` | V |
| 1207,132 | | | C>S 2104 `ResyncSyncSceneObjectDataForReloadReq`; 0x0F → 0x10 | V |
| 1207,718 | 21:28:02,60 | | **0x10 → 0x11 con `own M+0x231 1`** (lista de espera: 4 claves) | V |
| 1212,7 … 1247,8 | 21:28:07 … :42 | | 8 × `GuestReloadWait [still] … [P+0x15C] 0x11 for 5.006 s … 40.070 s … own M+0x231 1`. `GuestSeqPlay [state] … this client's own M+0x231 is not 3 yet \| X+0x231 3, M+0x231 1` | V |
| — | | (22:28:20,7) | `WRN TravelAck trip 1 after user 2's 2437: reload in the same field (2643/3066) \| 2643 1 3066 1 2174 0 2144 0 2762 0 2679 0 Nak 0 \| 2322 to user 2 0 \| host side effect: user 2's 2873 went to the server: the host's client got 2661 x12 2981 x12 2322 x1 within 3 s -> SIDE EFFECT` | V |
| 1252,111 | 21:28:46,99 | (22:28:45,77) | El usuario cierra. El mod del invitado retiene el adiós (2001) detrás de la 2910 final; el anfitrión: `user 2's SaveToFileCurrentPlayerReq 2910 … handed to the server` | V |
| — | 21:28:47,062 | | `SaveToFileCurrentPlayerAck 3016 … character 0xA010033E slot 100 -> NOT saved (result 0x896C24B6)`; `27 held packet(s) released` | V |
| — | | (22:28:45,8–48,8) | Anfitrión: `user 2 left after its GameLoadingStartReq (MOVE_ASKED (3093 1, 2437 3, 2174 2, 2549 2, 2144 2, 2445 3, 2762 3)): waiting up to 3000 ms for the server's MoveFieldAck 2174` → `no MoveFieldAck 2174 in 3000 ms … the 2001 as before`; 2679 normal (`0xA30CBBC8` = `eErrNoParentsRequestToLogout`) | V |

### 1.2 Referencia 1: el viaje rápido del invitado en la misma sesión (salió bien)

| t (G) | t (H) | Hecho | Marca |
|---|---|---|---|
| 85,939 | 241,028 | C>S 2283. `TravelPrepared … PREPARED: user 2's TS set, the host's untouched`; **`trip R1 begins: user 2's ReloadStageStartReq 2283 was accepted by F3`** | V |
| 87,918 / 87,993 | 243,024 | C>S 2437 → S>C 2643 + 3066. `trip R1: RELOAD (2643/3066, no 2174)` | V |
| 91,790 | 246,901 | C>S 2873 → `2873 #1 … DROPPED (the host's sequencers are left alone)` | V |
| 92,191 | 247,226 | S>C **2322 `3E 03 10 A0`** (`EMIT NOW … 2322 EMITTED to user 2`). En el invitado, 0x10 → 0x11 con **`own M+0x231 2`** y 0x11 → 0x12 en la misma pasada (92,386) | V |
| 95,445 / 95,550 | 250,563 | C>S 2445 → S>C 2762. `trip R1: LoadingCompleteAck 2762 … the reload's normal exit`; 0x17 → 0x18 | V |

### 1.3 Referencia 2: la reaparición nativa (N, el anfitrión solo)

```
130,644 C>S 2362 02 00          130,649 S>C 2993
130,754 C>S 2437                130,765 S>C 2643 (dueño = él mismo)   130,805 S>C 3066
131,238 C>S 2988                131,253 S>C 3053 + 2475
131,266 C>S 2873                131,619 S>C 2322 01 00 10 A0   (+353 ms: el suyo)
133,398 C>S 2445 00             133,413 S>C 2762
```

La secuencia del invitado es idéntica hasta el 2873. Lo único que falta es el 2322 con su clave.

### 1.4 Las tres clases de carga del usuario 2 en S27

| Clase | Respuesta del servidor al 2437 | Quién da al invitado su 2322 | S27 |
|---|---|---|---|
| Entrada / cambio de campo | 2174 → (2549) → 2144 + 3066 | `observer_milestone`, en la entrada `0x2BC84E2` (el cliente no manda 2873) | bien ×2 (2322 a t=28,665 y 58,607) |
| Recarga por viaje rápido (2283 → 2437) | 2643 + 3066 | `observer_milestone_reload`, viaje abierto por F3 | bien (t=92,191) |
| **Recarga por reaparición (2362 → 2437)** | 2643 + 3066 | **nadie**: el 2873 nativo se lo da al anfitrión | **atascado** |

## 2. Pregunta 1: la secuencia y qué hizo el mod

### 2.1 Dónde se queda el cliente [V]

```
0x779E39  mov rcx,[rdi+0xC0] ; mov rcx,[rcx]      ; Q
0x779E43  call 0x9A5BE0                            ; M = gestor del usuario [Q+0x180] (dueño del campo para el cliente)
0x779E48  cmp byte [rax+0x231],2 ; jc 0x77A1A8     ; <- aquí, M+0x231 = 1
0x779E72  call 0xA97D60(M, P+0x150)
0x779E7F  mov byte [rdi+0x15C],0x12
...
0xB87A64  call [rax+0x48] ; 0xB87A67 mov byte [rax+0x231],2   ; manejador del 2322 (0xB87890): el único que escribe 2
```

- Como el mod cambia el dueño del 2643 al usuario 2 (`user2_loading_owner_alias=1`, `Session2: user 2's
  LoadingFieldDataAck 2643 names the field owner 0x90100000 (the host); user 2's copy names user 2`), `M` es el
  `ClientSequencerStageManager` del propio Kliff del invitado, igual que en la S15 (`p8-s15-viaje-tunel.md` §2.1).
- Le falta un **`PlayAllPreLoadSequencerAck` 2322 con `3E 03 10 A0`**. En toda la captura G solo hay 2322 a
  t=28,665, 58,607 y 92,191 (los suyos) y 69,764 (el del anfitrión, copiado). Tras el 2873 de t=1206,922, ninguno.
- La comparación con el viaje 3 (el viaje rápido) lo deja claro:

  | | viaje 3 (viaje rápido, bien) | viaje 4 (reaparición) |
  |---|---|---|
  | 0x10 → 0x11 | 92,386, `own M+0x231 2` | 1207,718, `own M+0x231 1` |
  | 0x11 → 0x12 | 92,386 (misma pasada) | nunca (más de 40 s) |
  | 0x17 → 0x18 (2445) | 95,445–95,564 | — |

### 2.2 Qué hizo cada módulo

**Anfitrión:**

| Módulo | Qué hizo | Marca |
|---|---|---|
| `user2_loading_owner_alias=1` | Cambió el dueño del 2643 al usuario 2 (por diseño; por eso 0x11 mira su propio gestor) | V |
| `user2_fast_travel` (F2/F3) | No intervino: no hubo 2283. La 2362 no se filtra (correcto) | V |
| `User2Funnels` | vf40 de la 2362 → el gestor de misiones del usuario 2 (el llamador del punto de reaparición `0x2B03AA7`). Bajo las Req del usuario 2 aparecen `2873x93` en vf48 y `2873x12493` en vf40: el 2873 nativo corrió con los gestores del dueño | V |
| **`observer_milestone_reload=1`** | **Juzgó el 2873 #2 contra el viaje R1 ya cargado (`over`) y lo dejó pasar.** No abrió viaje nuevo: no hubo 2283 con F3 | V |
| `observer_seq_copy` | Copió al usuario 2 los 2661/2981 que el servidor volvió a preparar para el anfitrión, y su 2322 `01 00 10 A0`. En el invitado van a X (`GuestSeqRoute … ROUTED to the actor's own manager X`): no le sirven a M | V |
| `user2_world_anchor` | `[clear] before 2437`; espera un 2762 para volver a ponerse (`now: user 2 is loading \| held: nothing`) | V |
| child hold / `observer_load_defer` / reloj | Nada retenido (`still held 0`). La puerta del reloj no actúa: nunca hubo 2445 | V |
| Salida | Esperó 3 s un 2174 que una recarga nunca manda (`MOVE_ASKED … waiting up to 3000 ms`). Inofensivo | V |

**Invitado:**

| Módulo | Qué hizo | Marca |
|---|---|---|
| Túnel | `out 223783 packets, 0 bad, 0 dropped \| in 797772 delivered, 0 errors` | V |
| `guest_fast_travel_veto=probe` | Solo vio el viaje de las 21:09: `calls 1 … VETOED 0` | V |
| `guest_x_*_veto` | `V1 Loading in \| calls past its gate 0, VETOED 0` | V |
| `guest_reload_wait_fix=1` | No llegó a actuar: la carga no pasó de 0x11 a 0x12 | V |
| Guardado propio | Retuvo el adiós detrás de la 2910 y lo soltó al llegar el rechazo (`27 held packet(s) released: the server refused the final save`) | V |

## 3. Pregunta 2: causa raíz (probada)

**Causa [V]:** un paso del servidor que solo funciona para el dueño del campo, combinado con un hueco en el arreglo
del mod para ese paso.

1. **Servidor (paso «solo para el dueño»).** El 2873 del usuario 2 se resuelve con el gestor del **dueño del campo**:
   - `0x2A5F2B0 jmp 0x104D7F20`;
   - `ServerField::vf48 0x2B00CE0` (retorno `0x104D801E`) devuelve el gestor del anfitrión;
   - `0x2895EF0` → `0x287EF60` vuelven a preparar los secuenciadores de contenido del anfitrión;
   - `0x287F1D4 call 0x2999D80` manda el 2322 *ToTarget* a `[gestor+0x18]`, el Kliff del anfitrión.

   Está en `p8-s15-viaje-tunel.md` §2.3 (estático) y en S27 en vivo: `P8Probe [2322 path 0x287F1CC] … target actor
   0xA0100001`, la ráfaga 2661/2981 + el 2322 en la captura del cliente del anfitrión, y los contadores de vf48/vf40.
2. **Mod (el hueco).**
   - `Ledger::on_f3_accepted` es el **único** sitio que abre un viaje de recarga, y lo llama `troc_detour`
     (`user2_funnels.cpp:518`) solo para un 2283 con `ft_mgr`/`ft_block`.
   - `on_2437` solo avanza un viaje en fase `Prepared`. Con R1 en `Loaded`, el 2437 de la reaparición no abrió nada.
   - El 2643/3066 se ignoraron (fase ni `Asked` ni `Reload`), y `judge` dio `Gate::Over` → el 2873 pasó al servidor.
   - Es exactamente lo que la revisión 1 de la S16 decidió para «una reaparición u otro camino sin 2283 nuevo»
     (`p8-impl-viaje-rapido.md` §12.8 punto 1): no tragarlo **sin** 2322. Pero el camino nativo tampoco da el 2322
     al invitado.
3. **La reaparición en sí está bien [V].**
   - La 2362 (`0x298C490` → `.xtls 0x1048FFC0` → `cdmp_ReviveLoading_SetPendingReload 0x2947E00`) arma el TS del
     usuario 2 (`TransformSync::SetReloadDestination` sobre `[[actor+0x68]+0x1A0]` del que reaparece).
   - Lee el gestor de etapas del dueño (`0x2947F0E mov r8d,[rbx+0x180]` → `0x2A82590` → `[[[[user+0xD0]+0x68]+0xA8]
     +0x18]`) y le pregunta por un punto de reaparición de su etapa (`0x1F759C0` bajo su cerrojo; [E] solo lectura).
   - Además pasa por `0x14C745420`, que mira la vf48 del campo.
   - El resultado lo muestra el `TravelConsume`: TS del usuario 2 armado, rama `0x2BCA3D0`, `ExecuteTroc result 0x0`.

**Descartados [V]:**

| Candidato | Veredicto | Evidencia |
|---|---|---|
| Un paquete perdido, retenido o filtrado por el mod | No | Túnel e inyector sin pérdidas (§2.2); el 2322 que falta **no se emitió nunca** para `0xA010033E` (P8Probe/ObserverStage solo ven el del anfitrión) |
| `guest_fast_travel_veto`, `guest_x_*_veto` | No | `VETOED 0` |
| Puerta del reloj sobre el 2762 | No | no hubo 2445 ni 2762 |
| `guest_reload_wait_fix` / la lista de 0x12 | No (no se llegó) | parado en 0x11 |
| Reaparición en otro campo o sector | No | 2643 sin 2174, FieldInfoKey 1, a unos 19 m de la muerte |
| Una espera del cliente como la de 0x12 | No | 0x11 espera el 2322, no la lista |
| **2873 resuelto con el gestor del dueño + viaje de recarga abierto solo por F3** | **Causa** | §3 puntos 1–2 |

**El guardado.** La 2910 llegó al servidor y este la rechazó con `0x896C24B6`. El texto del exe es el del mensaje
que vio el usuario [V]. Que la marca que lo provoca sea el estado de carga del personaje del usuario 2, abierto por
el 2437 y que solo cierra el 2445, es [E]. No hace falta arreglarlo por separado: con la carga terminada, el guardado
final vuelve a funcionar, como en el paso 2.

## 4. Pregunta 3: ¿es la misma familia?

| Episodio | Mecanismo | Relación |
|---|---|---|
| S15: viaje rápido del invitado («túnel») | 2873 → gestor del dueño → 2322 al anfitrión; invitado en 0x11 | **El mismo fallo, punto por punto.** La S16 lo arregló solo para el disparador 2283 |
| **S27: reaparición del invitado** | Lo mismo, con el disparador 2362 | **El mismo fallo** |
| S25: viaje rápido del anfitrión (`mundo-teleport.md`) | El servidor cierra las etapas de horario del personaje del anfitrión en su 2437 | Misma raíz de fondo (el campo compartido tiene **un solo dueño**, el anfitrión, y el servidor guarda por dueño etapas, secuenciadores y misiones); otro síntoma; no es una carga atascada |
| S25: volver a entrar en el mismo arranque (`invisibles.md`) | El mod del invitado solo admite una conexión remota por arranque; la segunda va a su servidor local | **Otro mecanismo.** Solo comparte el patrón: el mod cubre un camino (la primera conexión, el 2283 con F3) y cualquier otro camino cae en el comportamiento de un jugador solo |

Lo que comparten:
- En la opción D, todo lo que el servidor resuelve «por dueño del campo» responde al anfitrión cuando pregunta el
  usuario 2.
- El mod lo parchea **por camino**:
  - la entrada: `observer_milestone`;
  - el 2283: F3 + `observer_milestone_reload`;
  - misiones y botín: los embudos de `user2_funnels`.
- La reaparición era un camino que no estaba en la lista.

El arreglo propuesto quita el «por camino» para las recargas: se dispara por el estado del servidor (un 2437
respondido como recarga) y no por la Req que la empezó.

## 5. Pregunta 4: el arreglo

### 5.1 Qué y dónde (solo ANFITRIÓN, sin ganchos nuevos)

> **Sustituido por el §10 (lo implementado) y el §11 (la revisión).** Lo de abajo es el diseño previo. Lo
> implementado abre el viaje en **cada** 2437 sin viaje abierto, sin leer `+0x569` para decidir. El aviso del
> anfitrión es solo para el campo del anfitrión. La última 2283/2362 se guarda en `observer_milestone.cpp`.

**Clave nueva:** `[session2] observer_milestone_reload_any = 0 | probe | 1`. Solo anfitrión. Necesita
`observer_milestone_reload` ≠ 0 y `observer_milestone_send=1`. **No** necesita F3 para estos viajes.

| Valor | Comportamiento |
|---|---|
| 0 | Como hoy: solo abre viajes un 2283 aceptado por F3 |
| probe | Abre un viaje «en sombra» en el 2437. El 2873 sigue al servidor y las líneas dicen `WOULD DROP` / `WOULD EMIT` |
| 1 | El viaje abierto en el 2437 descarta el 2873 y emite el 2322, igual que un viaje F3 |

1. **`observer_milestone_reload_logic.h`** (lógica pura):
   - `Trip` gana `origin` (`F3_2283` / `At2437`), `shadow` (abierto con `probe`) y `arming_op` (la Req que armó el TS).
   - Método nuevo `Ledger::on_2437_any(t_us, arming_op, shadow, s)`:
     - fase `Prepared` → `on_2437` de hoy (viaje F3);
     - fase `Asked` o `Reload` (un 2437 dentro de un viaje vivo; no se ha visto nunca) → solo una nota, sin cambios;
     - si no (`None`, `FieldChange`, `Refused`, `Cancelled`, `Loaded` o decidido) → `end_trip`; viaje nuevo `R<gen>`
       con `origin = At2437`, fase `Asked` y `t_2437 = now`.
   - `judge`: si `trip_.shadow`, devuelve `WouldDrop` aunque el modo sea `On` (probe nunca descarta).
   - Todo lo demás, sin cambios:
     - la clasificación (2643/3066 sin 2174 → `Reload`; 2174 → `FieldChange`, el camino de `observer_milestone`;
       2679 → `Refused`; 2762 → `Loaded`);
     - el límite de 45 s;
     - `exit_gate` (campo del anfitrión, personaje principal, sesión 2 viva…);
     - `NativeAlready`;
     - un solo 2322 por viaje.
2. **`observer_milestone.cpp`**, `observer_milestone_reload_before(2437)`:
   - con la clave ≠ 0, llama a `on_2437_any`; si no, a `on_2437`;
   - línea de configuración al arrancar;
   - contadores en el resumen (`trips at 2437 N: reload N, field change N`).
3. **`user2_funnels.cpp` `troc_detour`** (el gancho de entrada de ExecuteTroc `0x2774B20`, ya verificado), solo
   lectura:
   - con `user2_travel_probe=1`, envolver también la **2362** en `troc_travel`, como la 2283 y la 2437:
     `TravelPrepared trip N (2362 ReviveLoadingReq): TS_user2 before/after | TS_host before/after`;
   - guardar como `arming_op` la última Req del usuario 2 tras la que `TS_user2+0x550` pasó de 0 a 1.

   Así queda medido que la reaparición arma el TS del usuario 2 y no toca el del anfitrión ([H] hoy: no hay
   `TS_host` en el `TravelConsume`).
4. **Líneas esperadas** (todas, salvo la primera, existen ya):
   - `ObserverMilestone [reload] trip R2 begins at user 2's 2437 (no F3 2283; TS armed by 2362 ReviveLoadingReq)`;
   - `trip R2: RELOAD (2643/3066, no 2174)`;
   - `2873 #N … DROPPED`;
   - `EMIT NOW … 2322 EMITTED to user 2`;
   - `trip R2: LoadingCompleteAck 2762 … the reload's normal exit`;
   - `TravelAck … 2322 to user 2 1 | host side effect: clean`.
5. **Redes solo de log, sin actuar** (no son guardas):
   - **anfitrión:** un `WRN` si un 2873 del usuario 2 pasa al servidor cuando su último 2437 se respondió con
     2643/3066 y aún no hubo 2762 («el invitado esperará en 0x11: camino sin cubrir»). Con el arreglo no debería salir
     nunca; si sale, señala el siguiente camino;
   - **invitado:** un `WRN` por viaje en `guest_reload_wait` si 0x11 dura más de 10 s en un viaje `RELOAD` con
     `own M+0x231 1` («falta el 2322 de su propio personaje»).

Por qué abrir en el 2437 y no en la 2362:
- Cubre cualquier camino que acabe en `0x2BCA3D0` (viaje rápido, reaparición, y los que no conocemos: teletransportes
  de misión, `GimmickLogoutLevelQuickRespawnGimmickReq` 2637…).
- Usa solo la clasificación por paquetes que ya está medida en vivo (S16 en un PC, S27 R1 en Shadow).
- No lee ningún valor nuevo para decidir: `+0x569` solo se registra.

### 5.2 Pruebas unitarias (`tests/optd_observer_milestone_tests.cpp`)

1. **Repetición de S27:**
   - viaje F3 R1: 2873 descartado, emisión, 2762 → `Loaded`;
   - después, `on_2437_any(arming 2362)` → R2 `Asked`; 2643/3066 → `Reload`; `judge` → `Drop`;
   - `at_exit` a +300 ms → emitir; el 2322 propio en `on_out` → `Emitted`; 2762 → `Loaded`;
   - un 2873 posterior → `Over`.
2. 2437 → 2174 → `FieldChange`; el 2873 → `NotReload` (pasa); al cerrar, resultado `field change`.
3. Clave `probe`: `WouldDrop` y decisión en seco; con la clave en 0, S27 tal cual (`Over`).
4. Un 2437 dentro de un viaje vivo no lo reinicia; una época nueva o un logout lo cancelan como hoy.

### 5.3 Riesgos

- **[E]** Descartar el 2873 de la reaparición se salta en el servidor `0x2BD1920([[rem+0x68]+0x1A0], …)`, igual que
  en el viaje rápido. El viaje rápido terminó con 2445/2762 sin él dos veces (S16 en un PC, S27 R1 en Shadow), y el
  cliente está en la misma fase (0x0D).
- **[E]** Una reaparición respondida con 2174 (otro campo) pasaría a `FieldChange`. Entonces se aplica el camino de
  entrada de `observer_milestone`, que no depende del disparador. No está medido para una reaparición.
- **[V/E]** Si el anfitrión está a la vez en su propia carga, `exit_gate` da `NotHostField`: rechazo final y el
  invitado se queda como hoy. No empeora nada y queda en el log.
- **[E] El siguiente bloqueo probable es 0x12.**
  - En el viaje 4 la lista de espera empezó vacía y tenía 4–11 claves nuevas en 0x11.
  - `guest_reload_wait_fix` solo quita las que ya estaban NOT READY al empezar el viaje (criterio 5).
  - Con `observer_hold_family=1` no debería haber jefes huérfanos. Si sale `[0x12 BLOCKER]`, queda medido.
- **[H]** Que la muerte o la 2362 escriban en el `+0x6A0` o el TS del anfitrión. No se ha visto:
  `0x2BC4950's vf48: … the owner's after a block 0`. Lo medirá la línea `TravelPrepared (2362)`.
- El efecto en el anfitrión (§8) **desaparece**: sus secuenciadores ya no se vuelven a preparar.

### 5.4 Alternativas descartadas

- **R4a de la S15** (redirigir la vf48 del 2873, retorno `0x104D801E`, al gestor del usuario 2, para que el servidor
  emita su 2322 nativo). Es más nativa, pero `0x287EF60` nunca ha corrido sobre el gestor de alguien que no es el
  dueño [H], y las ~100 vf48 y ~12 000 vf40 internas seguirían dando los del dueño. Haría falta antes una sonda.
- **Fabricar el 2322 en el invitado o escribir `M+0x231=2`:** descartado en la S15.
- **Quitar el alias del 2643:** descartado en la S15 (cambia la vista de dueño del cliente a mitad de sesión).
- **Reinyectar el 2873 más tarde:** su 2322 iría otra vez al anfitrión (§12.8 de `p8-impl-viaje-rapido.md`).

## 6. Verificación en un PC (anfitrión real + `cdmp_peer`)

> **Corregido al implementar (§10) [V código]:** el `KillCheatReq` del peer **nunca llega** al servidor del anfitrión.
> `server.cpp` bloquea para el túnel cualquier Req cuya clase contenga «Cheat» (también «ForDev», «Debug», «ByGm»,
> «Test» y los guardados). El guion que se implementó reaparece **sin morir antes**. El guion de abajo queda como
> registro del plan.

**¿Puede un guion hacer morir y reaparecer al usuario 2?**
- Morir: `KillCheatReq` 2279 (8 B: `u32 víctima │ u32 0`) es la muerte nativa probada sobre el propio jugador
  (L1, motivo `0x10`; `pieza4-dano-servidor.md`, `pendiente-reaccion-muerte-invitado.md` §3). Para el usuario 2, el
  permiso de trucos (`[[actor+0xA0]+0x68]+0x130 → vf(0x140)(2)`) es **[H]**.
- Reaparecer: solo son Req que el peer sabe mandar.

**Guion** (lo crea quien implemente: `docs/research/data/login_e3lc00_revive_s27.troc` = `login_e3lc00_112627.troc`
sin cambios + esta cola). Los tiempos copian la reaparición real de G:

```
# ---- P8 S27: reaparición del usuario 2 (reaparicion.md §6). El anfitrión en Hernand, el peer entra a su lado.
expect_learned USER_KEY 1000
expect_learned CHARACTER_NO 1000
expect_learned PLAYER_KEY 1000
wait 5000
# Muerte nativa (L1): u32 víctima, u32 opción 0
send S27_KillCheatReq 2279 0 0000000000000000
put u32 0 PLAYER_KEY
expect 2530 10000  # NotifyHandleDeadAck (si llega un Nak 1013: sin permiso, ver abajo)
wait 3000
send S27_ReviveLoadingReq 2362 0 0200
expect 2993 10000  # ReviveLoadingAck
send S27_GameLoadingStartReq 2437 0 -
expect 2643 10000  # LoadingFieldDataAck (recarga en el mismo campo)
expect 3066 10000  # MoveFieldCompletePostAck
send S27_ReviveReq 2988 0 0000000002000000
put u32 0 PLAYER_KEY
expect 2475 10000  # ReviveAck
wait 60
send S27_PreLoadSequencerReq 2873 0 -
wait 400
send S16_RegionInOutReq_carrier 3067 0 0000000001000100
expect 2322 5000   # el 2322 del personaje del usuario 2 (solo con observer_milestone_reload_any=1)
wait 2000
send S27_LoadingCompleteReq 2445 0 00
expect 2762 10000
wait 3000
end
```

**Orden:**
1. Pruebas unitarias.
2. `login_e3lc00_travel_s14_b.troc` como regresión (el viaje F3 sigue emitiendo).
3. El guion nuevo con `observer_milestone_reload_any=probe`.
4. El guion nuevo con `=1`.

**Éxito (con 1):**
- en el `cdmp.log` del anfitrión, `trip R2 begins at user 2's 2437 (… 2362 …)`, `2873 … DROPPED`,
  `2322 EMITTED to user 2`, `the reload's normal exit` y `TravelAck … 2322 to user 2 1 | host side effect: clean`;
- en el `--record` del peer, un 2322 con `PLAYER_KEY`;
- en la captura del cliente del anfitrión, **ninguna** ráfaga 2661/2981/2322 tras el 2873.

**Con probe:** `WOULD DROP` y `WOULD EMIT`. El guion se para en `expect 2322` (es lo esperado).

**Si no hay permiso para `KillCheatReq`:**
- (a) mandar la 2362 sin morir [H]: `0x2947E00` comprueba la acción actual y `0x14C745420` (la lista de la etapa del
  dueño vía vf48), y no se ve una comprobación de muerte. El `ReviveReq` puede dar Nak, pero la parte que importa
  (2437 → 2643/3066 → 2873 → 2322) no depende de él;
- (b) que el anfitrión mande `KillCheatReq(clave del usuario 2, 0)` desde su sesión [H].

## 7. Prueba con Shadow (lo que un PC no puede probar)

Hace falta Shadow para:
- el cliente real después de 0x11: 0x12 con las claves que llegan durante la reaparición, 0x14, 0x17 (`A+C3`);
- que la pantalla negra termine;
- el guardado final.

1. Anfitrión con `observer_milestone_reload=1` + `observer_milestone_reload_any=1` (y `user2_travel_probe=1`).
   Invitado con su ini de siempre (`guest_reload_wait_fix=1`).
2. El invitado se deja matar (caída o enemigo) y pulsa reaparecer, primero cerca del anfitrión y luego lejos.
3. **Éxito:**
   - en el invitado: `GuestReloadWait [trace] … 0x10 -> 0x11 … own M+0x231 2`, `0x11 -> 0x12`, … `0x17 -> 0x18`;
     C>S 2445 y S>C 2762 en su captura; sin pantalla negra;
   - en el anfitrión: las líneas del §6;
   - al cerrar: `SAVE_RESULT … final` (sin `NOT saved`).
4. **Regresión:**
   - un viaje rápido del invitado (viaje F3, como R1);
   - una muerte y reaparición **del anfitrión** con el invitado dentro (camino nativo del dueño; mirar la copia de
     secuenciadores en el invitado).
5. **Abortar** (volver a `observer_milestone_reload_any=probe`):
   - 0x11 de nuevo más de 10 s;
   - `[0x12 BLOCKER]` más de 60 s;
   - un 2679 no pedido;
   - un ERR del anfitrión justo tras un `2322 EMITTED`.

## 8. Efectos secundarios y pendientes menores

- **Progreso perdido [V]:** el paso 1 (≈21 min) no se guardó. El paso 2: `continues THIS save from its mailbox gen7`.
- **Efecto en el anfitrión a las 22:28:01 [V]** (el de la S15 §5):
  - sus secuenciadores de contenido se volvieron a preparar (12 + 12);
  - recibió un 2322 suyo;
  - su cliente mandó 4 `SequencerEvadeTriggerLogoutReq` 2547.

  Si el anfitrión vio escenas o NPC reiniciarse en ese momento, viene de aquí. El arreglo lo quita.
- **Salida en mitad de una recarga:** el anfitrión espera 3 s un 2174 que una recarga (2643/3066) nunca manda.
  Inofensivo; mejorable diciéndole a la lógica de salida que el viaje es una recarga.

## 9. Direcciones (build 2976)

| RVA | Qué |
|---|---|
| `0x7797F0` / `0x779E39..0x779E7F` | Máquina de carga del cliente; estado 0x11: `cmp byte [M+0x231],2` |
| `0x77B0CE` | `M+0x231 = 1` (estado 0x09) |
| `0xB87890` / `0xB87A67` | Manejador del 2322 en el cliente; `M+0x231 = 2` |
| `0x298C490` → `.xtls 0x1048FFC0` → `0x2947E00` | Servidor: `ReviveLoadingReq` 2362 → `ReviveLoading_SetPendingReload` (TS del que reaparece) |
| `0x2947F0E` / `0x2A82590` / `0x1F759C0` | La 2362 lee el gestor de etapas del dueño (`[rbx+0x180]`) para el punto de reaparición |
| `0x298C070` → `0x1794000` | Servidor: `ReviveReq` 2988 |
| `0x2A5F2B0` → `.xtls 0x104D7F20`; vf48 `0x2B00CE0` (retorno `0x104D801E`); `0x2895EF0` → `0x287EF60` → `0x287F1D4 call 0x2999D80` | Servidor: el 2873 y el 2322 al dueño |
| `0x2BCA3D0` | Servidor: respuesta del 2437 con recarga en el mismo campo (2643 + 3066) |
| `0x2774B20` | ExecuteTroc (gancho de entrada ya existente: `user2_funnels.cpp` `troc_detour`) |
| `0x2A757A0` + `0x2999D80` | Emisor del 2322 verificado (`observer_milestone`) |

## 10. Implementado (12-10, rama `opcion-d`)

Sin abrir el juego ni desplegar. `.\scripts\build.ps1` en verde (57/57 tests). Solo el **anfitrión** cambia de
comportamiento; el invitado solo gana una línea de aviso.

### 10.1 Qué hace

- **Clave nueva** `[session2] observer_milestone_reload_any = 0 | probe | 1` (anfitrión).
  - **Requisitos:** `observer_milestone_reload=probe|1` y `observer_milestone_send=1`. Si falta alguno, queda apagada
    y lo dice: `observer_milestone_reload_any=… needs …: OFF`.
  - **0:** como en la S16; solo un 2283 aceptado por F3 abre un viaje.
  - **1:** el 2437 del usuario 2 **abre un viaje de recarga si no hay ninguno abierto**, venga de donde venga (el
    2362 de la reaparición u otro camino; origen `at its 2437`).
    - La clasificación no cambia: 2643/3066 sin 2174 = recarga; 2174 = cambio de campo (nunca nuestro); 2679 =
      rechazado; 2762 = terminado.
    - En una recarga: el 2873 se descarta en el hilo de red y se emite **un** 2322 para su personaje en la salida de
      su siguiente Req (≥ 300 ms). Es el mismo emisor verificado y las mismas puertas que un viaje F3.
    - Un 2437 dentro de un viaje vivo (su 2437 sin responder, o una recarga sin decidir) solo se anota.
    - Un viaje F3 que espera su 2437 lo toma como en la S16.
  - **probe:** esos viajes solo dicen `WOULD DROP` / `WOULD EMIT`. El 2873 sigue al servidor y **el invitado se
    queda en negro igual que en la S27**.
- **Aviso en el anfitrión** (solo log; basta con `observer_milestone_reload` ≠ 0, sea cual sea `_any`). El ledger
  sigue siempre el último 2437 del usuario 2. Si un 2873 va al servidor mientras ese 2437 es una recarga sin terminar
  (2643/3066, sin 2174 ni 2762) en un campo que no es suyo, sale un **WRN**. Como mucho 16 por partida.
- **`user2_travel_probe=1`** (solo lectura): la 2362 se envuelve como la 2283. La línea `TravelRevive` lleva el TS
  del usuario 2 y el del anfitrión antes y después, y el `+0x6A0` del gestor del anfitrión. La 2362 cuenta como viaje
  nuevo de la sonda, así que su 2437 sale en el `TravelConsume` del mismo número.
- **Aviso en el invitado** (`guest_reload_wait`, solo log, fuera del presupuesto de líneas): si la carga lleva
  ≥ 10 s en 0x11, el viaje es `RELOAD (2643, no 2174)` y su propio `M+0x231` es < 2, sale un **WRN** por viaje.
- **Inis:**

  | Ini | Valor |
  |---|---|
  | `cdmp.p8-s27-host-paso1.ini` | `observer_milestone_reload_any=probe` |
  | `cdmp.p8-s25-host-activo.ini` (paso 2) | `=1` |
  | `cdmp.p8-noche-local.ini` (la prueba en un PC) | `=1` |

  Todos llevan un comentario en español.

Archivos:
- `observer_milestone_reload_logic.h` (`Origin`, `Last2437`, `on_2437_any`, la puerta `shadow` y el aviso);
- `observer_milestone.{h,cpp}` (clave, líneas, contadores);
- `user2_funnels.cpp` (última 2283/2362 del usuario 2, TS antes del 2437, envoltura del 2362);
- `travel_probe{.h,.cpp,_logic.h}`;
- `guest_reload_wait{.cpp,_logic.h}`;
- `config.{h,cpp}`, `mod.cpp`, `session2.cpp`.

No hay ganchos nuevos: se usan la entrada de ExecuteTroc `0x2774B20`, el `take_frame` de `server.cpp` y el juicio
del 2873 en `session2.cpp`, que ya existían.

### 10.2 Líneas (qué buscar en `cdmp.log`)

**Anfitrión, al arrancar:**
- `Config: P8 S16 reload (host): observer_milestone_reload '1' | P8 S27 observer_milestone_reload_any '1'`;
- `ObserverMilestone reload: observer_milestone_reload_any=1 (a trip at user 2's 2437 too): … OPENS one …`.

**Anfitrión, en una reaparición** (con 1, en este orden):
1. `TravelRevive trip N …: user 2's ReviveLoadingReq 2362 (0xA01…) | TS_user2 before [...] after [+0x550 0x1 +0x568 0
   +0x569 1 +0x591 1] | TS_host … | host manager +0x6A0 a -> a | … -> PREPARED: user 2's TS set, the host's untouched
   (+0x569: its 2437 reloads the same field)`.
2. `TravelConsume trip N … 0x2BCA3D0 (reload in the same field: 2643 + 3066 expected)`.
3. `ObserverMilestone [reload] … trip RK begins at user 2's GameLoadingStartReq 2437 with no trip open (no F3 2283):
   the load began with ReviveLoadingReq 2362 (revive) (0.0xx s before) | TS_user2 before the 2437 [+0x550 0x1 +0x568 0
   +0x569 1 +0x591 1] …`.
4. `trip RK: RELOAD (2643/3066, no 2174) after 2643 to user 2`.
5. `user 2's PreLoadSequencerReq 2873 #n (RELOAD …; user 2 in the host's field (shared)) -> DROPPED … | NOT handed to
   the server`.
6. `trip RK: EMIT NOW: 0x2A757A0 + 0x2999D80 …`, `OUR 2322 reached take_frame … -> queued` y `2322 EMITTED to user 2
   after its PreLoadSequencerReq 2873: IS in user 2's queue (x1)`.
7. `trip RK: LoadingCompleteAck 2762 to user 2 (queued), … after our 2322 | the reload's normal exit`.
8. `TravelAck trip N after user 2's 2437: reload in the same field (2643/3066) | … 2762 1 … | 2322 to user 2 1 | host
   side effect: user 2's 2873 was DROPPED (observer_milestone_reload): … -> clean`.
9. Al acabar el viaje: `trip RK ends: EMITTED … | origin at its 2437, the load began with ReviveLoadingReq 2362
   (revive)`.

**Lo que no debe salir:**
- WRN `ObserverMilestone [reload] … user 2's PreLoadSequencerReq 2873 (#n of the run) WENT TO THE SERVER during a
  reload in the host's field (… after its 2437: 2643/3066, no 2174, no 2762 yet) with no 2322 of ours (…): the
  server's 2322 goes to the field owner's character and the guest's load will wait in state 0x11`. Es exactamente lo
  que pasó en la S27. Con `probe` **sí** sale (el gate dice `probe: WOULD DROP`).
- `TravelRevive … HOST TOUCHED` (ERR).

**Resumen de 60 s:** `|| reload (1 …; any 1 (a trip at user 2's 2437 too)): trips … (… at the 2437 X: reload Y, 2437
inside a live trip Z) | 2873 of user 2 …: DROPPED …, … DURING AN UNFINISHED RELOAD W) …`.

Ojo con la numeración: con 1, la segunda carga de cada entrada del invitado (2437 → 2174) también abre un viaje en el
2437. Ese viaje termina en `field change (2174): observer_milestone's` y no hace nada. Por eso la reaparición no
suele ser R1.

**Invitado (solo aviso):** `WRN GuestReloadWait [0x11 own 2322] … trip N (RELOAD (2643, no 2174)) | [P+0x15C] 0x11 for
10.0xx s with own M+0x231 1 (0x779E48 waits for 2): NO PlayAllPreLoadSequencerAck 2322 for this client's own character
0xA01… came after its PreLoadSequencerReq 2873 …`. En la línea de estado: `0x11 without its own 2322 (warned) N`.

### 10.3 Tests (ctest)

- `optd_observer_milestone`:
  - **la S27 con sus tiempos reales**: el viaje rápido R1 (F3) y después la reaparición;
    - con 1: viaje R2 en el 2437, recarga, descarte, nada antes de 300 ms, una emisión, `Emitted`, el 2762 lo cierra,
      un 2873 posterior pasa sin aviso, y la línea de fin lleva el origen y la 2362;
    - con 0: el 2873 contra R1 terminado, `over`, y el aviso;
    - con probe: viaje en sombra, `WOULD DROP`, decisión en seco y el aviso;
    - con `observer_milestone_reload=probe`: `WOULD`;
    - sin emisor: `emitter off`, nunca descartado;
  - la clasificación de un viaje en el 2437: sin respuesta, 2174 → cambio de campo (sin aviso; el siguiente 2437 abre
    otro), 2643 a otra sesión, 2679, campo propio;
  - un 2437 dentro de un viaje vivo se conserva y uno tras la decisión abre otro; un viaje F3 toma su 2437; con 0 no
    hay viaje sin F3; una época nueva o un logout olvidan el último 2437;
  - **regresión S15/S16:** el viaje F3 da lo mismo con la clave a 0, probe o 1 (un viaje, descarte, `Emitted`, ningún
    viaje en el 2437, ningún aviso). Los tests anteriores de la S16 no se han tocado.
- `optd_reload_wait`: el aviso con los números de los viajes 3 y 4 de la S27 (5 s no, 10 s sí, `M+0x231` 2/3/`0xFF`
  no, entrada no, 0x12 no).
- `optd` (`test_revive_script_flow`): el guion nuevo contra un servidor falso, con el orden de las 16 Req, la carga
  `02 00` del 2362, la clave del peer y el tipo 2 en el 2988, el 2873 vacío a ≥ 60 ms y el portador a ≥ 400 ms. Con
  2322 termina; sin él falla en su `expect`.

### 10.4 Prueba en un PC (anfitrión real + `cdmp_peer`)

**Preparación** (como `pendientes/p8-impl-viaje-rapido.md` §12.6):
1. `.\scripts\build.ps1` y `.\scripts\deploy.ps1`.
2. `bin64\cdmp.ini` = `docs\research\ini\cdmp.p8-noche-local.ini`, que ya lleva `observer_milestone_reload_any=1`.
3. Lanzar el juego y cargar la partida del anfitrión en **Hernand**; dejarlo quieto.
4. En `cdmp.log`, comprobar `ObserverMilestone ON (SEND)`, `ObserverMilestone reload: observer_milestone_reload=1 …`
   y `observer_milestone_reload_any=1 …`.

**Comandos**, desde la raíz del repo, uno por arranque del juego (el peer entra una vez por guion). `N` = la ranura
del anfitrión, como en la S16.

1. **Regresión S16** (viaje rápido con F3):
   ```
   .\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00_travel_s14_b.troc --var ACCOUNT=1 --var SAVE_SLOT=N --record D:\cdmp-grabaciones\viaje-s16-reg-s27.bin --start-after 3000 --linger 30000 --seconds 240
   ```
   Éxito, igual que en la S16:
   - `trip RK begins: user 2's ReloadStageStartReq 2283 was accepted by F3`, `RELOAD`, `2873 #… DROPPED`,
     `2322 EMITTED to user 2`;
   - `TravelAck … 2322 to user 2 1 | host side effect: … clean`;
   - ningún `WENT TO THE SERVER during a reload`; el peer, `script done`.

   El 2437 de la entrada del peer también abrirá un viaje que acaba en `field change`. Es lo esperado.
2. **Reaparición:**
   ```
   .\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00_revive_s27.troc --var ACCOUNT=1 --var SAVE_SLOT=N --record D:\cdmp-grabaciones\reaparicion-s27-1.bin --start-after 3000 --linger 30000 --seconds 240
   ```
   > **Medido (§11.1):** contra el servidor real este guion falla en `expect 2993`, porque el servidor echa a un
   > personaje vivo que manda una 2362. Hasta que el usuario 2 pueda morir en un PC, la reaparición se prueba en
   > Shadow.

   **Éxito (cuando se pueda):**
   - en `cdmp.log`, las líneas 1–9 del §10.2;
   - el peer, `script done` (su `expect 2322 5000 u32 0 PLAYER_KEY` solo acepta el 2322 de su personaje, §11.3);
   - en `reaparicion-s27-1.bin`, un 2322 con el `PLAYER_KEY` del peer y después el 2762;
   - en la captura del cliente del anfitrión, **ninguna** ráfaga 2661/2981/2322 tras el 2873 del peer.

**Qué buscar:**
```
Select-String -Path "D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\cdmp.log" -Pattern "TravelRevive|TravelConsume|ObserverMilestone \[reload\]|TravelAck .* after user 2's|WENT TO THE SERVER|observer_milestone_reload_any"
```

**Fallos:**

| Señal | Qué significa | Qué hacer |
|---|---|---|
| El peer: `FAILED … no Ack 2993`; en el log, un Nak 1013 tras su 2362 | El servidor no acepta la reaparición de un personaje vivo [H del §6] | Un PC no puede probarlo: pasar a Shadow |
| `observer_milestone_reload_any=… needs …: OFF` | Ini incompleto | Arreglar el ini |
| WRN `WENT TO THE SERVER during a reload` | La clave no actuó | Mirar la línea de arranque y la puerta del 2873 |
| `NO 2322 FROM US: …` | Una puerta final | Igual que en la S16 (§12.6) |
| `TravelRevive … HOST TOUCHED` | La 2362 tocó el TS o el gestor del anfitrión | Parar e informar: lo cubre el riesgo [H] del §5.3 |
| ERR o cierre | — | `observer_milestone_reload_any=0` |

### 10.5 Shadow

1. **Paso 1** (`cdmp.p8-s27-host-paso1.ini`, `=probe`): **no morir** salvo, si se quiere, una vez al final.
   - Con probe la pantalla negra **sigue** (el 2873 va al servidor).
   - Si hay muerte: deben salir el viaje en el 2437 con `probe trip`, `WOULD DROP`, `WOULD EMIT`, el WRN `WENT TO THE
     SERVER …` en el anfitrión y el WRN `[0x11 own 2322]` en el invitado. Eso confirma la clasificación con el cliente
     real.
2. **Paso 2** (`cdmp.p8-s25-host-activo.ini`, `=1`):
   - el invitado se deja matar (caída o enemigo) y pulsa reaparecer, primero cerca del anfitrión y luego lejos;
   - después, **un viaje rápido** del invitado (regresión F3);
   - si se puede, **una muerte del anfitrión** con el invitado dentro (camino nativo del dueño).
   - El invitado necesita el nuevo `cdmp.asi` solo para su aviso: el arreglo es del anfitrión.

**Éxito (paso 2):**
- en el anfitrión, las líneas 1–9 del §10.2, sin `WENT TO THE SERVER` ni `SIDE EFFECT`;
- en el invitado, `GuestReloadWait [trace] … 0x10 -> 0x11 … own M+0x231 2` (o `0x11 -> 0x12` en la misma pasada) y
  después `… -> 0x18`; **ningún** `[0x11 own 2322]`;
- en su captura: S>C 2322 con su clave tras su 2873, C>S 2445 y S>C 2762; sale de la pantalla negra en el punto de
  reaparición;
- al cerrar: `SAVE_RESULT … final` (sin `NOT saved`).

**Abortar** (volver a `observer_milestone_reload_any=probe`):
- el WRN `[0x11 own 2322]` en el invitado o `WENT TO THE SERVER` en el anfitrión (el arreglo no actuó);
- `[0x12 BLOCKER]` más de 60 s;
- un 2679 no pedido;
- un ERR del anfitrión justo tras un `2322 EMITTED`;
- `TravelRevive … HOST TOUCHED`;
- un cierre.

### 10.6 Riesgos que quedan

- **[E]** El 2873 descartado se salta `0x2BD1920` en el servidor, como en el viaje rápido. Ya se probó sin él en el
  viaje rápido, pero no en una reaparición.
- **[E]** El siguiente bloqueo probable es **0x12**: las claves que llegan durante la reaparición no las puede quitar
  `guest_reload_wait_fix` (criterio 5). Se ve en `[0x12 BLOCKER]`.
- **[H]** Que el servidor acepte una 2362 de un personaje vivo: solo condiciona la prueba en un PC.
- **[E]** Con la clave a 1, cada 2437 de una entrada abre un viaje que termina en cambio de campo. Solo son líneas;
  la clasificación por 2174 es la que ya está medida.
- Una reaparición mientras el anfitrión cambia de campo sigue sin 2322 (`NotHostField`). Queda en el log; el
  invitado se quedaría como hoy.

## 11. Revisión de `d09254d` y prueba en un PC (12-10, noche)

### 11.1 La prueba en un PC

Partida del anfitrión en la ranura 1 con `cdmp.p8-noche-local.ini`; log `bin64\cdmp_20261012-234005.log`.

- **Regresión S16: pasa.** El viaje es R2 (R1 = la entrada, que acaba en cambio de campo): `RELOAD`, `2873 DROPPED`,
  `2322 EMITTED` y `TravelAck … 2322 to user 2 1`.
- **Reaparición: no se puede probar en un PC [V].** El servidor responde a la 2362 de un personaje **vivo** sin armar
  nada y **echa al usuario 2**:
  - `TravelRevive trip 1 … TS_user2 before [+0x550 0x0 …] after [+0x550 0x0 …] … ExecuteTroc result 0x0`;
  - 15 ms después, `2679 LogoutCharacterToFieldServerAck … reason 0x10 error 0xC0838680`;
  - `TravelAck trip 1 after user 2's 2362: REFUSED (2679)`;
  - el peer, `no Ack 2993`.

  Es la rama de error del manejador `.xtls 0x1048FFC0`: si `0x2947E00` devuelve un código, manda el Nak y llama a
  `ServerActor::Logout(…, 0x10, código)`. La línea `TravelRevive` ahora lo dice así: `NOT armed: … (refused: e.g. a
  living character; a 2679 follows)`.

### 11.2 ¿Puede morir el usuario 2 en un PC? (punto 7 de la revisión)

No hay ninguna vía segura hoy. Por eso la reaparición se prueba en **Shadow** (§10.5, paso 2).

| Vía | Veredicto |
|---|---|
| `KillCheatReq` 2279 desde el peer | El servidor retail sí lo maneja (`0x29BF730` → `0x2C40C00`: registro vf52, `0x17AE080(HP, −HP, motivo 0x10)`, vf54 [V decompilado]). Pero `server.cpp` lo bloquea para el túnel, como toda clase con «Cheat», y el permiso de trucos para la sesión del usuario 2 (`[[actor+0xA0]+0x68]+0x130 → vf(0x140)(2)`) está **sin medir** [H]. Una clave solo de desarrollo (por defecto 0, nunca en los inis de Shadow) que deje pasar el 2279 del usuario 2 **con la víctima = su propio personaje** tocaría `server.cpp` y `config`, que otro agente está editando ahora; se deja para cuando estén libres. Si luego da Nak 1013, ni así se podría |
| `VaryStatByAccInputReq` 2181 / `VaryStatCheatReq` 2703 | No matan: dejan al actor «fatal» o derribado («zombi», `pieza4-dano-servidor.md` §2.d; `pendiente-reaccion-muerte-invitado.md` §3). Descartado |
| `DieByRunOutFatalReq` 2321 | Solo remata a un actor ya «fatal» [E]. No hay una forma segura de llegar a ese estado |
| Caída (`ProcessFallReq` 2095) o entorno | La muerte de la S27 no fue por caída: fueron los `DoFrameEventReq` 2380 del propio invitado (asesino = él mismo, motivo 7, a t=1155,367). Reproducirlos exige su acción y su estado de combate: no es seguro. `ProcessFallReq` no aparece en la captura |
| La 2362 de un personaje vivo | El servidor la rechaza y saca al usuario 2 (§11.1) |

El guion `login_e3lc00_revive_s27.troc` se queda para la prueba con servidor falso y para cuando el usuario 2 pueda
morir en un PC.

### 11.3 Correcciones (este commit)

1. **(media) Falso aprobado del `expect 2322`.**
   - `cdmp.p8-noche-local.ini` lleva `observer_seq_copy=1` y `observer_seq_copy_2322=1`, así que la copia del 2322
     **del anfitrión** (`01 00 10 A0`, unos 0,4 s después del 2873) bastaba para pasar la expectativa aunque el arreglo
     no hiciera nada.
   - **Nuevo en el guion:** `expect OPCODE TIMEOUT TYPE OFFSET NAME` (`troc_script.{h,cpp}`) solo cuenta un Ack con el
     valor de `NAME` en `OFFSET`. Los dos guiones (`login_e3lc00_travel_s14_b.troc` y `login_e3lc00_revive_s27.troc`)
     usan `expect 2322 5000 u32 0 PLAYER_KEY`.
   - Los servidores falsos de `optd` mandan primero la copia del anfitrión: sin el 2322 del peer, el guion falla.
   - **Criterio de éxito en un PC:** un 2322 con el `PLAYER_KEY` del peer en el `.bin` y la línea `2322 EMITTED to
     user 2`, no solo `script done`.
2. **(media-baja) Viaje vivo sin límite de edad.**
   - Antes, un viaje parado en `Asked`, o una recarga sin decidir, se tragaba cualquier 2437 posterior. La reaparición
     siguiente se juzgaba con el 2437 viejo: `over` → nativo → pantalla negra.
   - Ahora solo lo absorbe si tiene menos de `kMaxAgeUs` (45 s). Si no, se cierra (contador `stale live trips
     closed`) y se abre un viaje nuevo.
3. **(baja) El WRN `WENT TO THE SERVER`** solo sale en el campo del anfitrión (`where == HostField`).
4. **Tests:**
   - cada 2873 se ofrece a su hora real de la S27, así que el límite de 45 s funciona dentro de los tests; un 2873
     tardío pasa y avisa;
   - la numeración real: R1 = el 2437 de la entrada a t=205,573 (cambio de campo), R2 = el viaje rápido F3, R3 = la
     reaparición;
   - un viaje parado en `Asked`: a 30 s se conserva y a 45 s se cierra; la reaparición de detrás se descarta y se
     emite;
   - una recarga sin decidir se cierra a los 45 s;
   - el aviso con `NoTrip` en el campo del anfitrión sí sale, y en campo propio, de otro o desconocido no; tope de 16
     líneas;
   - el `expect` con valor: un valor distinto no cuenta, el correcto sí, uno demasiado corto se ignora, el error al
     agotar el tiempo dice cuántos llegaron, y la variable aprendida se compara al llegar el Ack.
5. **Cosméticos:**
   - la última 2283/2362 del usuario 2 vive ahora en `observer_milestone.cpp`
     (`observer_milestone_reload_note_load_req`) y se olvida con una época nueva o un logout;
   - `TravelRevive` tiene su propio texto (`ARMED` / `NOT armed` / `HOST TOUCHED … during user 2's 2362`);
   - el §5.1 queda marcado como sustituido por el §10.
6. **(Para saber) `on_out` no mira a quién nombra un 2174.** Cualquier 2174 a la sesión 2 viva tras el 2437 cuenta
   como cambio de campo. Falla hacia el lado seguro: nunca se descarta un 2873.

Build y tests en un árbol aparte (`D:\cdmp-wt\revive-fix`): 57/57.
