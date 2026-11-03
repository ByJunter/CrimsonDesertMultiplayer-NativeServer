# P8 · Reproducción en el invitado de las escenas copiadas del anfitrión (`guest_seq_play`)

10 de octubre de 2026, rama `opcion-d` (sobre `aeb55bb`). Build del juego **1.0.0.2976**, direcciones en **RVA**
(base `0x140000000`). Aprobado por el usuario: reproducir en el cliente del invitado las escenas que la opción A
copia al gestor X del Kliff del anfitrión. No se lanzó el juego ni se desplegó nada.

**[V]** = comprobado (instrucciones con capstone sobre el PE de solo lectura
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, RTTI, compilación, pruebas, desensamblado del `cdmp.asi`).
**[E]** = hipótesis pendiente de la prueba en el juego.

Base: `p8-xtls-lectura.md` (§1 el paso a 3, §2 `+0xCF`, §3 `0xAA3E70`, §6 riesgos R1–R8), `p8-s8-escenas.md`
(estado de la sesión 8 y plan), `p8-impl-opcion-a.md` (opción A).

## 0. Claves nuevas en `[session2]`

| Clave | PC | Valores | Qué hace |
|---|---|---|---|
| `guest_seq_play` | invitado | `0` \| `probe` \| `1` | Fase 1: en cada fotograma, `0xA953C0(X, Q, modo)` para el gestor X del Kliff del anfitrión, y `X+0x231 = 3` una vez por cada 2322 enrutado (§2). `probe`: decide, cuenta y escribe las líneas; no llama a nada ni escribe |
| `guest_seq_play_live` | invitado | `0` \| `1` | Fase 2 (necesita `guest_seq_play`): además `0xAA3E70(X, dt)` cuando X está en 3, y el 2524 enrutado a X (§3). `0xAA3E70` toca al jugador local, la cámara y la transición de capítulo: para X se salta esa parte con un gancho y una compuerta (§13.5). **Segundo paso** de la sesión, con `cdmp.p8-s7-shadow-fase2.ini` |

- Necesitan `guest_seq_route=1`: es la que llena la tabla de X. `guest_seq_play=1` sin ella se rechaza (ERR, apagado).
- En el anfitrión no hay clave nueva: el filtro de `observer_seq_copy` ya descarta 2276 y 2105 (§5).
- Inis:
  - **primer paso:** `cdmp.p8-s7-shadow.ini` con `guest_seq_play=1` y `guest_seq_play_live=0` (fase 1: tick `0xA953C0`
    y el 3);
  - **segundo paso, en la misma sesión:** `cdmp.p8-s7-shadow-fase2.ini`, idéntico salvo `guest_seq_play_live=1`
    (fase 2, con la guarda de §13.5), solo si el primer paso se vio bien.
  - Los comentarios de los inis explican cómo apagarlo (§10).

## 1. Puntos y bytes verificados [V]

### 1.1 El gancho del tick: `0x8B6144`

`0x8B6060` (RUNTIME_FUNCTION `0x8B6060–0x8B6906`). Su único llamador es `0xAD277F`, dentro de `0xAD2380` (el
bucle principal), **una vez por vuelta**; antes pone `xmm1 = [[r13+0x60]+0x64]` (el dt del fotograma).

| Dirección | Bytes | Instrucción | Para qué |
|---|---|---|---|
| `0x8B6076` | `48 8D 68 A1` | `lea rbp,[rax-0x5f]` | el marco: `Q = [rbp-0x41]` |
| `0x8B608F` | `C5 F8 28 F1` | `vmovaps xmm6,xmm1` | dt en `xmm6`; nada lo escribe antes de `0x8B6144` (registro no volátil) |
| `0x8B60AF` | `48 8B 4F 50 48 85 C9 0F 84 D8 00 00 00 F6 41 5E 10 0F 84 CE 00 00 00` | actor local `[rdi+0x50]` con `+0x5E & 0x10` | la misma condición que exige `0x8B2660` |
| `0x8B60C6` | `48 8D 55 B7 E8 F1 45 F4 00` | `lea rdx,[rbp-0x49] ; call 0x17FA6C0` | el FieldAttacher nativo: Q queda retenido hasta `0x8B618F` |
| `0x8B6102` | `48 8B 4D BF 48 8B 01 FF 50 48` | `call [Q->vf48]` (`0x9A5BE0`) | M en RAX |
| `0x8B613D` | `4C 8B 87 80 00 00 00` | `mov r8,[rdi+0x80]` | R8 = el objeto del modo |
| **`0x8B6144`** | **`45 0F B6 40 50`** (+ `48 8B 55 BF 48 8B C8 E8 6B F2 1D`) | **`movzx r8d,byte [r8+0x50]`** | **el punto**: 5 bytes, una sola instrucción, sin saltos hacia dentro, sin operando RIP |
| `0x8B6149` | `48 8B 55 BF 48 8B C8 E8 6B F2 1D 00` | `mov rdx,[rbp-0x41] ; mov rcx,rax ; call 0xA953C0` | el tick nativo de M justo después |
| `0x8B63CE` / `0x8B67E3` | `80 3D 43 9E 40 06 00 75 11 C5 F8 28 CE 48 8B CF` / `80 3D 2E 9A 40 06 00 75 0C …` | `cmp byte [0x6CC0218],0 ; jne ; vmovaps xmm1,xmm6 ; mov rcx,rdi ; call 0x8B2660` | la vida nativa de las escenas: solo con ese byte a 0, y con el mismo dt (`xmm6`) |

En el gancho: **RAX = M**, **R8 = `[rdi+0x80]`** (modo = `byte [R8+0x50]`, lo que carga la instrucción
reemplazada), **Q = `[RBP-0x41]`**, **dt = `XMM6`**. Es lo que pedía el encargo, comprobado.

- **Modo:** `[[rdi+0x80]+0x50]` es el mismo byte que usa el juego en los dos sitios: `0xA953E4` (`cmp r8b,2 ; je`:
  con 2 no llama a `0xA97300`) y `0x8B26A2` (`cmp byte [rcx+0x50],2 ; je`: con 2 no llama a `0xAA3E70`).

### 1.2 Las funciones que se llaman [V]

| Función | Primeros 16 bytes | Firma (comprobada en el código) |
|---|---|---|
| `0xA953C0` (tick) | `48 89 5C 24 10 48 89 6C 24 18 56 57 41 54 41 56` | `void(G rcx, Q rdx, u8 modo r8b)`. Con modo ≠ 2 llama a `0xA97300(G, Q)`, que **bloquea `[G+8]`** (`0x393650`, `r8b = 1`) y sube `G+0x260`; después recorre los borrados diferidos `+0x1F0`/`+0x1E0` con `[G+8]` (`0x393DF0`) |
| `0xAA3E70` (vida) | `48 8B C4 48 89 58 10 48 89 48 08 55 56 57 41 54` | `void(G rcx, float dt xmm1)`: `r13 = rcx`, `xmm6 = xmm1`, resta dt de `G+0x234` |
| `0x9A5B70` (vf40 del campo) | `40 53 48 83 EC 40 44 8B 81 80 01 00 00 48 8D 54` | `ptr(campo rcx)`: `FindUser([campo+0x180])` → `user+0xD0` → `[[a+0x68]+0x138]+0x18`, 0 si no lo encuentra |

- Vtables por RTTI: `pa::ClientField` = `0x55BCEA0` (`+0x40` = `0x9A5B70`, `+0x48` = `0x9A5BE0`);
  `pa::ClientSequencerStageManager` = `0x55C9720`; `pa::ClientSequencerPlayDataActorComponent` = `0x55BEE78`.
- Por qué vf40 [V]: `0x602940` (arranque de una instancia, llamado por `0xA9D860`) hace `call [r8]->vf40`
  (`0x602986`) y a continuación `lea rdx,[rax+8] ; cmove rdx,0 ; mov rdx,[rdx]`: con vf40 = 0 el juego se cierra en
  `0x602997`, antes incluso de `0x1AC2830`.

### 1.3 El punto del 2524: `0xC08178` [V]

Manejador de `StageOnBranchEventAck` 2524: `0xC07D00` (RUNTIME_FUNCTION `0xC07D00–0xC0835C`).

| Dirección | Bytes | Para qué |
|---|---|---|
| `0xC07D25` | `4D 8B F8 48 8B FA 48 8B D9 33 D2 4D 8B 70 18` | `mov r15,r8` (el envoltorio; R15 no se vuelve a escribir) y `mov r14,[r8+0x18]` (el paquete) |
| `0xC080D1` | `44 8B 45 A0 48 8D 55 08 49 8B CC E8 FF E6 C9 FF 90 80 7D 18 00 74 29 48 8B 5D 10` | la búsqueda del actor del paquete `0x8A67E0` |
| `0xC0810E` | `49 89 1F` | `mov [r15],rbx`: el actor resuelto (el que `0xC0811F` pasa a `0x17FA6C0`) |
| `0xC0816E` | `48 8B 4D B8 48 8B 01 FF 50 48` | vf48 del campo → M en RAX (`0xC08175` = `FF 50 48`, la llamada del encargo) |
| **`0xC08178`** | **`48 8D 4D F8 48 89 4C 24 28`** (+ `0F B6 8D A8 10 00 00`) | **el punto** (tras la llamada): `lea rcx,[rbp-8] ; mov [rsp+0x28],rcx`, 9 bytes, dos instrucciones enteras, nadie salta a `0xC0817C`, sin RIP |
| `0xC08188` | `88 4C 24 20 44 8B 4D A4 4C 8B 45 A8 48 8B 55 B8 48 8B C8 E8 E0 62 E9 FF` | `mov rcx,rax ; call 0xA9E480(M, campo, secuenciador, …)`: RAX no se toca entre medias |

- El gancho no puede ir en `0xC08175` (es la propia llamada): va en la instrucción siguiente, igual que los otros cuatro
  puntos de `guest_seq_route` (RAX ya es el gestor devuelto).
- Lectura: actor = `[R15]`; paquete = `[R15+0x18]`. El payload es `u8 4 | u32 actor @1 | u64 secuenciador @5 | …`,
  el mismo formato que el 2981. Con la cabecera de 5 bytes queda: clave en `raw+6`, secuenciador en `raw+10`.
  **Corregido en la sesión 9** (§15): antes se leían `@0`/`@4` y ningún 2524 se enrutó (`KeyMismatch` ×44).
- **No lo engancha ningún otro módulo**: es un gancho propio de `guest_seq_route`.

## 2. Fase 1: tick de X y paso a 3

### 2.1 Qué hace el gancho en cada fotograma

Por cada X de la tabla (§4), en este orden (`gsp::plan`; el primer fallo es el motivo):

1. **Fotograma:**
   - modo ≠ 2;
   - M ≠ 0;
   - `M+0x231 == 3` (la carga propia terminó: R2);
   - Q es un `ClientField` cuya vf40 es `0x9A5B70`.
2. **La ranura** (revisión, §12):
   - enrutada en esta conexión (`guest_connection_epoch`); si no, la ranura se vacía;
   - enrutada en este mismo campo: el `ClientField` sobre el que corrió la vf48 del manejador es el Q de este
     fotograma; si no, la ranura se vacía y el siguiente paquete enrutado la vuelve a llenar.
3. **Identidad de X** (lecturas con SEH):
   - `[X]` = vtable de `ClientSequencerStageManager`;
   - la clave de `[X+0x18]` = el personaje que vio el enrutado;
   - el gestor propio de ese actor (`[[[[X+0x18]+0x68]+0xA8]+0x18]`) es X;
   - **vida:** la tabla de actores del cliente da, para esa clave, ese mismo actor, y su gestor sigue siendo X
     (réplica de la búsqueda, solo lectura con SEH, en cada fotograma y solo si lo anterior se cumple);
   - X ≠ M;
   - `[X+8]` ≠ 0 y `[[X+8]]` = `[[M+8]]` (el cerrojo que bloquea `0xA97300` es de la misma clase que el de M).
4. **Solo con `1`:** `vf40(Q)` ≠ 0. Se pregunta una vez por fotograma, y solo si algún X pasó lo anterior.
5. **El 3**, si se cumple todo y **antes** del tick:
   - `X+0x231 == 2`;
   - `X+0x174 == 0`;
   - `M+0x231 == 3`;
   - `+0x1B8 + +0x1C8 > 0` (instancias en `+0x1B0` o `+0x1C0`);
   - aún no escrito para este X y este 2322 enrutado (o, de respaldo, X volvió a 2 sin un 2322 nuevo: hasta 3
     veces, contado como «re-armed»).
6. **Tick:** `0xA953C0(X, Q, modo)`.
7. **Vida** (con `_live`): `0xAA3E70(X, dt)` si X está en 3 y `byte [0x6CC0218] == 0`.

Un `thread_local` impide que el gancho se ejecute dentro de sí mismo (contado como «re-entries»).

### 2.2 Decisiones

- **Escribir el 3, no llamar a `0xA97B30`** (`p8-xtls-lectura.md` §1.1–§1.4 y §5, leído en Ghidra):
  - `0xA97B30` = `jmp .xtls 0x9F8FCE0`, que solo escribe `+0x231 = 3` (`0x9F8FD19`, sin cerrojo) y reproduce en el
    acto las entradas elegibles de `+0x1C0`; no toca contadores, tiempos, `+0x174`, `+0x1B0`/`+0x1D0` ni callbacks;
  - con el tick, escribir el 3 equivale a la llamada nativa, salvo la ráfaga inicial sin freno;
  - escribir no ejecuta `.xtls` fuera de su sitio y no depende del FieldAttacher del Kliff remoto;
  - el tick reproduce como mucho una instancia por llamada (`0xA97503`).
- **«Una sola vez» = una vez por 2322 enrutado a X.**
  - El único escritor de 2 en X es el 2322 enrutado (`0xB87A67`); los de 0/1 solo tocan M.
  - Por eso cada 2322 enrutado da a la ranura un `gen` único en la vida de la tabla, y el 3 vuelve a estar
    pendiente solo si llega otro.
  - Un X nuevo para el mismo personaje (su Kliff recreado) también vuelve a estar pendiente.
- **Orden respecto al tick de M (revisión 12):** el de X va justo **antes** del de M, en el mismo fotograma.
  - El gancho no puede ir después de `0x8B6150`: ahí RAX (M) y R8 (modo) ya no están, y `0x8B6156` es una
    instrucción con operando RIP.
  - X y M solo comparten Q (solo lectura) y quizá la clase de su objeto de cerrojo. Cada llamada toma y suelta el
    suyo dentro, sin anidar, así que el orden no cambia nada para M.
- **Hilo [V estático] (revisión 4):** `0xAD2380` (bucle principal, hilo principal; `troc-dispatch.md`) ejecuta los
  Ack en `0xAD246A call 0x35D0C0` → `0x35D1B6 call 0x35D2A0`, y después llama a `0x8B6060` en `0xAD277F`.
  - El 2322 (`0xB87890`, ranura 2 de la vtable `0x560AF90` del mensaje) y el resto de manejadores de escena corren
    ahí, en el mismo hilo y antes que el gancho en esa vuelta.
  - El 2 nativo de `0xB87A67` nunca puede pisar un 3 nuestro posterior: solo lo haría un 2322 nuevo, que trae un
    `gen` nuevo.
  - Aun así:
    - la tabla se escribe con un try-lock que no bloquea y se lee con una secuencia por ranura;
    - se cuenta si un gancho de enrutado corre en otro hilo («routing hooks on another thread»);
    - si X vuelve a 2 con el mismo `gen`, el 3 se reescribe, hasta 3 veces.
- **El tick también en el estado 2** (antes del 3): mueve lo cargado de `+0x1B0` a `+0x1C0`, como hace el juego con M.
  Pero solo con `M+0x231 == 3` y vf40 ≠ 0: `0xA97300` también reproduce en el estado 2 lo que esté en `+0x1D0`.
- **Comprobación de vf40 en cada fotograma con tick, no solo antes del 3:** el arranque de cualquier instancia
  (`0x602940`) la necesita.
- **Llamadas bajo filtro SEH con la política de la revisión I1:**
  - una excepción dentro del código del juego apaga el módulo (`g_disabled`), escribe una línea directa al log
    (`game_fault_continue`) y **se deja pasar**;
  - no se traga nunca: `0xA97300` tiene `[X+8]` bloqueado.
  - **Nadie más arriba la reanuda (revisión 5) [V, información de desenrollado del PE]:**
    - `0x8B6060`, `0xAD2380`, `0x617290`, `0xA953C0`, `0xA97300` y `0xAA3E70` solo tienen `UNW_FLAG_UHANDLER`
      (`0x4898DE8`), que limpia al desenrollar y no se consulta al buscar manejador;
    - `WinApp::vf9` `0x3F41240` no tiene manejador;
    - por encima, el `main` del CRT y el filtro de excepciones no manejadas, que cierran el proceso [E].
    - Es decir, el fallo es un cierre real donde ocurrió. Si algo lo reanudara, el módulo ya está apagado y no
      vuelve a llamar al juego. El cerrojo de `[X+8]` quedaría tomado: no se puede soltar de forma segura desde el
      filtro, porque su guarda vive en el marco que falló.
  - Las lecturas propias sí van con SEH que devuelve un valor por defecto.
- **Mismo hilo y fotograma que M:**
  - el hilo principal, dentro de `0x8B6060`, inmediatamente antes de `0xA953C0(M)`;
  - con Q retenido por el FieldAttacher nativo, el mismo estado en que el juego llama al tick de M.
- **`0xAA3E70(X)` en el mismo punto**, no en `0x8B63DE`:
  - como recomendaba `p8-xtls-lectura.md` §3.4: mismo fotograma, mismo dt y mismas condiciones (actor local con
    `+0x5E & 0x10`, modo ≠ 2, `byte [0x6CC0218] == 0`);
  - diferencia: el juego llama a la de M después de soltar el FieldAttacher (`0x8B618F`); la de X va con Q aún
    retenido. Ver riesgo R4 en §8.
- **Vida solo con X en 3:** en 1/2 no puede haber escenas vivas en X. `0xAA3E70` solo haría su parte del usuario
  local (`0xA96070`, `0xA9A1F0`, `0xA9A410`, `0xAA5040`, `0xAA5400`), que no se ha leído.
- **No se llama a `0xAA3350`:** busca las posiciones candidatas del usuario local y es la fuente probable del 2100
  (§3.2 del informe `.xtls`).
- **Nada en el gancho que asigne, bloquee o formatee:**
  - lecturas, atómicos, `gsp::plan` y un anillo de eventos acotado sin cerrojo;
  - las líneas las escribe el hilo de diagnóstico cada 5 s.
  - Comprobado en el `cdmp.asi` (`dumpbin /disasm`): `on_frame` solo llama a sus lecturas SEH, `gsp::plan`,
    `emit`, `p8_now_us`, los tres envoltorios guardados y los auxiliares atómicos. Ni CRT, ni `memcpy`/`memset`, ni
    instrucciones VEX.
  - Lo único que entra en el juego son las tres llamadas aprobadas: tick, vida y vf40.

## 3. Fase 2: vida de las escenas y el 2524

- **`0xAA3E70(X, dt)`** (§2.1, paso 6) avanza las escenas vivas de X (`G+0x28`):
  - ramas, esperas y temporizadores;
  - la cola de enganches `G+0x190` (`.xtls 0x8F80300`), que pone `comp+0x3D8 = 0` en los NPC que aparecen después
    (R5 del informe `.xtls`);
  - envía **2105** al cumplirse una espera de rama: lo descarta el filtro del anfitrión (§5).
- **2524 a X:** quinto punto de `guest_seq_route` (`Point::P2524`, §1.3):
  - mismas reglas de decisión que los otros (`gsr::decide`: el actor del paquete es otro personaje con su X, vtable
    comprobada, `X ≠ M`). RAX := X, y `0xA9E480(X, …)` engancha los actores a la escena viva de X (`.xtls 0x8F80300`).
  - Solo se instala con `guest_seq_play` (probe o 1) y `_live=1`.
  - Solo **enruta** con `guest_seq_play=1` y `guest_seq_route=1`. Con probe en cualquiera de las dos, decide y cuenta
    (`WOULD route`).
  - **Solo a un X que esté en la tabla** en esta conexión, es decir, uno que el gancho del tick atiende (revisión
    11). Si no, `Verdict::NotPlayed`: el 2524 sigue nativo, a M.
  - **Nunca pasa por la puerta de entrega:** los 2524 llegan de forma nativa (144 en la sesión 8, a M). Filtrarlos
    cambiaría ese camino validado. `gsr::named_actor` sigue sin conocer el 2524.
  - No alimenta la tabla de X: no crea nada en X.

## 4. La tabla de X (`gsp::XTable<8>`)

- La llenan los ganchos de `guest_seq_route` **solo cuando un 2661/2981/2810/2322 se enruta de verdad** a X
  (`Verdict::Route`; revisión 3): `guest_seq_play_note_x(X, clave, campo, es_2322)`.
  - `campo` es el `ClientField` sobre el que corrió la vf48 del manejador: la ranura del marco que carga en RCX justo
    antes de la llamada, con los bytes ya verificados en cada punto.
  - Son `[rbp-0x40]` (2661), `[rbp-0x38]` (2981), `[rbp-0x48]` (2810 y 2524) y `[rbp-0x58]` (2322).
- La leen el gancho del tick (que también vacía ranuras) y el hilo de diagnóstico.
- Funcionamiento:
  - escritura con un try-lock que no bloquea: si está ocupado, se cuenta («writer busy») y se reintenta con el
    siguiente paquete;
  - lectura con una secuencia por ranura: impar mientras se escribe, y una lectura inestable cuenta como ranura vacía;
  - una ranura por clave; un X, campo o conexión nuevos para la clave la sustituyen;
  - con la tabla llena se reutilizan primero las ranuras de una conexión anterior;
  - `gen`: 0 hasta el primer 2322 enrutado, después un valor único (contador global).
- **Se vacía** cuando la ranura es de otra conexión o de otro campo (§2.1, paso 2).
- Un X destruido en la misma conexión y el mismo campo lo detectan la identidad y la comprobación de vida.
- Si la tabla está llena, se cuenta (`full`).
- `guest_seq_play=1` se **rechaza** (apagado, una línea ERR) si `guest_seq_route` no es `1`.

## 5. Filtro del anfitrión: 2276 y 2105 (R1)

`observer_seq_copy` (anfitrión, hilo de red, `offer`) añade a la lista de C>S del usuario 2 que se descartan:

| Opcode | Formato | Evidencia |
|---|---|---|
| **2276** `DeleteSequencerReq` | `u64 secuenciador @0 \| u8 código` | [V] serializador `0xC02CB0`: `mov rax,[r8]` (`0xC02D1E`) 8 bytes, luego 1 byte; opcode `0x8E4` (`0xC02D9D`). Lo envía `0xA95530` (p. ej. código 6 desde `0xA9D860`) |
| **2105** `StageBranchStateDelayEndReq` | un `u64 @0` = id de secuenciador | [V] El manejador del servidor `0x2A5C920`: lee 8 bytes (`0x2A5C9BC`); comprueba que son todo el paquete (`0x2A5C9F6`); los pasa a `0x2892210` junto con el gestor de la vf48 del campo. `0x2892210` los busca con `0x1F6C190(gestor, &out, id)` (`0x289231B`). Es la misma búsqueda que hacen el 2810 del cliente con su `u64` de secuenciador (`0xA9D950` → `0xA9DA0B mov r8,rbx` → `0x1F6C190`) y `0xA95530` con el `u64` que serializa en el 2276 (`0xA95577`). Revisión 8 |

- Se descarta si el id es uno que el servidor dio al cliente del anfitrión (`ledger.known`) **y no** es de los
  secuenciadores propios del usuario 2. Los propios se comprueban primero y **nunca** se descartan.
- **Intención (revisión 7):** también se descartan los 2276/2105 nativos del usuario 2 sobre un secuenciador del
  anfitrión, y es a propósito.
  - El usuario 2 nunca borra ni avanza legítimamente una escena del **anfitrión** en el campo compartido. De forma
    nativa su cliente no tiene ninguna: el 2661 del anfitrión va ToTarget solo a su personaje. Por eso un 2276/2105
    así solo puede venir de la copia en X.
  - Los suyos pasan siempre: se comprueban primero.
  - El descarte es por id, haya o no viaje observador. Los descartados fuera de un viaje observador se cuentan
    aparte: «outside an observer trip» en el resumen.
- Contadores propios (`Req::HostDelete`, `Req::HostDelayEnd`) y su línea `[filter]`, con el id, para los 8 primeros.
- Además se cuentan los que pasan: `own` (propios) y `not the host's` (id desconocido o paquete corto), con una línea
  `[filter] … PASSES` y su id para los 8 primeros de cada opcode. Si con el invitado mirando aparecen 2105 «not the
  host's» con ids cercanos a los del anfitrión, algo no cuadra con el formato comprobado.
- `probe` (`observer_seq_copy=probe`): solo cuenta («WOULD BE DROPPED»).

## 6. Medidas G5 (`guest_seq_watch`, hilo de diagnóstico)

- **Por actor vigilado** (los de `guest_seq_watch` y los enlazados de cada 2661 enrutado):
  - lo de antes (`+0x5E`, `+0xC2`, `+0xCF`, `comp+0x2D8`, `comp+0x2E0`);
  - **`comp+0x3D8`** (1 = `+0xCF` espera a su escena), **`comp+0x320`**, **`a+0xAC`** (padre) y, si el padre está en el
    cliente, su `comp+0x3D8`.
  - Componente = `[[a+0x68]+0xB0]` con la vtable `0x55BEE78` comprobada.
- **Por X** (personaje enrutado):
  - `+0x231`, `+0x174`, `+0x17C`;
  - los tamaños de `+0x1B0`, `+0x1C0` y `+0x1D0`;
  - las escenas vivas (`+0x28`, número en `+0x30`; `0xAA43C7`);
  - la cola de enganches (`+0x190`, número en `+0x194`; `0xAA3F3D`).
  - Se escribe al cambiar (pasada de 5 s) y, con `guest_seq_play` activo, cada 30 s aunque no cambie.
- **Resumen `GuestSeqPlay summary` cada 60 s:**
  - fotogramas, llamadas al tick y a la vida (o «would» en probe);
  - 3 escritos y **cuándo** (hora QPC/captura, hace cuántos segundos, X y número de 2322);
  - por X, «started ~N»: instancias que salieron de `+0x1B0/+0x1C0/+0x1D0` en un tick con X en 3. Cada una se
    reprodujo (`0xA9D860`) o se borró con código (`0xA95530`, el 2276 que descarta el filtro);
  - «live list +0x28 grew N». Ojo, sesión 9 (`p8-s9-npc.md` §2.1): `+0x28` cuenta las instancias, no las escenas que
    se están reproduciendo (valía 17 antes del 3), así que esta cifra no mide la reproducción;
  - motivos de salto con su número, preguntas a vf40 y nulos, ranuras usadas y fallos;
  - de la tabla: llena, vaciadas, escritor ocupado, lecturas inestables, «routing hooks on another thread»
    (esperado 0), «re-entries» (esperado 0) y «re-armed after a native 2» (esperado 0).

## 7. Implementación

| Archivo | Contenido |
|---|---|
| `src/mod/session2/guest_seq_play_logic.h` (nuevo) | `gsp::Mode`, `Facts`, `Skip`, `plan`, `write3_due`, `WriteMark`, `consumed`, `XTable` |
| `src/mod/session2/guest_seq_play.{h,cpp}` (nuevos) | Gancho `0x8B6144`, verificación de bytes, llamadas guardadas, anillo, líneas, resumen, `describe` |
| `src/mod/session2/guest_seq_route_logic.h` | `Point::P2524`; `NpcState` ampliado; `XState` |
| `src/mod/session2/guest_seq_route.{h,cpp}` | Punto 2524 (propio, solo con `_live`); `note_x` hacia la tabla; G5 ampliada; la puerta no toca el 2524 |
| `src/mod/session2/observer_seq_copy_logic.h`, `observer_seq_copy.{h,cpp}` | 2276/2105 en el filtro, contadores, líneas, resumen |
| `src/mod/config.{h,cpp}`, `guest.cpp`, `guest_diag_worker.cpp`, `CMakeLists.txt` | Claves, arranque (play antes que route), tick, descripción, prueba nueva |
| `tests/optd_seq_play_tests.cpp` (nuevo), `tests/optd_seq_copy_tests.cpp` | §9 |
| `docs/research/ini/cdmp.p8-s7-shadow.ini` | `guest_seq_play=1`, `guest_seq_play_live=0` (solo fase 1), claves G5 de la sesión 8 |

## 8. Riesgos

| Id | Riesgo | Mitigación / qué se ve |
|---|---|---|
| R1 | Ráfaga de escenas del Kliff del anfitrión con él como «jugador»: cámara, entrada e interfaz del invitado [E] | Freno nativo: una por llamada (`0xA97503`), unas 22 en 22–60 fotogramas. Criterio de aborto (§10) |
| R2 | El 3 a mano no reproduce [E] (p. ej., recurso no listo → borrado con código 6, `0xA95530` → 2276) | «started» sube y `live list grew` no; el anfitrión cuenta `2276` descartados. Siguiente paso: `0xA97B30` desde este punto (§5 del informe `.xtls`) |
| R3 | X destruido o liberado (el anfitrión se va, se recrea su Kliff, reconexión, cambio de campo) | Ranura de otra conexión o de otro campo: se vacía. En cada fotograma: identidad completa más la comprobación de vida en la tabla de actores del cliente. Ganchos y bucle en el hilo principal [V], y la destrucción de actores del cliente también ocurre en ese hilo [E], así que no puede pasar entre la lectura y la llamada del mismo fotograma |
| R4 | `0xAA3E70(X)` con el FieldAttacher de Q retenido (el de M va después de soltarlo) [E] | El attacher es una referencia (`0x14343A0`), no el cerrojo de X. Si aparece un bloqueo: `guest_seq_play_live=0` |
| R5 | 2105 con otro formato | Descartado: el formato está comprobado (§5). Quedan las líneas `[filter] … 2105 … PASSES … not the host's` y su contador |
| R6 | `0xAA3E70` en X ejecuta también la parte del jugador local y la cola de cámara y capítulo (`0x8B4570` → `0xA96070`/`0xA9A1F0`/`0xA9A410`, `0xAA5040`, `0xAA5400`) **[V estático, §13]** | **`_live=0` en la prueba siguiente.** Para activarla hace falta la guarda de §13.3 |
| R12 | El 3 a mano con tick no filtra la primera ráfaga como lo hace el nativo `0xA97B30` → `.xtls 0x9F8FCE0` | El nativo reproduce en el acto solo lo que cumple misiones del dueño (`0x1AC2830`), tipo 5 o la caja `+0xD8…+0xEC`; el resto se queda en `+0x1C0` para después. Con el 3 y el tick, **todo** `+0x1C0` acaba reproduciéndose con el freno nativo de una por llamada (`p8-xtls-lectura.md` §1.1 y §1.3). Puede arrancar escenas que el nativo habría dejado esperando (p. ej. lejos del jugador). En el campo reescrito, las misiones del dueño del campo son las del invitado, así que esa parte del filtro tampoco sería la del anfitrión [E]. Se verá en «started» y en pantalla |
| R7 | NPC con padre (`+0xAC`) cuyo flag no se limpia (R6 del informe `.xtls`) | G5 muestra el padre y su `+0x3D8` |
| R8 | Coste: un tick (≈20 entradas) y una vida por fotograma | Lo mismo que hace el juego con M |
| R9 | Una excepción dentro del juego | No se traga (I1): línea `GuestSeqPlay: exception … NOT handled by the mod` y el cierre real. Si algo más arriba la maneja, el módulo queda apagado («SWITCHED OFF» en el resumen) |
| R11 | El campo del manejador (`[rbp-N]` antes de su vf48) no es el mismo objeto que Q en `0x8B6060` [E: los dos salen de `0x17FA6C0` sobre un actor del mismo campo] | No se haría ningún tick: el resumen muestra «routed in another field than this frame's (slot cleared)» en cada fotograma, y el campo de la ranura en «slot … (field 0x…)». Si pasa: `guest_seq_play=0` y revisar |
| R10 | Los NPC que fallan el login por `seq_bodyanim` (`0x13`, `0x1B0`, la mesa de pulsos) | Siguen sin arreglarse: su secuenciador se rechaza al crearse (`0xA9CE81`). Es el paso 4 de `p8-s8-escenas.md` §6 |

## 9. Compilación y pruebas [V]

- `.\scripts\build.ps1`: sin avisos y **34/34** (antes 33). Nueva: `optd_seq_play`.
- **`optd_seq_play`:**
  - valores de la clave;
  - `plan`: cada motivo y su orden, la pregunta a vf40 (solo con 1, una vez y nunca para un X muerto), el 3 (cada
    condición), la vida (`_live`, el byte de `0x6CC0218`, X en 3);
  - `effective_mode` (1 rechazado sin `guest_seq_route=1`);
  - motivos nuevos: otra conexión, otro campo (los dos vacían la ranura) y actor desaparecido;
  - `WriteMark` (una vez por X y por 2322; respaldo de re-armado, con tope) y `consumed`;
  - `XTable`: reclamo por clave, X/campo nuevos para la misma clave, `gen` único, llena, reutilización de ranuras de
    otra conexión, `clear`, `knows`, y dos escritores (claves compartidas) con un lector a la vez: sin ranura rota ni
    clave duplicada;
  - tipos de G5.
- **`optd_seq_copy`, ampliada:**
  - filtro 2276/2105: del anfitrión, desconocido, corto y propio (también con id reutilizado), y fuera de viaje
    observador;
  - el 2524 en el mapa de puntos, nunca retenido por la puerta, decidido como los demás y solo hacia un X de la
    tabla (`NotPlayed`).
- **No prueban** el gancho, las llamadas ni el comportamiento del juego: eso es la prueba en Shadow.

## 10. Qué mirar en el log y criterio de aborto

**Invitado** (`cdmp.p8-s7-shadow.ini`), en orden:

Con el ini de la prueba siguiente (`_live=0`) no salen `+ 0xAA3E70(X, dt)`, ni líneas del 2524, ni «live calls».

1. Arranque:
   - `GuestSeqPlay ON (guest_seq_play, live=0): hooked 0x8B6144 (movzx r8d,byte [r8+0x50]; …) | … | guest_seq_route=1`.
     Si sale `REFUSED` o `is not build 2976's .rsrc code`: no hay nada instalado.
   - `GuestSeqRoute ON (guest_seq_route): points: … | REFUSED: none | … | 2524: not routed (guest_seq_play_live=0)`.
     Con `_live=1` diría `… 2524 StageOnBranchEventAck 0xC08178 (own hook) …` y `2524: routed to X like the others …`.
2. Al entrar en el campo del anfitrión:
   - `GuestSeqRoute [2322 PlayAllPreLoadSequencerAck 0xB87A67] … ROUTED …` (como en la sesión 8);
   - luego `GuestSeqPlay [state] … slot 0 X 0x… (character 0xA0100001): this client's own M+0x231 is not 3 yet` (si
     llega antes que la carga propia);
   - y **`GuestSeqPlay [state] … : ticked | X+0x231 2, M+0x231 3, field mode …`**.
3. `GuestSeqPlay [first tick] … calls 0xA953C0(X, Q, mode) | X+0x231 2, +0x1B0 17, +0x1C0 0` (con `_live=1`, además
   `+ 0xAA3E70(X, dt)`).
4. **`GuestSeqPlay [3] … WROTE X+0x231 2 -> 3 (lists +0x1B0 n, +0x1C0 n; M+0x231 3) at frame N`**: una vez.
5. `GuestSeqRoute [G5 X] character 0xA0100001: X 0x… +0x231 3, loading +0x174 0, … lists +0x1B0 0 / +0x1C0 0 / +0x1D0 0,
   live scenes +0x28 N, engage queue +0x190 M`:
   - las listas bajan a 0 en uno o dos segundos;
   - `live scenes` sube.
6. `GuestSeqRoute [G5 actor] 0xB0100143: exists, +0x5E 0x10, +0xC2 1, +0xCF 1, sequencer component: owner 0xA0100001,
   scene +0x2E0 0x19A, +0x320 0x…, +0x3D8 0 …`:
   - **`+0x3D8 0` y después `+0xCF 1`** en Rhett, Dahlia, Grover, Tina y Turnali (claves de la sesión 8 en el ini;
     cambian entre sesiones, los enlazados de cada 2661 enrutado se vigilan solos).
7. Solo con `_live=1`: `GuestSeqRoute [2524 StageOnBranchEventAck 0xC08178] … ROUTED to the actor's own manager X`.
   Con `_live=0` las escenas arrancan pero no avanzan (R4 del informe `.xtls`): se espera la pose inicial de escena, sin
   ramas ni cambios posteriores, y que los NPC que aparezcan después sigan con `+0x3D8 1`.
8. `GuestSeqPlay summary (guest_seq_play=1, live=0): … ticks N, live calls 0, 3 written 1 (would 0, re-armed after a
   native 2 0) … routing hooks on another thread 0, re-entries 0 | slot 0 character 0xA0100001 X 0x… (field 0x…,
   2322 gen N): … 3 written x1 (last … = … s ago …), started ~N …, live list +0x28 grew N`.
9. En la pantalla:
   - Dahlia, posadero y tenderos en su pose de escena;
   - Tina, Turnali y Rhett visibles;
   - los niños de la taberna fuera de la A-pose.
   - Con `guest_s3_diag=1`: `GuestS3 … StartAction … INSTALLED` para esas claves.
   - Con `s1_diag=1`: `P8Probe [BodyAnim 0xA79770]` con claves `0xB…`. Las dos sondas están a 0 en el ini, por el
     volumen de log.

**Anfitrión** (sin cambios de ini; necesita este `cdmp.asi`):

- `ObserverSeqCopy [filter] … user 2's Req 2276 DeleteSequencerReq DROPPED: sequencer 0x… is the host's …` y lo mismo
  con `2105 StageBranchStateDelayEndReq`;
- en el resumen, `C>S dropped: host sequencer N, 2100 N, 2276 N, 2105 N | … | of the 2276/2105 dropped or would be,
  outside an observer trip N/N | 2276 passed: own N, not the host's N | 2105 passed: own N, not the host's N`.

**Abortar** (`guest_seq_play=0`, que deja todo como en la sesión 8; `_live` ya está a 0):

- cámara del invitado que salta a una escena, se queda fija o la toma el Kliff del anfitrión;
- pérdida de control o entrada bloqueada;
- interfaz de cinemática o diálogo que no es suya;
- cualquier cierre, o `GuestSeqPlay: exception … NOT handled by the mod`;
- `GuestSeqPlay summary … SWITCHED OFF`;
- `[vf40] … returned NULL` repetido;
- «routing hooks on another thread» o «re-entries» distintos de 0 en el resumen: hipótesis de hilo rota (§2.2).
  Apagar y revisar;
- en el anfitrión, `2105 passed: not the host's` creciendo mientras el invitado observa, o escenas propias del
  anfitrión que desaparecen (2810 sin su 2516).

## 11. Dudas abiertas

- R11: que el campo del manejador y Q sean el mismo objeto es [E]. El formato del 2105, que antes era una duda, ya
  está comprobado (§5).
- Que `0xA9D860` añada la escena a `G+0x28` es [E]: la cifra «started» no depende de ello, la de «live list grew» sí.
- La parte del usuario local de `0xAA3E70` sobre X (R6) no se leyó.
- Si `0x8B53C0` («no listo») hace que el 2524 del Kliff del anfitrión vuelva con «reintentar» antes de llegar a
  `0xA9E480` (`0xC07E4F`), el 2524 no se enruta nunca. Se verá como `ROUTED` ausente aunque lleguen 2524.

## 12. Revisión de `60b226a` (mismo día)

| # | Hallazgo | Resultado |
|---|---|---|
| 1 | X obsoleto: la tabla no se vaciaba nunca | **Corregido:** la ranura guarda la conexión y el campo, y se vacía si no son los del fotograma. Además, en cada fotograma, la comprobación de vida en la tabla de actores del cliente (clave → mismo actor, que sigue siendo dueño de X) antes de cualquier llamada. Pruebas |
| 2 | Sin protección de reentrada | **Corregido:** `thread_local`, contado («re-entries») |
| 3 | `note_x` también en probe; `1` sin el enrutado | **Corregido:** solo `Verdict::Route`. `guest_seq_play=1` sin `guest_seq_route=1` se rechaza (ERR, apagado). Texto del aviso corregido. Prueba |
| 4 | El 2 nativo podría pisar el 3 si el 2322 no va en el hilo principal | **Comprobado [V estático]:** los Ack corren en `0xAD2380` → `0x35D0C0`, en el mismo hilo y antes que `0x8B6060` (§2.2). Aun así, respaldo: re-armado si X vuelve a 2 con el mismo `gen` (tope 3, contado). Prueba |
| 5 | Fallo con `[X+8]` bloqueado | **Comprobado [V]:** ningún marco de la cadena `0x8B6060` → `WinApp::vf9` tiene manejador que capture (solo `UHANDLER`). Documentado (§2.2 y código) |
| 6 | `note`/`get` no atómicos entre campos | **Corregido:** escritura con try-lock (escritor único, deduplicado por clave) y lectura por secuencia. Prueba con dos escritores y un lector |
| 7 | El filtro también tira los 2276/2105 nativos | **Se mantiene** a propósito (§5). Documentado y contado aparte fuera de viaje observador |
| 8 | Formato del 2105 | **Comprobado [V]:** `0x1F6C190` por id de secuenciador, la misma búsqueda que el 2810 y el 2276 (§5). Las líneas `[filter]` siguen dando los ids |
| 9 | Citar la lectura de `.xtls` del paso a 3 | **Hecho** en el código (`guest_seq_play_logic.h`, `on_frame_body`) y en §2.2 |
| 10 | Atar cada X al campo en que se enrutó | **Hecho:** el campo de la vf48 del manejador (`kFieldSlot`); otro Q vacía la ranura. R11 |
| 11 | 2524 a un X fuera de la tabla | **Corregido:** solo hacia un X de la tabla en esta conexión (`NotPlayed`). Prueba |
| 12 | Orden del tick de X frente al de M | **Justificado** (§2.2): después de `0x8B6150` ya no están RAX ni R8, y `0x8B6156` tiene operando RIP. X y M no comparten nada que dependa del orden |

## 13. `0xAA3E70` no es pasiva: por qué la fase 2 queda apagada

Un análisis independiente (`INFORME-RAIZ-OBSERVADOR.txt` §5.3) lo señaló; aquí se comprueba con capstone. El código
de la fase 2 se queda, y la prueba siguiente va con **`guest_seq_play_live=0`**.

### 13.1 Lo que hace, además de avanzar las escenas vivas [V estático]

`0xAA3E70(G, dt)` (RUNTIME_FUNCTION `0xAA3E70–0xAA4D06`; `r13 = G`, `xmm6 = dt`):

1. **Cola de enganches y escenas vivas** (§3): `.xtls 0x8F80300`, y en las esperas de rama `stage+0x28A = 1` y
   `0xAA4907 call 0xC037D0` → `.xtls 0xA4D1A10`, que **envía 2105**. El anfitrión lo descarta (§5).
2. **Temporizadores de escena:** `0xAA4A2E call 0xAAF750` sobre `[[0x6D691E0]+0x118]` (lista global `+0x198`).
3. **Jugador local.**
   - `0xAA4B36 call 0x8B4570` con `rcx = [[0x6D691E0]+0x30]` (el gestor de actores del cliente). `0x8B4570` lee
     `[mgr+0x58]` y `[[mgr+0x58]+0xD8]`, es decir, el actor del jugador de este cliente, sea cual sea G.
   - Con él, `0xAA4B6C call 0xA96070(G, local)`: busca por id en G (`0x1F6C190`) y llama a `0x5DAC90`.
   - Según su resultado, `0xA9A1F0(G)` (`0xAA4C73`):
     - escribe `G+0x2F0 = -1.0f` y `G+0x2E0`;
     - busca la escena por id (`0x1F6C190`);
     - llama a vf98/vfA0 de lo encontrado, a `0x8AE1A0` y `0x17B54D0`, y a la vf188 de un actor.
   - Y `0xA9A410(G)` (`0xAA4C7F`):
     - escribe `G+0x2E8`;
     - llama a `0x22CC0E0`, `0x35DE50` y `0xAAFC00`.
4. **Cola final, siempre** (salvo que falle el FieldAttacher de la entrada, `0xAA3F2B`): `0xAA4C92 call 0xAA5040(G, dt)`
   y `0xAA4C9E call 0xAA5400(G, dt)`.
   - **`0xAA5040`**:
     - con `G+0x2A8` ≠ 0 y el temporizador `G+0x2B0`, interpola un valor con `0x41F8A0` y `0x3457100`;
     - con `G+0x288`, el bloque `_blendRatio` (`0xAA51D0`): `G+0x290/+0x294/+0x298`, `0x1325800`, `0x34588E0`,
       `0x34B4350`;
     - con **`G+0x2A4` ≠ 0**, escribe **`[[[0x6D691E0]+0xA8]+0x208] = 1.0f`** (`0xAA539A`) y pone `G+0x2A4 = 0`.
       Es un valor global del mundo del cliente, no de G.
   - **`0xAA5400`**:
     - el temporizador `G+0x2CC` (y `G+0x2D0`, `G+0x2D4`);
     - al vencer, el bloque **`_chapterTransition`** (`0xAA54BE`): lee `[[[0x6D691E0]+0xA8]+0x1040]+0x38`, escribe
       `[x+0x168] = 0` y llama a `0x4324A0`, `0xA9C9A0`, **`0x63F200`** y `0x3882380`.
   - Las dos dependen de campos **de G** (`+0x288`, `+0x2A4`, `+0x2A8`, `+0x2CC`, `+0x2D4`), pero escriben en
     **globales del cliente**.

### 13.2 Qué significa para X [E]

- Lo que hacen las partes 3 y 4 está escrito para el gestor del jugador local (M): su escena asignada (`+0x2E0`,
  `+0x2E8`, `+0x2F0`), la mezcla de cámara o tiempo (`_blendRatio`, `+0x208 = 1.0`) y la transición de capítulo.
- Con X, el «jugador» de las escenas es el Kliff del anfitrión, pero `0x8B4570` devuelve el jugador **del invitado**.
- Si una escena de X activa `+0x2A4`, `+0x288`, `+0x2A8` o `+0x2CC`, el invitado tendría la mezcla de cámara o la
  transición de capítulo de una escena ajena. Es justo el criterio de aborto (cámara, control, interfaz).
- No se ha probado que X active esas ramas en la sesión 8. Filtrar el 2105 no basta: los efectos son locales.

### 13.3 Qué haría falta para llamarla con seguridad para un gestor remoto

`0xAA3E70` no tiene otra entrada que haga solo la vida de las escenas: los pasos 1 y 2 están en línea, dentro de la
misma función (`0xAA3F31–0xAA4B27`). Opciones, de menos a más invasiva:

1. **Guarda de solo lectura antes de cada llamada.** Llamar a `0xAA3E70(X)` solo si las ramas locales de la cola no
   pueden hacer nada en ese fotograma: `X+0x2A4 == 0`, `X+0x288 == 0`, `X+0x2A8 == 0` (desde la sesión 9: o su
   temporizador `+0x2B0 < 0`, §15), `X+0x2CC <= 0`, `X+0x2D4 == 0`.
   - Falta leer qué decide `0xA96070` sobre X y si `0xA9A1F0`/`0xA9A410` pueden ejecutarse con X (sus condiciones
     `esi`/`edi` en `0xAA4C6C`/`0xAA4C78`, que salen de la parte del jugador local).
   - Sin eso, la guarda no cubre el paso 3.
2. **Gancho dentro de `0xAA3E70`** en `0xAA4B27` (fin del bucle de escenas vivas, antes de `0x8B4570`). Cuando la
   llamada es del mod (un `thread_local` «llamada para X»), salta a la limpieza del final (`0xAA4CA4`, tras
   `0xAA5400`).
   - Hay que verificar que entre `0xAA4B27` y `0xAA4CA4` no queda nada que liberar. El cerrojo de `0xAA4B60` se toma
     después de `0xAA4B27` [V]; falta confirmar que el FieldAttacher de la entrada (`[rbp-0x78]`) y las demás
     referencias se sueltan en la cola común, después de `0xAA4CA4`.
   - Es un cambio de flujo en `.rsrc`: necesita su propia verificación de bytes y de pila.
3. **Reimplementar solo el bucle de escenas vivas** con las funciones que llama (`0x600730`, `0x5DBEA0`, `0x5DC5B0`,
   `0x5F1D30`, `0x5DBA30`, `0x5DE800`, `0x5DEA00`, `0x5DD3B0`, `0x5DE4C0`; §3.3 del informe `.xtls`), bajo el
   cerrojo de `[X+8]`, igual que la función original. Es lo más limpio de efectos, pero también lo más largo.

Recomendación: la opción 2, después de leer `0xAA4B27–0xAA4CA4` entero. Mientras tanto, `_live=0`.

**Implementado (opción 2, con la 1 como compuerta adicional; §13.5).**

### 13.4 La primera ráfaga (R12)

`0xA97B30` → `.xtls 0x9F8FCE0` filtra la ráfaga inicial:

- reproduce en el acto lo que cumple las misiones del dueño del campo (`0x1AC2830`), el tipo 5 (`+0x61`) o la caja
  `+0xD8…+0xEC` con la posición del jugador;
- el resto se queda en `+0x1C0` hasta la actualización con freno.

Nuestro 3 con tick no hace esa primera selección: **todo** lo precargado se reproduce, una instancia por llamada
(`p8-xtls-lectura.md` §1.3). Es más gradual, pero no es el mismo conjunto. Si en la prueba arrancan escenas lejanas o
fuera de lugar, la alternativa es la de §5 de aquel informe: llamar a `0xA97B30(X, pos)` una vez desde `0x8B6144`, con
`pos` = la posición del jugador local, en lugar de escribir el 3.

### 13.5 El gancho de salto y la compuerta [V estático]

Lectura completa de `0xAA4B27–0xAA4CB2`, y de la cola hasta `0xAA4D05`:

- **Lo que se construye dentro del tramo, y dónde se suelta:**
  - `[rbp+0x20]`: lo escribe por primera vez `0x8B4570` en `0xAA4B36` (el asa del jugador local); ninguna otra
    instrucción de la función lo usa antes. Se suelta en `0xAA4CA4–0xAA4CAC`.
  - El cerrojo de `[G+8]` (`0xAA4B60`): se toma y se suelta dentro del tramo, antes de `0xAA4C8B`.
  - En el punto no hay ningún cerrojo tomado. La guarda de `0xAA43B5` se soltó en `0xAA43ED`.
- **Por eso se reanuda en `0xAA4CB2`, no en `0xAA4CA4`:** desde `0xAA4CA4` se soltaría un asa que nunca se construyó.
- **Lo que necesita `0xAA4CB2` y ya existe en el punto:**
  - la copia de la lista de escenas vivas `[rsp+0x38]`, iniciada en `0xAA4399` y copiada en `0xAA43CB`; la destruye
    `0x3D3000`;
  - el FieldAttacher de la entrada `[rbp-0x78]`;
  - `rsi = exe+0x5725E48`, la vtable base que `0xAA4CD2` guarda. Se carga en `0xAA3EF3`, `0xAA4AE6` y `0xAA4B13` en
    todos los caminos que llegan al punto (el camino nativo de `0xAA4B40` a `0xAA4C8B` también lo da por hecho).
  - `rsp` es el mismo en las dos direcciones: todas las llamadas intermedias están equilibradas.
- **Saltos al punto:** solo `0xAA4400 je 0xAA4B27` (lista vacía), y ninguno a `0xAA4B28–0xAA4B31`.
- **`r13` = G en el punto:** se recarga de `[rbp+0x490]`, la copia de `rcx` de la entrada, en `0xAA4389`, y no se
  escribe en el bucle.

Implementación (`guest_seq_play.cpp`):

- **Segundo gancho intermedio en `0xAA4B27`:**
  - parchea 11 bytes: `48 8D 55 20` + `48 8B 0D AE 46 2C 06` (el `mov` relativo a RIP lo reubica safetyhook);
  - verifica 20 bytes ahí, y además `0xAA4CA4` (24), `0xAA43C7` (14), `0xAA4400`, `0xAA3EA2` y los tres `lea rsi`;
  - solo se instala con `_live=1`.
- **El salto solo actúa en nuestra llamada:** un `thread_local` con X se pone justo antes de `0xAA3E70(X, dt)` y se
  quita después (también en el filtro de fallos).
  - Si está puesto y `r13 == X`: `rsi := exe+0x5725E48` (contado si no lo era) y `rip := 0xAA4CB2`.
  - En la llamada nativa de M no hace nada.
- **Compuerta de lectura antes de llamar:** `X+0x2A4 == 0`, `X+0x288 == 0`, `X+0x2A8 == 0` **o** `X+0x2B0 < 0`
  (desde la sesión 9, §15), `X+0x2D4 == 0` y
  `X+0x2CC <= 0`. Si una lectura falla, cuenta como no reposo.
  - Sin gancho no hay ninguna llamada a la vida de X (`gsp::plan`).
  - El 2524 tampoco se enruta sin gancho (`guest_seq_play_routes_2524`).
- **Línea de resumen nueva (cada 60 s):** `GuestSeqPlay live (phase 2): skip hook in | 0xAA3E70(X) calls N:
  local-player part skipped N (rsi set 0), NOT skipped 0 | held back: X's +0x2A4/+0x288/+0x2A8/+0x2D4/+0x2CC not idle
  N, no skip hook 0`. El arranque añade `live skip hook: hooked 0xAA4B27 -> 0xAA4CB2 for our calls only`.
- **Qué deja de hacer `0xAA3E70(X)`:** la parte del jugador local, `0xAA5040` y `0xAA5400`. Hace la cola de
  enganches, las escenas vivas (con su 2105, que se descarta en el anfitrión) y los temporizadores de escena.
- **Prueba (`optd_seq_play`, `test_live_gate`):** cada campo de la compuerta, sin gancho, no «debido» frente a
  «retenido», y probe.
- **Perfiles:**
  - `cdmp.p8-s7-shadow.ini` sigue con `_live=0` (primer paso);
  - `cdmp.p8-s7-shadow-fase2.ini` es idéntico salvo `guest_seq_play_live=1` y un comentario arriba (segundo paso de
    la misma sesión).
- **Abortar la fase 2:** «NOT skipped» o «rsi set» distintos de 0, «skip hook MISSING», o cámara, control o interfaz
  raros. Se vuelve al primer ini.

### 13.6 Evidencia del tramo saltado (capstone sobre el PE, revisión de `0383f70`)

Se volvió a desensamblar `0xAA3E70–0xAA4D06` (RUNTIME_FUNCTION de `0xAA3E70`, sin trozos encadenados) con
`observador/scripts/pedis.py`. Extractos literales:

_[Listado de desensamblado del juego retirado de la versión pública (104 líneas); la RVA y la conclusión están en el texto.]_

Lectura de cada punto que pidió la revisión:

- **Equilibrio de `rsp` [V]:**
  - (a) y (b): `rsp` solo cambia en el prólogo (`push ×7`, `sub rsp,0x550`) y en el epílogo (`mov rsp,r11` desde
    `rsp+0x550`, `pop ×7`). Entre `0xAA3E94` y `0xAA4CDF` no hay ningún `push`, `pop` ni escritura de `rsp`.
  - Por tanto, `rsp` en `0xAA4B27` es el mismo que en `0xAA4CB2`, y `[rsp+0x38]` es el mismo objeto.
- **`[rbp+0x20..0x37]` [V]:** (f): los únicos usos son el asa de `0x8B4570` (`0xAA4B27`, `0xAA4B3C`, `0xAA4B46`) y su
  liberación (`0xAA4CA4`, `0xAA4CA8`), que el salto se salta. Después de `0xAA4CB2` no hay ninguno.
- **Saltos al tramo `0xAA4B28–0xAA4B31` [V]:** (d): ninguno.
  - Hacia el tramo de la lista viva solo entran `0xAA3F93 → 0xAA4395` y `0xAA3F9F → 0xAA4399`. Los dos están antes de
    la copia de `[rsp+0x38]` (`0xAA4399`/`0xAA43CB`), así que la copia existe en todos los caminos que llegan al punto.
  - `0xAA4BEB jmp 0xAA4CAC` viene de la salida del bucle por `0xAA4535 → 0xAA4BCE`, que no pasa por el punto.
- **`r13` [V]:** (e): `0xAA3FEE` y `0xAA4029` cambian `r13` dentro del bucle de la cola de enganches, que vuelve a
  `0xAA3F90` tras recargarlo en `0xAA4389` (`mov r13,[rbp+0x490]`, la copia de `rcx` de la entrada, `0xAA3E77`). Los
  dos caminos a la lista viva salen de `0xAA3F93`/`0xAA3F9F`, con `r13 = G`. En el bucle de la lista viva
  (`0xAA4410–0xAA4B21`) no se escribe `r13`.
  - `[rbp+0x490]` sí se reutiliza después (guarda en `0xAA489A`), pero el gancho lee `r13`, no ese hueco.
- **FieldAttacher `[rbp-0x78]` [V]:** (c): se construye en `0xAA3EFA–0xAA3F23`. Todas las ramas anteriores
  (`0xAA3EBB`, `0xAA3EDF`, `0xAA3EEB`, `0xAA3EEF`) caen antes de `0xAA3EF3`. Si no se obtiene (`0xAA3F2B je
  0xAA4CCA`), la función va directa a su liberación, sin pasar por el bucle ni por el punto. Su liberación
  (`0xAA4CCA–0xAA4CDA`) está después de `0xAA4CB2`.
- **Cerrojos [V]:** (g): las tres guardas compartidas que se toman antes del punto se sueltan antes de salir de su
  bloque:
  - `0xAA43B5 → 0xAA43ED`: lineal, sin saltos en medio;
  - `0xAA456F → 0xAA45B1`: lineal;
  - `0xAA48A1 → 0xAA48CB` o `0xAA48DF`: las dos ramas de `0xAA48BD` la sueltan.
  - En `0xAA4B27` no hay ninguna tomada. La de `[G+8]` de `0xAA4B60` está dentro del tramo saltado y se suelta dentro
    de él.
- **Llamada anidada [V, grafo de llamadas directas]:**
  - el único llamador de `0xAA3E70` es `0x8B26B7` (en `0x8B2660`);
  - los únicos de `0x8B2660` son `0x8B63DE`/`0x8B67F3` (en `0x8B6060`);
  - no hay punteros a ninguna de las dos (barrido de `E8`/`E9` en `.rsrc` y de punteros en el PE).
  - Dentro de nuestra llamada no puede correr un `0xAA3E70` para otro gestor. Aun así, si el gancho viera
    `r13 ≠ X`, deja esa llamada nativa, apaga la fase 2 para toda la sesión y escribe una línea ERR `GuestSeqPlay [live
    OFF]`.

### 13.7 Quién escribe los campos de la compuerta [V parcial]

Barrido de escrituras con esos desplazamientos en las funciones de `0x770000–0x780000`, `0xA90000–0xAB8000`,
`0xB80000–0xB90000` y `0xC00000–0xC12000`. No es todo `.rsrc`; muchas escrituras son de otras estructuras.

| Campo de G | Lo pone | Lo limpia o lo descuenta |
|---|---|---|
| `+0x2CC` (temporizador del capítulo) | `0xA9A07D`, en la **vf7** `0xA99AD0` (vtable `+0x38`; también llamada desde `0xA9F675`) | **solo** `0xAA546F`, en `0xAA5400` (la resta de dt) |
| `+0x2D4` | `0xA9A06E` (vf7, `= 1`) | **solo** `0xA94A9B`, en la **vf1** `0xA94660` (vtable `+8`, reinicio) |
| `+0x2A4` | `0xA995D5`/`0xA995E2` (`0xA99430`) | `0xAA53A4` (`0xAA5040`) y `0xA94A72` (vf1) |
| `+0x288`, `+0x2A8` | `0xA9C841` (`+0x2A8`); otras escrituras en `0xAA9EF0`/`0xAADF60` son de otra estructura [E] | `0xA94A02` (vf1) |

- **Consecuencia:**
  - si una escena de X arranca una transición de capítulo (vf7) o una mezcla (`+0x2A4`), el valor se queda: quien lo
    limpia (`0xAA5040`/`0xAA5400`) se salta para X, y la vf1 es el reinicio del gestor.
  - La compuerta retiene entonces la vida de X **para siempre**. **No se fuerza.**
  - A los 10 s seguidos de retención se escribe una línea por episodio, `GuestSeqPlay [live held] … has been held
    N s: X+0x2A4 …, +0x288 …, +0x2A8 …, +0x2D4 …, +0x2CC …`, y se cuenta («held > 10 s (stuck, not forced)» en el
    resumen de la fase 2).
- Si pasa en la prueba, la vida de esas escenas queda congelada: el tick y el 3 siguen.
- Arreglarlo exigiría decidir qué parte de `0xAA5040`/`0xAA5400` es del gestor y cuál del cliente. Fuera de este
  cambio.

### 13.8 Cambios de la revisión de `0383f70`

| # | Cambio |
|---|---|
| 1 | `t_live_x` con RAII (`LiveScope`) alrededor de la llamada; el filtro SEH lo limpia también |
| 2 | Los cinco campos se vuelven a leer justo antes de `0xAA3E70(X)`, después del tick. Si no están en reposo, se retiene (contado: «right after the tick») |
| 3 | `r13 ≠ X` en nuestra llamada apaga la fase 2 para toda la sesión (una línea ERR, «LATCHED OFF» en el resumen; ni vida ni 2524). §13.6: no puede ocurrir según el grafo de llamadas |
| 4 | `+0x2CC <= 0.0f`: un NaN ya no cuenta como reposo (`/fp:fast` no está activado). Prueba con NaN y ±inf |
| 5 | §13.7 y `gsp::GateWatch`: retención de más de 10 s, una línea por episodio y su contador; no se fuerza. Prueba |

## 15. Sesión 9: correcciones (`p8-s9-npc.md`, `p8-s9-cinematica.md`)

### 15.1 Compuerta de la fase 2: `+0x2A8` [V]

- En la sesión 9 la fase 2 no corrió nunca: `0xAA3E70(X) calls 0` y `not idle 26424`. El único campo que no estaba en
  reposo era `X+0x2A8`.
- `+0x2A8` no es una petición, es un asa de objeto que vive con el gestor:
  - la crea `0xA9C6B0` (`0xA9C841 mov [rsi+0x2A8],rax`);
  - solo la limpia la vf1 (`0xA94A02`).
- `0xAA5040` solo trabaja con ella mientras el float `+0x2B0 >= 0` [V]:
  - `0xAA5081 vmovss xmm0,[rbx+0x2B0] ; vcomiss xmm0,xmm7(0) ; jb` salta el bloque si `< 0` o si es NaN;
  - `0xAA50EB` le resta dt;
  - `+0x2B4` es el valor objetivo de la interpolación (float, `0xAA50B1`).
- **Condición nueva:** `+0x2A8 == 0 || +0x2B0 < 0.0f`.
  - Un NaN cuenta como no reposo, el lado seguro (el juego, con NaN, se salta el bloque).
  - `+0x2A4`, `+0x288`, `+0x2D4` y `+0x2CC` no cambian: esos sí son peticiones que solo limpia el código saltado
    (§13.7).
- **`X+0x2A8` no se escribe nunca:** soltaría la referencia del gestor a su objeto sin liberarlo.
- **La línea `[live held]`** añade `+0x2B0` y `+0x2B4`, y `+0x2A8/+0x2B0/+0x2B4` de M para comparar.
- Si `+0x2B0 >= 0` se quedara fijo en X (solo lo descuenta `0xAA5040`, que se salta para X), se seguiría reteniendo.
  Se vería en esa misma línea, y la salida sería quitar `+0x2A8` de la compuerta (`p8-s9-npc.md` §5.3).
- Pruebas: `optd_seq_play` (`test_live_gate`), con el asa de GL2, temporizador `< 0`, `= 0`, `> 0`, NaN, y sin asa.

### 15.2 El 2524 [V]

- Payload real (GC2 t=61,409): `04 | 01 00 10 A0 | 64 01 00 00 00 00 00 00 | 01 00 00 00 00`, es decir
  `u8 4 | u32 actor @1 | u64 secuenciador @5 | u32 @13`.
- El gancho leía `@0`/`@4`. Ahora usa `gsr::kBranchActorAt`/`kBranchSeqAt`, las mismas constantes que el 2981.
- §1.3 corregido. Prueba con ese payload (`gsr::branch_ids`) en `optd_seq_copy`.

### 15.3 Filtro del anfitrión: 2888 [V]

- `ChangeSequencerPivotReq` 2888 lleva `u64 secuenciador @0 | 16 bytes` (24 en total).
- En la sesión 9 el invitado lo mandó sobre `0x163` del anfitrión a ~90 Hz, desde 1 s después del 3.
- Ahora se descarta si el id es del anfitrión, y nunca si es de un secuenciador propio del usuario 2: `Req::HostPivot`,
  con su propio contador (`2888 N` en el resumen y en el `describe`) y su línea `[filter] … ChangeSequencerPivotReq`.
- 2591 y 2436 ya estaban en la lista (`Req::HostSequencer`); hay prueba.

### 15.4 Escenas que recargan el campo: lista de exclusión `guest_seq_route_deny` [V el fallo, E la regla]

- **El fallo** (`p8-s9-cinematica.md`): el 2981 del anfitrión con la rama 2 del secuenciador `0x213` (liberación del
  campamento, recurso `0x8CD771B1`) se enrutó a X.
  - Arrancó la máquina de cambio de campo de **este** cliente: el estado `0x18 → 0x01`, después 2749 y 2437.
  - El servidor sacó al usuario 2 y el cliente volvió al título.
- **Bandera fiable en el 2661: no la hay [V].** En la captura de la sesión 9, los 86 bytes de datos de reproducción de
  `0x213` tienen la misma forma y las mismas constantes que los de escenas inofensivas (`0x13`, `0x19`, `0x73`,
  `0x147`, `0x16B`…): tipo `01 00`, posición a cero, `ff ff ff 7f 01`, `ff ×8`, `0d`.
  - Los únicos campos que cambian son la etapa `@19` (`0xF4568`) y el recurso `@27`. Ninguno dice «recarga».
- **Regla [E]:** una lista de recursos, `guest_seq_route_deny` (hex; por defecto `8CD771B1`; ya está en los dos inis
  de Shadow).
  - **2661** con un recurso de la lista: `Verdict::Denied` y rechazo (`kind := 0`). No se crea en X ni en M, igual que
    antes de la opción A. Así la escena tampoco puede llevarse la cámara del invitado, que es el riesgo R3 de la
    cinemática.
  - **2981 / 2524** de ese secuenciador: el gancho guarda el recurso de cada 2661 que llega (anillo de 256, solo en el
    hilo principal). Se quedan nativos, en M, donde no existe el id: sin efecto, como antes de la opción A.
  - La regla se aplica a cualquier secuenciador, no solo a los creados después de la instantánea de entrada. Uno de la
    instantánea con ese recurso sería igual de peligroso.
- **Para ampliar la lista:** cada 2981 de estado ≥ 2 que entra en X tiene su propia línea, con el recurso de su
  secuenciador:
  `GuestSeqRoute [2981 state N into X] … sequencer 0x… of character 0x… | resource 0x… | … add this resource to
  guest_seq_route_deny`.
  - Cuenta aparte en el resumen («2981 of state >= 2 put into X N»).
  - Cada línea de ruta lleva también `| resource 0x…`.
  - Si una de esas líneas precede a una carga, o a un cambio de cámara o de control, ese recurso va a la lista.
- **Limitaciones:**
  - es una lista, no la causa: otra cinemática con otro recurso puede repetir el fallo en su primera aparición;
  - la defensa de raíz es el veto del escritor de la recarga (A1 de `p8-s9-cinematica.md`), que busca otro agente.
- Pruebas (`optd_seq_copy`):
  - el payload real del 2661 de `0x213` (`gsr::prepare_ids`: recurso `0x8CD771B1`);
  - el análisis de la lista;
  - el anillo de recursos;
  - el veredicto: un 2661 denegado se rechaza, y un 2981/2524 denegado no se rechaza (sigue nativo); en probe solo se
    decide; el personaje propio nunca se juzga.

### 15.5 Inis

- `guest_seq_watch` con las claves de la sesión 9 en los dos inis de Shadow:
  - Rhett `B0100156`, Turnali `B0100BE6`, Tina `B0100BE8`, posadero `B0100A49`, Grover `B0100A4F`, Dahlia
    `B0100A50`;
  - más la mesa del posadero (`0x1A4`): Pedro `B0100A47`, mesa `B0100A48`, Freesword `B0100A4A`.
- `guest_seq_route_deny=8CD771B1` en los dos.
- `cdmp.p8-s7-shadow-fase2.ini` sigue siendo una copia exacta de `cdmp.p8-s7-shadow.ini` salvo
  `guest_seq_play_live=1` y su cabecera (comprobado con diff).

## 16. Sesión 10: cierre en `0xA9E63B` con la fase 2 (2524 enrutado a X durante la carga)

### 16.1 Evidencia [V]

- **Log del invitado** (`backups/p8-sesion10-shadow/cdmp.log`):
  - 17:57:20,655: `GuestSeqPlay [state] … this client's own M+0x231 is not 3 yet | X+0x231 2, M+0x231 2`;
  - 17:57:23,809: `exception 0xC0000005 at exe+0xA9E63B (read of 0x8)`, hilo principal, `rax 0`,
    `rbx = rsi = r14 0x3598F717300` (la instancia), `r13 0x359A9A17380`, `r12 0x1`.
- **Captura** (`cdmp_packets_20261010-175552.log`):
  - t=62,921: la instantánea entra en X (2661 de `0x164…0x19D` y 2322 del anfitrión);
  - t=67,308: tres 2524 (`04 | A0100001 | 0x164 / 0x165 / 0x167 | rama 1`);
  - t=67,356: fin de la captura.
  - Desde `f1f2991` (desplazamientos del 2524 corregidos) esos 2524 se enrutan a X: X está en la tabla porque sus
    2661 se enrutaron.
- **Código de `0xA9E480`** (capstone):
  - toma el cerrojo de `[G+8]` y busca la instancia por id con `0x1F6C190`;
  - si la encuentra **no comprueba la escena**: `0xA9E62E r13 = [rsi+0x58]`, `0xA9E637 rax = [r13+0x38]`,
    `0xA9E63B rdx = [rax+8]` (**aquí**: `rax = 0`), `0xA9E63F [rdx + r12*8]`;
  - `r12` es el u32 `@13` del 2524 (la rama, 1).
- **Causa:** la instancia de `0x164` ya existía en X (creada por el 2661 de t=62,9), pero su escena aún no estaba
  cargada (`[[inst+0x58]+0x38] = 0`).
  - El invitado y X seguían en la carga (`M+0x231 2`, `X+0x231 2`).
  - De forma nativa un 2524 solo llega para una escena que ya se reproduce. Al enrutarlo a X durante la carga se
    rompió esa suposición.
  - En el juego el campo sí puede valer 0: otras funciones lo comprueban (`0x5F5008 test rdx,rdx`).

### 16.2 Arreglo

- **El 2524 solo va a X si `0xA9E480` puede correr allí** (`gsr::ready_2524`). Lecturas SEH en el gancho; si algo
  falla, se queda nativo (M, que no tiene el id: sin efecto):
  1. `X+0x231 == 3`, `X+0x174 == 0` y `M+0x231 == 3` (fuera de la carga): si no, `NotPlaying2524`;
  2. la instancia del secuenciador está en el mapa de X y no está muerta. Es la misma búsqueda que `0x1F6C190`, de
     solo lectura: nodos `[X+0x120]` × `[X+0x10C]`, nodo `+8` = id, `+0x10` = instancia, `+0x15` = muerta. Si no,
     `NoInstance2524`;
  3. `stage = [inst+0x58]`, `data = [stage+0x38]` y `[data+8]` no nulos: si no, `NotLoaded2524` (la lectura que falló);
  4. la rama `< 256` y su entrada `[[data+8] + rama*8]` no nula: si no, `BadBranch2524`. La tabla no guarda su
     tamaño al lado: el juego confía en el índice.
- Las líneas de ruta del 2524 dicen el motivo, y el resumen lo cuenta por veredicto.
- Pruebas (`optd_seq_copy`): el caso del cierre (los dos en 2 y la escena a 0), cada condición, el orden con
  `NotPlayed`, y que los demás puntos no lo miran.

### 16.3 Los demás manejadores enrutados [V código, E en lo que va a `.xtls`]

| Punto | Consumidor | ¿Puede leer la escena sin cargar? |
|---|---|---|
| 2661 | `0xA9CA40` | Crea la instancia: es el camino de carga, que ya se recorre en cada instantánea (sesiones 8–10) |
| 2981 | `0xA9E100` | Busca la instancia (`0x1F6C190`) y comprueba que no sea nula (`0xA9E1C5`, `0xA9E34A`). Pasa `[inst+0x58]` a `.xtls 0x8F1FAA0` (`0x5E1BB0`), que no se puede leer aquí. Pero de forma nativa el 2981 llega **antes** o justo después de su 2661 (el orden nativo: §3 de `p8-impl-opcion-a.md`), con la escena sin cargar, y el cliente del anfitrión no se cierra. En las sesiones 8 y 9 se enrutaron 74–186 a X, también durante la carga, sin cierres [E] |
| 2810 | `0xA9D950` | Busca y borra por id; sin instancia, no hace nada |
| 2322 | `0xB87A67` | Solo escribe un byte en X |

Solo el 2524 da por hecha una escena en marcha. El 2981 en estado ≥ 2 sigue con su propia línea y con la lista
`guest_seq_route_deny` (§15.4).

### 16.4 Revisión de `b728ec7`

1. **El tamaño de la tabla de ramas [V lo que hay, E la cabecera]:**
   - Ningún código del juego acota el índice: `0xA9E63F`, `0xA9953C` y `0xAA1B4D` solo descartan −1. Tampoco hay en
     `.rsrc` un recorrido con contador de `[[stage+0x38]+8]`.
   - Se toma como un vector del motor, `{ptr @+8, u32 count @+0x10, u32 capacidad @+0x14}` (la forma de las listas
     del gestor, p. ej. `+0x1B0`/`+0x1B8`, y de `[x+0x90]` en `0x5F5008`).
   - Se exige: `count > 0`, `count <= capacidad <= 4096` y `rama < count`. Si la cabecera no tiene esa forma, se
     rechaza (`BadBranch2524`). La línea de comprobación escribe `count` y `capacidad`, para confirmarlo en la próxima
     sesión.
2. **Carrera (lecturas sin cerrojo) → la comprobación va dentro de `0xA9E480`:**
   - **Gancho en `0xA9E55B`** (`mov r15d,0x1FD`, 6 bytes, una instrucción, sin saltos a `0xA9E55C–0xA9E565`):
     - va justo después de la búsqueda nativa, con el cerrojo de `[G+8]` tomado (`0xA9E4FA`), y antes de
       `test rdi,rdi`;
     - solo actúa en la llamada que el gancho de enrutado mandó a X (un `thread_local` con X, comparado con
       `G = [rbp+0x1860]`, la copia de `rcx` de la entrada);
     - lee la instancia que encontró el juego (`rdi`) y su escena (`gsr::scene_2524`).
   - **Si no sirve:** `rdi := 0`, y el juego toma **su** camino de «no encontrado» [V]:
     - `0xA9E566` suelta el cerrojo;
     - `0xA9ECBC` libera el vector en línea `[rbp+0x870]` (aún sin crecer) y la referencia de la instancia
       `[rsp+0x58]` (`0x38AD90`).
   - **Bytes verificados:**
     - el punto;
     - `0xA9E48F` (la copia de `rcx`);
     - el prólogo `0xA9E494` (`lea rbp,[rsp-0x1820]`);
     - `0xA9E4B4` (`mov r12d,r9d`, que nadie vuelve a escribir antes del punto);
     - la llamada `0xA9E50C`;
     - `0xA9E534` (el movimiento de la instancia a `rdi`);
     - el uso que se protege, `0xA9E62E`;
     - el camino de «no encontrado», `0xA9E566`.
   - **Sin este gancho no hay ningún 2524 hacia X:** el punto `0xC08178` se rechaza.
   - En el gancho de enrutado solo queda la comprobación barata `gsr::pre_2524` (X y M en 3, sin cargas en X). Ya no
     se recorre el mapa de X sin cerrojo.
   - El resumen lo cuenta: `2524 scene check (inner hook in): ok, not found, NOT LOADED, bad branch, other G`. Hay
     una línea `[2524 scene check 0xA9E55B]` por cada rechazo y por las 8 primeras que pasan.
3. **Semántica de `0x1F6C190` [V]:**
   - busca en la cubeta del hash (`[G+0x118]`, `[G+0x108]` cubetas) la **primera** entrada con el mismo id;
   - si su instancia está muerta (`+0x15`), devuelve vacío **sin seguir buscando** (`0x1F6C240`);
   - el `find_instance` de `b728ec7` hacía lo mismo, pero ya no existe: ahora se usa el resultado del propio juego.
4. **`+0x174` [V]:** no son instancias. Es el número de entradas de un segundo mapa del gestor, el de tareas de carga:
   - cubetas en `+0x170`/`+0x180`, nodos en `+0x188`;
   - la vf1 `0xA94660` los libera hasta `+0x174` y lo pone a 0;
   - `0xA97366` (la actualización) y `0xA97DE8` (el predicado del estado `0x14`) esperan a que valga 0;
   - el mapa de instancias es `+0x108/+0x10C/+0x118/+0x120`.
   - La compuerta ya lo trataba como «cargas en curso». Corregidas las etiquetas que decían «instances»:
     `mgr_text` de `guest_seq_route`, `guest_stage_diag` (y su `.h`) y el comentario de `scene_diag_logic.h`.
