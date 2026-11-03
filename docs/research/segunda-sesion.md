# Segunda sesión de usuario en el servidor del host: estudio de viabilidad

Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). Direcciones en RVA (`exe+…`).
Análisis estático de solo lectura (capstone sobre el exe en disco) más las capturas
`captures/cdmp_packets_20261003-112627.bin` y `-105149.bin`. El juego no se ejecutó.
Scripts y volcados en `build/research/session2/`.

Leyenda: **[V]** verificado en el código o en la captura, **[S]** suposición razonada,
**[?]** sin verificar.

## Resumen

1. **El servidor sigue siendo multiusuario en lo esencial.** Crear un `ServerUserActor`,
   enlazarlo a una sesión, mandar respuestas a "la sesión del actor", el área de interés por
   sectores y la difusión `ToNear` funcionan para N usuarios. No hay ningún puntero global
   del tipo "mi usuario" en el servidor. [V]
2. **La única sesión está fijada en un solo sitio:** el bucle del hilo de red del servidor
   (`0x27733F0`) pasa siempre `ServerNet+0x18` a `OnRecvPacket`. Ningún otro código lee
   `ServerNet+0x18`. [V]
3. **Crear la sesión es trivial:** `NwVirtualAsyncSession` ocupa 0xD0 bytes, tiene un
   constructor base en `.rsrc` (`0x1436EE0`) y el resto son 5 campos. [V]
4. **El alta del usuario se hace con mensajes nativos:** `LoginUserToFieldServerReq` crea el
   `ServerUserActor` y sus personajes y lo enlaza a la sesión que mandó el paquete.
   `LoginCharacterToFieldServerReq` carga el personaje **desde una ranura de guardado del
   disco del host**. Varias piezas están virtualizadas por Denuvo, pero se ejecutan solas si se
   entra por el camino del mensaje. [V]
5. **Bloqueo nuevo e importante:** los buscadores `ToNearExceptOneControl` y
   `ToNearExceptOneSelf` están **vaciados** en esta build: su `vf1` no añade ninguna sesión.
   Por eso el servidor ya no reenvía a los demás jugadores el Move, StartAction, Rotate ni
   ChangeAIState del que controla. Se arregla con 2 hooks en la entrada de esas funciones. [V]
6. **Veredicto: arriesgado, pero factible por etapas.**
   - **Lado servidor:** de dificultad media. Los primeros experimentos son baratos y de bajo
     riesgo.
   - **Lado invitado:** lo caro es convertir su cliente en un "cliente fino" que se conecte al
     servidor del host desde la pantalla de título.
   - **Acoplamientos peligrosos:** el guardado en el disco del host y la carga del mundo, que
     depende del cliente del host.

---

## 1. Cómo se crea hoy la única sesión

### 1.1 Secuencia de login (capturas 112627 y 105149, idénticas)

| t (s) | Dir. | Mensaje | Notas |
|---|---|---|---|
| 0,000 | C→S | `1001` (no es Troc) | Payload `01 "127.0.0.1" …`. **Lo consume el propio cliente** (ver 1.2) |
| 0,000 | S→C | `ConnectToServerAck` 1002 | **Lo fabrica el cliente**, no el servidor |
| 0,045 | C→S | `ConnectCryptogramToServerReq` 2319 | 286 B, lleva una clave pública RSA DER (`30 82 01 0A…`) |
| 0,049 | S→C | `ConnectCryptogramToServerAck` 2840 | Todo ceros: el cifrado sigue apagado [S] |
| 0,059 | C→S | `LoginUserToFieldServerReq` 2490 | 19 B: `u64 cuenta=0, u32 0, u8 0, wstring ""` (vf2 `0x2A21060`) |
| 0,065–0,243 | S→C | `ServerTimeInfoAck`, `MainUserDataStartAck` (`0x90100000`), `CharacterLobbyDataAck` ("Kliff"), `MainUserDataEndAck` | |
| 3,437 | C→S | `LoginCharacterToFieldServerReq` 3093 | `u8 4, u64 0x7FFF…FF, u32 1, …`. El `u32` es la **ranura de guardado** [S] |
| 3,444–3,774 | S→C | `ClearCharacterLobbyDataForSinglePlayAck` y unos 60 Ack con todo el estado del personaje (buffs, stats, inventario, misiones, facciones, gimmicks, `PrivateActorMoveAck`…), `MainPlayerDataEndAck` | |
| 4,277 | S→C | `MoveFieldAck` | |
| 15,0 | C→S | `MoveFieldCompleteReq` → S→C `ActorLoadDataAck`… | |
| 33,3 | C→S | `LoadingCompleteReq`, `LoginCharacterToGameReq`, `GameLoadingStartReq` | |

Script: `build/research/session2/login_seq.py <captura> [tmax]`.

### 1.2 El "connect" ocurre solo en el cliente [V]

- `ClientNet::FlushSendQueue` `0x35BE90` detecta las tramas de conexión:
  - llama a `0x35C230` (connect: crea o elige la sesión del cliente; el constructor de
    `NwVirtualSyncSession` está mutado en `.xtls` `0x86E4310`, a través del thunk `0x35B550`);
  - después **fabrica** el `ConnectToServerAck` (`op 0x3EA`, `u32 índice de sesión`,
    `u32 resultado`) y lo mete en `ClientNet::OnRecvPacket` (`0x35C0A9`–`0x35C0ED`).
- El servidor no ve ninguna conexión. Su sesión ya existe desde que arranca.

### 1.3 La sesión del servidor se crea al iniciar el módulo [V]

```
0x2772050 (arranque del módulo servidor)
  └─ call 0x2775360 (thunk) ─► 0x100B8230 (.xtls, mutado pero legible) = ServerModule::Init(this=ServerNet, res, colaCliente)
       ├─ alloc(0xD0)                                  tamaño = [0x15B42DA8]−0xFFF4FB54 = 0xD0 (constante ofuscada)
       ├─ 0x1436EE0(obj, 0)                            NwSession::NwSession (.rsrc)
       ├─ vtbl = NwAsyncSession 0x5726140; +0xB0=+0xB8=0; +0xC0(u16)=0
       ├─ vtbl = NwVirtualAsyncSession 0x57260C0; +0xC8 = 0; +0x18(u16) = 0x31 ("1")
       ├─ ServerNet+0x18 = obj
       └─ obj+0xC8 = colaCliente (== ClientNet+0x18)
```

Campos de `NwSession` (constructor `0x1436EE0`):

| Offset | Contenido |
|---|---|
| `+0x08` | `WindowsLock` |
| `+0x10` | struct de estado (u16 `0x17`) |
| `+0x18` | cadena corta, nombre de la sesión ("1") [S] |
| `+0x80` | u32 |
| `+0x88` | `u32*` id (se usa como clave de cola si aún no hay usuario) |
| `+0x90` | `ServerUserActor*` |
| `+0x98` | `CryptogramAesGcm` (+0x24 = 0, apagado) |
| `+0xA0` | struct {ptr, u32, u8=2} |
| `+0xA8` | 5 |
| `+0xA9` | bool del argumento |

`NwVirtualAsyncSession` añade `+0xB0`, `+0xB8`, `+0xC0` y `+0xC8` (la cola de salida).

### 1.4 Alta del usuario y de sus personajes [V]

```
LoginUserToFieldServerReq::vf2 0x2A21060   (deserializa u64, u32, u8, wstring ≤1500)
  └─ 0x2C48BE0(troc, TrocParams*, &cuenta, &u32, &u8, &nombre)
       ├─ 0x2A838E0(SAM, &cuenta)   [Denuvo VM]  ¿ya hay un usuario con esa cuenta? → 0x2C17AB0 (relogin/expulsión) [S]
       ├─ ToTargetSendTargetFinder(TrocParams->session)  → las respuestas van A LA SESIÓN QUE PREGUNTA
       ├─ 0x2A1E9D0                  ServerTimeInfoAck [S]
       ├─ si TrocParams->actor == null:
       │    0x2A83000(SAM) = SAM::CreateUser
       │      └─ 0x2A83310: alloc 0x218, 0x206A940 (CommonUserActor), vtbl ServerUserActor 0x5B257A8,
       │                    +0x1F8 = 0 (sesión), +0x190 = 0
       │                    0x2A845E0 [Denuvo VM] (clave/registro) ; user->vf(0x40)(init)
       │                    por cada hueco hijo ([[user+0x68]+0x18]+0x20):
       │                       alloc 0xD8, 0x28F23F0, vtbl ServerChildOnlyInGameActor 0x5B1E2B0,
       │                       +0xD0 = user+0x1F8 ; init ; se cuelga del ChildContainer
       │                    0x2A837E0(SAM, clave) registra cada actor por serie
       │      registra todo en SAM+0xB8 / SAM+0x98 (0x2A84F00) y ++SAM+0xA4
       ├─ 0x1437400(sesión, usuario) [Denuvo VM]   = enlaza sesión+0x90 ↔ usuario [S]
       ├─ usuario+0x1F8 = sesión ; copia sesión+0x18 → usuario+0x190 ; cierra la sesión anterior si la había (vf 0x18)
       └─ 0x2C17520 (UserLogin): MainUserDataStart/Lobby/End (0x2A1EE40, 0x2A1F280), carga "lobby" con 0x186F570
```

`ServerUserActor+0x1F8` es la sesión: su `vf(0xE8)` = `0x2A7D9E0` = `mov rax,[rcx+0x1F8]`. En
`ServerChildOnlyInGameActor`, `vf(0xE8)` = `0xAD83F0` = `[rcx+0xD0]`. [V]

### 1.5 El personaje sale de la partida guardada del host [V]

```
LoginCharacterToFieldServerReq::vf2 0x298BA10 → 0x2C3E030
   si byte [exe+0x6CEEDE8] == 1   (modo un jugador; lo pone a 1 el inicializador estático 0x1CFC00, se consulta en ~47 sitios)
     0x186F570(GestorGuardado, res, slotNo, ownerActor=usuario)
        "Start Load SaveSlot%#"
        ranuras válidas: 0-2, 3, 100-108, 200-207 (si no: "싱글플레이 게임 데이터 불러오기에 실패…")
        guarda de concurrencia: byte [gestor+0x60]
        0x20688A0(usuario, res, …) = processLoadDataFromFile por ActorComponent (MemoryArchive; ficheros "lobby"/"save")
        si slot == 0xCE → 0x1871040 (borra la ranura de un solo uso) [S]
   si no (modo MMO) → camino de base de datos (0x9116E0…), que no sirve aquí
```

**Conclusión:** el servidor no recibe los datos del personaje por la red. Los lee él mismo del
disco del host, de la ranura que pida el cliente, y los vuelca en los componentes del usuario
que hizo login.

---

## 2. Objetos de transporte y difusión

### 2.1 vtables (RTTI) [V]

| Clase | vtable | Entradas útiles |
|---|---|---|
| `NwSession` | `0x57261C0` | constructor `0x1436EE0` |
| `NwAsyncSession` | `0x5726140` | |
| `NwVirtualAsyncSession` (servidor→cliente) | `0x57260C0` | vf0 dtor `0x1437580`, vf3 `0x1437600` (VM, probablemente Close), vf6 `0x359BA0` (=false), **vf9 Send `0x1437740`** (empuja `rq+0x30` a la cola `[this+0xC8]` con `0x35DF50`), vf14 `0x3600C0` (=true) |
| `NwVirtualSyncSession` (cliente→servidor) | `0x5726248` | vf6 Send `0x14380B0` |
| `ServerUserActor` | `0x5B257A8` | tamaño 0x218, vf(0xE8)=sesión, vf(0xF8)=`0x206AD10` (slot→actor) |

### 2.2 Recepción en el servidor [V]

```
0x27733F0 bucle del hilo de red del servidor
   pop(ServerNet+0x20, timeout 1000 ms)                  0x35E030
   si buf+0x800E: AsyncRequest interno → 0x2774730
   si no: OnRecvPacket(ServerNet, &res, **[ServerNet+0x18]**, buf+3, len-0x1F, canal)   ← única sesión, fija
   push(buf, ServerNet+0x78)  (devuelve el búfer al pool)
```

`ServerNet::OnRecvPacket` `0x2773C20(ServerNet*, int*, NwSession* s, u8* pkt, u16 size, u8 canal)`:
- `Troc::Find(op, máscara)`; comprueba el canal (`troc+0x14`), los tamaños y `troc+0x10 == 1`.
- `user = s+0x90`:
  - si existe y `slot ≥ 0`: `actor = user->vf(0xF8)(slot)`;
  - si `slot < 0`: `actor = user`;
  - **si no hay usuario**, solo pasan los Troc con `flags(+0x16) & 1` (los de login tienen
    `0xFF`) o `== 0x100` con `s->vf(0x30)`.
- **Síncronas** (`+0x1C == 0`, 84 Req: `MoveActorReq`, `StartActionReq`, `DoFrameEventReq`,
  `RotateActorReq`, `ChangeAIStateReq`, `AttackByCollisionReq`…):
  - se ejecutan **en este mismo hilo**, con `ExecuteTroc 0x2774B20`;
  - `TrocParams = {actor, sesión, size, pkt}`.
- **Diferidas** (`+0x1C == 1`, 402 Req, entre ellas todos los login):
  - se crea un `TrBufferPoolValue` (`+0x48` = sesión, `+0x50` = clave del actor,
    `+0x58` = clave del dueño o `*(s+0x88)`);
  - se encola en `ServerNet+0x38` con `0x143A580`, clave = `+0x58`. Parece una cola
    serializada por usuario [S].

### 2.3 Envío y buscadores de destinatarios [V]

- `ServerNet::SendPacket` `0x2773870(…, vector<NwSession*>* dst)`:
  - **una copia del búfer por sesión** (`0x2771B30`) y cifrado si toca;
  - después `sesión->vf9 Send`.
  - Las rutas inline (`ScopeSenderAck`) terminan también en `vf9`.
- Cada escritor de Ack hace `finder->vf1(&vectorTLS)`. Si el vector queda vacío, **no envía
  nada** (p. ej. `MoveActorAck` `0x2974C00`, `0x2974C60`–`0x2974C6A`).

| Buscador | vtable | vf1 | Qué recoge | Refs. |
|---|---|---|---|---|
| ToTarget | `0x5B25438` | `0x2A757F0` | `finder+0x10` (una sesión). 474/536 usos la sacan de `actor->vf(0xE8)` (sesión del actor) | 536 |
| ToControlUser | `0x5B252F8` | `0x2A757F0` | igual | 2 |
| **ToNear** | `0x5B25418` | `0x2A75860` → `0x2BC51E0` → **`0x2BC4F70`** | observadores del sector del actor (`TS+0x540` → `0x2AF2E60`: lista `sector+0x68`, entradas de 16 B `{ServerUserActor*, refcount}`, número en `+0x70`, lock en `+0x38`) → `usuario+0x1F8`, más la sesión propia | 152 |
| ToNearAndParty | `0x5B253D8` | `0x2A75AF0` → `.xtls 0x104E6420` | ToNear + miembros del grupo | 4 |
| ToWorld | `0x5B25318` | `0x2A75F00` → `.xtls 0x104EB5B0` | recorre todos los usuarios (`0x2A83A40`) → `+0x1F8` | 8 |
| ToField | `0x5B25398` | `0x2A75EF0` → `0x2AFFBF0` | usuarios del field | 9 |
| ToStage | `0x5B253B8` | `0x2A75D20` | claves de usuario de la stage → `vf(0xE8)` | 2 |
| ToSessionList | `0x5B25338` | `0x2A75AE0` → `.xtls` | | 3 |
| **ToNearExceptOneControl** | `0x5B25358` | **`0x2A759B0`** | **nada**: pisa `rdx` (el vector) en `0x2A759CC` sin guardarlo. Solo queda el attacher de field, el lock y una consulta de datos (`0x4A2010`) cuyo resultado se descarta | 9 |
| **ToNearExceptOneSelf** | `0x5B25378` | **`0x2A75880`** | **nada** (mismo cuerpo) | 6 |

Quién usa `ExceptOneControl`, y por tanto **no llega a nadie**:
- `MoveActorAck` (`0x2BCD8A0`);
- `StartActionAck` de relevo (`0x2A7A560`);
- `RotateActorAck` (`0x106F3120`);
- `ChangeAIStateAck` (`0x2AC66D0`);
- `UpdateWallUpDataAck` (`0x297A4A0`);
- `UpdateSeatDataAck` (`0x297A780`);
- `SyncActorDataAck` (`0x298EB30`);
- `0x297AA80`.

Quién usa `ExceptOneSelf`: `0x27DDE40`, `0x2A6DE90` (`ChangeVehiclePhysicsState`),
`0x2AC0650`, `0x2AD5CC0` y `0x2ADA5A0`.

**Interpretación** [S]: Pearl Abyss compiló fuera el relevo "a los demás" (por ejemplo con un
`#if` del modo MMO). `ToNear`, `ToWorld`, `ToField` y `ToTarget`, que también necesita un solo
jugador, siguen completos.

### 2.4 Área de interés por sectores: viva [V]

```
SetTransform core 0x2BCD8A0 → 0x2AF8090 → 0x2AF8AF0  (ServerField: el actor cambia de sector)
   si actor->vf(0x178)() (controlado por usuario) y usuario != null:
      0x2AFEDD0 sectores vecinos viejos/nuevos ; 0x2AF3010 diferencia (entran/salen)
      sale:  0x2AF2D80(sector, usuario)                      quita al observador
      entra: busca usuario en sector+0x68; si está ++refcount, si no inserta {usuario,1} (0x40B5B0)
   observadores viejos/nuevos (0x2AF2E60 ×2) → diferencia → 0x2B000C0 / 0x2B00380 (aparecer/desaparecer;
   0x2B00380 está mutado en .xtls 0x10650890 y llama al escritor de RemoveActorAck 0x29756F0)
```

Con un segundo `ServerUserActor` cuyo personaje se mueva por el field, **el servidor lo
registraría solo como observador** de los sectores que lo rodean, y `ToNear` lo incluiría.

### 2.5 ¿Se puede construir una segunda sesión? Sí

Por los mismos pasos que `0x100B8230`, todos en `.rsrc` o triviales:

1. Reservar 0xD0 bytes con el asignador del juego: `0x1447EF9B0`, o `0x1447EFA78` si el byte TLS
   `+0x1FD` != 0. El juego podría liberarla al hacer logout, así que no hay que usar memoria
   propia.
2. Llamar a `0x1436EE0(obj, 0)`.
3. Poner la vtable `0x57260C0`, `+0xB0 = +0xB8 = 0`, `u16 +0xC0 = 0` y `+0x18 = "2"` [S].
4. `+0xC8`: **no** apuntarlo a `ClientNet+0x18`, porque el cliente del host recibiría los
   paquetes del invitado. Ver §4.2.
5. `*(u32*)[obj+0x88]`: id de sesión distinto de 0, por ejemplo 1 [S].

Duplicar la sesión con `memcpy` no sirve: comparte el lock, la crypto y la cola.

---

## 3. Qué necesita el servidor para el segundo usuario

| Necesidad | Cómo se obtiene | Estado |
|---|---|---|
| Sesión | §2.5 | trivial |
| Clave de usuario | La asigna `SAM::CreateUser` (`0x2A845E0`, VM). Se espera `0x90100001` [S] | nativo |
| Que no choque con el host | `LoginUserToFieldServerReq` con **cuenta u64 ≠ 0**. Si no, `0x2A838E0` encuentra al host y va a la rama de relogin `0x2C17AB0` [S] | reescribir 8 bytes |
| Personaje | `LoginCharacterToFieldServerReq` → carga una **ranura del disco del host** (§1.5). Opciones: (a) la misma ranura que el host, que da un "clon de Kliff" en el servidor y sirve para probar; (b) hook en la carga (`0x186F570`/`0x20688A0`, en la entrada) para servir el blob de partida del invitado desde memoria cuando `ownerActor == usuario2` [S]; (c) copiar la partida del invitado a una ranura libre del host (peligroso) | ver bloqueos |
| Pertenencia al field/sector | Automática al moverse (§2.4). El primer `PrivateActorMoveAck`/`MoveFieldAck` coloca al personaje en la posición del guardado | nativo |
| Claves entre PCs | Con el modelo de "cliente fino" (§4) el invitado usa las claves del host y no hace falta traducir nada | — |

**Restos multiusuario vivos** [V]:
- relogin con expulsión de la sesión anterior (`0x2C48F5A`–`0x2C48FDD`);
- `ToWorld`/`ToField`/`ToStage`/`ToNearAndParty` recorren usuarios y sesiones;
- `OpenPartyAck` en cada login;
- el diff de observadores de `0x2AF8AF0`;
- el cliente acepta un segundo usuario: hito 4c, `ActorLoadDataAck` con dueño `0x9010F001`.

`UpdateVisiblePlayerAck` 2135 tiene handler en el cliente (`0xC06720`), pero **no tiene
escritor en el servidor**: no aparece ningún inmediato 2135 en `.rsrc`. Es código muerto o
está virtualizado.

---

## 4. Por dónde entra el invitado y cómo se captura su salida

### 4.1 Entrada (Req del invitado → servidor del host)

- Las Req síncronas hay que ejecutarlas **en el hilo de red del servidor**.
- **Propuesta A** (la más simple):
  - hook en la entrada de `ServerNet::OnRecvPacket` `0x2773C20` (firma
    `troc_server_on_recv_packet` en `troc-dispatch.md`);
  - después de procesar el paquete del host, vaciar nuestra cola llamando al original con
    `r8 = sesión2` y nuestro búfer. `OnRecvPacket` no se queda con el búfer: las diferidas lo
    copian a `TrBufferPoolValue`;
  - **problema:** si el host no envía nada, el bucle espera hasta 1 s (`pop` con timeout de
    1000 ms).
- **Propuesta B** (sin latencia) [S, verificar en vivo]:
  - sacar un `PacketBuffer` del mismo pool al que lo devuelve el bucle (`ServerNet+0x78`);
  - llenarlo como `NwVirtualSyncSession::Send` (trama en `+0x8000`, longitudes en
    `+0x8008`/`+0x800A`, `+0x800E = 0`) con una marca propia (p. ej. en `+0x8010`);
  - empujarlo a `ServerNet+0x20` con `0x35DF50`;
  - en el hook de `OnRecvPacket`, si el paquete es nuestro, cambiar `r8` por la sesión 2.
- Antes de hacer login con la sesión 2 solo pasan los Troc de login (flags `& 1`). Después,
  todo se atribuye a `usuario2` (`slot` → `user2->vf(0xF8)`) y se aplican **de forma nativa**
  las comprobaciones de dueño, como `vf170() == actor+0x90` en `MoveActorReq`.

### 4.2 Salida (servidor → sesión 2)

- **Hook en la entrada de `NwVirtualAsyncSession::Send` `0x1437740`** (firma
  `nw_virtual_async_session_send`).
- Si `this == sesión2`:
  - copiar la trama de `r8+0x30`: paquete en `+3`, longitud `*(u16*)trama − 0x1F`;
  - devolver el `PacketBuffer` al pool con `0x35DF50`, igual que hace el NetworkThread del
    cliente en `0x35BB5C` (`push [ClientNet+0x20]`);
  - salir con `*res = 0`, sin llamar al original.
- Por aquí pasan también las rutas inline y `ScopeSenderAck`.
- Hay que **verificar en vivo** que el pool de destino es el correcto: según la doc,
  `ClientNet+0x20 == ServerNet+0x80`.

### 4.3 El lado del invitado ("cliente fino") [S]

- Para que el invitado sea un usuario real del host, su cliente tiene que hacer login **en el
  servidor del host** y no en el suyo. La forma más limpia es desde la pantalla de título:
  - hook en `NwVirtualSyncSession::Send` `0x14380B0` del invitado, que manda sus tramas por
    la red en vez de a su cola local;
  - inyectar las tramas del host en su `ClientNet::OnRecvPacket` `0x35CA00`, como ya hace 4c.
- Su `1001`/`1002` es local y no cambia. Desde `ConnectCryptogram` todo va al host.
- El servidor local del invitado queda sin usuario.
- Es la parte más cara: estados de carga, `LoadingComplete`, guardado…

---

## 5. Bloqueos y riesgos

| # | Bloqueo | Gravedad | Evidencia | Mitigación |
|---|---|---|---|---|
| B1 | **Relevos "excepto el que controla" vaciados** | Alta (sin ellos no se ve moverse a nadie) | `0x2A759B0`/`0x2A75880` no escriben el vector | Hook en la entrada de ambas `vf1`: llamar a `0x2BC4F70(TS=[[finder+0x10]+0x68]+0x1A0, vec)` y quitar la sesión del que controla (`actor->vf(0xE8)`). En NPCs con IA del cliente, el "controlador" se elige de otra forma [?] |
| B2 | Sesión fija en el bucle del servidor | Media | `0x27735C7` `mov r8,[r15+0x18]` | §4.1 |
| B3 | **Partida guardada acoplada al disco del host** | Alta | §1.5. El gestor de guardado es único (`[gestor+0x60]` como guarda) y hay un guardado que vuelca la partida (`FlushPendingSaveDataOnceTimer`, `0x28FC6F0` → `0x186E1A0`) | Copia en `backups/` antes de nada. Bloquear (hook) cualquier guardado iniciado por `usuario2` o con él presente [?]. Servir su partida desde memoria (hook en la carga) |
| B4 | Estado de "mundo" en la partida | Media-alta [?] | La carga vuelca facciones, gimmicks del field, misiones y stages por componente. Hay que ver qué parte es global del servidor y no por usuario | Experimento E3 con la **misma** ranura que el host, para no meter un mundo distinto |
| B5 | Streaming de la zona | Alta [?] | Las spawns y la carga de niveles las pide el cliente (`SceneLevelLoadComplete…`, `ActivateSpawningPoolReq`…). El servidor vive en el mismo proceso que el cliente del host y podría depender de la escena y la navegación que este tiene cargadas | Mantener a los jugadores cerca (correa) en las primeras etapas |
| B6 | Denuvo en el camino | Baja si se usa el mensaje nativo | VM en: búsqueda por cuenta `0x2A838E0`, alta de clave `0x2A845E0`, enlace `0x1437400`, logout `0x2C482E0`/`0x2C48410`, desconexión (`.xtls 0x86E5500`), `NwVirtualAsyncSession::vf3` `0x1437600` | No llamar a esas funciones a mano. Para dar de baja al invitado no hay ruta legible: el logout está virtualizado y `LogoutUserToServerTranReq` (`0x2A22640`) solo deserializa |
| B7 | Singletons de un usuario | Baja en el servidor | No hay ningún global "mi usuario". El contexto por realm (`exe+0x6D694D8` cliente / `exe+0x6D694E0` servidor, unas 980 referencias) son gestores con búsqueda **por clave** (`ctx+0x10` → `vf(0x30)`, `vf(0x20)`, `vf(0x28)`) | — |
| B8 | Modo un jugador `[exe+0x6CEEDE8]=1` | Media | ~47 ramas: el login y la carga usan rutas de un jugador. Poner el byte a 0 lleva a ODBC | Dejarlo a 1 |
| B9 | Hilos | Media | Las Req síncronas, en el hilo de red del servidor. Las diferidas, en la cola por usuario `ServerNet+0x38` (otros hilos del servidor) | §4.1. Nunca llamar a `ExecuteTroc` desde un hilo propio |
| B10 | IA de NPC controlada por el cliente | Media [?] | `ChangeAIStateReq` es C→S; el MMO elegía qué cliente corre la IA de cada NPC | Observar en E4 a quién asigna el servidor el control |

---

## 6. Veredicto y plan

**Arriesgado pero factible.**
- Lo que da miedo en teoría (crear sesiones, usuarios, AOI, difusión) está vivo y se alcanza
  con mensajes nativos.
- Lo que de verdad cuesta:
  - el relevo vaciado (B1, unas pocas horas con hooks);
  - el acoplamiento con la partida guardada y con el streaming del cliente del host (B3–B5),
    que solo se resuelve probando;
  - convertir al invitado en cliente fino (§4.3), que es un proyecto en sí mismo.

Estimación muy aproximada: E0–E2 en 1-2 días; E3–E4 en 2-4 días; E5–E6 en semanas.

### Etapas (de menor a mayor riesgo)

- **E0, solo lectura (`cdmp_probe`):**
  - `[exe+0x6D69680]+0x18` → sesión: comprobar `+0x88`, `+0x90 = usuario`, `+0x98` crypto
    apagada y `+0xC8 == ClientNet+0x18`;
  - `usuario+0x1F8 == sesión` y `usuario+0x190`;
  - en el jugador, `TS=[[a+0x68]+0x1A0]`, `sector=[TS+0x540]`: leer `+0x68`/`+0x70`. Se espera
    1 entrada `{usuario, refcount}`;
  - contar los `ServerUserActor` (vtable `0x5B257A8`): se espera 1.
- **E0b, solo log:** hook en `SendPacket` (o en las `vf1` de los buscadores) que registra el
  tamaño del vector por buscador. Debe confirmar que `ExceptOneControl` devuelve 0.
- **E1, sesión dormida:**
  - construir la sesión 2 (§2.5) sin enlazarla a nada;
  - activar el hook de `0x1437740` filtrando por `this`;
  - comprobar que no pasa nada en 10 minutos de juego.
- **E2, login solo en el servidor** (copia de la partida en `backups/` antes):
  - inyectar por `OnRecvPacket` con `r8 = sesión2` el `ConnectCryptogramToServerReq` de la
    captura y `LoginUserToFieldServerReq` con cuenta = 1;
  - éxito: capturar en sesión 2 `MainUserDataStartAck` con `0x90100001` y ver 2
    `ServerUserActor`.
- **E3, personaje:**
  - `LoginCharacterToFieldServerReq` con la **misma ranura** que el host;
  - mirar si el cliente del host recibe `ActorLoadDataAck` del personaje del usuario 2 por la
    vía de los sectores;
  - vigilar los guardados: no guardar, y cerrar sin guardar.
- **E4, movimiento nativo:**
  - `MoveActorReq`/`StartActionReq` sintéticos para el usuario 2 (desde `cdmp_peer`);
  - más el hook de B1;
  - el cliente del host debería ver `MoveActorAck` sin espejo.
- **E5, combate:** `StartActionReq`/`DoFrameEventReq` del usuario 2 contra NPC del host.
  Comprobar daño con sus stats, aggro y botín.
- **E6, invitado como cliente fino** (§4.3) y partida del invitado servida desde memoria.

## 7. Direcciones nuevas (build 2976)

| RVA | Qué es |
|---|---|
| `0x2772050` → `0x2775360` → `.xtls 0x100B8230` | arranque del módulo servidor: crea la única `NwVirtualAsyncSession` (0xD0) |
| `0x1436EE0` | `NwSession::NwSession(this, bool)` |
| `0x1437740` / `0x1437580` / `0x1437600` | `NwVirtualAsyncSession` vf9 Send / dtor / vf3 (VM) |
| `0x35C230` | connect del cliente (trama 1001); `0x35C0A9` fabrica `ConnectToServerAck` |
| `0x2A21060` → `0x2C48BE0` | `LoginUserToFieldServerReq` → login del usuario |
| `0x2A20BD0` → `0x2C48570` | `LoginUserToAuthenticationServerReq` (variante MMO, máscara 2) |
| `0x2A83000` / `0x2A83310` | `SAM::CreateUser` / reserva del `ServerUserActor` (0x218) y sus hijos (0xD8) |
| `0x2A838E0` (VM) | buscar usuario por cuenta |
| `0x2A845E0` (VM) | alta y clave del actor |
| `0x1437400` (VM) | enlazar sesión ↔ usuario [S] |
| `0x2C17520` | `UserLogin`: MainUserData + lobby |
| `0x298BA10` → `0x2C3E030` | `LoginCharacterToFieldServerReq` |
| `0x186F570` / `0x20688A0` | cargar ranura de un jugador / cargar componentes desde fichero |
| `exe+0x6CEEDE8` | byte de modo un jugador (=1) |
| `0x2BC4F70` / `0x2BC51E0` / `0x2BC4D50` | ToNear: observadores del sector + sesión propia |
| `0x2AF2E60` | recoger los usuarios observadores de un `ServerFieldSector` (`+0x38` lock, `+0x68` {usuario, refcount}, `+0x70` número) |
| `0x2AF8AF0` | cambio de sector: alta/baja de observadores y diff de aparecer/desaparecer |
| `0x2A759B0` / `0x2A75880` | `vf1` vaciadas de ToNearExceptOneControl/Self |
| `0x2A75D20` | ToStage (claves de usuario → sesiones) |
| `0x2A7D9E0` | `ServerUserActor::vf(0xE8)` = `[+0x1F8]` |

## Scripts (`build/research/session2/`)

| Fichero | Qué hace |
|---|---|
| `login_seq.py` | mensajes vistos por primera vez durante el arranque de una captura |
| `ctxuse.py` | qué campo y vfunc se usa del contexto global por realm |
| `opimm.py` | opcodes usados como inmediatos (mov/cmp) |
| `sectorobs.py` | heurística para encontrar quién toca la lista de observadores de un sector |
| `totarget.py` | de dónde saca la sesión cada `ToTargetSendTargetFinder` |
| `srvnetuse.py` | qué campos de `ServerNet` se leen a partir del global |
| `*.asm` | volcados: login `0x2C48BE0`/`0x2C48570`, personaje `0x2C3E030`, sector `0x2AF8AF0`, buscadores, envío y recepción del cliente |
