# S25, opción U: el gestor de stages propio del usuario 2 (diseño del arreglo de fondo, build 1.0.0.2976)

Investigación de solo lectura (2026-10-12, noche): sin tocar código, sin compilar, sin desplegar, sin abrir el juego.
Ghidra (servidor de consultas de `D:\RE\CrimsonDesert_2976`) y capstone sobre el PE; `.xtls` solo leído.
Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). Todas las direcciones son RVA.

Marcas: **[V]** comprobado (log, captura, bytes o desensamblado); **[E]** leído en el código (decompilado) sin prueba en
vivo; **[H]** hipótesis.

Parte de `mundo-teleport.md` (causas C1–C3 y la propuesta «U»). Contexto: `noche/mundo-invitado.md` (ancla),
`noche/control-npc-lejos.md` (controlador de los NPC), `pendientes/p8-s12-tenderos-servidor.md` (la puerta de dueño y
el veredicto «S1 no»), `pendientes/p8-impl-viaje-rapido.md` §12 (por qué llamar a la preparación sobre el gestor de un no
dueño «no está limpio»).

Datos usados:
- Logs del anfitrión `C:\Emulador CD\backups\p8-sesion25-host\cdmp_20261012-063736.log`, `p8-sesion26-host\…-195346.log`,
  `p8-sesion27-host\…-222907.log` (`ShopProbe`, `User2Funnels`, `ObserverSchedCopy`).
- Capturas: anfitrión `p8-sesion25-host\cdmp_packets_20261012-063747.bin` (+ S26/S27); invitado
  `s25-shadow\cdmp_packets_20261012-054022.bin`; invitados S15–S22 (`p8-sesionNN-shadow`).
- Scripts (scratchpad `mundoU\`): `offw.py` (escrituras/lecturas de un desplazamiento), `dis.py`, `calls.py`,
  `reason.py`, `peek.py`, `windows.py` (ventana de stages), `owners2335.py` (dueños y claves), `overlap.py` /
  `overlap_guest.py` (solape de ventanas por distancia), y los decompilados `d_0x*.c`.

> **Resultados de la sonda U-0 en la sesión 28 (Shadow): `u0-resultados-s28.md`.** Cambian varias cosas de este
> diseño:
> - el gestor del usuario 2 está en estado **0**, no 1;
> - hace falta la condición de «mismo campo»;
> - nuevo F4: el 2362 del usuario 2 resetea el gestor del anfitrión;
> - la posición de la stage no está en `+0x90`;
> - el motivo 30 no pasa por C/D.
>
> **U-1 implementado** (`user2_stages = probe | 1`): `u0-resultados-s28.md` §8, con la prueba en un PC.

---

## 0. Resumen

1. **El usuario 2 sí tiene su propio `ServerSequencerStageManager` (y su `ServerNPCScheduleSpawnManger`), pero en la
   opción D está muerto desde que entra en el campo del anfitrión [V].**
   - Se llega a él por `[[[pj2+0x68]+0xA8]+0x18]` (`pj2` = personaje principal del usuario 2, `[usuario+0xD0]`); vtable
     `0x5B17A58`, `[gestor+0x18] == pj2`. Existe desde su login: `ShopProbe [gate]` lo ve en su primer campo.
   - `ShopProbe` cuenta, en S25 (dos logins), S26 y S27, **exactamente 3 entradas** de `0x288B7A0` para el gestor del
     usuario 2 (2 en la puerta, ambas «dueño», en su campo inicial) y **ninguna más** en 25–35 min de sesión. El gestor
     del anfitrión: 13–14 por minuto, antes y después de que entre el usuario 2.
   - Por qué [E]: sus temporizadores (2043 stages, 2045 horarios, 2046 facciones) llevan como sujeto a `pj2`, pero sus
     manejadores piden el gestor con `campo->vf48()`, que en el servidor es **siempre el del dueño del campo**
     (`0x2B00CE0`). La carga del usuario 2 hacia el campo del anfitrión lo deja en **estado 1** (descargado) y nada lo
     vuelve a preparar, porque `MoveFieldCompleteCore` solo prepara al dueño.
2. **Lo que compara la puerta `0x288B87A`** [V bytes]: `[campo+0x180]` (clave de usuario del dueño del campo) contra
   `[[pj+0xA0]+0x60]` (clave de usuario del personaje del gestor). Además del 2045/2043 y la puerta, el camino de
   actualización tiene **otra puerta de dueño** en la evaluación de misiones (`0x29113CB`) y una en los indicadores
   globales del campo (`0x2B047C0`). Las dos conviene **dejarlas como están**.
3. **Encontrado el llamador del motivo 19 [V]:** `0x287D4C0` («descargar todo»). Lo ejecuta el tick `0x287DBB0` cuando
   el gestor está en estado 1:
   - `0x287D8C0  mov r9b, 0x13` → `call 0x2808D80`, el `DeleteStage` del gestor de horarios → `.xtls 0x10225FC0` →
     `0x2817E80(stage, campo, 19)`;
   - el estado 1 lo pone `0x287DA50` (`.xtls 0x102C2A30`), que llama **solo** `SendMoveFieldAck 0x2BCA3D0` (en
     `0x2BCA7C9`), **solo si el que carga es el personaje principal del dueño del campo**, y sobre `campo->vf48()`.
   - Con U: la carga del anfitrión descarga solo el gestor del anfitrión; la del usuario 2 no descarga nada de forma
     nativa. El diseño añade esa descarga (§5, pieza U-d).
4. **Claves y duplicados [V/E]:**
   - Las claves de stage (`0x2000xxxx`) y los ids de secuenciador salen de **un único contador global del proceso**
     (`0x1F6B6E0` → `.xtls 0xDE10700`, `++[exe+0x6C8CDF0]`). Dos gestores **no pueden chocar** de clave.
   - La creación de una stage de horario (`0x28078E0`) solo deduplica **dentro de su propio gestor de horarios**. Dos
     gestores sí pueden crear la misma stage dos veces.
   - Los **personajes únicos** (dato estático `+0x14B`: tenderos, NPC con nombre) están protegidos por un **registro del
     `ServerField`** (`campo+0xD8`, `.xtls 0xE2B9EE0` / `0xE2B7070` / `0xE2B8B50`, usado por `0x2817130`): si ya hay
     una stage dueña de ese personaje, la segunda no lo genera. Pueden duplicarse los **aldeanos genéricos**.
5. **Ventanas medidas (S25, gestor del anfitrión) [V]:**
   - creación a 1–3 celdas de 64 m (distancia de Chebyshev; p95 226 m);
   - cierre por ventana a 2–4 celdas (p95 241 m; 5 celdas muy rara vez);
   - solape: con el anfitrión a ≥ 7 celdas del invitado, 0 stages vivas del anfitrión a ≤ 4 celdas del invitado; a ≥ 8
     celdas, 0 a ≤ 5.
   - **Histéresis propuesta:** U encendido a ≥ 10 celdas (≈ 640 m por eje) sostenidas 10 s, apagado a ≤ 7 celdas
     (≈ 450 m) sostenidas 5 s.
6. **Paquetes [V]:**
   - El 2335 llega de forma nativa también al otro cliente (609 nativos al invitado en S25).
   - Los 2126 de ventana y los 2958/2909 van solo al dueño (`observer_sched_copy` los copió: 567/29/17, 0 nativos).
     Solo los 2126 de `0x2817E80` van ToNear (36 nativos, los del motivo 19).
   - En el cliente del invitado, las stages con dueño = su propio personaje van a su gestor **M** por el camino nativo
     (`guest_sched_route` da `OwnCharacter`). Las del anfitrión siguen en **X**. Como las claves son globales, no se
     pisan.
   - **El cliente del anfitrión sí necesita una pieza nueva:** recibiría los 2335 del usuario 2 en su propio M y nunca
     sus cierres de ventana (pieza U-h).
7. **Diseño (§5):** `[session2] user2_stages = 0 | probe | 1`, solo en el anfitrión, con piezas pequeñas:
   - U-0: sonda;
   - U-a: puerta;
   - U-b: rutas de vf48 para 2043/2045/2046;
   - U-c: armado;
   - U-d: vaciado;
   - U-e: controlador al nacer, que es la pieza A de `control-npc-lejos.md` extendida a los temporizadores;
   - U-f: filtro de elementos;
   - U-h: sumidero en el cliente del anfitrión.

   Todo en entradas de función o con `create_mid`, en `.rsrc`, con la sonda primero. En un PC se verifica casi todo con
   `cdmp_peer`. Shadow hace falta para lo que ve y simula el cliente del invitado.

---

## 1. P1: el gestor del usuario 2, la puerta y lo que da por hecho el camino de actualización

### 1.1 Cómo se encuentra y qué hay dentro [V/E]

| Objeto | Cadena | Comprobación |
|---|---|---|
| Personaje principal del usuario 2 | `[usuario2+0xD0]` (ya lo usan `observer_milestone` y `user2_funnels`) | clave `+0x60` = la publicada por el mod |
| `ServerSequencerStageManager` | `[[[pj2+0x68]+0xA8]+0x18]`; es la misma cadena que sigue `vf48 0x2B00CE0` para el dueño (`ftl::seq_manager_of` ya la lee) | vtable `0x5B17A58`, `[mgr+0x18] == pj2` |
| `ServerNPCScheduleSpawnManger` (horarios) | `[mgr+0x148]` | vtable `0x5B16968`, `[sm+0x18] == pj2` |
| Gestor de facciones u otras stages (clave de clase 2) | `[mgr+0x10]` | lo tickea el 2046 (`UpdateFactionSpawnStageOnceTimer`) |

Campos del gestor que importan (de los decompilados de `0x287DBB0`, `0x288B7A0`, `0x287EF60`, `.xtls 0x102C4060`,
`.xtls 0x102CA7F0`, `0x287D4C0`) [E]:

| Campo | Significado |
|---|---|
| `+0x6A0` (u8) | Estado: 0 recién reiniciado (`0x102C4060`), **1 descargar** (`0x102C2A30`), **2 listo** (`0x102CA7F0`). `0x288B7A0` exige ≥ 2 |
| `+0x68A` (u8) | «Preparación pendiente»: `0x287EF60` lo pone a 1 si `0x287FDD0` dice «no listo» y reintenta con un 2043 a 10 ms |
| `+0x689` (u8) | «Dentro de la preparación» (`0x287EF60`, de 1 a 0) |
| `+0x680/+0x684` (2 × i32) | Última celda (X, Z) evaluada; `+0x688`, «evaluando» |
| `+0x28/+0x30` | Lista de stages del gestor (secuenciadores, clase 0); `+0x98` de cada una = personaje dueño |
| `+0x70…+0x88` | Mapa de stages activas (id → estado) |
| `+0xC8/+0xD0` | Lista de «conservar» en la descarga |
| `+0x168…+0x5B8` | Tablas por categoría y celda; `0x102CA7F0` las vacía al pasar a estado 2 |
| `[sm+0x30/+0x38]` | Stages de horario; `[sm+0x20]`, peso (tope 10 en `0x28078E0`); `[sm+0x90]`, **2045 pendientes** |

### 1.2 Su estado hoy en la opción D [V log + E código]

- **[V]** `ShopProbe [gate]` de S25: `the stage update 0x288B7A0 of character 0xA010034E (manager 0x3924D12CB00): its
  user 0x9010034D vs the field 0x391CED17700's owner 0x9010034D -> OWNER`, a las 06:41:09 y 06:41:19. Es su campo
  inicial, del que es dueño. El resumen se queda en `updates 3, gate 2, owner 2, NOT owner 0` hasta el final (07:13).
  - Lo mismo en el segundo login (`0xA020280A`), en S26 (`0xA010030E`, 20:22) y en S27 (`0xA0100377`, 22:45).
  - El gestor del anfitrión: 6 → 20 → 32 entradas por minuto antes del usuario 2, y 47 → 60 → 73 → 87 → 100 después.
    Es el mismo ritmo, ~13/min.
- **[V]** `User2Funnels vf48 … callers` bajo el sujeto del usuario 2, en S25/S26/S27:
  - `exe+0x1082F96A` ×58/45/56: el manejador del **2043**, `.xtls 0x1082F900`, `call [rax+0x48]` en `0x1082F967`;
  - `exe+0x10830A0F` ×73/37/38: el del **2045**, `.xtls 0x108309B0`;
  - `exe+0x10835A8F` ×73/37/38: el del **2046**.
  - Se cuentan junto con «`user 2's own field` 180/93/95» llamadas en su campo propio: son sus ticks mientras aún era
    dueño. Los siguientes van al gestor del anfitrión.
- **[E] Secuencia que deja el gestor en estado 1:**
  1. Login → campo propio (dueño) → `MoveFieldComplete` → `0x287EF60` → estado 2 → las 3 actualizaciones.
  2. El usuario 2 empieza la carga hacia el campo del anfitrión (su 2437 en su campo, aún como dueño) →
     `SendMoveFieldAck 0x2BCA3D0` → «el que carga es el dueño» → `0x287DA50(campo->vf48())` = **su** gestor → estado 1
     y un 2043 inmediato (`+0xB` = 0, sin reprogramar).
  3. Ese 2043, si llega mientras sigue en su campo, entra en `0x287D4C0`, que cierra lo que tuviera, y **no reprograma**.
  4. Ya en el campo del anfitrión, `MoveFieldCompleteCore 0x2BC7670` se salta `0x287EF60` para el no dueño
     (`0x2BC8470`, `p8-observador-escenas.md` §2.5). **Queda en estado 1 y sin cadena de 2043.**
  - Su campo inicial no tiene horarios: S25, S15–S22 dan **0 × 2335 con dueño del invitado** (`owners2335.py`).
- **[E] El 2045 del usuario 2 queda bloqueado.**
  - El último 2045 pendiente de su gestor de horarios lo consumió el gestor del anfitrión (vf48).
  - `[sm2+0x90]` (contador de 2045 pendientes) **se quedó en 1**, porque solo lo baja `.xtls 0x1025E4C0` sobre el gestor
    que se actualiza.
  - `0x1025C310` no vuelve a programar mientras `+0x90 > 0`. Aunque se armara el gestor, la cadena de horarios **no
    arrancaría sola** (§5, U-c). Pendiente de medir [H→sonda].
- Lateral [H]: cada 2045 del usuario 2 que va al gestor de horarios del anfitrión deja, por la lógica de `+0x90`, una
  cadena extra de 2045 en el anfitrión (actualizaciones de horario duplicadas). En el 2043 no pasa, porque la
  descarga no reprograma y el ritmo del anfitrión no cambió. La sonda cuenta 2045 por sujeto y minuto.

### 1.3 La puerta `0x288B87A` exacta [V]

```
0x288B84C  movzx ebx, byte [rbx+0x591]   ; [[[pj+0x68]+0x1A0]+0x4F0]…+0x591: TransformSync «cargando» → salir
0x288B861  jne 0x288BDEE
0x288B867  mov rax, [r13+0xA0]           ; r13 = pj del gestor (= [mgr+0x18]); +0xA0 = su usuario
0x288B86E  test rax, rax ; je 0x288B878  ; sin usuario → eax = esi (0)
0x288B873  mov eax, [rax+0x60]           ; clave de usuario del pj
0x288B876  jmp 0x288B87A
0x288B87A  cmp dword [r15+0x180], eax    ; r15 = campo (rdx de la entrada); +0x180 = clave de usuario del DUEÑO
0x288B881  jne 0x288BDEE                 ; no es dueño → sale sin evaluar
           41 39 87 80 01 00 00 | 0F 85 67 05 00 00
```

- `rdi` = gestor, `r13` = su personaje, `r15` = `ServerField`. `0x288B876` salta al **primer** byte del parche: vale
  para `create_mid` (`shop_probe` ya engancha G en esta dirección).
- **Un solo dueño del punto:** U tiene que compartir el gancho G de `shop_probe` (mismo `create_mid`). O se mueve G a
  U y `shop_probe` lee el veredicto, o la función de G de `shop_probe` consulta a U. Dos `create_mid` en la misma
  dirección no.

### 1.4 Todo lo que el camino de actualización da por hecho sobre el dueño [V/E]

| # | Sitio | Qué supone | Con U |
|---|---|---|---|
| D1 | `.xtls 0x1082F900` (2043), `.xtls 0x108309B0` (2045), 2046 (retorno `0x10835A8F`) | El gestor sale de `campo->vf48()` = el del dueño [V decompilado y log] | **U-b**: devolver el gestor del usuario 2 para sus temporizadores |
| D2 | `0x287DBB0` (tick) | Campo del actor `[mgr+0x18]`; `SetFieldTime_PreCheck([pj+0xA0])` da el campo y el «actor posición» (montura incluida); reprograma 2043 para `[mgr+0x18]` [E] | Nativo con el usuario 2 |
| D3 | `0x288B7A0` | `+0x6A0 ≥ 2`, `+0x68A == 0`, `TS+0x591 == 0`, **puerta** [V] | **U-a** (puerta); **U-c** (estado 2) |
| D4 | `0x288B7A0` → `.xtls 0x1071C6E0(TS, campo, pj, posición, flag)` | Por personaje: recorre la lista estática `+0x388` (regiones) y avisa a su usuario (`0x2BD1D00`) [E] | Correría para el usuario 2. Efecto por medir [H]: probablemente descubrimiento de regiones |
| D5 | `0x288B7A0` → `0x2B047C0(campo, pj, posición)` | **Tiene su propia puerta de dueño** (`[campo+0x180] == [[pj+0xA0]+0x60]`): indicadores globales del campo `+0x198` [E] | Se queda del anfitrión (correcto: es estado del campo) |
| D6 | Celda | `floor(pos/64)` en X y Z, con `sector*1000 + local` de `TransformSync_GetWorldPosition([[param_4+0x68]+0x1A0])`; clave 2D `(Z<<32)|X` en `+0x680`; 3D para `0x287DFE0` [V] | La del usuario 2 |
| D7 | `0x28818F0` → `0x2880BC0` | Contexto con el gestor de misiones de `vf40`. Los llamadores `0x28xxxxx` **no** están en la lista de la rama B de `user2_funnels` (`vf40_caller_allowed`), así que es el del **anfitrión**: el progreso del mundo es el del anfitrión [V código del mod] | Nativo |
| D8 | `0x2911300` (llamado en `0x28819CF`) | **Segunda puerta de dueño**: `0x29113AD mov r8d,[r8+0x180]` … `0x29113CB cmp r8d,eax ; jne 0x2912FA0` [V bytes]. Sin ella se salta la evaluación ligada a misiones | **No levantarla.** Así el gestor del usuario 2 no arranca stages de misión: menos riesgo de doble misión |
| D9 | `0x15025C310(sm, mgr, +0x689)` al final de `0x288B7A0` | Programa el 2045 si `[sm+0x90] == 0`; con `+0x689` actualiza ya (`0x2822D00`) [E] | **U-c**: la cadena de 2045 del usuario 2 está bloqueada (§1.2) |
| D10 | `0x2822DE0` (actualización de horarios) | Exige que exista el personaje del dueño del campo (`0x2AFFF50`), pero toma la **posición del personaje del usuario del propio gestor** (`0x206AE30` → `0x4F8220`) [V desensamblado `0x2822EE2`–`0x2822FA7`] | Funciona para el usuario 2 sin tocar nada |
| D11 | `0x287DFE0` | Stages `AutoSpawnOwnerData` (tipo `'\n'`) frente a los mapas de sectores del campo `[campo+0x228]+0x150/+0x170` [E] | **[H]**: si el servidor solo carga sectores alrededor del dueño, las de ese tipo junto al usuario 2 se cerrarían. La sonda cuenta sus cierres |
| D12 | Copias en línea de `FindUser([campo+0x180])` (`p8-sistemas-juego.md` §11): `0x2791CD0` (controlador al crear), `0x2BB4020` (baja de un NPC de stage: avisa al gestor de horarios **del dueño**, `0x28069A0`), `0x27EA160` (pools)… | Siguen apuntando al anfitrión [E] | `0x28069A0` busca la stage por clave en el gestor del anfitrión. Como las claves son globales (§3.1), no la encuentra y solo anota el estado del NPC en un registro por dato de horario (`+0x40`). Inocuo [E]. El **controlador** sí importa: **U-e** |

---

## 2. P2: el cierre de motivo 19, quién lo hace y qué haría un gestor por dueño

### 2.1 La cadena completa [V]

```
GameLoadingStartReq 2437 / ReloadStageStartReq 2283 del que carga
 └ TransformSync::GameLoadingStart 0x2BC9200 → … → SendMoveFieldAck 0x2BCA3D0
     if (clave del que carga == clave del pj principal del dueño)           // 0x2BCA4xx (lVar5 == iVar10)
        mgr = campo->vf48()                                                 // 0x2BCA7BA, retorno 0x2BCA7BD
        0x287DA50(mgr, b1, b2)  →  .xtls 0x102C2A30:                        // 0x2BCA7C9, único llamador
             [mgr+0x6A0] = 1;  EnqueueTimer(2043 inmediato, sujeto [mgr+0x18], +0xB = 0)
 └ 2043 → .xtls 0x1082F900 → campo->vf48() → 0x287DBB0(mgr, …)
     +0x68A == 0 && +0x6A0 == 1  →  0x287D4C0(mgr, …)                       // 0x287DD6B
         reúne claves: mgr+0x28 (con [stage+0x98] == pj), [mgr+0x148]+0x30, [mgr+0x10]+0x20; salta mgr+0xC8
         por clave, según key.lo >> 29:
           0 → 0x2879440(mgr, campo, &id, 0xE)           2810 DeleteSequencer
           1 → 0x2808D80([mgr+0x148], campo, &k, 0x13)    ← 0x287D8C0  41 B1 13  mov r9b,0x13 ; 0x287D8D0 call
               → .xtls 0x10225FC0 (vf6 DeleteStage) → 0x2817E80(stage, campo, 19) → Logout(NPC, 19) + 2126 ToNear
           2 → 0x27FB370([mgr+0x10], k, 0xE, campo)
         0x2809040([mgr+0x148])   // baja también los NPC «recordados» (+0x50), con +0x9C = 0x13
         0x27FB9C0([mgr+0x10]); 0x287BCE0()
     (no reprograma: el estado sigue en 1 hasta otra preparación 0x287EF60)
```

- La búsqueda anterior no encontraba el `0x13` porque se buscaba `mov r8b`. El motivo es el **cuarto** argumento del
  `DeleteStage` del gestor de horarios, `r9b`, y `0x2817E80` lo recibe en `r8b` desde `.xtls`.
- Los 6 ms entre el 2437 del anfitrión y los 2126 de S25 son el 2043 inmediato que programa `0x102C2A30`.

### 2.2 Gestor por dueño: carga del anfitrión frente a carga del usuario 2

| Hecho | Hoy (opción D) | Con U encendido |
|---|---|---|
| Carga del anfitrión (viaje rápido, reaparición, recarga) | Descarga su gestor, el único con stages, y se cierran también las del lado del invitado (C1) | Descarga **solo** su gestor; las del usuario 2 siguen vivas en el suyo [E, por construcción: `0x287D4C0` recorre listas del gestor que recibe] |
| Carga del usuario 2 en el campo del anfitrión | Nada nativo (no es dueño); sus stages no existen | Nada nativo **tampoco**. Sin U-d, sus stages se quedarían vivas durante su carga y tras un viaje quedarían lejos de él. **U-d** reproduce lo nativo: `0x287DA50(mgr2)` antes de su 2437/2283 |
| Salida del usuario 2 (2001) | — | U-d antes del 2001 (el mismo punto que usa `WorldAnchor [clear] before 2001`); si no, quedan NPC huérfanos con su gestor destruido [H] |
| U pasa de encendido a apagado (se acercan) | — | U-d: descarga nativa con motivo 19; el gestor del anfitrión crea las suyas cuando el invitado entra en su ventana |

- **No se propone** saltarse el motivo 19 en `0x2817E80` (lo mismo que `mundo-teleport.md` §5.5). Con U no hace falta:
  lo del usuario 2 ya no está en el gestor que se descarga.

---

## 3. P3: claves, duplicados y umbrales

### 3.1 Cómo se identifican las stages [V decompilado + capturas]

- **Clave = contador global.** `0x1F6B6E0` → `.xtls 0xDE10700(obj, out, clase)`:
  `out.lo = clase << 29 | (++[exe+0x6C8CDF0] & 0x1FFFFFFF)`, `out.hi` = id del campo en el cliente o un valor fijo en el
  servidor.
  - Lo usan las stages de horario (`0x2807945`, clase 1 = `0x2000xxxx`), los ids de secuenciador (clase 0) y la clase 2
    (`0x288AD85`), además de dos llamadores del cliente.
  - **Un solo contador para todo el proceso** (servidor y cliente del anfitrión): dos gestores nunca dan la misma clave.
    Encaja con las capturas: la primera clave es `0x20000014` justo después del secuenciador `0x13` (S25, t = 47), y
    todas las sesiones empiezan en `0x20000014`.
- **Formato de los paquetes** (comprobado contra S25, `peek.py`):
  - 2335: `u8 tipo | u32 dueño @1 | u32 dato de horario @5 | u32 @9 | u8 @13 | u64 clave @14 | … | 3f posición @30`;
  - 2126: `u32 dueño | u64 clave @4`;
  - 2958/2909: `u8 | u32 dueño @1 | u32 recurso @5 | u64 clave @9 | …`.
- **Deduplicación nativa:**
  - `0x28078E0` (crear una stage de horario) recorre **solo** `[sm+0x30]` de su propio gestor de horarios. Reutiliza la
    existente si coincide el dato de horario (`[[stage+0x60]+0x1C]`) y el id o la posición. Tiene un tope de peso de 10
    (`[sm+0x20]`).
  - **No mira el gestor del otro jugador:** la misma stage puede vivir dos veces, una por gestor [E].
  - **Personajes únicos:** `ServerField+0xD8/+0xDC/+0xE8/+0xF0`, un mapa «clave de personaje → stage dueña».
    - `.xtls 0xE2B9EE0` lo consulta, y solo para personajes con el dato estático `+0x14B` activo;
    - `0xE2B7070` registra; `0xE2B8B50` libera (desde `0x2817E80` y `0x2BB4020`);
    - `0x2817130` **no genera** el NPC si ya está registrado.
  - El mapa es del **campo**, compartido por los dos gestores: un tendero o NPC con nombre **no se duplica** de forma
    nativa [E fuerte].
  - Los aldeanos genéricos (sin `+0x14B`) sí pueden duplicarse si las dos ventanas se tocan.
- **Las escenas de secuenciador** (tiendas `cd_seq_funcnpc_*`, escenas `cd_seq_node_*`) resuelven sus NPC por el mapa del
  campo `+0xB8` (`0x20D5620`, `p8-s12-tenderos-servidor.md` §7.2): la del usuario 2 enlazaría el **mismo** tendero.

### 3.2 Ventanas medidas [V]

`windows.py` sobre la captura del anfitrión S25 (918 MB). Posición del dueño: su 2556 o el de su caballo `B0100003`.
Distancias en 2D (X/Z):

| Medida | N | Distancia al dueño | Celdas de 64 m (Chebyshev) |
|---|---:|---|---|
| Creación (2335) | 561 | p0 70, p5 86, p50 140, **p95 226** m | 1: 288 · 2: 216 · 3: 48 · 4: 1 · 5: 1 (y 7 a 14 celdas, posición caducada en un viaje) |
| Cierre por ventana (2126 fuera de cargas) | 565 | p0 33, p5 83, p50 145, **p95 241** m | 1: 24 · **2: 285 · 3: 207 · 4: 35** · 5: 2 (y unos pocos con posición caducada) |
| Cierre por carga (≤ 200 ms tras el 2437 del dueño) | 36 | — | — |
| Creaciones cuyo dato ya estaba vivo en el mismo gestor | 0 | — | — |

- Una stage nace a 1–3 celdas del dueño y vive mientras está a ≤ 4 (rara vez 5).
- **Solape medido** (`overlap_guest.py`, captura del invitado S25, cada 5 s): stages vivas del anfitrión a ≤ K celdas del
  invitado, según la distancia anfitrión–invitado en celdas:

| Distancia (celdas) | Muestras | Vivas del anfitrión | ≤ 3 del invitado | ≤ 4 | ≤ 5 |
|---:|---:|---:|---:|---:|---:|
| 0 | 11 | 45,6 | 40,6 | 40,6 | 41,0 |
| 3 | 2 | 41,0 | 16,0 | 33,5 | 35,0 |
| 4 | 1 | 37,0 | 8,0 | 21,0 | 29,0 |
| 6 | 3 | 45,3 | 0,0 | 2,3 | 10,7 |
| 7 | 1 | 48,0 | 0,0 | 0,0 | 1,0 |
| 8 | 3 | 60,3 | 0,0 | 0,0 | 0,0 |
| 10–16 | 30 | 23–46 | 0 | 0 | 0 |

(Pocas muestras en el tramo intermedio: la posición del invitado solo cuenta si las dos tienen menos de 3 s.)

### 3.3 Umbrales con histéresis (de lo medido)

- Una stage puede estar viva en los dos gestores solo si está a ≤ 4 (5) celdas de los dos. Eso exige anfitrión–usuario 2
  ≤ 8 (10) celdas. Lo medido da 0 por encima de 7 (K = 4) y de 8 (K = 5).
- **Propuesta (claves configurables, valores por defecto de lo medido):**
  - `user2_stages_on_cells = 10`: encender cuando la distancia de Chebyshev en celdas, con la **misma fórmula que
    `0x288B7A0`** y la posición **del servidor** de los dos personajes, sea ≥ 10 durante `user2_stages_on_dwell_ms =
    10000`;
  - `user2_stages_off_cells = 7`: apagar (U-d) a ≤ 7 durante `user2_stages_off_dwell_ms = 5000`. El tiempo de espera
    evita oscilar al cruzar el borde de una celda.
- **Lo que no arregla:** con el anfitrión a 4–9 celdas (≈ 250–600 m) el invitado no tiene gestor propio y solo le cubre
  el borde de la ventana del anfitrión. Para bajar los umbrales hay dos caminos:
  - aceptar duplicados de aldeanos genéricos (los únicos están protegidos, §3.1);
  - la pieza opcional U-i (§5.10), un arbitraje por dato de horario que hay que medir antes.
- La posición del anfitrión montado: usar la del servidor (la de `TransformSync` del personaje del servidor, o la de su
  montura). La sonda `[minute]` del ancla se equivoca con el jinete (`mundo-teleport.md` §4).

---

## 4. P4: paquetes y rutas de los clientes

### 4.1 Quién recibe cada paquete [V capturas + log de `ObserverSchedCopy` S25]

| Opcode | Lo emite | Destino | Evidencia |
|---|---|---|---|
| 2335 `CreateNPCScheduleStageAck` | Creación de la stage | **Llega a los dos clientes** (ToNear o del campo) | `host 2335 in the trip: user 2 got it natively 609` |
| 2126 `DeleteNPCScheduleStageAck`, cierre por ventana | Gestor de horarios | Solo al dueño | `2126: copied 567, already native 36` |
| 2126 desde `0x2817E80` (motivo 19 y otros que pasen por vf13) | `0x2A53EB0` + `ToNearSendTargetFinder` desde `[stage+0x50]` | Cerca del dueño de la stage | Los 36 nativos |
| 2958 / 2909 | Gestor de horarios | Solo al dueño | `copied 29 / 17, already native 0` |
| 2264, 2165, 2195, 2661, 2322, 2724 | Gestor de stages | ToTarget al personaje del gestor | `p8-s12-tenderos-servidor.md` §2.3 |
| 2005 de los NPC | Creación del actor | Por interés, a los clientes cercanos al actor | — |
| 2516 motivo 19/30/25… | `ServerActor::Logout` | Igual | — |

Con U, para las stages del usuario 2 (dueño = `pj2`): el invitado recibe 2335/2126/2958/2909/2264/2661 **como en un
jugador**. El anfitrión recibe sus 2335 (si los hay cerca) y los 2126 de `0x2817E80`, pero **no** sus cierres por
ventana.

### 4.2 Cliente del invitado [V código del mod + log de S25]

- `guest_sched_route` (activo en S25: `GuestSchedRoute ON … points 2335 0xC0B5C1, 2126 0xC0BA4E, 2958 0xC0C761, 2909
  0xC0CC91`):
  - un paquete que nombra a **otro** personaje va a su X;
  - uno con el **propio** personaje (`gsr::decide` → `OwnCharacter`) queda nativo en **M**, que es donde lo buscará
    `0x9ED290` del NPC (`FindCharacter(comp+0x2D8)` = el propio personaje → M). **Es lo correcto para las stages del
    usuario 2 sin cambiar nada.**
- **Sin colisiones entre X y M** porque las claves son globales (§3.1). `sched_2335_create` (duplicado en X) no se activa.
- **Puerta de entrega** (`gsr::gate`): solo filtra 2661/2810/2322 que nombran a otro personaje; los del propio personaje
  pasan.
- **`guest_sched_bind`** reintenta enlazar NPC cuyo 2005 llega antes que su 2335. Para las stages propias, el orden es
  el nativo del servidor (2335 y luego los NPC), así que no hace falta [E].
- **`observer_sched_copy`** (anfitrión) solo copia lo que ve el **cliente del anfitrión** con dueño `A0100001` (`not
  user 2's 0`). No toca lo del usuario 2.
- **Filtro R1 y `guest_seq_route` (escenas):**
  - una escena propia del usuario 2 (2661 con su personaje) va a M de forma nativa;
  - sus Req (2436/2591/2704/2105/2276…) pasan el R1, porque no son ids del anfitrión, pero en el servidor `vf48` los
    manda al gestor del anfitrión y **se pierden** (le pasó a la `0x19D` en S12).
  - **Eso solo afecta a tiendas y escenas (fase U2, §5.9), no a la población.**
- **2322 (`PlayAllPreLoadSequencerAck`):** el diseño **no** llama a `0x287EF60`, así que no se emite ningún 2322 nuevo
  para el usuario 2. El que manda `0x287F1D4` reescribiría `M+0x231` del invitado [H de riesgo; por eso U-c es
  mínimo].

### 4.3 Cliente del anfitrión: lo que falta (U-h) [E]

- Los 2335 del usuario 2 que lleguen al cliente del anfitrión entran por su `vf48` del cliente (`0x9A5BE0` → M del dueño
  = **el anfitrión**) en su propio M.
- Los cierres por ventana nunca le llegan (ToTarget al usuario 2), así que en ese M quedarían **stages ajenas sin
  cierre** (fuga), y sus NPC buscarían la stage en `X(pj2)` del cliente del anfitrión sin encontrarla.
- **U-h:** en el **proceso del anfitrión**, los mismos cuatro puntos de `guest_sched_route` (`0xC0B5C1`, `0xC0BA4E`,
  `0xC0C761`, `0xC0CC91`; código de cliente idéntico) en un modo «anfitrión». Si el dueño del paquete no es el personaje
  de M (el anfitrión) → `RAX` := el **gestor sumidero** que `gsr` ya tiene para `DuplicateCreate` y los no creadores.
  - Primero en `probe`: contar 2335/2126/2958/2909 de dueño ajeno que llegan al cliente del anfitrión (hoy deberían ser
    0).
  - Alternativa más completa (si el anfitrión tiene que ver la animación de horario de esos NPC al acercarse): rutarlos
    a `X(pj2)` y espejar desde `take_frame` los 2126/2958/2909 que el servidor manda a la sesión 2. Se deja para
    después: con U, al acercarse se apaga y esas stages se descargan.

### 4.4 IA de los NPC que nacen junto al usuario 2 [E + `control-npc-lejos.md`]

- El controlador (`ServerActor+0x90`) se fija una vez, en `0x2791CD0` (`0x2792A11…0x2792A7B`), con este orden:
  `desc+0x40`, después el usuario padre, después `[campo+0x180]`.
- Para NPC de stage no se ha visto que `desc+0x40` sea el personaje del gestor [H]. En S16, los 70 NPC nacidos de
  spawns del invitado tenían todos de controlador al anfitrión, y los de stage del anfitrión salen con él por la rama
  del dueño del campo. La sonda de la fase 1 lo registra en U-e.
- Con U nacerían a ≥ 600 m del anfitrión, que es el caso **sin datos** de `control-npc-lejos.md` (≤ 250 m medido al nacer).
- **U-e**: la pieza A de ese informe (controlador = usuario 2 al nacer), con la condición «sujeto del usuario 2 y opcode
  2043/2045/2046 con U encendido» además de 2424/2550.
  - Implementada el 14-10 como `user2_npc_control` en `0x2792A7B` (`u0-resultados-s28.md` §8.17), tras la S31: sin
    ella, la IA del cliente lejano del anfitrión los daba de baja (motivo 42) y la stage los reponía en bucle.

---

## 5. Diseño (para implementar después, de forma mecánica)

### 5.1 Clave, alcance y dependencias

- `[session2] user2_stages = 0 | probe | 1`, solo en el **anfitrión**. Más:
  - `user2_stages_on_cells=10`, `user2_stages_off_cells=7`, `user2_stages_on_dwell_ms=10000`,
    `user2_stages_off_dwell_ms=5000`;
  - `user2_stages_elements = schedule | schedule+funcnpc | all` (U-f; por defecto `schedule`).
- Necesita `user2_funnels` con el sujeto de `ExecuteTroc 0x2774B20` (`t_frame.actor/opcode`) y el desvío de vf48
  instalado (como `user2_fast_travel=1`), `user2_world_anchor=1` (sin ancla el recolector vacía la zona,
  `mundo-invitado.md`) y la pieza G compartida con `shop_probe` (§1.3).
- Sin esas dependencias, U se queda en 0 y escribe una línea WRN.
- **No identifica nada entre PC por dirección.** Todo es local al proceso del anfitrión: gestores leídos por cadena y
  vtable; los personajes, por su clave publicada.

### 5.2 Máquina de estados (hilo del servidor, decidida a la salida de cada `ExecuteTroc` del usuario 2)

```
OFF ──(dist ≥ on durante on_dwell, usuario 2 Stable en el campo del anfitrión, claves listas,
       mgr2/sm2 legibles, mgr2 en estado 1 y listas vacías)──► ARMING ──(U-c hecho)──► ON
ON ──(dist ≤ off durante off_dwell | 2437/2283/2001 del usuario 2 | el campo deja de ser el compartido)──► DRAINING
DRAINING ──(la sonda ve 0x287D4C0(mgr2) terminado, o 5 s)──► OFF
```

- La distancia es la de Chebyshev en celdas de 64 m, calculada como en `0x288B7A0` con la posición del **servidor**
  (sector × 1000 + local) de `pj2` y del personaje del anfitrión (o su montura).
- **ON**: puerta abierta (U-a) y rutas (U-b).
- **DRAINING**: rutas abiertas para que el 2043 de descarga llegue a `mgr2`; puerta cerrada, porque con estado 1 no se
  usa.
- **OFF**: comportamiento nativo de hoy.

### 5.3 Puntos de enganche (todos en `.rsrc`, bytes de la build 2976)

| Pieza | Dirección | Tipo | Bytes comprobados | Hilo | Lee / escribe |
|---|---|---|---|---|---|
| U-0 E | `0x288B7A0` (entrada; la `E` de `shop_probe`) | ya existe | `48 8B C4 48 89 58 10` | servidor | lee `rcx` (mgr), `rdx` (campo), `r8` (pj) |
| **U-a** G | `0x288B87A` | `create_mid` (compartido con `shop_probe`) | `41 39 87 80 01 00 00` (+ `jne` `0F 85 67 05 00 00` sin parchear; `0x288B876` salta al primer byte) | servidor | si estado ∈ {ON} y `rdi == mgr2` y `[r13+0x60] == clave de pj2`: **`ctx.rax = [r15+0x180]`** (solo el registro) |
| **U-b** | `ServerField::vf48 0x2B00CE0` (el desvío inline de `user2_funnels`) | ya existe | — | servidor | si el sujeto es `pj2`, `t_frame.opcode ∈ {2043, 2045, 2046}`, el estado ∈ {ON, DRAINING} y el dueño del campo = el anfitrión: devolver **`mgr2`** a **todas** las vf48 de ese `ExecuteTroc` (no solo la primera, `0x1082F96A`/`0x10830A0F`/`0x10835A8F`), para que lo que hay dentro de `0x28818F0`/`0x2885BD0`/`0x288A970`/`0x28431A0` use el mismo gestor (el problema «A no está limpia» de `p8-impl-viaje-rapido.md` §12.1) |
| U-0 A1 | `0x2817E80` (entrada, vf13 de cierre) | inline en la entrada | `4C 89 4C 24 20 44 88 44 24 18` | servidor | `rcx` stage, `rdx` campo, `r8b` motivo, `[rsp]` retorno; clave `[stage+0x30]`, dueño `[[stage+0x50]+0x60]`, NPC vivos (`+0x138`, número `+0x140`, paso `0x168`, clave `+0x28`) |
| U-0 A5 | `0x287DA50` (el thunk `jmp .xtls`) | inline en la entrada (precedente: `0x20DEE60` en `user2_funnels`) | `E9 DB 4F A4 0D` | servidor | `rcx` mgr: qué gestor se descarga en qué carga |
| U-0 A6 | `0x287D4C0` (entrada, descargar todo) | inline en la entrada | `48 89 5C 24 18 88 54 24 10` | servidor | `rcx` mgr; tamaños de las tres listas |
| U-0 A7 | `0x2808D80` (thunk `jmp .xtls 0x10225FC0`, `DeleteStage` del gestor de horarios) | inline en la entrada | `E9 3B D2 A1 0D` | servidor | `rcx` sm, `r8` `&clave`, `r9b` motivo, retorno (motivo 19 = `0x287D8D5`; el motivo 30 y otros llamadores virtuales se identifican aquí) |
| U-0 A8 | `0x28078E0` (crear stage de horario) | inline en la entrada | `48 8B C4 48 89 58 10` | servidor | `rcx` sm, dato de horario `[[r9]+0x1C]`: creaciones por gestor y si el otro gestor tiene viva la misma |
| U-0 A9 | `0x2822D00` (thunk → `.xtls 0x1025E4C0`, actualizar horarios) | inline en la entrada | `E9 BB B7 A3 0D` | servidor | `rcx` sm: qué gestor de horarios se actualiza bajo qué sujeto |
| U-0 A10 | `0x287EF60` (preparación) | inline en la entrada | `48 89 5C 24 10 4C 89 4C 24 20` | servidor | qué gestor se prepara y cuándo |
| **U-e** | `0x2792A7B` (`mov [r14+0x90],eax` dentro de `ICreateServerActorDesc::vf3` `0x2791CD0`; el `create_mid` de `docked_gimmicks`, una sola instalación) | `create_mid` (implementado el 14-10, `u0-resultados-s28.md` §8.17) | `41 89 86 90 00 00 00`; cabeza del bloque `0x2792A11` y lectura del dueño `0x2792A70` comprobadas | servidor | con U armado y sujeto `pj2` en 2043/2045/2046: descriptor `CreateServerActorDesc_NPCSchedule` (`0x5B04CB0`), tipo 3/6, sin controlador, `desc+0x40` ni padre, `eax` = `[campo+0x180]` → `eax` := `[pj2+0x90]` (clave del usuario 2) |
| **U-f** | `0x288A970` (elemento de stage) | inline en la entrada (desvío que puede no llamar al original) | por leer | servidor | si `rcx == mgr2` y el elemento no está permitido: `*rdx = 0` y volver sin llamar. Permitidos: `[elem+0x10] != 0` (horario) y, con `schedule+funcnpc`, `[elem+8]` cuyo recurso empieza por `cd_seq_funcnpc_` |
| **U-h** | `0xC0B5C1`, `0xC0BA4E`, `0xC0C761`, `0xC0CC91` (cliente; los de `guest_sched_route`) | `create_mid`, ya verificados para `gsr` | los de `gsr` | **cliente del anfitrión** (hilo principal) | dueño del paquete ≠ personaje de M → `RAX` := sumidero |

Llamadas nativas que hace el mod (solo en modo 1, en el hilo del servidor, a la salida del `ExecuteTroc` **más externo**
del usuario 2, con las mismas comprobaciones de hilo que `observer_milestone_reload`: TLS `+0x1EC` del servidor, sin
estar dentro de `take_frame`, sin *scope sender* abierto):

| Llamada | Para qué | Notas |
|---|---|---|
| `0x287F450(mgr2)` (thunk → `.xtls 0x102CA7F0`) | Estado 2 y tablas de celdas vacías | Solo con estado 1 y listas vacías (§5.4). Quita del mapa activo `+0x70` lo que no esté en `+0xC8`. Con listas no vacías dejaría stages huérfanas: no se llama |
| `EnqueueTimer 0x2774430(g, &res, retardo, pj2, …, troc2043, &carga, 0x10)` | Arrancar la cadena 2043 de `mgr2` | Copia literal de `0x287F0CC–0x287F18F`: `rcx = [exe+0x6D69680]`, retardo = `[exe+0x6D06098] × 1000 / 100` (ms), `r9 = pj2`, `[rsp+0x28] = [exe+0x6FA3ED8]` (troc 2043, ya inicializado por la preparación del anfitrión; si es 0, no armar), `[rsp+0x30] = &carga`, `[rsp+0x38] = 0x10`. Carga: `u16 0x7FB, u8 0xFF, u64 [exe+0x5545F50] (celda cero), u32 1 @+0xB (reprogramar), u8 0 @+0xF`. **Pendiente de leer:** `[rsp+0x20]` en esa llamada |
| `EnqueueTimer(…, pj2, …, troc2045 [exe+0x6FA3E58], …)` | Desbloquear la cadena 2045 si `[sm2+0x90] > 0` y no ha llegado ningún 2045 del usuario 2 en 2 periodos | Carga y retardo: los de `0x1025C310` (`.xtls`, `uStack_40 = 3`, retardo `[exe+0x6D05878]`); **leerlos exactos antes de implementar**. El manejador hace `+0x90 -= 1` y reprograma solo: no se escribe en `+0x90` |
| `0x287DA50(mgr2, 0, 0)` | U-d: descarga nativa | Pone estado 1 y un 2043 inmediato a `pj2`; U-b lo lleva a `mgr2` → `0x287D4C0` → 2126 + motivo 19 |

### 5.4 Armado (U-c) en detalle

1. **Precondiciones** (lecturas SEH, registradas siempre):
   - `mgr2` con vtable `0x5B17A58` y `[mgr2+0x18] == pj2`;
   - `sm2 = [mgr2+0x148]` con vtable `0x5B16968`;
   - `+0x68A == 0`, `+0x689 == 0`, `+0x6A0 == 1`;
   - listas vacías: `[mgr2+0x30]`, `[sm2+0x38]`, `[[mgr2+0x10]+0x28]` y el número del mapa `+0x74`;
   - `[exe+0x6FA3ED8] != 0`.
2. Si se cumplen: `0x287F450(mgr2)` y después `EnqueueTimer(2043)`. No se llama a `0x287EF60`, así que no hay 2322,
   ni `0x287F7D0` (secuenciadores «siempre activos», como el clima o el `quest_progression_call`), ni `0x28431A0`.
3. Si `[sm2+0x90] > 0` y no ha llegado un 2045 del usuario 2 en `2 × periodo`: `EnqueueTimer(2045)`.
4. **Igual para 2046** (`[mgr2+0x10]`): primero medir con la sonda si su cadena está viva.
5. Si el estado es 2 (no esperado): no se llama a `0x287F450`; solo se programa un 2043 si no ha llegado ninguno en 2
   periodos. Si el estado es 0 o hay listas no vacías: **no armar** y escribir una línea con los valores. Esto **no** es
   una guarda que rechace sin medir: la sonda (fase 0) dirá qué estado hay de verdad antes del modo 1.
6. A partir de aquí todo es nativo:
   - el tick 2043 de `mgr2` evalúa su celda (U-a) y crea stages (2335 → invitado M);
   - reprograma su 2043;
   - el 2045 actualiza `sm2` (posición de `pj2`, D10);
   - el cierre por ventana sigue a `pj2`.

### 5.5 Vaciado (U-d)

- **Cuándo:**
  - al pasar a apagado (distancia);
  - antes de **ejecutar** el 2437 / 2283 del usuario 2, en la entrada de su `ExecuteTroc`, donde `user2_funnels` ya ve el
    2437;
  - antes de su 2001;
  - si el campo del usuario 2 deja de ser el del anfitrión.
- **Qué:** `0x287DA50(mgr2, 0, 0)` y estado DRAINING.
  - El 2043 inmediato pasa por U-b a `mgr2` → `0x287D4C0`: cierre nativo, 2126 ToNear, NPC con motivo 19, 2810 de sus
    secuenciadores, `0x2809040` de los NPC recordados.
  - A6 ve la descarga de `mgr2` y pasa a OFF. Si en 5 s no la ve, pasa a OFF con WRN y la sonda sigue contando.
- **Tras el 2762 del usuario 2:** si sigue lejos, se vuelve a armar (U-c) cuando pasen los tiempos de espera.
- **Carga del anfitrión con U encendido:** no se hace nada. A1/A5 deben mostrar `0x287DA50(mgr_anfitrión)` y ningún
  cierre de stage con dueño `pj2`.

### 5.6 Qué se escribe en memoria del juego

- U-a: solo un **registro** (`rax`) en el contexto del `create_mid`.
- U-b: el valor devuelto por vf48.
- U-e: el controlador del **actor** (`actor+0x90`), no `desc+0x90` (corregido el 14-10: `desc+0x90` es un byte del
  descriptor que lee `0x2792456`). Implementado como `user2_npc_control` (`u0-resultados-s28.md` §8.17): solo el
  registro `eax` en la escritura `0x2792A7B` (`mov [r14+0x90],eax`), sin tocar la memoria del actor.
- U-f: `*param_2 = 0` al saltarse un elemento.
- Las llamadas nativas de §5.3 escriben lo que escribe el propio juego. Nada más: ni `+0x180`, ni `+0x90` del gestor de
  horarios, ni el estado.
- **Copia de seguridad** de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups/` antes de cualquier prueba en modo 1 (regla
  del proyecto).

### 5.7 Sonda (fase 0, `user2_stages=probe`): solo registro

Todo con los puntos U-0 de §5.3 más una lectura cada 5 s en el hilo del módulo. Anillo sin bloqueo; líneas desde su
hilo, como `shop_probe`.

- `UserStages [mgr] host … | user 2 mgr 0x… state S 68A x 689 x cell (X,Z) lists seq N sched N faction N active N |
  sm2 +0x90 N | chains: 2043/2045/2046 of user 2 seen Δs ago (routed to: host|own)`.
- `UserStages [dist] cells D (server positions host (…) user 2 (…)) | would: OFF→ARM / stay / ON→DRAIN | host stages
  within 3/4/5 cells of user 2: a/b/c` (el solape en vivo de §3.2, con todas las muestras).
- `UserStages [close] stage 0x2000xxxx owner 0xA… reason R ret exe+0x… (caller) | npcs alive N, nearest to host/user 2
  m` (A1 + A7).
- `UserStages [unload] mgr 0x… (host|user 2) at the load of 0x… (2437|2283) | lists …` (A5 + A6).
- `UserStages [create] sm 0x… (host|user 2) data 0x… | same data alive in the other manager: yes/no` (A8).
- `UserStages [sched] sm 0x… updated under subject 0x… (opcode)` (A9; la cadena extra del anfitrión del §1.2).
- `UserStages [would arm] … preconditions …` y `[would drain] …`.
- Resumen cada 60 s.

Responde antes del modo 1:
- (a) el estado real de `mgr2`, `sm2+0x90` y la cadena 2046;
- (b) que el motivo 19 sale de `0x287D8D5` y quién emite el 30;
- (c) el solape en vivo con muchas muestras;
- (d) los cierres `AutoSpawnOwnerData` (D11);
- (e) que la posición del servidor del anfitrión montado es buena.

### 5.8 Fases

1. **Fase 0**, `probe` en un PC con `cdmp_peer` (§6.1). No cambia nada.
2. **Fase 1**, modo 1 con `user2_stages_elements=schedule`: U-a, U-b, U-c, U-d, U-e, U-f y U-h. Primero en un PC; después,
   Shadow.
3. **Fase 2**, `schedule+funcnpc`: tiendas propias. Necesita U2 (§5.9).
4. **Fase 3**, opcional: U-i (§5.10), si los umbrales dejan demasiado hueco.

### 5.9 U2 (tiendas y escenas del gestor del usuario 2; después)

- En el desvío de vf48: para las Req de escena del usuario 2 (2436, 2591, 2704, 2105, 2276, 2173, 2100, 2671 y el
  temporizador 2044 `StageBranchStateOnceTimer`) cuyo id de secuenciador **está en `mgr2`**, devolver `mgr2`.
  - La búsqueda es la nativa `0x1F6C190(mgr2, id)`, en el gancho y con el cerrojo `[mgr2+8]`.
  - Es la pieza 2 de S2 (`p8-s12-tenderos-servidor.md` §5.2) pero sin gemelas: la escena ya es del usuario 2.
- El enlace al tendero lo resuelve el mapa del campo (el mismo NPC). Con U solo hay escena propia lejos del anfitrión,
  así que no hay doble diálogo con la del anfitrión, salvo en el traspaso.

### 5.10 U-i (opcional): arbitraje por dato de horario

- En la entrada de `0x28078E0`, para `sm2`: si `[sm_anfitrión+0x30]` tiene viva una stage con el mismo
  `[[stage+0x60]+0x1C]`, devolver el error de capacidad nativo (`DAT_146cf6b60`, el camino `10 < peso`).
  `0x288A970` lo propaga (`*param_2 = res`) [E].
- **Riesgo [H]:** no está claro que el elemento se reintente. Si la stage madre ya arrancó, no se reevalúa hasta que
  `pj2` cambie de celda: puede quedar un hueco.
- Solo si la sonda A8 muestra duplicados reales con umbrales más bajos.

### 5.11 Riesgos

| Id | Riesgo | Mitigación / medida |
|---|---|---|
| R1 | Aldeanos genéricos duplicados donde se tocan las ventanas | Umbrales 10/7 de lo medido; los únicos, protegidos por el registro del campo (§3.1); A8 cuenta «mismo dato vivo en el otro gestor» |
| R2 | Secuenciadores globales o de misión arrancados dos veces (clima, `quest_progression`, `organization_*`) | No se llama a `0x287EF60`/`0x287F7D0`; la segunda puerta `0x29113CB` sigue cerrada; U-f solo deja elementos de horario |
| R3 | NPC nuevos sin IA a ≥ 600 m del anfitrión | U-e (controlador = usuario 2 al nacer); sin él, fase 1 solo para medir |
| R4 | Fuga de stages ajenas en el M del cliente del anfitrión | U-h |
| R5 | Cadena 2045 bloqueada (`+0x90`) o 2046 muerta | Desbloqueo por 2045/2046 nativo programado (U-c), sin escribir contadores |
| R6 | `AutoSpawnOwnerData` cerradas por los sectores del campo (D11) | La sonda lo cuenta; si pasa, esas stages no se arreglan con U [H] |
| R7 | `0x1071C6E0` del usuario 2 manda paquetes de región nuevos al invitado | Efecto nativo de un jugador; registrar en fase 1 [H] |
| R8 | Reentrada o cerrojos al llamar a `0x287F450` / `EnqueueTimer` desde la salida de un `ExecuteTroc` | Mismo patrón y comprobaciones que el emisor de `observer_milestone_reload`; `0x102CA7F0` toma `[mgr+8]` él mismo; no se llama desde dentro de un gancho de función del gestor |
| R9 | U-b demasiado amplio (otras vf48 del mismo `ExecuteTroc` que esperan al dueño) | Solo opcodes 2043/2045/2046, solo con sujeto `pj2`, solo en ON/DRAINING. Los contadores de retorno de `user2_funnels` muestran todas las vf48 de esos Troc |
| R10 | Un crash va a Pearl Abyss | Lecturas SEH; funciones de la build comparadas byte a byte; fase 0 primero; cualquier fallo apaga el módulo con una línea ERR |
| R11 | Coste de CPU y red del servidor del anfitrión con dos ventanas | Medir con la sonda: 2043/2045 por minuto, paquetes a la sesión 2 (R4 de `mundo-invitado.md`) |
| R12 | Parpadeo en el traspaso (U-d con el anfitrión a ~7 celdas: lo del invitado se descarga y el anfitrión no lo recrea hasta tenerlo a ≤ 3 celdas) | Es el precio de no duplicar. Si molesta: bajar `off_cells` (aceptando R1) o hacer U-i |

---

## 6. Verificación

### 6.1 Un PC (anfitrión + `cdmp_peer`)

- **Montaje:** `cdmp_peer --mode replay --script <login>.troc --record peer.bin` (script de login del S25,
  `docs/research/data/login_e3lc00_112627.troc`, como en las pruebas de cuerda). El usuario 2 se queda donde entra, o
  anda con un script de 2556.
- **Separación:** lo que cuenta es la distancia, así que basta con que **el anfitrión se aleje** a caballo más de 700 m
  (≥ 11 celdas), espere más de 10 s, vuelva a menos de 450 m y después haga un viaje rápido.
- **Fase 0** (`probe`), en el log del anfitrión:
  - `[mgr]`: estado de `mgr2` (se espera 1), listas, `sm2+0x90`, cadenas;
  - `[dist]`: celdas y «would ARM» / «would DRAIN»;
  - `[unload]` en el viaje del anfitrión: solo `mgr_anfitrión`;
  - `[close]`: motivo 19 desde `0x287D8D5`;
  - y el solape en vivo.
- **Fase 1** (modo 1):
  - en `peer.bin`, **2335 con dueño = la clave del personaje del peer** al alejarse el anfitrión, y NPC nuevos (2005)
    a < 150 m del peer;
  - en el viaje rápido del anfitrión, **ningún 2126 ni 2516 de motivo 19 con dueño del peer**;
  - al volver a < 7 celdas, un `[drain]` y los 2126 del peer;
  - en el log, `ShopProbe [gate] … 0xA0… (user 2) … OWNER (via U)`, `updates` del usuario 2 creciendo, y `U-h` con 0
    paquetes ajenos en el cliente del anfitrión.
- **Usuario 2 andando:**
  - `make_grapple_script.py … --no-*` sirve para cortar un paseo a pie de una captura (`--move-hz 5`) y sustituir la
    clave por `PLAYER_KEY`;
  - para un paseo largo hace falta un generador nuevo, `make_walk_script.py`: puntos de paso tomados de un camino real de
    captura e interpolados a 1,5–4 m/s y 5 Hz, con la plantilla de un 2556 real a pie.
  - El servidor aceptó el paseo de 60 m de S25. Un paseo largo con interpolación recta está por probar [H].
  - Comprueba que la ventana sigue a `pj2`: cierres de ventana con dueño = peer.
- **Lo que el peer no prueba:** no carga mundo, no simula IA, no muestra nada.

### 6.2 Lo que necesita Shadow (avisar al usuario antes de la fase 1 de dos PC)

- Que el **cliente del invitado** cree las stages propias en M, enlace sus NPC y los muestre con su horario (sin
  `[LATE]` en `GuestSchedLoad`, `GuestSchedRoute` con `OwnCharacter`).
- Que los NPC nuevos **se muevan** (U-e: controlador = usuario 2, 2556 del invitado para ellos).
- Que no haya duplicados visibles en el traspaso, ni parpadeo excesivo.
- Que el viaje rápido del anfitrión **no vacíe** el pueblo del invitado (C1), y que su propio viaje o reaparición sí
  descargue y recree lo suyo (U-d + U-c).
- Rendimiento del invitado y del túnel con la población propia.
- Fase 2: tiendas propias (2661 propio → aviso → 2671/2720/2436 a `mgr2`).

---

## 7. Direcciones (build 1.0.0.2976)

| RVA | Qué es | Estado |
|---|---|---|
| `0x288B7A0` | `ServerSequencerStageManager` update; puerta `0x288B867–0x288B881` (`cmp [r15+0x180],eax` en `0x288B87A`) | [V] bytes, [E] resto |
| `0x287DBB0` | Tick: `+0x68A` → `0x287EF60`; `+0x6A0 == 1` → `0x287D4C0` (`0x287DD6B`); si no, `0x288B7A0` (`0x287DE14`) y reprograma el 2043 | [E] |
| `.xtls 0x1082F900` / `0x108309B0` / (2046) | Manejadores 2043 / 2045 / 2046; vf48 con retorno `0x1082F96A` / `0x10830A0F` / `0x10835A8F` | [V] |
| `0x287DA50` → `.xtls 0x102C2A30` | Paso de carga del dueño: `+0x6A0 = 1` + 2043 inmediato. Único llamador: `0x2BCA7C9` (`SendMoveFieldAck 0x2BCA3D0`, solo si el que carga es el dueño; vf48 en `0x2BCA7BA`) | [V] |
| `0x287D4C0` | Descargar todo; motivo 19 en `0x287D8C0` (`41 B1 13`) → `0x287D8D0 call 0x2808D80` | [V] |
| `0x2808D80` → `.xtls 0x10225FC0` | `DeleteStage` del gestor de horarios (vf6): `0x2817E80(stage, campo, motivo)` | [V] |
| `0x2817E80` | `ServerNPCScheduleStage` vf13: cierre, `Logout(NPC, motivo)`, registro de únicos, 2126 ToNear | [V] |
| `0x287EF60` | Preparación: `0x287F450` (estado 2), `0x2896C60`, `0x287F7D0`, `0x287FA30`, `0x288B7A0`, 2043 periódico (`0x287F18F`), 2322 (`0x287F1D4`), `0x28431A0` | [V] llamadas |
| `0x287F450` → `.xtls 0x102CA7F0` | Estado 2 y vaciar tablas por celda | [V] |
| `.xtls 0x102C4060` | Reinicio completo del gestor (estado 0, celda 0, …) | [E] |
| `0x2774430` | `ServerNet::EnqueueTimer`; `g_ServerNet` = `[exe+0x6D69680]`; periodo del 2043 `[exe+0x6D06098] × 10` ms; troc `[exe+0x6FA3ED8]` | [V] |
| `.xtls 0x1025C310` / `0x1025E4C0` / `0x2822DE0` | Gestor de horarios: programar 2045 si `+0x90 == 0` / actualizar y `+0x90 -= 1` / actualización (dueño exigido, posición del propio usuario) | [V] |
| `0x28078E0` | Crear stage de horario (dedup solo en su gestor, peso ≤ 10, clave de `0x1F6B6E0`) | [E] |
| `0x1F6B6E0` → `.xtls 0xDE10700` | Clave y id global: `clase<<29 | ++[exe+0x6C8CDF0]` | [V] |
| `.xtls 0xE2B9EE0` / `0xE2B7070` / `0xE2B8B50` | Registro de personajes únicos del `ServerField` (`+0xD8…+0xF0`, dato `+0x14B`) | [V] |
| `0x2817130` | No genera un único ya registrado | [E] |
| `0x2911300` (`0x29113AD–0x29113CE`) | Segunda puerta de dueño (evaluación de misiones), llamada en `0x28819CF` | [V] bytes |
| `0x2B047C0` | Indicadores globales del campo, con su propia puerta de dueño | [E] |
| `0x2BB4020` | `ServerSequencerPlayDataActorComponent::vf30`: baja del NPC → gestor de horarios del dueño (`0x28069A0`) | [E] |
| `0x287DFE0` | Stages `AutoSpawnOwnerData` frente a los sectores del campo `[campo+0x228]` | [E] |
| vtables | `ServerSequencerStageManager 0x5B17A58`, `ServerNPCScheduleSpawnManger 0x5B16968`, `ServerNPCScheduleStage 0x5B169B0`, `ServerField 0x5B29A60` | [V] |

## 8. Lecturas pendientes (resueltas el 13-10, salvo lo marcado)

- **`EnqueueTimer 0x2774430`** (`→ .xtls 0x100B4050`), firma
  `(net rcx, int* res rdx, u32 retardo r8, actor r9, [rsp+0x20], troc [rsp+0x28], const void* carga [rsp+0x30],
  u16 tamaño [rsp+0x38])` [V decompilado]:
  - **`[rsp+0x20]` no se usa.** En `0x287F18F` lleva `&[exe+0x5545F50]`, la celda cero que guardó `0x287F0B6` para la
    llamada a `0x288B7A0`, y `EnqueueTimer` no lo lee.
  - Del actor solo copia `+0x60` (clave) y `+0x58`. La carga se copia con `memcpy` (`tamaño` bytes). El troc decide la
    cola por `[troc+0x88]` (0 o 1).
- **2043 periódico** (`0x287F0CC–0x287F18F`) [V]:
  - `rcx = [exe+0x6D69680]`;
  - retardo `= [exe+0x6D06098] × 1000 / 100`;
  - `r9 = [mgr+0x18]`, troc `[exe+0x6FA3ED8]`, tamaño `0x10`;
  - carga `FB 07 | FF | 8 bytes de [exe+0x5545F50] (cero) | 01 00 00 00 | 00` (`+0xB` = reprogramar).
- **2045** (`.xtls 0x1025C3E9–0x1025C4BE`) [V]:
  - `rcx = [exe+0x6D69680]`;
  - retardo `= (i32)[exe+0x6D05878] / 100`, sin el × 1000 del 2043;
  - `r9 = [sm+0x18]`, troc `[exe+0x6FA3E58]`, **tamaño 3**;
  - carga `FD 07 | xx`: el tercer byte sale de un valor calculado en `.xtls` (`0x1025C392`), **0xFF** por analogía con
    el 2046 [E].
  - Contador: `inc dword [sm+0x90]` en `0x1025C37C` **antes** de la rama. Con la rama directa lo devuelve
    `0x1025E4C0`; si la cola falla, lo resta `0x1025C504`.
- **2046** (`UpdateFactionSpawnStageOnceTimer`, gestor `[mgr+0x10]`, `.xtls 0x101FBFA0`) [V]:
  - **contador de pendientes = byte `[fm+0x31]`**: sale si no es directo y `+0x31 > 0`; si no, `inc byte [fm+0x31]`;
  - retardo `= (i32)[exe+0x6D05878] / 100`;
  - `r9 = [fm+0x18]`, troc `[exe+0x6FA3E48]`, `rcx = [exe+0x6D69680]`;
  - **tamaño 3**, carga `FE 07 | FF` (`0x101FC0B1–0x101FC0B9`).
- **Entrada de `0x2791CD0`** (U-e): `48 8B C4 | 48 89 58 18 | 48 89 50 10 | 55 56 57 41 54 41 55` [V]. Es el mismo
  prólogo `mov rax,rsp` que `0x288B7A0`/`0x28078E0`; un gancho inline en la entrada reubica 11 bytes enteros.
- **Entrada de `0x288A970`** (U-f): `48 89 5C 24 08 | 48 89 74 24 10 | 48 89 7C 24 18 | 55 41 54 41 55 41 56 41 57`
  [V]. Firma `(mgr rcx, int* res rdx, stage r8, elemento r9, …)`. Las tres ramas devuelven `param_2`; la rama de horario
  propaga el resultado de `0x28078E0`.
  - Pendiente [H]: que devolver `*res = 0` sin crear el elemento no deje la stage madre a medias. Lo medirá la fase 1
    con `elements=schedule`.
- **Motivo 30:** no está como inmediato en `.rsrc` (`reason.py` sobre `0x2780000–0x2900000` solo da `0x25` y `0x30` en
  otras funciones), así que sale de `.xtls` o de una variable [V la búsqueda]. La sonda A7 (`0x2808D80`, dirección de
  retorno) y A1 (`0x2817E80`, `[rsp]`) lo identifican.

### 8.1 Estado de la implementación de U-0

- Implementada el 13-10 en `opcion-d`, en el worktree principal (§9). Al principio se iba a hacer en una rama aparte,
  pero no se pudo crear su worktree.
- Solo existe la sonda (`probe`). El modo 1 (U-a…U-h) **no está implementado**.

---

## 9. Implementado (U-0): la sonda de solo lectura

Build 1.0.0.2976. Clave `[session2] user2_stages = 0 | probe`, **solo en el anfitrión**, con `create_session=1`.
- `1` no existe en esta versión: se queda en `probe` y lo dice con un WRN.
- No escribe nada en el juego, no llama a funciones del juego y no toca paquetes.
- Ya está en `cdmp.p8-s27-host-paso1.ini` y `cdmp.p8-s25-host-activo.ini`.

Código:
- `src/mod/session2/user2_stages.{h,cpp}`: los ganchos y el hilo;
- `user2_stages_logic.h`: la lógica pura;
- `tests/optd_user2_stages_tests.cpp`: los tests;
- en `user2_funnels.{h,cpp}`, `user2_funnels_subject_now`: el sujeto del `ExecuteTroc` en curso, solo lectura.

### 9.1 Qué engancha

Ganchos `create_mid` en **entradas de función** de `.rsrc`, con `p8_install_mid`: bytes comparados antes de escribir y
reintentos de `hook_prepare`.
- Se instalan diferidos, tras `WorldSystem`.
- Un barrido de los bytes de todas las secciones ejecutables no encontró saltos `rel8/rel32` que caigan dentro de un
  parche; los dos aciertos eran bytes en mitad de instrucciones.
- Los cuatro *thunks* son `jmp .xtls` de 5 bytes, como `0x20DEE60` en `user2_funnels`.

| Letra | Dirección | Qué registra |
|---|---|---|
| C | `0x2817E80` (cierre de stage) | clave, dueño, **motivo** (`r8b`), **llamador** (`[rsp]`), NPC vivos, posición |
| D | `0x2808D80` (DeleteStage) | clave, dueño del gestor de horarios, motivo (`r9b`), **llamador** (`0x287D8D5` = descarga) |
| L | `0x287DA50` (carga del dueño) | qué gestor pasa a estado 1, y el sujeto |
| U | `0x287D4C0` (descargar todo) | gestor, estado, listas |
| N | `0x28078E0` (crear stage de horario) | dato de horario, y si el **otro** jugador lo tiene vivo (duplicado) |
| T | `0x287DBB0` (tick del 2043) | a qué gestor llegó cada 2043, y si su sujeto era el usuario 2 |
| S | `0x2822D00` (horarios) | lo mismo para el 2045 (`r8b = 0`); `r8b ≠ 0` = llamada directa dentro de una actualización |
| F | `0x27FBD60` (facciones) | lo mismo para el 2046 |
| P | `0x287EF60` (preparación) | qué gestor se prepara, su estado y el sujeto |

- La puerta `0x288B87A` y la entrada `0x288B7A0` siguen siendo de `shop_probe` (un dueño por dirección). Sus
  `ShopProbe [gate]` y `stage update per character` dan las entradas por personaje.
- El sujeto viene de `user2_funnels`, así que hace falta que esté encendido. En los dos ini lo está.

Hilo propio (cada segundo; línea cada 5 s si algo cambia, si no cada 30 s):
- lee los dos jugadores: personaje, gestor `[[[pj+0x68]+0xA8]+0x18]` (vtable `0x5B17A58`), gestor de horarios
  (`0x5B16968`) y gestor de facciones;
- y la **posición del servidor**: `TransformSync+0xB4` con el padre `+0xD0`, la misma cuenta que
  `0x4F8220`/`0x50EA80`. Con el anfitrión montado no vale cero.

### 9.2 Líneas para buscar en el log del anfitrión (`bin64\cdmp.log`)

- `User2Stages (user2_stages=probe…): C … hooked | D … hooked | …`: qué ganchos entraron. Un `REFUSED` va acompañado
  de su línea de error.
- `User2Stages [state] … host: … manager … state S (+0x689 x +0x68A y) last cell (X,Z) seq … active … keep … |
  schedules: stages N weight W pending 2045 P | factions: stages N pending 2046 P | server position (…) cell (X,Z) || user 2: … ||
  distance D cells … | host schedule stages read N: within 3/4/5 cells of user 2 a/b/c; from the host m..M cells | user 2's schedule
  stages read N || U would be ON/OFF | arm check on user 2's manager: …`
- `User2Stages [would ARM]` / `[would DRAIN]`: los cruces de los umbrales (≥ 10 celdas durante 10 s / ≤ 7 durante 5 s).
- `User2Stages [close] … reason R = … | called from exe+0x… | live NPC entries …`.
- `User2Stages [delete] … reason R | caller exe+0x…`.
- `User2Stages [owner load] …`, `[unload] …`, `[prepare] …`.
- `User2Stages [create] … the same data alive in the other player's: YES/no`.
- `User2Stages [timer] 2043/2045/2046 reached the manager … (host|user 2) | subject user 2|none`: solo cuando interviene
  el usuario 2.
- `User2Stages summary` (cada 60 s): tablas de cierres y `DeleteStage` por (motivo, dueño, llamador), cargas, descargas,
  preparaciones, creaciones y duplicados, y la matriz de temporizadores → gestor alcanzado.

### 9.3 Qué decide cada resultado

| Resultado | Para qué pieza | Lectura |
|---|---|---|
| Gestor del usuario 2 en `state 1`, listas a 0 → `arm check: READY` | U-c | El armado mínimo (`0x287F450` + 2043) vale tal cual. Si sale `state 2` o listas no vacías: replantear U-c antes del modo 1 |
| `pending 2045` del usuario 2 ≥ 1 sin `[timer] 2045 … subject user 2` nuevos | U-c | Confirma la cadena 2045 bloqueada: U-c tiene que programar un 2045. Lo mismo con `pending 2046` |
| `timers`: `2043 [subject user 2: host N user2 0]` | U-b | Confirma la ruta por vf48. Con `user2 > 0` habría que revisar U-b |
| `[close] reason 19 … from exe+0x…` en el viaje del anfitrión, con `owner load` solo del anfitrión | U-d, §2 | El motivo 19 sale de la descarga del gestor que carga. El llamador de `[delete]` es `exe+0x287D8D5` |
| `[close]` o `[delete]` con **motivo 30**: su `called from` / `caller` | §8 | Quién cierra por ventana. Si es `.xtls`, la siguiente pieza a leer es esa dirección |
| `distance` y `within 3/4/5 cells` en el paseo | Umbrales §3.3 | Si a ≥ 8 celdas hay stages del anfitrión a ≤ 5 del usuario 2, subir `on`/`off` |
| `server position … (child of a mount)` con distancia razonable | Sonda del ancla (A4) | Confirma que la posición del servidor sirve con el anfitrión montado |
| `[create] … YES` | R1 / U-i | Duplicados reales del mismo dato. Con U apagado deberían ser 0 |
| `[prepare]` del usuario 2 | §1.2 | Cuándo se prepara su gestor; se espera solo en su campo inicial |
| `user 2's schedule stages read N > 0` | §1.2 | Si el gestor del usuario 2 tiene stages vivas, algo las creó: revisarlo antes de U |
| Un gancho `REFUSED` | — | Ese dato falta. Avisar con la línea de error |

### 9.4 Qué hacer en el juego (dos PC, con Shadow)

1. Desplegar con el ini del paso que toque (los dos llevan `user2_stages=probe`). El invitado entra como siempre y
   **se queda quieto** en el pueblo.
2. El anfitrión, a caballo, se aleja **más de 700 m** (≥ 11 celdas) y espera **1 minuto** allí. Debe salir un
   `[would ARM]` a los 10 s.
3. El anfitrión vuelve junto al invitado (a menos de 450 m). Debe salir un `[would DRAIN]`.
4. El anfitrión hace **un viaje rápido**. Se esperan `[owner load]` del anfitrión, `[unload]` y `[close] reason 19`.
5. Dejarlo un par de minutos más y cerrar con normalidad.
6. Traer el `cdmp.log` del anfitrión (y la captura). Del invitado no hace falta nada para U-0.

- Esto también se puede hacer en un PC con `cdmp_peer` en lugar de Shadow: el peer se queda quieto y el anfitrión hace
  los pasos 2–4.
- Lo único que Shadow añade es el lado del invitado, que U-0 no mide.
