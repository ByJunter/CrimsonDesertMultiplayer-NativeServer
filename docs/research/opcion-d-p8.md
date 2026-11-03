# Opción D, pieza P8: el "cliente fino" del invitado (diseño estático)

Build del juego: **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). **Todas las direcciones son
RVA (`exe+…`).** Análisis estático de solo lectura sobre el exe del disco (capstone,
`C:\Emulador CD\build\research\`), más las capturas `captures/cdmp_packets_20261003-105149` y
`-112627` y el log del mod `captures/cdmp_20261003-105149.log`. El juego no se abrió, no se ejecutó
`deploy.ps1` y no se tocó el código del repositorio.

Leyenda:
- **[V]** visto en vivo o en capturas;
- **[E]** leído en el desensamblado;
- **[E-x]** leído en código de `.xtls` (Denuvo) mutado pero legible;
- **[S]** suposición razonada.

Scripts y volcados nuevos: `C:\Emulador CD\build\research\optd\p8\` (lista al final).

---

## Resumen y recomendación

**Qué hay que construir en el PC del invitado (Shadow):**

1. **Salida (C→S): inline hook en la entrada de `NwVirtualSyncSession::Send` `0x14380B0`**, sin
   llamar al original cuando la conexión está en modo remoto. [E]
   - Firma: `int* Send(NwVirtualSyncSession* this, int* res, PacketBuffer* pb)`. El original solo
     hace `push(this+0xE0 = Q0, &pb); *res = 0; return res;` (46 bytes).
   - El hook copia el paquete (`pb+3`, longitud `[pb+0x800A] − 0x1F`) a la cola del túnel.
   - Después **devuelve `pb` a Q3** (`0x35DF50([ClientNet+0x28], &pb)`). Es lo mismo que hace el
     servidor tras procesarlo (`0x27735E4`). El NetworkThread lo libera en la misma vuelta del
     bucle (`0x35BB7B`-`0x35BBE6`, `free([pb+0x8028])`). Así no hay fuga ni doble liberación.
   - **Siempre `*res = 0`.** Con `*res != 0`, `FlushSendQueue` llama a la desconexión
     (`0x35C490` → `.xtls 0x86E5500`). Esta fabrica un `DisconnectToServerAck` 1004 y el búfer
     se pierde.
2. **Entrada (S→C): una "bomba" en el NetworkThread** (recomendada). Es un `create_mid` en
   `0x35BAD4`, el punto del bucle `0x35B800` por el que se pasa en cada vuelta, como mucho cada
   16 ms. Desde ahí se llama a `ClientNet::OnRecvPacket` `0x35CA00(ClientNet, [ClientNet+0x10],
   0, pkt, size)` con búferes nuestros.
   - `OnRecvPacket` **copia** el paquete (`0x1439060` + `memcpy`), así que nuestra memoria no se
     queda en el juego. [E]
   - La inyección actual de `troc_sniffer.cpp` **no basta tal cual**: se cuelga de un paquete
     real que llega a `OnRecvPacket`. Con P8 el servidor local del invitado no manda nada, así que
     los Ack del anfitrión se quedarían esperando para siempre.
   - Alternativa sin hook: empujar a Q1 un `AsyncRequestSend` propio (receta en §3.4). Funciona,
     pero lo libera el **servidor local del invitado** (`0x27725F0`, drenando Q2). Si ese bucle se
     para (pausa, cierre), la memoria crece a 32 KB por paquete. La dejo como plan B.
3. **El `1001`/`1002` sigue local sin hacer nada:** `FlushSendQueue` `0x35BE90` consume el `1001`
   y el `1003` **antes** de llamar a `Send`, y el `1002`/`1004` los fabrica el propio cliente. [E]
4. **Cuándo:** el cliente conecta y hace login de usuario **solo al arrancar**, en cuanto el
   servidor embebido está listo, sin que el jugador pulse nada.
   - Secuencia: `1001` → `ConnectCryptogramToServerReq` (+45 ms) → `LoginUserToFieldServerReq`
     (+60 ms). [E + V]
   - **En la pantalla de título el cliente no manda ninguna Req**: en 90 s de 105149, solo esas
     tres y luego `LoginCharacterToFieldServerReq` al elegir la partida. [V]
   - **El hook de `Send` tiene que estar instalado antes del `1001`.** Hoy el mod instala sus
     hooks en el "late init", unos segundos antes del `1001`: es una carrera.
5. **Decisión por conexión:** el hook detecta una conexión nueva cuando cambia `[this+0xB0]`.
   - Ese campo lo escribe `OnConnect` `0x8CB08E` al procesar el `1002`, antes de que salga
     `ConnectCryptogramToServerReq`. [E]
   - En ese momento decide "remoto" (túnel arriba y sesión 2 del anfitrión lista) o "local"
     (partida normal). Nunca mezcla dentro de una misma conexión.
6. **El servidor local del invitado se queda callado solo:** no recibe ninguna Req y no tiene
   usuario.
   - `ServerTimeInfoRepeatTimer` usa `ToWorld` (lista de usuarios del SAM). [E-x]
   - El latido solo decrementa un contador. [E-x]
   - El cliente no espera nada de él en la pantalla de título. [V]

**Problema del lado del anfitrión (bloquea G2 y también E2):**
- `server.cpp` solo da canal a los handlers con `kind == 4`.
- `ConnectCryptogramToServerReq` 2319 tiene `kind = 0x7FFFFFFE`, y el servidor lo encuentra porque
  `Troc::Find` compara **por bits** (`test [troc+8], mask`, `0x143B351`). [E]
- Resultado: la primera Req del invitado (y el paso 1 del guion E2) se descarta como
  "desconocida" (`g_unknown_from_guest`) y nunca llega el `ConnectCryptogramToServerAck`.
- Lo mismo pasa con `LogoutSessionToServerReq` 2001, el logout nativo que el cliente manda a cada
  sesión al salir (`0x8CAD10`). Su handler `.xtls 0x1089A360` cierra o desconecta al usuario.
- Arreglo propuesto (no aplicado): aceptar `(kind & 4) != 0` excluyendo los temporizadores
  (nombre `*Timer`, `UpdateFieldNightAndDay`, `ReserveShutDownServerReq`), o una lista blanca
  {2319, 2001}. Detalle en §5.1.

**Etapas de prueba (§6):**

| Etapa | Qué prueba |
|---|---|
| G0 | sonda en Shadow, solo registro |
| G1 | bucle local: el hook manda las Req a su propio servidor por nuestro camino |
| G2 | login de usuario remoto; el invitado se queda en el título |
| G3 | partida clonada y carga de zona remota |
| G4 | juego y pausa |
| G5 | salir al título y cortes del túnel |

---

## 1. Ruta de salida del cliente y dónde desviar las Req

### 1.1 La cadena, con evidencia [E]

```
hilo que envía: ClientNet::PostPacket 0x35C670
   alloc(0x8030,0x10) → copia paquete a pb+3 → +0x8008/+0x800A/+0x800C = len+0x1F, +0x800E = 0,
   +0x8010 = índice de sesión (4), +0x8014 = flags, +0x8018/+0x8020 = época de la sesión (+0xB8/+0xB0),
   +0x8028 = pb → push a la cola NO bloqueante [[ClientNet+0xE0]] (0x143AB10)
NetworkThread 0x35B800 (modo virtual: slot 4 = [ClientNet+0x58] existe y su vf(0x58) = true):
   espera Q1 [ClientNet+0x18] hasta 16 ms (0x35B9C6-0x35B9D2, WaitForSingleObject(…, 0x10))
   0x35BAD4: mientras [[ClientNet+0xE0]]+0x58 != 0 → FlushSendQueue(ClientNet, dl = 1) 0x35BB05
   si sacó algo de Q1: OnRecvPacket(ClientNet, [ClientNet+0x10], ?, pb+3, [pb+0x800A]−0x1F) 0x35BB53
                       → push Q2 [ClientNet+0x20] 0x35BB60
   drena Q3 [ClientNet+0x28] (timeout 0): free([item+0x8028]) 0x35BB7B-0x35BBE6
FlushSendQueue 0x35BE90(ClientNet, bool virtual):
   pop de +0xE0 → pb
   op == 1001 → conectar (0x35C230) y fabricar 1002 → free(pb)      [nunca llega a Send]
   op == 1003 → desconectar (0x35C490)              → free(pb)      [nunca llega a Send]
   si no: sesión = [ClientNet+0x38 + pb.idx*8]; comprobaciones de época (+0x8018/+0x8020 frente a
          sesión+0xB8/+0xB0); si la crypto de la sesión está activa, cifra en sitio (vf 0x28);
          si no, exige op == 2319 o sesión->vf(0x58) (virtual = true);
          sesión->vf6 Send(&res, pb) 0x35BFC7; si res != 0 → 0x35C490(…, res, 1) (desconexión)
   con dl = 1 (virtual) NO libera pb tras Send: ahora es de Q0/servidor; con dl = 0 (socket) sí
NwVirtualSyncSession::Send 0x14380B0 → push(Q0 = [this+0xE0] == ServerNet+0x20, &pb)
servidor 0x27733F0: pop Q0 → OnRecvPacket del servidor → push Q3 (0x27735E4) → el cliente libera
```

Volcados: `optd/p8/` y `session2/cli_flush.asm`, `optd/f_35b800_cli_netloop.asm`,
`optd/f_35c670_postpacket.asm`.

### 1.2 `NwVirtualSyncSession::Send` `0x14380B0` [E]

```
0x14380b0: 40 53                 push rbx
0x14380b2: 48 83 EC 20           sub  rsp, 0x20
0x14380b6: 48 8B 89 E0 00 00 00  mov  rcx, [rcx+0xE0]        ; Q0 (== ServerNet+0x20)
0x14380bd: 48 8B DA              mov  rbx, rdx
0x14380c0: 48 8D 54 24 30        lea  rdx, [rsp+0x30]
0x14380c5: 4C 89 44 24 30        mov  [rsp+0x30], r8          ; item = pb
0x14380ca: E8 81 5E F2 FE        call 0x35DF50                ; push(Q0, &pb)
0x14380cf: 48 8B C3              mov  rax, rbx
0x14380d2: C7 03 00 00 00 00     mov  dword [rbx], 0          ; *res = 0 siempre
0x14380d8: 48 83 C4 20 / 5B / C3
```

- **Firma:** `int* __fastcall Send(NwVirtualSyncSession* this, int* res, PacketBuffer* pb)`;
  devuelve `res`. Es la vf6 (`+0x30`) de la vtable `0x5726248`.
- **Sin llamadas directas** (xrefs = 0). Solo se llega por la vtable, que usan el ctor `.xtls
  0x86E4310` y el dtor `0x1438010`.
- Hay una sola instancia: `[ClientNet+0x10]`, la sesión por defecto que crea el arranque de
  `ClientNet`. `0x35C230` la pone en el slot 4 cuando el `1001` lleva `payload[0] = 1`
  (`0x35C2A9`-`0x35C2BB`).
- **En el PC del invitado, todas las llamadas a `0x14380B0` son Req para su servidor.** No hay otra
  ruta C→S del cliente:
  - `0x2774F00` también empuja a Q0, pero es el servidor despertando sus propios hilos;
  - las sesiones socket (`NwSyncSession`, vf6 `0x1437DA0`) no se usan en un jugador.
- Otros métodos de `NwVirtualSyncSession` (vt `0x5726248`):

  | Método | Dirección | Qué hace |
  |---|---|---|
  | vf5 Connect | `0x35EA90` | stub que devuelve una constante |
  | vf9 socket | `0x3600B0` | devuelve 0 |
  | vf11 (`+0x58`) "es virtual" | `0x3600C0` | `mov al,1; ret` |

  El "connect" virtual siempre funciona y no habla con el servidor.

### 1.3 Ciclo de vida del `PacketBuffer` C→S (0x8030 B) [E]

| Paso | Quién | Dónde |
|---|---|---|
| Reserva `alloc(0x8030, 0x10)` | `PostPacket` (hilo que envía) | `0x35C6A8`: `0x47EFA78` (global con lock) si `TLS[0x1FD] != 0`; si no, `0x47EF9B0`. `+0x8028 = pb` |
| Cola de salida | `PostPacket` | push no bloqueante a `[[ClientNet+0xE0]]` (`0x143AB10`) |
| Envío | NetworkThread, `FlushSendQueue(dl=1)` | `Send` lo pasa a Q0; **`FlushSendQueue` ya no lo toca** (`0x35BFE8`: con `dl = 1` sale sin liberar) |
| Proceso | servidor `0x27733F0` | `OnRecvPacket` lo lee; después `push(Q3 = ServerNet+0x78 == ClientNet+0x28, &pb)` (`0x27735E4`) |
| Liberación | NetworkThread | pop de Q3 con timeout 0; `[pb+0x8000] = 0`; `free([pb+0x8028])` con `0x47F03BC` o `0x47F02BC` según `TLS[0x1FD]` (`0x35BB90`-`0x35BBC5`) |
| Casos que libera `FlushSendQueue` | NetworkThread | `1001`, `1003` y el modo socket (`dl = 0`) (`0x35C0F2`-`0x35C136`) |
| Rareza nativa | — | con `dl = 1`, si la sesión no existe, la época no cuadra o `Send` falla, el búfer **se pierde** (no se libera). No nos afecta |

**Qué hacer con `pb` si no va a Q0:** empujarlo **una vez** a Q3, exactamente como el servidor.
- El NetworkThread lo libera en la misma vuelta: el drenaje de Q3 va después del de `+0xE0`, en el
  mismo hilo que ejecuta nuestro hook.
- No liberarlo nosotros: habría que reproducir la elección `0x47F02BC`/`0x47F03BC`, y si además se
  empuja a Q3 habría doble liberación.
- No devolver `*res != 0`: se desconecta y se fabrica un 1004.
- No volver a tocar `pb` después del push.
- Si el push falla (excepción): **perder** el búfer (32 KB) y apagar P8. Nunca empujarlo a Q0,
  porque el servidor local lo procesaría (dos mundos).

### 1.4 Diseño del hook de salida

```cpp
// Inline hook (safetyhook) en la ENTRADA de 0x14380B0; corre en el NetworkThread.
int* __fastcall p8_send(void* self, int* res, uint8_t* pb) {
    const uint64_t epoch = peek<uint64_t>(self + 0xB0);   // lo escribe OnConnect 0x8CB08E
    if (epoch != g_epoch) {                                // conexión nueva (tras el 1001/1002)
        g_epoch = epoch;
        g_remote = g_opt.remote && tunnel_up() && host_session2_ready();
        log("P8: conexión nueva (época {:X}) → modo {}", epoch, g_remote ? "REMOTO" : "local");
        if (g_remote) tunnel_control(P8Reset{epoch});      // el anfitrión rehace la sesión 2 si hace falta
    }
    if (!g_remote) return g_tramp.unsafe_fastcall<int*>(self, res, pb);   // partida normal
    // [pb+0x8000] == pb, [pb+0x8028] == pb, len = [pb+0x800A]-0x1F en [5, 0x7FDA], parse() OK
    // crypto apagada: byte [[self+0x98]+0x24] == 0 (si no, la trama ya va cifrada: 0x35BF99)
    copy_to_tunnel(pb + 3, len);                           // troc::PacketQueue, sin bloquear
    void* item = pb;
    guarded_push(*(void**)(clientnet + 0x28), &item);       // Q3: lo libera el NetworkThread
    *res = 0;
    return res;
}
```

**Alternativas descartadas o de reserva:**

| Opción | Pros | Contras |
|---|---|---|
| **A. Inline hook en `0x14380B0`** (recomendada) | función mínima y sin RIP-relativo en sus primeros 16 B; cada llamada es exactamente una trama C→S; conserva el orden de `FlushSendQueue` y sus filtros (época, `1001`/`1003` fuera) | parchea código del juego (6 B reubicados) |
| B. Vtable privada para `[ClientNet+0x10]` (como D-7 en el anfitrión) | sin parche de código | escribe el vptr de un objeto que crea el juego (ctor en `.xtls`); si `ClientNet` se recrea, se pierde en silencio |
| C. Saltarse el original en `PostPacket` `0x35C670` | no hay búfer que liberar | rompe el orden con el `1001`: `ConnectCryptogram` saldría por el túnel antes de que el NetworkThread procese el `1001` y fabrique el `1002`. Pierde los filtros de época. Corre en muchos hilos (393 llamadas) |
| D. Mid-hook en `0x35BFC7` (la llamada a `vf6`) | — | a mitad de función y sin ventaja sobre A |

---

## 2. La conexión `1001`/`1002` y el momento del login

### 2.1 Qué tiene que seguir siendo local [E]

| Trama | Quién la crea | Qué pasa | ¿Pasa por `Send`? |
|---|---|---|---|
| `1001` connect (`01 "127.0.0.1"…`, 0xCF B) | `0x8C8520` (ruta local, `payload[0] = 1`, sesión 4) o `ClientNet::Connect` `0x35C150` (ruta socket) | `FlushSendQueue` → `0x35C230`: el slot 4 pasa a ser `[ClientNet+0x10]`; vf5 Connect es un stub | **no** (`0x35C013`) |
| `1002` `ConnectToServerAck` | `FlushSendQueue` `0x35C0A9`-`0x35C0ED` lo fabrica y lo mete en `OnRecvPacket` | su vf2 `.xtls 0xA799780` [E-x] → `0x8CAFF0` (hilo principal): `sesión+0xB0` = época nueva, `+0xB8 = 0x7FFF…`, `+0xA8 = 0`; prepara la clave RSA (`0x12B39F0`) y sale `ConnectCryptogramToServerReq` | — |
| `1003` disconnect | cliente | `0x35C490` → `.xtls 0x86E5500` [E-x]: vacía el slot, reinicia la crypto, fabrica `DisconnectToServerAck` 1004 y lo mete en `OnRecvPacket` (`0x86E55AA`); `0x35C560` libera | **no** (`0x35BFF6`) |

- **No hay que hacer nada para que sigan siendo locales**: el hook de `Send` no los ve nunca.
- Tampoco hay que avisar al servidor del anfitrión: no tiene concepto de conexión. `OnConnect`,
  `OnDisconnect` y `Heartbeat` son stubs (D0 §10).
- **Sí conviene un mensaje de control propio del túnel** ("conexión nueva, época X"), porque el
  anfitrión no se entera de una reconexión del invitado (§5, salir al título).
- A partir de `ConnectCryptogramToServerReq` **todo** va al anfitrión:
  - la clave RSA es la del invitado, así que el servidor del anfitrión cifra el Ack con ella y el
    invitado lo descifra sin cambiar nada (`opcion-d-login.md` §4.1);
  - el anfitrión reescribe la cuenta de `LoginUserToFieldServerReq` (`server_inject`, cuenta ≠ 0).

### 2.2 ¿Al arrancar o al pulsar? Al arrancar [E + V]

- **[E] Cadena del connect.** Un paso del arranque, `0xA0C860` (sin llamadas directas: método de un
  estado por vtable), comprueba que el servidor embebido está listo:
  - `0x2A84520` sobre `[x+0xE8]`;
  - el estado de sus hilos con `0x2774F00`/`0x2774FC0`.

  Después llama a `0x8B48B0([exe+0x6D691D0], 1)` → `0x8C8520`. Si el slot 4 sigue vacío, este
  publica el `1001` con `PostPacket(ClientNet, …, 4, …)` (`0x8C8689`-`0x8C86DD`). La misma ruta se
  usa tras volver al título: `0x8B48B0` espera a que se destruyan los actores y conecta (flag
  `+0xCA3`).
- **[V] Tiempos (105149):**
  - el mod instaló los hooks a las 10:51:49,97 ("late init");
  - el primer `Present` fue a las 10:51:52,6;
  - el `1001` llegó segundos después (≈ 4-10 s tras el late init, según cómo se cuente);
  - `ConnectCryptogramToServerReq` a +46 ms y `LoginUserToFieldServerReq` a +61 ms;
  - el jugador estuvo **90 s** en el título; `LoginCharacterToFieldServerReq` llegó a los 90,45 s;
  - en 112627 (jugador rápido), a los 3,4 s.
- **Conclusión:**
  - `ConnectCryptogram` y `LoginUser` salen solos al arrancar, antes de que aparezca o se use la
    pantalla de título;
  - `LoginCharacter` sale **al elegir partida** (lleva la ranura, u32 @9).

### 2.3 ¿Qué Req manda la pantalla de título? Ninguna [V]

En 105149, entre t = 0 y t = 90,45 s:

| Dirección | Mensajes |
|---|---|
| **C→S** | solo `1001`, `ConnectCryptogramToServerReq` y `LoginUserToFieldServerReq`, y al final `LoginCharacterToFieldServerReq` |
| S→C | `ConnectToServerAck`, `ConnectCryptogramToServerAck`, `MainUserDataStartAck`, `CharacterLobbyDataAck`, `MainUserDataEndAck`, `ClearCharacterLobbyDataForSinglePlayAck` y **19 `ServerTimeInfoAck`** (uno cada 5 s) |

- **No hay `SaveDataSlotRefreshReq` 2579.** La lista de ranuras del título la lee el cliente de su
  propio disco, sin el servidor. [V + S]
- **Consecuencia para P9:** la ranura que el invitado elige es un número, y el servidor del
  anfitrión cargará **esa ranura del disco del anfitrión** (`0x186F570`).
- `CharacterLobbyDataAck` ("Kliff", `characterNo = 0x7FFF…`) lo empuja el servidor como respuesta
  a `LoginUser`. Con P8 lo manda el del anfitrión, con los mismos datos de un jugador.

### 2.4 Consecuencias para P8

1. **El hook de `Send` tiene que estar puesto antes del `1001`.**
   - El late init actual espera a `WorldSystem` + ventana visible + 5 s (`mod.cpp:584`). El margen
     es de pocos segundos y en Shadow puede ser menor.
   - **Propuesta:** instalar P8 en un hilo propio en cuanto `[exe+0x6D69170]` (`ClientNet`) exista
     y el prólogo de `0x14380B0` cuadre.
   - Si al instalar `[ClientNet+0x58] != 0`, el cliente ya se conectó a su servidor local: P8 no
     se activa y escribe en el log "demasiado tarde, reinicia el juego".
2. **Encender P8 a mitad de partida no sirve.**
   - El cliente ya tendría `MainUserData` del servidor local (usuario `0x90100000`).
   - El anfitrión asignaría otras claves al usuario 2.
   - El servidor local, ya con usuario, seguiría mandando `ToWorld` (dos mundos).
3. **Antes de arrancar el juego del invitado tienen que estar arriba el túnel y la sesión 2 del
   anfitrión** (el anfitrión ya en el mundo).
   - El túnel del mod conecta en milisegundos al arrancar el proceso (`Session: connected` a las
     10:51:38,8 en 105149).
   - Si no está listo cuando sale la primera trama de la conexión, la conexión queda en modo local
     (partida normal).
   - No sé si el cliente tolera esperar mucho el `ConnectCryptogramToServerAck` [S]. No conviene
     retener las Req.

---

## 3. Entrada al cliente (S→C)

### 3.1 `ClientNet::OnRecvPacket` `0x35CA00` [E]

- **Firma:** `void OnRecvPacket(ClientNet* this, NwSession* s, (sin uso), const u8* pkt,
  u16 size /*[rsp+0x28]*/)`.
  - El tercer argumento no lo lee: en `0x35BB53` el bucle ni siquiera carga `r8`.
- **Qué hace:**
  1. `size − 3 ≤ 0x7FD7`.
  2. Si `s` tiene crypto activa (`[s+0x98]->vf(0x10)`), descifra en sitio (vf `0x30`).
  3. `Troc::Find(op, 1)` y límites de tamaño `+0x18`/`+0x1A`.
  4. Exige el bit 9 de `flags_16`; el bit 10 pasa por `[exe+0x6D69178]->vf(0x20)`.
  5. **`TrBufferPoolValue` nuevo (`0x1439060`) con una copia del paquete** (`0x35CB65`).
  6. `ctx+0x48 = s`, `+0x54/+0x58 = *(s+0x88)`.
  7. `troc->vf1` encola en `+0xE8`, el hilo principal. Si devuelve el código `[0x6CDB9D0]`, va a
     **`+0xF0`** (la cola de reintentos).
  - **No se queda con el puntero `pkt`**: el llamante puede liberar su búfer al volver.
- **Llamadas:**

  | Sitio | Qué es |
  |---|---|
  | `0x35BB53` | bucle (modo virtual) |
  | `0x35C0ED` | 1002 fabricado |
  | `0x35C951` | modo socket |
  | `.xtls 0x86E55AA` | 1004 fabricado |

  Todas en el NetworkThread, salvo que la desconexión venga de otro sitio.

### 3.2 Por qué la inyección actual no sirve tal cual

`troc_sniffer.cpp::on_recv_packet` es un mid-hook en la entrada de `0x35CA00`. Mete nuestros
paquetes **delante de un paquete real** (para la copia 4c llegaban Ack del servidor local a
~60/s).
- En P8 el servidor local del invitado **no manda nada**.
- La única llamada real a `OnRecvPacket` sería el `1002` fabricado (y el `1004` al desconectar).
- Sin otra cosa, los Ack del anfitrión se quedarían en la cola indefinidamente.
- Hace falta un disparador que corra siempre: la "bomba" de §3.3, o Q1 (§3.4).

### 3.3 Opción recomendada: bomba en `0x35BAD4` [E]

- **Punto:** `0x35BAD4`, `mov rax, [r15+0xE0]` (`49 8B 87 E0 00 00 00`, 7 B, sin RIP-relativo).
  - Sin saltos que caigan en `0x35BAD5`-`0x35BAE3`; él mismo es destino de `0x35BAC6`/`0x35BACE`,
    lo que no molesta a un mid-hook.
  - Ahí llega el bucle **en cada vuelta del modo virtual**, después de la espera de Q1 (paquete o
    timeout de **16 ms**) y antes de vaciar la salida.
  - `r15 = ClientNet`.
  - Firma AOB única (39 B mínimo): `49 8B 87 E0 00 00 00 48 8B 38 48 89 7D C7 48 8B 07 48 8B CF
    FF 50 08 90 8B 5F 58 48 8B 07 48 8B CF FF 50 10 85 DB 74`.
- **Qué hace el mid-hook:**

  ```cpp
  void p8_pump(safetyhook::Context& ctx) {            // NetworkThread
      if (!g_remote) return;
      const uintptr_t net = ctx.r15, s = peek<uintptr_t>(net + 0x10);
      for (auto& p : g_from_host.take_up_to(4096))     // todo lo recibido, en orden
          guarded_on_recv(net, s, 0, p.data(), (uint16_t)p.size());   // 0x35CA00, con SEH
  }
  ```

- **Por qué es la más segura:**
  1. hilo nativo (el mismo que llama a `OnRecvPacket` en `0x35BB53`);
  2. función nativa que el mod ya llama desde 4c (`guarded_inject`);
  3. memoria nuestra, que `OnRecvPacket` copia, sin ciclo de vida en el heap del juego;
  4. no depende del servidor local del invitado;
  5. solo corre en modo virtual (slot 4 conectado), así que no entrega nada antes del `1001`.
- **Latencia:** hasta 16 ms (la espera de Q1).
- **Ráfaga:** la carga de zona manda 13.000-42.000 Ack en ~12 s. Se entregan todos los que haya en
  cada vuelta; `OnRecvPacket` es barata (reserva, `memcpy`, encolar).
- **Convivencia con el sniffer:**
  - el mid-hook de entrada de `0x35CA00` seguirá capturando estos paquetes, lo que viene bien para
    el log;
  - la inyección de copias (4c/coop) tiene que estar **apagada** en el invitado P8.

### 3.4 Alternativa: empujar a Q1 un `AsyncRequestSend` propio [E]

- **Qué pide el NetworkThread a un búfer de Q1** (`0x35BB37`-`0x35BB53`):
  - solo `[pb+0x800A]` (u16: tamaño de trama, paquete = `−0x1F`) y los bytes de `pb+3`;
  - como sesión usa siempre `[ClientNet+0x10]`;
  - después hace `push(Q2 = [ClientNet+0x20] == ServerNet+0x80 del invitado, &pb)`.
- **Quién lo libera:** el bucle de diferidas del **servidor local del invitado**, `0x27725F0`
  (`0x2772800`-`0x27728E0`):
  - `rq = pb − 0x30`;
  - `xadd [rq+0x8050], −1`;
  - **si el valor previo era 1**, limpia `+0x8038/+0x8040/+0x8048/+0x8050`, pone las vtables
    `AsyncRequestSend`/`AsyncRequest` y hace `free([rq+0x8058])` (`0x47F03BC`/`0x47F02BC`).
  - Si el valor previo no era 1, no hace nada. Contaba con que `SendPacket` hiciera su propio
    `xadd`.
- **"El servidor no tendría la referencia":** no la necesita.
  - La "referencia" es solo el contador dentro del búfer. Con `[rq+0x8050] = 1` y
    `[rq+0x8058] = rq`, el drenaje lo libera.
  - Si ponemos otro valor (0, 1.000.000), el búfer se pierde (fuga), pero no hay doble liberación.
- **Receta:**
  1. `rq = alloc(0x8060, 0x10)` con el asignador del juego (`0x47EFA78`, como P2 en el anfitrión).
  2. Rellenar:
     - `pb = rq+0x30`;
     - `pb[0..1] = len+0x1F`, `pb[2] = canal`, paquete en `pb+3`;
     - `[pb+0x8000] = pb` y `[pb+0x8008] = [pb+0x800A] = len+0x1F`;
     - `[rq+0x8050] = 1` y `[rq+0x8058] = rq`.
  3. `0x35DF50([ClientNet+0x18], &pb)`, desde cualquier hilo (es thread-safe). El semáforo
     despierta al NetworkThread al instante.
- **Pros:**
  - no hace falta ningún hook para la entrada;
  - latencia 0;
  - orden FIFO con lo que hubiera en Q1.
- **Contras (por qué es plan B):**
  1. depende de que el **servidor local del invitado** drene Q2;
  2. si su bucle de diferidas se para (pausa del invitado: no sé si `0x27725F0` se detiene en el
     menú [S]; cierre del servidor), quedan **32 KB por paquete**. Con unos cientos de Ack por
     segundo en juego son varios MB/s, y en una pausa de minutos, GB;
  3. 32 KB de heap del juego por Ack (como el servidor nativo, pero sin su ritmo).

### 3.5 Recomendación

**Bomba en `0x35BAD4` + `OnRecvPacket`.**
- Usar Q1 solo si un mid-hook en el bucle de red diera problemas.
- En ese caso, vigilar el tamaño de Q2 (`[ServerNet+0x80]+0x58`) y no empujar si crece.

---

## 4. Que el servidor local del invitado no hable ni confunda

| Fuente posible | Qué pasa con P8 | Evidencia |
|---|---|---|
| Respuestas a Req (`ToTarget(sesión)`) | no recibe ninguna Req: Q0 queda vacía | hook de §1 [E] |
| Usuario en la sesión | `sesión+0x90 = 0` para siempre: sin `LoginUser` no se crea usuario, y sin usuario solo pasarían los Troc de login | `OnRecvPacket` del servidor (`opcion-d-login.md` §5) [E] |
| `ServerTimeInfoAck` periódico (cada 5 s) | lo manda `ServerTimeInfoRepeatTimer` 1022 con **`ToWorldSendTargetFinder`** (lista de usuarios del SAM), así que sin usuarios no va a nadie | `.xtls 0x108552C0`, `0x108552FA`-`0x10855376` [E-x] |
| Latido | `HeartbeatToParentRepeatTimer` solo decrementa un contador; `HeartbeatReq` 1008 lo manda el cliente solo en modo socket (`0x35BD2C`, con `+0x112`) | D0 §10 [E-x], `0x35BD2C` [E] |
| Temporizadores de usuario (1007, 1023) | se encolan por clave de usuario: no hay | D0 §10 [E] |
| `ToSessionList` (`0x2A75AE0`) | 3 usos (`0x2ACA390`, `.xtls 0x10650890`, `0x10651260`), con listas de sesiones sacadas de actores del mundo; sin mundo, nada | [E] + [S] |
| Guardado automático del servidor (2077, 2011, 2070) | actúa sobre usuarios o partidas cargadas: no hay | [S] |
| El cliente esperando algo del servidor local | en el título no manda nada [V]; el arranque solo exige que el servidor **esté listo** (`0xA0C860` → `0x2A84520`, `0x2774F00`) antes del `1001` [E] | **no apagar el servidor local**: el connect lo espera |

- **Conclusión:** basta con no darle ninguna Req. No hay que pararlo ni parchearlo [E + S].
- Queda una duda [S]: código del cliente que lea directamente estructuras del servidor en el mismo
  proceso (atajos de un jugador). Solo he visto el de arranque. Si existiera otro, devolvería
  "vacío", porque el servidor local no tiene mundo. Se vigila en G3.

---

## 5. Riesgos

| Riesgo | Qué pasa | Qué hacer |
|---|---|---|
| **5.1 `ConnectCryptogramToServerReq` rechazada por el anfitrión** | `server.cpp::load_channels` solo da canal a `kind == 4`. 2319 tiene `kind = 0x7FFFFFFE` (el servidor la acepta: `Troc::Find` hace `test [troc+8], mask`, `0x143B351`). `session2.cpp::inject_or_kill` la descarta como desconocida, así que **no llega el Ack 2840: G2 y E2 se quedan en el paso 1** [E] | aceptar `(kind & 4) != 0` excluyendo temporizadores (nombre `*Timer`, `UpdateFieldNightAndDay` 2017, `ReserveShutDownServerReq` 2790); `OnConnect`/`OnDisconnect`/`Heartbeat` son stubs inofensivos. También entra `LogoutSessionToServerReq` 2001 (§5.6) |
| 5.2 Guardar | Las Req de guardado (`SaveAutoReq` 3037, `SaveToFileCurrentPlayerReq` 2910, `SaveDataSlotRefreshReq` 2579, `DeleteSaveDataReq`) son Req **al servidor**: el guardado lo hace el servidor. Con P8 irían al anfitrión, que las bloquea por nombre (`Save*`: se descartan y se cuentan). El invitado **no recibe `SaveToFileCurrentPlayerAck` 3016** y puede quedarse con la UI de "guardando" [S]. Su disco no se escribe en P8: su servidor no tiene usuario y el cliente no guarda por su cuenta [S]. **Su progreso se pierde** (P9). Los temporizadores de guardado del anfitrión guardarían al usuario 2 sobre la ranura del anfitrión: P6 lo bloquea | G3/G4: anotar qué hace la UI; más adelante, Ack falso o guardado en el invitado (P9) |
| 5.3 Pausa | No hay Req de pausa: el menú para el reloj y el mundo del propio PC (§2.10). La pausa del invitado no para al anfitrión; mientras tanto el invitado sigue recibiendo y los Ack se acumulan en `+0xE8` (como en el cooperativo actual, `pausa-copia-congelada.md` §1.2). La pausa del anfitrión congela su servidor y, con él, al invitado | medir en G4 |
| 5.4 `ScopeSenderAck` 2042 | Llega entero en la trama del anfitrión (P3 copia en `Send`). Su vf2 `0xCB45E0` → `0xCB4160` reparte los sub-paquetes en el hilo principal. No hay que hacer nada. Solo los logs y estadísticas lo ven como un sobre | desempaquetar en el log si hace falta |
| 5.5 `ClientNet+0x112` / `+0xF0` | `+0x112` **no es "conectado"**: es "red arrancada" (`= 1` en el arranque `0x35B097`, `= 0` en la parada `0x35B32C`); el NetworkThread espera a que valga 1 (`0x35B6F2`) y solo condiciona el latido socket y `LogoutSession`. `+0xF0` es la cola local de **Ack aplazados**: `vf1`/`ExecuteTroc` devuelven `[0x6CDB9D0]` (actor no listo, `0x8B53C0`) y `0x35CD50` los reintenta en cada frame en el hilo principal. Ninguno habla con el servidor [E] | nada; contar los aplazados en G3 |
| 5.6 Salir al título / reconectar | El cliente manda `LogoutSessionToServerReq` 2001 a cada sesión viva (`0x8CAD10`), se desconecta en local (1003/1004 fabricados) y reconecta (`1001`). Su `LoginUser` llegaría a la sesión 2, que ya tiene usuario 2 con la misma cuenta, así que entra en la rama de relogin (`0x2C17AB0`) [S] | el hook ve la época nueva y avisa (`P8Reset`); el anfitrión pasa el 2001 (logout nativo, `.xtls 0x1089A360`: `0x2068340(actor, 4, …)` o `Close`) o rehace la sesión 2 |
| 5.7 Corte del túnel | El cliente del invitado se queda colgado esperando Ack; no hay timeout conocido | mostrar aviso; cerrar sin guardar |
| 5.8 Cifrado | Si la crypto de la sesión del invitado se activara, `FlushSendQueue` cifraría antes de `Send` (`0x35BF99`) y `OnRecvPacket` descifraría lo nuestro. En el loopback sigue apagada [V]; con el anfitrión los mensajes son los mismos [S] | G0/G2: leer el byte `[[ClientNet+0x10]+0x98]+0x24`; si vale ≠ 0, P8 se apaga |
| 5.9 Carrera de instalación | Si el hook llega tarde, el invitado ya hizo login en local (§2.4) | instalación temprana y comprobar `[ClientNet+0x58]` |
| 5.10 Spawns pedidos por el cliente | `ActivateSpawningPoolReq`, `AutoSpawnRoadReq`… del invitado en la zona del anfitrión pueden duplicar NPC (`opcion-d-login.md` §4.5) [S] | observar en G3; P10 |
| 5.11 Ranura y `characterNo` | El número de ranura del título del invitado se aplica al disco del anfitrión; con un clon, `characterNo`/`itemNo` repetidos [S] | P9 |

---

## 6. Etapas de prueba en Shadow

Opción propuesta: `[p8] mode = off | observe | loopback | remote`. Todo apagado por defecto. Copia
de las partidas antes de cada etapa que escriba; cerrar sin guardar.

| Etapa | Dónde | Hooks / opción | Qué se espera en el log | Decide |
|---|---|---|---|---|
| **G0** sonda | Shadow solo | `mode=observe`: hook de `Send` que llama al original y solo cuenta; bomba en `0x35BAD4` que solo cuenta vueltas | `P8: hook Send instalado, ClientNet+0x58 = 0 (antes del connect)`; `P8: conexión nueva (época …)`; primeras tramas `2319`, `2490` y, al elegir partida, `3093`; `crypto byte = 0`; `bomba: ~60+ vueltas/s` (al menos 1 cada 16 ms); margen en ms entre la instalación y el `1001` | que llegamos antes del `1001`, que la crypto está apagada y que la bomba gira |
| **G1** bucle local | Shadow solo | `mode=loopback`: el hook copia el paquete, empuja **un `pb` nuevo** a Q0 (receta P2) con su propio `free` en `+0x8028` y devuelve el original a Q3. La bomba reinyecta una copia de cada `ServerTimeInfoAck` recibido (idempotente) | partida normal de un jugador durante 10 min; contadores `C>S copiados = Q3 devueltos`; memoria del proceso estable; `bomba: N ServerTimeInfoAck reinyectados` sin errores | que el ciclo de vida del búfer y la llamada a `OnRecvPacket` desde la bomba son correctos |
| **G2** login remoto | 2 PC; anfitrión en el mundo con `accept_tunnel=1` y **el arreglo §5.1** | `mode=remote` en el invitado; él se queda en el título | invitado: `modo REMOTO`, `C>S 2319 → túnel`, `S>C 2840 entregado`, `2490 →`, `MainUserDataStartAck 0x9010000x`, `CharacterLobbyDataAck`, `MainUserDataEndAck`, `ServerTimeInfoAck` cada 5 s; su servidor local sin usuario (sonda: `ServerNet+0x18 user = 0`, 0 tramas en Q0). Anfitrión: `user 2 key 0x9010000x`, 2 `ServerUserActor` | login remoto y servidor local mudo |
| **G3** partida clonada | 2 PC; P6 activo en el anfitrión | el invitado elige **la misma ranura** que el anfitrión | `3093 →`; `MainPlayerDataStartAck` (`PLAYER_KEY`); `MoveFieldAck` tipo 3 → escena de aparcamiento → `MoveFieldCompleteReq` → `LoadingCompleteReq` → `GameLoadingStartReq` → `MoveFieldAck` tipo 1 → ráfaga (13-42 mil Ack, 0 perdidos) → `LoadingCompleteReq` #2 → `Local player: in world`; Ack aplazados en `+0xF0`; `save Req seen` / bloqueadas | la carga de zona remota (E6) |
| **G4** juego | 2 PC | + P5 (`relay_fix=1`) en el anfitrión | 10 min: los dos se ven moverse; reloj alineado por `ServerTimeInfoAck`; pausas de cada lado (medir congelación); un intento de guardar del invitado (UI) | pausa y guardado |
| **G5** robustez | 2 PC | — | el invitado sale al título: `2001 →`, `época nueva → P8Reset`, el anfitrión cierra o rehace la sesión 2; corte del túnel: aviso y sin cierre del juego | reconexión |

---

## 7. Direcciones, firmas y prólogos

Primeros bytes según `optd/p8/p8_points.txt`; ninguna tiene RIP-relativo en los primeros 16 B ni
saltos que caigan en su ventana.

| RVA | Qué es | Firma | Primeros bytes (cortes de instrucción) | Uso en P8 |
|---|---|---|---|---|
| `0x14380B0` | `NwVirtualSyncSession::Send` (vf6, vt `0x5726248`) | `int* (VSess* this, int* res, PacketBuffer* pb)` | `40 53` \| `48 83 EC 20` \| `48 8B 89 E0 00 00 00` \| `48 8B DA` \| `48 8D 54 24 30` (cortes 2, 6, 13, 16, 21); AOB `nw_virtual_sync_session_send` (32 B, única) | **inline hook de salida** |
| `0x35BAD4` | bucle NetworkThread, tras la espera de Q1 (modo virtual) | mid-hook; `r15 = ClientNet` | `49 8B 87 E0 00 00 00` \| `48 8B 38` \| `48 89 7D C7` (cortes 7, 10, 14); AOB única de 39 B en §3.3 | **bomba de entrada** |
| `0x35CA00` | `ClientNet::OnRecvPacket` | `void (ClientNet*, NwSession* s, ?, const u8* pkt, u16 size@[rsp+0x28])` | `48 89 5C 24 08` \| `48 89 74 24 10` \| `48 89 7C 24 18` \| `55` (AOB `troc_client_on_recv_packet`) | se llama desde la bomba; el mid-hook de captura sigue |
| `0x35C670` | `ClientNet::PostPacket` | `int* (ClientNet*, int* res, u32 idx, const u8* pkt, u16 size, u8 canal, u16 flags)` | `48 89 5C 24 10` \| `4C 89 4C 24 20` \| `55` … | sniffer (ya enganchado); ve el `1001` |
| `0x35BE90` | `ClientNet::FlushSendQueue` | `void (ClientNet*, bool virtual)` | `48 8B C4` \| `53` \| `55` \| `56` \| `57` … | no se engancha |
| `0x35B800` | bucle del NetworkThread | — | — | referencia |
| `0x35DF50` | push de cola bloqueante | `void (Queue*, void** item)` | `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F2 48 8B F9` (no única: sale del `E8` en `0x14380B0+0x1A`) | devolver `pb` a Q3 |
| `0x35E030` | pop de cola bloqueante | `void (Queue*, int* res, void** out, u32 ms)` | D0 §9 | — |
| `0x35C230` | connect del cliente (slot ← `[ClientNet+0x10]` si `payload[0] = 1`) | `int* (ClientNet*, int* res, u32 idx, const u8* pkt, NwSession** out)` | — | referencia |
| `0x35C150` | `ClientNet::Connect` (publica el `1001` de socket) | `int* (ClientNet*, int* res, u32 idx, const u8* info0xC9)` | `48 89 5C 24 08 48 89 6C 24 10 …` | referencia |
| `0x8C8520` | flujo de login: conecta (`1001` local `"127.0.0.1"`, sesión 4) | — | `48 89 5C 24 08 48 89 74 24 10 4C 89 44 24 18 55 57 41 54 41 56` | referencia (momento del connect) |
| `0x8B48B0` / `0xA0C860` | llamadas del connect (recolección de actores / paso de arranque que espera al servidor) | — | — | referencia |
| `0x8CAFF0` | `OnConnect` (desde el vf2 de `1002`, `.xtls 0xA799780`): `sesión+0xB0` = época nueva | — | — | **detección de conexión nueva** (`[this+0xB0]`) |
| `0x35C490` → `.xtls 0x86E5500` | desconexión local (fabrica el 1004) | `(ClientNet*, idx, motivo, bool fabricar)` | — | evitar llegar aquí (`*res = 0`) |
| `0x8CAD10` / `0x8CAE00` | publican `LogoutSessionToServerReq` 2001 (todas las sesiones / sesión 2) | — | — | §5.6 |
| `0x35D0C0` / `0x35CD50` | hilo principal: cola `+0xE8` / reintentos `+0xF0` | — | — | referencia |
| `0x27725F0` | servidor: drena Q2 (`xadd [rq+0x8050], −1` → `free([rq+0x8058])`) | — | — | plan B (§3.4) |
| `0x143B2C0` | `Troc::Find(reg, op, mask)`: `test [troc+8], mask` | — | — | causa de §5.1 |
| `0x47EFA78` / `0x47EF9B0` | `alloc(size, align)` global / local | — | — | plan B |
| `0x47F02BC` / `0x47F03BC` | `free` | — | — | lo usa el juego |

**Campos:**

| Objeto | Campo | Qué es |
|---|---|---|
| `ClientNet` `[exe+0x6D69170]` | `+0x10` | `NwVirtualSyncSession` (persistente) |
| | `+0x18` | Q1 |
| | `+0x20` | Q2 |
| | `+0x28` | Q3 |
| | `+0x58` | slot 4 (≠ 0 tras el `1001`) |
| | `+0xE0` | cola no bloqueante de salida |
| | `+0xE8` / `+0xF0` | colas del hilo principal |
| | `+0x111` | bucle activo |
| | `+0x112` | red arrancada |
| `NwVirtualSyncSession` | `+0x98` | crypto (byte `+0x24`) |
| | `+0xA0` | RSA |
| | `+0xA8` | — |
| | `+0xB0` | época (`OnConnect`) |
| | `+0xB8` | — |
| | `+0xE0` | Q0 |
| `PacketBuffer` C→S | — | D0 §4 |

---

## 8. Scripts y volcados (`C:\Emulador CD\build\research\optd\p8\`)

| Fichero | Qué es |
|---|---|
| `p8_points.py`, `p8_points.txt` | prólogos, cortes, RIP-relativo y destinos de salto de cada punto de enganche |
| `sigs_p8.txt` | AOB únicas (`0x35BAD4`, `0x14380B0`) |
| `f_35c230_connect.asm` | connect del cliente |
| `f_35ca00_onrecv.asm` | `ClientNet::OnRecvPacket` |
| `f_35d0c0_procrecv.asm` | cola del hilo principal y aplazados |
| `f_8c8520.asm`, `f_8c7d00.asm`, `f_8b48b0.asm`, `f_a0c860.asm` | quién publica el `1001` y cuándo |
| `f_8caff0_onconnect.asm` | `OnConnect` (época `+0xB0`) |
| `f_bdfc10_cryptack.asm` | handler de `ConnectCryptogramToServerAck` (no seguido a fondo) |
| `x_86e5500_disconnect.asm` | desconexión local (`.xtls`) |
| `x_108552c0_srvtime_timer.asm` | `ServerTimeInfoRepeatTimer` con `ToWorld` (`.xtls`) |
| `f_2aca390.asm` | uso de `ToSessionList` |

Volcados previos que se reutilizan: `session2/cli_flush.asm` (`FlushSendQueue`),
`optd/f_35b800_cli_netloop.asm`, `optd/f_35c670_postpacket.asm`, `optd/f_27725f0.asm`,
`optd/f_27733f0_srvloop.asm`.
