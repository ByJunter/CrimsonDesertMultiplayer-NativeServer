# P8: sondas S1/S2 implementadas (solo lectura)

9 de octubre de 2026. Crimson Desert PC **1.0.0.2976**, base **0x140000000**, direcciones como **RVA exe+0x…**.
Responde a `p8-raiz-invitado.md` §3, §4, §7.1 y §7.2 (y §4.4/§5 de `p8-actores-anfitrion.md`).
**[V]** = comprobado (instrucciones del PE autorizado, código, compilación, pruebas); **[E]** = inferencia o
lo que la sesión con Shadow tiene que confirmar. Nada de esto se ha ejecutado en el juego: no se arrancó, no se
copió nada a `bin64`.

## 1. Qué hay

Todo detrás de claves de `[session2]` que vienen a 0; sin ellas no se instala nada nuevo.

| Clave | Dónde | Qué añade |
|---|---|---|
| `guest_actor_diag=1` (ya existía) | invitado | **A.** Capas cur/play también **en la salida** de `0x8646D0`; lista de espera **entera** (hasta 1024) con total / inspeccionadas / no legibles y la clave que bloquea; **transiciones** de `[P+0x15C]` cuando ocurren (hook `0x7798D0`). |
| `load_diag=1` | invitado | **B.** `0x779E48`, `0x779E77`, `0xB87890`, `0xB87A67`. |
| `load_diag=1` | anfitrión | **B.** `0x2BC8470`, `0x287F1CC`, `0xE919C0`. |
| `s1_diag=1` | anfitrión e invitado | **C.** `0x24314C0`, `0x86392A`, `0xA79770`, `0xA79BAC`, `0xA79F4C`, `0x9ED431`, `0x9ED47F`. |

Código: `src/mod/session2/p8_probes.{h,cpp}` (B, C y el instalador común `p8_install_mid`),
`src/mod/session2/p8_probe_logic.h` (lógica pura: anillo, límites, réplica de `0x24314C0`, atribución de
`0xA97D60`), `guest_actor_diag.{h,cpp}` y `guest_actor_diag_logic.h` (A, `scan_wait_list`). Arranque en
`mod.cpp` (invitado: junto a `guest_start`; anfitrión: con `session2` activo). Pruebas:
`tests/optd_p8_probe_tests.cpp`. Ini: `docs/research/ini/cdmp.p8-shadow-s12.ini` (invitado) y dos líneas
comentadas en `cdmp.p8-host.ini`.

Cambios pequeños fuera de esos ficheros: `guest_connection_epoch()` en `guest.{h,cpp}` (lectura atómica de la
época de conexión del cliente, para sellar los eventos del invitado) y `sniffer_first_packet_us()` en
`troc_sniffer.{h,cpp}` (el `t_us` del primer paquete de `cdmp_packets_*.log`, origen de su columna `t`).

## 2. Cómo se verificó cada punto [V]

- PE: `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, SHA-256 `57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`
  (recalculado). Lectura con Python `-I` + Capstone 5.0.7 (scripts propios en el scratchpad, solo lectura del PE).
- Por punto: sección `.rsrc` de inicio a fin del parche; desensamblado lineal **desde el inicio de la función**
  (el punto es frontera de instrucción y el final del parche también); instrucciones que reubica safetyhook
  (enteras hasta ≥ 5 bytes); **ramas de la función que caen estrictamente dentro del parche**; y **barrido de
  todos los rel32** (`E8`/`E9`/`0F 8x`) de `.rsrc`, `.xtls` y `.text` (2.971.972 + 1.773.164 + 4.829
  candidatos) que caigan dentro. Comprobado que el barrido encuentra destinos conocidos (`0x779E72→0xA97D60`,
  `0x287F1D4→0x2999D80`).
- Registros: escrituras del registro leído entre el inicio de la función y el punto (Capstone con detalle):
  `RDI` en `0x7797F0` solo `0x779823 mov rdi,rcx`; `R14` en `0x9ED290` solo `0x9ED2B5`, `RBP` solo `0x9ED2A8`;
  `RSI` en `0xA79770` solo `0xA797BF`; `R15` en `0x863880` solo `0x86389C`; `R14` en `0x287EF60` solo
  `0x287EF84 mov r14,r8`; `R13` en `0x2BC7670` solo `0x2BC769E mov r13,r8` (R14 se recarga de `[rbp+0xB8]` en
  varias ramas: es el actor, su `+0xA0` es el usuario que lee `0x2BC8452`).
- El hilo de `P8Probe` comprueba build (base y `TimeDateStamp`), espera a que exista WorldSystem (pantalla de
  título, como `late_init`; hasta 10 min) y a que los bytes coincidan (hasta 2 min).
- En tiempo de ejecución, antes de instalar (`p8_install_mid`): rango parcheado dentro de `.rsrc` según la tabla
  de secciones **en memoria**; bytes esperados (parche + contexto) iguales a los del PE; hook creado
  **deshabilitado**; sus bytes reubicados deben ser **exactamente** los verificados (si safetyhook cayera a su
  salto de 14 bytes, o decodificara otra cosa, se rechaza sin escribir nada); después se habilita. Cada punto
  además comprueba los bytes del código del que depende la lectura de registros (columna «contexto»). Fallo →
  `P8Probe: … REFUSED …` / `GuestDiag: … REFUSED …`, y el resto sigue.
- Las callbacks no llaman a ninguna función del juego, no reservan memoria, no formatean ni escriben a disco, y
  no usan instrucciones VEX: comprobado **en el `cdmp.asi` compilado** (dumpbin `/disasm`, recorrido de llamadas
  de cada `on_*`): ninguna VEX, ningún `memset`/`memcpy`; solo `QueryPerformanceCounter`, `GetCurrentThreadId`,
  atómicas y lecturas `__try`. El stub de safetyhook guarda flags, los 16 GPR y `xmm0–15`; al no ejecutar VEX
  no se tocan las mitades altas de los YMM del juego.

## 3. Tabla de hooks

«Parche» = bytes que sustituye el `jmp` de 5 bytes (instrucciones enteras, reubicadas en el trampolín).
Ninguna rama de la función ni ningún rel32 de `.rsrc/.xtls/.text` cae dentro de ninguno.

| Punto | Bytes parcheados (verificados) | Contexto verificado | Regs / lecturas | Clave / rol |
|---|---|---|---|---|
| `0x7798D0` | `44 0F B6 B7 5C 01 00 00` (8: `movzx r14d,byte [rdi+0x15C]`) + `44 88 B5 D0 00 00 00` | `kWaitChecks` previos | RDI=P; `[P+0x15C]`; filtro P == `[[exe+0x6D69190]+0x60]` | `guest_actor_diag` / invitado |
| `0x779E48` | `80 B8 31 02 00 00 02` (7: `cmp byte [rax+0x231],2`) + `0F 82 53 03 00 00` | `0x779E39` 15 B (`call 0x9A5BE0`) | RDI=P, RAX=M; P+0x15C/162/163/150..158; M+0x231/174/2D4/2D0, +0x1B0/1B8, +0x1C0/1C8; f32 `exe+0x6CC8898` | `load_diag` / invitado |
| `0x779E77` | `84 C0 0F 84 29 03 00 00` (8: `test al,al ; je rel32`, el `je` reubicado) + `C6 87 5C 01 00 00 12` | `0x779E55` 34 B (pos → `call 0xA97D60`) | AL, RDI=P; M del mismo hilo en `0x779E48`; ≤ 32 entradas de M+0x1B0 (+0x28C, +0x60, +0x61, +0xD8..+0xEC) | `load_diag` / invitado |
| `0xB87890` | `48 89 5C 24 08` (5) + 11 B de prólogo | `0xB878AC` 21 B (`r14=[r8+0x18]`, `[r14+3]`), `0xB87918` `lea rax,[r14+5]` | RCX (+0x21), R8 wrapper, raw `[R8+0x18]`, u16 `[R8+0x10]`, u16 `[raw]`/`[raw+3]`, u32 `[raw+5]` | `load_diag` / invitado |
| `0xB87A67` | `C6 80 31 02 00 00 02` (7: `mov byte [rax+0x231],2`) + 7 B | `0xB87A5D` 10 B (`call [rax+0x48]`) | RAX=M, `[M+0x231]` antes | `load_diag` / invitado |
| `0x2BC8470` | `41 3B C0 75 64` (5: `cmp eax,r8d ; jne short`, reubicado como `jne` near) + 4 B | `0x2BC8452` 30 B, `0x2BC84D4 call 0x287EF60` | EAX userKey, R8D dueño, R13 campo, R14 actor (+0x60, +0xA0→+0x60) | `load_diag` / anfitrión |
| `0x287F1CC` | `48 8D 54 24 50` (5: `lea rdx,[rsp+0x50]`) + `49 8B CE E8 A7 AB 11 00 90 48 89 7C 24 50` | el `call 0x2999D80` en `0x287F1D4` va dentro de los bytes esperados | R14 actor destino (+0x60, +0x90, +0xA0), finder = RSP+0x50 | `load_diag` / anfitrión |
| `0xE919C0` | `48 89 5C 24 08` (5) + 10 B | slot de vtable `exe+0x569CA40` == `exe+0xE919C0` | RCX this (vtable `exe+0x569C6B8`), RDX, R8, R9B, `[RCX+0x160]`, `[RCX+0x168]` | `load_diag` / anfitrión |
| `0x24314C0` | `48 89 5C 24 08` (5) | **toda la función** (0xDA bytes) | EDX hash (filtro), RCX graph (+0x18, +0x20), R8 out, `[RSP]` llamador | `s1_diag` / ambos |
| `0x86392A` | `48 83 C7 08 48 83 C3 78` (8: `add rdi,8 ; add rbx,0x78`) + `48 83 ED 01 75 BB` | `0x8638EA` 64 B (bucle y ambas llamadas), `0x86389C mov r15,rcx` | R15=CC, RDI=CC+0x70+8i, `[RDI+0x18]` capa (+0x110), ESI hash, RBX tripleta | `s1_diag` / ambos |
| `0xA79770` | `48 8B C4 48 89 58 18` (7) + 9 B | `0xA797B8` 10 B (`r15b=r8b`, `r14=rdx`, `rsi=rcx`) | RCX contexto, CC=`[RCX]`, actor `[CC+8]`; RDX desc (+0x18, +0x28, +0x30, +0x3C); R8B, R9B; CC+0x70/+0x78 | `s1_diag` / ambos |
| `0xA79BAC` | `48 89 0B 4C 89 3F` (6) + 6 B | `0xA79B78` (`rbx=[rsi]+0x70`), `0xA79BA9`, `0xA797B8` | RBX ranura, RCX gráfico, RSI contexto, CC=`[RSI]`, `[RBX]` previo | `s1_diag` / ambos |
| `0xA79F4C` | `48 89 0B 33 C0` (5) + 6 B | `0xA79F18` (`rbx=[rsi]+0x78`), `0xA79F49`, `0xA797B8` | igual, CC+0x78 | `s1_diag` / ambos |
| `0x9ED431` | `80 7D E8 00 75 1C` (6: `cmp byte [rbp-0x18],0 ; jne short`) + 3 B | `0x9ED2A8 mov rbp,rsp`, `0x9ED2B5 mov r14,rcx`, `0x9ED415` 11 B | R14 comp (+8, +0x2D8, +0x2E0), `[RBP-0x18]`, `[RBP-0x20]` | `s1_diag` / ambos |
| `0x9ED47F` | `48 8B 7D A8 48 85 FF` (7) + **`0F 84 E9 00 00 00` entero** (no parcheado) | `0x9ED2A8`, `0x9ED2B5`, `0x9ED470` 15 B | R14 comp, `[RBP-0x58]` instancia | `s1_diag` / ambos |

**Coexistencia [V].** Ninguna constante de otro módulo cae dentro de estos rangos (búsqueda en `src`). Puntos
existentes y su relación:

- `guest_actor_diag`: `0x4FD491`, `0x8646FC`, `0x864B1D` (sin cambios de bytes) y el nuevo `0x7798D0`. Sus
  comprobaciones de `kWaitChecks` (que leen `0x7798D0`) se hacen **antes** de instalar ese hook. `0x779E48`,
  `0x779E77` (p8) no solapan `0x779ED0` (lista de espera) ni `0x7798D0`.
- `companion_probe` (anfitrión): `0x8AF580/60E/685/6C6`, `0x206B2D0/394/49E/4FD/52F/5D0`, `0x232D5C0/5EE`
  (+ lecturas `0x232D51E/543/59D`), `0xCB4160`, `0xAAFC00/1D`, y el mismo par `0x8646FC/0x864B1D` (nunca a la vez
  que `guest_actor_diag`). Sin solape con p8.
- `horse_diag` (`0x8646D0` y la contención de `0x85FBA0`/`0x860D30`), guardas de `remote_puppet`
  (`0x1F63F70`, `0x2329700`), sniffer (`ClientNet::PostPacket`/`OnRecvPacket`), `server.cpp`, `clock_gate`
  (`0x2BC0A30`…), `field_share` (`0x2B07790`, `0x2BC04F0`), `child_hold` (`0x2B000C0`…, `0x296CAA0`),
  `identity_*` (`0x20688A0`, `0x2941020`): sin solape.
- **S3 (en paralelo):** sus candidatos del informe (`0x232D5C0`, `0x232D2A0`, `0x48B120`, `0x48B197`,
  `0x48AD40`, `0x863530`, `0x85FBA0`) no solapan. Requisito para integrar: que nadie parchee
  `0x24314C0..0x243159A` (esta sonda verifica la función entera porque la replica), `0x8638EA..0x863931` ni los
  rangos de la tabla.

## 4. Qué responde cada sonda

**A1 — restauración, antes y después [V código].** La línea `GuestDiag restore` ya mostraba el snapshot
*previo*; ahora añade `after (exit): L0 chart … cur … play … | L1 …` leído en `0x864B1D` (`R13` = CC), cuando
`0x863880` (y `0x2329A90` si tuvo éxito) ya volvieron. Responde a §3.4: si tras una restauración «correcta» la
capa sigue sin acción. Contador nuevo: `chart and no action at the exit`.

**A2 — lista de espera completa [V código].** Lee hasta 1024 ranuras; una clave no legible se cuenta y **no**
corta la lectura. `scan_wait_list` cuenta total / inspeccionadas / no legibles / no encontradas / listas /
no listas y repite el swap-remove del estado `0x12` sobre la lista entera: si el recorrido llega a una ranura
no legible o no inspeccionada, dice `?` con su índice en vez de adivinar. Muestra hasta 16 ranuras «no listas o
no legibles».

**A3 — transiciones de `[P+0x15C]` [V código].** `0x7797F0` vuelve a `0x7798B6` cada vez que un estado cambió
`[rdi+0x15C]` (`0x77A1B0 cmp r14b,[rdi+0x15C] ; je ; jmp 0x7798B6`), así que todo valor por el que pasa una
llamada se lee en `0x7798D0`. Un evento por cambio (QPC, hilo, P, viejo, nuevo); se escribe cada 5 s con la
duración en el estado anterior y `EXIT FROM 0x11` cuando sale de `0x11`.

**B, invitado.**
- `0x779E48`: el **primer predicado** y todo lo que lee `0xA97D60` que se puede leer, solo cuando cambia
  (firma: M, estado, P+0x162/163, M+0x231, M+0x174, M+0x2D4, `M+0x2D0 < umbral`, tamaños de listas).
- `0x779E77`: **AL de `0xA97D60`** y la **primera dependencia que con seguridad la hace falsa**, en el orden de la
  función: `M+0x231<2`, `M+0x174≠0`, temporizador, y en la lista M+0x1B0 la primera entrada con `+0x28C==0`,
  `+0x61==5` o modo `+0x60∈{0,1}` con la posición dentro de su caja. Esas tres condiciones bloquean sea cual sea
  `0x1AC2830`, pero las entradas anteriores dependen de `0x1AC2830` (no se llama): se cuentan como
  «undetermined before». Si nada legible lo explica: `undetermined by reads` (contexto de campo `0x1F6C540`,
  `0x1AC2830`, o la lista M+0x1C0 vía `0x5DB510`). Un evento por cambio de (AL, motivo, índice, M) y siempre
  que AL=1 (la transición a `0x12`).
- `0xB87890`: llega un 2322, con su clave, tamaño, `[this+0x21]` (si ≠0 el handler no hace nada) y la época de
  conexión. `0xB87A67`: el `M` que recibe el 2 y su valor previo, y si es **el mismo M** que vio `0x779E48`.

**B, anfitrión.**
- `0x2BC8470`: por cada `MoveFieldCompleteCore`, userKey del viajero, dueño del campo, y la rama: dueño →
  preparación (`0x2BC84D4 call 0x287EF60`) o no dueño → se salta preparación y 2322 (§4.2).
- `0x287F1CC`: cada vez que se va a llamar al emisor `0x2999D80` (el 2322 dirigido): actor destino y finder.
- `0xE919C0`: eventos de la UI ExtraReward. `R9B==0 && R8==[this+0x160]` es la rama de cierre
  (`vf+0x28`); `R8==[this+0x168]` el otro botón. Se registran siempre esas dos y el resto solo si cambia
  (R8, R9B). El tiempo QPC y `capture t=` permiten alinear el cierre con el tráfico cliente↔servidor local que
  graba el sniffer (`record=1`) en el **mismo** proceso.

**C, S1 (anfitrión e invitado).**
- `0x86392A`: por cada hash ≠0 del bucle de `RestartLayerAction`: actor, capa, hash, **camino** (gráfico de la
  capa / fallback `CC+0x70+8i` / no encontrado / sin gráfico fallback) y la **tripleta resultante**. Los dos
  lookups se **replican** leyendo la tabla (sin llamar). En los 15 fallos de S1 se espera
  `NOT FOUND (no fallback graph)` con tripleta `INCOMPLETE`. Primeros 2 por (actor, capa) y todos los
  incompletos.
- `0x24314C0`: solo para `837487D4` / `35DD392F`: gráfico, cantidad, tabla, llamador y resultado replicado.
  Primeras 2 por (gráfico, llamador) y todos los «no encontrado».
- `0xA79770`, `0xA79BAC`, `0xA79F4C`: si en este proceso se monta el contexto BodyAnimation del actor y se
  instalan `CC+0x70/+0x78`, con el gráfico nuevo, su cantidad y si contiene los dos hashes. **La prueba decisiva
  de §7.1**: mismo actor (clave) en el log del anfitrión y del invitado; si el anfitrión tiene `install` y el
  invitado no, el contexto no llegó/no se aplicó en el invitado.
- `0x9ED431` / `0x9ED47F`: si el componente de secuenciador resolvió el **actor del binding** y la
  **instancia**; si no, `success without binding`.

## 5. Líneas esperadas

Prefijo común de `P8Probe`: `P8Probe [<punto>] qpc S.US s (~hh:mm:ss.mmm) capture t=<s> T<hilo> | …`.
`qpc` es el mismo reloj que `t_us` de `cdmp_packets_*.bin`; `capture t` es la columna `t` de
`cdmp_packets_*.log` (o `-` sin captura).

| Línea | Qué dice |
|---|---|
| `Config: … P8 load diag 1 s1 diag 1` | las claves llegaron |
| `P8Probe ON (read only, guest\|host): load_diag … s1_diag … \| hooked: … \| REFUSED: …` | qué puntos entraron; si hay REFUSED, buscar `P8Probe: … REFUSED` |
| `GuestDiag ON (read only): … state transitions at exe+0x7798D0 on …` | A instalado |
| `GuestDiag loading transition: [P+0x15C] 0x11 -> 0x12 after N s in 0x11 \| EXIT FROM 0x11 \| qpc …` | la salida del 0x11 cuando ocurre |
| `GuestDiag loading: … wait list total 173 inspected 173 unreadable 0 \| not found … ready … NOT READY … \| … state 0x12 waits for 0x…` | la lista entera |
| `GuestDiag restore … -> RestartLayerAction 0x863880 FAILED \| after (exit): L0 chart yes cur 0 play 0 …` | antes y después |
| `P8Probe [loading 0x779E48] … S2 state [P+0x15C] 0x11 P … M … \| M+0x231 1 (needs >= 2) …` | primer predicado |
| `P8Probe [loading 0x779E77] … S2 0xA97D60(M, pos) -> 0 \| first certain blocker: …` | segundo predicado |
| `P8Probe [2322 entry 0xB87890] … actor key 0xA010022E …` / `P8Probe [2322 write 0xB87A67] … SAME manager` | el hito y su M |
| `P8Probe [owner 0x2BC8470] … -> NOT the owner: jne 0x2BC84D9 skips the preparation and its 2322` | anfitrión, usuario 2 |
| `P8Probe [2322 path 0x287F1CC] … target actor 0x…` | anfitrión, emisión dirigida |
| `P8Probe [ExtraReward 0xE919C0] … -> CLOSE branch (vf+0x28)` | instante del cierre del aviso |
| `P8Probe [Restart 0x86392A] … actor 0xB0100078 … hash 0x837487D4 … -> NOT FOUND (no fallback graph) \| … triple INCOMPLETE` | S1 |
| `P8Probe [install +0x70 0xA79BAC] … actor 0x… new graph … (count …, seq_bodyanim_action_xxx yes, loop …)` | S1, productor |
| `P8Probe [binding 0x9ED431] … binding actor NOT FOUND …` / `[instance 0x9ED47F] … NULL …` | S1, binding |
| `P8Probe host\|guest \| ring dropped N \| <punto> on: hits … queued … limited … dropped … per-key skipped … lines …` | contadores cada 60 s |

Ojo: en `P8Probe`, **M** es el gestor de stages/secuenciadores de `0x9A5BE0` (§4.1). En `GuestDiag loading`,
la columna «M» es `[P+0xB8]` (dueño del contenedor de espera): objetos distintos.

## 6. Coste y límites

- **Anillo**: Vyukov MPMC acotado de 8192 eventos (~1 MB), sin bloqueo; lleno → se cuenta `dropped`.
  Transiciones de A: anillo propio de 256.
- **Límites**: 256 eventos por punto cada 5 s (`limited`), 4000 líneas por punto por sesión; S1: primeros 2 por
  clave (BodyAnimation 4, instalaciones 8) más todos los fallos dentro del límite (`per-key skipped`);
  `0x779E48`/`0x779E77`/`0xE919C0` solo con cambios. Formateo y escritura en un hilo propio cada 5 s
  (A: el hilo de instalación de `guest_actor_diag`, cada 5 s).
- **Frecuencia [E, estimada]**: `0x24314C0` es el punto caro: se ejecuta en *cada* búsqueda de acción del proceso
  (filtro de dos comparaciones tras el stub, ~decenas de ns por llamada). `0x779E48/77` una vez por frame solo en
  el estado 0x11 (≤ 32 entradas leídas). `0x86392A` dos veces por `RestartLayerAction` (restauraciones y 2991).
  El resto es esporádico (paquetes 2322, viajes, eventos de UI, BodyAnimation, binding).
- **No observable sin llamar al juego**: contexto de campo (`0x1F6C540`), `0x1AC2830` (usa un objeto virtual del
  contexto) y `0x5DB510` (busca actores con `0x8AE940`). Por eso la atribución dice «undetermined» cuando
  corresponde, nunca un culpable inventado.
- `0x7798D0` solo cuenta el P de `[[exe+0x6D69190]+0x60]` (el mismo del informe de 5 s); otros P se cuentan como
  `other P`.
- Época: en el invitado, la de la conexión del cliente (`[session+0xB0]`); en el anfitrión 0.

## 7. Correcciones y precisiones al informe

1. **[V] `0x863914` no se puede enganchar.** Su `jmp` de 5 bytes cubre `0x863914..0x863918` y safetyhook
   reubicaría también `0x863918 mov rcx,[rdi]`; `0x863908 je 0x863918` cae **dentro** del parche. Se sustituye
   por `0x86392A` + réplica de ambos lookups (mismo dato: AL del principal, si hubo fallback y su resultado,
   tripleta final).
2. **[V] `0x287F1D4` → `0x287F1CC`.** Mismo camino (ninguna rama entra en `0x287F1CD..0x287F1D4`), mismos
   registros (`R14`; el finder es `RSP+0x50`), sin reubicar un `call` (su dirección de retorno apuntaría al
   trampolín, sin datos de unwind).
3. **[V] `0xE919C0` es exclusivo de ExtraReward:** la única referencia qword a `0x140E919C0` en todo el PE es el
   slot 113 (`exe+0x569CA40`) de la vtable `exe+0x569C6B8`, sin límite de vtable entre medias (todas las entradas
   `0x569C6B8..0x569CA58` apuntan a `.rsrc`). Se comprueba también en ejecución.
4. **[V] Las transiciones del estado se ven en una sola lectura:** ver A3 (`0x77A1B0..0x77A1BD`).
5. **[V] `0x24314C0` solo «encuentra» con igualdad exacta** en tripletas completas; con un puntero nulo compara
   0 (por eso hash 0 «encuentra» una entrada incompleta). Se replica instrucción a instrucción y se prueba contra
   una implementación de referencia.
6. **[V] Umbral del temporizador:** `DAT_146cc8898` (`exe+0x6CC8898`, `.rdata`) vale `0.0` en el fichero; la
   sonda lo lee en ejecución.
7. **[V] `0x9ED47F`:** el parche son 7 bytes (`mov rdi,[rbp-0x58] ; test rdi,rdi`); el `je rel32` de
   `0x9ED486` queda fuera y se verifica entero.
8. **[V] La lista de 64** (§4.3) queda corregida por A2.

## 8. Pruebas (`tests/optd_p8_probe_tests.cpp`, `ctest -R optd_p8_probe`)

**Alcance: lógica offline**, sin juego; no prueban el comportamiento del motor ni los offsets (eso es §2 y la
sesión real).

- anillo: FIFO, lleno → descartado y contado, 4 productores + 1 consumidor (recibidos + descartados = enviados,
  sin duplicados);
- `WindowLimiter` (por ventana de 5 s), `KeyCounter` (presupuesto por clave, tabla llena, concurrencia),
  `ChangeGate` (solo cambios), `qpc_to_us` (sin desbordamiento);
- `scan_wait_list`: conteo con ranuras no legibles, no encontradas, listas y no listas; bloqueo tras
  swap-remove; ruta que alcanza una ranura no legible o no inspeccionada → indeterminado; cuenta implausible;
  coincide con `wait_blocker` en 500 listas aleatorias;
- réplica de `0x24314C0`: todas las claves, ausentes, vacía, `count > 0x7FFFFFFF`, ilegible, tripleta
  incompleta, hash 0, 300 tablas aleatorias contra referencia;
- camino de `RestartLayerAction` (incluido el caso de los 15 actores) y tripleta completa;
- atribución de `0xA97D60`: orden de predicados, cada motivo, entradas indeterminadas previas, lectura truncada,
  ilegible, AL=1, caja inclusiva.

## 9. Compilación

`.\scripts\build.ps1` en el worktree: compila sin avisos nuevos y **16/16 pruebas** pasan (incluida
`optd_p8_probe`).

## 10. Correcciones tras la revisión (9 de octubre de 2026)

Revisión independiente de S1/S2/S3 (detalle de S3 en `p8-impl-s3.md` §12). Nada se ha ejecutado en el
juego; solo build y tests.

### 10.1 I5 — `0x24314C0` (ActionGraph_Find, 81 llamadores) sin guardar contexto en cada llamada

Antes era un *mid hook*: guardaba todos los registros (también XMM) en **cada** búsqueda de acción del
proceso, aunque solo dos hashes pasan el filtro. Ahora es un **hook en línea** (`SafetyHookInline`) cuyo
detour hace solo esto:
1. cuenta la llamada en un fragmento por hilo (`[TEB+0x48] & 63`, carga + escritura sin `lock`, aproximado);
2. compara EDX con `0x837487D4` y `0x35DD392F`, y solo para ellas ejecuta la sonda de siempre (réplica de la
   tabla, sin llamar al juego);
3. llama al original por su trampolín con **los cuatro registros de argumentos sin tocar** y devuelve RAX
   entero.

Verificación **[V]**:
- La función toma `rcx` (gráfico), `edx` (hash) y `r8` (salida).
- `r9` se sobrescribe en `0x24314DD` (`xor r9d,r9d`), así que no es argumento.
- No hay argumentos en pila: el cuerpo solo escribe sus huecos *home* `[rsp+8..0x18]`.
- El prólogo reubicado es **una sola instrucción**, `mov [rsp+8],rbx` (`48 89 5C 24 08`): sin direccionamiento
  relativo a RIP.
- Al instalar, como en `p8_install_mid`, se exige:
  - el parche dentro de `.rsrc`;
  - los 0xDA bytes de la función iguales;
  - que `original_bytes()` de SafetyHook sea exactamente esos 5 bytes. Si no: `REFUSED`, nada escrito.
- La dirección de retorno del llamador (antes `[RSP]` en el *mid hook*) es `_ReturnAddress()` del detour:
  es el mismo valor.

El contador total sale en la línea de contadores de cada minuto: `0x24314C0 calls, every hash (approximate,
the filter's load) N`. Al instalar se registra que **`s1_diag` es para sesiones cortas**.

**[E]** Riesgo residual: igual que los demás hooks en línea del proyecto (`NotifyAppear`, `StartActionReq`,
`0x863530`), el detour es C++ y respeta la ABI de Windows x64. Solo se rompería si un llamador dependiera de
registros volátiles que `0x24314C0` no toca, algo que la ABI no garantiza.

### 10.2 I4 — `KeyCounter` acotado

Antes, `p8p::KeyCounter::bump` recorría las 8192 ranuras con la tabla llena. Ahora:
- sondeo de como mucho 32 ranuras;
- carga máxima N/2 (4096 claves por punto);
- al alcanzarla, una marca **pegajosa** que se mira primero: ya no se intenta insertar, solo queda la
  búsqueda acotada, y las claves conocidas siguen contando. Una clave no registrada recibe 0 (fuera de
  presupuesto), como antes.

Igual en `s3::KeySet` y `s3::GuardTable` (§12.5 de `p8-impl-s3.md`).

### 10.3 M7 — `guest_actor_diag`: la línea de restauración ya no se formatea en el hilo del juego

`on_restore_exit` (`0x864B1D`) guarda un registro fijo en un anillo de 256, solo si pasa el tope de líneas.
Lo imprime el tick de 5 s (`guest_actor_diag_tick` → `restore_report`) con el mismo texto, más el hilo y la
hora QPC.

### 10.4 Tests y build

- `optd_p8_probe`: `KeyCounter` pequeño (cierre a N/2, claves conocidas que siguen contando) y de tamaño
  real (10 000 claves sobre 8192 ranuras).
- `.\scripts\build.ps1`: **build OK, 17/17 tests**.
