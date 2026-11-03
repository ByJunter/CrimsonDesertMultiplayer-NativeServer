# P8 — «Jugar» y «Cargar» grises en el título del invitado

Build **1.0.0.2976**, base `0x140000000`; todas las direcciones son RVA. Análisis estático de solo
lectura del 2026-10-09 (Ghidra con servidor de consultas propio, arrancado y parado por este
análisis; capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`). No se ejecutó el juego,
no se tocó el código ni `D:\SteamLibrary`. **[E]** decompilado/desensamblado `.rsrc`; **[E-x]**
tramo legible de `.xtls` (solo lectura, ningún enganche propuesto allí); **[V]** logs/capturas;
**[S]** inferencia. Los nombres `cdmp_*` son anotaciones del equipo. Scripts y volcados:
`<scratch>\titulo\`.

## Resumen

- **[E]** El título no pregunta nada al servidor ni mira el `CharacterLobbyDataAck`. Habilita
  «Jugar» y «Cargar» **solo si la tabla de ranuras del gestor de guardado**
  (`[[g_WorldSystem 0x6D69190]+0x88]`, vector en `+0x40`, cuenta en `+0x48`, entradas de 0x68 B
  `{u32 ranura @0, i64 fecha @8}`) contiene alguna ranura 0-2 o 100-108.
- **[E]** Esa tabla **solo la rellena código del servidor**: `0x1870C20` (salto a `.xtls 0xCBD92D0`,
  «refrescar ranuras desde disco»). En un jugador lo llama `Server::UserLogin 0x2C17520` (`+0x1BA`)
  al procesar el `LoginUserToFieldServerReq` 2490 del propio cliente. Otros llamadores:
  `SaveDataSlotRefreshReq 0x2A22380`, `DeleteSlot 0x1871040`, `0x2782DF0` (NG+). Ninguno es cliente.
- **[V+E]** Con P8 el 2490 va al anfitrión: el servidor local del invitado nunca ejecuta
  `UserLogin`, la tabla del invitado se queda vacía y el título deshabilita los dos botones. «Nueva
  partida» no depende de la tabla. Encaja con todo lo probado (ranuras en disco, otra cuenta,
  `characterNo`, Steam Cloud: nada de eso llega a la tabla).
- **[S] Arreglo raíz:** en modo remoto, el invitado ejecuta **el mismo refresco nativo y nada más**,
  con la misma guarda que `0x2A22380`, en el momento en que su servidor local habría hecho el login
  (al salir el primer 2490 remoto). No hay login local, sesión, usuario, Ack ni escritura de partida.

## 1. Qué decide «Jugar»/«Cargar»

**Clase del título [E]:** `pa::uiCommonScript::UIGamePlayControlRootTitle`, vftable `0x56D6CD8`.
Botones (campos del objeto): `+0x1D8` (`[0x3b]`) **Jugar/Continuar**, `+0x1E0` (`[0x3c]`)
**Cargar** (abre `GameDataLoadView`, `0x1030830`), `+0x1E8` (`[0x3d]`) **Nueva partida**, `+0x1D0`
(`[0x3a]`) variante del modo no-un-jugador. Los clics los reparte `0x102E050`.

**Construcción del menú [E]** — `0x102FE20`, llamada desde el `Update` `0x102C6C0` cuando los
bits de estado `+0x294` llegan a `0x1E` (tras avisos/intro):

```
010303E5  cmp  byte [g_SinglePlayerMode 0x6CEEDE8], 0 ; je → solo [0x3a]
010303EE  mov  rbx,[rsi+0x1D8] ; show(0xCB6BD0) ; 0xCC13E0          ; Jugar
01030405  mov  rbx,[rsi+0x1E0] ; show ; 0xCC13E0                     ; Cargar
0103041C  mov  r10,[rsi+0x1D8]
01030423  call 0x65EAD0                 ; ¿no hay partidas?
0103042B  test al,al ; jne 0x1030449
0103042F  call 0xCC0110 (Jugar) ; call 0xCC0110 (Cargar)     ; hay partidas → activar
01030449  call 0xCC0210 (Jugar) ; call 0xCC0210 (Cargar)     ; no hay → gris
```

`0xCC0110` pone `+0x225 = 1` y quita el estado visual 5; `0xCC0210` hace lo contrario (son
genéricos, ~200 llamadores). **Predicado `0x65EAD0`** = `jmp .xtls 0x9096890` **[E-x]**:

```
09096890  mov rax,[g_WorldSystem] ; mov rcx,[rax+0x88] ; rdx=[rcx+0x40] ; eax=[rcx+0x48]
          por cada entrada de 0x68 B: cuenta si ranura<3 o ranura-100<9
090968DF  test r8d,r8d ; sete al ; ret          ; true = NINGUNA ranura válida
```

«Jugar» (`0x65DB90`) exige además el actor de usuario local (`cdmp_GetLocalPlayerActor
[WS+0x30]`; si no, `eErrNoUserNotLogin`) —en P8 existe, lo crean 2646/2716 remotos— y toma de la
**misma tabla** la ranura con mayor fecha, que manda con `characterNo = 0x7FFF…FFFF` fijo
(`local_res20`) por `thunk 0x149928D30([WS+0xD0], …)`, es decir el 3093.

**Lectores de `[WS+0x88]`** (barrido capstone): `0x65DC3E` (Jugar), `0x102E1D7` (Nueva partida),
`0x631047` y `0x668A4B` (le pasan `[WS+0x88]` como `this` a `cdmp_SaveManager__ConsumePendingRecord
0x1871680`), `0xBDDD10` (`NewGameStartAck`) y el predicado. Es un gestor de guardado.

## 2. Quién llena la tabla y por qué en P8 está vacía

**[E]** `Server::UserLogin 0x2C17520`, rama `g_SinglePlayerMode`:

```
gestor = [g_ServerFieldContext 0x6D696A0 + 0x30]       ; = ServerActorManager+0x190
lock(gestor[0]) ; si byte [gestor+0x60] → error «ocupado» ; [gestor+0x60]=1 ; unlock
0x1870C20(gestor)        → .xtls 0xCBD92D0: vacía +0x40 y lo rehace con las carpetas "slotN"
                            de [gestor+0x50]+0x38 (válidas 0-2,100-108,200-207, sin 3 ni 201),
                            leyendo el "lobby" de cada una (0x1871D60)
lock ; [gestor+0x60]=0 ; unlock
elige la ranura más reciente de +0x40 → 0x186F570 LoadSlot
```

`SaveDataSlotRefreshReq 0x2A22380` hace exactamente la misma secuencia con
`[g_ServerActorLookup 0x6D69698 + 0x48]` (= también `ServerActorManager+0x190`, ver
`0x27C0664`/`0x27C086A` en `StoreParamContexts`, que copian `[rbx+0x190]` a los dos contextos) y luego solo envía un Ack de estado.

**[S, confianza alta]** En el mismo proceso `[WS+0x88]` y `ServerActorManager+0x190` son el mismo
gestor (mismo diseño de tabla `+0x40/+0x48/+0x60`, funciones `SaveManager` comunes). No encontré la
escritura de `WS+0x88` (estará en `.xtls`); se comprueba en vivo (§4, paso 1).

**Por qué falla solo en P8 [V]:** en 112627 el cliente del anfitrión manda 2490 a +0,059 s y su
servidor local ejecuta `UserLogin`; cuando el título construye el menú, la tabla ya está llena. En
Shadow (`shadow_20261009-1440_p8c.log` 14:40:47.685 «REMOTE … first packet 2319»; paquetes
`shadow_20261009-1431_packets.log`) el 2490 sale por el túnel, `local packets 0`, el servidor local
nunca hace login → no hay refresco → tabla vacía → `0x65EAD0` = true → `0xCC0210`. El anfitrión sí
ejecuta `UserLogin` para el usuario 2, pero refresca **su** tabla, no la del invitado. Por eso no
influyen el 2869, la copia `slot1`, la cuenta ni Steam Cloud.

## 3. Arreglo raíz propuesto (sin datos falsos)

Rellenar la tabla del invitado con su propio disco mediante la rutina nativa, sin login local.
Alimentarla con el 2869 no sirve (el título no lo lee y no lleva ranuras).

**Punto y secuencia** (todo `.rsrc`, solo llamadas; nada se engancha en `.xtls`):

1. En el hook existente de `NwVirtualSyncSession::Send 0x14380B0` (NetworkThread), en modo REMOTO,
   al ver el primer **2490** de cada conexión nueva (época), marcar `refresh_pending`. Es el momento
   en que el `UserLogin` nativo habría corrido.
2. En el mid-hook del NetworkThread `0x35BAD4` (mismo hilo, fuera de la cola), si
   `refresh_pending`:
   ```
   ws = *(u64*)(exe+0x6D69190); g = ws ? *(u64*)(ws+0x88) : 0
   si !g o !*(u64*)(g+0x50) o !*(u64*)g → reintentar en la siguiente vuelta (máx. ~5 s)
   L = *(void**)g; L->vf[1](L); busy = *(u8*)(g+0x60); if(!busy) *(u8*)(g+0x60)=1; L->vf[2](L)
   if busy → reintentar
   call 0x1870C20(g)                                  // __try/__except
   L->vf[1](L); *(u8*)(g+0x60)=0; L->vf[2](L)
   log: g, [[exe+0x6D69698]+0x48], cuenta [g+0x48], ranuras y fechas
   ```
   Copia literal de `0x2A22380` (lectura del gestor `+0xF5`, llamada `+0x14D`), sin el Ack
   `0x2A206E0`.
3. Repetir en cada conexión REMOTA nueva (salir al título, G5), igual que el relogin nativo.

**Riesgos:**
- *Hilo/heap* [S]: el nativo corre en un hilo del servidor; aquí en el NetworkThread. Los búferes
  son mimalloc por hilo (`0x47EF9B0`/`0x47EFA78` según `TLS[0x1FD]`), que admite liberar desde otro
  hilo. La guarda `+0x60` y el lock `gestor[0]` son los mismos que usa el juego.
- *Doble login* no ocurre: no se crea usuario ni sesión en el servidor local (`ServerNet+0x18 user
  = 0` debe seguir así).
- *Escrituras* [S]: el refresco lee el «lobby» de cada ranura (`RegisterPendingFile(…,0)` +
  `0x2355700`). Es lo mismo que hace cada arranque normal, pero hay que comprobar que no se abre
  nada con escritura (hooks de fichero de P6 o ProcMon). El anfitrión no se toca.
- *Semántica*: la ranura elegida en el título es del disco del invitado, y el anfitrión carga ese
  número de **su** disco (conocido, P9). «Jugar» manda `characterNo` 0x7FFF…FFFF fijo, no el F002
  aprendido del 2869: vigilar con `identity_fix`.
- *Momento*: el menú se evalúa una o dos veces al construirse (bit `0x40`). Si el refresco llegara
  después, los botones seguirían grises: el log debe mostrar el refresco antes de que aparezca el
  menú (en el nativo ocurre ~0,1 s después del 1001, mucho antes del título).

## 4. Verificación

1. **Un PC, partida normal (P8 local):** sondeo cada 100 ms de `[WS+0x88]`,
   `[[0x6D69698]+0x48]` y `[g+0x48]`. Esperado: punteros iguales; cuenta 0 hasta el 2490 local y N
   justo después (correlado con el sniffer). Confirma el mecanismo y la identidad [S].
2. **Un PC, prueba de la llamada:** opción de ini de diagnóstico que hace el paso 2 nada más
   instalar los hooks (antes del 1001). Esperado: cuenta N antes del login, sin cuelgue, «Jugar»
   activo, sin escrituras; el login nativo vuelve a refrescar sin error «ocupado».
3. **Shadow (P8):** log `P8: refresco de ranuras … N entradas` tras `2490 →`; título con «Jugar» y
   «Cargar» activos; servidor local sin usuario; ningún 3093 hasta pulsar; al pulsar, `out 3093`
   con la ranura más reciente del disco de Shadow.

## 5. Implementado (2026-10-09) y análisis de escrituras

**Código** (`src/mod/session2/guest.cpp`, `slot_refresh.{h,cpp}`, `offsets_s2.h`): en modo REMOTO,
el primer 2490 que sale al anfitrión en cada conexión arma el refresco; la siguiente vuelta del
NetworkThread (`0x35BAD4`) ejecuta la secuencia de `0x2A22380` (`+0xF5..+0x172`) sin su Ack. Antes
de la primera llamada se comparan con el exe, byte a byte: esa secuencia (0x7D B), el thunk
`0x1870C20` (`E9 AB 86 36 0B`), la lectura del título `0x65DC3E`, `WindowsLock<3>` (vtable
`0x557C120`; vf1 `0x1371340` → `EnterCriticalSection`, vf2 `0xCE4EF0` → `LeaveCriticalSection`);
en vivo, que `[g] == g+8` con esa vtable y que `[[0x6D69698]+0x48]` es `g` o nulo. Si algo
difiere, no llama (log). `+0x60` ocupado → reintenta cada 100 ms (50 veces); excepción → SEH,
libera `+0x60` con el lock y no vuelve a intentarlo en esa ejecución. Diagnóstico de un PC:
`[session2] guest_slot_refresh_test=1` (prueba P8-T de `opcion-d-pruebas.md`).

**Un único gestor [E]:** el ctor `.xtls 0xCBD2920` (thunk `0x186DEB0`) tiene un solo llamador
(`0xACDA95`, guarda en `[obj+0x108]`): `[WS+0x88]` y `ServerActorManager+0x190` son copias del
mismo puntero. El lock de `+0` es el `WindowsLock<3>` embebido en `+8` (sección crítica, spin 4000).

**¿Escribe `0x1870C20` en disco? [E/E-x]** Por cada carpeta `slotN` válida, `0x1871D60` lee
`lobby` (y `0x186F920` lee `save`) con `RegisterPendingFile(ranura, f, 0)` (lista de **carga**
`+0x6A0`) y `CommitSlot 0x2355700 → .xtls 0xEF51850`, que llama en orden a:
- vf20 *Commit Load* (`.xtls 0xEF601B0`): sale si `+0x6A8 == 0`; si no, `RestoreBackups
  0x235A800` y `ReadSlotFiles` (solo lectura, `CreateFile2`/`ReadFile`);
- vf48 *Valid* (cuota de Steam Cloud, lectura);
- vf28 *Commit Save* `0x23571D0`: **`if ([ranura+0x6B8] == 0) return 1`**, sin E/S;
- vf30 *Commit Delete* `0x2357C60`: **`if ([ranura+0x6C8] == 0) return 1`**, sin E/S.

El refresco solo registra cargas, así que no guarda ni borra mientras la ranura no tenga
guardados/borrados pendientes (los registra solo el camino de guardado, `0x186E450`, que en el
invitado remoto no corre: su servidor local no tiene usuario). **Única escritura posible:**
`RestoreBackups` recupera un guardado interrumpido (borra `…_delete`, renombra `…_new`/`…_backup`
con `0x2358A20`/`0x1329EB0`) si quedaron esas carpetas; es la misma recuperación que hace cada
arranque normal en `UserLogin`. Un fallo de lectura añade una entrada de error en memoria
(`0x1872620`, sin disco) y puede pedir un informe de Sentry (`cdmp_SaveFailReport`, RPT-0).
