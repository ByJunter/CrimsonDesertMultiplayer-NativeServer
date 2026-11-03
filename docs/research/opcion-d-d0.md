# Opción D, etapa D0: incógnitas resueltas en estático

Build del juego: **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). **Todas las direcciones son
RVA (`exe+…`)** salvo que se diga lo contrario. Análisis estático de solo lectura sobre el exe del
disco (capstone, `C:\Emulador CD\build\research\`). El juego no se abrió.

Leyenda:
- **[V]** visto en vivo o en capturas (por informes anteriores);
- **[E]** leído en el desensamblado;
- **[E-x]** leído en código de `.xtls` (Denuvo) **mutado pero legible**;
- **[S]** suposición razonada.

Scripts y volcados nuevos: `C:\Emulador CD\build\research\optd\` (lista al final).

---

## Conclusión (lo primero)

**N = 1, pero no limita nada.** [E] + cuadra con el valor vivo [V]

- `N = param+0x40` (`ServerModuleFrameworkParameter`). Lo escribe el constructor del módulo
  servidor en `0x27BFDB3`: **`N = (byte [módulo+0x320] != 0) ? 1 : 10`**. Ese byte vale 1 en el
  juego (es el que hace arrancar el servidor embebido, `0x27C0336`), así que **N = 1**.
- Comprobación con el valor vivo: `param+0x44` = 15.400 + 101·N. En vivo vale **15.501** ⇒ N = 1.
- **La cuota no se comprueba en ningún sitio.** La reserva de un usuario (`0x2A83310`) y la de un
  actor del mundo (`0x2A82BF0`) solo fallan si **la cola común de 15.501 claves está vacía**
  (`eErrNoInvalidActorKey`). No hay ningún contador por tipo comparado con la cuota. La tabla
  `SAM+0x50` solo se escribe en `SAM::Initialize` y se usa como "descriptor de registro" del actor
  (`actor+0x88`, de donde sale la clase de la clave). La cuota también se copia a una tabla de
  estadísticas global, que nadie lee para decidir.
- **Consecuencia:** un segundo `ServerUserActor` **cabe sin parchear nada**. Consume 1 clave de
  usuario y 100 de hijos de la cola común (hoy quedan unas 15.300 libres). **No hace falta tocar la
  cuota.** Si en E2 la creación fallara, la causa no sería N.

Resumen del resto:

| # | Pregunta | Respuesta | Conf. |
|---|---|---|---|
| 2 | Sesión `+0x90` / `+0xA0` / `+0xC8` | `+0x90` = `ServerUserActor*` (lo único que lee `OnRecvPacket`). El "+0xA0" de cdtools es **del actor hijo** (`ServerChildOnlyInGameActor+0xA0` = usuario dueño), no de la sesión. `sesión+0xA0` es un `unique_ptr` a un struct de 16 B que solo tocan el ctor, el dtor y `Close`. `+0xC8` = la cola de salida, y esa cola **hereda de `WindowsLockForOnlyEngineXXX`** (por eso la RTTI da ese nombre): las dos versiones son ciertas | alta |
| 3 | Pools | **No son pools, son colas de devolución.** Las cuatro colas salen de un mismo bloque `[x+0x140]`: Q0 = `ServerNet+0x20`; Q1 = `sesión+0xC8` = `ClientNet+0x18`; **Q2 = `ServerNet+0x80` = `ClientNet+0x20`**; **Q3 = `ServerNet+0x78` = `ClientNet+0x28`**. Push `0x35DF50(cola, &ptr)`, pop `0x35E030(cola, &res, &ptr, timeout_ms)` | alta |
| 4 | Opción B de P2 | Confirmada. `PacketBuffer` de 0x8030 B del asignador del juego. Campos libres para la marca: `+0x8010..+0x8027`. Detalle en §4 | alta |
| 5 | TLS `+0x1EC` | Lo ponen a 1 los tres procs de hilo del servidor (`0x2773390`, `.xtls 0x100B1F10`, `.xtls 0x100B2F40`). Hay un ayudante de "cambio de realm con ámbito" en `.xtls` | alta |
| 6 | Asignador | `0x1447EF9B0` es **VA**: RVA `0x47EF9B0` = `alloc(size, align)` (heap mimalloc por hilo); RVA `0x47EFA78` = la misma firma con un heap global y lock; liberar con `0x47F02BC` / `0x47F03BC`. La sesión la libera su dtor `0x1437580` | alta |
| 7 | 2508 / 2168 | `ChangeAiControlAck` solo lo escribe el **cheat** `AiControlChangeCheatReq`. `OverriedOwnershipStateTypeyAck` sale al crear actores con descriptor, al cambiar gimmicks y en la muerte/estado. El controlador de un NPC (`actor+0x90`) se fija **al crearlo**, a partir del descriptor/solicitante | media-alta |
| 8 | Quién llama a `0x2A83310` | Solo `SAM::CreateUser` `0x2A83000`, al que llaman los dos login (`0x2C48BE0` campo y `0x2C48570` autenticación) | alta |
| 9 | Prólogos | Los cinco son seguros para un inline hook (sin RIP-relativo en los primeros 20 B). Las dos `vf1` vaciadas son **idénticas** byte a byte salvo los `rel32`: un AOB con comodines da 2 coincidencias. Hay que resolverlas por vtable | alta |
| 10 | Heartbeat y listas | `HeartbeatToParentRepeatTimer` solo decrementa un contador global. `OnConnect/OnDisconnect/HeartbeatReq` son stubs. **No hay lista global de sesiones**: la única referencia es `ServerNet+0x18`. Los usuarios sí van a una lista del SAM (`SAM+0xB8`), en la que `CreateUser` mete al usuario 2 solo | media-alta |

---

## 1. Cuota N de usuarios

### 1.1 De dónde sale cada valor [E]

Orden de arranque del servidor embebido:

```
.xtls 0x9E2C260 (mutado, legible)   pone un byte TLS ofuscado a 1 (realm servidor), llama a:
  0x27BEB50  ServerModule::Initialize(this=módulo, res, …, desc)
    0x27BFB65  alloc(0x1D0) → ServerModuleFrameworkParameter (hereda de NwServerFrameworkParameter)
    0x27BFBA2…0x27BFD15  ctor: todo a 0 (+0x80…+0x98 incluidos, 0x27BFBE5)
    0x27BFD3C…0x27C0008  RELLENO CON CONSTANTES (no hay fichero ni XML):
        +0x30 = tabla[0x6A65A0A][modo 0x6A65564=4]   +0x34 = +0x38 = 1   +0x3C = 0x10001
        +0x40 = (byte [módulo+0x320] != 0) ? 1 : 10        ← N
        +0x48 = 5000, +0x4C..+0x74 = 100/5000 (varios)
        +0x80 = 5000   +0x84 = 300   +0x88 = 100   +0x8C = 2
        +0x90 = 2000   +0x94 = 10000 +0x98 = 200   +0x9C = 1000   +0xA0 = 1
        +0x08…+0x28 y +0xA8…+0x1C8: timeouts en ms (30000, 600000, 10000, 5000, 60000…)
    0x27C0015  0x27C0450(módulo, param): solo guarda el puntero en contextos (no escribe nada)
    0x27C0024  0x27C1650: nada del param
    0x27C00B9  si byte+0x320 == 1 y modo un jugador [0x6CEEDE8] == 1 → salta la carga SQL de gremios
    0x27C01ED  0x27C1D80(módulo, res, param)
        [módulo+0]: struct {u32 N; param*; ServerNet*; u8 byte320}
        0x27C1E47  SAM::Initialize(SAM, res, N = param+0x40)
    0x27C0219  0x27C0280 → si byte+0x320 != 0: 0x2772050 ServerNet::Start (crea colas y la sesión)
```

- **Nadie escribe `+0x80…+0x98` ni `+0x40` después.** El relleno está justo después del ctor, en
  la misma función, con inmediatos. `0x27C0450` y `0x27C1650` reciben el param pero no lo modifican.
  Búsqueda de lecturas `[param+0x40]` por las vías habituales (`ServerNet+0x88`, `SAM+0x40`): solo
  aparece `0x27C1D80` (script `optd/pairscan.py`).
- **`byte [módulo+0x320]`** = "hay servidor embebido". Lo leen `0x27BFD3C` (N), `0x27C00B9` (salta
  SQL), `0x27C02D2` (arranca `ServerNet`) y `0x27C1DD3` (si es 0, reserva otra cosa en
  `0x2B0BB70`). En el juego vale 1, porque el servidor funciona. **Ese 1 es el que da N = 1.**
- Si el byte fuera 0 (servidor "real" de MMO), N = 10 [E]. Es la huella de BDO: 10 usuarios por
  proceso.

### 1.2 Qué hace `SAM::Initialize` `0x2A7DEA0` con N [E]

Tabla `SAM+0x50` (8 entradas de 0x18 B: `u8 clase, u8 tipo, …, +8 SAM*, +0x10 u32 cuota`):

| Entrada | `clase,tipo` | Cuota | ¿Suma a `param+0x44`? |
|---|---|---|---|
| 0 (`+0x00`) | 1,0 usuario | **N = 1** | sí |
| 1 (`+0x18`) | 2,1 jugador | 100·N = 100 | sí |
| 2 (`+0x30`) | 1,2 | N = 1 | **no** (`0x2A7E04C`: no hay `add`) |
| 3 (`+0x48`) | 3,3 | `+0x80` = 5000 | sí |
| 4 (`+0x60`) | 3,4 | `+0x88` = 100 | sí |
| 5 (`+0x78`) | 3,5 | `+0x98` = 200 | sí |
| 6 (`+0x90`) | 3,6 | `+0x88` = 100 | sí |
| 7 (`+0xA8`) | 3,7 | `+0x94` = 10000 | sí |

- `param+0x44` = 1 + 100 + 5000 + 100 + 200 + 100 + 10000 = **15.501** (`0x3C8D`), que es el valor
  vivo de `pendiente-rango-claves.md`. Con N = 10 daría 16.410.
- Cada cuota se copia además a una tabla global de estadísticas: `0x1439540()` → `[ret+8]+0x18+tipo*4`.
  Ese objeto (`exe+0x6EFBFF8`, iniciado de forma perezosa) es un panel de contadores de monitorización
  (`+0xA8` usuarios logueados, `+0xC0` paquetes en vuelo, `+0xE4`…). Lo que se escribe en
  `+0x18..+0x34` no lo lee nadie (búsqueda de `[r+r*4+0x18]` en todo `.rsrc`).
- Después llena la cola: `for i < 15.501: push(0x80000000 | i)` (`0x2A7E100`).

### 1.3 Qué pasa al reservar un usuario [E] + [E-x]

```
SAM::CreateUser 0x2A83000(SAM, outAttacher)
  0x2A83310(SAM, res, vector<actor*>)
     alloc 0x218 → CommonUserActor 0x206A940 → vt ServerUserActor; +0x1F8 = 0, +0x190 = 0
     0x2A845E0 → jmp .xtls 0x10538980 [E-x, mutado legible] = "pop de clave":
         lock SAM+0x68
         si SAM+0xD0 (cuenta de la cola) == 0 → res = [0x6CDB9E4] eErrNoInvalidActorKey   ← ÚNICO fallo
         v = datos[head++ % cap]; cuenta--; actor->vf5(v & 0xFFFFF); generación de v en actor+0x60
     user->vf8(res, &SAM+0x50[0] (entrada usuario), 0, 1, 0)  → .xtls 0xE10B190 → 0x2067F40 → .xtls 0xE0BFAC0
         [E-x] actor+0x88 = entrada; clase = entrada[0] << 28; NO mira la cuota
     por cada hueco hijo ([[user+0x68]+0x18]+0x20, 100 en vivo):
         alloc 0xD8 → ServerChildOnlyInGameActor; 0x2A845E0 (otra clave); vf8(…, &SAM+0x50[1], …)
     generación+1 (0x2A83608) y 0x2A837E0 registra cada clave
  si falla: 0x2A846B0 devuelve a la cola las claves ya reservadas y CreateUser devuelve un
  attacher vacío; el login (0x2C48DFE) va a su rama de error 0x2C4906A
  si va bien: cada actor al mapa SAM+0x98 (0x2A84F00), a la lista SAM+0xB8, ++SAM+0xA4
```

- **No existe comprobación "usuarios ≥ N".** Tampoco en el handler del login `0x2C48BE0`: sus únicas
  comparaciones son el "¿ya existe esa cuenta?" (`0x2A838E0`, VM) y los resultados de las llamadas.
- Las lecturas de la tabla `SAM+0x50` en todo el código del SAM son las escrituras de `Initialize`
  (`optd/pairscan50.py`), y no hay lecturas de la cuota a través de `actor+0x88`
  (`optd/pairscan88.py`).
- La creación de NPC (`0x2A82BF0`) sigue el mismo patrón: solo la cola común (`0x2A82D57`).

### 1.4 ¿Hay que parchear? No

- **Recomendación: no tocar nada.** El parche no tiene objetivo: el número de usuarios no está
  limitado por N.
- Si por prudencia se quisiera que las estadísticas "cuadraran", bastaría con escribir datos en
  tiempo de ejecución (`[SAM+0x50]+0x10` y `+0x40`). No cambia ningún comportamiento conocido, así
  que **tampoco se recomienda**.
- Lo que sí hay que vigilar en E2 (canario barato): `u32 [[exe+0x6DA67D8]+0xD0]` (claves libres)
  antes y después del login del usuario 2. Se esperan **101 menos** (1 usuario + 100 hijos).

---

## 2. Campos de la sesión: `+0x88`, `+0x90`, `+0xA0`, `+0xC8` [E]

### 2.1 Quién lee qué

| Función | Campos de la sesión que usa |
|---|---|
| `ServerNet::OnRecvPacket` `0x2773C20` | `+0x98` crypto (`vf2` ¿activa? → `vf6` descifra), **`+0x90` usuario** (`0x2773E3E`), `+0x88` (`u32*` id, clave de la cola de diferidas si no hay actor, `0x277407E`), `vf6` (`0x2773F58`, = `false` en la virtual) |
| `NwVirtualAsyncSession::Send` (vf9) `0x1437740` | **solo `+0xC8`** (la cola) |
| `ServerNet::SendPacket` `0x2773870` | `vf14`, `+0x98` crypto, `vf9` |
| `NwSession::NwSession` `0x1436EE0` | crea `+0x08` lock (0x30), `+0x10` estado (u16 `0x17`), `+0x88` (`u32` = 0), `+0x98` `CryptogramAesGcm` (`+0x24` = 0), **`+0xA0` = struct de 0x10 B `{ptr = 0, u32 = 0, u8 = 2}`**, `+0xA8` = 5, `+0xA9` = argumento |
| `vf3` `0x1437600` → `.xtls 0xB8A9000` (Close) [E-x] | toma y pone a 0 `+0x90`; reinicia la crypto; libera `[[+0xA0]]` (`0x40621F0`) y deja el struct en `{0, 0, 2}`; si `+0xA9 == 0`, llama a `usuario->vf(0xA8)(+0x80, sesión)` |
| `vf0` dtor `0x1437580` → base `0x1437350` | libera `+0x08`, `+0x10`, `+0x88`, `+0x98`, `+0xA0`. **No toca `+0xC8`** (la cola no es suya) |

- **`+0xA0` no es el registro de mensajes del servidor.** Es un `unique_ptr` a un struct de 16 B cuyo
  puntero se libera al cerrar la sesión (probablemente un búfer de recepción o reensamblado del
  transporte real). Ninguna función de la ruta virtual (recepción, envío, buscadores) lo lee
  (`optd/grep_ins.py "\+ 0xa0\]"` en `0x1434000-0x143C000`, `0x35A000-0x361000`,
  `0x2770000-0x2778000` y `0x2A75000-0x2A76000`). Lo que vio `troc-bdo-y-runtime.md` §2.2 en vivo
  probablemente fue otra cosa al seguir punteros. **Para la sesión 2 da igual:** el ctor crea el suyo.
- **`+0x90` vs `+0xA0`:**
  - `sesión+0x90` → `ServerUserActor`;
  - `ServerUserActor+0x1F8` → sesión;
  - `ServerChildOnlyInGameActor+0xA0` → `ServerUserActor` dueño (`troc-bdo-y-runtime.md` §2.2 y
    `0x2792A2A`);
  - `ServerChildOnlyInGameActor+0xD0` → sesión.
  - El "+0xA0" de cdtools/LiveTransmog es el del **actor hijo**. No hay contradicción.
- **`+0xC8`:** es un puntero a la **cola** Q1 (`== ClientNet+0x18`, ver §3).
  - La clase de cola deriva de `WindowsLockForOnlyEngineXXX` (vtable `0x571A2A8`). Su ctor/dtor
    (`0x35E640`/`0x35EC80`) pone esa vtable y crea la sección crítica en `+8`.
  - Por eso la RTTI del objeto apuntado dice `WindowsLockForOnlyEngineXXX`. **Las dos lecturas eran
    correctas.**
- **Cierre de la sesión y el usuario** (útil para la pregunta abierta 8 de la guía) [E-x]:
  - `Close` llama a `ServerUserActor::vf(0xA8)` = `0x2A7D790`. Este pone `usuario+0x1F8 = 0`, guarda
    la hora en `usuario+0x200` y avisa a sus componentes (`vf 0xF0`).
  - **El usuario no se borra** en ese momento: queda "desconectado" con marca de tiempo [S: lo
    recogerá un temporizador de logout].

### 2.2 Tercera discrepancia de la guía (`[exe+0x6D69698]` vs SAM) [E]

- `0x27C090C` guarda en `exe+0x6D69698` un struct de contexto.
- El login hace `CreateUser(this = [[exe+0x6D69698]])` (`0x2C48D89`-`0x2C48DA0`), y `CreateUser`
  usa `this+0x50/+0x68/+0x98/+0xB8/+0xC8`, que son campos del SAM.
- Por tanto **`[[exe+0x6D69698]] == [exe+0x6DA67D8]` (SAM)**. El `0x2A827B0` de `MoveActorReq` es la
  búsqueda del mismo SAM.

---

## 3. Colas ("pools") de `PacketBuffer` [E]

### 3.1 Las cuatro colas

Las cuatro colas son objetos de 0x80 B consecutivos a partir de una base `B = [x+0x140]`, donde `x`
es el objeto de framework (`[this+8]` en los dos arranques).

| Cola | Base | Servidor | Cliente | Quién empuja | Quién saca |
|---|---|---|---|---|---|
| Q0 | `B` | **`ServerNet+0x20`** (entrada) | `NwVirtualSyncSession+0xE0` | cliente: `0x14380B0` (vf6 Send) | hilo servidor `0x27734EA` (timeout 1000 ms) |
| Q1 | `B+0x80` | `sesión+0xC8` | **`ClientNet+0x18`** (entrada) | servidor: `0x1437740` (vf9 Send) | NetworkThread `0x35B9C2` (pop inline) |
| Q2 | `B+0x100` | **`ServerNet+0x80`** | **`ClientNet+0x20`** | cliente: `0x35BB60`, tras `OnRecvPacket` | servidor `0x277281A` (`0x27725F0`): refcount y liberación |
| Q3 | `B+0x180` | **`ServerNet+0x78`** | **`ClientNet+0x28`** | servidor: `0x27735E4`, tras `OnRecvPacket` | cliente `0x35BB7B`: libera `[pb+0x8028]` |

Pruebas:
- **Servidor:** `.xtls 0x9E2C352` [E-x] arma un desc con `+0x70 = B`, `+0x78 = B+0x80`,
  `+0x80 = B+0x100`, `+0x88 = B+0x180`. `0x27C02B0` los pasa a `ServerNet::Start` `0x2772050`, que los
  guarda en `+0x20`, `0xC8` de la sesión (a través de `ServerModule::Init`), `+0x80` y `+0x78`
  (`0x2772086`-`0x277209E`).
- **Cliente:** `0xA49F28` pasa `B`, `B+0x80`, `B+0x100` y `B+0x180` a `0x35ABC0` → `.xtls 0x86E2FC0`
  → `0x35ADD0`, que guarda `+0x18 = B+0x80`, `+0x20 = B+0x100` y `+0x28 = B+0x180`
  (`0x35AE01`-`0x35AE21`).

**Por tanto `ClientNet+0x20 == ServerNet+0x80` y `ClientNet+0x28 == ServerNet+0x78`**, y además
`sesión+0xC8 == ClientNet+0x18`, como ya se vio en vivo. La documentación tenía razón, pero **no son
pools de reutilización: son colas de "devolver para liberar"**. Cada lado libera los búferes que
reservó él.

### 3.2 Ciclo de vida de un búfer S→C [E]

1. `0x2771B30` reserva un `AsyncRequestSend` de **0x8060 B** con el asignador del juego.
   `PacketBuffer = rq+0x30`. Pone `rq+0x8050` (= `pb+0x8020`) = **1.000.000** y `rq+0x8058` = rq.
2. `SendPacket` llama a `sesión->vf9(&res, rq)`. Si `res == 0`, descuenta 1 de su contador local.
   Al final hace `xadd [rq+0x8050], -local`. Si el valor previo coincidía, libera él.
3. El cliente procesa `pb` y lo empuja a Q2. El servidor (`0x27725F0`) hace
   `xadd [pb-0x30+0x8050], -1` y, si el valor previo era 1, libera `[rq+0x8058]` con
   `0x47F02BC`/`0x47F03BC`.

**Consecuencia para P3:**
- El hook de `vf9` que se trague la trama de la sesión 2 debe:
  - **copiarla**;
  - **empujar `pb = r8+0x30` a Q2** (`0x35DF50([ServerNet+0x80], &pb)`), igual que hace el cliente en
    `0x35BB5C`;
  - devolver `*res = 0`.
- Así el refcount queda igual que con un consumidor real. Es lo que ya proponía la guía, y es
  correcto.
- Alternativa: devolver `*res != 0` sin empujar. `SendPacket` lo libera solo, pero puede que otras
  rutas inline traten el error de otra forma [S]. Mejor la primera.

### 3.3 Firmas de las funciones de cola [E]

- **`push 0x35DF50(Queue* q, void** item)`**:
  - lock `q->vf1`; añade a la deque (`q+0x30`…, cuenta en `q+0x58`); unlock `q->vf2`;
  - `ReleaseSemaphore([q+0x78], 1)`; no devuelve nada útil.
- **`pop 0x35E030(Queue* q, int* res, void** out, u32 timeout_ms)`**:
  - `WaitForSingleObject([q+0x78], timeout)`; `*res` = 0, o `0x102` (`WAIT_TIMEOUT`);
  - saca el primero a `*out`;
  - el servidor usa 1000 ms y el cliente 0 (no bloquea).
- Ninguna de las dos es de `.xtls`; se pueden **llamar** desde cualquier hilo (son thread-safe).

---

## 4. Opción B de P2: layout del `PacketBuffer` C→S [E]

Lo crea `ClientNet::PostPacket` `0x35C670`: `alloc(0x8030, 0x10)` con el asignador del juego.

| Offset | Tipo | Valor (cliente) | ¿Lo lee el servidor? (`0x27733F0`) |
|---|---|---|---|
| `+0x0000` | trama | `u16 len+0x1F`, `u8 canal`, paquete (`u16 op, i8 slot, u16 plen, payload`) | sí: paquete en `pb+3`, canal en `[[pb+0x8000]+2]` |
| `+0x8000` | `u8*` | `pb` (inicio de trama) | sí |
| `+0x8008` | u16 | `len+0x1F` | no |
| `+0x800A` | u16 | `len+0x1F` | **sí**: tamaño = `[+0x800A] − 0x1F` |
| `+0x800C` | u16 | capacidad (`len+0x1F`) | no |
| `+0x800E` | u8 | **0** = paquete; ≠ 0 = `AsyncRequest` interno (`+0x8010` = `AsyncRequest*`, va a `0x2774730`) | **sí** |
| `+0x8010` | u32 | índice de sesión del cliente (4) | solo si `+0x800E != 0` |
| `+0x8014` | u16 | flags de `PostPacket` | no |
| `+0x8018` | u64 | `sesión_cliente+0xB8` o `0x7FFF…FF` | no |
| `+0x8020` | u64 | `sesión_cliente+0xB0` o `0x7FFF…FF` | no |
| `+0x8028` | `void*` | **`pb`**: base que liberará el cliente | no (lo usa el cliente al liberar) |

Bucle del servidor (`0x27734C4`-`0x27735E9`):
```
pop(Q0 = ServerNet+0x20, 1000 ms) → pb
TLS+0x4460: si != 0, el hilo se para (Sleep 10 en bucle): pausa externa del hilo [E]
si pb+0x800E != 0: AsyncRequest (no se devuelve a Q3)
si no: OnRecvPacket(ServerNet, &res, [ServerNet+0x18], pb+3, [pb+0x800A]-0x1F, [[pb+0x8000]+2])
       push(Q3 = ServerNet+0x78, pb) → el NetworkThread del cliente lo libera con [pb+0x8028]
```

**Receta de la opción B** (verificar en vivo en E1):
1. `pb = alloc(0x8030, 0x10)` con el asignador del juego (§6). **Es obligatorio**, porque lo libera
   el cliente con `0x47F02BC`/`0x47F03BC`.
2. Rellenar la trama:
   - `pb[0..1] = len+0x1F`, `pb[2] = canal`, paquete en `pb+3`;
   - `+0x8000 = pb`, `+0x8008 = +0x800A = +0x800C = len+0x1F`, `+0x800E = 0`, `+0x8028 = pb`;
   - el tráiler (12 IV + 16 tag) no se lee con la crypto apagada, pero hay que reservar su sitio:
     `len+0x1F ≤ 0x8000`.
3. **Marca de "sesión 2"** en campos que el servidor no lee:
   - `+0x8010 = 0xD0D0_0002` (el cliente pone ahí 4);
   - `+0x8018 = 0x32534553_44504F43` ("COPDSES2").
4. `0x35DF50([ServerNet+0x20], &pb)`.
5. Hook en la entrada de `OnRecvPacket` `0x2773C20`:
   - `pb = r9 − 3`; si `[pb+0x8000] == pb`, `[pb+0x8028] == pb` y `[pb+0x8018] == MAGIC`, entonces
     `r8 = sesión2`;
   - leer `pb+0x8018` es seguro: la única llamada (`0x27735D3`) pasa siempre `pb+3` de un búfer de
     0x8030 B. Aun así, protegerlo con `safe_memory`.
6. Sin espera: el `pop` del hilo del servidor se despierta con el semáforo.

**Sin hook en `OnRecvPacket`** no hay forma de que el bucle elija otra sesión: lee siempre
`[ServerNet+0x18]` (`0x27735C7`).

Detalle para antes del login:
- Las Req diferidas de la sesión 2 sin usuario se encolan con clave `*(sesión+0x88)` (`0x277407E`).
- El ctor deja ese `u32` a 0, igual que la sesión 1 antes de su login. **Hay que dejarlo a 0**
  [E]: `0x143A580` manda la clave 0 a la cola común y con cualquier otra indexa una tabla de 1
  entrada sin comprobar límites. La recomendación anterior (2) [S] era errónea y rompió B2
  (`data/b2-ack2840.md`, D-39).

---

## 5. Byte TLS `gs:[0x58] → [+0x1EC]` (realm del servidor) [E]

`gs:[0x58]` es el `ThreadLocalStoragePointer`. `[…]` es el bloque TLS del exe (índice 0; el
asignador usa el índice de `exe+0x6C88C50`). En la plantilla TLS (`.text 0x16F10038`) el byte
`+0x1EC` vale **0**.

Escritores (`optd/tls1ec_writers.txt`, script `optd/tlsoff.py`):

| RVA | Qué |
|---|---|
| `0x27733A6` | **proc de `ServerRunThread`** `0x2773390`: `byte [tls+0x1EC] = 1` y salta al bucle `0x27733F0` |
| `.xtls 0x100B1F2B` [E-x] | otro proc de hilo del servidor (`0x100B1F10`): `= 1`, bucle con `Sleep(100)` mientras `[obj+0x108] == 0` [S: `ServerWalkThread`] |
| `.xtls 0x100B2F56` [E-x] | tercer proc (`0x100B2F40`), igual [S: `ServerRepeatTimerThread`] |
| `.xtls 0x0B82B2F9` [E-x] | **restaura** `[tls+0x1EC]` desde un byte guardado: final de un "cambio de realm con ámbito" |
| `.xtls 0x09E2C28E`-`0x09E2C3DB` [E-x] | `ServerModule` init: guarda el byte de `tls+(0xB32C0AA3 + [0x8509684])`, lo pone a 1 durante `0x27BEB50` y lo restaura. El offset está ofuscado y su valor está en datos de `.xtls` que no vienen en el fichero; casi seguro es `0x1EC` [S] |

- Lectores: **4.286** sitios del tipo `cmp byte [tls+0x1EC],0 ; cmovne global_cliente, global_servidor`.
  Eligen la copia de los globales y del reloj (por ejemplo `0x6D69430`/`0x6C8CD80`, `0x6D69438`).
- **Regla confirmada:** el código del servidor solo funciona bien en un hilo con `+0x1EC = 1`. Las
  Req síncronas de la sesión 2 tienen que ejecutarse en `ServerRunThread`, que es lo que consiguen
  las opciones A y B.
- Trinity (`+0x1F2`) es de otra build: en esta, `+0x1F2` no aparece con ese uso.

---

## 6. Asignador, firma y liberación de la sesión [E]

**`0x1447EF9B0` es una VA** (capstone imprime destinos absolutos). Las RVA son:

| RVA | Firma | Qué es |
|---|---|---|
| **`0x47EF9B0`** | `void* alloc(size_t size, size_t align)` | heap por hilo de tipo mimalloc (heap en `[TLS[exe+0x6C88C50]]+0x90`; si `+0x98 == 0`, lo crea con `0x48953C4`) → `0x47EF648` |
| **`0x47EFA78`** | igual | heap **global** protegido con `EnterCriticalSection(exe+0x6C169C8)` (heap en `exe+0x6C0E668`) |
| `0x47F02BC` | `void free(void*)` | ruta rápida si el bloque es del hilo actual (`gs:[0x30]`); si no, `0x47EFFF4` (liberación entre hilos) |
| `0x47F03BC` | `void free(void*)` | siempre la ruta genérica `0x47EFFF4` |

- Patrón del juego en todas partes: `if (byte [tls+0x1FD]) alloc = 0x47EFA78 else 0x47EF9B0`, y lo
  mismo con `free`. La plantilla TLS tiene `+0x1FD = 0`. No hay escritor directo en `.rsrc`: se
  pondrá desde `.xtls` o en el arranque/fin de hilo [S].
- Las dos parejas son compatibles entre sí: el juego reserva con una y libera con la otra según el
  hilo. Ejemplo: `SendPacket` reserva en el hilo del servidor y `0x27725F0` libera en otro.
- Tamaños y alineación usados: la sesión pide `(0xD0, 0x10)` (constante ofuscada en `0x100B8274`), el
  `PacketBuffer` `(0x8030, 0x10)`, el `AsyncRequestSend` `(0x8060, 0x10)`, `ServerUserActor`
  `(0x218, 0x10)` y el hijo `(0xD8, 0x10)`.
- **Recomendación para el mod:**
  - desde el hilo worker del mod, usar **`0x47EFA78`**: el heap global con lock no crea un heap
    mimalloc ligado a un hilo nuestro, que podría terminar;
  - desde dentro de un hook en un hilo del juego, se puede seguir el patrón del byte `+0x1FD`.
- **Liberación de la sesión:**
  - `vf0` = `0x1437580(this, flags)`: vtable de vuelta a `NwAsyncSession`, `0x1437350` (libera los
    sub-objetos, §2.1) y, si `flags & 1`, `free(this)` con `0x47F02BC`/`0x47F03BC` según `+0x1FD`;
  - el juego la llama al reemplazar `ServerNet+0x18` (`.xtls 0x100B8318`, flag ofuscado) y al
    destruir el módulo;
  - el logout no libera la sesión: llama a `vf3` (Close, §2.1).
  - Una sesión 2 reservada con el asignador del juego se puede destruir con `vf0(this, 1)`, ya en el
    apagado.

---

## 7. Escritores de `ChangeAiControlAck` 2508 y `OverriedOwnershipStateTypeyAck` 2168 [E]

Opcodes resueltos por `docs/research/data/troc_messages_2976.csv`:
- 2508: handler del cliente `0xB681F0`;
- 2168: handler `0xBB1640`;
- 2442: `AiControlChangeCheatReq`, vf2 `0x29BFB80`.

### 7.1 `ChangeAiControlAck` 2508: solo por cheat

```
AiControlChangeCheatReq::vf2 0x29BFB80 → 0x2C41360
   requisito de permiso: user->vf(0x160)->+0x68->+0x130->vf(0x140)(3)   [S: nivel de GM/cheat]
   objetivo = SAM.Find(clave) (0x2A827B0); exige objetivo+0xA0 == 0 (no es hijo de un usuario)
   tipo de registro ([objetivo+0x88]+1): 1, o 3..9 por tabla de saltos
   si objetivo+0x90 (controlador) != 0 y == clave del que pide (vf 0x170):
        0x28FA4C0 (sube el contador de secuencia del componente de IA, +0x40→+0x40→+0x20; +0xC0 = 0)
        0x28FBAC0 (sube el contador TS+0x598; +0x5B8 = 0)
        0x206EED0 …
        escritor 0x297B430 (ChangeAiControlAck, payload con la clave del actor) por ToNear
```

- **El único escritor** (`0x297B430`, 1 llamada) solo se alcanza desde el cheat. En una partida
  normal el servidor **nunca** manda 2508 [E].
- `opscan 2508` da otros inmediatos 2508 en `0x1FA7BC0`, `0x3AF3070`, `0x46EAD20`, `0x47FD870`: son
  funciones del cliente o comparaciones, no escritores del servidor.
- Además, el cheat **solo actúa si quien lo pide ya es el controlador**. No sirve para "quitarle" un
  NPC a otro cliente sin cambiar antes `actor+0x90` [E, confianza media en la semántica].

### 7.2 `OverriedOwnershipStateTypeyAck` 2168

```
escritor 0x29DD490 ← 0x2A9B670(componente, _, enviar, nuevoEstado)
     componente+0x61 = nuevoEstado; si cambia y enviar: Ack por ToNear con la clave del actor (comp+8)+0x60
llamadores de 0x2A9B670:
  0x27A1430 ← 0x2791CD0 = ICreateServerActorDesc::vf3 / CreateServerActorDesc_DailyRoutine::vf3
             (creación de un actor desde descriptor, si el tipo de registro == 7)
  0x2A9B420 ← 0x2A9C070 = ServerGimmickActorComponent::vf22
  0x2A9B0F0 ×3 ← 0x2AB6F60 ← 0x2AF6530 ← 0x2AF62C0 ← ServerStatusActorComponent::vf94 (0x2950F00)
             y ← 0x2942730 (zona de muerte/crédito, cerca de 0x29431A0) [S]
```

- Es un **estado de "propiedad" por componente** (byte `+0x61`), no la asignación de la IA.
- Se emite al crear actores con descriptor (tipo 7), al cambiar gimmicks y en la muerte o cambio de
  estado.

### 7.3 Quién decide el controlador de un NPC (`actor+0x90`) [E]

Al crear desde un descriptor (`0x2791CD0`, `0x2792A11`-`0x2792A7B`):
```
si actor+0x90 == 0:
   controlador = desc+0x40 ? [desc+0x40]+0x60            (actor/usuario indicado en el descriptor)
               : actor+0xA0 ? [actor+0xA0]+0x60          (usuario dueño)
               : [ctx+0x180]                              (clave del contexto de creación)
   actor+0x90 = controlador
```

- No hay un temporizador ni un reparto posterior que reasigne el control (salvo el cheat).
- Hay 114 escrituras `dword [r+0x90]` en `0x2780000-0x2D00000` (`optd/srv_wr_90.txt`), casi todas en
  pila o en otros objetos. Falta revisar si alguna reasigna el controlador [pendiente].
- **[S] para P10:** el NPC queda controlado por el usuario cuyo contexto de creación se usó. Si los
  spawns los pide el cliente (`ActivateSpawningPoolReq`…, guía §2.6), cada cliente controlaría lo
  que él mismo hace aparecer. Hay que confirmarlo en E5 con el sniffer (`actor+0x90` en el
  `ActorLoadDataAck`).

---

## 8. Quién llama a `0x2A83310` [E]

- `0x2A83310`: **1 referencia**, `0x2A83056` en `SAM::CreateUser` `0x2A83000`. No hay punteros
  absolutos a ella (solo su entrada de `.pdata`).
- `0x2A83000`: 2 referencias.
  - `0x2C48DA0` en `0x2C48BE0`: `LoginUserToFieldServerReq` (máscara 4, la que usa el juego).
  - `0x2C486A4` en `0x2C48570`: `LoginUserToAuthenticationServerReq` (máscara 2, variante MMO).
- Las otras altas no pasan por aquí:
  - `0x2A82BF0` (NPC): desde `0x2AFA3B2` en `0x2AFA110`;
  - `0x2A7D9F0` (pool por usuario): desde `0x2A82C7E` y `.xtls 0x1052D2FE`.
- **Conclusión:** un usuario nuevo solo se crea por la vía nativa del login. Así lo quiere el plan
  (regla 8), y no hay atajos que debamos vigilar.

---

## 9. Prólogos para inline hooks y firmas AOB [E]

Volcado completo en `optd/prologues.txt`. Ninguna de estas funciones tiene saltos que caigan dentro
de sus primeros 16 bytes (índice de xrefs).

| Función | Primeros bytes (límites de instrucción) | RIP-relativo en los primeros 20 B | Firma |
|---|---|---|---|
| `NwVirtualAsyncSession::Send` `0x1437740` | `48 89 5C 24 10` \| `57` \| `48 83 EC 20` \| `48 8B 89 C8 00 00 00` \| `49 8D 40 30` | **no** (cortes en 5, 6, 10, 17, 21) | ya en `troc-dispatch.md`: `nw_virtual_async_session_send` (32 B, única) |
| `ServerNet::OnRecvPacket` `0x2773C20` | `48 89 5C 24 10` \| `4C 89 4C 24 20` \| `48 89 4C 24 08` \| `55` \| `56` \| `57` \| `41 54` | **no** (cortes en 5, 10, 15, 16, 17, 18, 20) | ya en `troc-dispatch.md`: `troc_server_on_recv_packet` (47 B, única) |
| `ToNearExceptOneSelf::vf1` `0x2A75880` | `40 53` \| `48 83 EC 50` \| `48 8B 41 10` \| `48 8B 48 68` \| `48 8B 99 A0 01 00 00` | **no** (cortes en 2, 6, 10, 14, 21; el primer `call rel32` está en `+0x21`) | **no hay AOB única**: ver abajo |
| `ToNearExceptOneControl::vf1` `0x2A759B0` | idénticos | **no** | ídem |
| ToNear observadores `0x2BC4F70` | `48 89 5C 24 10` \| `48 89 6C 24 18` \| `48 89 74 24 20` \| `57` \| `41 54` | **no** | nueva: `48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 83 EC 70 48 8B FA 48 8B F1 48 8B 49 08 48 8B 01 FF 90 E8` (44 B, única) |
| pop `0x35E030` | `48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 20 4D 8B F0 …` | no | nueva: `48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 20 4D 8B F0 48 8B FA 48 8B F1 48 8B 59 78 41 8B D1 48 8B 0B FF 15` (39 B, única) |
| push `0x35DF50` | `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F2 48 8B F9 …` | no | **no es única** en 96 B. Resolverla desde `nw_virtual_sync_session_send` (`0x14380B0`, firma única): `E8 rel32` en el offset **`+0x1A`** → destino = `0x35DF50` |

- **Las dos `vf1` vaciadas** tienen 299 B y son iguales salvo los `rel32` (diferencias en `+0x22`,
  `+0x31`, `+0x53`, `+0x7B`, `+0x9C`…).
  - Una AOB con comodines encuentra las dos. Con bytes exactos sería única, pero depende de las
    distancias de las llamadas.
  - **Forma recomendada:** leer el slot 1 de la vtable por RTTI: `ToNearExceptOneControlSendTargetFinder`
    vt `0x5B25358` (+8 → `0x2A759B0`) y `ToNearExceptOneSelfSendTargetFinder` vt `0x5B25378`
    (+8 → `0x2A75880`). El mod ya tiene `rtti`.
  - Alternativa: la AOB con comodines da 2 coincidencias; la de dirección menor es `ExceptOneSelf`
    y la otra, a `+0x130`, `ExceptOneControl`.
- **Idea para P3 sin inline hook [S]:** `0x1437740` no tiene llamadas directas (xrefs = 0); solo se
  llama por la vtable.
  - Como la sesión 2 la construimos nosotros, se le puede dar una **copia privada de la vtable**
    `0x57260C0` (15 entradas, más el `COL` en `[-1]` para conservar la RTTI) con el slot 9
    (`+0x48`) apuntando a nuestra función.
  - Así la sesión 1 no pasa por ningún hook y desaparece el riesgo del inline hook en la ruta más
    caliente del servidor.
  - Hay que comprobar que nada compara la vtable por identidad (no se ha visto).

---

## 10. Heartbeat y listas globales de sesiones [E]

- `HeartbeatToParentRepeatTimer` 1019, vf2 `0x2C371D0` → `.xtls 0x108547B0` [E-x]:
  `*res = 0; if ([exe+0x6DAA940] > 0) --[exe+0x6DAA940]; else = 0`. Es un contador global: **no
  recorre sesiones**.
- `OnConnectToServerReq` 1005, `OnDisconnectToServerReq` 1006 y `HeartbeatReq` 1008 tienen vf2 =
  `0x35EA90` → `.xtls 0x86F0570`: `*res = constante; return` (stub). **Nada registra sesiones al
  conectar.**
- **Referencias a la sesión que existen:**
  - `ServerNet+0x18`: solo el bucle de red y `ServerModule::Init`;
  - `usuario+0x1F8` e `hijo+0xD0`: los pone el login;
  - `TrBufferPoolValue+0x48`: por paquete diferido.
- **Listas que el usuario 2 integra solo:**
  - `SAM+0xB8` (todos los usuarios; la usa `ToWorld` vía `0x2A83A40`);
  - el mapa `SAM+0x98`;
  - los observadores de sector (`0x2AF8AF0`);
  - la cola de diferidas `ServerNet+0x38`, por clave de usuario.
- Temporizadores de usuario: `0x27725F0` procesa `UserOnceTimerProcessRepeatTimer` 1007 y
  `GamePlayOnceTimerProcessRepeatTimer` 1023. Encolan por clave de usuario (`[x+0x58]`) en
  `ServerNet+0x38`; también funcionan por usuario.
- Lo que no se ha mirado: `ToSessionList` (`0x2A75AE0` → `.xtls`). Tiene 3 usos y no parece afectar
  al juego en un jugador.

---

## Direcciones nuevas o confirmadas (build 2976)

| RVA | Qué es |
|---|---|
| `0x27BEB50` | `ServerModule::Initialize` (crea y rellena `ServerModuleFrameworkParameter` 0x1D0 B, con valores constantes) |
| `0x27BFDB3` | `param+0x40 = N = byte[módulo+0x320] ? 1 : 10` |
| `0x27C1D80` | prepara `{N, param, ServerNet, byte320}` y llama a `SAM::Initialize(N)` |
| `0x27C0280` / `0x2772050` | arranque de `ServerNet` (solo si `byte[módulo+0x320] != 0`); colas `+0x20`, `+0x78`, `+0x80` |
| `.xtls 0x9E2C260` | llamador de `0x27BEB50`; arma el desc de colas (`B`, `B+0x80`, `B+0x100`, `B+0x180`) y cambia el realm TLS |
| `0xA49E90` → `0x35ABC0` → `.xtls 0x86E2FC0` → `0x35ADD0` | arranque de `ClientNet` con las mismas colas |
| `0x35DF50` / `0x35E030` | push / pop de la cola bloqueante |
| `0x35E640` / `0x35EC80` | ctor / dtor de la cola (deriva de `WindowsLockForOnlyEngineXXX` `0x571A2A8`) |
| `0x2771B30` | reserva `AsyncRequestSend` 0x8060 (refcount `+0x8050` = 1.000.000, `+0x8058` = self) |
| `0x27725F0` | bucle de diferidas y temporizadores; drena Q2 (`ServerNet+0x80`) y libera los S→C |
| `0x2773390` | proc de `ServerRunThread` (TLS `+0x1EC = 1`) |
| `.xtls 0x100B1F10` / `0x100B2F40` | otros dos procs de hilo del servidor (TLS `+0x1EC = 1`) |
| `0x47EF9B0` / `0x47EFA78` | `alloc(size, align)`, local / global con lock |
| `0x47F02BC` / `0x47F03BC` | `free` (rápida o genérica) |
| `.xtls 0x10538980` | pop de clave de la cola común (destino de `0x2A845E0`); único fallo: cola vacía |
| `.xtls 0xE0BFAC0` | init común de actor (`actor+0x88` = entrada de registro; clase en la clave) |
| `0x1437350` / `0x1437580` | dtor base `NwSession` / dtor `NwVirtualAsyncSession` |
| `.xtls 0xB8A9000` | `NwVirtualAsyncSession::vf3` Close → `ServerUserActor::vf(0xA8)` `0x2A7D790` (desconecta al usuario: `+0x1F8 = 0`, `+0x200 = hora`) |
| `0x2C41360` / `0x297B430` | cheat de control de IA / escritor de `ChangeAiControlAck` |
| `0x2A9B670` / `0x29DD490` | cambio de `OwnershipStateType` (componente `+0x61`) / escritor de 2168 |
| `0x2792A11`-`0x2792A7B` | asignación del controlador `actor+0x90` al crear desde descriptor |
| `[[exe+0x6D69698]]` | == SAM |
| `exe+0x6EFBFF8` | objeto global de estadísticas del servidor (`0x1439540`) |

## Scripts y volcados (`C:\Emulador CD\build\research\optd\`)

| Fichero | Qué es |
|---|---|
| `dispwr.py`, `dispuse.py` | escrituras y usos de `[reg+disp]` en un rango |
| `grep_ins.py` | regex sobre el desensamblado de las funciones `.pdata` de un rango |
| `after_call.py`, `before_call.py` | contexto antes y después de cada llamada a una función |
| `tlsoff.py` | usos y escritores de un offset del bloque TLS del exe (`mov r32, off` …) |
| `pairscan.py`, `pairscan88.py`, `pairscan50.py` | lecturas de `param+0x40` y de la tabla de cuotas |
| `prologues.txt`, `sigs_d0.txt` | prólogos y firmas |
| `tls1ec_writers.txt`, `tls1fd_writers.txt`, `srv_wr_90.txt`, `callers_35df50_push.txt`, `callers_35e030_pop.txt` | resultados |
| `f_*.asm` / `x_*.asm` | volcados de `.rsrc` / `.xtls`: `f_27beb50` (init y param), `f_27c1d80`, `f_2a83000_createuser`, `f_2a83310_reserveuser`, `x_10538980_keypop`, `x_e0bfac0_actorinit`, `x_b8a9000_session_close`, `f_1437740_vasend`, `f_1436ee0_nwsession_ctor`, `net_1437400`, `f_2773c20_onrecv`, `f_27733f0_srvloop`, `f_2773870_sendpacket`, `f_2771b30_reqalloc`, `f_27725f0`, `f_35b800_cli_netloop`, `f_35c670_postpacket`, `f_35df50_push`, `f_35e030_pop`, `f_2772050_srvstart`, `f_27c0280`, `x_86e2fc0_clientnet_start`, `x_9e2c260`, `x_100b8230_srvinit`, `x_100b5bf0`, `f_2c41360_aictlcheat`, `f_297b430_w2508`, `f_2a9b670_ownership`, `f_2791cd0_createdesc`, `f_2a7d9f0_userpool` |
