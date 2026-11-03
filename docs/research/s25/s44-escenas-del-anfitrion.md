# S44 — Las escenas del anfitrión en el cliente del invitado: arreglo de raíz

Build 1.0.0.2976 (PE `0x6AB28F00`, base `0x140000000`). Rama `opcion-d-escenas`, worktree `C:\Emulador CD-escenas`.
Encargo: [`s44-ENCARGO-escenas-del-anfitrion.md`](s44-ENCARGO-escenas-del-anfitrion.md).

Marcas: **[V]** leído en instrucciones, decompilación, logs o capturas; **[E]** inferencia fuerte; **[H]** hipótesis que
hay que medir.

Estado: **probado en vivo con dos copias el 15-10 (§11)**. Paso 1 (`probe`) confirmó el mecanismo; paso 2 (`1`)
arregló 9 de 10 revanchas y falló en la primera por el estado `Learning`, que se ha quitado (§11.3).

## 0. Resumen

1. **Causa [V salvo el último paso, E/H].** El cliente nativo crea toda escena 2661 con `kind == 4` en el gestor del
   dueño del campo y enlaza a su actor cada enlace de tipo 4/5 (`0xA9CA40` → `0x8AF170`). El nodo de jugador del
   `.paseq` (`ActorTrackNodeData_CurrentControl`, `nodo+0xAC == 0`) se da por hecho que es el jugador local:
   `ClientActor::vf178 0x4FE260` lo comprueba y, cuando no lo es (la copia del Kliff del anfitrión en el invitado), el
   motor lo trata como un NPC:
   - alineación al empezar: `0x602940` → `0x6030D0` → `TransformSync vf1A0(pos, rot, 4, 5, 0)` en `0x603522`;
   - control de secuenciador: `SequencerControl_Input/_AI/_Off` → vf21 `0x601470` / vf22 `0x601C10` → `0x9F8980` →
     control por actor en `.xtls`.

   Todos esos destinos se resuelven en **`0x205DC30`**, que **solo** produce actor con un enlace de tipo 5 e **ignora**
   el tipo 0 (ni siquiera lo cuenta). Nada convierte un 0 en 5 (solo el 4 pasa a 5, en `0x205B4D0`). El resultado
   visible es la copia congelada en una pose (revancha de Matthias, `0x9586FFA4`; correlación 2/2 frente a 0/12 con la
   escena denegada). Qué paso exacto pone la pose (control de `.xtls`, alineación o un evento de cuerpo) sigue sin
   medir [H]. Los que resuelven por enlace pasan por `0x205DC30` y el arreglo los cubre; **no todos**: el dueño también
   se alcanza por clave a través de `stage+0x98` (`0x205D7C0`, `desc+0` de vf21/vf22) y de `0x20532B0` (revisión H-2,
   §2.3, §8).
2. **Principio de diseño.** En el cliente del invitado, el personaje de **otro** jugador nunca debe ser movido por una
   escena: su estado autoritativo llega por red desde su propio cliente, que es el que reproduce de verdad sus escenas
   personales. Por eso, **en toda escena cuyo dueño es otro jugador y que vaya a X**, el enlace a ese jugador (el dueño)
   se neutraliza. Sin listas de nombres: se aplica igual a todas las escenas del anfitrión. NPC, gimmicks y tiendas se
   quedan como están.
3. **Arreglo** `guest_seq_foreign_player = 0 | probe | 1` (solo invitado; por defecto `0` = exactamente como hoy). En
   el gancho mid que **ya existe** en `0xC0612A` (`guest_seq_route`), cuando el 2661 va a X: la entrada del vector de
   enlaces del manejador cuyo actor es el **dueño** de la escena, con tipo 5 (o 4), pasa a tipo **0** antes de que
   `0xA9CA40` lo lea. `probe` solo registra `[would foreign]`. **Tras la revisión (H-1) el 0 no es inerte** (es el
   estado nativo «nodo sin enlazar», §2.2-§2.3) y **tras la prueba en vivo (§11)** la decisión es: con `1` se
   neutraliza **desde la primera aparición**, y el mapa que aprende P1 guarda solo las **excepciones**: un (recurso,
   nodo) donde P1 ve al dueño en un nodo que no es de jugador queda `NotPlayerNode` y no se vuelve a tocar.
4. **Sondas de solo lectura** `guest_seq_foreign_probe = 0 | 1`: P1 (inicio de escena), P2 (SequencerControl), P3
   (alineaciones) y P4 (`0x5EB310`, la orden que crea un actor de cliente sobre un nodo de tipo 0). Dicen si la etapa
   conserva el 0 y si, tras el arreglo, ya no llega nada a la copia. P1 y P4 se instalan también con
   `guest_seq_foreign_player` distinto de 0 (P1 alimenta el mapa aprendido; P4 vigila el riesgo del doble).
5. `guest_seq_route_deny` **no cambia**: `8CD771B1` (la cinemática de carga) se sigue denegando, porque actúa sobre el
   cliente local (recarga el campo del invitado), no sobre el actor. Los inis nuevos de S44 **no** llevan `9586FFA4`:
   la prueba es que el arreglo de raíz cubre la revancha sin denegarla.

Informes de la investigación (copiados del scratchpad a [`s44-datos/`](s44-datos/); las rutas `scratchpad\s44\...` que
citan se refieren a esta copia):
- [`s44-datos/nativo.md`](s44-datos/nativo.md): la semántica nativa (causa y diseño, el principal).
- [`s44-datos/inventario.md`](s44-datos/inventario.md): el inventario de las 1 520 escenas del anfitrión recibidas por
  el invitado; tablas en `s44-datos/inventario/` (sin `names.json`, 173 MB, ni `caps.jsonl`).
- [`s44-datos/mod.md`](s44-datos/mod.md): el mapa del mod actual (archivo:línea).
- [`s44-datos/verificacion/verificacion-2976.txt`](s44-datos/verificacion/verificacion-2976.txt): la verificación de
  bytes, `.pdata`, firmas y saltos de esta implementación sobre el exe de Steam (SHA-256 incluido).

No se copiaron los recursos del juego extraídos (`nativo\data\`) ni las decompilaciones (`nativo\dc\`): son datos y
código del juego.

---

## 1. Causa, con pruebas

### 1.1 Qué hace el cliente con un 2661 [V]

- El manejador `0xC05BB0` (`.pdata` `0xC05BB0–0xC0632F`) **no mira de quién es la escena**: toma el gestor del dueño
  del campo (`0x17FA6C0` → `ClientField::vf48`) y llama a `0xA9CA40(M, &res, &playdata=[rbp], &enlaces=[rbp−0x80])`.
  En el juego nativo no hace falta: el servidor manda el 2661 **solo** al dueño (ToTarget). En el invitado llega porque
  el anfitrión lo copia (`observer_seq_copy`), y `guest_seq_route` lo lleva a X (el gestor del Kliff del anfitrión).
- `0xA9CA40` (`.pdata` `0xA9CA40–0xA9D47E`) recorre los enlaces: con tipo 4 o 5 toma una referencia al actor
  (`0x8AF170`; si falta, descarta la escena entera); **con cualquier otro tipo guarda la entrada sin actor**
  (`0xA9CB87 movzx eax,byte [r14+8] ; cmp al,4 ; je ; cmp al,5 ; jne 0xA9CC20`).
- La etapa (`.xtls`) lleva la lista de actores en `stage+0x120` (número `+0x128`), entradas de 0x28 B: `+0` nodo
  (`nodo+0x30/+0x34` ids, `nodo+0xAC` tipo de nodo), `+8` clave del actor, `+0xC` tipo de enlace.

### 1.2 El rol de jugador lo da el nodo, no el número `0x2000000N` [V]

`nodo+0xAC` lo escribe la clase de datos del nodo: `0` = `CurrentControl` (el jugador), `1` = actor fijo, `2` =
`SpawnCharacter`, `4` = `PivotActor`. El número `0x20000002` es solo una ranura (inventario §5.2: 37 recursos del mundo
enlazan al anfitrión ahí, entre ellos las 16 tiendas).

### 1.3 Por qué la copia queda congelada [V hasta el último paso]

`ClientActor::vf178 0x4FE260` = `[[0x6D69190]+0x30]+0x58 == [actor+0xA0] && byte [actor+0x94]` (verificado con
capstone; el global es el `kWorldSystemSlot` del mod). En el Kliff del anfitrión, en el invitado, es **falso**. Por eso:
- `0x6030D0` lo alinea a la marca de la escena (`0x603379` vf178 falso → … → `0x603522 call [rax+0x1A0]`);
- `0x601470` / `0x601C10` le aplican el control «de NPC» (`0x9F8980(TransformSync, 1)` y el control de `.xtls`);
- todo evento de la pista del jugador y toda orden de etapa resuelven el actor con `0x205DC30`.

En el anfitrión el mismo evento cae en la rama del jugador local (control con entrada) y no se congela. El último
eslabón (qué deja la pose fija) es [H]; ver §8.

### 1.4 Lo que distingue «mundo» y «personal» en los datos [V]: nada suficiente

El inventario (`s44-datos/inventario.md` §5) y la semántica (`nativo.md` §2) coinciden:
- `kind` = 4 y tipo de enlace = 5 en todo (1 635 2661, 4 051 enlaces);
- los bytes `@17/@18` separan la revancha (2/2) y el uso de objetos (6/3), pero no la cinemática de carga ni la
  recompensa, que son 1/0 como el tiempo y las tiendas;
- `faction_bluemontwinehouse_loading` y `weather_cloudy_0007` tienen la misma forma y el mismo enlace único `2:H`.

Por eso el diseño **no clasifica escenas**: neutraliza el único punto en el que una escena del anfitrión toca al
anfitrión en este cliente, el enlace a su personaje. Es justo el caso que el juego nativo nunca produce (un tercer
cliente con la escena de otro).

---

## 2. Diseño

| Pieza | Qué hace | Por qué ahí |
|---|---|---|
| `guest_seq_foreign_player` | En el 2661 que va a X, el enlace al **dueño** (otro jugador) con tipo 5/4 pasa a 0 en el vector del manejador | Corta en el origen: `0xA9CA40` no toma referencia, la etapa nace sin ese actor y `0x205DC30` nunca lo devuelve |
| `guest_seq_foreign_probe` | P1/P2/P3/P4 de solo lectura | Medir que la etapa conserva el 0 (riesgo [H] de `.xtls`), que ya no hay control ni alineación sobre la copia y que ninguna orden crea un actor sobre el nodo a 0 |
| `guest_seq_route_deny=8CD771B1` | Igual que antes | La cinemática de carga actúa sobre el cliente local; el arreglo no la cubre |

Reglas de la decisión por enlace (`gsf::judge_bind`, `gsf::plan`):
- solo cuando `guest_seq_route` dio `Route` (va a X; con `WouldRoute`, en `probe` de route, solo «would»);
- solo el enlace cuyo actor es el **dueño** de la escena (`payload @5`, comprobado contra `[rbp]`, el `playdata+0` que
  el manejador pasa a `0xA9CA40`);
- solo si ese dueño es un personaje (`0xA…`) y **no** es el propio (ni la clave del 2756 ni el personaje de M);
- solo tipos 5 (activo) y 4 (pendiente: `0x205B4D0` lo haría 5). Los demás tipos del dueño se cuentan y no se tocan;
- **excepto si P1 ya vio que el nodo del dueño NO es de jugador**: el mapa aprendido (recurso, id de nodo) → tipo de
  nodo (`nodo+0xAC`), que escribe P1 al empezar cada escena de un 2661 juzgado (`gsf::NodeMap`, sin bloqueo), es una
  lista de **excepciones**. Sin dato o con `0` (`CurrentControl`): se neutraliza (desde la primera aparición). Con
  1/2/4 o visto con dos tipos distintos: `NotPlayerNode`, nunca se toca. Como esa primera instancia ya se neutralizó,
  P1 avisa con un WRN `[foreign NOT player node]` que nombra la escena. La clave es el recurso del 2661 y el id del nodo
  del enlace, que P1 casa con `nodo+0x30` (o `+0x34`) [E: es el id que compara `0x205DC30`];
- nunca un enlace de NPC, gimmick u otro jugador; nunca una escena propia del invitado (su tienda U2, su uso de objeto).

**Por qué el mapa, y por qué de excepciones.** El inventario pone al dueño en `0x20000001` en 331 escenas de tiempo y 41
de fauna (`spawn_auto`, 10/1) y en `0x20000003` en `memory` y en 23 de `quest`; la revisión (H-1) temía que alguno no
fuera `CurrentControl`. **Los datos de las dos sesiones en vivo lo acotan [V]:** los 133 pares (recurso, nodo)
aprendidos tienen al dueño en un nodo `CurrentControl` (`+0xAC == 0`) — tiendas y revancha en `0x20000002`, tiempo,
fauna, recompensa y misiones en `0x20000001` —, `OTHER NODE 0` y P4 `none`. En cambio, esperar a aprender costó la
escena que importa (§11.3). Por eso el defecto es neutralizar y el mapa queda como red de seguridad para un caso que
todavía no se ha visto.

**Tipo 4.** El inventario solo vio tipo 5 (4 051 de 4 051). Se neutraliza también el 4 por evidencia nativa: el 4 es
«pendiente» y `0x205B4D0` lo convierte en 5 más tarde, así que acabaría resolviendo al dueño; `0x6075B0` además
encola el control de un enlace de tipo 4. Con 0 no ocurre nada de eso.

### 2.1 Por qué el vector es memoria propia del manejador [V capstone]

| Dirección | Instrucción | Qué demuestra |
|---|---|---|
| `0xC05C79` | `mov [rbp-0x80],r15 ; mov [rbp-0x78],r15` | Nace a 0 en el marco de esta llamada |
| `0xC05E12` / `0xC05E19` | `call` al asignador (n × 12 B, del hilo o global) | Se reserva en esta llamada |
| `0xC05E44` | `mov [rbp-0x80],r14 ; mov [rbp-0x74],ebx` | Puntero y capacidad, solo en el marco |
| `0xC05E60` | `mov dword [rax-8],-1 ; mov [rax-4],edx ; mov word [rax],7 ; mov [rax+2],dl ; lea rax,[rax+0xC]` | Entradas de 12 B: `{u32 nodo, u32 actor, u8 tipo, u8, u8}` |
| `0xC05E84` | `mov [rbp-0x78],ebx` | El número |
| `0xC05EA0` | `lea rcx,[rax+rax*2] ; lea rbx,[rsi+rcx*4]` | Paso 12 |
| `0xC05EE9` / `0xC05F1D` | `lea rdx,[rbx+4]` / `lea rdx,[rbx+8]` | Actor en `+4`, tipo en `+8` (leídos del paquete campo a campo) |
| `0xC06179` | `lea r9,[rbp-0x80] ; lea r8,[rbp] ; … call 0xA9CA40 ; mov rsi,[rbp-0x80]` | Se pasa por dirección; después se libera |
| `0xA9CB2E` / `0xA9CC28` | `mov r14,[rsi] ; mov eax,[rsi+8]` / `vmovsd xmm0,[r14] ; mov eax,[r14+8]` | `0xA9CA40` **copia** cada entrada a la tarea (`task+0xA8`, 0x30 B) |
| `0xC061EF` → `0xC0620E` / `0xC06222` | `test rsi,rsi ; …` → liberación | Se libera dentro del mismo manejador |

El paquete crudo (`[R12+0x18]`) no se escribe: lo siguen leyendo igual `guest_stage_diag`, `g_seq_res` y la
clasificación del 2524. El gancho corre en el hilo que construyó el vector, entre su llenado y su único consumidor.

### 2.2 Qué cambia aguas abajo con el tipo 0 [V en `.rsrc`] — el 0 NO es inerte (revisión H-1)

El 0 es el estado nativo **«nodo sin enlazar»**, con caminos propios. Lo que hace cada lector:
- `0xA9CA40`: no toma referencia al actor del anfitrión; la escena ya no exige que esté presente.
- `0x205DC30`: no lo devuelve para ninguna orden ni evento (no cuenta el 0).
- `0x6030D0`: no lo alinea.
- `0x601470`: si el único destino era el jugador, no resuelve ningún actor y va a `0x6075B0`, que solo encola enlaces
  de tipo 4 (`0x6078E7 cmp byte [..+0x14],4 ; jne`); con 0 no encola nada. `0x601C10` resuelve con el mismo
  `0x205DC30` [V] y su camino sin actores es análogo [E].
- `0x602940`: el nodo 0 todavía busca su actor por clave (`0x8AE940`) y solo lo desenlaza si su componente apunta a
  esta etapa (no será el caso: nunca se enlazó); **ya no quita el bit `0x200`** del Status (solo lo hace con tipo 5).
- `0x205F7F0`, `0x2065F40` y `0x20521E0` exigen tipo 5: lo ignoran.
- **`0x5EB310`** (la orden de etapa que monta un `CreateClientActorDesc_Sequencer`; su único llamador es `0x5EB2C0`,
  ranura de vtable en `.xdata 0x558C398`): busca el enlace cuyo nodo tiene el id de la orden (`0x5EB3B1`) y **sigue
  adelante con los tipos {0,3,6,7}** (`0x5EB41F cmp al,7 ; ja error ; bt 0xC9`); con {1,2,4,5} da error. Con el dueño a
  0, una orden así sobre ese nodo **crearía un actor de cliente** (un «doble» del anfitrión). Hoy falla porque el nodo
  está enlazado. P4 lo vigila (§3).
- **`0x205E520`** (llamado desde `0x5ECD90`, otra orden de etapa, y `0x2239BF0`): el 0 tiene una rama propia
  (`0x205E6AF test al,al ; jne`) que resuelve el actor por los datos del nodo (`SequencerGamePlayData_GimmickActor`,
  `0x525520`); el 5 resuelve por clave (`0x205E9A1`) y el resto no hace nada.

Por eso el arreglo excluye los nodos que P1 ve que no son `CurrentControl` (§2): en nodos de jugador estas órdenes no
se esperan [E], y en vivo P4 dio `none` en las dos sesiones (§11). P4 sigue siendo criterio de aborto.

### 2.3 ¿Hay otro valor de tipo inerte en todos los lectores? (revisión H-1 c) [V capstone]

Lectores de `+0xC` encontrados en `.rsrc` (barrido de toda función que recorre `stage+0x120/+0x128` con paso 0x28, más
los ya conocidos):

| Lector | 0 | 1 / 2 | 4 / 5 |
|---|---|---|---|
| `0xA9CA40` (creación) | sin actor | sin actor | referencia al actor |
| `0x205DC30` (destinos) | ignorado, no cuenta | **cuenta**, no resuelve | 5 resuelve |
| `0x602940` (inicio) | sin bit `0x200` | sin bit `0x200` | 5 quita `0x200` |
| `0x205F7F0`, `0x20521E0` | ignorado (exigen 5) | ignorado | 5 |
| `0x205B4D0` | — | — | 4 → 5 |
| `0x6067C0` (`0x606ABF`) | «no aplicar» ({0,1,2,3,6}) | «no aplicar» | aplica |
| `0x6075B0` (pendientes) | no encola | no encola | 4 encola |
| **`0x20532B0`** (`0x2053794`) | **salta** la parte ligada al actor ({0,3,6,7}) | **la ejecuta** (como 4/5: `0x1F53D50`, vf `0xE8` de la etapa con la clave del actor) | la ejecuta |
| **`0x5EB310`** (`0x5EB41F`) | **acepta: crea actor de cliente** | rechaza | rechaza |
| **`0x205E520`** (`0x205E6AF`) | rama de datos del nodo (gimmick) | nada | 5 resuelve por clave |

`0x20532B0` se llama desde `0x1F6D320`, que llaman el tick del gestor `0xA953C0` (el que `guest_seq_play` ejecuta sobre
X) y el 2810 (`0xA9D950`): corre sobre las etapas de X. Ningún valor es inerte en todos: 1/2 evitan `0x5EB310` y
`0x205E520`, pero hacen que `0x20532B0` trate el nodo como enlazado a un actor (lo contrario de lo que se busca) y
que `0x205DC30` lo cuente. **Decisión: se mantiene el 0**, limitado por el mapa de §2, y P4 cuenta en `0x5EB310`
cualquier orden sobre un nodo del dueño a 0 en una escena ajena (`SpawnOnOwner`). Si sale alguna: criterio de aborto.

Otros alcances del dueño **por clave**, sin pasar por los enlaces (no los cambia el arreglo, pero hay que saberlo):
- **`stage+0x98`** (`0x205D7C0`, 19 llamadores): vf21 `0x6014A9 → 0x6014C6 mov [r15],rax` y vf22 `0x601C4D → 0x601C6D`
  ponen el actor dueño en `desc+0`, que consume `.xtls`; también el 2981 (`0xA9E100`), el 2524 (`0xA9E480`) y
  `0x6021F0` / `0x602250` / `0x6022B0` (envíos C>S desde el dueño, preexistentes). P2 marca en cada SequencerControl de
  una escena ajena si `desc+0` es otro jugador (revisión H-2).
- **`0x20532B0`**: busca por clave el actor de **cada** enlace (`0x20533F8`, sea cual sea el tipo) y llama
  `0x1F55140` / `0x1F52BB0` sobre su componente; solo la parte de `0x2053794` depende del tipo.

Si con `1` la pose persiste y P2/P3 ya no muestran `0xA0100001`, el sospechoso es `desc+0` en el control de `.xtls`
(o `0x20532B0`). El siguiente paso sería medir `0x205D7C0` desde vf21/vf22 en escenas ajenas, no ampliar el arreglo a
ciegas.

---

## 3. Claves

| Clave (`[session2]`) | Valores | Por defecto | Dónde |
|---|---|---|---|
| `guest_seq_foreign_player` | `0` \| `probe` \| `1` | `0` (como antes de S44) | `src/mod/session2/guest_seq_foreign.{h,cpp}`, lógica `guest_seq_foreign_logic.h` |
| `guest_seq_foreign_probe` | `0` \| `1` | `0` | igual |

- Solo invitado (`guest=1`). El arreglo necesita `guest_seq_route=1`: vive en su gancho de `0xC0612A` (no se instala
  otro). Con `guest_seq_route=probe` las escenas van a M y el arreglo solo dice «would».
- Al instalar, el arreglo verifica build y `PE`, la `.pdata` de `0xC05BB0` y `0xA9CA40` y los bytes de §2.1 (más el
  propio `0xC0612A`). Si algo no cuadra: `WRN` y no lee ni escribe nada (igual que `0`).
- Las sondas verifican `.pdata`, la firma de 40 B (única en `.rsrc`, comprobado fuera del juego) y el contexto
  (`0x603120`, `0x603368`, `0x603538`, `0x603373` y `0x4FE260` para P3). Si algo no cuadra: `WRN`, esa sonda no se pone.

| Sonda | Punto | Parche | Lee |
|---|---|---|---|
| P1 | entrada de `0x602940` (`.pdata` `0x602940–0x6030C8`) | 5 B `mov [rsp+0x20],r9` | RCX = etapa: `+0x30`, `+0x28`, `+0x60/+0x61`, `+0x98`, `[+0x48]+0x1B8` (nombre), cada entrada de `+0x120`: `[e]+0xAC`, `[e]+0x30`, `e+8`, `e+0xC` |
| P2 | entradas de `0x601470` (`…–0x601C04`) y `0x601C10` (`…–0x6021E1`) | 5 B `mov [rsp+8],rbx` | RCX = etapa, `+0x28B`; R8: `[R8+8]+0x30` ids, `+0x38` número; réplica de `0x205DC30` sobre `+0x120` |
| P3 | `0x6034FA` en `0x6030D0` (`…–0x603646`) | 7 B `mov rax,[rbx] ; mov rcx,[rax+0x68]` | `[rbx]` = actor que se va a alinear, R15 = etapa; réplica de vf178 |
| P4 | entrada de `0x5EB310` (`…–0x5EC372`) | 5 B `mov [rsp+0x10],rbx` | RCX: etapa en `[rcx+0x10]`; R8: `[r8]` = nodo (`+0x30` id); el enlace de ese nodo (`0x5EB3B1`) y su tipo (`0x5EB41F`) |

- P1 y P4 se instalan con `guest_seq_foreign_probe=1` **o** con `guest_seq_foreign_player` distinto de 0; P2 y P3 solo
  con la sonda. Sin P1 no se aprende ninguna excepción y `1` neutraliza en todos los nodos (WRN al arrancar).
- P1 aprende para los 2661 que el arreglo juzgó (un anillo secuencia → recurso, `gsf::SeqResRing`, que llena el
  gancho del 2661). Las líneas P1/P2/P4 marcan FOREIGN solo con la clave propia conocida (revisión H-9).
- P2 trunca a 16 ids de destino y lo marca (`TRUNCATED`, H-7).
- La línea del 2661 lleva el bit `0x200` del Status de la copia (`[[[actor]+0x68]+0x20]+0x390`; verificado en
  `0x602C8E` / `0x602C9F` / `0x17954E4`) y los nodos completos (H-3, H-8).

En los ganchos: lecturas SEH, atómicos y un anillo sin bloqueo; nada de CRT, reservas ni llamadas al juego. Los actores
se identifican por clave, nunca por dirección.

---

## 4. Líneas de log (`cdmp.guest.log`)

- `GuestSeqForeign guest_seq_foreign_player=… (handler layout verified …)`: arranque del arreglo.
- `GuestSeqForeign guest_seq_foreign_probe=1 (READ-ONLY): hooked: … | refused: …`: arranque de las sondas.
- `GuestSeqForeign [foreign] … | 2661 sequencer 0x… resource 0x… of owner 0xA0100001 … | owner's binding
  NEUTRALIZED (type -> 0) | copy's Status +0x390 0x… (bit 0x200 clear) | … written 1/1 | first binds
  (entry, node -> actor, type): [1 0x20000002 -> 0xA0100001 type 5 -> 0 NEUTRALIZED, node type 0 (CurrentControl)]
  [0 0x20000004 -> 0xB0… type 5, node type ?] …`
- `GuestSeqForeign [would foreign] …`: lo mismo en `probe` (nada escrito; sin aprendizaje: «would» siempre).
- `WRN GuestSeqForeign [foreign NOT player node] … | stage seq 0x… '<nombre>' resource 0x… | FOREIGN, owner at a node
  that is NOT the player node | … this instance's owner binding WAS neutralized (type 0) before the exception was
  known …`: la excepción aprendida tarde; esa instancia ya se neutralizó, las siguientes del mismo (recurso, nodo) no.
- `GuestSeqForeign [no owner bind] …`: escena de X sin enlace al dueño (seotda, `node_*_closefilter`, …): no se toca.
- `GuestSeqForeign [not touched] …`: dueño en un nodo que no es de jugador (`owner at a NON-player node`), dueño con
  otro tipo, vector ilegible, dueño del `playdata` distinto, etc.
- `GuestSeqForeign [P1 start 0x602940] … | stage seq 0x… '<nombre>' resource 0x… owner 0x… | FOREIGN, owner at the
  player node with link 5 (moved as an NPC)` (sin arreglo) o `… owner's binding at link 0 (neutralized)` (con arreglo)
  u `owner at a node that is NOT the player node`, y los actores `[node 0x2000000N type T key 0x… link L OWNER]`.
- `GuestSeqForeign [P2 SequencerControl vf21 0x601470] … | desc+0 (0x205D7C0, [stage+0x98]) = ANOTHER player's
  character | target ids N (first …) | 0x205DC30 would count N, resolve M: 0x…(link 5) | control to ANOTHER player's
  character (NPC branch)`.
- `GuestSeqForeign [P3 align 0x6034FA] … | actor 0xA0100001: ANOTHER player's character aligned to the scene's mark`.
- `GuestSeqForeign [P4 client actor order 0x5EB310] … | node 0x…: key 0x… link L node type T | FOREIGN, OWNER's binding
  at 0: a client actor WOULD be created (double)` (u otra clase).
- `GuestSeqForeign summary | …` cada 60 s: veredictos, escrituras y fallos, copias con `0x200`, el mapa aprendido
  (pares, notas, lleno), **por recurso** (veredictos del arreglo y, de P1, el tipo de nodo del dueño, su id y
  `active` / `OTHER NODE` / `neutral` / `pending` / `no owner`, revisión H-1 a), P1/P2/P3/P4 por clase.
- Ya existentes que hay que mirar a la vez: `GuestStage [2661 result 0xC061C1] … 0xA9CA40 result 0x… = …`,
  `GuestSeqRoute [2661 PrepareSequencerAck 0xC0612A] … ROUTED …`, `GuestXVeto [...]`.

---

## 5. Criterios de aborto (apagar con `guest_seq_foreign_player=0`)

1. Cualquier fallo de creación **nuevo** en una escena con `[foreign]`: `GuestStage [2661 result 0xC061C1]` con
   resultado distinto de 0 para esa secuencia (compárese con la misma escena en `probe`).
2. Cierre del juego (de cualquiera de los dos) o error de `crashpad`.
3. `GuestSeqForeign summary` con `write failures` > 0, o un `[P1 …]` con `owner at the player node with link 5` justo
   después de un `[foreign]` de la misma secuencia (el 0 no llegó a la etapa: el riesgo [H] de `.xtls`).
4. La copia del anfitrión invisible, en T, o saltando a otra posición tras empezar una escena (el arreglo ya no quita el
   bit `0x200` del Status de la copia, §2.2). Mírese también `copy's Status … bit 0x200 SET` en las líneas del 2661.
5. NPC de una escena del anfitrión (Matthias, tenderos, posadero) que dejan de animarse o se quedan en A-pose cuando con
   `probe` sí se animaban, **o que miran al vacío o se colocan en otro sitio** (al quitar al anfitrión de la escena, los
   `LookAt 0x5F3600`, `Talk_TwoTarget`, Pivot y posiciones relativas a él se quedan sin destino; revisión H-4).
6. Cámara, interfaz o control del invitado alterados (no debería cambiar nada: esas órdenes ya las cubren los vetos de
   `guest_x_*` y no dependen del actor).
7. **Un doble del anfitrión**: cualquier `[P4 …] OWNER's binding at 0` o un actor nuevo junto a escenas de fauna,
   tiempo o tiendas (revisión H-1).
8. Un WRN `[foreign NOT player node]` (una excepción aprendida tarde, cuya primera instancia ya se neutralizó): mirar
   esa escena en pantalla (NPC, colocación, dobles); si se ve mal, apagar. Y nunca un `[foreign]` posterior del mismo
   (recurso, nodo): el mapa lo impide.

---

## 6. Inis

En `docs/research/ini/`, con cabecera de comentario y la base conservada:
- `cdmp.2inst-s44-probe-guest.ini`: base exacta `cdmp.2inst-s43-activo-guest.ini` + `guest_seq_foreign_probe=1` +
  `guest_seq_foreign_player=probe`. `guest_seq_route_deny=8CD771B1` (sin `9586FFA4`).
- `cdmp.2inst-s44-activo-guest.ini`: igual con `guest_seq_foreign_player=1`.
- **Anfitrión:** `cdmp.2inst-s43-activo-host.ini` sin cambios (el arreglo es solo del invitado; no hace falta un ini
  nuevo).
- `guest_seq_watch` queda como en la base: la vigilancia G5 solo admite claves de NPC (`0xB…`,
  `guest_seq_route.cpp` `watch()`), así que `A0100001` no serviría para la sonda BodyAnim. Lo que mide la copia del
  anfitrión son P1/P2/P3.

---

## 7. Plan de prueba (dos copias en el PC del usuario)

Preparación: `scripts\deploy.ps1` con el `cdmp.asi` de este commit en los dos procesos; copia de las partidas en
`backups/`; anfitrión con `cdmp.2inst-s43-activo-host.ini`. Reiniciar los dos procesos entre pasos. Marcar con
Ctrl+F12 cada momento interesante.

**Paso 1, `cdmp.2inst-s44-probe-guest.ini`** (nada cambia en el juego; solo medir):
1. Anfitrión junto a **Matthias en la taberna de Hernand** al menos 2 minutos, con el invitado al lado. Esperado: la
   pose congelada **sigue** (no hay denegación ni arreglo), `[would foreign]` para `0x9586FFA4`, `[P1 …]` con el dueño
   en el nodo 0 y enlace 5, y P2/P3 con `ANOTHER player's character`. Esto confirma el mecanismo. Anotar también si P2
   marca `desc+0 … ANOTHER player's character` (H-2) y el Status de la copia (H-3).
   **Condición para pasar al paso 2 (revisión H-1):** en el resumen por recurso, anotar el tipo de nodo del dueño por
   familia (tiempo y fauna en `0x20000001`, `memory`/`quest` en `0x20000003`, tiendas y revancha en `0x20000002`). El mapa
   ya impide tocar un nodo que no sea de jugador, pero si `OTHER NODE` sale en familias enteras conviene saberlo antes.
2. Cerca de **tenderos**, hablando con ellos (anfitrión y después invitado). Esperado: `[would foreign]` para los
   `cd_seq_funcnpc_*` del anfitrión; las escenas propias del invitado sin línea (son suyas).
3. **Puertas** (`scene_level_share`): abrir y cruzar como en S43.
4. **Paseo por la ciudad** mirando NPC animados de escenas del mundo (posadero, mesas, niños, Grover, Dahlia): solo
   observar.

**Paso 2, `cdmp.2inst-s44-activo-guest.ini`** (el arreglo actúa), los mismos cuatro puntos:
1. Matthias ≥ 2 min, **desde la primera revancha** (y también con el anfitrión solo en la zona y el invitado entrando
   después: al unirse recibe las escenas vivas copiadas, su primera aparición): **la copia del anfitrión no se
   congela**; `[foreign]` con `written 1/1`; `[P1 …]` con `link 0 (neutralized)`; ningún P2 `ANOTHER player's
   character` ni P3 sobre `0xA0100001`; ningún `[P4 …] OWNER`; ningún WRN `[foreign NOT player node]`; Matthias y el
   gimmick siguen animados y orientados como en el paso 1.
2. Tenderos: el tendero sigue visible y animado y el invitado puede hablar con él (SetInteraction intacto); cuando el
   anfitrión usa la tienda, el invitado no ve su menú ni pierde el control.
3. Puertas: igual que en S43.
4. Paseo: los NPC de escenas del mundo se animan como en el paso 1. **La cámara y la interfaz del invitado no
   cambian** en ningún momento.

Si todo sale bien: unir la rama y borrar el worktree (encargo §4). Después, la prueba final con Shadow.

---

## 8. Lo que no está demostrado

- **[H] Que la etapa conserve el 0.** La etapa se crea en `.xtls` (`thunk 0x14DE16970`), que nunca ha visto un tipo 0
  del servidor. En `.rsrc` todos los lectores de `+0xC` lo aceptan, pero **con semántica propia** («nodo sin enlazar»,
  §2.2-§2.3): no es inerte. P1 lo dirá en el paso 2; P4, si alguna orden intenta crear un actor sobre él.
- **[H] Que los nodos `CurrentControl` nunca reciban la orden de `0x5EB310`** ni la rama de datos de `0x205E520`.
- **[E] Que el id de nodo del 2661 sea `nodo+0x30`** (o `+0x34`), que es lo que casa el mapa aprendido (en vivo, 133
  pares casaron con los nodos del dueño en todos los recursos con enlace a él).
- **[H, acotado por los datos] Que el dueño esté siempre en un nodo `CurrentControl`.** Visto en 133 de 133 pares en dos
  sesiones (§11). Un recurso nuevo con el dueño en otro nodo se neutralizaría una vez (WRN) antes de quedar excluido.
- **[H] Qué deja la pose fija** (control de `.xtls` `0x14F098530`, alineación con modo 4/5 o un evento de cuerpo). Los
  caminos por enlace los corta `0x205DC30`; `desc+0` (`stage+0x98`) y `0x20532B0` siguen alcanzando al dueño por clave
  (§2.3). P2/P3 en el paso 1 dirán cuáles se aplicaban.
- **El principio no se aplica a M (revisión H-10).** Una escena **propia** del invitado (va a M) que enlazara al
  anfitrión no se neutraliza, ni un 2661 del anfitrión que enlazara al invitado sin ser su dueño (`judge_bind` →
  `Keep`). No se ha visto (0 de 1 635).
- **Lecturas sin cerrojo (revisión H-11).** P1, P2 y P4 leen `stage+0x120` sin el cerrojo `[stage+0x20]` (el juego lo
  toma en `0x602A15` y `0x6014E8`): SEH las hace seguras frente a fallos, pero los datos pueden estar desfasados (una
  línea errónea o un dato del mapa que falta). `rd()` captura cualquier excepción, como en el resto del mod.
- **[E] Que el bit `0x200` no se quede puesto** en la copia: con 0, `0x602940` ya no lo quita, pero tampoco hay nada
  que lo ponga por esta escena (nunca se enlaza). Criterio de aborto 4.
- **[H] Condiciones `Player.condition_*` evaluadas en X** sobre la copia dejan de cumplirse. Las ramas llegan del
  servidor (2981), así que no deberían cambiar; si alguna escena del mundo se quedara en una rama, sería esto.
- **P2 a la entrada** replica `0x205DC30` (lectura sin el cerrojo `[stage+0x20]`, como hace el propio juego desde
  `0x601470`): dice a quién iría el control, no que se aplicó. P3 sí está justo antes de la llamada real.
- La cinemática de carga (`0x8CD771B1`) y las órdenes de cámara, interfaz o carga actúan sobre globales del cliente,
  no sobre el actor: el arreglo **no** las cubre (siguen la lista y los vetos `guest_x_*`).
- No se ha medido con Shadow (red real, cargas a distinta velocidad).

---

## 9. Relación con los tenderos (`opcion-d-tenderos`)

- El servidor crea una tienda **por jugador**: en `tenderos-p2b` creó para el invitado sus propias
  `cd_seq_funcnpc_smithy` / `_butcher` (dueño `0xA0100066`, el invitado en `0x20000002`, misma etapa `0x41934310` y
  misma posición que la del anfitrión; inventario §7). **La del invitado es la que debe usar.**
- Este arreglo no toca la del invitado (escena propia: `OwnScene`, va nativa a M) y en la copia de la del anfitrión en
  X solo quita el enlace al Kliff del anfitrión: el tendero (`Shopnpc`, `DummyItem`) sigue enlazado y animado, y las
  entradas SetInteraction siguen igual. Las órdenes `Player.*` del anfitrión (InputBlock, set_enterStore, menú) se
  quedan sin destino en el invitado: es exactamente la distinción que necesita la pieza de tenderos.
- No cambia nada de `guest_shop_order` (rama de tenderos), ni el orden de entrega, ni `gsr::Scene2524`.

---

## 10. Implementación y verificación

- Commits: `573600b` (sondas), `e270ab6` (arreglo + tests), `dc30c87` (inis + este documento + el encargo),
  `b988a4a` (hallazgos de la revisión independiente, `s44-datos/revision.md`: mapa aprendido, P4, H-2/H-3/H-7/H-8/H-9,
  tests de fauna y del dueño `0xA0100363`) y el de §11.3 (sin `Learning`: el mapa pasa a ser de excepciones).
- Código: `src/mod/session2/guest_seq_foreign_logic.h` (lógica pura), `guest_seq_foreign.{h,cpp}` (ganchos, líneas,
  resumen), llamada en `guest_seq_route.cpp` (`guest_seq_route_hook`, punto 2661, veredicto `Route`/`WouldRoute`),
  claves en `config.{h,cpp}`, arranque en `guest.cpp`, tick en `guest_diag_worker.cpp`.
- Tests: `tests/optd_seq_foreign_tests.cpp` con 2661 reales de las capturas (revancha `0x440` de
  `p8-2inst-pose-anfitrion` y `0x2F1` de `p8-2inst-s43-activo`, tienda del anfitrión `0x265`, tienda propia del
  invitado `0x143` de `tenderos-p2b`, `weather_cloudy_0007` `0x5C2`, `use_sealedartifact` 6/3 del anfitrión y del
  invitado, `seotda` (solo NPC) y la cinemática `0x213`): parser, vector del manejador, modos 0/probe/1, decisión por
  enlace, límites y clasificación de P1/P2/P3. `.\scripts\build.ps1`: 80/80.
- Verificación de bytes sobre `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe` (SHA-256
  `57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`, igual al de `D:\RE\CrimsonDesert_2976`):
  `s44-datos/verificacion/verificacion-2976.txt`.

---

## 11. Resultado en vivo (15-10, dos copias en el PC del usuario)

Logs: `C:\Emulador CD\backups\p8-2inst-s44-probe\guest.log` (paso 1) y `...\p8-2inst-s44-activo\guest.log` (paso 2).
Anfitrión con `cdmp.2inst-s43-activo-host.ini`.

### 11.1 Paso 1, `probe` [V]

- Arreglo: `WOULD` ×34 y `no owner bind` ×3; nada escrito; copia oculta (`0x200`) 0 de 37 lecturas.
- Revancha `0x9586FFA4`: `would` ×8; P1 `owner at the player node with link 5` ×7 (nodo `0x20000002`).
- P1 (32 llamadas): `owner at the player node with link 5` ×29, `no binding to the owner` ×2, `OTHER NODE` 0.
- P2: vf21 sobre escenas ajenas 69 (en todas, `desc+0` = otro jugador): `control to ANOTHER player's character (NPC
  branch)` ×26; vf22: ×13. **Confirma el mecanismo: el control de secuenciador «de NPC» sobre la copia.**
- P3 (alineación de `0x6030D0`): ninguna; la pose no viene de la alineación.
- P4: `none`. Mapa: 88 pares, todos con el dueño en `CurrentControl`.

### 11.2 Paso 2, `1` (con la versión que tenía `Learning`) [V]

- Arreglo: `NEUTRALIZED` ×12, `would` (`Learning`, primera aparición) ×25, `no owner bind` ×3; `written 12`, fallos 0;
  copia oculta 0 de 40.
- Revancha: **la primera** (`0x2E9`, 23:00:52) quedó en `Learning`. vf21 dio el control a `0xA0100001` por la rama de
  NPC desde las 23:00:52 hasta el vf22 de las 23:01:43, y el usuario vio la pose justo ahí (marca 1, 23:01:15). **Las 9
  siguientes** (`0x2EA` … `0x2F3`) se neutralizaron: P1 `owner's binding at link 0 (neutralized)` en las que llegaron a
  empezar, control solo a NPC, P4 `none`, copia oculta 0.
- P1 (40): `link 5` ×25 (las primeras apariciones), `link 0 (neutralized)` ×11, `no binding` ×3, `OTHER NODE` 0.
- Mapa: **133 pares**, todos con el dueño en `CurrentControl` (`0x20000002` en tiendas y revancha; `0x20000001` en
  tiempo, fauna `0x2BC74C06` y misiones como `0x519CC9E7` o `0x55116B0A`). P3 `none`.

### 11.3 Por qué falló el paso 2 y qué cambia

- El estado `Learning` dejaba sin arreglar justo la **primera aparición** de cada escena. La que importa es el
  **«recuerdo» del jefe** (`cd_seq_boss_rematch_*`: el fragmento de memoria en el sitio del combate), que pasará en
  todos los jefes; su primera instancia de la sesión es la que el usuario ve.
- Según el usuario, también pasa con el anfitrión solo en la zona y el invitado entrando después: al unirse, el
  invitado recibe las escenas vivas copiadas, que son exactamente su primera aparición.
- **Cambio:** con `1` se neutraliza desde la primera aparición (enlace del dueño de tipo 4/5). El mapa que aprende P1
  pasa a ser de **excepciones**: si P1 ve al dueño en un nodo que no es de jugador, ese (recurso, nodo) queda
  `NotPlayerNode` y no se vuelve a tocar, con un WRN `[foreign NOT player node]` que nombra la escena (esa instancia ya
  se neutralizó). `Learning` desaparece; en `probe`, `would` sin aprendizaje. P4 sigue siendo criterio de aborto.
- El riesgo H-1 queda acotado por los datos: 133/133 pares en `CurrentControl`, `OTHER NODE 0`, P4 `none` y copia
  oculta 0 en las dos sesiones.
- **Pendiente:** repetir el paso 2 con esta versión (la revancha desde su primera instancia, y el invitado entrando con
  el anfitrión ya en la zona).

## 12. Resultado en vivo final (15-10, 23:22–23:30, `b432007`, `C:\Emulador CD\backups\p8-2inst-s44-activo2\`)

Dos copias en un PC. El anfitrión va solo hasta el recuerdo de Matthias en la taberna de Hernand y después entra el
invitado; luego el anfitrión se aleja y vuelve.

- **Rendimiento del arreglo:**
  - `0x9586FFA4`: `applied 3 (node not seen yet 1)`, P1 `neutral 3` (link 0 en las 3 instancias, también la primera), y
    0 controles vf21/vf22 sobre `0xA0100001`;
  - en total, 36 escenas del anfitrión neutralizadas: `NEUTRALIZED at a non-player node 0`, sin ningún
    `[foreign NOT player node]`;
  - P4 `none`, copia oculta 0 de 40, 0 ERR en las dos copias.
- **En vivo:** se veía bien, sin puntos rojos. Visto en la captura de las dos ventanas.
- **Único efecto visual nuevo:** la mascota del anfitrión se ve en su hombro en el invitado cuando ya está en el suelo.
  Es cosmético y se resolvió solo; queda en `PENDIENTES.md`, «Opcionales».

**Estado:** arreglo de raíz validado en vivo para la pose congelada / puntos rojos del recuerdo de jefe.
