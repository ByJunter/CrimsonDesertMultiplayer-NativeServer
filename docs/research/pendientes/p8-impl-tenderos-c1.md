# P8 sesión 13: tenderos — C1 en el invitado y sonda del servidor en el anfitrión

Build del juego 1.0.0.2976 (base `0x140000000`, direcciones en RVA). Fecha: 2026-10-11. Rama `p8-shop-c1` (desde
`opcion-d` 620efaa). Sigue a `p8-s12-tenderos.md` (lado cliente) y `p8-s12-tenderos-servidor.md` (lado servidor; su §7
es la lectura estática nueva para S2). Sin juego: compilado y probado solo con tests.

Convenciones: **[V]** comprobado (instrucciones leídas o test), **[E]** inferencia fundada, **[H]** sin demostrar.
X = gestor de escenas del Kliff del anfitrión replicado en el invitado; M = el del Kliff propio del invitado.

## 1. Qué hay

| Clave `[session2]` | Dónde | Valores | Qué hace |
|---|---|---|---|
| `guest_x_interaction_stage` | **invitado** (necesita `guest_seq_route=1`) | `0` \| `probe` \| `1` (por defecto 0) | C1: en la comprobación del aviso de interacción, si la etapa de la entrada no está en M pero sí en X, la busca en X (`1`) o solo lo decide y lo cuenta (`probe`) |
| `shop_probe` | **anfitrión** (necesita `create_session=1`) | `0` \| `1` (por defecto 0) | Sondas de **solo lectura** del lado servidor: puerta de dueño, escenas `cd_seq_funcnpc_*` preparadas, resultado del 2671, comprobación `0x2126920` del servidor, Req del usuario 2 y Ack que se le encolan |

Inis: `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini` llevan `shop_probe=1`; `cdmp.p8-s7-shadow.ini`,
`cdmp.p8-s7-shadow-fase2.ini` y `cdmp.p8-s12-shadow-seguro.ini` llevan `guest_x_interaction_stage=probe`.

Código: `src/mod/session2/guest_x_interaction.{h,cpp}` + `guest_x_interaction_logic.h` (invitado),
`shop_probe.{h,cpp}` + `shop_probe_logic.h` (anfitrión), `stage_lookup_logic.h` (común: réplica de `0x1F6C190` y los
puntos dentro de `0x2126920`). `guest_x_veto.cpp` solo avisa a C1 de cada X que ve (un 2981 enrutado, un manejador con
G = X); no cambia lo que hace. Tests: `tests/optd_shop_c1_tests.cpp` (`ctest -R optd_shop_c1`).

## 2. Puntos y bytes

Todos en `.rsrc`, con `safetyhook::create_mid` vía `p8_install_mid`: antes de escribir nada se comparan los bytes del
punto y su contexto con la build 2976; si algo no coincide, ese punto no se instala y sale un `ERR ... REFUSED`. Para
cada parche: instrucciones enteras, ninguna rama de la función ni ningún `call`/`jmp` rel32 del PE cae **dentro** del
parche (comprobado con capstone, barrido de la función por `.pdata` e índice de referencias). Que una rama caiga en el
**primer** byte es correcto (salta al gancho).

### 2.1 Dentro de `0x2126920(entrada, J)` (invitado: C1; anfitrión: la llamada del servidor)

| Punto | Bytes | Registros |
|---|---|---|
| A `0x2126A1F` | `48 8B F8` `48 89 85 E8 08 00 00` (`mov rdi,rax ; mov [rbp+0x8E8],rax`, 10 B) | `RAX` = `field.vf48` (el gestor de la búsqueda); `[rsp+0x48]` = el campo; `R13` = la entrada (`[r13+0x20]` = id de etapa, u64: `0x2126A5C mov r8,[r13+0x20]`); `R15` = J |
| R `0x2126AAB` | `48 8D 8D D0 08 00 00` (`lea rcx,[rbp+0x8D0]`, 7 B; destino de dos `je`) | `RSI` = instancia encontrada o 0; `[rbp+0x8E8]` = gestor usado |
| F `0x2126D11` | `48 89 7D 80` `48 8D 4D 80` (8 B; destino de un `jmp`) | `BL` = resultado: 0 rechazada, 1/2 aceptada (lo lee `0x2120470`) |

Contexto comprobado: `0x2126A14 mov rcx,[rsp+0x48] ; mov rax,[rcx] ; call [rax+0x48]` y `0x2126A5C ... call 0x1F6C190`.
Con `RAX := X` en A, las dos instrucciones reubicadas copian X a `rdi` y `[rbp+0x8E8]`, de modo que el cerrojo
(`0x393DF0` sobre `[X+8]`, `0x2126A56`), la búsqueda (`0x2126A6A`), la comprobación de muerta (`0x1F6FC40`,
`0x2126AD0`) y la liberación (`[rbp+0x8E0]`) usan X de forma coherente. `vf40` (misiones) sigue siendo el del campo
local [V].

### 2.2 Anfitrión (`shop_probe`)

| Punto | Bytes | Qué lee |
|---|---|---|
| E `0x288B7A0` | `48 8B C4 48 89 58 10` (entrada, 7 B) | `r8` = personaje del gestor → cuenta de actualizaciones por personaje |
| G `0x288B87A` | `41 39 87 80 01 00 00` (7 B; el `jne` rel32 siguiente se comprueba, no se parchea; destino de un `jmp`) | `r13` personaje, `eax` su clave de usuario, `[r15+0x180]` dueño del campo, `rdi` gestor |
| P `0x288AAEF` | `90 48 8B 4D 77` (tras `call 0x287C200`, 5 B) | `[r14]` resultado, `[rbp-0x51]` dueño, `[rbp-0x49]` id, `[rbp+0x77]` instancia, `[[[rdi+8]]]` ruta del recurso, `[rdi+0x200]` datos de enlace, `r15` gestor |
| C `0x29E9282` | `8B 5D A8 85 DB` (tras `call 0x2B36860`, 5 B; el `je` rel32 no se parchea) | `[rbp-0x58]` resultado, `[rbp+0x70]` quien pide, `[rbp+0x78]` NPC, `[rbp-0x60]` interacción, `r13` actor que pide |
| S A/R/F | los de §2.1 | solo si el campo es un `ServerField` (vtable `0x5B29A60`); J = quien pide |

Taps sin gancho: `session2.cpp` (camino de las Req del invitado, justo después de `observer_seq_copy_filter_req`) y
`server.cpp` (`take_frame`, junto a `observer_seq_copy_on_out`).

### 2.3 Réplica de `0x1F6C190` (C1)

C1 no llama al juego: replica la búsqueda con lecturas SEH (`isl::find_stage`) [V código, V test con tabla falsa]:
cuenta `[mgr+0x10C]`, cubetas `[mgr+0x108]`, `h = lookup3(id, 8 bytes, initval 0xC5EDE)` (`0x1364780`, comprobado contra
`gsl::hashlittle` y contra una transcripción instrucción a instrucción), cubeta `[mgr+0x118] + (h % cubetas) * 0x100`
con `n` pares {hash, índice}, elemento `[[mgr+0x120] + índice*8]` con el id en `+8` y la instancia en `+0x10`, y la
instancia con `[+0x15]` puesto cuenta como no encontrada. Sin el cerrojo del juego: una tabla a medio cambiar da «no
encontrada» o una respuesta vieja, nunca una escritura.

Qué X conoce C1: los que `guest_x_veto` le pasa (cada 2981 enrutado a X por `guest_seq_route`, cada manejador que corre
con G = X). Antes de usar uno se comprueba como `guest_seq_play` comprueba su X: vtable
`ClientSequencerStageManager`, no es M, su personaje no es el propio, y el gestor actual de ese personaje es X.

## 3. Qué significa cada línea

### 3.1 Invitado (`GuestXInter`)

- `GuestXInter (guest_x_interaction_stage=probe): A 0x2126A1F hooked, R ... hooked, F ... hooked | ...` — instalación.
  Un `REFUSED` en A deja C1 sin efecto (R y F solo leen).
- `GuestXInter [entry] ... | stage id 0x197 of entry 0x...: in X, not in M | M ... | X 0x... (character 0xA0100001)
  instance 0x... (its +0x30 0x197, name 'cd_seq_funcnpc_...') | X known 1 valid 1 ... | WOULD REDIRECT (probe ...)` —
  una entrada dinámica cuya etapa vive en X y no en M: **la causa de la sesión 12 vista en vivo**. Con `1` dice
  `REDIRECTED (RAX := X ...)` (sale como `WRN`). El nombre y `+0x30` son de mejor esfuerzo (la misma cadena que lee
  `guest_x_veto` desde un procesador); si `+0x30` coincide con el id, la instancia es la etapa.
  Otras conclusiones posibles en la misma línea: `in neither M nor X` (entrada de una escena que no está en ningún gestor
  conocido: escena ya borrada, o X no anotado), `dead in X`, `no X known` (C1 no conoce ningún X: `guest_x_veto` no ha
  visto ninguno todavía), `M unreadable`. Las entradas cuya etapa está en M (`in M (native)`) solo se cuentan.
  Límite: 3 líneas por id y conclusión, 24 por 5 s.
- `GuestXInter [result] ... | stage id 0x197 ... (in X, not in M; would redirect): the lookup used manager 0x... (M) and
  found nothing | 0x2126920 returned 0 (refused: no prompt)` — el resultado real de esa misma llamada. En `probe` debe
  salir **M / found nothing / 0** (confirma el diagnóstico). En `1` debe salir **X / found instance / 1 o 2**.
- `GuestXInter summary (...)` cada 60 s: llamadas en A, conclusiones, `REDIRECTED n, would n`, resultados de las
  redirigidas / de las que se redirigirían / de las demás (`0:a 1:b 2:c other:d` + lo que encontró la búsqueda nativa),
  los X conocidos y el estado de sus comprobaciones.

### 3.2 Anfitrión (`ShopProbe`)

- `ShopProbe (shop_probe=1, read only): E ... hooked | G ... | P ... | C ... | S ... | taps ...` — instalación.
- `ShopProbe [gate] ... character 0xA0100336 (manager 0x...): its user 0x90100335 vs the field 0x...'s owner 0x90100000
  -> NOT the owner: leaves without evaluating any stage` — la puerta de §2.2 del documento del servidor en vivo (2
  líneas por personaje y resultado; los totales en el resumen).
- `ShopProbe [funcnpc] ... 'cd_seq_funcnpc_butcher...' sequencer id 0x197, owner 0xA0100001, result 0x0 | instance ...,
  binding data ... | manager 0x... of character 0xA0100001 (user 0x90100000)` — una escena de tienda preparada por el
  servidor (su id queda guardado para marcar los paquetes). `[stage]` = otros elementos (solo los 24 primeros).
- `ShopProbe [2671] ... USER 2 asked: requester 0xA0100336 ... -> NPC 0xB0100156 interaction 0x000F4321 | 0x2B36EF0
  path result 0x0 -> ACCEPTED (2663 follows)` — lo que el servidor decide con el 2671 (las del anfitrión: 16 líneas).
- `ShopProbe [server check] ... 0x2126920 on the SERVER for USER 2 ...: entry ... stage id 0x197 looked up in manager
  0x... (the field owner's ...): FOUND (instance ...) | result 1 (accepted)` — la misma comprobación que C1, en el
  servidor (§3.1 del documento del servidor predice FOUND).
- `ShopProbe [user2 req] ... 2671 / 2720 ...` — lo que pide el invitado; `2436 StageBranchStateReq: sequencer 0x197
  (then 0x...), a cd_seq_funcnpc_* scene of the host | observer_seq_copy R1: DROPPED ...` — el cambio de rama que
  abriría la tienda, y si el filtro R1 lo corta (no se cambia nada: solo se cuenta).
- `ShopProbe [to user2] ... 2663 ... queued | n bytes: ...` — Ack de interacción encolados al usuario 2 (y 2981/2524 de
  escenas de tienda).
- `ShopProbe summary` cada 60 s: actualizaciones / puerta / dueño / no dueño por personaje, escenas preparadas y de
  tienda, 2671 por quién y rechazados, llamadas del servidor a `0x2126920` y resultados, Req del usuario 2 (con caídas
  por R1) y Ack que se le encolan.

## 4. Sesión 13: qué decide el paso de C1 a `1` y el paso a S2

Orden: anfitrión con `shop_probe=1`; Shadow con `guest_x_interaction_stage=probe`. El jugador de Shadow se acerca a 2–3
tenderos (carnicero, herrero, taberna) y anota la hora en que intenta hablar.

**Paso 1 (probe).** Se pasa C1 a `1` si se cumplen las tres:
1. `GuestXInter [entry]` con `in X, not in M` y `WOULD REDIRECT` para ids de tienda (los de las líneas
   `ShopProbe [funcnpc]` del anfitrión, o los `stage 'cd_seq_funcnpc_*'` de `GuestXVeto ... SetInteraction add`).
2. Sus `[result]`: `used manager ... (M) and found nothing | returned 0`. Es la causa de §4.2 de `p8-s12-tenderos.md`.
3. Ninguna `[entry]` con `X ... character` igual al propio del invitado, y el resumen sin `REFUSED`.

Si sale `no X known` en vez de `in X, not in M`: falta el aviso de X (ver `GuestXVeto`: ¿hay 2981 enrutados?); no
pasar a 1. Si sale `in M (native)` para las tiendas: el diagnóstico era otro; no pasar a 1.

**Paso 2 (C1 en `1`, misma sesión si el paso 1 salió bien).** Hablar con el carnicero:
- Invitado: `[entry] REDIRECTED`, `[result] ... (X) and found instance ... | returned 1 (accepted)` o `2`, y **aparece
  el aviso** de hablar.
- Anfitrión: `ShopProbe [2671] USER 2 ... ACCEPTED`, `[server check] ... FOUND ... result 1/2`, `[to user2] 2663` y,
  tras pulsar, `2916`/`2525`.
- Esperado [E, §3.2 del documento del servidor]: **la tienda no se abre** (nadie manda el 2436 de la rama 6, o R1 lo
  corta: `[user2 req] 2436 ... DROPPED`).

**Pasar a S2** (escena gemela, `p8-s12-tenderos-servidor.md` §5.2) si en el paso 2: el servidor acepta (2671 resultado
0 y 2663/2916 encolados) y la tienda no abre. Eso confirma que lo que falta es una escena **propia** del usuario 2. Con
§7 de ese documento, T1 ya no es el bloqueo; antes de S2 en 1 hay que cerrar T2 (reentrada del cerrojo).

**Si el servidor rechaza** (2671 con resultado ≠ 0, o `[server check] ... result 0`): C1 solo no aporta; volver a
`guest_x_interaction_stage=0` y apuntar el código de resultado. S2 sigue siendo el camino (con su propia escena la
comprobación pasa por construcción [E]).

## 5. Cuándo abortar (volver a `guest_x_interaction_stage=0`, o a `probe`)

- Cierre del juego o congelación al acercarse a un NPC o al pulsar sobre él con C1 en `1`.
- El anfitrión ve abrirse **su** diálogo o tienda cuando habla el invitado, o pierde cámara/entrada.
- El invitado pierde interacciones que antes tenía (NPC propios, objetos): mirar si sus `[entry]` dicen `in M (native)`
  (no se tocan) o `REDIRECTED` con ids que no son de tienda.
- Un `REDIRECTED` con `X ... character` igual al Kliff propio (las comprobaciones lo impiden; si sale, es un error).
- `shop_probe` no se aborta por el juego (solo lee); si el registro crece demasiado: `shop_probe=0`.

## 6. Riesgos

| Riesgo | Nota |
|---|---|
| Búsqueda en X desde el hilo del aviso | El juego toma el cerrojo de `[X+8]` como haría con M; X lo actualiza `guest_seq_play` en el hilo principal [E] |
| X liberado entre la comprobación y el uso | X es del actor del Kliff del anfitrión, que vive mientras dura la conexión; se comprueba en cada llamada [E] |
| Condición de misiones local (`0x1AC10F0(vf40, [inst+0x70])`) | Usa las misiones **del invitado**: puede rechazar aunque la etapa se encuentre [H]; se vería como `found instance ... returned 0` |
| Réplica de `0x1F6C190` sin cerrojo | Lectura SEH; una tabla cambiando a la vez da «no encontrada» (no se redirige) [V código] |
| T3 (NPC con dos escenas) | Solo afecta a S2 (§7.3 del documento del servidor) |
| Coste | A hace dos búsquedas por llamada con id de etapa; las llamadas sin id no llegan a A [V] |

## 6b. Resultado de la sesión 13 (resumen)

C1 en probe: 922/922 llamadas «in X, not in M», todas con la búsqueda nativa en M sin resultado y `0x2126920` = 0 (sin
aviso), para `cd_seq_funcnpc_pub/smithy/grocery/clothes/craftsmit` y otras [V]. Se cumple el paso 1 de §4: la sesión 14
lleva C1 en `1`. Detalle, lo que falta comprobar después de `0x2126920` y la sonda de la gemela S2:
`p8-impl-tenderos-s2.md`.

## 7. Reproducción

- Bytes y ramas: capstone con `C:\Emulador CD\build\research\cdpe.py`/`xref_index.pkl` (`py -I`), scripts en el
  scratchpad de la sesión (desensamblado por rango y comprobación «ninguna rama dentro del parche»).
- Tests: `.\scripts\build.ps1` (41/41 en verde con `optd_shop_c1`).
