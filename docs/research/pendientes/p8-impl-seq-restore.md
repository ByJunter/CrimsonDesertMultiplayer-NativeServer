# P8 · Complemento de restauración `seq_bodyanim` en el invitado (`seq_actor_restore`)

10 de octubre de 2026, rama `opcion-d` (sobre `3710b14`). Build del juego **1.0.0.2976**, direcciones en **RVA**
(base `0x140000000`). Aprobado por el usuario. No se lanzó el juego ni se desplegó nada. Nada de `.xtls`: ni se
parchea ni se engancha (la única lectura es `0x2329A90 → jmp .xtls 0xEE58A40`, que se cita y se deja).

**[V]** = comprobado (capstone sobre el PE de solo lectura `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, logs y
capturas de la sesión 8, compilación, pruebas). **[E]** = hipótesis pendiente de la prueba en el juego.

Base: `p8-s8-escenas.md` §4.2 (mesa de pulsos y Damiane), `p8-actores-anfitrion.md` §1.3 y §4.1 (ii) (el
«complemento»), `p8-impl-opcion-a.md`, `p8-impl-escenas-play.md`, `p8-s1-escenas-npc.md` y el informe independiente
`observador-raiz-20261010-work\INFORME-RAIZ-OBSERVADOR.txt` (§3.3, §5.2: la mesa sí se anunció; la escena se rechazó por
otro actor enlazado; «no esperar a CF1 de todos los enlazados»).

## 0. Clave nueva en `[session2]`

| Clave | PC | Valores | Qué hace |
|---|---|---|---|
| `seq_actor_restore` | invitado | `0` \| `probe` \| `1` | Si un actor remoto llega con una acción `seq_bodyanim` que ningún gráfico tiene, esa capa restaura la espera `0x26107D3C` (o nada) en vez de fallar el login. `probe`: decide, cuenta y escribe las líneas; no escribe nada en el juego |

- Inis: `cdmp.p8-s7-shadow.ini` con `seq_actor_restore=1` (§6). En `cdmp.p8-s7-host.ini` solo un comentario: en el
  anfitrión no instala nada.
- Código: `src/mod/session2/guest_seq_restore.{h,cpp}`, lógica pura en `guest_seq_restore_logic.h`, pruebas en
  `tests/optd_seq_restore_tests.cpp` (`optd_seq_restore`). Enganche: `guest.cpp` (arranque y línea de estado),
  `guest_diag_worker.cpp` (líneas cada 5 s), `config.{h,cpp}`. No se tocó `observer_seq_copy`, `guest_seq_play`,
  `guest_seq_route`, `child_hold`, `remote_interp`, `attach_follow` ni `server.cpp`.

## 1. El fallo, con datos de la sesión 8 [V]

| Hecho | Fuente |
|---|---|
| En toda la sesión hubo **12** logins fallidos en el invitado, **12** con `eErrNoInvalidActionChart`, todos de actores no controlados aquí | GL `GuestDiag … login failures 12 (not controlled here 12, action chart 12)` |
| De 2191 restauraciones de actores remotos: 2179 bien, **12 `CHART FAILED`**, 0 por otra causa | GL `restore 2191 calls … CHART FAILED 12, other failed 0` |
| Las 12: capa 0 `0x837487D4` (`seq_bodyanim_action`) y capa 1 `0x35DD392F` (`seq_bodyanim_action_loop`), `NOT FOUND (no fallback graph)`, `[CC+0x70/+0x78] = 0` | GL `P8Probe [Restart 0x86392A]`, `GuestDiag restore` |
| Son `B010006D`–`B0100076` (secuenciador `0x13`, grupo de Damiane) y `B0100F36`–`F3A` (secuenciador `0x1B0`, Wandering Freesword de la mesa de pulsos) | GL `GuestDiag login FAILED` |
| Sus 2661 copiados llegan después y se rechazan: `0x13` por `B010006D` (t=53,71) y `0x1B0` por `B0100F36` (t=600,357) | GL 1345, 11793 (`GuestStage [2661 missing actor 0xA9CE87]`) |
| Son **los únicos dos rechazos** de los 45 2661 copiados | `p8-s8-escenas.md` §2.2 |

Cronología de `B0100F36` en el invitado (GC): 2005 entregado ≈600,10; su componente de secuenciador ya dice
`owner 0xA0100001, comp+0x2E0 0x1B0` (600,130); restauración fallida 600,2506; `login FAILED` 600,25; el actor
desaparece; el 2661 copiado llega 600,357 → rechazado. G5 al final: `NOT in this client`.

**El gráfico de la espera está en esos mismos gráficos [V].** Los 12 usan cuatro objetos de gráfico:

| Gráfico (capa, tamaño) | Actores con `seq_bodyanim` | ¿Resolvió `0x26107D3C` para otros NPC? |
|---|---|---|
| `0x488BC18F1E0` (capa 0, 27173) | 11 | sí, **86** veces el mismo objeto |
| `0x488C05B4A60` (capa 1, 4645) | 11 | sí, **273** veces el mismo objeto |
| `0x488BC18E1A0` (capa 0, 28096) | 1 (`B010006D`) | otro objeto del mismo tamaño (`0x488900D8840`) sí, 2 veces [E: mismo recurso] |
| `0x488BC18F5A0` (capa 1, 7183) | 1 (`B010006D`) | otro objeto del mismo tamaño (`0x48809482BC0`) sí, 1 vez [E] |

En cualquier caso el gancho lo comprueba en cada llamada con la misma búsqueda que hace el juego (§2.3).

## 2. Opciones evaluadas

### 2.1 (b) Anfitrión: retener los 2005 o mandar el 2661 antes — descartada [V]

- **2661 antes que el 2005: imposible.** `0xA9CA40` (creación de la instancia) recorre los enlaces; para tipo 4/5
  (`0xA9CB87`) busca el actor (`0x8AF170` sobre el `ClientActorManager`) y, si no está, `0xA9CC13 je 0xA9CE81`:
  rechaza **todo** el secuenciador. No hay enlace diferido para tipo 4/5 (el tardío `0x606420 → .xtls` es del lado
  del componente del NPC, no de la creación) [V capstone].
- **2661 justo detrás del 2005: no basta.** Aunque el 2661 llegara en el mismo paquete, la instancia solo se *crea*
  (carga asíncrona `+0x174`, lista `+0x1B0`); el gráfico dinámico `[CC+0x70/+0x78]` lo instala `BodyAnim 0xA79770`
  **al reproducirse**, que en el mejor caso (con `guest_seq_play`) es un tick y un `+0x231 = 3` después. La
  restauración corre 0,12–2,5 s después del 2005 (`0x1B0`: 0,12 s; `0x13`, en carga: 2,5 s) y no espera a nada.
- En el anfitrión el orden nativo no importa: allí los NPC son de control local (modo 3) y el llamador `.xtls
  0x984D830` **nunca** restaura (`vf188(5)`). Retener 2005 en el anfitrión solo añadiría latencia.

### 2.2 (c) Reintentar el 2661 cuando el actor vuelva — descartada [V]

- El actor **no vuelve**: el servidor cree que el invitado lo tiene; G5 dice `NOT in this client (unchanged)` hasta
  el final. Si volviera (salir y entrar en la zona), su 2005 traería otra vez `seq_bodyanim` y fallaría igual.
- Solo tendría sentido junto con (a), y con (a) ya no hace falta: el actor existe cuando llega el 2661.

### 2.3 (a) Invitado: el complemento de restauración — elegida

Es la más nativa y la de menor riesgo:
- Actúa **solo** cuando el juego ya iba a fallar (y destruir el actor). En los demás casos no cambia nada.
- El resultado es uno de dos estados que el juego ya produce para cientos de NPC remotos: «restaurado con la espera
  `0x26107D3C`» (la acción que traían el posadero, Dahlia y otros) o «sin restauración» (byte de restauración 0).
- Rompe la dependencia circular que señala el informe independiente: el actor existe (con `+0xCF = 0`, como los otros
  64 NPC de secuenciador de la sesión 8) → el 2661 se acepta → la escena se reproduce (`guest_seq_play`) → el
  enlace limpia `comp+0x3D8` → `+0xCF = 1`. No se espera a `CF1` ni se fuerza.
- **¿Retrasa la carga?** No en la sesión 8: los 64 NPC de secuenciador con `+0xCF 0` existían durante el estado
  `0x12` y este duró 1,5 s (`0x11 → 0x12` en t=56,18, `0x12 → 0x13` en t=57,68) [V]. Los de `0x13` serán uno más de
  ese grupo [E].

## 3. Dónde y cómo [V]

`RestoreActionsFromLoadData 0x8646D0(CC, int* res)`; el punto es `0x86490F`.

| Dirección | Bytes | Instrucción | Para qué |
|---|---|---|---|
| `0x8646FC` | `48 8B F2 4C 8B E9` | `mov rsi,rdx ; mov r13,rcx` | `rsi` = res, `r13` = CC (no se vuelve a escribir) |
| `0x864724` | `45 33 F6` | `xor r14d,r14d` | `r14 = 0` hasta `0x864971` |
| `0x86473F` | `49 8B 45 40 44 38 70 30 75 59` | byte de restauración | con 0: `mov [rsi],r14d` y salida |
| `0x8647ED` | `49 8B 8D 88 00 00 00 48 8B 81 10 01 00 00 48 89 44 24 38` | gráfico de la capa 0 → `[rsp+0x38]` | |
| `0x864811` | `8B 41 3C 89 44 24 50` | (con gráfico) hash → entrada 0 `[rsp+0x50]` | |
| `0x864862` | `49 8B 8D 90 00 00 00 48 8B 81 10 01 00 00 48 89 44 24 40` | gráfico de la capa 1 → `[rsp+0x40]` | |
| `0x86487A` | `8B 41 3C 89 45 C8` | (con gráfico) hash → entrada 1 `[rbp-0x38]` = `[rsp+0xC8]` | |
| `0x8648CA`–`0x86490B` | | liberación del cerrojo de `0x864739` | |
| **`0x86490F`** | **`49 8B 5D 08 48 8D 53 08`** | **`mov rbx,[r13+8] ; lea rdx,[rbx+8]`** | **el punto** (8 bytes, todo camino con byte ≠ 0 pasa por aquí) |
| `0x864AE0` | `48 8B B5 A8 00 00 00 45 33 F6` | `mov rsi,[rbp+0xA8] ; xor r14d,r14d` | (camino del bucle) res desde su ranura |
| `0x864AEA` | `4C 8D 44 24 50 … E8 82 ED FF FF` | `call 0x863880(CC, &out, entradas)` | |
| **`0x864B1A`** | `44 89 36 48 8B C6 …` | `mov [rsi],r14d ; mov rax,rsi ; … ret` | **la salida del «saltar»** |

- Ninguna rama cae en `0x864910`–`0x864916`: barrido de rel32 de todas las secciones ejecutables y de las ramas de
  la propia función (capstone) [V]. Sin RIP-relativo en los 8 bytes. `p8_install_mid` exige `.rsrc`, los 16 bytes
  esperados y que SafetyHook reubique exactamente los 8 verificados.
- `0x863880` (`RestartLayerAction`) [V]: por capa con hash ≠ 0, `ActionGraph_Find 0x24314C0` en
  `[[CC+0x88+8i]+0x110]` y, si no, en `[CC+0x70+8i]`; capa con hash 0 = se salta; las dos a 0 o un triple
  incompleto = `eErrNoInvalidActionChart`; si no, `0x2329A90` (= `jmp .xtls`, no se toca) aplica las acciones.
- `0x24314C0` [V]: búsqueda binaria sobre `[chart+0x18]` (entradas de 24 B `{p0,p1,p2}`, clave `u32 [p2+0x18]`),
  `[chart+0x20]` elementos, comparaciones sin signo. Sin cerrojo ni asignación. `sar::chart_find` la replica con
  lecturas SEH (verificados los bytes de `0x24314CF`–`0x2431599`; la cabeza la puede enganchar P8Probe).
- Comprobados también al instalar: `0x8646F2` (cabeza tras el prólogo), `0x8638EA`–`0x863929` y `0x863938`–`0x86396F`
  (búsquedas y comprobación del triple de `0x863880`). No se comprueban los puntos de otros módulos (`0x8646FC` y
  `0x864B1D` de `guest_actor_diag`, `0x86392A` de P8Probe): no se pisan.

**Decisión en el gancho** (`sar::plan`, todo en la pila del juego y por lectura):
1. Camino rápido: si ninguna entrada (con gráfico) lleva `0x837487D4`/`0x35DD392F`, no hace nada (dos comparaciones).
2. Actor de clase `0xA…`/`0xB…`, con `+0x90` ≠ 0 y ≠ usuario local (`[[[WorldSystem]+0x30]+0x50]+0x90`). Si no:
   `NotRemote`, nativo.
3. Por capa: búsqueda en su gráfico y en `[CC+0x70/+0x78]`, igual que `0x863880`. Si las `seq_bodyanim` se
   encuentran: `Resolvable`, nativo (la escena ya se está reproduciendo). Si falla otra capa con un hash que **no**
   es `seq_bodyanim`: `OtherFailure`, no se toca nada (el error nativo). Lectura imposible: `Unreadable`, nada.
4. Cada capa `seq_bodyanim` que fallaría: la entrada pasa a `0x26107D3C` si **su** gráfico la tiene; si no, a 0.
5. Si queda alguna capa con hash: `Substituted`, y `0x863880` corre de forma nativa sobre las entradas.
6. Si las dos quedan a 0: `Skipped`, el gancho pone `rip = 0x864B1A` (escribe `[rsi] = r14d = 0`: el resultado de
   «byte de restauración 0»). Solo si `r14d == 0` y `rsi == [rbp+0xA8]`; si no, `CannotSkip`, nada.

`probe` hace todo menos las dos escrituras de pila y el salto.

## 4. Reglas del proyecto

- Solo `.rsrc`; `.xtls` ni se engancha ni se parchea.
- En el gancho: lecturas y como mucho dos escrituras de `u32` en las entradas locales de la función (su propia pila,
  no el montón), un `rip` nuevo; sin CRT, sin asignación, sin cerrojos, sin formateo. Un anillo sin cerrojo
  (`p8p::EventRing<256>`, 32 sucesos cada 5 s) y contadores atómicos.
- Las líneas y el resumen salen del hilo de diagnóstico del invitado (`guest_diag_worker`, cada 5 s; resumen cada
  60 s). El resumen consulta cada actor tocado con `game::find_actor` (lecturas SEH, sin llamar al juego).
- El hilo del mod que instala no usa el montón del juego.

## 5. Líneas en `cdmp.log` del invitado

| Línea | Qué dice |
|---|---|
| `GuestSeqRestore ON (seq_actor_restore): hooked 0x86490F …` | instalado (`PROBE` con `probe`). `… REFUSED …` si algún byte no cuadra: no se instala nada |
| `GuestSeqRestore [restore 0x86490F] … actor 0xB0100F36 control 0x90100000 (local user 0x9010…) \| L0 hash 0x837487D4 (seq_bodyanim_action) chart 0x… (count 27173): not found, fallback [CC+0x70] 0x0: no chart, wait in chart: found -> wait 0x26107D3C \| L1 … \| SUBSTITUTED (idle wait), 0x863880 runs natively \| APPLIED to the entries \| exit registers ok` | cada decisión (no las del camino rápido), hasta 400 |
| `GuestSeqRestore summary (seq_actor_restore=1): restores seen N (with a seq_bodyanim hash M) \| SUBSTITUTED … xK; … \| layers changed A -> wait, B -> 0 \| … \| actors touched K: 0xB0100F36 exists +5E 0x0010 C2 1 CF 0, …` | cada 60 s, con el estado actual de cada actor tocado |
| `… \| seq restore on (seq hashes M, substituted K, skipped S)` | en la línea de estado `Session2 guest` |

**Criterio de éxito en la próxima sesión (mesa de pulsos o Damiane a la vista):**
1. `GuestSeqRestore [restore 0x86490F] … SUBSTITUTED … APPLIED` para `B0100F36`–`F3A` (o los equivalentes).
2. Ningún `GuestDiag login FAILED` para esas claves.
3. `GuestStage [2661 result …] … created` (no `REJECTED`) para su secuenciador.
4. El resumen: `0xB0100F36 exists …`, no `NOT on this client`.
5. Con `guest_seq_play`, cuando la escena se reproduzca: `P8Probe [BodyAnim 0xA79770]` con su clave y `CF 1`.
6. A la vista: los Freeswords en la mesa (de pie en espera hasta que la escena arranque).

**Aborto:** cierre o un NPC en pose rara que no se recupere → `seq_actor_restore=probe` (mide sin tocar) o `0`.

## 6. Por qué a 1 en el ini del invitado

- La causa está **[V]** de extremo a extremo (código de `0x8646D0`/`0x863880`/`0x24314C0`/`0xA9CA40` y los 12 casos
  de la sesión 8). El complemento cubre el **100 %** de los logins fallidos de esa sesión y deja intactos los otros
  2179.
- La espera `0x26107D3C` se resolvió en los **mismos objetos de gráfico** de 11 de los 12 actores [V], y el gancho la
  vuelve a buscar en cada caso.
- El peor caso (`Skipped`) es el estado nativo de «byte 0» (gráfico sin acción), que contienen `guest_layer_guard=1`
  y `guest_update_guard=1`, ya puestos en ese ini.
- `probe` queda como paso atrás sin desinstalar nada.

## 7. Riesgos y abiertos

- **Campos de la entrada:** resuelto en la revisión (§9.7). El único campo que depende de la acción anterior, la
  palabra `+0x3A`, pasa a `0xFFFF`.
- **[E] `0x8AF170` encuentra al actor antes de que termine el login.** Sin el complemento el actor ya no existía;
  con él existe desde el 2005. Lo confirma la línea `created` del punto 3 de §5.
- **[E] La escena puede no reproducirse** (depende de `guest_seq_play`): entonces los Freeswords se quedan en espera,
  de pie, con `+0xCF 0` (como Dahlia o Grover en la sesión 8). Mejor que desaparecer, pero no es la mesa animada.
- **Otros hashes.** Si aparece un login fallido con un hash que no es `seq_bodyanim` (p. ej. un gráfico de
  interacción), sale `another hash fails too: untouched` en las líneas y en el resumen: se deja al juego hasta
  tener evidencia.
- El personaje del anfitrión (`0xA…`) en una escena cuando entra el invitado también pasa por aquí (misma lógica);
  en la sesión 8 no ocurrió.

## 8. Comprobado

- `scripts/build.ps1`: compila y pasan todas las pruebas (las 34 previas + `optd_seq_restore`).
- `optd_seq_restore`: la réplica de `0x24314C0` (gráficos grandes y pequeños, vacíos, entradas incompletas, memoria
  ilegible, cuenta corrupta), cada veredicto de `sar::plan` con los casos de la sesión 8 (`0x1B0`, `0x13`, el
  gráfico dinámico presente, una capa que conserva su acción, la espera ausente, los registros de salida, otro hash
  que falla, el actor propio, `probe` frente a `1`) y `sar::KeyLog` con varios escritores.
- Herramientas: scripts de capstone en el scratchpad de la sesión (no se conservan); no se usó Ghidra.

## 9. Revisión del 10-10 (puntos 1–8) [V capstone sobre el PE de solo lectura, salvo donde se dice]

### 9.1 Convivencia con `guest_actor_diag` y `companion_probe` (ROJO)

- **El fallo:** la versión anterior comprobaba `0x8646F2` (16 B, hasta `0x864701`) y `0x864B1A` (14 B, hasta
  `0x864B27`). Esos rangos incluyen los ganchos de `guest_actor_diag` (`cmp::kRestoreHook = 0x8646FC`, 6 B;
  `cmp::kRestoreExitHook = 0x864B1D`, 11 B). Como el ini del invitado lleva `guest_actor_diag=1`, el resultado
  dependía de qué hilo instalaba antes.
- **Ahora** solo se comprueba lo que este módulo necesita:
  - `0x8646F2`–`0x8646FB` (dos `vmovaps`) y `0x864B1A`–`0x864B1C` (`44 89 36`, `mov [rsi],r14d`, lo que ejecuta el
    salto).
  - En `0x8646FC` (6 B) y `0x864B1D` (11 B) se aceptan dos formas (`sar::classify_shared`): los bytes originales, o
    un `E9 rel32` que apunte **fuera** de la imagen, que es el salto de SafetyHook al trampolín de otro módulo. Un
    `E9` hacia dentro de la imagen o cualquier otro cambio se rechaza. La línea de arranque dice cuál vio
    (`shared sites: exe+0x8646FC original|another module's hook`).
- **En sentido contrario:** las comprobaciones de `guest_actor_diag`/`companion_probe` (`kRestoreSite` `0x8646F2`–`0x864701`,
  `kRestoreLock` `0x86473F`–`0x864748`, `kRestartCall` `0x864AEA`–`0x864B05`, `kRestoreExit` `0x864B1A`–`0x864B27`)
  no tocan el parche de este módulo (`0x86490F`–`0x864916`). Ningún orden de instalación hace que se rechacen.
- **Encadenamiento:** son tres ganchos en direcciones distintas que no comparten bytes; ninguno envuelve a otro. En
  una llamada se ejecutan así:
  1. `0x8646FC` (diag «antes», lee las capas);
  2. `0x86490F` (este módulo, decide y escribe);
  3. `0x863880` nativo, o el salto a `0x864B1A`;
  4. `0x864B1D` (diag «salida», ve el resultado; con el salto, `0`).
- `horse_diag` engancha `0x8646D0`–`0x8646D6` solo en el anfitrión (co-op), fuera de lo que se comprueba aquí (el
  prólogo se comprueba desde `0x8646DB`).
- P8Probe engancha `0x86392A` (8 B) y la cabeza de `0x24314C0` (5 B), también fuera de lo comprobado
  (`0x8638EA`–`0x863929`, `0x863938`–`0x86396F`, `0x24314CF`–`0x2431599`).

### 9.2 Las ranuras de pila

- **Prólogo comprobado al instalar:** `0x8646DB 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 A0 48 81 EC 60 01 00 00`
  (`push ×7 ; lea rbp,[rsp-0x60] ; sub rsp,0x160`). En el cuerpo queda `rbp = rsp + 0x100`. Entre `0x8646F2` y
  `0x86490F` no hay `push`/`pop` netos (las llamadas devuelven la pila igual).
- **En cada llamada** el gancho exige además `ctx.rbp == ctx.rsp + 0x100`. Si no se cumple, el veredicto es
  `BadFrame` y no se toca nada.
- **Direccionamiento:** el mismo que usa el código nativo para cada ranura:

| Ranura | Escritura nativa | Gancho |
|---|---|---|
| gráfico capa 0 / 1 | `0x8647FB mov [rsp+0x38],rax` / `0x864870 mov [rsp+0x40],rax` | `rsp+0x38` / `rsp+0x40` |
| hash entrada 0 | `0x864814 mov [rsp+0x50],eax` | `rsp+0x50` |
| hash entrada 1 | `0x86487D mov [rbp-0x38],eax` | `rbp-0x38` |
| índice entrada 0 / 1 | `0x864838 mov [rbp-0x76],ax` / `0x8648A0 mov [rbp+2],ax` | `rbp-0x76` / `rbp+2` |
| res | `0x8646D7 mov [rax+0x10],rdx` (= `[rbp+0xA8]`), recargada en `0x864AE0` | `rbp+0xA8` |

### 9.3 El hash de una capa sin gráfico

- `0x864702 lea r11,[rsp+0x50] ; mov rcx,r11 ; call 0x1EC98F0 ; add r11,0x78 ; lea rax,[rbp+0x40] ; cmp r11,rax ;
  jne` construye las dos entradas (`rsp+0x50` y `rsp+0xC8`).
- El constructor `0x1EC98F0` hace `xor r9d,r9d ; … ; mov [rcx],r9`: el qword `+0`, que contiene el hash, queda a 0.
  Bytes comprobados al instalar.
- Sin gráfico, `0x86480F je 0x864862` / `0x864878 je 0x8648CA` saltan la escritura del hash, así que queda en 0.
- Aun así el gancho lee **siempre** los dos hashes, como `0x863880` (`0x8638F3 mov esi,[rbx-8] ; test ; je`). Una
  capa con hash ≠ 0 y sin gráfico se busca solo en el gráfico dinámico (`0x863905`/`0x863918`), igual que en el
  juego.

### 9.4 Hilo y carrera con `BodyAnim`

- Sesión 8 (GL) [V log]: las 28 líneas `GuestDiag restore (0x8646D0 …)` son de `T13152`, igual que los 12
  `login FAILED`, las 4 llamadas de `P8Probe [BodyAnim 0xA79770]`/instalación del gráfico, las 45 líneas
  `GuestSeqRoute [2661 …]` y el tick de `guest_seq_play`. Ese hilo es el principal (`0xAD2380`: los manejadores de
  paquetes y el tick de escenas).
- La restauración y la instalación del gráfico dinámico corren, por tanto, en el mismo hilo y no se pueden intercalar
  entre la lectura y la escritura del gancho [E: no hay prueba estática de que nunca se llame desde otro hilo].
- De todas formas, justo antes de escribir, el gancho vuelve a leer `[CC+0x70/+0x78]` (`sar::still_valid`). Si cambió
  respecto a lo que vio el plan, no escribe ni salta (contador `dynamic graph changed before the write`).

### 9.5 Usuario local desconocido

- `local == 0` (sin actor controlado) → veredicto `LocalUnknown`. Es nativo y se cuenta aparte en el resumen.

### 9.6 El salto equivale al camino nativo del byte 0

Camino nativo con byte 0:

```
0x864739 call 0x393650            ; toma el cerrojo (guarda en [rsp+0x20]/[rsp+0x28])
0x86473F mov rax,[r13+0x40] ; cmp [rax+0x30],r14b ; jne 0x8647A2
0x864749 mov [rsi],r14d           ; resultado 0
0x86474C mov rbx,[rsp+0x20] ; … call [rax+0x20] ; cmp byte [rsp+0x28],1 ; … lock xadd … ; lock dec …   ; libera
0x86479D jmp 0x864B1D             ; epílogo
```

- Con el byte 0 **no** se ejecuta nada de `0x8647A2`–`0x864B1C`: ni las entradas, ni el segundo guardián
  (`0x864921 call 0x393DF0`), ni el bucle de tiempos `0x864967`, ni `0x864AEA call 0x863880`, ni `0x85ED00` /
  `0x2329D70`.

Camino con el gancho:
- `0x8647A2`–`0x8648C7`: marca de tiempo y entradas.
- `0x8648CA`–`0x86490B`: la **misma** liberación del cerrojo de `0x864739` (bytes comprobados).
- `0x86490F`: el gancho → `rip = 0x864B1A mov [rsi],r14d` (`r14d == 0` y `rsi == [rbp+0xA8]` comprobados) →
  `0x864B1D` epílogo (`mov rax,rsi ; lea r11,[rsp+0x160] ; … ; ret`).
- Lo que había detrás del punto no se ejecuta: el segundo guardián se toma en `0x864921`, después del punto, así que
  no queda ningún cerrojo tomado; tampoco el bucle, `0x863880`, `0x85ED00` ni `0x2329D70`.
- `0x864954`–`0x864971` (`test bl,bl ; je 0x864AEA ; dec dil ; cmp dil,1 ; ja 0x864AEA ; xor ebx,ebx ; … lea
  r14,[rsp+0x50]`) está detrás del punto: con el salto no se ejecuta, igual que con el byte 0.

Única diferencia con el byte 0:
- `0x8647A2`–`0x8647E9` escribe una marca de tiempo (`[[[actor+0x68]+0x1A0]+0x20] = r8`). Esa escritura ocurre en
  toda restauración con byte 1, incluidas las 12 que fallaban.
- El orden de «resultado / liberar» se invierte; no tiene efecto.

### 9.7 Qué hace `0x2329A90` con la entrada (`.xtls`, solo lectura con capstone; no se engancha)

- `0x2329A90 jmp 0xEE58A40` (`.xtls`, código legible, sin virtualizar):
  1. copia las 2 entradas (`0x1EC9A80`);
  2. por entrada con triple completo y `[p2+0x109] & 0x20` (acción dinámica) llama a `0x1EC7120`;
  3. si la capa 1 tiene gráfico, llama a `0x1EC84C0`;
  4. con la bandera TLS `+0x1EC`, `0x232AC80(CC, entradas)`;
  5. si no, mira las banderas `+0x50`/`+0x51`/`+0x52` y copia cada entrada con hash ≠ 0 a la acción pendiente de su
     capa: `0x86B3A0` es solo un `operator=` campo a campo, hacia `[CC+0x80]+0x78·i`;
  6. por último pone `[CC+0xA8] = 1` y llama a `vf 0x1B0` del actor.
- La entrada se consume después en `0x2329D70`, que copia las pendientes y llama a `0x232AC80`. En `0x232AC80` y en
  `0x2329F00`–`0x232AC80` no hay ningún acceso directo a `+0x3A` (búsqueda en el desensamblado). No se siguió cada
  callee [E].
- **Qué campos dependen de la acción anterior.** Todo lo que `0x8646D0` escribe en la entrada es constante salvo:
  - el hash;
  - la posición `[CC+0x20]` (la del actor);
  - `+0x64`, el tiempo, que recalcula el bucle `0x864967` **con el hash de la entrada**, ya sustituido;
  - `+0x3A` (`word [capa+0x38]`), que es de la acción actual de la capa (la de escena).
- **El valor neutro de `+0x3A` es `0xFFFF` [V]:**
  - lo pone el constructor (`0x1EC994F mov [r10+0x3A],r8w` con `r8w = 0xFFFF`);
  - lo pone el propio juego al sustituir una entrada por una acción dinámica (`0x1EC72E0 mov word [rdi+rbx+0x3A],0xFFFF`);
  - en una acción de red viene del paquete (`0x1EC9CEA mov [rdi+0x3A],ax`).
- **Cambio:** en cada capa sustituida el gancho escribe primero `+0x3A = 0xFFFF` y después el hash. Con eso la
  entrada es «espera recién construida» más los campos constantes de la restauración.
- **No se pudo probar sin el juego [E]:** que ningún consumidor más profundo (`0x232AC80` → `0x2329F00`, `.xtls`)
  falle con `0xFFFF` en una acción no dinámica. Pero es el valor por defecto de toda entrada nueva.

### 9.8 Contadores

- Las capas (`-> wait`, `-> 0`) solo se cuentan para `Substituted` (`sar::layer_counts`). Un `Skipped` cuenta solo
  como veredicto: ya no suma dos capas «a 0».
- Contadores nuevos: `dynamic graph changed before the write`, y los veredictos `LocalUnknown` y `BadFrame`.

### 9.9 Pruebas añadidas

En `optd_seq_restore`:
- marco inválido;
- gráfico dinámico que aparece antes de escribir;
- palabra de índice (solo en capas sustituidas, nunca en `probe`);
- capa sin gráfico con hash de escena (→ 0 / salto) y con el 0 del constructor;
- usuario local desconocido;
- recuento sin `Skipped`;
- `classify_shared`: original, gancho ajeno, `E9` dentro de la imagen, otro cambio, longitud distinta.
