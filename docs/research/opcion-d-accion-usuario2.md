# Opción D: por qué el Kliff del usuario 2 llega sin acción, y el arreglo de raíz

Build 1.0.0.2976 (base `0x140000000`, RVA). Análisis de solo lectura, 2026-10-09. No se ejecutó ni
se tocó el juego; no se cambió código ni el proyecto de Ghidra.

Marcas: **[E]** leído en desensamblado o decompilado (`.rsrc`); **[E-x]** leído en `.xtls`;
**[V]** visto en una captura, grabación o log; **[S]** inferencia.

Fuentes: servidor de consultas de Ghidra (`decomp`, `disasm`, `xrefs`, `class`, `vtable`), Python +
capstone sobre la copia `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (SHA-256 `57da440d…`),
grabaciones `D:/cdmp-grabaciones/c1d.bin`, `c1e.bin`, `c1f.bin`, capturas del anfitrión
`cdmp_packets_20261003-112627.bin`, `-125936.bin` y `-131004.bin`, y los logs
`backups/logs/cdmp_20261009-06{42_c1d,47_c1e,52_c1f}.log`.

## 0. Resumen

- **La acción actual de un actor que aparece viaja en su propio `ActorLoadDataAck`.** El bloque del
  `CharacterControl` (CC) trae, por cada una de las dos capas, el hash de la acción, un u16 y una
  marca de tiempo. Trae también un **byte de restauración** (`[CC+0x40]+0x30`) [E].
  - El cliente que crea el actor solo arranca esa acción si el byte vale 1. Lo hace con
    `0x8646D0` → `0x863880` al terminar de cargar los recursos [E].
  - Con 0, `0x8646D0` sale sin hacer nada [E] y la capa se queda con gráfico y sin acción: es el
    estado que cierra el juego.
- **El servidor pone ese byte a 1 cuando aplica de verdad una acción del actor**:
  `0x2328894 mov byte [rax+0x30],1`, dentro de `CC::vf89` (`0x2327320`). Se llega por
  `Broadcast 0x2A797C0` → `0x232AC80` → `vf89` [E]. El empaquetador lo copia tal cual al registro
  (`0x231FC90`) [E].
- **El Kliff del usuario 2 llega con el byte a 0 porque su personaje nunca ha hecho una acción en el
  servidor.**
  - Registros [V]: usuario 2 en C1d, C1e y C1f = 0; anfitrión en su 1.ª carga = 0 y en la 2.ª = 1;
    los 243 NPC, 5 monturas y 100 animales que vio la sesión 2 = 1.
  - **No es la ruta del modo 2 del cliente**: `0x8646D0` no filtra por modo [E].
- **Causa raíz: el guion de reproducción omite la `StartActionReq` del ciclo 1** (la escena de
  aparcamiento en (0, 1000, 0)).
  - Un cliente real la manda siempre, 9–16 ms después de su propio `ActorLoadDataAck` y antes de
    `LoadingCompleteReq` #1 [V, 3 capturas].
  - `opcion-d-login.md` la dio por prescindible ("simulación del cliente; un invitado sintético no
    la necesita para cargar"). Para cargar no hace falta; **para que otros te vean, sí**.
  - El cooperativo nunca tuvo el problema con el jugador copia porque su plantilla es la 2.ª carga,
    que ya trae el byte a 1 (`local_capture.cpp` ignora la carga en (0, 1000, 0)) [E + V].
- **Las dos acciones probadas no podían arreglarlo:**
  - **C1d:** demasiado pronto, antes de `MoveFieldCompleteReq` #1. El servidor la descartó: los
    registros posteriores siguen a 0 [V].
  - **C1f:** demasiado tarde. El cliente del anfitrión ya había creado al Kliff con 0, 0,35 s antes
    [V]. Además el servidor no la difundió; la comprobación exacta que falla no se puede fijar en
    estático (§2.4).
- **Arreglo de raíz (opción b):** que el usuario 2 haga su primera acción **en el ciclo 1**, como el
  cliente real.
  - En P8 (cliente real) ocurre solo. En el guion hay que mover un paso.
  - Así el registro que recibe el anfitrión en el ciclo 2 lleva el byte a 1 y el motor arranca la
    acción por su ruta nativa, sin carrera.
  - Verificación en una prueba (§4.3).
  - P5 (`relay_fix`) sigue siendo necesario para las acciones **posteriores**, y las contenciones
    siguen haciendo falta para los hijos del usuario 2 y los NPC que nacen con los dos jugadores
    presentes (§3.3, §4.5).

## 1. Pregunta 1: cómo obtiene el cliente la acción actual de un actor que aparece

### 1.1 El bloque del CC en `ActorLoadDataAck`: empaquetado y desempaquetado [E]

- **Empaquetador** (servidor, compartido): `CC::vf25` `0x231FC90`, en las vtables Common
  `0x597C358` y Server `0x5B25468`, slot `+0xC8`. Escribe, en orden:
  1. u32 `CC+0x18`.
  2. Por cada capa (`[CC+0x88]`, `[CC+0x90]`):
     - u8 "vacía" = 1 si `capa+0`, `+8` o `+0x10` es nulo;
     - si no está vacía: u16 `[[capa+0x18]+0x38]+0x5E`, u32 **hash** `[capa+0x10]+0x18` y u64
       `capa+0xF8` (tiempo).
  3. u8 bandera (y, si vale 1, u8 `[CC+0x30]+0x1B`).
  4. u64 `[CC+0x40]+8` y u64 `+0xC` (`0x473DE0`).
  5. u8 `+0x10`; cadena `+0x38` (u32 longitud + bytes; es la ruta `…app_xml` del aspecto).
  6. u64 `+0x20` (el id de "sesión de acción", ver §2.1).
  7. **u8 `[CC+0x40]+0x30` = byte de restauración**.
  8. u8 `+0x6E`, u32 `[[CC+0x38]+8]+0xE8`, 4 estructuras y la lista del final.
- **Desempaquetador** (cliente): `CC::vf11` `0x2320200`. Simétrico al anterior:
  - las capas no vacías van a `capa+0x38`/`+0x3C`/`+0xF8` (solo datos, no reproducción);
  - `[CC+0x40]+0x28 = capa0+0xF8`;
  - el byte de restauración va a `[CC+0x40]+0x30`.
  - Los dos `ReadHelper` (`0x23307D0` → `.xtls 0xEE68CB0`) leen 8 bytes cada uno [E].
- **Comprobado en las grabaciones** (`cc.py` parsea el bloque con este formato y localiza la
  cadena `…app_xml` como ancla) [V]:

| Registro | Capa 0 (hash, tiempo) | Capa 1 | Byte (offset en el registro) |
|---|---|---|---|
| 112627 #808, 15,019 s: anfitrión `0xA0100001`, ciclo 1 | `0x26107D3C`, 27740 | `0x26107D3C`, 27740 | **0** (`+0x2594`) |
| 112627 #1075, 42,813 s: anfitrión, ciclo 2 | `0x26107D3C`, 60582 | `0x26107D3C`, 60582 | **1** (`+0x2594`) |
| c1f #952: anfitrión visto por la sesión 2 | `0x25DC7FB9`, 101545 | `0x26107D3C`, 99268 | **1** (`+0x1FCB`) |
| c1f #778/#944/#1085: usuario 2 `0xA01002EC` (ciclos 1 y 2) | `0x26107D3C`, 102035 / 102783 | igual que la capa 0 | **0** (`+0x1FB7`) |
| c1d #779/#941/#1076: usuario 2 `0xA01002FA` | `0x26107D3C` ×2 | igual | **0** |
| c1e #778/#940/#1068: usuario 2 `0xA01002BE` | `0x26107D3C` ×2 | igual | **0** |

- **Recuento por tipo** (`rstat.py`) [V]:
  - **c1f (sesión 2 como observador):** el byte vale 1 en todos los registros con CC salvo dos, el
    personaje del usuario 2 y su mercenario (tipo 6, `0xB01002ED`). Son 243 de tipo 3, 6 de tipo 4,
    5 de tipo 5 y 100 de tipo 6. Muchos de esos NPC no reciben ningún `StartActionAck`: el byte basta.
  - **112627 (un solo jugador):** la **primera aparición** de cada actor tiene el byte a 0 (44 de
    tipo 3, 46 de tipo 6, 9 de tipo 5, el jugador). Los arranca la IA local o el control local
    (§1.4).
- Las dos capas con el **mismo** hash de espera `0x26107D3C` y la **misma** marca de tiempo son el
  estado por defecto que fija el servidor al cargar.
  - `0x2A771D0` (`ServerCC::vf13`, "processAfterLoadDb") pone `capa0+0xF8 = capa1+0xF8 = ahora` y
    llama a `.xtls 0x14EE52660` [E/E-x].
  - Se repite en cada cambio de campo: en el ciclo 2 del anfitrión los tiempos vuelven a ser
    iguales, **pero el byte sigue a 1** [V]. **El byte persiste entre campos.**

### 1.2 La ruta de restauración en el cliente [E]

`0x8646D0` `CC::RestoreActionsFromLoadData`. Solo la referencia una entrada de datos, en
`0x17122AD8`: la llama la carga asíncrona de recursos.
1. Si `[[CC+0x40]+0x30] == 0`, devuelve 0 sin hacer nada (`0x8646D0`, primera rama).
2. Si no, para cada capa con gráfico (`capa+0x110`) monta `{hash capa+0x3C, id capa+0x38, ts}`.
   - Con `vf188(5)` y el modo de IA solo calcula el **tiempo de reanudación**.
3. Llama a `0x863880` `RestartLayerAction`: `ActionGraph_Find` sobre `capa+0x110` o `[CC+0x70+8i]`
   y después `.xtls 0x14EE58A40`.
4. Si va bien, llama a `0x85ED00` `AfterRestoreAction` (estado de locomoción, vehículo e IK; no
   crea acción) y a `0x2329D70` `ProcessPendingActions`.

**No hay filtro por modo de control.** Un actor remoto (modo 2) se restaura igual que uno local.

**En vivo [V]:** C1f, `HorseDiag +97345 ms RESTORE(0x8646D0) USER 2 0xA01002EC … chart=true …
restoreByte=0` a las 46,599. Su primera actualización llega a las 46,651. La restauración se
ejecuta **antes** del primer tick con gráfico, pero no hace nada porque el byte es 0.

### 1.3 Quién pone el byte a 1 en el servidor [E]

- **Única escritura** de `mov byte [[x+0x40]+0x30],1` en todo el código (barrido de `.rsrc`,
  `.xtls` y `.text`, `x30.py`): **`0x2328894`**, dentro de `0x2327320`.
  - Esa función es el cuerpo común de `CC::vf89`: el cliente la llega por `0x8610A0`, el servidor
    por `0x2A7BD50`, slot `+0x2C8` de `0x5B25468`.
  - Condición `0x232886A–0x2328894`:
    - en un hilo de cliente (`TLS+0x1EC == 0`), siempre;
    - en el servidor, si **no** tienen el bit 0 ni la acción 0 (`+0x50`) ni la 1 (`+0xC8`).
- **El servidor llega a `vf89` desde una `StartActionReq` aceptada:**
  `StartActionReq::vf2 0x296CAA0` → `ServerStartActionLogic 0x2A78CA0` →
  `ServerStartActionOnServer 0x2A7A8C0` → `Broadcast 0x2A797C0` → `0x232AC80` →
  `call [vt+0x2C8]` (vf89) [E].
- **El servidor lee el byte para marcar la primera acción como "de aparición":** en `0x2A78DAF–0x2A78DFF`,
  `cmp [[CC+0x40]+0x30],0` → flag `0x40` en las dos acciones.
  - Con ese flag, el relevo `0x2A7A560` manda `StartSpawnActionAck` 2723 en vez de
    `StartActionAck` 2991.
  - Las dos van por `ToNearExceptOneControl`.

### 1.4 Las demás vías y cuál usa cada actor

| Actor en el cliente | Cómo consigue su primera acción |
|---|---|
| El personaje propio (modo 1) | Lo arranca el control local en cuanto llega su registro y lo pide con `StartActionReq`: 15,033 s (112627), 57,332 (125936) y 15,189 (131004), con la espera `0x26107D3C` en (0, 1000, 0) [V] |
| NPC, monturas y mercenarios con `+0x1F` = usuario local (modo 3) | La IA local, a 0,26 s de mediana (`enemigos-del-anfitrion.md`, 1.232 de 1.235) [V] |
| Cualquier actor remoto (modo 2) **ya activo en el servidor** | **El byte de restauración del registro** (§1.2), sin ningún mensaje extra [E + V] |
| Un actor remoto que todavía no ha hecho nada | Ninguna hasta que llegue el relevo de su primera acción (`StartSpawnActionAck`) desde el cliente que lo controla [E]. En esta build el relevo `ExceptOneControl` está vaciado (A2 [V]), así que sin P5 nunca llega |

- **No hay "puesta al día" para observadores nuevos.**
  - El servidor no manda una `StartActionAck` al recién llegado.
  - En c1f, los 33 `StartActionAck` de la sesión 2 son relevos de acciones nuevas (P5), no copias
    del estado; 243 NPC con el byte a 1 no recibieron ninguno [V].
- **La contraprueba del cooperativo:**
  - `local_capture.cpp` (`"ignoring the default character load at (0, 1000, 0)"`) toma como
    plantilla del jugador copia la **2.ª** carga, con el byte a 1 (tabla §1.1). Por eso el jugador
    copia nunca se cerró, y `caballo-coop.md` §9.2 lo atribuía, como suposición, a que no tenía
    gráfico [E + V].
  - El caballo copia salía de la **1.ª** carga del caballo (byte 0, 112627 #1470) y sí se cerró.
  - `INFORME-CABALLO-CRASH.md` §11 ya lo midió ("1 en el caballo real, 0 en la copia") y apuntó
    el arreglo pendiente: "poner el byte en el paquete de carga".

### 1.5 Respuesta: ¿falta un dato en el servidor o es la ruta del modo 2?

- **Es el estado del servidor.** El personaje del usuario 2 tiene capas válidas (espera ×2, con
  gráfico; si no, el empaquetador escribiría "vacía"), pero `[ServerCC+0x40]+0x30 = 0`. Nunca pasó
  por `vf89` en el servidor, porque nunca se le aceptó una acción.
- **El cliente se comporta como debe** con un registro "aún no activo".
- **El anfitrión estaba igual en su 1.ª carga.** No se cerró porque es modo 1 y se arranca solo.

## 2. Pregunta 2: por qué el servidor ignora la `StartActionReq` del usuario 2

### 2.1 Comprobaciones en orden [E]

Todos los errores de esta cadena tienen **severidad 2**. Ninguno genera respuesta: el manejador
solo manda `Nak` 1013 si la severidad es 1 (`0x296CD4F`, `cmp [rax+8],2`) y, en la rama del actor,
solo si además es de tipo 1. La severidad está sacada de la tabla de `0x1427790` (`errtab.py`,
1.564 entradas).

| # | Dónde | Comprobación | Si falla |
|---|---|---|---|
| 0 | `ServerNet::ProcessAsyncRequest` `0x2774730` | `ctx[0]` = actor de la sesión, resuelto con la clave `[[sesión+0x90]+0x60]` por el delegado de `cdmp_g_ServerActorManager+8` (`0x2775950`, que se fija en `.xtls`) | la Req no se ejecuta [E/S] |
| 1 | `0x296CAA0` | tamaño; `byte [troc+0x21]` (Req desactivada) | `eErrNoTrVariableSizeIsInvalid` / nada |
| 2 | `0x296CAA0` | se deserializan la clave, 2×A (`0x2442610`), B (`0x2442BF0`) y un u8; el total debe cuadrar | `eErrNo_BadPacketFormat` |
| 3 | `0x296CCF0` | por cada A: si `+4 == 0`, exige `+0x22 == 2`; si no, `+0x22 ≤ 1` | `eErrNoInvalidTrData` (`0x6CDB978`), sev. 2 |
| 4 | `0x296CAA0` | clave ≠ 0 y bit 30 = 0 | `eErrNoInvalidActorKey`, sev. 2 |
| 5 | `cdmp_SAM__FindActor` `0x2A827B0` | el actor existe | nada |
| 6 | `0x296CAA0` | tipo `[[actor+0x88]+1]` ∈ {1,3,4,5,6,8,9} (es el **tipo** de actor; el jugador es 1) | nada |
| 7 | `0x2A78CD5–0x2A78D00` | `ctx[0]->vf160()` ≠ 0 y `*ctx[0]->vf170() == actor+0x90`. En `ServerActor` (`0x5B1E740`): vf160 = `0x50E6B0` (`[+0xA0]`), vf170 = `0x17EA4E0` (`[[+0xA0]+0x60]`) | `eErrNoNoHavePermision` (`0x6CF7A04`), sev. 2 |
| 8 | `0x2A78D0D` | `B+0x281 != 0` → otra rama (`0x232FF90`) y devuelve 0 | — |
| 9 | `0x2A78D37–0x2A78D6B` | `FM+8` (TS `+0x4F8` = clave del campo, `0x30000` = ninguno) con bit 31; `[[[comps+0x1A8]+0x28]+0x18]+0x10 ≠ 0` y `[[[comps+0x1A8]+0x28]+8]+0x70 ≠ 0` (datos del componente Navigation) | **`eErrEnterToStageNotCompleted`** (`0x6CF78D4`, "todavía no ha entrado en el campo"), sev. 2 |
| 10 | `0x2A78E58` | posición de B ≠ (0, 0, 0) | `eErrNoInvalidActionMove` (`0x6CF70B4`), sev. 2 |
| 11 | `0x2A7A8C0` | `CC+0xC0 ≥ 1` (acción forzada por el servidor sin confirmar, ver `ChangeActionSessionIDAck` 2954 / `EchoActionSessionIDReq` 2350): se acepta **sin aplicar ni difundir** | — |
| 12 | `0x2A7A8C0` | `actor->vf188(5)` (`0x28F3030`: actor del servidor si `+0x90 == 0` o congelado) | `eErrNoActionHasntControlPermission` (`0x6CF6F3C`), sev. 2 |
| 13 | `0x2A7A8C0` | `FindAction` del hash en el gráfico (`eErrNoNoExistAction`), ramas y sub-ramas, muerto (`eErrNoDeadActorActionRequest`)… | sev. 2 |
| 14 | `0x2A7A560` | en la difusión: bit 31 de `FM+8` y `+0x10A & 1` de la acción; destino `ToNearExceptOneControl` (vacío sin P5) | sin envío |

### 2.2 El payload de 725 B [V/E]

- **Es byte a byte la primera `StartActionReq` del anfitrión en 112627** (#819, 15,033 s), salvo los
  bytes `+0..+3`, la clave (`sar.py`: diferencias solo en `0x0`/`0x1`).
- **Campos:**
  - `+0x00` clave;
  - `+0x04` A[0]: u32 0, u32 `0x26107D3C` (espera), …;
  - `+0x49` A[1]: ídem;
  - `+0x8E` B = MoveData: posición **(0, 1000, 0)** (la de aparcamiento) y cuaternión (0, 0, 0, 1)
    en `+0x9A`;
  - u64 `+0x15F` = `0x9656` (38.486 ms, reloj del cliente);
  - u32 `+0x168` = `0xE76E330D`;
  - u64 `+0x2CA` = `0x9656`;
  - **u8 final `+0x2D4` = 0**.
- **Consecuencias:**
  - Con el u8 final a 0, `ServerStartActionLogic` **no** llama a `ApplyMoveFromClient`
    (`*param_6 == 0`, `0x2A78E58…`): ni la posición ni las marcas de tiempo se validan contra el
    estado del servidor.
  - El servidor usa su propio reloj (`param_6 = ahora + desfase`).
  - El hash existe en el gráfico de Kliff: es el que el propio servidor puso en las capas del
    usuario 2 (§1.1).
- **Conclusión [S fuerte]: el contenido no es la causa.** La misma Req es la que el servidor aceptó
  al anfitrión en el mismo momento del login.

### 2.3 Respuestas en las grabaciones [V]

- **C1d, C1e, C1f y e3b:** ningún `Nak` 1013, `StartActionNak` 2570, `StartSpawnActionAck` 2723,
  `ChangeActionSessionIDAck` 2954 ni `MoveForceAck` con la clave del usuario 2 tras la Req
  (`keyrefs.py`, `ops.py`).
- **En 112627 tampoco hay 2954 ni 2723**: el anfitrión no tuvo acciones forzadas pendientes (#11).
- **C1d:** la Req salió a 0,600 s. Los registros posteriores del usuario 2 (1,903 y 2,470 s) siguen
  con el byte a 0. Si la acción hubiera llegado a `vf89`, valdría 1 (§1.3), como en el anfitrión
  (0 → 1 entre ciclos).
  - **Probado: la Req de C1d no se aplicó.**
- **C1f:** la Req salió a 1,448 s y no hay registro posterior del usuario 2 (el juego se cerró a
  las 06:53:52).
  - En el anfitrión, `HorseDiag` no registró ningún cambio de forma (sin `pending(+A8)=1`) en los
    5,8 s siguientes, mientras la contención saltaba 11 actualizaciones.
  - **No llegó ningún `StartActionAck` ni `StartSpawnActionAck` al cliente del anfitrión.**

### 2.4 Veredicto por prueba

- **C1d: demasiado pronto.**
  - La Req salió **antes de `MoveFieldCompleteReq` #1** (1,612 s) y antes del propio
    `ActorLoadDataAck` del personaje (1,903 s).
  - El cliente real nunca la manda así: siempre después de recibir su registro (§3.1).
  - La comprobación que encaja con "todavía sin entrar al stage" es la #9,
    `eErrEnterToStageNotCompleted` [S]. Sin una medida en vivo no se descartan la #0 (resolución de
    `ctx[0]` antes de entrar) ni la #7.
- **C1f: demasiado tarde.**
  - El anfitrión recibió el registro del usuario 2 (byte 0) en el `EnterField` del ciclo 2, a las
    46,349 (`Capture: user template … 0xA01002EC`). Fue **antes** de `MoveFieldCompleteReq` #2
    (≈46,37) y la Req salió a ≈46,69 [V].
  - Con el gráfico ya cargado (46,599), solo un relevo llegado a tiempo podía salvarlo.
  - **Por qué no hubo relevo: sin determinar en estático.** Candidatos, por orden:
    1. **#9 en el campo compartido** [S]:
       - el usuario 2 no es el dueño del campo;
       - `MoveFieldComplete` se salta las ramas solo-dueño de stages (`0x2BC7885`, `0x2BC8469`,
         `opcion-d-campo-compartido.md` §3.3);
       - los datos de Navigation/stage que mira #9 podrían no estar enlazados para él.
       - Si es esto, **todas** sus acciones en el campo del anfitrión se perderían. Es crítico para
         E4/P8 aunque no sea la causa del cierre.
    2. **#7** si el delegado de `ctx[0]` no resuelve igual para la sesión 2 [S]. Débil:
       `MoveFieldCompleteReq` y `LoadingCompleteReq` de la sesión 2 sí funcionan, aunque no consta
       que usen `vf160`/`vf170`.
    3. Aceptada pero sin difusión (#14 o el relevo P5 hacia la sesión 1) [S]. Débil: el anfitrión
       no vio ningún `pending`.

### 2.5 Medida que lo cierra en una prueba (mid-hooks de solo lectura, todos en `.rsrc`)

- `0x296CAA0` (entrada): contar y registrar la clave.
- `0x2A78CA0` (entrada): la clave del actor, `ctx[0]` y `[ctx[0]+0xA0]+0x60`.
- `0x2A79088` (`EnterToStage`) y `0x2A79090` (sin permiso): registrar cuál salta y, para #9:
  - `[[actor+0x68]+0x1A0]+0x4F8`;
  - `[[comps+0x1A8]+0x28]`, `+0x18→+0x10` y `+8→+0x70`.
- `0x2A797C0` (entrada de `Broadcast`) y `0x2328894`, la escritura del byte: muestran si se aplicó.
- `0x2A7A560`: `FM+8` y si sale por `StartSpawnActionAck` o por `StartActionAck`.
- Todo filtrado por la clave del personaje del usuario 2. Va dentro de la prueba de §4.3.

## 3. Pregunta 3: qué hace un cliente real (P8)

### 3.1 Secuencia del login real [V]

| Paso | 112627 | 131004 | 125936 |
|---|---|---|---|
| `LoginCharacterToFieldServerReq` | 3,437 | 2,792 | 42,489 |
| `MoveFieldAck` tipo 3: aparcamiento (0, 1000, 0), un campo propio | — | — | — |
| `MoveFieldCompleteReq` #1 | 15,017 | 15,156 | 57,316 |
| `ActorLoadDataAck` propio (byte 0) | 15,019 | 15,173 | 57,323 |
| **`StartActionReq` (espera, (0, 1000, 0), u8 final 0)** | **15,033** | **15,189** | **57,332** |
| `LoadingCompleteReq` #1, `LoginCharacterToGameReq` | 33,256 | 33,268 | — |
| `GameLoadingStartReq` → `MoveFieldAck` tipo 1 (nivel real) | — | 37,322 | — |
| `MoveFieldCompleteReq` #2 → `ActorLoadDataAck` propio (**byte 1**) | 42,811 / 42,813 | 42,849 | — |

- En el ciclo 1 el cliente real **siempre** actúa antes de salir del aparcamiento. Cuando entra en
  el nivel de juego, el servidor ya tiene el byte a 1 [V+E].
- En opción D (E3b) el aparcamiento del usuario 2 es un campo propio:
  - `H1: user 2 travels (FieldInfoKey 0x3): own field as before` [V];
  - H1 solo comparte si el nivel coincide con el del anfitrión, y el aparcamiento nunca coincide.
  - Ahí nadie más lo ve.
- Después, `GameLoadingStartReq` → `EnterField` en el campo del anfitrión → `AddActor` → el
  anfitrión recibe su registro (46,349 en C1f). **Con un cliente real, ese registro llevaría el
  byte a 1.**

### 3.2 ¿Desaparece el problema en una sesión P8 real? Para el personaje, sí; sin carrera

- **El cliente del anfitrión crea al personaje del usuario 2 con el byte a 1** y lo restaura en
  `0x8646D0` antes del primer tick (§1.2).
  - No necesita ningún mensaje de red ni P5 para aparecer.
  - El orden está garantizado por el protocolo: la acción del ciclo 1 se aplica (ida y vuelta de
    unos ms) mucho antes del `GameLoadingStartReq`, que el cliente real manda ~22 s después [V].
- **Las acciones siguientes del usuario 2 sí necesitan P5:**
  - las difunde `0x2A7A560` por `ToNearExceptOneControl` (también `MoveActorAck`: `0x2BCEB18`);
  - esa lista está vaciada en esta build (A2: `finders control 0/24594 nonempty` [V]);
  - `relay_fix` es el mecanismo que las lleva a la sesión 1. Ya llevó 33 `StartActionAck` del
    anfitrión a la sesión 2 en C1f [V].
- **Condición** para que lo anterior valga: que el servidor acepte las acciones del usuario 2
  **en el campo compartido**. Es la duda de §2.4, candidato 1.

### 3.3 Dónde sigue habiendo carrera, incluso en P8 [S fuerte]

- **Hijos del usuario 2** (caballo, mercenarios: tipos 4, 5 y 6):
  - su primera aparición es en el ciclo 2, ya en el nivel de juego, con el byte a 0 (112627: 0
    para `0xB0100002/3/4`) [V];
  - los arranca la IA del cliente que los posee unos 0,3–0,4 s después [V];
  - el anfitrión los crea a la vez, sin acción, hasta que llega el relevo de su
    `StartSpawnActionAck` (con P5).
  - Es exactamente el caso del caballo copia.
- **NPC que nazcan con los dos jugadores presentes:**
  - el cliente del anfitrión los simula (modo 3) y el invitado los ve en modo 2 con el byte a 0
    hasta el relevo;
  - en el MMO original esos NPC eran del servidor (`vf188(5)`: actor sin `+0x90`) y su acción salía
    por `ServerInitiateStartAction` `0x2A79420` hacia `ToNear` [E].
- Para estos casos hace falta una red de seguridad (§4.5); no es una cuestión de protocolo.

## 4. Pregunta 4: el arreglo de raíz

### 4.1 Opciones evaluadas, ordenadas por robustez para P8 con 4 jugadores

1. **(b) Protocolo: primera acción en el ciclo 1, como el cliente real. RECOMENDADA.**
   - **Qué:** usa el mecanismo nativo de punta a punta: el servidor marca el actor como activo
     (`vf89`), el empaquetador lo publica y cada observador lo restaura.
   - **P8:** no hay que hacer nada. Solo comprobar que P8 no filtra ninguna Req del ciclo 1.
     Corregir la línea 356 de `opcion-d-login.md`: esa `StartActionReq` **no** es opcional.
   - **4 jugadores:** cada invitado actúa en su propio aparcamiento y llega activo; ningún
     observador ve nunca su personaje sin acción.
   - **Cambio en el guion** (`login_e3lc00act*_112627.troc`, ver la variante C1g de §4.3): mover el
     bloque `send StartActionReq 2223 0 010010a0…` + `put u32 0 PLAYER_KEY` justo **después** del
     paso 4 (`expect 2144` del `MoveFieldCompleteReq` #1) y **antes** del paso 6
     (`LoadingCompleteReq` #1).
     - `MoveFieldCompleteAck` 2144 llega después del `ActorLoadDataAck` propio (c1f #778 → #782)
       [V], así que el orden queda como en 112627 (registro 15,019 → Req 15,033).
     - Sin esperas añadidas.
     - Opcional: dejar una segunda `StartActionReq` al final, solo como sonda de §2.4. No hace
       falta para el arreglo.
2. **(a) Servidor: forzar el byte o la acción al crear o entrar. Solo como reserva para el guion.**
   - **Variante a1:** en H2 (`0x2BC04F0`, antes de `AddActor`), escribir `[[[pj2+0x68]+0x40]+0x40]+0x30 = 1`.
     - El anfitrión restauraría la espera que ya está en las capas.
     - Pero es un estado que el servidor no ha producido: la primera acción real del usuario 2 ya
       no saldría como "de aparición" (flag `0x40`/2723), y queda una divergencia servidor↔cliente.
   - **Variante a2:** iniciarle una acción con `ServerInitiateStartAction` `0x2A79420`.
     - Manda `ChangeActionSessionIDAck` 2954 al controlador y sube `CC+0xC0`.
     - Mientras el cliente no conteste con `EchoActionSessionIDReq` 2350, **todas sus Req de acción
       se aceptan sin aplicarse** (#11). El guion no lo hace, así que es peor.
3. **(c) Cliente: que el anfitrión dé una espera a todo remoto con el byte a 0.**
   - El motor no lo hace: el byte existe precisamente para eso.
   - Para el personaje del usuario 2, (b) lo hace innecesario.
   - **Variante útil (c′):** solo como red de seguridad para §3.3. En la entrada de `0x8646D0`
     (`.rsrc`), si el actor es remoto (`actor+0x90` ≠ usuario local), el byte vale 0 y la capa 0
     trae hash (`capa+0x3C ≠ 0`) y gráfico, tratar el byte como 1.
     - Así el motor arranca **por su propia ruta** la acción por defecto que el servidor ya puso en
       las capas, antes de cualquier consumidor.
     - Cubre también `0x1831598`, que las contenciones actuales no cubren.
     - Riesgo: un "salto" visual cuando llegue la primera acción real; es aceptable.
4. **(d) Otros:**
   - P5 (`relay_fix`) es **imprescindible** para todo lo que pase después de aparecer (§3.2).
   - Hay que **diagnosticar §2.4** antes de E4/P8: si el usuario 2 no puede actuar en el campo
     compartido, se ve pero no se mueve.
   - El mercenario que falla al crearse (`c1-actor-rechazado.md`, c5) y el `characterNo` repetido
     (C2) son problemas aparte.

### 4.2 Qué cambiar y dónde

| Pieza | Cambio | Sitio |
|---|---|---|
| Guion de reproducción | Mover la `StartActionReq` al ciclo 1 (§4.1-b) | `docs/research/data/login_e3lc00act*_112627.troc` (y su generador `tools/scripts/make_troc_script.py` / `login_script_112627.json`: no descartar la Req de F4) |
| P8 | Nada para el personaje; no filtrar Req del ciclo 1 | `opcion-d-p8.md` |
| Documentación | Corregir que la Req de F4 "no se manda" | `opcion-d-login.md` línea 356 |
| Diagnóstico | Mid-hooks de solo lectura de §2.5 | entradas y saltos en `.rsrc`; nada en `.xtls` |
| Red de seguridad P8 (más adelante) | (c′) en `0x8646D0` o la contención generalizada (§4.5) | `.rsrc` |

### 4.3 Verificación en una sola prueba (propuesta "C1g")

- **Configuración:** la de C1f (`share_field=1`, `relay_fix=1`, contenciones D-44/D-46 activas
  como red), guion = actfast con la `StartActionReq` en el ciclo 1 (y la sonda al final), y los
  mid-hooks de §2.5.
- **Éxito, en este orden:**
  1. En la grabación de la sesión 2, el registro propio del ciclo 2 (≈1,13 s, tras
     `MoveFieldCompleteReq` #2) lleva el **byte a 1** (`cc.py <grabación> 20 <clave>`).
     - Prueba que el servidor aceptó y aplicó la acción del ciclo 1, sin mirar al anfitrión.
     - El diagnóstico debe mostrar `Broadcast` + `0x2328894` para esa clave.
  2. En el log del anfitrión: `HorseDiag … RESTORE(0x8646D0) USER 2 … restoreByte=1` y, en la
     siguiente actualización, `cur(+10)=true play(+18)=true`.
  3. Contadores de las contenciones a 0 para el personaje y sin cierre en 60 s. El segundo Kliff,
     visible y en espera.
  4. Sonda final (opcional): el diagnóstico dice si la Req en el campo compartido pasa #9/#7 y si
     el relevo P5 la entrega (en el anfitrión, `pending(+A8)=1` y luego `cur` con otro hash).
- **Si falla el paso 1:** el diagnóstico dirá qué comprobación de §2.1 rechaza al usuario 2 en su
  aparcamiento. Sería algo propio de la sesión 2, porque al anfitrión la misma Req le funciona.
  Solo entonces tiene sentido la reserva (a1).

### 4.4 Riesgos residuales

- **R1.** Que el servidor no acepte la Req de la sesión 2 ni en el aparcamiento (#0/#7). Lo detecta
  el paso 1.
- **R2.** Que las acciones en el campo compartido se pierdan (§2.4-1): el personaje aparecería, pero
  quieto.
  - Afecta a E4/P8, no a C1.
  - Si es #9, el arreglo es enlazar Navigation/stage para el no-dueño en H1/H2, no tocar las
    acciones.
- **R3.** El byte persiste entre campos [V en un solo jugador], pero no se ha comprobado tras
  muerte, viaje rápido o recarga del usuario 2 [S].
- **R4.** Carrera de hijos y NPC (§3.3), inherente a que la IA corre en un cliente.
- **R5.** La primera acción del usuario 2 en el campo del anfitrión ya no será "de aparición"
  (2723) sino normal (2991), igual que para el anfitrión en un solo jugador [E]. Es inocuo.

### 4.5 Las contenciones de hoy (D-44 y D-46)

- **Mantenerlas activas** durante C1g y P8 como red de seguridad. Con el arreglo (b), para el
  personaje del usuario 2 deben quedarse en 0 y no hacer nada.
- **Cambiarlas, no quitarlas**, cuando se pase a P8 con compañeros:
  - el predicado no debería ser "la clave del personaje del usuario 2", sino "actor remoto
    (`+0x90` ≠ usuario local) con capa con gráfico y sin acción";
  - con un tope de tiempo (unos 3 s) y registro, para cubrir §3.3;
  - el cooperativo ya demostró que basta con saltar 1 actualización si el relevo llega
    (`INFORME-CABALLO-CRASH.md` §11).
- **No sirven como arreglo:** no cubren `0x1831598` (C1e) ni los demás lectores de la acción
  actual. Si §3.3 da problemas en P8, (c′) es mejor que añadir más saltos, porque crea la acción en
  el origen.
- **Quitar el caso especial de la clave del usuario 2** solo cuando C1g pase y exista la versión
  generalizada.

## 5. Direcciones (build 2976)

| RVA | Qué es | Marca |
|---|---|---|
| `0x231FC90` | `CC::vf25`, empaquetador del bloque del CC (servidor) | [E] |
| `0x2320200` | `CC::vf11`, desempaquetador (cliente) | [E] |
| `0x8646D0` | `RestoreActionsFromLoadData` (sale si el byte vale 0) | [E] |
| `0x863880` / `0x85ED00` / `0x2329D70` | rearrancar capa / después de restaurar / acciones pendientes | [E] |
| `0x2327320` (`0x2328894`) | cuerpo de `CC::vf89`; **escribe el byte = 1** | [E] |
| `0x8610A0` / `0x2A7BD50` | `vf89` del cliente y del servidor | [E] |
| `0x2A771D0` | `ServerCC::vf13`: capas por defecto con la marca de tiempo actual | [E] (+ `.xtls 0x14EE52660`) |
| `0x296CAA0` | `StartActionReq::vf2` | [E] |
| `0x2A78CA0` | `ServerStartActionLogic` (salidas `0x2A79088`/`0x2A79090`) | [E] |
| `0x2A7A8C0` | `ServerStartActionOnServer` | [E] |
| `0x2A797C0` → `0x232AC80` → vf `+0x2C8` | aplicación en el servidor | [E] |
| `0x2A7A560` | relevo: 2723 o 2991 por `ToNearExceptOneControl` | [E] |
| `0x2A79420` / `0x2966ED0` | acción iniciada por el servidor / `ChangeActionSessionIDAck` 2954 | [E] |
| `0x28F3030` | `ServerActor::vf188` | [E] |
| `0xB57BC0` | cliente: `StartSpawnActionAck` → `0x48B120` | [E] |
| `0x6CF78D4` / `0x6CF7A04` / `0x6CF6F3C` | `eErrEnterToStageNotCompleted` / `eErrNoNoHavePermision` / `eErrNoActionHasntControlPermission` (todos sev. 2) | [E] |

## 6. Scripts y nota de procedimiento

- **Scripts** (en el scratchpad de la sesión; Python con `-I -B`, lector
  `tools/scripts/analyze_capture.py`):

| Script | Qué hace |
|---|---|
| `cc.py` | Parsea el bloque del CC de cada registro |
| `rstat.py` | Recuento del byte por tipo |
| `reqs.py` / `ops.py` / `keyrefs.py` | Req, Ack y referencias a una clave |
| `sar.py` | Compara payloads de `StartActionReq` |
| `firstsar.py` | Secuencia de login real |
| `errtab.py` | Tabla de errores con severidad |
| `dispscan.py` / `x30.py` / `w40_30.py` | Barridos de escrituras |

- **Nota:** la primera ejecución de `errtab.py` usó por error el `pe.py` antiguo, que apunta al
  `CrimsonDesert.exe` de la carpeta del juego.
  - Fue una lectura (mmap de solo lectura); no se escribió ni se ejecutó nada.
  - En cuanto lo vi lo cambié a la copia de `D:\RE\CrimsonDesert_2976\bin\` y repetí ahí todo el
    análisis.
