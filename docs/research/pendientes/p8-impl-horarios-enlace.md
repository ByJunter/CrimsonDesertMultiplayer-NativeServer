# P8 · Reenlace de los NPC de horario (`guest_sched_bind`)

10 de octubre de 2026, rama `opcion-d` (sobre `b946339`, «Merge guest_sched_route»). Build del juego **1.0.0.2976**
(PE `0x6AB28F00`), direcciones en **RVA** (base `0x140000000`). No se lanzó el juego ni se desplegó nada. `.xtls` solo
se leyó; el gancho nuevo está en `.rsrc`.

**[V]** = comprobado con capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (scripts de
`observador/scripts/`: `pedis.py`, `verify2.py`, `regw.py`, y unos propios en el scratchpad: barrido de
desplazamientos, referencias `E8/E9/0F8x` y punteros absolutos, FNV de cuerpos). **[E]** = interpretación.
**[H]** = pendiente de la prueba en el juego.

Base: `p8-impl-horarios-x.md` §4 (quién enlaza), `p8-impl-horarios-snapshot.md` (la instantánea que suelta 2335
después de los 2005), `p8-s1-escenas-npc.md` §9.2 (las sondas H2 de `0x9F2FA0`, `0x9F30CB`, `0x9F312D`), el informe
independiente `…\npc-raiz-s11-20261010-work\schedules\informe.txt` (que pedía comprobar dueños, cerrojos y argumentos
antes de llamar a `0x9F2FA0`: es lo que hace este documento).

## 0. Resumen

> **Sesión 12 (§10):** `0x8AE1A0` no dio ninguno de los 196 NPC: el lote se ejecutó con el campo cargando y ni los
> NPC ni el Kliff del anfitrión tenían `+0x5E & 0x10`. Ahora esos NPC van a una cola y se reintentan desde el
> fotograma (`0x8B6144`) cuando están conectados y `M+0x231 == 3`. Los 21 «no stage» son ficheros de horario
> descargados antes de que se ejecutara el 2335 (§10.5, pendiente).

- **El hueco [V]:** un NPC solo se enlaza a su escena de horario en la vf13 `0x9ED290` (una vez, al desempaquetar su
  2005) o en `0x9F2FA0` (solo desde el 2874 y la vf107 de la IA, que no actúan en el invitado). Con
  `observer_sched_copy` la instantánea de 2335 del anfitrión se suelta en la puerta (2322 del invitado o 30 s tras su
  2144), es decir **después** de los 2005 de esos NPC: su vf13 no encontró la escena en X y se quedan con
  `comp+0x318 = 0` para siempre. Igual los NPC creados antes de que existiera el Kliff del anfitrión en el cliente.
- **Implementado** `[session2] guest_sched_bind = 0 | probe | 1` (solo invitado; necesita `guest_sched_route=1`, si no
  se apaga con un aviso).
  - Un punto más en el manejador del 2335, **`0xC0B626`**, justo después de `call 0xA9FEE0` (hilo principal, dentro
    del manejador nativo).
  - Solo para el 2335 que el gancho `0xC0B5C1` **enrutó a X en esa misma llamada** (marco y clave comprobados) y solo
    si la escena con esa clave está ya en `[X+0x148]`.
  - Recorre (acotado) la tabla de actores del cliente y toma los NPC con `comp+0x2D8 ==` el personaje,
    `comp+0x2E0 ==` la clave y `comp+0x318 == 0`.
  - `probe`: los lista. `1`: para cada uno (64 como máximo por 2335) `0x8AE1A0` (referencia, la búsqueda nativa con
    cerrojo) → nueva comprobación → **`0x9F2FA0(comp, &clave)`** (la misma llamada que hace el 2874) → lee
    `comp+0x318` → `0x1434880` (suelta la referencia).
- **No se escribe nada a mano** (ni `comp+0x318`, ni `stage+0x178`, ni el `+0x66` de la IA): todo lo hace la función del
  juego.
- **Inis:** `cdmp.p8-s7-shadow.ini` y `cdmp.p8-s7-shadow-fase2.ini` → `guest_sched_bind=1`; `cdmp.p8-s7-host.ini` y
  `cdmp.p8-noche-local.ini` → `guest_sched_bind=0`.
- Compilación sin avisos y **40/40 pruebas OK** (la función nueva `test_guest_sched_bind` va en `optd_seq_copy`).

## 1. El contrato de `0x9F2FA0` [V]

RUNTIME_FUNCTION `0x9F2FA0–0x9F322F`. Llamadores (barrido de `E8/E9` en `.rsrc` + `.xtls` y punteros absolutos):
`0xC0D24D` (`call`, manejador 2874 `0xC0CE50`) y `0x91CE05` (`jmp`, final de la vf107 de la IA `0x91CD90`). Ninguna
vtable lo contiene.

### 1.1 Argumentos

| Registro | Qué es | Prueba |
|---|---|---|
| `rcx` | el `ClientSequencerPlayDataActorComponent` (vtable `0x55BEE78`) | los dos llamadores pasan `[[actor+0x68]+0xB0]` (`0xC0D23A..0xC0D246`, `0x91CDDF..0x91CDEA`); la función usa `[rcx+8]` (actor) y `[rcx+0x2D8]` (dueño, `mov r8d`: 32 bits) |
| `rdx` | puntero a la clave de escena (`u64`) | 2874: `lea rdx,[rbp-0x50]`, copia de la clave del paquete (`0xC0D064`); solo se lee como `r8` de `[X+0x148]->vf8` (`0x9F3122`) |

**Nada más del payload del 2874.** El recurso/nodo (`[rbp-0x60]`) y los 10×4 bytes del 2874 los usa el manejador
antes, para `0x1869D70` (resolver el nodo) y `0x20853D0` (re-fijar la IA); `0x9F2FA0` no los recibe. El nodo («local_res8»)
lo saca la propia función del actor:
- tipo 7 (`[[actor+0x88]+1] == 7`, camino A, `0x9F2FDC`): `[[[actor+0x68]+0x30]+0x50]` (con comprobación de nulo);
- si no (camino B, NPC): el *handle* de la IA `[[[[actor+0x68]+0x58]+0x90]+8]` → `0x15EE200` → `[+0x50 + idx·0x140]`.
  Estas tres lecturas **no** se comprueban (`0x9F3069..0x9F307C`).

Sale sin hacer nada si el nodo no es válido (`0x9F30CB`, `0x9F30D5`) o si `0x8AE1A0` no encuentra al dueño
(`0x9F30FB`).

### 1.2 Cuerpo, comparado con la vf13 `0x9ED290`

Las dos funciones son el mismo código salvo lo marcado:

| Paso | vf13 `0x9ED290` | `0x9F2FA0` |
|---|---|---|
| Clave | `comp+0x2E0` (sale si `& 0x1FFFFFFF == 0`) | `[rdx]` |
| `comp+0x3D9` | lo pone a 0 al entrar y a 1 si la etapa no está activa (`0x9ED568`) | **no lo toca** |
| Nodo (local_res8) | igual (caminos A/B, guardas `0x393DF0` del actor) | igual |
| Dueño | `0x8AE1A0([[0x6D69190]+0x30], comp+0x2D8)` | igual |
| X | `[[[dueño+0x68]+0xA8]+0x18]`, `[X+0x148]->vf8(&etapa, clave)` **sin comprobar `[X+0x148]`** | igual |
| Enlace | `0x43B4B0(etapa, &nodo, actor)` | igual |
| `comp+0x318` | guarda exclusiva `0x393650(…,1)`; si cambia: suelta la anterior (`0x38AD90`), pone la nueva, `+0x10`++, `+0x16 = 1` | igual |
| Sin etapa | tipo ≠ 7: `[[ai+0x90]+0x66] = ([[ai+0x50]+0x41] != 0)` | nada |
| Al final | suelta la etapa (`0x38AD90`) y el *holder* del dueño (`0x1434880` con vtable `0x5725E48`) | igual |

- **`comp+0x3D9` [V/E]:** solo lo lee la vf16 del componente (`0x9ED670 → .xtls 0x9C81520`): «esperando a que la etapa
  se active». Con `0x9F2FA0` queda en 0 (lo dejó así la vf13 al no encontrar etapa). No hace falta: si la etapa ya está
  activa, `0x43B4B0` da las referencias en el acto; si no, `0x43CEF0` (fin de carga) recorre los nodos con NPC
  (`+0x28`) y se las da (`p8-impl-horarios-x.md` §4.3). Es lo mismo que pasa con el 2874 nativo.
- **`+0x66` de la IA [V/H]:** el constructor de X1 (`0x1EB7B70`, `0x1EB7ED5`) y su *reset* (`0x1EB83D0 → .xtls
  0xDB94B10`, `0xDB94B86`) lo ponen a 2. La vf13 sin etapa lo pone a 0/1. `0x2086000` y `0x20863A0` fallan si no es 2,
  y `0x2083470` lo copia a otro estado (`+0x2EC`). Los dos caminos nativos de reenlace (2874 y vf107) llaman antes a
  `0x20853D0` (que hace ese *reset*); `guest_sched_bind` **no** (necesitaría el nodo y la lista del 2874). Por eso el
  NPC reenlazado puede seguir con `+0x66 = 0/1`. Cada línea de llamada lo registra (`AI mode [X1+0x66]`) para medirlo.

### 1.3 Lo que hace `0x43B4B0` (el enlace en sí) [V]

`0x43B4B0(etapa, &nodo, actor)`, `0x43B4B0–0x43B781`:
1. `0x1869D70` resuelve el nodo.
2. Busca en `etapa+0x138` (paso `0x168`) el nodo con el mismo fichero/índice. **Solo si `nodo+0x28 == 0`** pone
   `nodo+0x28 = actor+0x60` y `nodo+0x34 = 0`; si ya lo tiene otro actor, no toca el nodo (pero `0x9F2FA0` pone igual
   `comp+0x318`).
3. Si el actor no es tipo 7 y `etapa+0x178 != 0`:
   - junta las referencias del nodo (`+0x120`, vector en la pila de 32; crece en el montón del juego con
     `0x43E160` solo si hay más);
   - se las da a `0xA53CC0([[actor+0x68]+0x40]+0xE8, …)`;
   - si `actor+0xC2` y `actor+0xCF`, pide a la IA su vf50 (`[[[actor+0x68]+0x58]]+0x190`).

No hay envío C>S en el cuerpo. La petición a la IA es el mismo efecto que tiene hoy la vf13 nativa en el invitado
cuando el 2335 llega antes que el 2005.

### 1.4 Precondiciones, hilo y cerrojos

| Precondición | Cómo se cumple |
|---|---|
| Hilo principal, contexto de un manejador Troc | el punto está dentro del manejador del 2335; el 2874 llama a `0x9F2FA0` desde el mismo despacho |
| Ningún cerrojo del juego tomado de forma que se bloquee | en `0xC0B626` `0xA9FEE0` ya soltó sus guardas; el manejador solo tiene un *holder* (referencia, sin cerrojo) del Kliff y otro del campo. `0x8B3C00`/`0x8AE1A0` sueltan el cerrojo del gestor de actores antes de volver |
| Reino TLS de cliente (`[[gs:0x58]]+0x1EC == 0`) | se comprueba antes (igual que `guest_sched_load`) |
| `[X'+0x148] != 0`, X' = el X del dueño que da **su** búsqueda | la etapa con la clave está en `[X+0x148]` (X del gancho de ruta) **y** el actor del personaje en la tabla tiene ese mismo X; si no, no se llama (`the character's X in the table not the routed X`) |
| Camino de nodo legible (las tres lecturas sin comprobar) | `[actor+0x88]`; tipo 7: `[[actor+0x68]+0x30]`; si no, `[[actor+0x68]+0x58]`, su `+0x90` y ese `+8` no nulos (`no AI node path`) |
| Actor vivo durante la llamada | lo coge `0x8AE1A0` (cerrojo del gestor, `actor+0x5E & 0x10`, referencia con la vf24 del actor) y se comprueba que es el mismo puntero y el mismo componente, con `comp+0x318` aún 0 |
| Montón del juego | `0x9F2FA0`/`0x43B4B0` asignan en el montón del juego (TLS `gs:[0x58]`, índice `0x1FD`) como cuando los llama el 2874: es el hilo del juego |

### 1.5 ¿Es inocuo llamarla de más?

- **`comp+0x318 != 0`:** la función suelta la etapa anterior y pone la nueva, y vuelve a pasar por `0x43B4B0` (que no
  toca un nodo ya ocupado). No es un no-op: **no se llama** (`already bound`).
- **Etapa ausente en X:** `vf8` devuelve 0 y se salta todo hasta soltar el *holder* (`0x9F3134 → 0x9F31FC`): inocuo.
  Pero `[X+0x148] == 0` sería un fallo (lectura de `[0]`): se comprueba antes.
- **Nodo inválido / dueño no encontrado:** sale al principio, sin efectos (`still 0` en el log).

## 2. Por qué en `0xC0B626` y no en otro sitio

- Es el mismo hilo y el mismo tipo de contexto que el 2874 nativo (un manejador Troc del hilo principal).
- La etapa acaba de crearse en X: no hace falta un reintento periódico ni un hilo del mod que llame al juego (prohibido:
  los hilos del mod no llaman al juego).
- Llamar a funciones del juego desde el *callback* de un gancho medio es seguro en cuanto a ABI [V, safetyhook v0.7.0
  `mid_hook.cpp`]: el *stub* guarda todos los registros generales, las banderas y `xmm0–15`, alinea la pila a 16
  (`and rsp,-16`) y reserva `0x30` de *shadow space* antes del `call`; al volver lo restaura todo.
- **Excepción dentro del juego:** no se trata (regla I1, `game_fault.h`). El filtro apaga la función para siempre
  (`LATCHED OFF`), escribe una línea ERR directa y la deja pasar. Puede haber cerrojos del actor o del gestor tomados:
  tragársela dejaría el juego colgado más tarde.
- **Descartado:**
  - el punto justo tras el `call` (`0xC0B61E`): su segunda instrucción es `lea rax,[rip+…]`, `verify2` lo rechaza;
  - diferirlo a otro manejador: no hay ninguno que llegue con la etapa recién creada y el mismo X.

## 3. El punto [V]

```
0xC0B612  48 8B 55 D8                mov rdx,[rbp-0x28]
0xC0B616  48 8B C8                   mov rcx,rax
0xC0B619  E8 C2 48 E9 FF             call 0xA9FEE0
0xC0B61E  90                         nop
0xC0B61F  48 8D 05 3A 78 D0 04       lea rax,[rip+…]          (0x5912E60)
0xC0B626  48 89 45 D0                mov [rbp-0x30],rax       ← PUNTO (8 bytes)
0xC0B62A  80 7D E0 00                cmp byte [rbp-0x20],0
0xC0B62E  74 0E                      je 0xC0B63E
```

- `verify2.py C0B626:8` → `VERDICT OK`:
  - instrucciones enteras desde el inicio de la función;
  - ninguna rama de la función ni `rel32` real de `.rsrc`/`.xtls`/`.text` dentro de `(0xC0B626, 0xC0B62E)`;
  - sin RIP-relativo.
- `regw.py C0B5C1 C0B626 rbp`: `rbp` no se escribe y ninguna rama entra en el tramo. Además, `rbp` no se escribe en
  toda la función tras `0xC0AFD0`. De `0xC0B5C1` a `0xC0B626` el código es lineal y la única llamada es `0xA9FEE0`.
- Bytes verificados al instalar:

  | Dirección | Bytes |
  |---|---|
  | `0xC0B626` (16) | `48 89 45 D0 80 7D E0 00 74 0E 48 8B 4D F8 48 8B` |
  | `0xC0B612` (20) | `48 8B 55 D8 48 8B C8 E8 C2 48 E9 FF 90 48 8D 05 3A 78 D0 04` |
  | `0xC0AFD0` (8) | `lea rbp,[rsp-0x10A0]` |
  | `0xC0B242` (8) | la clave a `[rbp-0x38]` |
  | `0xC0B5EC` (9) | `lea rcx,[rbp-0x38] ; mov [rsp+0x30],rcx`: la clave va a `0xA9FEE0` por dirección |

  `p8_install_mid` exige que safetyhook reubique exactamente 8 bytes.
- Cuerpos comparados (FNV-1a 64) antes de instalar:

  | Función | Tramo | FNV-1a 64 |
  |---|---|---|
  | `0x9F2FA0` | `–0x9F322F` | `D15E3D78D6C57955` |
  | `0x8AE1A0` | `–0x8AE385` | `CBE3B75431B9931B` |
  | `0x1434880` | `–0x143491C` | `3FA7949A2E381275` |
  | `0x43B4B0` | `–0x43B781` | `06AD352D3DCC67B3` |

  El método da el mismo valor que `guest_sched_load` para `0x38AD90`.
- Ningún otro módulo engancha `0xC0B5C1..0xC0B62E` salvo `guest_seq_route` en `0xC0B5C1` (9 bytes, no se solapan).

### 3.1 Emparejado con `0xC0B5C1`

- **Pre:** `guest_sched_route_hook` (2335), en **cada** 2335 que juzga, llama a `guest_sched_bind_note`. Con `Route`
  deja una nota armada en una variable `thread_local`:
  - `rbp`, clave, X, personaje y recurso.
  - Si no hay `Route`, la deja vacía.
- **Post** (`0xC0B626`): consume la nota siempre.
  - Sin nota armada → nada.
  - `rbp` distinto o `[rbp-0x38]` distinto de la clave → `frame mismatch`, nada.
  - Una nota vieja (p. ej. si `0xA9FEE0` lanzara) no sobrevive: el siguiente 2335 la reescribe antes de llegar aquí.

## 4. El recorrido y la llamada

1. La etapa: `[X+0x148]+0x18` (≤ 1024), la primera con `+0x30 == clave`. Si no hay → línea `NO stage with that key`
   (p. ej. `0xA9FEE0` deduplicó por *wake spot*, o un 2335 de otro tipo que no crea).
2. La tabla `[[0x6D69190]+0x30]+8` (la misma que usan `0x9F2FA0` y `game::find_actor`), cubeta por cubeta:
   - ≤ 65536 cubetas y ≤ 16384 entradas;
   - nodo con la clave correcta, como `0x464290`.
   - Solo lecturas SEH y solo **recoge** claves y punteros. Para el actor del personaje anota su X.
3. Por actor: componente (`[[a+0x68]+0xB0]`, vtable `0x55BEE78`, `[comp+8] == a`) → dueño → escena → `+0x318` →
   camino de nodo. Solo se cuenta lo que es de este personaje y esta escena.
4. Si el X del personaje en la tabla no es el X enrutado → no se llama a nada.
5. `1`: por candidato (≤ 64), en este orden:
   - `0x8AE1A0(gestor, &holder, clave)`; el *holder* va en la pila del mod (`0x20` bytes, alineado);
   - si no hay referencia o el actor no es el mismo → `not found`;
   - se repiten las lecturas; si algo cambió → `changed`;
   - `0x9F2FA0(comp, &clave)` (la clave en la pila del mod);
   - se lee `comp+0x318` y, de esa etapa, `+0x30`/`+0x28` → `BOUND` / `still 0` / `ANOTHER stage`;
   - se busca el NPC en los nodos de la etapa (`a node … now carries the NPC`);
   - `holder+0 = exe+0x5725E48` y `0x1434880(&holder)`: lo mismo que hace `0x9F3206` con el suyo.

Coste: un recorrido por 2335 enrutado. Las lecturas SEH sin fallo son baratas: con ~2000 actores y ~2000 cubetas,
unas 10 000 lecturas (≈ 0,1 ms **[E]**). Las llamadas solo se hacen para los candidatos.

## 5. Qué mirar en el log del invitado

1. `GuestSchedBind ON (guest_sched_bind): point 0xC0B626 … bodies checked: 0x9F2FA0 0x8AE1A0 0x1434880 0x43B4B0`.
   Si sale `NOT installed (…)`, el motivo; con `guest_sched_route` distinto de 1, `needs guest_sched_route=1`.
2. Al soltarse la instantánea (tras el 2322 del invitado):
   - una línea `GuestSchedBind [2335 0xC0B626] … its NPCs: N with comp+0x318 0, M already bound (K to this stage:
     vf13 found it) … called C: bound B, still 0 S` por cada 2335 con NPC afectados;
   - por NPC, `GuestSchedBind [0x9F2FA0] NPC 0xB… … BOUND to the stage in X: comp+0x318 0x… , a node of it now
     carries the NPC (0x43B4B0)`.
3. `still 0` = el nodo de la IA no se resolvió (fichero de horario no cargado: `guest_sched_load`), o el dueño no se
   encontró. Mirar `AI scene key [X1] … (NOT this scene)` y `GuestSchedLoad`.
4. `AI mode [X1+0x66]`: 2 es lo normal; 0/1 = lo dejó así la vf13 sin etapa (§1.2, §9). El resumen da el reparto de
   los enlazados (`AI mode X1+0x66 of the bound: 0 …, 1 …, 2 …`).
5. `GuestSchedRoute [census]`: «bound to an NPC» de X debe subir tras la instantánea. Los NPC vigilados (G5) deben
   pasar a `bound stage +0x318 … manager +0x28 = X`.
6. `GuestSchedBind summary` (60 s), con los contadores:
   - `calls`, `BOUND`, `still 0`;
   - `over the cap`;
   - `the character's X in the table not the routed X`;
   - `frame mismatch` (debe ser 0);
   - `faults` (debe ser 0).
7. Lo visual: los NPC de horario que antes se quedaban quietos tras la entrada (barrenderas, cestas, fauna de la
   instantánea) empiezan su rutina, como en el anfitrión.

Con `probe` las líneas son `GuestSchedBind [probe 0xC0B626] … WOULD call 0x9F2FA0(comp, &scene)` y nada se llama.
Conviene una pasada en `probe` si se quiere ver el número de candidatos antes.

## 6. Criterios de aborto

Pasar a `guest_sched_bind=probe` (solo lista); si sigue mal, `0`. Motivos:
- cierre del juego (una línea ERR `GuestSchedBind: exception …` dice dónde) o `LATCHED OFF`;
- `faults` distinto de 0;
- tirones al soltarse la instantánea (64 llamadas por 2335 como máximo, pero puede haber decenas de 2335 seguidos);
- NPC que tiemblan, saltan de pose o hacen dos cosas a la vez (dos productores de acción: la IA local pedida por
  `0x43B4B0` y el anfitrión);
- `bound to ANOTHER stage` o `frame mismatch` distintos de 0;
- el censo de X con `of another manager` distinto de 0.

## 7. Riesgos

- **IA local [H]:**
  - `0x43B4B0` pide a la IA del NPC su vf50 si la etapa está activa;
  - la vf13 nativa del invitado ya lo hace hoy cuando el 2335 llega antes que el 2005, así que no es un efecto nuevo,
    pero ahora puede afectar a muchos NPC a la vez.
- **`+0x66` de la IA [H]:** §1.2. El reenlace nativo lo reinicia y este no.
- **Más de 64 NPC en una escena:** los que pasan el tope no se enlazan hasta el siguiente 2335 de esa clave (contador
  `over the cap`).
- **Nodo ocupado:** si otro actor ya tiene el nodo, la función pone `comp+0x318` sin enlazar el nodo, como la vf13. Se
  ve como `NO node carries the NPC`.
- **Tabla de actores sin cerrojo:** el recorrido solo recoge. Todo lo que se llama pasa antes por `0x8AE1A0` con
  cerrojo y referencia, y se vuelve a comprobar. Una lectura rota solo hace perder un candidato.
- **Excepción dentro del juego:** cierre real (regla I1), con la línea ERR. El *stub* del gancho no tiene datos de
  *unwind*: una excepción que lo cruce no se puede tratar más arriba. Es lo mismo que en `guest_sched_load`.

## 8. Código y pruebas

- `src/mod/session2/guest_sched_bind.{h,cpp}`: el punto, el emparejado, el recorrido, las llamadas protegidas, las
  líneas y el resumen.
- `src/mod/session2/guest_seq_route_logic.h` (sección `guest_sched_bind`), la lógica pura:
  - `bind_mode_of`, `BindPending`/`bind_pending_matches`;
  - `BindActor`/`bind_judge`, `bind_result`;
  - `walk_actor_table` (plantilla sobre una memoria abstracta), `BindTally`, `kBindCap`.
- Lo demás:
  - `guest_seq_route.{h,cpp}`: la nota en el gancho 2335, la instalación tras los puntos de horario, el *tick* y el
    *describe*;
  - `config.{h,cpp}`: la clave `guest_sched_bind`; `guest.cpp`: se la pasa a `guest_seq_route_start`.
- `tests/optd_seq_copy_tests.cpp` (`test_guest_sched_bind`):
  - el modo (rechazado sin `guest_sched_route=1`);
  - el emparejado (marco, clave, X y dueño);
  - cada motivo de `bind_judge`, los tres resultados;
  - el recorrido sobre una memoria falsa: nodo viejo, índice sin nodo, cubeta sin contador, tope de entradas, tope de
    cubetas, tabla ausente, parada por el *callback* y contador de cubeta > 31.
- `.\scripts\build.ps1`: compilación sin avisos, **40/40 pruebas OK**.

## 9. `X1+0x66` de la IA: decisión (revisión del 10-10)

- **Qué es [V]:**
  - el constructor de X1 (`0x1EB7B70`, `0x1EB7ED5`) y su *reset* (`0x1EB83D0 → .xtls 0xDB94B10`, `0xDB94B86`) lo
    ponen a 2;
  - la vf13 sin etapa lo pone a `([[[actor+0x68]+0x58]+0x50]+0x41 != 0)`, es decir 0/1 (`0x9ED5A2..0x9ED5D7`);
  - lo leen `0x2086000` y `0x20863A0` (`cmp byte [X1+0x66],2`, `0x208620A` / `0x20866E0`), que si no es 2 escriben un
    error (`[0x6CDD1D8]`) en su salida y vuelven;
  - lo lee `0x2083470` (`0x208377E`), que lo copia a otro estado (`+0x2EC`).
- **Para qué sirven [E]:** `0x2086000` / `0x20863A0` son consultas del nodo de horario del NPC: toman el campo
  (`0x17FA6C0`), el *handle* de la IA (`0x15EE200`) y devuelven un código. Los llama la lógica de comportamiento
  (`0x1716FF5`, `0x171731F`, `0x1720C92`, `0x172F710`, `0x22E4D9B`, y `.xtls`). Con 0/1 esas consultas fallan: la IA
  **local** del NPC no consulta su nodo de horario.
- **En el invitado [E]:**
  - quien mueve a un NPC del anfitrión es el anfitrión (`remote_motion`/`remote_interp`, acciones del servidor), no la
    IA local;
  - que la IA local no consulte su nodo **reduce** el riesgo de dos productores de acción (§7), no lo aumenta;
  - lo visible (la rutina) llega por las acciones del anfitrión;
  - el enlace sí importa para lo que da `0x43B4B0`/`0x43CEF0` (referencias de la etapa a los componentes del NPC), y
    eso no depende de `+0x66`.
- **Volver a 2 sin escribir a mano exigiría `0x20853D0(ia, &nodo, &clave, &lista, 0x6A658E0)`:**
  - el nodo (`0x1869D70` sobre el `recurso|nodo` del 2874) y la lista de 10 × 4 bytes del 2874 no los tenemos;
  - antes reinicia X1 (`0x1EB83D0`: suelta la referencia del *handle* `+8`+`0x10` y vacía las listas).
  - Copiar el *handle* actual sin sumar referencia es inseguro; reconstruirlo, inventar datos. **No se implementa.**
- **Medida:** cada línea `GuestSchedBind [0x9F2FA0]` lleva `AI mode [X1+0x66]` y el resumen el reparto (0 / 1 / 2 /
  otro) de los NPC enlazados.
  - Si en Shadow los NPC enlazados con 0/1 se ven distintos de los que la vf13 enlazó de forma nativa (con 2), se
    estudia `0x20853D0` con datos reales.
  - Una opción sería guardar en el invitado el nodo y la lista del último 2874 del NPC, si llega alguno; en S11 no
    llegó ninguno.

## 10. Sesión 12: `0x8AE1A0` no dio ningún NPC (corrección: cola de reintento)

Rama `p8-sched-bind-fix` (sobre `opcion-d` `f76ab6b`). Logs: invitado
`backups\p8-sesion12-shadow\cdmp_20261010-215406.log`, anfitrión `backups\p8-sesion12-host\cdmp_20261010-225143.log`.
No se lanzó el juego.

### 10.1 Lo que pasó

- `guest_sched_route` funcionó: X acabó con 33 etapas y ~118 nodos con NPC.
- `guest_sched_bind` no llamó nunca a `0x9F2FA0`:
  - 22 pasadas con etapa (de 43 2335 enrutados; 21 sin etapa, §10.5);
  - 196 candidatos (`comp+0x318 0`), los 196 con `not called: the game's lookup 0x8AE1A0 did not give this actor`;
  - todos con `+0xC2 0`, `+0xCF 0`, etapa `+0x178 0`, `[X1]` = la escena y `X1+0x66 = 2`.

### 10.2 Causa [V código, V log, E para el dueño]

**No es la convención de llamada [V].** `0x8AE1A0(gestor rcx, holder* rdx, u32 clave r8d)` es la misma llamada que
hacen `0x9F2FA0` (`0x9F30DF..0x9F30F1`) y la vf13 (`0x9ED290`):
- el gestor es `[[0x6D69190]+0x30]`; la búsqueda usa `[gestor+8]` con `0x464290` (`0x8AE2B8`), la misma tabla que
  recorre el mod;
- no depende del reino TLS: recibe el gestor explícito, y el reino era el de cliente (`realm not client 0`);
- el *holder* es `{vt, actor +8, ref +0x10, +0x11, +0x18}` (`0x8AE2EF..0x8AE319`).

**La condición que falla [V capstone]:**
```
0x8AE2BC  call 0x464290            ; el actor de la clave en [gestor+8]
0x8AE2C4  test rax,rax ; je 0x8AE331
0x8AE2C9  test byte [rax+0x5E],0x10 ; je 0x8AE331   ; ← sin «logged in»: holder VACÍO (+8 = 0, +0x10 = 0)
0x8AE2CF  call [actor vf24](4, 0x10) → holder+0x10
```
`u16 actor+0x5E` es el estado del actor (`0x10` = conectado y cargado, `0x20` saliendo, `0x40` liberado;
`guest_lifecycle.h`). `0x8AE1A0` es un `UserOrCharacterValidLoginedScopeAttacher`: no da un actor que no esté
«logined».

**El estado en ese momento [V log]:**
- Los 2335 de la instantánea se ejecutaron en `qpc 431.02`, con el campo del invitado aún cargando (`[P+0x15C] 0x0F`).
  El cliente los ejecutó ~3,2 s después de entregarlos (`427.79`); `p8-s12-npc-familias.md` (familia b) lo explica:
  el cliente difiere los mensajes de escena hasta el final de la carga.
- La lista de espera de la carga en ese instante: 194 NPC, **184 NOT READY** con `+5E 0x0008 C2 0 CF 0`.
- Sus 2005 se habían entregado en `427.8` (3 s antes): son los NPC creados durante la carga del campo.
- La lista se vació en el paso `0x11` (`433.68`, +2,7 s); la carga terminó (`0x18`) en `442.9`. `M+0x231` volvió a 3
  entre `451` y `456` (`GuestStage [M+0x231 change]`).

**El dueño también [V log / E]:**
- `0x9F2FA0` busca a su **dueño** con el mismo `0x8AE1A0` (`0x9F30DF`) y sale si no lo da (`0x9F30FB`).
- La vf13 de esos mismos NPC (en `427.8`) pasó su comprobación de nodo («passes: the owner actor is looked up next»)
  y **nunca** llegó a su sonda del dueño `0x9ED453`. La primera línea `GuestStage [NPC owner 0x9ED453]` es de
  `465.78`, ya cargado: durante la carga la búsqueda del Kliff del anfitrión (`0xA0100001`) fallaba.
- El Kliff sí estaba en la tabla: el recorrido lo encontró en las 22 pasadas, con su gestor = X (`the character's X
  in the table not the routed X 0`). Así que no falta en la tabla ni es otro reino: falla la misma condición
  (`+0x5E & 0x10`, o la vf24) **[E]**. Ahora cada línea registra los dos bits («logged in … its owner …») **[H]**.
- Conclusión: arreglar solo la búsqueda del NPC no bastaba. `0x9F2FA0` habría salido en `0x9F30FB` (`still 0`).

### 10.3 La corrección

**Diseño:** llamar más tarde, en el hilo principal, cuando el NPC **y** su dueño estén conectados y la carga propia
haya terminado.

- **Primer intento (`0xC0B626`), igual que antes salvo que:**
  - si el NPC o el dueño no tienen `+0x5E & 0x10`, no se llama a nada y el NPC va a la **cola**
    (`DEFERRED … queued for a retry`);
  - si `0x8AE1A0` lo rechaza a pesar del bit, también a la cola;
  - los candidatos que pasan del tope de 64 también van a la cola (antes esperaban a otro 2335).
- **La cola** (`gsr::BindQueue`, 512 como máximo; 196 en S12):
  - una entrada por clave de NPC (`clave, dueño, escena, X, desde, intentos`);
  - solo la usa el hilo principal; un `atomic` evita la entrada de un segundo hilo (la pasada se salta, no espera).
- **Dónde se reintenta (hilo principal, contexto nativo):**
  - el gancho por fotograma de `guest_seq_play`, `0x8B6144` (`guest_sched_bind_frame(RAX = M)`), antes del tick
    nativo. Es el bucle principal (`0xAD2380 → 0x8B6060`), el mismo hilo que los manejadores Troc (`T12216` en S12
    para los dos). `guest_seq_play` ya llama ahí a `0xA953C0` (que bloquea `[G+8]`), con el `FieldAttacher` Q retenido:
    tomar cerrojos de actor allí es lo que hace el juego **[E]**;
  - sin ese gancho (`guest_seq_play=0`), al llegar el siguiente 2335 enrutado (`0xC0B626`).
- **Cada pasada** (cada 250 ms como mucho, 32 llamadas como mucho):
  1. reino TLS de cliente;
  2. un recorrido acotado de la tabla de actores; si la tabla no se lee entera, no se decide nada;
  3. por NPC, `gsr::retry_judge`:
     - `Gone`: ya no está en la tabla;
     - `Changed`: otro dueño, otra escena o sin componente;
     - `BoundMeanwhile`: `comp+0x318` ya no es 0 (lo enlazó el juego);
     - `NoStage`: la etapa ya no está en X (un 2126);
     - `OwnerMoved`: el X del Kliff ya no es ese X;
     - `Call`: NPC y dueño con `+0x5E & 0x10`, `M+0x231 == 3` y camino de nodo de la IA presente;
     - `Wait`; `TimedOut` a los 120 s.
  4. `Call` → lo mismo que en `0xC0B626`: `0x8AE1A0` (referencia), nueva comprobación, `0x9F2FA0(comp, &escena)`,
     `comp+0x318` leído y `0x1434880`.
     - `0x8AE1A0` aún vacío: sigue en la cola.
     - `still 0`: sigue en la cola hasta 3 veces.
- **`M+0x231 == 3`:** cada pasada lo lee de su M, con la vtable comprobada; sin M no se llama (§10.9):
  - en el fotograma, el RAX de `0x8B6144`;
  - en `0xC0B626`, el M de la vf48 de ese 2335.
- **En S12 [E]:** el reintento habría llamado hacia `451–456 s`, ~25 s después del 2335, con el Kliff conectado.
- **Nada se escribe a mano.** No hay ganchos nuevos ni bytes nuevos: el punto `0xC0B626` y el gancho `0x8B6144` ya
  estaban verificados.

### 10.4 ¿Sirve enlazar a una etapa inactiva? (`+0x178 0`) [V código, H efecto]

- `0x43B4B0` (desde `0x9F2FA0`):
  - pone el nodo (`nodo+0x28 = actor+0x60`) aunque la etapa no esté activa;
  - solo con `etapa+0x178 != 0` da al NPC las referencias del nodo (`[[[actor+0x68]+0x40]+0xE8]`, vía el *thunk*
    `0x9E4E930`);
  - solo si además `+0xC2` y `+0xCF` pide la vf50 a la IA.
- `0x43CEF0` (fin de la carga de la etapa):
  - pone `etapa+0x178 = 1` (`0x43D691`);
  - recorre los nodos con NPC (`+0x28 != 0`) y referencias (`+0x128 != 0`) (`0x43D6F0..0x43D721`);
  - busca cada actor con la vf+0x10 del raíz de cliente `[[0x6D69518]]` o de servidor `[[0x6D69520]]`, según el
    TLS `+0x1EC` (`0x43D727..0x43D752`);
  - le da las mismas referencias y, si `+0xC2/+0xCF`, la vf50.
- **Es decir:** un NPC enlazado antes de la activación recibe sus referencias en `0x43CEF0`, si esa búsqueda lo da
  en ese momento. Que esa búsqueda tenga la misma condición «logined» es **[H]**: no se resolvió la vtable del raíz.
  - Un NPC enlazado (por nosotros) **antes** de estar conectado podría quedarse sin referencias si la etapa se activa
    mientras carga.
  - Con la cola eso no pasa: se llama cuando el NPC ya está conectado.
- **En S12 [V log]:** las 22 etapas estaban activas ya en el primer censo tras el lote (`21:55:42`, `22 active
  +0x178`).
  - Los reintentos encontrarán la etapa activa: `0x43B4B0` da las referencias en el acto.
  - `+0xCF` probablemente seguirá a 0 (espera a su escena: `comp+0x3D8 = 1`), así que sin vf50, como en un 2874
    nativo con el NPC aún no listo.
- `0x9F2FA0` no pone `comp+0x3D9` (la vf13 sí, si la etapa no está activa): no hace falta con la etapa activa.
- El resumen lo mide: `stage active +0x178 at the retried calls: 0 …, 1 …`.

### 10.5 Los 21 «no stage in X» [V log, E causa]

- Son 21 2335 de **20 recursos** (`0x1295A470` sale dos veces, escenas `0x2000017A` y `0x20000182`).
- Los 20 tienen la misma historia:
  - `GuestSchedLoad [load] … LOADED` en `427.79`, en el NetworkThread al entregarse el paquete;
  - `GuestSchedLoad [lookup miss] … 0x1862A80 (2335 / stage lookup) found no file (… its state: LOADED …)` en
    `431.02`, el mismo instante en que `0xA9FEE0` tenía que crear la etapa.
- El contenedor tenía 15 ficheros en `431.70`, cuando se habían cargado 35.
- **No es** deduplicación por *wake spot* ni otro tipo de 2335: el fichero de horario ya no estaba.
- **Causa probable [E]:**
  - `guest_schedule_load` carga y suelta su *handle* en el acto (`0x38AD90`);
  - el contenedor descarga los ficheros ociosos. Cada búsqueda `0x186A810` pone a 0 el contador de inactividad del
    *holder* (`holder+0x10`), así que lo que nadie busca envejece;
  - como el cliente ejecutó el 2335 3,2 s después de entregarlo, 20 de esos ficheros se descargaron antes.
  - El umbral y el recolector están en `.xtls`: no comprobado.
- **No se corrige aquí.** Cargar en el hilo principal (en `0xC0B5C1`, justo antes de `0xA9FEE0`) sería llamar al
  cargador `0x1864140` desde un contexto en el que el juego nunca lo llama: sus 6 llamadores nativos son código de
  servidor (`0x27EA160`, `0x2848CE0`, `0x2849400`, `0x281B700`, `0x288A970`, `0x292A410`).
- **Propuesta (pendiente):**
  - que `guest_schedule_load` guarde el *handle* de los ficheros que nombra un 2335 hasta que ese 2335 se ejecute
    (lo marca `0xC0B626`) o 30 s;
  - antes hay que confirmar que una referencia viva impide la descarga (recolector en `.xtls`).
- Los NPC de esas 21 escenas no tienen etapa a la que enlazarse. La cola no los toca: nunca son candidatos, porque no
  hay etapa.

### 10.6 Qué mirar en el log (S13)

1. `GuestSchedBind ON … queued and retried from the frame hook 0x8B6144 …`.
2. En el lote de la instantánea: líneas `[0x9F2FA0] … logged in (+0x5E & 0x10) 0, its owner 0 … DEFERRED …`.
   Confirman el bit del NPC y el del dueño (§10.2 [E]).
3. Al acabar la carga: `GuestSchedBind [retry frame 0x8B6144] … queued N ms ago … 0x9F2FA0: BOUND to the stage in X
   … a node of it now carries the NPC`.
4. `GuestSchedBind retry summary` (60 s):
   - `queue now 0`;
   - `outcomes`: sobre todo `called`, pocos `bound meanwhile`, ningún `timed out`;
   - `retried calls`: `BOUND` cerca de 196;
   - `refused by 0x8AE1A0 although logged in 0`;
   - `this client's M+0x231 3`;
   - `frames seen` > 0.
5. Censo de X: `bound to an NPC` debe subir de ~118 hacia ~300.
6. **Abortar** (`guest_sched_bind=probe`) si hay `LATCHED OFF`, `faults` distinto de 0, un tirón al acabar la carga
   (32 llamadas por pasada, 4 pasadas por segundo como mucho) o NPC con dos acciones a la vez (§6).

### 10.7 Riesgos

- **Contexto `0x8B6144` [E]:**
  - el juego no llama a `0x9F2FA0` desde ahí;
  - sí llama ahí a `0xA953C0` (vía `guest_seq_play`, y el juego justo después con M), que toma el cerrojo de actor
    `[G+8]`: es un sitio donde el juego ya toma cerrojos de actor;
  - no hay cerrojo tomado en el punto (Q es una referencia, `p8-impl-escenas-play.md` §1.1).
- **Un NPC enlazado con 25 s de retraso** pasa a ser servido por la etapa en mitad de su vida local. Es lo mismo que
  hace un 2874 nativo, pero ahora con hasta 196 NPC en pocos segundos.
- **`X1+0x66`:** como en §9, no se reinicia.
- **Sin `guest_seq_play`:** la cola solo avanza con 2335 enrutados, que tras la instantánea pueden tardar minutos
  (en S12, 4 min). Las entradas caducan a los 120 s. Los inis de Shadow tienen `guest_seq_play=1`.
- **Inis sin cambios.**

### 10.8 Código y pruebas

- `guest_seq_route_logic.h`:
  - `BindResult::Deferred`;
  - `BindRetry`, `RetryFacts`, `RetryStep` / `retry_judge`, `retry_due`, `BindQueue`;
  - las constantes `kRetry*`.
- `guest_sched_bind.{h,cpp}`:
  - `call_one` (la llamada factorizada), `queue_npc`, `retry_pass`, `guest_sched_bind_frame`;
  - las líneas `[retry …]` y el resumen `retry summary`;
  - los bits «logged in» en las líneas de llamada.
- `guest_seq_play.cpp`: `on_frame` llama a `guest_sched_bind_frame(ctx.rax)` antes del cuerpo del fotograma.
- `tests/optd_seq_copy_tests.cpp` (`test_guest_sched_bind`):
  - la cola (deduplicado, lleno, quitar);
  - cada paso de `retry_judge`, con el dueño y `M+0x231`;
  - `retry_due`.
- `.\scripts\build.ps1`: sin avisos, **40/40 OK**.

### 10.9 Revisión de `51f9599` (rama `p8-sched-bind-fix-2`, sobre `opcion-d` `2997dbb`)

1. **MEDIA, corregido.** Antes había dos problemas:
   - `retry_judge` devolvía `Call` antes de mirar el plazo: `TimedOut` solo se alcanzaba desde `Wait`;
   - si `0x8AE1A0` rechazaba un NPC tras la llamada, solo se sumaba un intento, sin límite.

   Con 32 o más NPC así, se llamaban cada 250 ms para siempre y gastaban todo el cupo de la pasada. Como el recorrido
   empezaba siempre por el final, los demás no se enlazaban ni caducaban.
   - **El orden de `retry_judge` ahora:**
     1. lo que ya no aplica (`Gone`, `BoundMeanwhile`, `Changed`, `NoStage`, `OwnerMoved`);
     2. el plazo (`TimedOut`, aunque estuviera listo para llamarse);
     3. `Call` o `Wait`.
   - **Tras una llamada** (`gsr::retry_keep_after`):
     - `0x8AE1A0` sin referencia: sigue en la cola hasta 3 veces (`kRetryRefusedMax`); después sale con el motivo
       `Refused`;
     - `still 0`: igual, 3 veces, y sale con el motivo `StillZero`.
   - **La pasada** ahora va en tres fases:
     1. juzga todas las entradas (solo lecturas);
     2. hace las llamadas empezando donde se quedó la anterior (`gsr::retry_rotated`, `g_rotate`);
     3. quita las marcadas (`BindQueue::remove_marked`).
2. **BAJA, corregido.** `load_done = m_state < 0 || m_state == 3` daba por hecha la carga:
   - antes del primer fotograma (−1);
   - con un 3 viejo tras recargar el campo.

   Ahora cada pasada lee `M+0x231` de **su** M, con la vtable comprobada; sin M, la carga no está hecha:
   - el fotograma usa el RAX de `0x8B6144`;
   - en `0xC0B626` se usa el M que dio la vf48 de ese 2335. El gancho `0xC0B5C1` lo pasa en `guest_sched_bind_note`
     (`BindPending::m`).
3. **BAJA, corregido.** Antes había dos casos en que ninguna entrada se juzgaba:
   - las entradas cuyo dueño pasaba de los 4 seguidos hacían `continue` antes del juicio;
   - con la tabla incompleta (`!ws.ok`, `truncated`), sin tabla o en un reino que no era el de cliente, la pasada
     salía sin juzgar nada.

   Así no caducaba nada y la cola podía llenarse. Ahora toda entrada se juzga en cada pasada:
   - **sin tabla entera** (`RetryFacts::table_ok`): solo `Wait` o `TimedOut`;
   - **dueño desconocido** (no seguido, o no está en la tabla en esta pasada): no se llama ni se descarta por
     `OwnerMoved`. Sí sale por `NoStage`, `Changed`, `Gone`, etc., y por el plazo.
   - Ahora se siguen 8 dueños.
4. **BAJA (solo modo 1), corregido.** `guest_seq_route_2524=1` solo exigía `guest_seq_play_knows_x`, que también
   es cierto con `guest_seq_play=probe`. Ahora `gsr::mode_2524(route, m2524, legacy, play_ticks)`: con 1 se enruta
   solo si `guest_seq_route=1` y `guest_seq_play_ticks()` (`guest_seq_play=1` y no apagado). Detalle en
   `p8-impl-secuenciador-bodyanim.md` §5.
- **Pruebas nuevas:**
  - `test_guest_sched_bind`:
    - el plazo frente a `Call`;
    - `table_ok` y `owner_known`;
    - `retry_keep_after` (rechazo, `still 0`, los tres finales);
    - `retry_rotated` (vuelta e índices únicos);
    - `remove_marked`.
  - `optd_seq_bodyanim`: `mode_2524` con `play_ticks`.
- `.\scripts\build.ps1`: sin avisos, **41/41 OK**.
