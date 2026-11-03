# P8 · Escenas de horario de NPC en el gestor X (`guest_sched_route`)

10 de octubre de 2026, rama `opcion-d` (sobre `8b25afb`). Build del juego **1.0.0.2976** (PE `0x6AB28F00`, SHA-256
`57DA440D…FDE1E7`, el de `bin64` y el de `D:\RE` son el mismo), direcciones en **RVA** (base `0x140000000`). No se
lanzó el juego ni se desplegó nada. `.xtls` solo se leyó; los cuatro ganchos están en `.rsrc`.

**[V]** = comprobado con capstone sobre el PE (scripts propios en el scratchpad: desensamblado por RUNTIME_FUNCTION,
barrido de `E8/E9/0F 8x rel32` y de `lea/mov [rip+d32]` en `.rsrc` y `.xtls`, comparación de bytes con el código),
o con las capturas de S11. **[E]** = interpretación. **[H]** = pendiente de la prueba en el juego.

Base: informe independiente `…\npc-raiz-s11-20261010-work\INFORME.txt` §4 y `schedules\informe.txt`;
`p8-s1-escenas-npc.md` §2; `p8-impl-opcion-a.md` (el mismo patrón para los secuenciadores).

## 0. Resumen

- **El fallo [V, informe S11]:** el 2335 `CreateNPCScheduleStageAck` crea la `pa::ClientNPCScheduleStage` en
  `[M+0x148]` (M = gestor del personaje propio del invitado, porque la 2174 reescrita le hace dueño del campo), pero
  los NPC la buscan en `[X+0x148]` (X = gestor del personaje dueño, `comp+0x2D8` = Kliff del anfitrión). S11: X vacío,
  M con 115 / 25. Ningún NPC de horario se enlaza.
- **Implementado** `[session2] guest_sched_route = 0 | probe | 1` (solo invitado), dentro de `guest_seq_route`:
  cuatro mid hooks justo después de la `vf48` del campo en los manejadores 2335 / 2126 / 2958 / 2909. Si el paquete
  nombra a otro personaje (`0xA…`) que no es el propio de M y ese personaje tiene un X válido (vtable
  `0x55C9720`) con contenedor `X+0x148`, `RAX := X`. La baja y los cambios de una clave van al gestor al que fue su
  2335 (libro clave → gestor).
- **Enlace [V]:** no hay reintento nativo periódico. La vf13 `0x9ED290` corre una vez por actor (al desempaquetar su
  2005), y los otros dos caminos (`0x9F2FA0`) solo los llaman la vf107 de la IA y el 2874. Pero en S11 **todos** los
  2335 que recibió el invitado llegaron **antes** que el 2005 de sus NPC (328/328 en la fase 1, 15/15 en la fase 2):
  con el 2335 en X, la vf13 nativa los encuentra. Los NPC sin 2335 (401 / 336, la instantánea que falta) siguen igual.
- **Activación [V]:** `stage+0x178` la pone la propia creación (`0xA9FEE0 → 0x88E5F80 → 0x43AA30`, y por la carga
  asíncrona `0x43CEF0`), sin tick y sin depender del gestor. No hace falta tocarla.
- **No se escribe** `comp+0x318` ni `stage+0x178`. Solo lectura de diagnóstico (G5 + censo cada 5 s).
- **Inis:** `cdmp.p8-s7-shadow.ini` y `-fase2.ini` → `guest_sched_route=1`; `cdmp.p8-s7-host.ini` y
  `cdmp.p8-noche-local.ini` → `guest_sched_route=0` (§7).
- Compilación y 40/40 pruebas OK (§9).
- **Revisión del 10-10, rama `p8-sched-guest-2`** (§10, §11):
  - un 2335 que **crea** una clave que `X+0x148` ya tiene se rechaza (la copia de `observer_sched_copy` puede llegar
    antes que el nativo);
  - un 2335 que **no crea** (tipo ≠ 1/4) va como seguimiento de su clave, y se rechaza si su gestor no la tiene
    (fallaría en `.xtls 0x88E5F80`);
  - una **guarda** en el desmontaje de la etapa (`0x43B0B6`) evita llamar a un X ya liberado cuando el Kliff del
    anfitrión sale del cliente con NPC aún enlazados;
  - el censo cuenta las etapas de un X ido y avisa cuando el personaje sale con etapas en X.

## 1. Desensamblado de los cuatro manejadores [V]

Los cuatro tienen la misma forma que los de secuenciador: decodifican el payload en el marco, resuelven el actor con
`0x8A67E0` (gestor de actores del cliente), guardan el actor en el *wrapper* (`[r15]`), piden el campo con
`0x17FA6C0`, llaman a `field->vf48` y pasan `RAX` a la función de la familia.

### 1.1 2335 `CreateNPCScheduleStageAck`, `0xC0AFC0` (RUNTIME_FUNCTION `0xC0AFC0–0xC0B83E`)

```
0xC0AFD0  lea rbp,[rsp-0x10A0]                     marco
0xC0AFE5  mov r15,r8                               wrapper; 0xC0AFF0 mov r14,[r8+0x18] (paquete crudo)
          u8 kind @0 -> [rbp+0x10F0] ; u32 actor @1 -> [rbp-0x60] ; u32 @5 -> [rbp-0x40] ; u32 recurso @9 -> [rbp-0x48]
          u8 @13 -> [rbp+0x10F8] ; u64 clave @14 -> [rbp-0x58], copiada en 0xC0B242 a [rbp-0x38] ; u32 @22 -> [rbp-0x5C]
0xC0B50A  mov r8d,[rbp-0x60] ; lea rdx,[rbp+0x58] ; mov rcx,r12 ; call 0x8A67E0 ; nop
          cmp byte [rbp+0x68],0 ; je ; mov rbx,[rbp+0x60] ... 0xC0B547 mov [r15],rbx
0xC0B55E  movzx ebx,byte [rbp+0x10F0]              BL = kind (se pasa como r8b)
0xC0B5B7  mov rcx,[rbp-0x28] ; mov rax,[rcx] ; call [rax+0x48]     vf48 -> M en RAX
0xC0B5C1  lea rcx,[rbp+8] ; mov [rsp+0x58],rcx      ← PUNTO (9 B)
   ...    (solo escribe rcx, r9, r8, rdx)
0xC0B612  mov rdx,[rbp-0x28] ; mov rcx,rax ; call 0xA9FEE0(G, campo, kind, &@5, &recurso, …, &clave [rsp+0x30], …)
```

- El informe independiente propone `0xC0B5C1` con `48 8D 4D 08 48 89 4C 24 58` y `RAX = M`: **confirmado**. RAX no se
  escribe entre `0xC0B5CA` y `0xC0B616`.
- `0xA9FEE0` sale si `[G+0x148] == 0` (`0xA9FF0C`); con kind 1/4 crea la etapa (0x1A0) con `stage+0x28 = G`,
  `stage+0x50 = [G+0x18]` (dueño) y la mete en `[G+0x148]+0x18`. Deduplica por *wake spot*, no por clave.

### 1.2 2126 `DeleteNPCScheduleStageAck`, `0xC0B840` (`0xC0B840–0xC0BB94`)

```
0xC0B850  lea rbp,[rsp-0x20]
          u32 actor @0 -> [rbp+0x70] ; u64 clave @4 -> [rbp+0x78] (0xC0B98E)
0xC0B9B5  mov r8d,[rbp+0x70] ; lea rdx,[rbp-0x18] ; mov rcx,rsi ; call 0x8A67E0 ; nop
          cmp byte [rbp-8],0 ; je ; mov rsi,[rbp-0x10]                encontrado [rbp-8], actor [rbp-0x10]
0xC0BA44  mov rcx,[rbp-0x58] ; mov rax,[rcx] ; call [rax+0x48]       vf48 -> M
0xC0BA4E  lea rdx,[rbp+0x78] ; mov rcx,[rax+0x148]   ← PUNTO (11 B)
0xC0BA59  call 0x43C750  (= jmp .xtls 0x890EE10: baja por clave en el contenedor)
```

- La segunda instrucción reubicada lee `RAX` **después** del callback: con `RAX := X` el contenedor que se pasa es el
  de X. Sin RIP-relativo. `[X+0x148]` **no** se comprueba aquí en el código del juego (en M nunca es 0): por eso el mod
  no enruta si `X+0x148` es 0 (`NoSchedContainer`).

### 1.3 2958 `ChangeNPCScheduleMiseensceneAck` `0xC0C3F0` y 2909 `DeactivateMiseensceneScheduleEvadeObstacleAck` `0xC0C920`

Mismo cuerpo (desplazamiento `+0x530`):

```
0xC0C400 / 0xC0C930  lea rbp,[rsp-0x1050]
          u8 @0 -> [rbp+0x10A0] ; u32 actor @1 -> [rbp+0x10A8] ; u32 recurso @5 -> [rbp-0x58] ; u64 clave @9 -> [rbp-0x60]
          (0xC0C5BA / 0xC0CAEA) ; u32 × n -> vector [rbp+0x30]
0xC0C6B7 / 0xC0CBE7  mov r8d,[rbp+0x10A8] ; lea rdx,[rbp+8] ; mov rcx,r12 ; call 0x8A67E0 ; nop ;
          cmp byte [rbp+0x18],0 ; je ; mov rdi,[rbp+0x10] ... mov [r15],rdi     encontrado [rbp+0x18], actor [rbp+0x10]
0xC0C757 / 0xC0CC87  mov rcx,[rbp-0x48] ; mov rax,[rcx] ; call [rax+0x48]
0xC0C761 / 0xC0CC91  lea r9,[rbp-8] ; lea r8,[rbp-0x60]   ← PUNTO (8 B)
0xC0C769 / 0xC0CC99  lea rdx,[rbp-0x58] ; mov rcx,rax ; call 0xAA0F00 / 0xAA1850 (= jmp .xtls 0x9FA5090)
```

- `0xAA0F00(G, &recurso, &clave, &vector)`: sale si `[G+0x148] == 0` (`0xAA0F3D`), busca el fichero
  (`0x1862A80`), toma el cerrojo `[[G+0x148]+8]` y busca la etapa por clave (`0x43C3A0`).
- Payload de ejemplo (H1 #284867): `04 | 01 00 10 a0 | f5 20 ff 8e | 81 01 00 20 00 00 00 00 | 01 00 00 00`.

### 1.4 Comprobaciones de los puntos [V]

| Comprobación | Resultado |
|---|---|
| Inicio y fin del parche son límites de instrucción (desensamblado desde el inicio de su RUNTIME_FUNCTION) | sí, los 4 |
| Saltos directos `rel8/rel32` de la función hacia `(punto, punto+parche)` | ninguno |
| `E8/E9/0F 8x rel32` de **todo** `.rsrc` y `.xtls` hacia el interior de los cuatro tramos | ninguno |
| Operando RIP-relativo en las instrucciones reubicadas | ninguno |
| `RAX` escrito entre el punto y su uso (`0xC0B616`, `0xC0BA52`, `0xC0C76D`, `0xC0CC9D`) | no |
| Ranuras de marco leídas: escritas entre su definición y el punto | no (las únicas llamadas intermedias, `0x466060`, `vf150` y `0x17FA6C0(…, rbp-0x30 / -0x60 / -0x50)`, no llegan a ellas) |
| Otro módulo del mod engancha esas direcciones | no |
| Bytes del código (`check_cpp.py`: 25 tramos de `guest_seq_route.cpp` contra el PE) | 25/25 iguales |

## 2. Puntos y bytes verificados

| Punto | Parche | Bytes verificados (16) | Contexto verificado al instalar |
|---|---|---|---|
| `0xC0B5C1` 2335 | 9 | `48 8D 4D 08 48 89 4C 24 58 48 8D 4D 18 48 89 4C` | `0xC0AFD0` (8), `0xC0AFE5` (3), `0xC0B242` (8), `0xC0B50A` (27), `0xC0B55E` (7), `0xC0B5B7` (10), `0xC0B612` (12) |
| `0xC0BA4E` 2126 | 11 | `48 8D 55 78 48 8B 88 48 01 00 00 E8 F2 0C 83 FF` | `0xC0B850` (5), `0xC0B98E` (8), `0xC0B9B5` (27), `0xC0BA44` (10) |
| `0xC0C761` 2958 | 8 | `4C 8D 4D F8 4C 8D 45 A0 48 8D 55 A8 48 8B C8 E8` | `0xC0C400` (8), `0xC0C5BA` (8), `0xC0C6B7` (30), `0xC0C757` (10), `0xC0C769` (12) |
| `0xC0CC91` 2909 | 8 | `4C 8D 4D F8 4C 8D 45 A0 48 8D 55 A8 48 8B C8 E8` | `0xC0C930` (8), `0xC0CAEA` (8), `0xC0CBE7` (30), `0xC0CC87` (10), `0xC0CC99` (12) |

Las matrices completas están en `src/mod/session2/guest_seq_route.cpp` (`kS2335*`,
`kS2126*`, `kSChange*`, `kS2958*`, `kS2909*`). `p8_install_mid` exige que safetyhook reubique exactamente 9 / 11 / 8 / 8
bytes; si no, rechaza sin escribir.

Lo que lee cada gancho (todo con lecturas SEH):

| Punto | Actor resuelto | Clave del paquete | Clave de escena | Recurso / kind |
|---|---|---|---|---|
| 2335 | `[rbp+0x68] ? [rbp+0x60]` | `u32 [rbp-0x60]` | `u64 [rbp-0x38]` | `[rbp-0x48]` / `BL` |
| 2126 | `[rbp-8] ? [rbp-0x10]` | `u32 [rbp+0x70]` | `u64 [rbp+0x78]` | — |
| 2958 / 2909 | `[rbp+0x18] ? [rbp+0x10]` | `u32 [rbp+0x10A8]` | `u64 [rbp-0x60]` | `[rbp-0x58]` / — |

## 3. Diseño

### 3.1 Decisión (`gsr::decide` con `need_sched`)

En este orden; la primera que falla es el motivo y el paquete sigue nativo (M):
`Off`, `NotHooked`, `NoManager` (RAX 0), `NoActor` (el manejador no lo encontró), `KeyMismatch` (el actor resuelto no
es la clave del paquete), `NotCharacter` (clave no `0xA…`), `OwnCharacter` (la de M: los horarios propios del
invitado), `NoX`, `NotStageManager` (vtable de X ≠ `exe+0x55C9720`), `XNotItsOwn` (`[X+0x18]` no es el actor),
`AlreadyX` (vf48 ya dio X), **`NoSchedContainer`** (`[X+0x148] == 0`). Si pasa todo: `Route` (`RAX := X`) o, en
`probe`, `WouldRoute`.

No hay compuerta de entrega ni rechazo: estos paquetes llegan de forma nativa (S11: 204/204 2335 idénticos H/G) y, sin
la opción, ya caen en M. Lo que no se puede enrutar sigue como antes.

### 3.2 Coherencia: el libro clave → gestor (`gsr::SchedLedger<512>`)

- 2335: `sched_created_where(verdict)` → **X** si `Route`/`WouldRoute`; **M** si es de otro personaje y no se pudo
  enrutar; nada si el paquete no se juzga (`Off`, `NotHooked`, `NotCharacter`, `OwnCharacter`).
- 2126 / 2958 / 2909 (`sched_followup`): si X no es enrutable ahora → nativo. Si no:
  - la clave fue a **M** → `CreatedInM`, nativo (la baja va a M, donde está);
  - fue a **X** → a X, salvo que X haya cambiado desde entonces (`LedgerStale`, nativo);
  - clave **desconocida** (creada antes de activar la opción, o expulsada del libro) → a X solo si el contenedor de X
    la tiene (`sched_has`: recorrido acotado de `[X+0x148]+0x18`, comparando `stage+0x30`); si no, `NotInX`, nativo.
- El 2126 libera la entrada. Con el libro lleno, un anillo expulsa la más antigua (contador `evicted`).
- El libro solo lo tocan los ganchos, en el hilo que lo posee (`res_thread_ok`, el principal del juego, el mismo que
  la tabla de recursos de los secuenciadores); un gancho en otro hilo no lo toca y se cuenta.

### 3.3 Hilos y memoria

- Ganchos: hilo principal del juego (los manejadores Troc). Solo lecturas SEH, atómicos y el anillo de eventos sin
  bloqueo; sin reservas, sin CRT, sin llamadas al juego. `guest_x_context_hook_entry()` y `guest_x_note_m()` como en
  los puntos de secuenciador.
- Diagnóstico: hilo de diagnóstico del invitado (cada 5 s), lecturas SEH acotadas (≤ 1024 etapas, ≤ 256 nodos).

### 3.4 Arranque

`guest_seq_route_start(…, sched_mode)`: el hilo de instalación arranca si `guest_seq_route` **o** `guest_sched_route`
no son 0. Los puntos de horario se instalan primero (no comparten dirección con nadie); con `guest_seq_route=0` no se
instala ningún punto de secuenciador (como antes).

## 4. Enlace, reintento y activación

### 4.1 Quién enlaza [V]

- `0x9ED290` solo aparece como puntero en `0x55BEEE0` = vtable `0x55BEE78` (`ClientSequencerPlayDataActorComponent`)
  ranura 13. Ninguna llamada directa ni referencia RIP-relativa en `.rsrc`/`.xtls`.
  - La llama el *unpack* `0x17EA310` (`jmp .xtls 0xC867CC0`) a cada componente al crear el actor: vf11/vf13/vf14
    (`INFORME-CABALLO-CRASH.md`, `caballo-coop.md` §552).
  - Una sola vez por NPC en el invitado (sesión 6: 235 NPC con clave, 235 lecturas de `local_res8`;
    `p8-s1-escenas-npc.md` §2.3).
- `0x43B4B0` (el enlace en sí) solo lo llaman `0x9ED497` (vf13) y `0x9F3142` (`0x9F2FA0`).
- `0x9F2FA0` (el mismo cuerpo con la clave por parámetro) solo lo llaman:
  - `0x91CE05` (`jmp`, final de `ClientAiActorComponent::vf107` `0x91CD90`);
  - `0xC0D24D` (manejador 2874 `0xC0CE50`).
- En el invitado ninguno de los dos actúa: la vf107 necesita el «cerebro» de IA (`0x9245B0` sale con
  `[[ai+0x48]+8] == 0`; sesión 6, 0 reenlaces), y en S11 llegaron 0 × 2874.

**Conclusión [V dentro de los límites del barrido]:** no hay reintento periódico. Un NPC cuya vf13 corrió antes de
que su etapa existiera en X queda con `comp+0x318 = 0` hasta que el actor se vuelva a crear (un 2005 nuevo al volver a
entrar en su zona, **[E]**).

Límite: el barrido de `.xtls` solo cubre los rangos con RUNTIME_FUNCTION.

### 4.2 Orden en S11 [V sobre los JSON del informe independiente]

Para cada NPC de horario cuyo bloque de IA leyó G1 (= su 2005, donde corre la vf13), se buscó si el 2335 de su clave
ya había llegado al invitado:

| Fase | NPC | 2335 **antes** del 2005 | después | nunca |
|---|---:|---:|---:|---:|
| 1 | 729 | 328 | **0** | 401 |
| 2 | 351 | 15 | **0** | 336 |

Con la opción, esos 328 / 15 deberían enlazarse por el camino nativo, siempre que el actor dueño (`comp+0x2D8`) ya
exista en el cliente en ese momento **[H]**. Hay NPC cuya vf13 corrió antes que el Kliff del anfitrión
(`owner found 0` en S11). Los 401 / 336 sin 2335 necesitan la instantánea de entrada (`p8-impl-horarios.md` §5):
esto no los arregla.

### 4.3 Activación `stage+0x178` [V]

Escritores de `+0x178` en funciones con el paso de nodo `0x168`. Barrido de `C6/C7/88/89… [reg+0x178]` en
`.rsrc`+`.xtls`, confirmado desensamblando la función. Hay tres:

| Dirección | Función | Valor | Qué es |
|---|---|---|---|
| `0xAA02DA` | `0xA9FEE0` (2335) | 0 | inicialización tras el constructor `0x15EE420` |
| `0x43AA62`, `0x43AAF6` / `0x43AE15` | `0x43AA30` | 1 / 0 | activar |
| `0x43D691` | `0x43CEF0` | 1 | fin de carga |

Detalle de `0x43AA30` (activar):
- Pone 1 de entrada si el byte global `exe+0x6CE94D8` es 0.
- Recorre los nodos (`+0x138`, `+0x140`, paso `0x168`) con `0x1815520`. Si no queda nada pendiente, pone 1 y vuelve.
- Si queda algo, crea un trabajo cuyo *callback* es `0x43DFD0` (`jmp 0x43CEF0`; su única referencia es el `lea` de
  `0x43ACED`), lo entrega con `0x1400AF0([[WorldSystem]+0x30]+0xF0, …)` y deja `+0x178 = 0`.

Detalle de `0x43CEF0` (fin de carga):
- Pone `+0x178 = 1`.
- Recorre los nodos con NPC enlazado (`+0x28`) y referencias (`+0x120`/`+0x128`) y se las da a cada NPC: atiende a los
  NPC que se enlazaron mientras la etapa no estaba activa.

Quién llama a `0x43AA30`: solo `.xtls 0x88E6105`, el final de `0x88E5F80`. Es la preparación que `0xA9FEE0` llama al
crear (`call 0x43AA30` y luego `jmp 0x15EFED0(stage, 1)`).

**[V]** La activación es parte de la creación y no depende del gestor: con el 2335 en X, la etapa se activa igual.
**[E]** El trabajo asíncrono es una carga de recursos de los nodos.

### 4.4 Actualización periódica de `ClientNPCScheduleStage` [H]

- La vtable `0x5578D80` (17 ranuras) no tiene un *update*. Sus ranuras son:
  - destructor `.xtls 0x88D3590`;
  - consultas (`0x43A6D0` → `+0x58`, `0xC850B0`, `0x15F19B0`, `0x15EF950`, `.xtls 0xC18E430`);
  - recogida de actores `0x15EF4A0` y copia `0x15F1870`;
  - baja `0x43B790` → `0x43AE50`;
  - mapas `.xtls 0x88F9EB0` / `0x88FAC70`.
- Ninguna escribe `+0x178`.
- `0xAA3E70` recorre `+0x190`/`+0x28`, no `+0x148` (informe independiente).
- **[E]** La etapa es un contenedor de datos. El comportamiento lo da la IA del NPC con las referencias que le pasan
  `0x43B4B0` (si `+0x178`) o `0x43CEF0`. No se encontró un tick propio.

### 4.5 Si no basta: arreglo seguro propuesto

**Implementado** como `guest_sched_bind` (`p8-impl-horarios-enlace.md`):
- se llama a `0x9F2FA0` en `0xC0B626` (tras `0xA9FEE0`, mismo manejador), con el contrato analizado entero;
- solo para los NPC con `comp+0x318 == 0`;
- la instantánea (`p8-impl-horarios-snapshot.md`) suelta los 2335 **después** de los 2005, así que el punto 1 de
  abajo no se cumple y hace falta.

Lo que sigue es la propuesta original.

Para los NPC ya cargados cuando llega su 2335 (en S11 no hubo ninguno: §4.2), o los que se enlazaron sin dueño:

1. **Preferible:** que el anfitrión mande la instantánea de 2335 activos **antes** de los 2005 de sus NPC (orden
   nativo; `p8-impl-horarios.md` §5).
2. Si hiciera falta un reenlace, usar el camino **nativo** que ya usa el 2874: `0x9F2FA0(comp, &clave)`.
   - Desde un gancho del hilo principal justo después de `0xA9FEE0` (`0xC0B61E`, mismo manejador y mismo hilo).
   - Solo para los NPC con `comp+0x2E0 == clave`, `comp+0x2D8 ==` el personaje y `comp+0x318 == 0`.
   - Antes hay que leer entero `0x9F2FA0` (cerrojo del actor, `[[ai+0x90]+8]` válido) y medir con un `probe`.
3. **Nunca** escribir `comp+0x318` ni `stage+0x178` a mano: `0x9ED290` también suma la referencia (`+0x10`, `+0x16`),
   llama a `0x43B4B0` (nodo `+0x28`, referencias a la IA) y pone `+0x3D9`.

## 5. Diagnóstico añadido (solo lectura)

- **G5** (actores vigilados: `guest_seq_watch`, los enlazados de los 2661 enrutados y hasta 16 NPC enlazados en X):
  también `comp+0x318` (etapa enlazada), `comp+0x3D9` (esperando activación) y, de esa etapa, `+0x30` (clave),
  `+0x178` (activa) y `+0x28` (gestor: X o M).
- **Censo** `GuestSchedRoute [census]`, cada 5 s si cambia (y cada 60 s igual):
  - de `X+0x148` (X del último personaje enrutado) y de `M+0x148` (M del personaje propio): etapas, activas, «of
    another manager» (`stage+0x28 ≠` ese gestor: debe ser 0), nodos y nodos con NPC (`+0x28`);
  - de los NPC vigilados: cuántos con componente, enlazados, con etapa activa, esperando y con la etapa en X / en M.
- **Resumen** `GuestSchedRoute summary` cada 60 s:
  - veredictos por punto;
  - libro: claves vivas en X y en M, expulsadas, claves desconocidas juzgadas por el contenedor de X;
  - censo y ganchos en otro hilo.
- Líneas por paquete `GuestSchedRoute [2335 … 0xC0B5C1]` (y 2126/2958/2909), con:
  - clave, personaje, recurso y kind;
  - M y X con su número de etapas;
  - lo que dice el libro y el veredicto.
  - Hasta 4 por clave y 128 cada 5 s; las de NPC/usuario y del personaje propio no se escriben.

## 6. Qué mirar en el log del invitado (prueba en dos PC)

1. `GuestSchedRoute ON (guest_sched_route): points: …` con los **cuatro** puntos y `REFUSED: none`.
2. Al entrar en el campo del anfitrión: líneas `[2335 …] … ROUTED to the actor's own manager X` con
   `ledger: noted in X`, y X subiendo de 0 etapas.
3. `[census]`:
   - X con etapas, la mayoría activas, **`0 of another manager`** y nodos con NPC subiendo;
   - M sin crecer por las escenas del anfitrión (los 115 de S11 deberían quedarse en las propias).
4. NPC vigilados `bound (comp+0x318)` > 0 y `stage in X` > 0. En G5, `bound stage +0x318 … manager +0x28 = X`.
5. Bajas: `[2126 …] ledger: its 2335 went to X … ROUTED`; las etapas de X bajan. Una 2126 de una clave creada en M
   dice `its 2335 went to M: native (M), the same manager`.
6. Cruce con `GuestStage [NPC … 0x9ED48C]` (instancia encontrada) si `guest_stage_diag=1`: debe pasar de 0 a > 0.
7. Lo visual: los NPC de horario (barrenderas, cestas, fauna) haciendo algo en lugar de quedarse quietos, comparado
   con el anfitrión. Solo los que tuvieron 2335 (§4.2).

## 7. Inis

- `cdmp.p8-s7-shadow.ini` y `cdmp.p8-s7-shadow-fase2.ini`: `guest_sched_route=1`, tras `guest_schedule_load=1`. Los
  dos módulos se complementan: el fichero se carga antes del 2335 y la etapa va a X.
- `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini`: `guest_sched_route=0`. El módulo solo arranca con `guest=1`, y en
  el cliente del anfitrión la vf48 del campo ya devuelve el gestor de su propio personaje (M = X): sus 2335 ya están
  donde los buscan sus NPC. No gana nada.

## 8. Criterios de aborto y riesgos

Aborto: pasar a `guest_sched_route=probe`, que decide y cuenta sin tocar RAX. Si sigue mal, `0`. Motivos:
- cierre del juego;
- `of another manager` ≠ 0;
- X que crece y no baja tras las bajas;
- NPC o interacciones raras al entrar en el campo, o tirones;
- `hooks on another thread` ≠ 0.

Riesgos:
- **Reenlace [H]:** si el Kliff del anfitrión aún no existe en el cliente cuando corre la vf13 de un NPC, ese NPC no
  se enlaza aunque su 2335 esté en X (§4.1). El censo lo muestra: `bound` bajo con etapas en X.
- **Efectos de la IA [H]:** con la etapa enlazada, `0x43B4B0`/`0x43CEF0` piden acciones a la IA del NPC (`vf190`).
  En el invitado la IA es remota (el anfitrión manda). Hay que ver que no haya dos productores de acción para el mismo
  NPC (pose que salta, NPC que «tiembla»). Si pasa: `probe`.
- **`guest_x_ui_veto=1` [E]:** sus manejadores son eventos de secuenciador (`TimelineEvent`, `SetInteraction`…) y
  clasifican por gestor (X frente a M). Una etapa de horario no es un secuenciador, pero si alguno corriera con G = X
  se vetaría. Mirar los contadores de `GuestXVeto` antes y después.
- **Preparación con misiones del campo [V informe]:** `0x88E5F80` consulta `field->vf40` (misiones del dueño del
  campo, el invitado). Igual que sin la opción: el campo no cambia, solo el gestor.
- **Instantánea:** las 42 / 46 escenas activas antes de la entrada siguen sin 2335 en el invitado. No es regresión.

## 9. Código y pruebas

- `src/mod/session2/guest_seq_route_logic.h`:
  - `SchedPoint`, `sched_created_where`, `sched_followup`;
  - `SchedLedger`, `sched_line_ok`, `SchedCensus`;
  - los veredictos `NoSchedContainer`, `CreatedInM`, `NotInX`, `LedgerStale`;
  - `NpcState` con `+0x318`/`+0x3D9`/`+0x178`/`+0x30`/`+0x28`.
- `src/mod/session2/guest_seq_route.{h,cpp}`: los cuatro puntos, `guest_sched_route_hook`, `install_sched`, el censo,
  el resumen y G5.
- Lo demás:
  - `src/mod/config.{h,cpp}`: la clave `guest_sched_route`;
  - `src/mod/session2/guest.cpp`: se la pasa a `guest_seq_route_start`.
- `tests/optd_seq_copy_tests.cpp` (`test_guest_sched_route`):
  - decisión con y sin `X+0x148`, probe, personaje propio, NPC, `AlreadyX`, `NoActor`, `KeyMismatch`;
  - dónde fue el 2335 y los seguimientos;
  - el libro: no anota claves 0 ni sin juzgar, repetición, hueco libre tras una baja, expulsión;
  - límite de líneas por clave, censo e igualdad de `NpcState`.
- `test_guest_sched_kinds_and_teardown` (§10, §11):
  - tipos que crean, duplicado y probe;
  - 2335 sin creación con y sin la clave en su gestor;
  - el registro de X (repetición, nuevo X del mismo personaje, expulsión);
  - la decisión de la guarda: ajeno, vivo, ido, otro X, desajuste.
- `.\scripts\build.ps1`: compilación sin avisos, **40/40 pruebas OK**.

## 10. Tipos de 2335 y creaciones duplicadas (revisión del 10-10)

### 10.1 Qué decide el tipo en `0xA9FEE0` [V]

`0xA9FEE0(G, campo, tipo r8b, &@5, &recurso, &u8@13, &clave, &u32@22, u32 bajo de la clave, …)`,
`0xA9FEE0–0xAA0473`:

1. Sale en el acto si `[G+0x148] == 0` (`0xA9FF0C`/`0xA9FF14`). Antes solo guarda `rbx`/`r8b`/`rdx` en su pila.
2. Busca el fichero de horario por recurso (`0x1862A80`); sin fichero, sale.
3. `r14d` = el *wake spot* del `u32 @5` en la tabla `[0x6D6C1F8]` (−1 si no está).
4. Toma el cerrojo exclusivo de `[[G+0x148]+8]`.
5. **Tipo 1 o 4** (`0xAA0066`/`0xAA0072`): si `r14d != −1` y alguna etapa del contenedor tiene `+0x58 == r14d`, no
   crea nada (`0xAA0130..0xAA0137` → `0xAA0162`). Si no, reserva `0x1A0`, construye (`0x15EE420`), mete en
   `+0x18` (`0x43DD20`), `0x1F70540` y, ya sin el cerrojo, `0x15EE280` y `0x43A890`.
   **La deduplicación es solo por *wake spot*, nunca por clave.** Dos 2335 de la misma clave cuyo `@5` no esté en
   esa tabla crean dos etapas con la misma clave.
6. **Otro tipo:** `0x43C3A0(contenedor, &out, &clave)` (`.xtls 0x88FDAC0`: busca `stage+0x30 == clave`; `out = 0` si
   no está o si se está destruyendo, `+0x15`) y después, sin comprobar nada, `0x43A890(etapa, tipo, campo, …)` =
   `.xtls 0x88E5F80`, que lee `[etapa+0x138]` (`0x88E5FDF`). **Con la clave ausente, el cliente falla.**

### 10.2 Lo implementado

- **Rechazo limpio:** con `RAX :=` un sumidero del mod de `0x200` bytes a cero (`g_sched_sink`), `0xA9FEE0` sale en
  `0xA9FF14` sin tocar nada.
  - Poner el tipo a 0 (como el 2661) **no** es limpio aquí: lleva al camino de búsqueda + `0x43A890` (§10.1, punto 6).
  - El manejador no vuelve a leer `G` tras la llamada (§1.1).
- **2335 que crea** (`gsr::sched_creates`): si va a X (`Route`/`WouldRoute`) y `sched_has(X, clave)` →
  `DuplicateCreate`. El libro sigue con la clave en X y la nota de `guest_sched_bind` no se arma.
  - Solo el contenedor decide, no el libro. El libro se anota al decidir la ruta, no cuando la creación sale bien
    (`0xA9FEE0` puede no crear: sin fichero, o deduplicado por *wake spot*). Un «el libro dice X» sin la clave en X
    rechazaría un 2335 que sí tiene que crear.
- **2335 que no crea:** el mismo camino que 2958/2909 (`find_mut`, `sched_followup`), nunca se anota como creación.
  Si el gestor al que iría (X si `Route`/`WouldRoute`, M si no) no tiene la clave y el paquete es de otro personaje
  → `NoStageForKey`, rechazado. Los paquetes del personaje propio y de NPC siguen nativos.
- **`probe`:** los mismos veredictos, sin tocar RAX (la línea dice `(probe: not applied)`).
- **Líneas:** cada rechazo tiene su línea `GuestSchedRoute [2335 …] … DUPLICATE create refused` (o `REFUSED: a
  non-creating 2335 …`), fuera del límite de 4 por clave. El resumen añade `refused 2335 N`.
- **Límite:** no hay tope por clave en el contenedor. Si el anfitrión tuviera de verdad dos etapas con la misma clave
  (dos creaciones nativas sin *wake spot*), X tendría una. El 2126 (`0x43C750`) borra por clave **[E]**.

## 11. Vida de X: el Kliff del anfitrión sale del cliente con NPC enlazados

### 11.1 Qué hace el desmontaje nativo [V]

- **Destructor de X** (vtable `0x55C9720` ranura 0 → `.xtls 0x9F74C50` → `0xA94440` → `.xtls 0x9F798B0`): libera sus
  listas y, al final, el contenedor: `[X+0x148]->vf0(1)` (`0x9F79AA2..0x9F79AB8`).
- **Contenedor** (vtable `0x5578D38`, creado en `.xtls 0x9F6FE83..0x9F6FEC3`): su destructor `0x43C210` llama, por
  cada etapa, a **`0x43AE50`** (desmontaje) y suelta su referencia.
- **`0x43AE50` (desmontaje de etapa):**
  - limpia la lista `+0x158` (`+0x160 = 0`), `+0x170 = 0` y `+0x50 = 0`;
  - si `stage+0x58 != −1` (tiene *wake spot*), llama a **`[stage+0x28]->vf12(&stage+0x58, 6)`**
    (`0x43B0A3..0x43B0BC`; `vf12` = `0x1F708A0` → `.xtls 0xDE1D0E0`: cerrojo `[X+8]`, tabla `+0x70..+0x88`);
  - **no toca** `comp+0x318` de los NPC ni `nodo+0x28`.
- **Referencias:** los NPC enlazados guardan su etapa en `comp+0x318`, con referencia (`+0x10`). La etapa sobrevive a
  X mientras haya un NPC enlazado.
- **Su última liberación** llega por:
  - el desenlace `0x1F5DFD0` (dentro de `0x9F3230`);
  - un reenlace;
  - la destrucción del NPC.

  Entonces corre el destructor de la etapa, `.xtls 0x88D3590`, que **vuelve a llamar a `0x43AE50`**. Ese
  `+0x58` no se borró en el primer desmontaje, así que llama otra vez a `[stage+0x28]->vf12` → **X ya liberado: uso
  tras liberar, cierre del juego.**
- **Nativo:** los NPC solo se enlazan al gestor del personaje propio, que no se destruye antes que ellos.
  `guest_sched_route` crea el caso: etapas en el X de otro personaje, que en cooperativo puede salir del cliente
  (lejos, salida) mientras los NPC del invitado siguen.
- **Otros lectores de `stage+0x28`:** barrido de las 17 ranuras de la vtable de la etapa `0x5578D80` (lecturas
  `+0x28` sobre el registro de la etapa): solo `0x15EF4A0`, y es `dword [nodo+0x28]` (la clave del NPC), no el gestor.
  `vf16`/`0x9C81520`, `0x9F3230` y `0x9F2FA0` llegan a X por el dueño **[V dentro de ese barrido]**.

### 11.2 La guarda (`0x43B0B6`, con `guest_sched_route=1`)

```
0x43B0A3  lea rdx,[rdi+0x58] ; mov r15d,-1 ; cmp [rdx],r15d ; je 0x43B0BF
0x43B0B2  mov rcx,[rdi+0x28]
0x43B0B6  mov rax,[rcx] ; mov r8b,6      ← PUNTO (6 bytes)
0x43B0BC  call [rax+0x60]
```

- **Verificación:**
  - `verify2 43B0B6:6` OK;
  - `rdi` = la etapa en los dos caminos que llegan (`0x43AE71 mov rdi,rcx` y la recarga `0x43B08F` desde su hogar;
    `regw`: las dos ramas que entran, `0x43AEC2` y `0x43AED4`, llevan la etapa en `rdi`).
- **Bytes comprobados:** `0x43B0B6` (16), `0x43B0A3` (19), `0x43AE50` (10), `0x43AE71`, `0x43B08F`.
- **Llamadores de `0x43AE50`:**
  - `0x43B794` (la baja de la vtable);
  - `0x43C253` (el contenedor);
  - `0x43CAA5`;
  - `.xtls 0x88D35B3` (el destructor);
  - `.xtls 0x890EF99` (borrado por clave).

  Corre en cualquier hilo: el *callback* solo hace lecturas SEH y atómicos.
- **Registro** `gsr::XRegistry<8>`: los X a los que el gancho del 2335 llevó una creación (`Route`), con su
  personaje.
- **Decisión** (`gsr::teardown_guard`): si `rcx == [rdi+0x28]` está en el registro y el X actual de su personaje
  (`manager_of(find_actor(dueño))`) ya no es ese → `RCX :=` un gestor del mod que no hace nada (su vtable de 16
  entradas apunta a una función vacía). La `call [rax+0x60]` va ahí. No se escribe nada del juego.
  - Cualquier otro gestor (M, los nativos) no se toca.
- **Falso «ido»** **[E, inocuo]:** mientras X se destruye, su personaje ya puede faltar en la tabla. Entonces se
  salta el `vf12` del primer desmontaje sobre un X que todavía existe. Solo marca/suelta una entrada de *wake spot* de
  la tabla de X, que se destruye justo después.
- **Fallo que evita:** que una dirección reutilizada parezca «viva». Se compara con el gestor que da la tabla de
  actores, no con la memoria del X viejo.

### 11.3 Diagnóstico

- **Censo:**
  - `stage of a GONE X` (NPC vigilados cuya etapa es de un X del registro que ya no está);
  - `routed X managers gone`.
- **`GuestSchedRoute [X gone]`:** el personaje salió de la tabla de actores (o cambió de X) mientras su X tenía
  etapas. Dice cuántas, cuántos nodos con NPC y si la guarda está puesta.
- **`GuestSchedRoute [stage teardown 0x43B0B6]`:** cada redirección (límite por ventana), con etapa, clave, X viejo y
  X actual.
- **Resumen:** `teardown guard (in): ours live N, X GONE redirected N, mismatch N`.

### 11.4 Alternativas descartadas

- **Desenlazar los NPC con `0x9F3230`/`0x1F5DFD0` antes de que muera X:** haría falta un punto en el hilo principal
  justo antes del destructor de X. El destructor está en `.xtls` y su entrada en `.rsrc` es un `jmp rel32`
  (`p8_install_mid` no lo reubica byte a byte), y no hay un aviso fiable de «va a morir».
- **Borrar las etapas antes (2126 nativo):** no quita las referencias de los NPC; el problema sigue.
- **Mantener vivo al personaje con una referencia:** cambiaría la vida de un actor del juego.
