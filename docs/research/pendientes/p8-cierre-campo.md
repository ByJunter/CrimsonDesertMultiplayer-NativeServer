# P8 — Cierre del invitado al entrar en el campo del anfitrión (E3b con cliente real)

Build **1.0.0.2976**, base `0x140000000`; todas las direcciones son RVA. Análisis estático de solo
lectura del 2026-10-09. Herramientas: consultas de Ghidra (al empezar ya había un servidor de
otra sesión, instancia `14a10fd5…`, que no arranqué ni paré; a mitad del trabajo desapareció y las
consultas siguieron en modo headless), y capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`.
No se ejecutó el juego, no se tocó el código ni `D:\SteamLibrary`, y no se escribió nada en el
proyecto de Ghidra (solo `exports\decomp\<rva>.c`, que escribe la propia herramienta).
**[E]** decompilado/desensamblado `.rsrc`; **[E-x]** tramo legible de `.xtls` (solo lectura, no se
propone engancharlo); **[V]** volcado, capturas y logs; **[S]** inferencia.
Scripts: `<scratch>\cierrecampo\` (`dmp2.py`, `stk.py`, `x*.py`,
`sweep.py`, `d_*.c`).

## Resumen

- **[V+E]** El invitado se cierra en el estado `0x0A` de la máquina de cambio de campo del cliente
  (`0x7797F0`). Ahí, `.xtls 0x940B170` pide el **personaje del dueño del campo**
  (`0x9A5BE0`) y usa el resultado **sin comprobar que no sea nulo**.
- **[V+E]** La búsqueda que falla es la de un **usuario por su clave**: `0x8AE390`, con
  `ClientActorContainer::Find` (`0x464290`). La clave es **`0x90100000`, el usuario del
  anfitrión**: llega en el `MoveFieldAck` 2174 como dueño del campo. En el volcado, `r8` aún
  vale `0x90100000`, y eso solo es posible en la rama de "no encontrado".
- **[V]** No interviene ni el `characterNo` (`7FFF…`) ni ninguna clave de actor. El cliente del
  invitado **no ha visto nunca** al usuario `0x90100000`: en toda su captura (`.bin` completo) esos
  4 bytes salen **una sola vez**, en ese `MoveFieldAck`.
- **[E]** En el juego original, un `ServerField` tiene un solo usuario (el tope está en
  `.xtls 0x1064C806`), así que el dueño que recibe un cliente es **siempre él mismo**. El primer
  `MoveFieldAck` del invitado, al entrar en su propio campo, traía `0x9010030A`, su usuario, y
  funcionó.
- **[S] Arreglo raíz (en el anfitrión):** en el `take_frame` de la sesión 2, reescribir en el
  `MoveFieldAck` 2174 la clave de dueño (`payload+9`): cambiar la del anfitrión por la del
  usuario 2. Así se restablece en el cliente del invitado lo que el juego da por hecho
  (dueño = yo), sin tocar el estado del servidor.

## 1. Cadena de llamadas (validada)

Pila de `0x13F7000-0x1400000` (hilo 4332, `rsp = 0x13FE750`). Para cada candidato se comprobó que
lo precede un `call` y que cuadra con el tamaño de marco del llamador (`stk.py`).

| `rsp+` | Retorno | `call` previo | Función / marco |
|---|---|---|---|
| — | **crash** `.xtls 0x940B223` `mov rdx,[rdx]` | — | `.xtls 0x940B170` (prólogo: 7 `push` + `sub rsp,0xA0` ⇒ retorno en `+0xD8` ✔) |
| `-0x8` | `0x940B215` (residuo) | `0x940B210 call 0x9A5BE0` | la llamada que devolvió 0 |
| `+0xD8` | `0x77B5D4` | `0x77B5CF call 0x77DA30` (`jmp` → `.xtls 0x940B170`) | `0x77B2F0` (marco `0x1190+0x38` ⇒ su retorno en `+0x12A8` ✔) |
| `+0x12A8` | `0x779C89` | `0x779C84 call 0x77B2F0` | `0x7797F0`, caso `0x0A` del `switch(P+0x15C)` |
| `+0x1478` | `0xAD274D` | `0xAD2748 call 0x7797F0` | `cdmp_MainLoop_NetUpdate_AD2380` |
| `+0x1598` | `0x6173F2` | `0x6173ED call 0xAD2380` | bucle principal |

Descartados:
- `0x780340`: **no** lo precede un `call`. Es un puntero a código que ha quedado en un local de
  `0x940B170`; seguramente una de las retrollamadas de estado de `P`, como `0x7800E0` o
  `0x780140`.
- `0x5B6C988`, `0x571AF00` y `0x5573E28`: no son retornos.
- `0x1371B40`, `0x3387D22`, `0xB064CD`, `0x3D5AEE`, `0x39CDC03`, `0x5B37C5`, `0x3CFB570`,
  `0x5B3A47`, `0x366C83C` y `0x26D3B30`: sí los precede un `call`, pero están **dentro del marco
  de `0x77B2F0`**, entre `+0xE0` y `+0x12A8`. Son residuos de llamadas anteriores (hay junto a
  ellos punteros de D3D12, Steam y NVIDIA).

### 1.1 Qué hace cada pieza [E/E-x]

- `P = *(g_WorldSystem+0x60)` es el objeto cliente del **cambio de campo**:
  - estado en `+0x15C`;
  - campo destino `+0x128`, **dueño `+0x12C`**, índice principal `+0x130`;
  - posición destino `+0x134…+0x15B`.
  `MoveFieldAck::Process 0xB87360` → `0x9FA660` → `.xtls 0x9404C60` (*thunk* `0x7792C0`) rellena
  esos campos con el paquete.
- `Q = **(P+0xC0)` es el **campo del cliente**: nivel `+0x2B2`, **dueño `+0x180`**.
  - Estado 9 (`0x77AE70`, en `0x77AEE9`) → `0x9A5D20`:
    `Q+0x2B2 = nivel; Q+0x180 = P+0x12C` (`0x9A5EE2`).
- Estado `0x0A`, `0x77B2F0`: interpola la posición del jugador local hacia `P+0x150` (en la
  pila están los flotantes del paquete, `C627772A 441B0AA0 C574A8EA`) y llama a
  `.xtls 0x940B170(P, &pos, rot)`, que:
  - si `P+0x200 & 0x1FFFFFFF` (un nodo de destino creado en el estado 4, `0x77A3B0`, con el
    contexto de campo del jugador **local**) → `x = 0x9A5BE0(Q)`;
  - hace `rdx = x ? x+8 : 0; mov rdx,[rdx]` → **AV si `x == 0`**;
  - después, `0x1F6C190(x, …, P+0x200)` busca ese nodo en `x`.
- `0x9A5BE0(Q)` ejecuta:
  1. `0x8AE390(ClientActorManager, &att, Q+0x180)`: el usuario con esa clave si la clase es
     usuario (`&0x30000000 == 0x10000000`), si `Find` lo encuentra y si tiene `+0x5E & 4`;
  2. `0x206BA40(usuario)` → `usuario+0xD0`, su personaje;
  3. devuelve `*(*(pj+0x68)+0xA8)+0x18`.

  Es decir, **un componente del personaje principal del dueño del campo**.

### 1.2 Qué puntero es nulo y por qué [V+E]

- Prueba por registros:
  - `0x9A5BF4` carga `r8d = [Q+0x180]`;
  - en la rama "encontrado", `0x8AE4C4` pone `r8d = 0x10` antes de `vf(+0xC0)`, y `0x206BA40`
    vuelve a fijarlo;
  - en la rama "no encontrado/no válido" (`0x8AE521` → `0x9A5C1F` → dtor `0x1434880`, que con
    `+0x10 == 0` sale sin hacer nada), `r8` sobrevive.

  En el volcado, **`r8 = 0x90100000`**. Además, `rax = rdi = rbx = 0` cuadra con `ebp = 0` →
  `rax = 0`.
- Es decir, **`Q+0x180 = 0x90100000`** y el cliente no tiene ese usuario (o no lo tiene
  "válido").
- Los estados 2, 7 y 9 llaman a `0x9A5BE0` **antes** de que `0x9A5D20` cambie el dueño y
  comprueban el nulo. El primer uso con el dueño nuevo es `.xtls 0x940B170`, que no lo comprueba.
- Captura del invitado:
  - **96.798** — `MoveFieldAck` `04 | 0B03 10A0 | 03000000 | 0A03 1090 | …`: dueño = el propio
    usuario 2. Funciona.
  - **126.058** — `… | 01000000 | 0000 1090 | 01000000 | {1, A010030B, pos…}`: dueño = el
    anfitrión → cierre 2,5 s después.
- El servidor pone ese valor a propósito: `TransformSync::SendMoveFieldAck 0x2BCA3D0` envía
  `(int)field[0x30]`, es decir, `ServerField+0x180`. Con H1, sigue siendo el anfitrión
  (`opcion-d-campo-compartido.md` §3.3).

Formato del payload de 2174 (tras la cabecera troc):

| Desplazamiento | Contenido |
|---|---|
| `+0` | u8 `04` |
| `+1` | u32 actor |
| `+5` | u32 campo (`FieldInfoKey`) |
| **`+9`** | **u32 usuario dueño** |
| `+13` | u32 índice principal |
| `+17` | N entradas de `0x30` (`{u32 índice, u32 actor, 40 B de transformación}`) |

Comprobado con los dos paquetes, de 65 B = 17 + 48.

## 2. Por qué solo con un cliente real

- `cdmp_peer` es un cliente sintético: manda las Req de un guion y no ejecuta el código de
  cliente del juego. No tiene `ClientActorManager` ni máquina `0x7797F0`, y no hay `0x9A5BE0`.
  El `0x90100000` le llega y no lo usa nadie.
- El cliente del anfitrión (sesión 1) siempre recibe su propio usuario como dueño.
- Solo un cliente real que recibe como dueño a **otro** usuario recorre `0x77AE70` → `0x9A5D20`
  (`Q+0x180 = 0x90100000`) → `0x77B2F0` → `0x940B170`. Ese caso no existe en el juego original
  (un usuario por campo).

## 3. Arreglo propuesto

### 3.1 Raíz, en el anfitrión: reescribir el dueño del 2174 para la sesión 2 [S, apoyado en E]

En `take_frame` (`src/mod/session2/server.cpp`, P3, sobre la copia `p` que va al túnel):
- si `opcode == 2174`, `len == 17 + 0x30·n` y `u32 @ payload+9` es de clase usuario y
  **≠ usuario 2**;
- entonces escribir **la clave de usuario 2**. Ya se lee en ese mismo sitio del
  `MainUserDataStartAck` 2646 (`u32 @ +0`); conviene guardarla en un atómico propio por época, o
  usar `session_user_key(s2)`;
- registrar una línea `antes → después`.

No llama al juego, no toca el servidor (`ServerField+0x180` sigue siendo el anfitrión, como
requiere H1) y no cambia el `SetMoveFieldAck` 2277 (`actor, u8`, sin dueño).

Por qué es seguro [E]:
- Lectores en el cliente de `P+0x12C`/`Q+0x180` (barrido de las clases `0x779000-0x781000`,
  `0x9A5000-0x9B2000` y de los tramos `.xtls`):
  - `0x77AEE9` → `0x9A5D20`, que lo copia a `Q+0x180`;
  - `0x77C615` → `0x7792C0`, que reaplica el movimiento;
  - `0x9A5B70` y `0x9A5BE0`: obtienen componentes del personaje del dueño (13 llamadores de
    `0x9A5BE0`).

  Todos **lo resuelven en local**. Ninguno lo devuelve al servidor:
  - `MoveFieldCompleteReq` 2549 es `00`;
  - ningún C>S de la captura lleva clave de usuario.
- Con "dueño = yo", `0x940B170` busca el nodo `P+0x200` en el **mismo** contexto en el que lo
  creó el estado 4, igual que en la primera entrada de campo, que funcionó.

Alternativa descartada: **que el invitado conozca al usuario del anfitrión**. Habría que crearle
un usuario remoto "válido" (`+0x5E&4`), algo que el cliente original nunca hace. Aun así,
`0x9A5BE0` devolvería el componente del pj **del anfitrión**, y `0x1F6C190` buscaría en él un
nodo creado en el contexto del invitado: un fallo distinto.

Alternativa en el invitado (solo si el anfitrión no puede): enganchar la entrada `.rsrc` de
`0x9A5D20` (o `0x9FA660`, cambiando el `param_4`) y sustituir el dueño por el usuario local en
modo remoto. Nada en `.xtls` ni en los *thunks* `0x77DA30`/`0x7792C0`.

### 3.2 Riesgos

- **R1 (el siguiente fallo probable):**
  - otros S>C de la sesión 2 pueden llevar `0x90100000`: los `ActorLoadDataAck` 2005 del Kliff
    del anfitrión y los NPC/gimmicks de su campo (en el campo propio llevaban `0A031090` en
    `~+20`);
  - no se ha comprobado si el cliente los busca con control de nulo;
  - **no** reescribirlos a ciegas: primero medirlos.
- **R2:** con dueño = yo, el invitado ejecuta lo que el cliente hace "como dueño", igual que en su
  campo. Las comprobaciones de verdad las hace el servidor (C5/C7 de E3b).
- **R3:** solo hay que reescribir valores de clase usuario y distintos del usuario 2. Si el
  formato no cuadra, dejar el paquete intacto y contarlo.
- **R4:** aparte de esto, la captura muestra `LoadingCompleteReq` `01` (C6 pide `00`).

### 3.3 Verificación

En un PC (anfitrión + `cdmp_peer`, E3b):
1. Prueba unitaria del reescritor con los dos 2174 reales (96.798, sin cambio; 126.058, con
   cambio) y con longitudes erróneas.
2. Log `MoveFieldAck s2 dueño 0x90100000→<u2>`. H1/H2 deben salir igual que en E3b, y
   `[F+0x180]` del servidor debe seguir valiendo el anfitrión.
3. Contador nuevo: S>C a la sesión 2 que contienen la clave del anfitrión, por opcode (R1).

En Shadow:
4. El invitado pasa el estado `0x0A` sin cierre y manda `MoveFieldCompleteReq` 2549.
5. Recibe `MoveFieldCompleteAck`/`PostAck` y los `ActorLoadDataAck` del campo del anfitrión.
   Vigilar en concreto qué hace con el Kliff `0xA0100001` (dueño `0x90100000`).
6. Si hay otro cierre: mismo método (`dmp2.py`/`stk.py`) y comprobar si es R1.
