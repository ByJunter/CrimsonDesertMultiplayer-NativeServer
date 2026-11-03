# Opción D — inventario de código y herramientas reutilizables

Repo: `<onedrive>\Documentos\ChatGPT\Crimson Desert Multiplayer` (commit 6318627).
Build del juego: 1.0.0.2976, PE ts `0x6AB28F00`, base fija `0x140000000`. Todas las direcciones son RVA.
Solo lectura: no se modificó nada del repo.

Documento de diseño ya existente que ES la opción D: `docs/research/segunda-sesion.md` (427 líneas,
estudio estático de "segunda sesión de usuario en el servidor del host", etapas E0–E6, bloqueos B1–B10).
Complemento: `docs/research/troc-dispatch.md` (flujo Req/Ack, objetos de red, 13 firmas AOB verificadas).

---

## 0. Mapa rápido del código (líneas)

| Área | Ficheros | LOC |
|---|---|---|
| Sniffer / inyección Troc | `src/mod/net/troc_sniffer.{h,cpp}` | 28 + 557 |
| Captura local (plantillas, claves propias, hooks servidor) | `src/mod/net/local_capture.{h,cpp}` | 99 + 959 |
| Puppet (copia del otro jugador por inyección S>C) | `src/mod/net/remote_puppet.{h,cpp}` | 101 + 1495 |
| Co-op glue (sesión ↔ juego) | `src/mod/net/coop.{h,cpp}` | 102 + 447 |
| Enemigos/combate espejo (específico de la opción "espejo") | `enemy_relay, guest_hits, player_hits, aggro_test, attack_probe, piece4_test, horse_diag, puppet_minimap` | ~3.900 |
| Red común | `src/common/{protocol,reliable,client_session,udp,transport,byte_ring,clock_mapper,clock,math3d,pattern}` | ~1.550 |
| Steam | `src/mod/net/steam_transport.{h,cpp}` | 70 + 399 |
| Memoria del juego | `src/mod/game/{offsets.h,safe_memory,actor_lookup,player_reader,camera_reader,rtti,game_errors}` | ~900 |
| Arranque / config / log | `src/mod/{dllmain,mod,config,log}.cpp` | 46 + 766 + 150 + 71 |
| Overlay | `src/mod/render/{dx12_hook,hud}.cpp` | 519 + 183 |
| Herramientas | `tools/{peer_sim,host,probe,dx12_test}`, `tools/scripts/*.py` | ~2.200 |
| Tests | `tests/tests.cpp` (15 tests, solo `cdmp_common`) | 675 |

CMake (`CMakeLists.txt`): `cdmp_common` (static: pattern, protocol, udp, client_session, reliable; ws2_32),
`cdmp_mod` → `cdmp.asi` (static CRT `/MT`, `/W4`, imgui v1.92.9b + safetyhook v0.7.0 vía FetchContent con Zydis),
`cdmp_peer`, `cdmp_host`, `cdmp_dx12_test`, `cdmp_probe`, `cdmp_tests` (ctest "unit").
`build_id.h` generado: hash git corto (8 hex) + dirty → `CDMP_BUILD_ID`, comparado en el handshake.

---

## 1. `troc_sniffer.cpp` — intercepción e inyección (núcleo reutilizable)

### Hooks (ambos `safetyhook::create_mid` en la ENTRADA de función, con verificación de cabecera)
| Función | RVA | Head bytes comprobados (offsets.h:74-77) | Hilo |
|---|---|---|---|
| `ClientNet::PostPacket` (C>S) | `0x35C670` | `48 89 5C 24 10 4C 89 4C 24 20 55 56 57 41` | hilo que envía (juego principal y otros) |
| `ClientNet::OnRecvPacket` (S>C) | `0x35CA00` | `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24` | NetworkThread del cliente (proc `0x35B6E0`, bucle `0x35B800`) |

Registros en la entrada: `r9` = paquete, `[rsp+0x28]` = u16 tamaño (cabecera incluida) (`kHookArgSizeStackOffset`).
PostPacket: `rcx` ClientNet, `rdx` int* res, `r8d` sesión (4 = servidor), `[rsp+0x30]` u8 canal, `[rsp+0x38]` u16 flags.
OnRecvPacket: `rcx` ClientNet, `rdx` NwSession*, `r8` ?, `r9` pkt, `[rsp+0x28]` size.

Instalación: `sniffer_install` (troc_sniffer.cpp:494) — exige base == 0x140000000 y PE ts == 0x6AB28F00, compara cabeceras,
`load_troc_names` (262: recorre instancias estáticas Troc `0x6A51AE0..0x6A82C80`, cada 0x90 B, RTTI `.?AVTrocTr*`,
kind en +0x08, opcode en +0x0C → `g_name_by_opcode`), valida 8 opcodes esperados contra RTTI (si no casan, desactiva inyección),
reserva ring de 64 MB (`VirtualAlloc`), crea los dos mid-hooks y el hilo `writer_thread` (410).
Se llama desde `late_init` (mod.cpp:569) tras `WorldSystem` != 0 y ventana visible + 5 s.

### Formato de paquete Troc
`u16 opcode | i8 slot | u16 payload_len | payload` (5 B de cabecera). Slot = índice de actor controlado
(`actor->vf(0x168)` en cliente, `ServerUserActor->vf(0xF8)(slot)` en servidor; -1/0xFF = el usuario/loads/timers).
Máx. 0x7FC6 total (buffers del juego 0x8000). Sin cifrado (AES-GCM presente pero apagado: `session+0x98`, byte +0x24 = 0).
Trama de transporte interna: `[u16 frameLen = len+0x1F][u8 canal][paquete][0x1C bytes IV+tag]`.

### Captura (pasiva)
`capture()` (85): bajo `g_lock` (SRWLOCK exclusivo, solo un memcpy) reserva en `ByteRing` (`src/common/byte_ring.h`)
un `RecordHeader` de 32 B + bytes (máx 32768) con SEH (`guarded_copy`). Si no cabe → `g_dropped++` (nunca bloquea al juego).
`writer_thread` (410): cada 10 ms drena el ring sin lock, escribe `.log/.bin`, llama a `capture_observe_c2s/s2c`
(fan-out a todos los módulos co-op), `capture_tick()`, `puppet_tick()`, `game::game_errors_drain()`; stats cada 10 s.

### Inyección C>S — `sniffer_inject_c2s` (485) + `on_post_packet` (157)
- Cola `g_c2s_queue` (deque, máx 16, `g_c2s_lock` SRW). Paquete completo con cabecera de 5 B.
- Se envía DENTRO de la siguiente llamada real a PostPacket, en su hilo: `TryAcquire` del lock, swap de la cola, y para cada uno
  `guarded_post(rcx, r8d, pkt, size, canal, flags)` llamando al original `0x35C670` (re-entrada protegida por `thread_local t_posting`).
- Canal/flags: aprendidos por opcode de llamadas reales (`g_post_args[4096]`, bit31 visto | flags<<8 | canal) o los del último paquete.
- Caduca a los 3000 ms (`kMaxC2SWaitMs`). Una excepción → `g_post_fn = nullptr` (desactivado).
- Limitación para D: va SIEMPRE a la sesión del host (r8d=4 → NwVirtualSyncSession → `ServerNet+0x20`), es decir, se
  atribuye al usuario del host. No sirve para un segundo usuario sin cambiar la sesión en el servidor.

### Inyección S>C — `on_recv_packet` (213) + `puppet_take_injection` (remote_puppet.cpp:1377)
- Antes de cada paquete real que llega al cliente, vacía la cola del puppet llamando al original `0x35CA00`
  con los mismos `rcx/rdx/r8` (ClientNet, NwSession*, arg3) y nuestro buffer (`guarded_inject`, SEH; re-entrada `t_injecting`).
- Cola: `g_queue` (deque ordenada por `due_ms`, `g_queue_lock` SRW; el NetworkThread solo hace TryAcquire),
  `enqueue_at` (177), `puppet_inject_s2c(op, payload, delay)` (950), `puppet_inject_s2c_batch` (954),
  `make_packet(op, payload)` (126, slot 0xFF). Movimiento caducado (>2000 ms) se descarta.
- Una excepción desactiva toda la inyección (`g_injection_enabled=false`).
- Limitación: solo se ejecuta cuando llega algún paquete real (latencia ligada al tráfico del servidor; en juego es continuo).
- Para D: es justo lo que el **invitado** necesitaría para meter en su cliente las Acks que vengan del servidor del host
  (`segunda-sesion.md` §4.3), sustituyendo/además de su propio servidor local.

### Lo que NO hay en código (solo en docs)
- Ningún hook en el lado servidor de la red: `ServerNet::OnRecvPacket 0x2773C20`, `NwVirtualAsyncSession::Send 0x1437740`,
  `NwVirtualSyncSession::Send 0x14380B0`, `ExecuteTroc 0x2774B20`, `ServerNet::SendPacket 0x2773870`, bucle servidor `0x27733F0`.
  Firmas AOB listas (1 coincidencia en disco, 13/13 en vivo) en `troc-dispatch.md` bloque ```sigs y `build/research/troc_dispatch_sigs.md`.
- No se desempaqueta `ScopeSenderAck` (2042, sub-paquetes; sub-dispatch cliente `0xCB4160`): ni el sniffer ni pk.py lo abren.
- Objetos: ClientNet `[exe+0x6D69170]` (+0x18 cola entrada, +0x38+i*8 sesiones, slot4 +0x58 = NwVirtualSyncSession, +0xE0 cola salida,
  +0xE8 cola hilo principal, +0x118 NetworkThread); ServerNet `[exe+0x6D69680]` (+0x18 NwVirtualAsyncSession única, +0x20 cola entrada,
  +0x38 diferidas, +0x78/+0x80 pools). NwVirtualAsyncSession vt `0x57260C0` (0xD0 B, +0x90 ServerUserActor, +0xC8 == ClientNet+0x18).
  NwVirtualSyncSession vt `0x5726248` (+0xE0 == ServerNet+0x20). ServerActorManager `exe+0x6DA67D8`.

---

## 2. Grabaciones y análisis

### Formato `cdmp_packets_<yyyymmdd-hhmmss>.bin` (troc_sniffer.cpp:314-334)
Cabecera 12 B: `"CDPK" | u32 version (2) | u32 pe_timestamp`. Registros: 24 B
`u64 t_us (QPC) | u8 dir (1 C>S, 2 S>C) | u8 pad | u16 size | u16 copied | u16 pad | u32 session (r8d en C>S, 0 en S>C) | u32 thread`
+ `copied` bytes (paquete completo con cabecera de 5 B; v1 truncaba a 1024). struct Python: `"<QBBHHHII"`.
`.log` texto: `t dir name opcode slot len | 48 B hex del payload`, `# stats 10s` por opcode, totales al final.
Se abren `_SH_DENYNO` (legibles mientras el juego corre). `[sniffer] record=0` = hooks activos sin escribir disco. 50–100 MB/sesión.

Ubicaciones: `bin64\` (hoy), `D:\cdmp-grabaciones` (76 ficheros, 3,2 GB), `captures\` (8 .bin del 3-oct, incl. **112627 y 105149 con
el login completo** usado en segunda-sesion.md §1.1), Shadow: `<onedrive>\cdmp-shadow\logs`. `captures\actorload_kliff_{0..3}.bin`.

### Scripts
- `tools/scripts/analyze_capture.py` (125): `summary | show <name|op> [N] | floats <op> | phases`; `read_capture()` reutilizable
  (devuelve `(t, dir, op, slot, plen, size, payload, thread)`), `load_names()` desde el CSV.
- `build/research/pieza4/pk.py`: clase `Cap` (mmap + índice numpy t/dir/op/off/n/trunc cacheado en `cache/<sid>.npz`),
  `sessions("today"|"old"|"all")`, `nm(op)`, `OP[name]`, helpers u32/i64/f32, `dfe_targets` (DoFrameEventReq), tabla MODLOG captura↔log.
  `is_key` usa `>>20 in (0xB01,0xA01)` (obsoleto: ver keys.h, generaciones >1).
- `build/research/session2/login_seq.py <cap> [tmax]`: primera aparición de cada (dir, op) en el arranque → secuencia de login.
- Otros (build/research raíz): `cdpe.py` (PE+RTTI+pdata, capstone en `venv/`), `xref.py` (índice RIP-rel → `xref_index.pkl`),
  `callers.py`, `cddis.py`/`lin.py`/`dumprange.py` (desensamblado anotado), `findop.py` (opcodes inmediatos en una función),
  `opscan.py` (quién construye un Ack: `mov word [..], imm16`), `troc_table.py` (→ `troc_table.{json,tsv}`),
  `walk_troc_table.py` + `probe_util.py` (lecturas vivas vía `cdmp_probe`), `mksig.py` (firma mínima única),
  `srv_keys.py` (clasifica actores servidor por clave), `vtidx.py/vtref.py`, `rtti_cache.pkl`, `rtti_hierarchy.json`.
  Volcados .asm de servidor: `srv_27725f0.asm`, `net_1436e00.asm`, `srv_movereq_2977e10.asm`, `srvmove_*.asm`, `s_*.asm`.
  `session2/`: `cli_flush.asm`, `cli_netthread.asm`, `s_2c48be0.asm` (login usuario), `s_2c48570.asm`, `s_2c3e030.asm` (login personaje),
  `s_2af8af0.asm` (cambio de sector/AOI), `targetfinders.asm`, `ctxuse.py`, `opimm.py`, `sectorobs.py`, `srvnetuse.py`, `totarget.py`.
  Dumps vivos: `server_user_actor.txt` (ServerUserActor vt `0x5B257A8`, key +0x60 = 0x90100000), `server_child_actor.txt`,
  `client_user_actor.txt`, `nwsession_dump1.txt`, `nwvirtualsync_dump1.txt`, `sam_dump1.txt`, `inst_*.txt`, `heap_net_instances.txt`,
  `refs_server_player.txt`.
- Carpetas temáticas (scripts + asm, mayormente del enfoque espejo): appearance, arrow, climb, combat, enemigos, equip2, horse/2/3,
  ids, interactions, invis, merc, mr, p_3g, p_aggro, p_attack, p_boss, p_ground, p_guest_react, p_keys (k1..k14 claves servidor, SAM ctor),
  p_proj, p_rewards, p_riders, p_robust, pausa, pieza4 (102 ficheros), pointclimb, steam (exports steam_api64).
- `tools/scripts/aob_scan_file.py` (firmas en disco, bloques ```sigs), `tools/scripts/rtti_dump.py` (RTTI completo → JSON).

### `docs/research/data/troc_messages_2976.csv` — 1.115 filas + cabecera
Columnas: `name, id (opcode dec), kind (1 Ack/Nak, 4 Req, 2 auth/login, 2147483646 timer interno), dir, domain (16 dominios),
vtable, on_receive_slot1 (vf1, casi siempre 0xb54130), handler_slot2 (vf2 = deserializa+ejecuta), handler_section, static_instance,
min_size, max_size, flags_14 (dword en +0x14: canal byte + flags u16 en +0x16), flag_10, flag_1c (servidor: 1 = diferida por cola +0x38),
flag_88, register_fn`. Reparto: 544 Ack/Nak, 486 Req, 80 timers, 5 auth. Ejemplos de login: LoginUserToFieldServerReq 2490
(vf2 0x2A21060, flags 0xff0002, diferida), LoginCharacterToFieldServerReq 3093 (0x298BA10), ConnectCryptogramToServerReq 2319
(kind interno), ConnectToServerAck 1002 (11 B fijo), LoadingCompleteReq 2445, MoveFieldCompleteReq 2549, LogoutUserToServerTranReq 9998.
También `build/research/troc_table.{tsv,json}`, `troc_messages.{csv,json}`, `troc_table_client.txt`, `troc_table_session.txt`.

---

## 3. Red, sesión, herramientas

- **protocol** (`src/common/protocol.{h,cpp}`): datagrama = cabecera 16 B (`magic "CDMP" | u16 kVersion=6 | u8 type | u8 | u32 seq | u32 send_ms`)
  + cuerpo serializado campo a campo; máx 1200 B. Tipos: Hello/Welcome (nombre, nonce, mod_build), Bye, Ping/Pong, PlayerState,
  GameEvent (EventKind 1..12, payload ≤1024), RelData/RelAck. `SpawnInfo` + `encode_spawn/decode_spawn`. Para D: se puede
  añadir un tipo "TrocFrame" (paquete Troc crudo, C>S o S>C) o mandar los Troc por el canal fiable; subir kVersion.
- **reliable** (`src/common/reliable.{h,cpp}`): `ReliableChannel` — mensajes hasta 256 KB, fragmentos de 1100 B, ventana 128,
  reenvío 250 ms, entrega exactamente-una-vez y EN ORDEN por stream, `reset()` en reconexión. Pura lógica, testeada.
  Para D es la pieza clave: los Troc C>S/S>C deben llegar en orden y sin pérdida (ActorLoadDataAck de hasta ~32 KB → ~30 fragmentos).
  Ojo throughput: login envía ~60 Acks grandes; ventana 128×1100 B ≈ 140 KB en vuelo.
- **ClientSession** (`src/common/client_session.{h,cpp}`): máquina de estados Initiator/Responder, Hello cada 1 s, Ping 1 s, timeout 5 s,
  nonce (detecta reinicio del otro), comprobación de versión y build, `send_event` (no fiable) / `send_reliable`, handlers
  `set_game_handlers`. `on_datagram` (95). Reutilizable tal cual (1 invitado).
- **Transport** (`src/common/transport.h`): interfaz `send/recv/poll/ready/status`; `PeerAddr` (Udp ip:port | Steam id64).
  `UdpTransport` (udp.cpp, loopback opcional). `SteamTransport` (steam_transport.cpp): ISteamNetworkingMessages por exports planos
  del `steam_api64.dll` del juego (sin SDK, sin callbacks, sin RunCallbacks), canal 67, acepta amigos jugando (AppID 3321460)
  por sondeo de `AcceptSessionWithUser`, envía `UnreliableNoNagle|AutoRestartBrokenSession` (la fiabilidad la pone ReliableChannel),
  cada llamada en SEH (`call/call_void`), `friends_playing()`. Límite 1200 B por mensaje (podría subirse: Steam admite 512 KB, pero
  `recv` filtra `> kMaxPacketSize`).
- **run_network** (mod.cpp:185): hilo worker; elige transporte por `[net] transport=udp|steam, role=guest|host`; host se fija al
  primer Hello; drena hasta 512 datagramas por vuelta (5 ms timeout), `session.tick`, `CoopLink::update`, HUD cada 1/send_rate,
  log de estado cada 5 s, Ctrl+F12 = marca en el log para alinear los dos PCs.
- **CoopLink** (`src/mod/net/coop.{h,cpp}`): pegamento sesión↔juego del modelo espejo (sink de eventos, spawn/despawn del puppet,
  pin de mapa, enemigos). Para D se sustituiría por un "TrocBridge" equivalente (mismo patrón: vive en el hilo de red, colas hacia hooks).
- **cdmp_peer** (`tools/peer_sim/main.cpp`): servidor UDP de relevo (`--mode relay|ghost|bot`, `--delay`, `--loss`, `--jitter`,
  `--public`); con un solo jugador devuelve el eco. Reutilizable como relevo entre dos PCs o para simular un invitado
  (habría que añadir un modo que reproduzca Req de una captura .bin, p. ej. el login de 112627).
- **cdmp_host** (`tools/host/main.cpp`): carga `cdmp.asi` en un proceso sin juego (`CDMP_TEST_HOST`), para probar arranque/red.
  `LoadLibrary` falla con rutas largas: usar `build\e2e\`.
- **cdmp_probe** (`tools/probe/main.cpp`, solo `PROCESS_VM_READ`): `info, scan, sigs, rip, read, chain, watch, dump (con RTTI), rtti,
  classes, vtables, findptr [--heap]`. Base de las etapas E0 de segunda-sesion.md (contar ServerUserActor, leer sesiones).
- **cdmp_dx12_test**: ventana D3D12 para probar el overlay sin el juego.
- **Overlay/HUD** (`render/dx12_hook.cpp`, `hud.cpp`): inline hooks safetyhook sobre Present/Present1/ResizeBuffers/ResizeBuffers1
  (funciones, no vtable) + ExecuteCommandLists para encontrar la cola; imgui; `hud_update(HudData)` (conectado, peer, rtt, remoto).
- **Config** (`src/mod/config.{h,cpp}`): `cdmp.ini` junto al .asi (se crea por defecto). Secciones [net], [steam], [overlay], [sniffer],
  [dev], [coop]. Plantillas en bin64: `cdmp.ini.prueba-*` (incl. `prueba-shadow-anfitrion`).
- **Log** (`src/mod/log.{h,cpp}`): `log::info/warn/error` con std::format, flush por línea, rotación `.1..5`.
- **dllmain.cpp**: filtra proceso (`CrimsonDesert.exe` o `CDMP_TEST_HOST`), solo crea el hilo worker; `cdmp_shutdown` exportado.
- **Scripts**: `build.ps1` (vswhere → cmake de VS, VS 17 2022 x64, build + ctest), `deploy.ps1` (copia cdmp.asi/pdb + Ultimate ASI
  Loader v9.7.4 como `winmm.dll`; `-Remove`; falla con el juego abierto), `package_kit.ps1` (kit de captura), `package_coop.ps1`
  (zip con loader, mod, ini host/invitado, cdmp_peer e instrucciones: el que se manda a Shadow).
- **Tests** (`tests/tests.cpp`): roundtrip/rechazo de protocolo, handshake/retry/ping/timeout/bye, pattern, math, byte_ring,
  mensajes coop, reliable (orden, streams), versiones, game data, clock_mapper, host/guest. Solo `cdmp_common`.

---

## 4. Memoria del juego

- `safe_memory` (`safe_read` = VirtualQuery de páginas + SEH; `read<T>`, `read_ptr` (≥0x10000), `has_vtable`).
- `actor_lookup.cpp:32 find_actor(key)`: `[exe+0x6D69190]` WorldSystem → +0x30 ClientActorManager → +0x08 tabla hash
  (+0x88 nº buckets, +0x98 buckets de 0x100 B {u32 n, pares (key, idx)}, +0xA0 nodos; nodo +0x04 key, +0x08 actor).
  "Mismo layout que el del servidor" (comentario) → reutilizable para buscar en `ServerActorManager [exe+0x6DA67D8]`.
  `actor_world_position/rotation` (TransformSync comp +0x1A0: pos +0xB4, sector i16×2 +0xC0, quat +0xA4).
- `player_reader.cpp:40`: WorldSystem → ActorManager (vt `0x55B62C8`) → +0x50 actor controlado (vt `0x5585C40`) → +0x68 componentes →
  +0x40 CharacterControl (vt `0x55B5460`) → +0x140 → +0x2C0 física (ejes +0x60/70/80, pos +0x90) + `kChunkOrigin 0x6D56500`; stats.
- `rtti.cpp`: `rtti_name_of_vtable/object`, `short_class_name`.
- `game_errors.cpp`: mid-hook del logger interno `0x35D8D0` (empieza con `jmp` a `.xtls`; si no se puede, copia el jmp a un stub cercano,
  engancha el stub y repunta 4 bytes). Ring de 32 entradas (código, severidad, texto printf, valores), drenado por el writer del sniffer.
  Muy útil en D: los rechazos del servidor (login, dueño, tamaños) aparecen aquí.
- `keys.h`: clave de actor = bit31 válido | bit30 local-cliente | bits28-29 clase (1 user, 2 player, 3 world) | bits20-27 generación |
  bits0-19 índice; cola del servidor de 0x3C8D índices; el mod usa índices ≥0x8000. `is_game_key / is_mod_key / is_class`.
- `offsets.h` (constantes más relevantes para D):
  `kWorldSystemSlot 0x6D69190`, `kWorldSystem_ActorManager 0x30`, `kActorManager_ControlledActor 0x50`, `kActor_ComponentTable 0x68`,
  `kActor_Key 0x60`, `kActor_OwnerUser 0x90`, `kServerActor_Key 0x60`, `kClientPostPacket 0x35C670`, `kClientOnRecvPacket 0x35CA00`
  (+heads), `kHookArgSizeStackOffset 0x28`, `kTrocInstancesBegin/End 0x6A51AE0/0x6A82C80`, `kTroc_Kind 0x08`, `kTroc_Opcode 0x0C`,
  opcodes (ActorLoadDataAck 2005, RemoveActorAck 2516, MoveActorReq 2556/Ack 2697, StartActionReq 2223/Ack 2991, RotateActorReq 2828/Ack 2687,
  UpdateWallUpDataAck 2893, UnequipItemAck 2280, OtherEquipItemAck 2757, OtherUnequipItemAck 3073, pins 2824/2992),
  `kServerOtherEquipWriter 0x29ED1F0` (+head), `kServerDockingWriter 0x2969BC0` (+head), `kRelayedAcks[]` (13 Acks "ToNear" con offsets
  de claves ajenas), `kLoadPositionFromEnd 154`, `kLoadRotationFromEnd 142`, `kLoadKindMount 5`, `kLoadingParkHeight 1000`,
  cámara `0x6C8CF30`, minimapa `0x9E89C0`, guardas de capas `0x1F63F70/0x2329700`, PointClimb `0x691400`, RideOn/Off `0x681C80/0x682C20`.

---

## 5. Pistas del servidor ya en código

- **Hooks de escritores servidor** (local_capture.cpp:187/230, instalados en 730, mid-hook en entrada, hilo del servidor):
  `0x29ED1F0` writer de OtherEquipItemAck (`rcx` actor servidor, key +0x60; `r8` u32* serial; `r9` vector {items*, u32 count +8},
  item 8 B {u32 itemKey, u16 slot}). Se ejecuta incluso sin destinatarios (prueba de que la lógica "a los demás" sigue viva en el servidor).
  `0x2969BC0` writer de ChangeDockingSocketByFrameEventAck (desactivado por constexpr; nunca disparó en un jugador).
  Patrón: copia con SEH a buffers fijos (sin alloc en hilo servidor), TryAcquire con spin ≤1000, drenado en el writer.
- **Cómo el mod encuentra al jugador local** (local_capture.cpp:456 `capture_observe_s2c`): de los `ActorLoadDataAck` S>C —
  payload `u16 size | 03/04 | u32 key @+3`; si key es clase User y `payload[0x0D]` = tipo (1 jugador, 5 montura) → `+0x0E` = clave del
  player (0xA...) / caballo; se ignora la carga por defecto aparcada en (0,1000,0); el body = primer actor World tras el user que contiene
  la clave del player. Guarda `g_user_key` (p. ej. 0x90100000), `g_player_key`, `g_body_key`, plantillas `g_user/g_body/g_horse`.
  API: `capture_own_user_key()` (810), `capture_own_player_key()` (815), `capture_spawn_info()` (763).
  El usuario servidor del host es `0x90100000`; segunda-sesion.md espera `0x90100001` para el segundo.
- `capture_observe_c2s` (642): filtra Req propias por clave en payload+0 (NPC IA usa los mismos opcodes), tamaños fijos
  (MoveActorReq 514 B, ts @0xD5; StartActionReq 725, ts @0x15F; RotateActorReq 176, ts @0x9C), reloj del juego.
- `remote_puppet.cpp`: demuestra que el cliente acepta un segundo usuario/jugador inyectado (user 0x9010F001, player 0xA010F001,
  body 0xB010F001), con número de personaje propio (`kPuppetCharacterNo`, offsets user +0x17/+0x43), recorte del body a su propio
  registro, RemoveActorAck `04 00 00 00 00 15 | u32 key`, recetas Req→Ack (`move_ack 257`, `start_action_ack 272`, `rotate_ack 298`,
  `wallup_ack 312`; formato en `docs/research/mirror-ack-formats.md`; el Ack se descarta si no consume exactamente payload_len).
  Hooks de contención (necesarios mientras haya copias sin acción): `0x1F63F70` y `0x2329700` (mid), `0x691400`, `0x681C80`, `0x682C20` (inline).
- `horse_diag.cpp`: mid-hooks `0x85FBA0`, `0x860D30`, `0x8646D0`, `0x2329D70` (contención de crash del caballo/copias).
- `aggro_test.cpp`: inline `0x22141E0` IsTargetable, mid `0x2217290` SetCurrentTarget, inline `0x22E0A30` CheckAIBattleType.
- `puppet_minimap.cpp`: inline `0x9E89C0`.

---

## 6. Para la opción D: reutilizar vs sustituir

**Reutilizable casi tal cual**: safetyhook + patrón "mid-hook en entrada con verificación de cabecera y SEH"; `troc_sniffer`
(captura, nombres por RTTI, ByteRing, writer, formato .bin) — ampliable a los puntos del servidor; `sniffer_inject_c2s`/`on_recv_packet`
como mecanismo de "llamar al original desde dentro del hook en el hilo correcto"; cola ordenada `enqueue_at`/`puppet_take_injection`;
`ReliableChannel`, `ClientSession`, `UdpTransport`, `SteamTransport`, `cdmp_peer`, `cdmp_probe`, `game_errors`, `safe_memory`,
`actor_lookup`, `rtti`, `keys.h`, config/log/dllmain/late_init, scripts de build/deploy/package, analizadores .bin y el CSV de opcodes.

**Hay que escribir** (nada existe en código):
1. Host: construir la 2.ª `NwVirtualAsyncSession` (0xD0, ctor `0x1436EE0`, vt `0x57260C0`, alloc del juego `0x1447EF9B0`), hook en
   `ServerNet::OnRecvPacket 0x2773C20` (o push a `ServerNet+0x20` con `0x35DF50`) para entregar Req del invitado con `r8 = sesión2`,
   hook en `NwVirtualAsyncSession::Send 0x1437740` filtrando `this == sesión2` (trama `r8+0x30`, paquete +3, len `*(u16*)trama-0x1F`,
   devolver el buffer al pool) → túnel al invitado; hooks B1 en `0x2A759B0`/`0x2A75880` (ToNearExceptOneControl/Self vaciados).
2. Invitado: hook en `NwVirtualSyncSession::Send 0x14380B0` (o en PostPacket, reutilizando el del sniffer pero BLOQUEANDO en vez de copiar)
   para desviar sus Req al host; inyección S>C ya existe. Problema: ahora mismo el hook de PostPacket es mid-hook pasivo — para
   bloquear el original hace falta inline hook (o hook en la Send virtual).
3. Un mensaje de túnel Troc en `protocol` (subir kVersion) o reutilizar RelData con un RelKind nuevo; datagramas >1200 B.
4. Puntos ciegos: `ScopeSenderAck` 2042, conexión local `1001/1002` (fabricada por el cliente en `0x35BE90`/`0x35C0A9`), login con
   cuenta u64 ≠ 0, guardado del host (`0x186F570`, `0x20688A0`, `FlushPendingSaveDataOnceTimer 0x28FC6F0 → 0x186E1A0`),
   byte modo un jugador `exe+0x6CEEDE8`.
