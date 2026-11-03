# Opción D: el servidor del anfitrión acepta al invitado como segundo jugador real

Documento de traspaso para el proyecto nuevo que va a intentar la opción D ("segunda sesión", el
"emulador como si fuera un MMO"). Escrito el 2026-10-05 a partir de todo lo investigado en este
repositorio (unos 60 informes, 21.000 líneas) y de un repaso de referencias externas.

Build del juego: **1.0.0.2976** (parche 2.03.02, PE timestamp `0x6AB28F00`, base fija
`0x140000000`). **Todas las direcciones son RVA (`exe+…`) y valen solo para esta build.**

Nivel de confianza de cada dato:
- **[V]** visto en vivo en el juego o medido en capturas de paquetes;
- **[E]** leído en el desensamblado (análisis estático), sin probar en vivo;
- **[S]** suposición razonada, sin comprobar;
- **[R]** fuente externa leída a través de resúmenes, sin contrastar con nuestro exe.

Cuando un dato tiene un informe detrás, va citado como `informe.md §n` (todos en `docs/research/`).

---

## 0. Resumen en una página

**Qué es la opción D.** Hoy el mod hace el cooperativo con "copias": cada PC tiene su propio
mundo y su propio servidor interno, y le enseña al otro jugador una marioneta movida con paquetes
inyectados. Todo lo demás (enemigos, daño, aggro, botín) se traduce a mano entre los dos mundos, y
de ahí vienen casi todos los problemas pendientes (`docs/PLAN-PENDIENTES.md`). La opción D cambia
el modelo: **el servidor interno del juego del anfitrión atiende también al cliente del
invitado**, como un servidor de MMO con dos usuarios. El motor reparte solo los enemigos, el daño
con las estadísticas de cada uno, el aggro, el botín y la visibilidad.

**Por qué es posible.** Crimson Desert nació como MMO, precuela de Black Desert Online, y el exe
lleva dentro el servidor de BDO, casi intacto [V] (`troc-sintesis.md`, `troc-bdo-y-runtime.md`).
- Cliente y servidor se hablan con **1.115 mensajes "Troc" serializados en bytes**, por un
  **loopback en memoria** y sin cifrar [V].
- El servidor tiene la maquinaria multiusuario completa:
  - sesiones (`NwVirtualAsyncSession`) y usuarios (`ServerUserActor`);
  - área de interés por sectores;
  - destinatarios `ToNear`, `ToWorld`, `ToField`…, y ningún global de "mi usuario" [E]
    (`segunda-sesion.md`).
- El login de un segundo usuario se puede hacer con los mensajes nativos del juego
  (`LoginUserToFieldServerReq`, `LoginCharacterToFieldServerReq`) [E].
- El cliente acepta un segundo usuario con su personaje: lo demostró el hito 4c y lo usa el
  cooperativo actual [V] (`experimento-4c.md`).

**Por qué es difícil.**
1. **El relevo del movimiento a los demás jugadores está vaciado** en esta build
   (`ToNearExceptOneControl`/`Self`) [E]. Se arregla con 2 hooks (`segunda-sesion.md` §5 B1).
2. El servidor solo atiende **una** sesión, fija en su bucle de red. Hay que añadir la segunda a
   mano (§4) [E].
3. **El personaje se carga del disco del anfitrión**, de una ranura de guardado. El servidor puede
   escribir el guardado del anfitrión con datos del invitado [E] (B3).
4. **El cliente del invitado tiene que hacer login en el servidor del anfitrión** en lugar del
   suyo. Es un "cliente fino" que debe completar de verdad la carga de zona, o el servidor lo
   "rescata" y lo mata [R] (§3.6). Es la parte más cara.
5. La IA de los NPC **la ejecuta un cliente, no el servidor**. Hay que saber a quién asigna el
   servidor cada NPC cuando hay dos usuarios [E] (B10).
6. Piezas clave virtualizadas por Denuvo (`.xtls`); los cierres del juego se envían a Pearl Abyss.

**Veredicto de la investigación:** *arriesgado pero factible por etapas* (`segunda-sesion.md` §6).
- Casi todo el lado del anfitrión se puede probar **con un solo PC**: un "invitado sintético"
  manda Req grabadas o fabricadas desde `cdmp_peer`.
- El lado del invitado necesita dos PC: el del usuario como anfitrión y el **Shadow PC** como
  invitado.

**Estado (2026-10-06, rama `opcion-d`):** P1-P7 y P8 programados detrás de `[session2]`
(apagado por defecto), con tests de lo que no toca el juego y revisados; **nada probado aún en el
juego**. E3b y P9 diseñados. Estado al día, decisiones y pruebas pendientes:
`docs/research/opcion-d-diario.md` y `docs/research/opcion-d-pruebas.md`. El texto de abajo es el
traspaso original (algunos datos los corrigen `opcion-d-d0.md`, `opcion-d-login.md` y
`opcion-d-e3-riesgos.md`).

**Estado del traspaso (2026-10-05):** **no hay ni una línea de código** de la opción D. Todo está en papel. Lo que ya existe
y se reutiliza está en §6.

---

## 1. Lo que hay que construir (vista general)

```
PC ANFITRIÓN (CrimsonDesert.exe + cdmp.asi)                       PC INVITADO (CrimsonDesert.exe + cdmp.asi)
┌──────────────────────────────────────────────┐                 ┌────────────────────────────────────────┐
│ Cliente A ─PostPacket→ NwVirtualSyncSession  │                 │ Cliente B (su jugador = control local) │
│   ↑            ↓                             │                 │   PostPacket / NwVirtualSyncSession    │
│   │     cola ServerNet+0x20                  │                 │     ↓ [HOOK: desviar sus Req]          │
│   │            ↓                             │   Steam / UDP   │     túnel ───────────────────────┐     │
│   │   ServerNet::OnRecvPacket 0x2773C20 ←────┼─[HOOK: sesión 2]←─────────────────────────────────┘     │
│   │      (sesión 1 = A, sesión 2 = B)        │                 │                                        │
│   │            ↓                             │                 │   ClientNet::OnRecvPacket 0x35CA00     │
│   │   servidor (ToNear, sectores, combate)   │                 │     ↑ [inyección S>C, ya existe]       │
│   │            ↓                             │                 │     │                                  │
│ NwVirtualAsyncSession::Send 0x1437740 ───────┼─[HOOK: si this == sesión 2 → túnel]──────────────────────┘
│   (sesión 1 → cola del cliente A)            │                 │ Su servidor local: sin usuario (aparcado)
└──────────────────────────────────────────────┘                 └────────────────────────────────────────┘
```

Piezas, de abajo arriba:

| # | Pieza | Lado | Dónde se explica |
|---|---|---|---|
| P1 | Sesión 2 del servidor (`NwVirtualAsyncSession`, 0xD0 B) | anfitrión | §4.1 |
| P2 | Entrada: Req del invitado → `OnRecvPacket` con la sesión 2 | anfitrión | §4.2 |
| P3 | Salida: Ack para la sesión 2 → túnel | anfitrión | §4.3 |
| P4 | Login del usuario 2 y de su personaje | anfitrión | §4.4 |
| P5 | Relevo de movimiento (arreglar los buscadores vaciados, B1) | anfitrión | §4.5 |
| P6 | Proteger el guardado del anfitrión | anfitrión | §4.6 |
| P7 | Túnel Troc fiable y ordenado entre PCs | los dos | §4.7 |
| P8 | Cliente fino: el cliente del invitado habla con el servidor remoto | invitado | §4.8 |
| P9 | Partida del invitado servida al servidor del anfitrión | los dos | §4.9 |
| P10 | Control de la IA de los NPC con dos clientes | anfitrión | §4.10 |

---

## 2. Arquitectura interna del juego (lo que sabemos)

### 2.1 Mensajes Troc [V]

Fuentes: `troc-sintesis.md`, `troc-dispatch.md`, `troc-rtti.md`, `troc-bdo-y-runtime.md`.

- **Cada mensaje es un handler estático**, no un objeto que viaje. Hay 1.115 instancias de 0x90 B
  en `.rdata`, registradas por opcode en una tabla hash (`exe+0x6A65BC8`; 41 buckets, 1.115
  entradas en vivo).
  - Reparto: 486 Req (cliente→servidor), 525 Ack y 19 Nak (servidor→cliente), 85 temporizadores
    del servidor.
  - Las 1.115 clases heredan directamente de `pa::Troc`. Su vtable tiene 3 entradas: vf0
    destructor, vf1 OnReceive (encola), **vf2 Process** (deserializa y ejecuta).
- **Campos del handler:**

  | Offset | Campo |
  |---|---|
  | `+0x08` | máscara de lado: 1 cliente, 4 servidor, 2 auth, `0x7FFFFFFE` temporizador |
  | `+0x0C` | u16 opcode (1002-9999) |
  | `+0x10` | 1 |
  | `+0x14` | canal |
  | `+0x16` | flags |
  | `+0x18`/`+0x1A` | tamaño mínimo y máximo |
  | `+0x1C` | servidor: 1 = diferida por la cola `ServerNet+0x38` |
  | `+0x21` | deshabilitado |

- **Formato de paquete:** `u16 opcode | i8 slot | u16 payload_len | payload` (5 B de cabecera).
  - `slot` = índice del actor controlado: `actor->vf(0x168)` en el cliente,
    `ServerUserActor->vf(0xF8)(slot)` en el servidor; -1/0xFF = el propio usuario.
  - Trama de transporte: `[u16 frameLen = len+0x1F][u8 canal][paquete][12 IV + 16 tag]`.
    **AES-GCM presente pero apagado** (`session+0x98`, byte `+0x24` = 0) [V].
  - El deserializador tiene que consumir **exactamente** `payload_len` bytes; si no, el paquete se
    descarta en silencio (`mirror-ack-formats.md`).
- **Los opcodes cambian entre parches.** Hay que resolverlos por nombre (RTTI o la tabla), no
  fijarlos como constantes.
- Tabla completa: `docs/research/data/troc_messages_2976.csv` (1.115 filas; columnas `name, id,
  kind, dir, domain, vtable, on_receive_slot1, handler_slot2, handler_section, static_instance,
  min_size, max_size, flags_14, flag_10, flag_1c, flag_88, register_fn`). También están
  `build/research/troc_table.{tsv,json}`, `troc_table_client.txt` y `troc_table_session.txt`.

### 2.2 Transporte en memoria y objetos de red [V]

```
REQ  juego ─(serializa)→ ClientNet::PostPacket 0x35C670 ─→ cola ClientNet+0xE0
     NetworkThread del cliente 0x35BE90 ─→ NwVirtualSyncSession::Send 0x14380B0 ─→ cola ServerNet+0x20
     hilo servidor 0x27733F0 ─→ ServerNet::OnRecvPacket 0x2773C20 ─→ Troc::Find(op,4) 0x143B2C0
       ├ síncrona (+0x1C == 0; Move, StartAction, DoFrameEvent, Rotate, ChangeAIState, AttackByCollision…)
       │   → ExecuteTroc 0x2774B20 → vf2, en el mismo hilo
       └ diferida (+0x1C == 1; todos los login…) → TrBufferPoolValue → cola ServerNet+0x38 (por usuario) → ExecuteTroc
ACK  servidor ─→ ServerNet::SendPacket 0x2773870 (o rutas inline) ─→ NwVirtualAsyncSession::Send 0x1437740
     ─→ cola ClientNet+0x18 ─→ NetworkThread ─→ ClientNet::OnRecvPacket 0x35CA00 ─→ Find(op,1) ─→ vf1 (encola)
     ─→ cola ClientNet+0xE8 ─→ hilo principal 0x35D0C0 ─→ ClientNet::ExecuteTroc 0x35D2A0 ─→ vf2
     (ScopeSenderAck 2042 agrupa sub-paquetes: sub-dispatch 0xCB4160)
```

| Objeto | Dónde | Campos útiles |
|---|---|---|
| ClientNet (sin vtable) | `[exe+0x6D69170]` | `+0x18` cola de entrada (desde el servidor), `+0x20/+0x28` pools, `+0x38+i*8` sesiones (17 slots), **slot 4 (`+0x58`) = `NwVirtualSyncSession`**, `+0xE0` cola de salida, `+0xE8` cola del hilo principal, `+0xF0` reintentos, `+0x112` byte "conectado", `+0x118` NetworkThread |
| ServerNet (sin vtable) | `[exe+0x6D69680]` | **`+0x18` la única `NwVirtualAsyncSession`**, `+0x20` cola de entrada, `+0x38` cola de diferidas, `+0x78/+0x80` pools (compartidos con ClientNet `+0x28/+0x20`), `+0x88` `ServerModuleFrameworkParameter`, `+0x90` `ServerModuleThreadStorage` |
| `NwVirtualSyncSession` (cliente→servidor) | ClientNet+0x58; vt `0x5726248` | `+0x98` crypto; **`+0xE0` == ServerNet+0x20** (comprobado en vivo); vf6 Send `0x14380B0` |
| `NwVirtualAsyncSession` (servidor→cliente) | ServerNet+0x18; vt `0x57260C0`; 0xD0 B | `+0x08` lock, `+0x18` nombre ("1"), `+0x88` `u32*` id, **`+0x90` `ServerUserActor*`**, `+0x98` crypto, `+0xA0`…, **`+0xC8` cola de salida == ClientNet+0x18** (comprobado en vivo); vf9 Send `0x1437740` |
| `ServerUserActor` | vt `0x5B257A8`; 0x218 B; 1 instancia | `+0x60` clave `0x90100000`, `+0x190` copia del nombre de sesión, **`+0x1F8` sesión** (`vf(0xE8)` = `mov rax,[rcx+0x1F8]`), `vf(0xF8)` slot→actor; hijos `ServerChildOnlyInGameActor` (vt `0x5B1E2B0`) |
| ServerActorManager (SAM) | `[exe+0x6DA67D8]`; vt `0x5B25988` | cola de índices de claves `+0xC8..+0xE0`; tabla de cuotas `+0x50`; `ServerModuleFrameworkParameter*` `+0x40`; contador de actores `+0x9C` |
| ClientActorManager | `[[exe+0x6D69190]+0x30]` | `+0x50` personaje controlado, `+0x58` `ClientUserActor` |

**Discrepancias a resolver con E0 (§5):**
- `NwVirtualAsyncSession+0xC8`: `segunda-sesion.md` y `troc-dispatch.md` dicen "cola de salida"
  y lo comprobaron en vivo. `troc-bdo-y-runtime.md` §2.2 lo llama
  `WindowsLockForOnlyEngineXXX` y pone el registro de mensajes del servidor en `+0xA0`.
- Para pasar de la sesión al `ServerUserActor`, cdtools/LiveTransmog usa `+0xA0` en un sitio y
  `+0x90` en otro. Nosotros vimos `+0x90` en vivo.
- El handler de `MoveActorReq` busca actores en `[exe+0x6D69698]` (`0x2A827B0`), no en el SAM
  `[exe+0x6DA67D8]` (`broadcast-servidor-movimiento.md` §1.1). Puede ser un envoltorio del mismo
  gestor.

**Hilos.**
- El hilo principal del juego envía las Req y ejecuta los Ack.
- El NetworkThread del cliente (`0x35B6E0`, bucle `0x35B800`) mueve las colas.
- El servidor tiene `ServerRunThread` (bucle de red `0x27733F0`), `ServerWalkThread` y
  `ServerRepeatTimerThread`. Hay otro bucle de diferidas y temporizadores en `0x27731C0` /
  `0x2774730`.
- **Un byte de TLS distingue cliente y servidor por hilo:** `gs:[0x58] → [+0x1EC] != 0` = hilo
  del servidor [E]. Elige qué copia de los globales y del reloj se usa (`hora-del-dia.md` §1.2).
  Trinity usa `+0x1F2` en otra build. **Hay que confirmarlo en vivo.**
- **Regla:** nunca ejecutar código del servidor (`ExecuteTroc`, handlers) desde un hilo propio.
  Las Req síncronas van en el hilo de red del servidor.

### 2.3 Cómo se crea hoy la única sesión y el usuario [E]

Fuente: `segunda-sesion.md` §1.

1. **Arranque del módulo servidor:** `0x2772050` → thunk `0x2775360` → `.xtls 0x100B8230`
   (`ServerModule::Init`, mutado pero legible):
   - `alloc(0xD0)`;
   - `NwSession::NwSession` `0x1436EE0(obj, 0)` (está en `.rsrc`);
   - vtables `NwAsyncSession 0x5726140` y luego `NwVirtualAsyncSession 0x57260C0`;
   - `+0xB0 = +0xB8 = 0`, `u16 +0xC0 = 0`, `+0x18 = "1"`;
   - `ServerNet+0x18 = obj`;
   - `obj+0xC8 = cola del cliente` (== ClientNet+0x18).
2. **El "connect" ocurre solo en el cliente:**
   - `ClientNet::FlushSendQueue` `0x35BE90` detecta la trama `1001` (lleva "127.0.0.1") y llama
     a `0x35C230`;
   - **fabrica** `ConnectToServerAck` 1002 y lo mete en su propio `OnRecvPacket`
     (`0x35C0A9`-`0x35C0ED`);
   - el servidor no ve ninguna conexión.
3. **Secuencia de login** (capturas `captures/cdmp_packets_20261003-112627.bin` y `-105149.bin`;
   script `build/research/session2/login_seq.py`):

| t (s) | Dir. | Mensaje |
|---|---|---|
| 0,000 | C→S | `1001` (no es Troc; lo consume el cliente) → S→C `ConnectToServerAck` 1002 (fabricado por el cliente) |
| 0,045 | C→S | `ConnectCryptogramToServerReq` 2319 (286 B, clave pública RSA DER) → `ConnectCryptogramToServerAck` 2840 (todo ceros: cifrado apagado) |
| 0,059 | C→S | **`LoginUserToFieldServerReq` 2490** (19 B: `u64 cuenta=0, u32 0, u8 0, wstring ""`) |
| 0,065–0,243 | S→C | `ServerTimeInfoAck`, `MainUserDataStartAck` (`0x90100000`), `CharacterLobbyDataAck` ("Kliff"), `MainUserDataEndAck` |
| 3,437 | C→S | **`LoginCharacterToFieldServerReq` 3093** (`u8 4, u64 0x7FFF…FF, u32 1, …`; el `u32` es la ranura de guardado [S]) |
| 3,444–3,774 | S→C | `ClearCharacterLobbyDataForSinglePlayAck` y ~60 Ack con el estado del personaje (buffs, stats, inventario, misiones, facciones, gimmicks, `PrivateActorMoveAck`…), `MainPlayerDataEndAck` |
| 4,277 | S→C | `MoveFieldAck` 2174 |
| 15,0 | C→S | `MoveFieldCompleteReq` 2549 → ráfaga de `ActorLoadDataAck` 2005… |
| 33,3 | C→S | `LoadingCompleteReq` 2445, `LoginCharacterToGameReq` 2601, `GameLoadingStartReq` 2437 |

4. **Alta del usuario** (`LoginUserToFieldServerReq::vf2` `0x2A21060` → `0x2C48BE0`):
   - `0x2A838E0` (Denuvo VM) busca un usuario con esa cuenta; si lo hay, rama de relogin/expulsión
     `0x2C17AB0`.
   - Las respuestas van **a la sesión que preguntó** (`ToTargetSendTargetFinder(TrocParams->session)`).
   - `SAM::CreateUser` `0x2A83000` → `0x2A83310`:
     - reserva 0x218 B, `CommonUserActor` `0x206A940`, vt `ServerUserActor`;
     - `0x2A845E0` (VM) da la clave;
     - un `ServerChildOnlyInGameActor` (0xD8 B, `0x28F23F0`) por hueco hijo.
   - `0x1437400` (VM) enlaza la sesión `+0x90` ↔ usuario. Después `usuario+0x1F8 = sesión`.
   - `0x2C17520` (UserLogin) manda MainUserData y el lobby.
   - **Con cuenta = 0 el segundo login encontraría al anfitrión** y entraría en la rama de
     relogin. Hay que usar una cuenta u64 ≠ 0 [S].
5. **El personaje sale del disco del anfitrión** (`LoginCharacterToFieldServerReq::vf2`
   `0x298BA10` → `0x2C3E030`):
   - si `byte [exe+0x6CEEDE8] == 1` (modo un jugador, ~47 ramas): `0x186F570(GestorGuardado, res,
     slotNo, ownerActor = usuario)` ("Start Load SaveSlot%#");
   - ranuras válidas: 0-2, 3, 100-108 y 200-207; guarda de concurrencia `[gestor+0x60]`;
   - `0x20688A0` = `processLoadDataFromFile` por componente;
   - si no, camino ODBC del MMO, que no sirve. **No tocar el byte de modo un jugador.**

### 2.4 Recepción y despacho en el servidor [E]

- El bucle `0x27733F0` hace `pop(ServerNet+0x20, timeout 1000 ms)` y luego
  `OnRecvPacket(ServerNet, &res, [ServerNet+0x18], buf+3, len-0x1F, canal)`. Es **la única
  llamada** (`0x27735D3`) y la sesión sale siempre de `ServerNet+0x18` (`0x27735C7`).
- `ServerNet::OnRecvPacket` `0x2773C20(ServerNet*, int*, NwSession* s, u8* pkt, u16 size, u8 canal)`:
  - `Troc::Find`; comprueba canal, tamaños y `troc+0x10 == 1`;
  - `user = s+0x90`: si `slot ≥ 0`, `actor = user->vf(0xF8)(slot)`; si no, `actor = user`;
  - **sin usuario** solo pasan los Troc de login (`flags(+0x16) & 1`).
- Las comprobaciones de dueño son **por sesión**: el handler exige que el usuario de la sesión sea
  `actor+0x90`.
  - `MoveActorReq` `0x2977E10`, `RotateActorReq` `0x29784C0`, `DoFrameEventReq` `0x296D130`,
    `CurrentTargetChangeReq` `0x2980740`, `CreateProjectileFromFrameEventReq` `0x2A3E000`,
    `OnHitProjectileReq` `0x2A3A1C0` [E].
  - Con el usuario 2 en la sesión 2, sus Req pasarían esos controles **de forma nativa**.
- Las claves desconocidas se ignoran en silencio, sin Nak (`pieza4-capturas.md` §1.3) [V].

### 2.5 Envío y destinatarios [E]

- `ServerNet::SendPacket` `0x2773870(…, vector<NwSession*>* dst)` copia el búfer una vez por
  sesión y llama a `sesión->vf9 Send`. Las rutas inline (`ScopeSenderAck`) también acaban en
  `vf9`, así que **el embudo único de salida es `NwVirtualAsyncSession::Send` `0x1437740`**.
- Cada escritor de Ack hace `finder->vf1(&vectorTLS)`. Si el vector queda vacío, no se serializa
  nada. Por eso los relevos "a otros jugadores" existen pero nunca salen en un jugador.

| Buscador | vtable | vf1 | Qué recoge |
|---|---|---|---|
| ToTarget | `0x5B25438` | `0x2A757F0` | una sesión (474/536 usos: la del actor) |
| ToControlUser | `0x5B252F8` | `0x2A757F0` | igual |
| **ToNear** | `0x5B25418` | `0x2A75860` → `0x2BC51E0` → **`0x2BC4F70`** | observadores del sector del actor (`TS+0x540` → `0x2AF2E60`: lista `sector+0x68` de `{ServerUserActor*, refcount}` de 16 B, número en `+0x70`, lock en `+0x38`) → `usuario+0x1F8`, más la propia |
| ToNearAndParty | `0x5B253D8` | `0x2A75AF0` → `.xtls 0x104E6420` | + grupo |
| ToWorld | `0x5B25318` | `0x2A75F00` → `.xtls 0x104EB5B0` | todos los usuarios (`0x2A83A40`) |
| ToField | `0x5B25398` | `0x2A75EF0` → `0x2AFFBF0` | usuarios del field |
| ToStage | `0x5B253B8` | `0x2A75D20` | claves de usuario de la stage |
| ToSessionList | `0x5B25338` | `0x2A75AE0` → `.xtls` | |
| **ToNearExceptOneControl** | `0x5B25358` | **`0x2A759B0`** | **nada**: pisa el vector en `0x2A759CC` |
| **ToNearExceptOneSelf** | `0x5B25378` | **`0x2A75880`** | **nada** (mismo cuerpo) |

Quién usa los dos vaciados:
- `ExceptOneControl` (por eso **no se ve moverse a nadie**):
  - `MoveActorAck` (`0x2BCD8A0` → escritor `0x2974C00`);
  - `StartActionAck` de relevo (`0x2A7A560` → `0x2963FF0`);
  - `RotateActorAck` (`0x106F3120` → `0x29750E0`);
  - `ChangeAIStateAck` (`0x2AC66D0` → `0x297CF50`);
  - `UpdateWallUpDataAck` (`0x297A4A0`);
  - `UpdateSeatDataAck`;
  - `SyncActorDataAck`;
  - `0x297AA80`.
- `ExceptOneSelf`:
  - `OtherEquipItemAck` 2757 (escritor `0x29ED1F0`, que el mod ya engancha [V]) y
    `OtherUnequipItemAck` 3073 (`0x29ED760`);
  - `ChangeDockingSocketByFrameEventAck` 2747 (`0x2969BC0`, que sale en `0x2969C20` si no hay
    destinatarios);
  - `ChangeVehiclePhysicsStateAck`, `BreakSceneObjectAck`, `0x2AC0650`.

El mapa completo de los 731 puntos de envío (clase → escritor → opcode) está en
`build/research/appearance/broadcast_scan.txt` (`broadcast_scan.py`).

### 2.6 Área de interés por sectores: viva [E]

```
SetTransform core 0x2BCD8A0 → 0x2AF8090 → 0x2AF8AF0 (el actor cambia de sector)
   si actor->vf(0x178)() (controlado por un usuario) y usuario != null:
      0x2AFEDD0 sectores viejos/nuevos ; 0x2AF3010 diferencia
      sale:  0x2AF2D80(sector, usuario) ; entra: inserta {usuario, 1} en sector+0x68 (0x40B5B0)
   observadores viejos/nuevos (0x2AF2E60 ×2) → diferencia → 0x2B000C0 aparecer / 0x2B00380 desaparecer
   (0x2B00380 mutado en .xtls 0x10650890, llama al escritor de RemoveActorAck 0x29756F0)
```

Con un segundo `ServerUserActor` que se mueva por el field, el servidor debería registrarlo como
observador y mandarle las altas y bajas de lo que tiene alrededor **sin código nuestro** [S].
- El servidor quita actores por distancia a unos **200-225 m** (motivo 25) [V].
- El servidor **no simula el mundo entero**: tiene el mismo conjunto de actores que el cliente,
  limitado al radio de streaming (125 de 128 coinciden) [V].
- **Los spawns los pide el cliente** (`ActivateSpawningPoolReq` 2424, `AutoSpawnRoadReq` 2550,
  `SceneLevelOnDemandGimmickSpawnReq` 2928, `MeshGroupAutoSpawnReq` 2809,
  `TerrainRegionVoxelSpawnReq` 2327) [V].
  - Si el invitado es un cliente del servidor del anfitrión, **su propio streaming pediría los
    spawns de su zona** [S].
  - Riesgo B5: los pools pueden depender de la escena cargada en el cliente del anfitrión.

### 2.7 Claves de actor [V]

Fuentes: `pendiente-rango-claves.md`, `src/mod/net/keys.h`.

- **Formato:**
  - bit 31: válida;
  - bit 30: clave local del cliente (siempre 0 en el servidor);
  - bits 28-29: clase (1 usuario `0x9…`, 2 jugador `0xA…`, 3 mundo `0xB…`);
  - bits 20-27: generación;
  - bits 0-19: índice.
- **Una sola cola FIFO de 15.501 (`0x3C8D`) índices**, compartida por usuarios, jugadores y
  actores del mundo (SAM `+0xC8` cabeza, `+0xCC` cola, `+0xD0` cuenta, `+0xD4` capacidad,
  `+0xE0` datos).
  - Reserva: `0x2A82BF0` (mundo), **`0x2A83310` (usuario/jugador)**, `0x2A7D9F0` (pool por
    usuario).
  - Liberación: `0x2A846B0`. El servidor busca solo por índice (`0x2A81900`).
- Las claves de un relogin salen del mismo contador (usuario `0x9010173F`, jugador `0xA0101740`).
  **El usuario 2 en el servidor del anfitrión recibiría claves que no chocan** [V].
- **Ventaja estructural de la opción D:** hay un solo espacio de claves (el del anfitrión), así
  que desaparece toda la traducción de claves entre PCs. Hoy esa traducción es fuente de muchos
  errores.
- **Cuotas** (`SAM::Initialize` `0x2A7DEA0`; tabla `SAM+0x50`, 8 entradas de 0x18 B):
  usuarios **N**, jugadores **100·N**.
  - **N no se ha extraído.** Si N = 1, la segunda reserva de usuario podría fallar
    (`eErrNoInvalidActorKey`). **Es lo primero que hay que medir (E0).**
  - Los parámetros vienen de `ServerModuleFrameworkParameter` (`SAM+0x40`, `+0x44` = 15.501;
    `+0x80…+0x98` cuotas, puestas a cero en el ctor `0x27BFBE5`).

### 2.8 Autoridad: quién manda en cada actor [E + V]

- El servidor **no simula jugadores ni NPC**: solo valida y reenvía. Move, StartAction, Rotate y
  ChangeAIState Ack solo se escriben desde los handlers de las Req. No hay ningún temporizador
  de posiciones (`broadcast-servidor-movimiento.md` §1-2).
- **Cada actor tiene un cliente controlador**: `actor+0x90` = clave del usuario que ejecuta su
  IA.
  - En un jugador, **el cliente simula todos los NPC del mundo** (modo 3, `+0x90` = usuario
    local; `MoveActorReq` de 3.393 actores distintos en una captura) [V].
  - El servidor calcula el HP, los buffs, el daño, la muerte, el botín, la experiencia y las
    misiones.
- **En el cliente, `byte actor+0xC8`** (`0x4FA530`):
  - 2 = jugador local; 1 = simulado aquí; 3 = gimmick;
  - **0 forzado si `actor+0x90` ≠ usuario local**. `vf188(5)` = "lo controlo yo".
  - Modo de movimiento `0x88E8E0`: 1 local, 2 remoto (interpolación), 3 simulado aquí.
- **Mensajes candidatos para repartir el control** (nunca estudiados):
  - `ChangeAiControlAck` 2508 (handler cliente `0xB681F0`: cambia el grafo de IA solo si
    `actor+0x90` es el usuario local);
  - `OverriedOwnershipStateTypeyAck` 2168 (handler `0xBB1640`);
  - `AiControlChangeCheatReq` 2442 (`0x29BFB80`).
  - El MMO tenía un solo controlador por NPC: `ChangeAIStateAck` va a `ExceptOneControl`.

### 2.9 Combate en el servidor [E + V]

Fuentes: `combate-coop.md`, `pieza4-*.md`, `pendiente-recompensas-progreso.md`.

- **El cliente detecta el golpe y el servidor calcula el daño.**
  - Cadena: `DoFrameEventReq` 2380 (`0x296D130`) → `0x2A896E0` valida la acción actual del
    atacante (hash, variante, índice de frame, vivo) → `ServerFrameEventAttack` `0x27D8550`.
  - **El daño es un buff:** `0x209A220` → fórmula `0x209A680` (no virtualizada; usa el ataque
    `+0xA6` del atacante y su arma) → `0x17AE080` → `0x17AC040` (muerte o "fatal").
  - El 17 % de los golpes reales al jugador vienen de un `DoFrameEventReq` con la lista de
    objetivos vacía: el servidor busca los objetivos por su cuenta.
- **Crédito de la muerte** (`0x29431A0`): `KillActor` 0x61 a **un solo jugador**, del que salen
  la experiencia, las misiones y los desafíos.
  - Una muerte hecha por el jugador de **otra sesión** (`0xA0101740`, un relogin) dio
    experiencia 9 de 9 veces [V]. Es buena señal para la opción D.
- **Botín** (`0x2942350` → `0x2B83960`): depende solo de la lista de la variante; el destinatario
  es "el usuario del field".
  - Con dos usuarios no se sabe de quién es el botín (experimento R6 de
    `bdo-emuladores-monstruos.md`).
- **Aggro:** no hay tabla numérica. Cada enemigo guarda su lista en el cliente que lo controla
  (`AddAggro 0x221D470`, requiere `vf188(5)`) y se la comunica al servidor
  (`AggroRelationListReq` 3006).
- **Bloqueador conocido que la opción D no arregla sola** [V, sonda en vivo]:
  - Los enemigos no hacen ataques normales a un objetivo con `+0xC8 = 0` (remoto). La lista de
    rango de ataque `C+0x58` solo se llena para objetivos controlados localmente, y
    `AICondition_CheckAIBattleType` (`0x22E0A30` → `.xtls 0xC5DC280`) devuelve "esperar".
  - En el cliente del anfitrión, el jugador real del invitado sigue siendo un actor remoto.
    O se traspasa el control de esos enemigos al invitado (§4.10), o se reutiliza el hook que
    ya existe en `src/mod/net/aggro_test.cpp`, que funciona [V]
    (`copia-objetivo-sin-ataques.md` §7).
- **Jefes:** cada combate es una stage de secuenciador **por usuario**, llevada por el cliente,
  con condiciones `Player.` (`pendiente-jefes.md`). El invitado no estaría en la stage del
  anfitrión, y las recompensas van al usuario del anfitrión.

### 2.10 Reloj, pausa y hora del día [V + E]

- "Ahora" en el cliente = `0x1416B80()` + `[exe+0x6D69438]`. El desfase lo escribe el handler de
  `ServerTimeInfoAck` 2188 (`0xBDF310`), que llega cada 5 s (`u32 0 | u64 ms del servidor | u64
  unix`).
  - En la opción D, si el servidor del anfitrión manda `ServerTimeInfoAck` al invitado, **el
    reloj del invitado se alinea solo** [S]. Hoy esa alineación es un problema
    (`pausa-copia-congelada.md`).
- **Los menús detienen todo el juego, incluido su reloj** [V, prueba con dos PCs del
  2026-10-05].
  - Si el anfitrión abre un menú, su servidor probablemente se para y **congela al invitado**
    [S].
  - Hay que medirlo pronto: puede obligar a impedir la pausa del servidor del anfitrión o a
    aceptar esa limitación.
- **Hora del día:** `pa::FieldTime`, con copias globales `0x146A6D218` (cliente) y `0x146A6D238`
  (servidor); `SetFieldTimeReq` 2125 / `SetFieldTimeAck` 2816 (`hora-del-dia.md`).
  - En la opción D, la hora sería la del servidor del anfitrión, pero el cliente del invitado
    calcula la suya. Habría que mandarle `SetFieldTimeAck` al conectar [S].

### 2.11 El cliente y sus suposiciones de "único usuario" [V + E]

- **Crear actores** (`ActorLoadDataAck` 2005, `0x8BD530`):
  - el actor de un usuario exige que ese usuario esté "logined" (`+0x5E & 4`); si no, sale el
    error "Actor 생성에 실패했습니다";
  - los grupos padre/hijos hacen login juntos (`0x8B7B50`, `copias-grupo-invisibles.md`);
  - el servidor manda un **preámbulo** antes de cada usuario: VaryHP, buffs,
    `RefreshMercenaryDataAck`, `JoinPartyAck`, `SetMinStatRateAck`. En la opción D lo
    generaría el propio servidor.
- **Puerta "no listo"** `0x8B53C0`: el actor necesita `+0x5E == 0x10` y `+0xCF == 1`; si no, el
  paquete va a la cola de reintentos (`ClientNet+0xF0`).
- Suposiciones de un solo usuario en el cliente:

  | Suposición | Detalle |
  |---|---|
  | `actor+0x96` | marca de "personaje local", solo se pone en `0x46B271` |
  | **`characterNo`** | `0x8AEB30` devuelve el **primer** actor con ese número. Los dos PC usan el número de un jugador `0x7FFFFFFFFFFFFFFF`; esto ya rompió la pantalla de equipo [V] (`apariencia-estado-coop.md` §5.3). **En la opción D, las tablas del servidor indexadas por `characterNo` o `itemNo` podrían chocar si los dos usan copias de la misma partida** [S, riesgo alto] |
  | Escalada libre | el sensor solo corre en el personaje local (hook `0x691400`) |
  | Montar | `RideOn`/`RideOff::Enter` fallan sin enlace jinete↔montura |
  | Minimapa | no da icono a otros jugadores |
  | Escalar salientes | `UpdateWallUpDataReq` depende de un bool, `exe+0x6D76D5C`, que nunca se escribe |
  | Caballo remoto | cierre por capa de acción nula (contenido en `src/mod/net/horse_diag.cpp`, `INFORME-CABALLO-CRASH.md`) |

- `UpdateVisiblePlayerAck` 2135: el cliente tiene handler (`0xC06720`), pero **el servidor no
  tiene escritor** [E].

---

## 3. Referencias externas

Fuentes: las notas de la investigación del 2026-10-05 (`docs/research/opcion-d-notas/`),
`bdo-emuladores-monstruos.md` y `genshin-coop-referencia.md`.

### 3.1 Nadie ha hecho la opción D en público
- Todo lo público en Crimson Desert es "cada uno en su mundo y un relevo":
  - blizz3010/CrimsonDesertCoop: sin licencia, secuestra un compañero NPC, solo referencia
    (`docs/ANALISIS-CrimsonDesertCoop.md`);
  - el post de Reddit de u/No-Context2224 del 2026-10-05 (r/CrimsonDesert/comments/1wx3hxy, que
    recogieron Insider Gaming, Wccftech y Tech4Gamers) describe exactamente nuestro mod de
    relevo.
- El CEO de Pearl Abyss dijo que el multijugador "requiere revisar estructuras de servidor" [R].

### 3.2 World Builder / cdmodkit: la referencia más útil
Repositorio Moon-yungg/crimson-desert-world-builder, MIT, commits hasta el 2026-10-04, nuestra
build; Nexus mods/3546. Lo leímos por resúmenes [R]. **Abrir sus `notes/FORMATS.md` y
`notes/GIMMICK_SPAWN.md` antes de usar nada.**
- **Inyecta Req en el servidor** llamando a `execute(handler, int* result, packet)` (vf2 del
  handler estático) **en el hilo del tick del servidor**, sin pasar por la red.
  - Hemos comprobado que sus RVAs coinciden con nuestra tabla:
    - `SpawnCharacterCheatReq` 2115 vf2 `0x29BE8C0`;
    - `GameLoadingStartReq` 2437 `0x29C59D0`;
    - `ReloadStageStartReq` 2283 instancia `0x6A7E240`;
    - `AiControlChangeCheatReq` 2442 `0x29BFB80`;
    - `EchoMoveSessionIDReq` 2624 `0x2978900`.
- **El remitente (campo +0) tiene que ser un `ServerChildOnlyInGameActor` válido.** Lo sacan
  enganchando la vf2 de `MoveActorReq`/`EchoMoveSessionIDReq`. Encaja con que el invitado
  necesita su propio usuario y personaje en el servidor.
- **Proteger el guardado:** hooks de imports (`CreateFileW/A/2`, `DeleteFileW`, `MoveFileExW`,
  `CopyFileW`…) que devuelven `ACCESS_DENIED` bajo `...\CD\save`, y no ejecutar los temporizadores
  de autoguardado (`SaveGameDataAutoTimer` 2077, `GamePlaySaveDataRepeatTimer` 2011,
  `FlushPendingSaveDataOnceTimer` 2070). **Es el patrón para P6.**
- **Lección crítica:** su "carga directa" con `ReloadStageStartReq` falla. A los ~10 s el
  servidor devuelve al jugador a (0,1000,0) y lo mata, porque el cliente nunca completó la
  recarga. **Servidor y cliente hacen un apretón de manos de carga de escena**: el cliente del
  invitado tiene que completar de verdad `MoveFieldComplete`/`LoadingComplete`.
- Creación de actores en el servidor:
  - `ServerField::vtable[17]` (`0x2AF9AE0`) filtra listas de `ICreateServerActorDesc` por motivo
    de aparición (desc `+0xA`, 41 motivos: Level 0, Sequencer 1, AutoSpawn 2-11/38-39,
    NPCSchedule 12, SceneCollectSpawn 31, EquipDocking 46…);
  - avisan de que **lo que se crea puede autoguardarse y duplicarse al recargar**.
- `reference/CrimsonDesertTelemetry/docs/WORLD_BUILDER_RESEARCH.md` tiene RVAs de la 2976:
  ResolveActorCore `0x2827A40`, RemovalLoop `0x2780450`, ActorCtor `0x28F23F0`, UuidLookup
  `0x1851010`.
- `reference/cdtools` (MIT): la cadena PlayerStatic `0x6D69680` → `+0x18` sesión → usuario →
  `+0xD0` personaje del servidor, con autorreparación por RTTI.

### 3.3 BDO (antecesor directo)
- No hay emulador abierto y legal que pase de "entrar al mundo y andar". Los filtrados (BDOEmu,
  "server files") **están descartados**.
- Lo útil:
  - la secuencia de entrada al mundo (`EnterPlayerCharacterToField → LoadField →
    LoadFieldComplete → Add* → SetReadyToPlay`), equivalente a la nuestra de §2.3;
  - las altas y bajas por área de interés con motivo (`ERemoveActorType`: OutOfRange 9,
    Logout 0, QuitPlayer 15).
- En BDO la IA de los monstruos estaba en el servidor; en Crimson Desert la ejecuta el controlador
  de cada NPC.

### 3.4 Genshin / Grasscutter (patrón de co-op con servidor del anfitrión)
- **El invitado abandona su mundo** y entra en el del anfitrión. Tiene `peer_id`, y la autoridad
  sobre cada entidad es explícita.
- Solo recibe las entidades **después de terminar de cargar** (`EnterSceneDone`); mientras carga,
  el servidor ignora sus movimientos.
- La visibilidad es la unión de las posiciones de todos los jugadores.
- Es el modelo de la opción D (`genshin-coop-referencia.md`).

### 3.5 Emuladores de BDO y proyectos análogos
Notas completas: `docs/research/opcion-d-notas/optD-web-agent.md`.
- **BDO (lista de opcodes del cliente 819, Yurdead/819-packets-xml-):**
  - mensajes `CM*`/`SM*`, con los opcodes barajados en cada build;
  - login en dos servidores (`CMLoginUserToAuthenticServer` → `SMGetWorldInformations` →
    `CMLoginUserToFieldServer`);
  - entrada al mundo (`CMEnterPlayerCharacterToField` → `SMLoadField`/`SMLoadFieldComplete` →
    `SMEnterPlayerCharacterToFieldComplete` → `CMSetReadyToPlay`);
  - otros actores (`SMAddPlayers`, `SMAddMonsters`, `SMAddNpcs`, `SMRemoveActor`);
  - **cada mensaje del cliente solo vale en ciertos estados de conexión** (CONNECTED, AUTHED,
    ENTERED). Es la misma idea que el filtro `flags & 1` de §2.4.
  - Cabecera actual: `u16 len | u8 flag | u16 opcode` por TCP.
  - No hay código de emulador público que se pueda compilar.
- **Skyrim Together:** identificadores estables que da el servidor; un dueño por actor, con
  "época" y traspaso cuando el dueño se va; el jugador remoto es un actor real del motor movido por
  animaciones e interpolado 300 ms por detrás; cada cliente guarda su partida.
- **FiveM OneSync:** el servidor guarda el estado, con migración de dueño. Su punto débil conocido
  es la desincronización al migrar el dueño una y otra vez.
- **Seamless Co-op, KenshiCoop, Outer Wilds, Fallout 76:** el anfitrión manda en el mundo y las
  misiones; partidas separadas para el invitado. **Los sistemas pensados para un solo jugador
  acaban reescribiéndose**, aunque el servidor acepte una segunda conexión.
- Lecciones para la opción D:
  - un solo dueño por actor, con época;
  - el anfitrión manda en misiones y mundo;
  - la partida del anfitrión es la de verdad y el invitado va en otra ranura;
  - contar con que habrá que tocar suposiciones de un solo jugador en el cliente (§2.11).

---

## 4. Diseño de cada pieza

### 4.1 P1: la sesión 2 en el servidor del anfitrión [E]
Mismos pasos que `0x100B8230` (`segunda-sesion.md` §2.5):
1. Reservar 0xD0 B **con el asignador del juego**, porque el juego podría liberarla al hacer
   logout: `0x1447EF9B0`, o `0x1447EFA78` si el byte TLS `+0x1FD` != 0.
2. `0x1436EE0(obj, 0)` (`NwSession::NwSession`).
3. vtable `0x57260C0`, `+0xB0 = +0xB8 = 0`, `u16 +0xC0 = 0`, `+0x18 = "2"` [S].
4. **`+0xC8` no debe apuntar a `ClientNet+0x18`**, porque si no el cliente del anfitrión
   recibiría los paquetes del invitado. Se deja en una cola que nadie lee, y el hook de P3
   intercepta antes.
5. `*(u32*)[obj+0x88]` = **0**, como la sesión 1. ~~id de sesión ≠ 0~~: era falso; con 2 el servidor
   tiraba la 2319 sin avisar (B2, `research/data/b2-ack2840.md`, decisión D-39).

No sirve duplicarla con `memcpy`: compartiría el lock, la crypto y la cola. Para desmontarla no
hay una ruta legible: el logout está virtualizado (`0x2C482E0`/`0x2C48410`, `.xtls 0x86E5500`).
Al principio, **la sesión 2 vive hasta que se cierra el juego**.

### 4.2 P2: entrada (Req del invitado → servidor)
- **Opción A (la más simple):**
  - inline hook en la entrada de `ServerNet::OnRecvPacket` `0x2773C20` (firma
    `troc_server_on_recv_packet` en `troc-dispatch.md`);
  - después de cada paquete real, vaciar nuestra cola llamando al original con `r8 = sesión 2`;
  - **problema:** si el anfitrión no manda nada, el bucle espera hasta 1 s. En la práctica el
    cliente del anfitrión manda Req continuamente (~40 Hz moviéndose), pero en un menú no manda
    ninguna.
- **Opción B (sin latencia) [S, verificar]:**
  - sacar un `PacketBuffer` del pool `ServerNet+0x78`;
  - llenarlo como `NwVirtualSyncSession::Send` (trama en `+0x8000`, longitudes en
    `+0x8008/+0x800A`, `+0x800E = 0`) con una marca propia;
  - empujarlo a `ServerNet+0x20` con `0x35DF50`;
  - en el hook, si el paquete es nuestro, cambiar `r8` por la sesión 2.
- **Opción C (World Builder):** llamar a la vf2 del handler en el hilo del servidor. **No
  vale** para la sesión 2, porque se salta `OnRecvPacket`, que es donde se resuelve el actor por
  sesión y se encolan las diferidas.
- Antes de hacer login en la sesión 2 solo pasan los Troc de login.

### 4.3 P3: salida (Ack para la sesión 2 → túnel)
Hook en la entrada de `NwVirtualAsyncSession::Send` `0x1437740` (firma
`nw_virtual_async_session_send`):
- si `this == sesión 2`:
  - copiar la trama de `r8+0x30` (paquete en `+3`, longitud `*(u16*)trama − 0x1F`) a una cola
    hacia el túnel;
  - devolver el `PacketBuffer` al pool con `0x35DF50`, como hace el NetworkThread del cliente en
  `0x35BB5C`;
  - salir con `*res = 0` sin llamar al original.
- Hay que **verificar en vivo** que el pool es el correcto (`ClientNet+0x20 == ServerNet+0x80`
  según la documentación).
- Es un **inline hook** (hay que poder no llamar al original). Está en la entrada de una función
  normal de `.rsrc`, así que lo permite la regla del proyecto.

### 4.4 P4: login del usuario 2 y de su personaje
- Inyectar por P2, con `r8 = sesión 2`:
  1. `ConnectCryptogramToServerReq` (bytes de la captura 112627);
  2. `LoginUserToFieldServerReq` con **cuenta u64 = 1** (o cualquier ≠ 0).
  - Éxito: llega a la sesión 2 un `MainUserDataStartAck` con un usuario nuevo y hay **dos**
    `ServerUserActor`.
- `LoginCharacterToFieldServerReq` con la **misma ranura** que el anfitrión: el servidor crea un
  "clon de Kliff" con la partida del anfitrión. Sirve para probar, pero no es el producto final
  (P9).
- Después, `MoveFieldCompleteReq` / `LoadingCompleteReq` / `LoginCharacterToGameReq` /
  `GameLoadingStartReq` en el orden de la captura.
- El cliente del anfitrión debería recibir por `ToNear` el `ActorLoadDataAck` del personaje del
  usuario 2 y verlo **sin copia ni marioneta**.

### 4.5 P5: relevo de movimiento (B1)
- Inline hook en la entrada de `vf1` de `ToNearExceptOneControl` (`0x2A759B0`) y
  `ToNearExceptOneSelf` (`0x2A75880`):
  - llamar a `0x2BC4F70(TS = [[finder+0x10]+0x68]+0x1A0, vec)`, que llena con los observadores
    del sector;
  - quitar la sesión del que controla (`actor->vf(0xE8)`) o la propia.
- Con eso, `MoveActorAck`, `StartActionAck`, `RotateActorAck` y `ChangeAIStateAck` llegan al otro
  usuario de forma nativa.
- **No tocar** la `vf1` cuando solo hay una sesión (debe quedar como hoy). Activarlo solo con
  `[dev]`.

### 4.6 P6: proteger el guardado del anfitrión (antes de cualquier login)
- **Copia de seguridad obligatoria** de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups/`
  antes de cada prueba.
- El guardado lo escriben componentes del servidor (p. ej. `ServerContentsMiscActorComponent`
  slot 10 `0x28B7360`; `FlushPendingSaveDataOnceTimer` `0x28FC6F0` → `0x186E1A0`). Un segundo
  `ServerUserActor` podría acabar escrito en la partida del anfitrión.
- Defensa en dos capas, como World Builder:
  1. **Bloqueo de ficheros:** hook de imports (`CreateFileW/A/2`, `MoveFileExW`, `DeleteFileW`,
     `CopyFileW`, `SetFileAttributesW`…) que devuelve `ACCESS_DENIED` bajo `...\CD\save`
     mientras haya un usuario 2.
  2. No dejar que corran `SaveAutoReq` 3037, `SaveToFileCurrentPlayerReq` 2910 ni los
     temporizadores 2077, 2011 y 2070 con el usuario 2 presente (filtro en la entrada de
     `OnRecvPacket`/`ExecuteTroc`).
- Regla para el usuario: **nunca guardar** durante las pruebas y cerrar el juego sin guardar.

### 4.7 P7: túnel Troc entre PCs
- Reutilizar `ReliableChannel` (`src/common/reliable.{h,cpp}`): mensajes de hasta 256 KB,
  fragmentos de 1.100 B, ventana de 128, entrega ordenada y exactamente una vez, con tests.
- Añadir un tipo de mensaje "TrocFrame" (paquete Troc crudo, con dirección) y **subir
  `kVersion`**.
- **Volumen:** una sesión normal mueve ~300 paquetes/s (356.000 en 19 min), y el login ~60 Ack
  grandes (`ActorLoadDataAck` de hasta ~32 KB).
  - La mayor parte del C→S son `MoveActorReq` **de los NPC que simula el cliente** (3.393 actores).
  - Con dos clientes, **el invitado no debe simular ni reportar los NPC del anfitrión** (P10),
    o el túnel y la autoridad se saturan.
- Movimiento fiable o no:
  - empezar con todo fiable, por sencillez y orden;
  - más adelante, `MoveActorReq`/`Ack` pueden ir por la vía no fiable con descarte de los
    viejos.
- Steam: `ISteamNetworkingMessages`, ya probado entre los dos PC [V].

### 4.8 P8: cliente fino del invitado (la parte más cara)
- **Desviar sus Req:** hook en `NwVirtualSyncSession::Send` `0x14380B0` del invitado (inline;
  no llamar al original), que manda la trama al túnel en vez de a la cola de su servidor local.
  - Su `1001`/`1002` sigue siendo local, porque lo fabrica el cliente.
  - Desde `ConnectCryptogramToServerReq` todo va al anfitrión.
- **Recibir:** meter los Ack del anfitrión en su `ClientNet::OnRecvPacket` `0x35CA00`. Esa
  inyección ya existe (`remote_puppet.cpp`/`troc_sniffer.cpp`).
  - Atención: hay que **impedir que su servidor local le hable**, porque si no recibiría dos
    mundos.
  - Sin login en su servidor local, ese servidor no tiene usuario y no envía nada [S].
- **Desde cuándo:** lo más limpio es desde la pantalla de título, antes de cargar partida.
  - El cliente pide "cargar ranura N"; esa Req va al anfitrión, que carga P9.
  - Toda la carga de zona (`MoveFieldAck` → `MoveFieldCompleteReq` → `ActorLoadDataAck`… →
    `LoadingCompleteReq`) ocurre contra el servidor del anfitrión, que es el apretón de manos que
    exige §3.2.
- Riesgos:
  - estados de carga;
  - pantallas de muerte y reaparición (`ReviveLoadingReq`…);
  - viaje rápido (`MoveFieldAck`);
  - pausa (§2.10);
  - `ScopeSenderAck` (sub-paquetes);
  - reconexión: si se cae el túnel, el cliente del invitado queda colgado sin servidor.
- **Plan B intermedio (híbrido) [S]:**
  - el invitado sigue en su mundo, pero sus Req de **su propio jugador** (Move, StartAction,
    DoFrameEvent…) se duplican hacia la sesión 2 del anfitrión;
  - el anfitrión ve al invitado nativo y el daño del invitado se calcula con sus estadísticas;
  - el invitado sigue viendo sus propios enemigos (como hoy);
  - sirve de escalón y de demostración, pero no es la opción D completa.

### 4.9 P9: la partida del invitado en el servidor del anfitrión
- Opciones (`segunda-sesion.md` §3):
  - **(a)** la misma ranura que el anfitrión: un clon, solo para probar;
  - **(b)** hook en la entrada de `0x186F570` o de `0x20688A0`: cuando `ownerActor ==
    usuario 2`, servir desde memoria el blob de la partida del invitado, recibido por el túnel.
    El fichero `save.save` está cifrado (magic "SAVE", entropía 8,0); haría falta pasar los
    bytes tal cual y que el cargador del juego los descifre;
  - **(c)** copiar la partida del invitado a una ranura libre del anfitrión: **peligroso**.
- `characterNo` e `itemNo` repetidos entre partidas de la misma cuenta (§2.11) [S]: medir en E3
  qué tablas del servidor se indexan por ellos.
- Progreso del invitado: con la opción D, su experiencia, botín y misiones viven en el servidor
  del anfitrión. **Para no perderlos** habría que guardar la partida del usuario 2 y devolvérsela
  al invitado. Es un problema para más adelante, pero hay que tenerlo presente desde el diseño.

### 4.10 P10: control de la IA de los NPC con dos clientes
- Hoy el cliente que carga un NPC lo simula (`+0x90` = su usuario). Con dos clientes, ¿qué hace
  el servidor? **No se sabe.**
- Primero, solo lectura:
  - registrar con el sniffer cuándo y a quién van `ChangeAiControlAck` 2508 y
    `OverriedOwnershipStateTypeyAck` 2168;
  - buscar sus escritores en el servidor (`senders.py` / `broadcast_scan`) (R2/R3 de
    `bdo-emuladores-monstruos.md` §10).
- Posibles resultados:
  - el servidor asigna el NPC al primer cliente que lo carga: el del anfitrión cerca de él, el
    del invitado si está solo;
  - o hay que asignarlo nosotros, por ejemplo con `AiControlChangeCheatReq`.
- Esto decide también el bloqueador de §2.9 (los enemigos no atacan a un objetivo remoto).

---

## 5. Plan por etapas

Cada etapa se cierra con una prueba, un commit y un apunte en el informe correspondiente. **Una
etapa que escribe en el juego se activa solo con una opción `[dev]` apagada por defecto.**

| Etapa | Qué | Cómo se prueba | Riesgo | Decide |
|---|---|---|---|---|
| **D0** | Resolver incógnitas en estático: **cuota N de usuarios** (`SAM::Initialize` `0x2A7DEA0` y `ServerModuleFrameworkParameter`), `+0x90` vs `+0xA0`, `+0xC8` de la sesión, pools, byte TLS `+0x1EC`, escritores de 2508 y 2168, asignador `0x1447EF9B0`, opción B de P2 | solo exe y capturas | ninguno | si N=1 hay que parchear la cuota |
| **E0** | Sonda en vivo, solo lectura (`cdmp_probe` o el mod): sesión (`+0x88`, `+0x90`, `+0x98` crypto apagada, `+0xC8 == ClientNet+0x18`), `usuario+0x1F8`, observadores del sector del jugador (1 entrada esperada), número de `ServerUserActor` (1), cuotas del SAM | juego abierto, sin escribir | bajo | confirma el mapa de §2.2 |
| **E0b** | Hook de solo registro en `SendPacket` o en las `vf1`: tamaño del vector por buscador | log | bajo | confirma B1 (`ExceptOneControl` = 0) |
| **E1** | Sesión 2 "dormida" + hook de `Send` filtrando por `this`; 10 min de juego sin efectos | un PC | medio | que crear la sesión no rompe nada |
| **E2** | Login solo del usuario 2 (copia de seguridad antes; P6 activo): `ConnectCryptogram` + `LoginUserToFieldServerReq` con cuenta = 1 | un PC; Ack capturados en la sesión 2 | medio-alto | `MainUserDataStartAck` con `0x9010xxxx` y 2 `ServerUserActor` |
| **E3** | Personaje del usuario 2 con la **misma ranura** del anfitrión; carga de zona con las Req de la captura | un PC | alto (guardado) | ¿ve el cliente del anfitrión al personaje 2 por `ToNear`? ¿choques de `characterNo`? |
| **E4** | Movimiento nativo: `MoveActorReq`/`StartActionReq` sintéticos del usuario 2 desde `cdmp_peer` + P5 | un PC | medio | el cliente del anfitrión ve moverse al usuario 2 **sin copia** |
| **E5** | Combate: `StartActionReq`/`DoFrameEventReq` del usuario 2 contra NPC del anfitrión; botín, experiencia y aggro; registrar 2508/2168 | un PC | medio | modelo de daño, botín y control de la IA |
| **E6** | Túnel real con Shadow: el cliente del invitado hace login en el servidor del anfitrión desde la pantalla de título (P8), con la ranura del anfitrión (clon) | dos PC | alto | que la carga de zona remota funciona |
| **E7** | Partida del invitado servida desde memoria (P9) | dos PC | alto | progreso real del invitado |
| **E8** | Pulido: control de NPC, jefes, pausa, hora, reconexión, viaje rápido, muerte | dos PC | — | — |

Estimación de `segunda-sesion.md`: E0-E2 en 1-2 días, E3-E4 en 2-4 días, E5-E6 en semanas.

**Cómo probar el lado del anfitrión con un solo PC:** el "invitado" es `cdmp_peer`, que manda por
el túnel Req fabricadas o grabadas (por ejemplo, el login de la captura 112627 con las claves
cambiadas). Hay que añadirle un modo "reproducir captura" (no existe).

---

## 6. Qué se reutiliza del proyecto actual

| Pieza | Fichero | Uso en la opción D |
|---|---|---|
| Hooks de cliente (mid-hook en la entrada, con comprobación de cabecera) | `src/mod/net/troc_sniffer.cpp` (`0x35C670`, `0x35CA00`) | captura e inyección S→C en el invitado |
| Inyección S→C desde dentro de `OnRecvPacket`, en su hilo | `troc_sniffer.cpp:213`, `remote_puppet.cpp` (`enqueue_at`, `puppet_take_injection`) | meter los Ack del anfitrión en el cliente del invitado |
| Inyección C→S dentro de la siguiente `PostPacket` real | `sniffer_inject_c2s` (`troc_sniffer.cpp:485`) | herramientas; **siempre va a la sesión 1** |
| Grabación `.bin` (`"CDPK"`, registros `<QBBHHHII` + paquete) y analizadores | `troc_sniffer.cpp:314`, `tools/scripts/analyze_capture.py`, `build/research/pieza4/pk.py` (clase `Cap`), `build/research/session2/login_seq.py` | todo el análisis |
| Nombres de opcode por RTTI | `load_troc_names` (`troc_sniffer.cpp:262`) y el CSV | resolver opcodes por nombre |
| Red | `src/common/reliable`, `client_session`, `protocol`, `udp`, `src/mod/net/steam_transport` | el túnel P7 |
| Herramientas | `cdmp_peer` (relevo, pérdida y jitter simulados), `cdmp_probe` (lecturas en vivo, firmas), `cdmp_host` | pruebas en un PC, E0 |
| Errores del juego | `src/mod/game/game_errors.cpp` (hook del logger interno `0x35D8D0`) | ver los rechazos del servidor durante el login |
| Memoria protegida, búsqueda de actores, RTTI, claves | `src/mod/game/safe_memory`, `actor_lookup` (mismo formato de hash que el SAM), `rtti`, `src/mod/net/keys.h`, `offsets.h` | todo |
| Hooks de escritores del servidor | `local_capture.cpp:187` (`0x29ED1F0`) | patrón para hooks en el hilo del servidor sin reservar memoria |
| Contención de cierres de copias y caballo | `horse_diag.cpp`, `remote_puppet.cpp` (capas `0x1F63F70`/`0x2329700`, `0x691400`, RideOn/Off) | el anfitrión verá al invitado como actor remoto: siguen haciendo falta |
| Ataques al actor remoto | `aggro_test.cpp` (hook `0x22E0A30`) | §2.9 |
| Configuración, log con rotación, HUD, build, deploy y kit | `src/mod/config`, `log`, `render/`, `scripts/*.ps1` | todo |

Lo que **no** se reutiliza en la opción D: `enemy_relay`, `guest_hits`, `player_hits`,
`aggro_test` (salvo el hook), `piece4_test`, `attack_probe` y casi todo `coop.cpp` y
`remote_puppet.cpp` (marioneta). Son del modelo de copias. Conviene que **la opción D viva en
módulos nuevos** (por ejemplo `src/mod/session2/`), con su propio interruptor `[session2]` en el
`.ini` y sin tocar el cooperativo actual, que funciona.

---

## 7. Reglas del proyecto (no negociables)

Vienen de `CLAUDE.md` y de lo aprendido:
1. **Responder al usuario en español.**
2. **Hooks:** solo inline hooks en la **entrada** de funciones, o `safetyhook::create_mid` en un
   punto concreto. Nunca un inline hook a mitad de función. **Nunca enganchar código de `.xtls`**
   (Denuvo).
3. **Toda lectura de memoria del juego, protegida** (`VirtualQuery` + SEH; `safe_memory`). **Un
   cierre del juego se envía a Pearl Abyss** (sentry/crashpad).
4. Nunca identificar entidades entre PCs por su dirección de memoria.
5. Nada de trabajo dentro de `DllMain`: todo en el hilo worker. El loader también carga en
   `crashpad_handler.exe` (`dllmain.cpp` filtra).
6. Si cambia el formato del protocolo, **subir `kVersion`** (`src/common/protocol.h`).
7. **Copia de seguridad de las partidas** (`%LOCALAPPDATA%\Pearl Abyss\CD\save` → `backups/`)
   antes de cualquier prueba que escriba en el juego. El usuario cierra el juego **sin guardar**
   tras cada prueba.
8. Nunca llamar a mano a funciones virtualizadas (login, logout, enlace de sesión): entrar
   siempre por el camino del mensaje nativo.
9. Ejecutar el código del servidor solo en sus hilos. No dejar un hilo con el byte TLS de realm
   cambiado.
10. Compilar con `.\scripts\build.ps1`; instalar con `.\scripts\deploy.ps1` (falla con el juego
    abierto). Probar sin juego con `cdmp_peer` + `cdmp_host` (en `build\e2e\`, porque
    `LoadLibrary` falla con rutas largas).

---

## 8. Contexto práctico para el proyecto nuevo

- Las pruebas en el juego se hacen a mano: quien prueba describe lo que ve y pulsa Ctrl+F12 para
  dejar marcas en el log (Ctrl+F7 solo hace falta para copias de enemigos).
- **Juego:** `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe`. Log del
  mod: `bin64\cdmp.log` (rota a `cdmp.1.log`…`cdmp.5.log`). Capturas: `bin64\cdmp_packets_*.bin`
  (las de hoy), `D:\cdmp-grabaciones\` (las antiguas), `captures\` (con el login completo:
  112627 y 105149).
- **Dos PC:** el anfitrión es el PC principal y el invitado un segundo PC (un PC en la nube con una
  segunda cuenta de Steam con el juego). Los logs del invitado vuelven por una carpeta compartida
  (`<onedrive>`). En un mismo PC sí se pueden abrir dos juegos (14-10): ver `docs/research/dos-instancias.md`.
- **Cooperativo actual:** el proyecto original sigue en
  `<onedrive>\Documentos\ChatGPT\Crimson Desert Multiplayer` (rama `main`, commit
  6318627, protocolo v6). La sección A de la prueba con dos PC funcionó el 2026-10-05. Hay
  arreglos en marcha allí (ropa al desmontar, pausa, hora del día, saltos de la copia):
  `docs/research/ropa-al-desmontar.md`, `pausa-copia-congelada.md`, `hora-del-dia.md`.
- **Comparten la carpeta del juego.** Si los dos proyectos ejecutan `deploy.ps1`, se pisan
  `cdmp.asi`, `winmm.dll` y `cdmp.ini`. Coordinar con el usuario quién instala.
  `cdmp_peer` usa el mismo puerto en los dos.
- Ficheros que **no están en git** y hay que copiar a mano:
  - `build/research/` (2,3 GB: scripts, volcados `.asm`, tablas, índices y `venv` con capstone);
  - `reference/` (repositorios de referencia);
  - `captures/` (930 MB, con el login completo);
  - `backups/`.
  - No hace falta copiar el resto de `build/` (compilaciones).

### 8.1 Qué se puede hacer sin el juego (trabajo de noche)
- D0 entero: análisis estático con `build/research/cdpe.py`, `xref.py`, `callers.py`, `cddis.py`,
  `findop.py`, `opscan.py` (venv con capstone en `build/research/venv`).
  - Prioridad: **cuota N**, opción B de P2, escritores de 2508 y 2168, quién llama a
    `0x2A83310`, `SAM::Initialize`, asignador `0x1447EF9B0`.
- Analizar a fondo la secuencia de login de las capturas 112627 y 105149: qué Req manda el
  cliente y en qué orden, y qué Ack recibe hasta `LoadingCompleteAck`. Es la base del guion de E2-E3
  y de P8.
- Diseñar y programar los módulos de la sesión 2 (P1-P7) detrás de `[session2] enabled=0`, con
  tests unitarios de todo lo que no toque el juego: túnel, cola, formato TrocFrame.
- Añadir a `cdmp_peer` el modo "reproducir Req de una captura".
- **No ejecutar `deploy.ps1` ni abrir el juego** sin el usuario presente.

---

## 9. Preguntas abiertas, por prioridad

1. **Cuota N de usuarios del SAM**: ¿cabe un segundo `ServerUserActor`?
2. ¿La sesión 2 hecha a mano funciona sin que el juego la "conozca" en otro sitio (listas de
   sesiones, `ToSessionList`, temporizadores de heartbeat `HeartbeatToParentRepeatTimer` 1019)?
3. ¿Qué hace el servidor con la IA de los NPC cuando hay dos clientes (2508, 2168)?
4. ¿Se para el servidor del anfitrión con su menú de pausa? ¿Congela al invitado?
5. ¿Qué tablas del servidor se indexan por `characterNo` o `itemNo`? ¿Chocan dos copias de la
   misma partida?
6. ¿Depende la carga de zona del servidor de la escena del **cliente del anfitrión** (B5)? ¿Puede
   el invitado estar lejos del anfitrión?
7. ¿De quién es el botín y la experiencia con dos usuarios? ¿Hay reparto en grupo
   (`ToNearAndParty`, `JoinPartyAck`)?
8. ¿Cómo dar de baja al usuario 2 (logout virtualizado) sin cerrar el juego?
9. ¿Qué pasa con el guardado del anfitrión si el usuario 2 está presente cuando salta un
   autoguardado (aunque se bloquee la escritura)?
10. Partida del invitado servida desde memoria: ¿el cargador acepta un blob cifrado de otra
    cuenta de Steam (la clave puede depender del SteamID)?

---

## 10. Índice de informes del repositorio para la opción D

| Informe | Para qué |
|---|---|
| **`docs/research/segunda-sesion.md`** | **El estudio de viabilidad de la opción D**: login, sesión, buscadores, bloqueos B1-B10, etapas E0-E6. Léelo entero primero |
| `troc-sintesis.md`, `troc-dispatch.md` (13 firmas AOB verificadas), `troc-rtti.md`, `troc-bdo-y-runtime.md` | Arquitectura de mensajes, transporte, hilos, objetos en vivo |
| `broadcast-servidor-movimiento.md` | Ruta del movimiento en el servidor, buscadores, correcciones (`MoveForceAck`, `ChangeMoveSessionIDAck`) |
| `mirror-ack-formats.md`, `movimiento-remoto-cliente.md` | Formatos Req→Ack y cómo interpola el cliente a un actor remoto (puerta de modo de control) |
| `apariencia-estado-coop.md` | Equipo, `characterNo`, caché de equipo, relevos `ToNear` |
| `combate-coop.md`, `pieza4-dano-servidor.md`, `pieza4-*.md` | Daño, validación de `DoFrameEventReq`, muerte, botín |
| `pendiente-recompensas-progreso.md`, `pendiente-jefes.md` | Crédito de muertes, experiencia, misiones, stages de jefes |
| `copia-objetivo-sin-ataques.md` | Lista de rango y el hook de `CheckAIBattleType` |
| `enemigos-del-anfitrion.md`, `pendiente-3g-suprimir-enemigos-invitado.md` | Spawns pedidos por el cliente, distancias, formato de carga |
| `companeros-mercenarios.md`, `copias-grupo-invisibles.md` | Hijos del usuario, grupos, login de grupo |
| `caballo-coop.md`, `docs/INFORME-CABALLO-CRASH.md` | Montura remota y su cierre |
| `pendiente-rango-claves.md` | Claves y cuotas del SAM |
| `steam-transporte.md`, `pendiente-robustez.md` | Transporte y protocolo de pruebas con dos PC |
| `pausa-copia-congelada.md`, `hora-del-dia.md` | Reloj, pausa, hora del día |
| `bdo-emuladores-monstruos.md` (§8), `genshin-coop-referencia.md`, `bdo-otra-ia.md` | Referencias de MMO |
| `docs/ANALISIS-CrimsonDesertCoop.md` | Por qué el otro mod no sirve |

**Notas de trabajo de esta investigación** (con cita de informe y confianza en cada dato), en
`docs/research/opcion-d-notas/`:
- `optD-mundo.md`: servidor, cliente, transporte, reloj, claves; la más completa;
- `optD-combate.md`: combate, recompensas, aggro, trucos;
- `optD-codigo.md`: inventario del código, con file:line;
- `optD-referencias.md`: `reference/`, World Builder, web;
- `optD-web-agent.md`: BDO y proyectos análogos.

Tablas y datos: `docs/research/data/troc_messages_2976.csv`, `build/research/troc_table.*`,
`build/research/appearance/broadcast_scan.txt`, volcados `build/research/session2/*.asm` (login de
usuario `s_2c48be0.asm`, personaje `s_2c3e030.asm`, sector `s_2af8af0.asm`, buscadores
`targetfinders.asm`, envío y recepción del cliente `cli_flush.asm` y `cli_netthread.asm`).

---

## 11. Mensaje para arrancar la sesión del proyecto nuevo

Para que trabaje toda la noche sin pararse, el usuario arranca la sesión con **`/loop`** delante
del mensaje (sin intervalo: la sesión decide cuándo seguir) y con un modo de permisos que no pida
aprobación para lo normal. Si no, la sesión se detiene al acabar cada respuesta o se queda
esperando un permiso.

> /loop Lee `CLAUDE.md` y después `docs/OPCION-D-EMULADOR.md` entero, y `docs/research/segunda-sesion.md`.
> Vamos a intentar la opción D: que el servidor interno del juego del anfitrión acepte al invitado
> como segundo jugador real. Trabaja en la rama `opcion-d`.
>
> **Trabaja de forma autónoma toda la noche y no me hagas preguntas: estoy durmiendo.** Cuando
> haya que decidir algo, elige la opción más prudente, apunta la decisión y el motivo en
> `docs/research/opcion-d-diario.md` y sigue. No te detengas hasta que yo te lo diga por la
> mañana: al acabar una tarea, pasa a la siguiente de la lista.
>
> Esta noche **no abras el juego ni ejecutes `deploy.ps1`** (la carpeta del juego la comparte el
> otro proyecto). Orden de trabajo:
> 1. Etapa D0 en estático, empezando por la cuota N de usuarios del servidor.
> 2. Análisis de la secuencia de login de las capturas 112627 y 105149.
> 3. Las piezas P1-P7, programadas detrás de `[session2] enabled=0`, con tests para todo lo que no
>    toque el juego.
> 4. El modo "reproducir captura" de `cdmp_peer`.
>
> Usa todos los agentes que necesites. Haz commits pequeños en `opcion-d`. Escribe lo que vayas
> encontrando en `docs/research/`. Mantén al día en `docs/research/opcion-d-diario.md` qué está
> hecho, qué falta y qué pruebas en el juego necesitas que haga yo, en orden, para leerlo al
> despertar.
