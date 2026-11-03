# P8 sesión 15: el invitado se queda en el «túnel» tras su viaje rápido

Build 1.0.0.2976, base `0x140000000`. Investigación de solo lectura del 11-10: no se abrió el juego. El análisis
estático se hizo con capstone sobre `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe`.
Marcas: **[V]** verificado (log, captura o código); **[E]** estimación fuerte; **[H]** hipótesis.

Datos:
- Invitado (Shadow): `C:\Emulador CD\backups\p8-sesion15-shadow\cdmp_20261011-103833.log` y
  `cdmp_packets_20261011-103850.{bin,log}`.
- Anfitrión: `C:\Emulador CD\backups\p8-sesion15-host\cdmp_20261011-113231.log` y
  `cdmp_packets_20261011-113242.{bin,log}`.

**Relojes.**
- Entre las capturas: t del anfitrión = t del invitado + **382,709 s** [V]. Se comprueba con la 2437: en el invitado
  está a t=176,060; el `TravelConsume` del anfitrión la ve a t=558,7696.
- En los relojes de pared, el anfitrión va ≈ +1 h 00 min 00,45 s por delante (igual que en la S14).

Referencias:
- `p8-impl-viaje-rapido.md` §11;
- `p8-s14-teleport-cierre.md`;
- el informe externo `INFORME.txt` §10, que contiene la máquina `0x7797F0` y los manejadores
  `0xB88FD0`/`0xB87F60`;
- `p8-raiz-invitado.md` §4.1–4.2, que explica el estado 0x11 y el 2322.

## 0. Resumen

1. **La máquina de carga del invitado se para en el estado `[P+0x15C]=0x11` [V].** Llega ahí a las 10:42:18.492
   (hora del invitado) y no sale en **184,6 s**: el usuario sale del juego a las 10:45:24.
   - La recarga fue bien hasta ese punto. El invitado recibió 2643 y 3066. La fase D (0x0D) mandó
     **`PreLoadSequencerReq` 2873** (t=178,369), la misma rama `P+0x163≠0` que usa el viaje nativo del anfitrión.
     La fase F (0x0F) pasó al instante gracias al `P+0x290` del 3066.
   - El estado 0x11 espera `u8 [M+0x231] ≥ 2`. El gestor `M` sale de `0x9A5BE0(Q)`, que se busca a partir de
     `[Q+0x180]` (el dueño del campo **según el cliente**). Gracias al alias del 2174 y del 2643, ese dueño es el
     **usuario 2**. Por tanto `M` = `0x50E12305D00`, el `ClientSequencerStageManager` del propio Kliff del invitado
     (`0xA010046B`).
   - La recarga dejó `M+0x231` en **1**: lo escribe `0x77B0CE` en el estado 0x09, a las 10:42:15.302. Para subir a 2
     hace falta un **`PlayAllPreLoadSequencerAck` 2322 con la clave `0xA010046B`** (manejador `0xB87890`, escritura en
     `0xB87A67`). **Ese 2322 no llegó nunca.**
2. **Por qué no llegó [V].**
   - El manejador del 2873 en el servidor, `0x2A5F2B0`, es un `jmp` a `.xtls 0x104D7F20`. Pasa por el **mismo embudo
     de dueño único** que el 2283 de la S14: `[campo]->vf48` (`0x2B00CE0`), cuya dirección de retorno es
     `0x104D801E`, devuelve el gestor **del anfitrión**.
   - Con él llama a `0x2895EF0(gestor, &res, remitente)`, que a su vez llama a
     `0x287EF60(gestor, …, r8=[gestor+0x18], …)`. Esta prepara de nuevo **los secuenciadores del anfitrión** y emite el
     2322 *ToTarget* a `[gestor+0x18]`, que es **el Kliff del anfitrión** (`0x287F1D4 call 0x2999D80`).
   - Lo confirma la captura del anfitrión. Su cliente no mandó ningún 2873, pero recibe:
     - a +149 ms del 2873 del invitado, 21 `StageBranchStateAck` 2981 y 21 `PrepareSequencerAck` 2661 (t=561,227–561,507);
     - a +460 ms, el **2322 `01 00 10 A0`** (t=561,538).

     Es el mismo patrón que la recarga nativa del propio anfitrión a t=722 (2873 → +132 ms 2661 → +501 ms 2322).
3. **Ningún módulo del invitado vetó nada [V].** El problema es que el 2322 que el cliente espera, solo del dueño, se
   lo lleva el anfitrión. `observer_milestone` solo lo fabrica en la entrada (2174 → … → `0x2BC84E2`), y una recarga
   no tiene 2174. Ver §2.
4. **Siguiente bloqueo probable: el estado 0x12 [E fuerte].**
   - En la recarga, la lista de espera del contenedor **no se vacía**: tiene 217 claves, y 2 son NPC del anfitrión que
     llevan «NOT READY» desde la entrada (`0xB01000A3`, `0xB010012D`; `+5E 8`, `C2 0`, `CF 0`).
   - La réplica del mod dice: «state 0x12 waits for 0xB010012D (slot #173)».
   - En las dos entradas con 2174 la lista estaba vacía en 0x11/0x12.

   Ver §3. **Causa raíz y arreglo en el invitado (`guest_reload_wait_fix`): §8.**
5. **El cuerpo del invitado en la pantalla del anfitrión [V].**
   - Natural en una recarga dentro del mismo campo: el servidor **no** reanuncia al viajero a los demás (ni 2516 ni
     2005) y solo lo mueve con el siguiente **`MoveActorAck` 2697**, que sale de su `MoveActorReq` 2556.
   - El invitado, atascado en 0x11, no volvió a mandar ningún 2556 de su Kliff. El último fue a t=171,013, antes del
     viaje. Por eso el cliente del anfitrión lo deja donde estaba.
   - El overlay pinta la **posición del canal P2P del mod**, no la del juego. El invitado manda su posición local, que
     ya es el destino. Ver §4.
6. **Efecto secundario en el anfitrión [V].** Por el embudo, el 2873 del invitado **volvió a preparar los
   secuenciadores de contenido del anfitrión**:
   - 21 2661, 21 2981 y un 2322 a su cliente;
   - vf40 ×13 004 y vf48 ×143 bajo esa Req;
   - `ShopProbe`: `cd_seq_contents_wanted_her_0010` en el gestor `0x508E01E0700` del Kliff del anfitrión;
   - después, 15 `SequencerEvadeTriggerLogoutReq` 2547 de su cliente.

   No hubo ningún ERR ni 2679, y el anfitrión siguió jugando. Ver §5.
7. **Arreglos, en orden** (§6):
   - **R1:** 2322 para el usuario 2 al final del ExecuteTroc de su 2873, con el emisor ya verificado de
     `observer_milestone`.
   - **R2:** medir y, si hace falta, despejar el estado 0x12 en el invitado.
   - **R3:** nada para el cuerpo; se comprueba que el 2697 vuelve.
   - **R4** (más adelante): quitar el efecto sobre el anfitrión, redirigiendo vf48 al gestor del usuario 2 (variante
     nativa) o descartando el 2873.

## 1. Cronología (hora del invitado; anfitrión = +1 h 00 min 00,45 s)

| Hora inv. | t inv. | t anf. | Hecho | Marca |
|---|---|---|---|---|
| 10:42:10–13 | — | — | Menú principal y mapa (`ui_pause_block`: el mundo sigue) | V |
| 10:42:13.262 | 174,099 | 556,808 | `GuestFastTravel (probe)`: `0x655110` al punto 6. **C>S 2283** (53 B). `[P+0x15C]` 0x18 → 0x01 → 0x02 → **0x04** | V |
| — | — | 11:42:14.034–.146 | Anfitrión: `TravelBegin` → F3 (vf48 `0x2A5F7FC` y `0x2BC4C53` → gestor del usuario 2) → **`TravelPrepared … PREPARED: user 2's TS set, the host's untouched`** | V |
| 10:42:15.223 | 176,060 | 558,769 | 0x04 → 0x05 → **0x06: C>S 2437** | V |
| 10:42:15.3 | 176,121 | 558,770 | S>C 2208, **2643** (68 B: `04 6B 04 10 A0 01 00 00 00` + dueño **`6A 04 10 90`** = usuario 2 por el alias, 1 registro), **3066** `6B 04 10 A0 00 00`. Anfitrión: `TravelConsume … 0x2BCA3D0 (reload in the same field)` | V |
| 10:42:15.301–.302 | 176,137 | — | 0x06 → 0x07 → 0x08 → **0x09** (`M+0x231: 0 → 1` en `0x77B0CE`, gestor `0x50E12305D00` del Kliff `0xA010046B`) → 0x0A | V |
| 10:42:15.4–17.5 | — | — | 0x0A → 0x0B → 0x0C (1,9 s) → **0x0D** | V |
| 10:42:17.532 | **178,369** | 561,078 | Fase D: **C>S 2873 `PreLoadSequencerReq`** (0 B). No hay 2549 | V |
| 10:42:17.532 | — | — | 0x0D → 0x0E → **0x0F → 0x10** en 0 s (`P+0x290` ya puesto por el 3066) | V |
| — | — | 561,227–561,507 | Al **cliente del anfitrión**: 21 × 2981 + 21 × 2661 del Kliff `0xA0100001` (secuenciadores nuevos `0x1B4…0x311`). Su cliente no había mandado ningún 2873 (§5) | V |
| — | — | **561,538** | Al cliente del anfitrión: **2322 `01 00 10 A0`** | V |
| 10:42:18.492 | 179,328 | — | 0x10 → **0x11** (0,96 s en 0x10) | V |
| 10:42:18.5–19 | — | — | `observer_seq_copy` copia 41 2661 del libro del anfitrión al usuario 2: en el invitado van a **X** (`0x50E20CC3F00`, `+0x231 3`). Ninguno es del Kliff del invitado | V |
| 10:42:23 → 10:45:24 | — | — | Cada 5 s: `state 0x11 … NOT READY 2 … state 0x12 waits for 0xB010012D (slot #173)`; `GuestScene [G3 X] … 0xA010046B … X+0x231 1`; `GuestSeqPlay … own M+0x231 is not 3 yet`. **Ningún 2322** al invitado. **Ningún 2556** de su Kliff | V |
| — | — | 11:42:36.375 | `TravelAck trip 1 … reload in the same field (2643/3066) … 2174 0 2144 0 2762 0 2679 0 Nak 0`. Fase de movimiento: `MOVE_ASKED (… 2437 2 … 2445 2 …)` sin cambios | V |
| 10:45:23–26 | 363,9–367,1 | — | El usuario sale. 2001; 2679 **normal** (`07 C8 BB 0C A3` = `eErrNoParentsRequestToLogout`); 0x11 → 0x18; `Terminate` | V |

La recarga nativa del anfitrión, en la misma captura (t=717,8–726,6), sirve de referencia [V]:

```
717,820  C>S 2283
719,808  C>S 2437
719,818  S>C 2643 (dueño 0x90100000)
719,873  S>C 3066
722,062  C>S 2873
722,194  S>C 2981/2661 (×~20)
722,563  S>C 2322 01 00 10 A0
726,556  C>S 2445
726,572  S>C 2762
```

No hay 2549, 2174, 2144 ni 2714. Tampoco hay un 2005 del propio actor.

## 2. Pregunta 1: dónde se para y qué espera

### 2.1 El estado 0x11 [V, capstone]

```
0x779E39  mov rcx,[rdi+0xC0] ; mov rcx,[rcx]   ; Q
0x779E43  call 0x9A5BE0                         ; M = gestor del usuario [Q+0x180] (0x9A5BF4 mov r8d,[rcx+0x180])
0x779E48  cmp byte [rax+0x231],2 ; jb 0x77A1A8  ; <- aquí está parado: M+0x231 = 1
0x779E72  call 0xA97D60(M, P+0x150)             ; segundo predicado (no se llegó a evaluar con éxito)
0x779E7F  mov byte [rdi+0x15C],0x12
```

- Qué es `M`, según el log del invitado:
  - `GuestStage [M+0x231=1 0x77B0CE] manager 0x50E12305D00 (actor 0xA010046B) +0x231: 0 -> 1 | P … state 0x9`;
  - `GuestScene [G3 X] character 0xA010046B … X = its stage manager 0x50E12305D00 … X+0x231 1` (cada 5 s hasta el
    final);
  - el campo en la vista del cliente: «owner user in this client's view 0x9010046A».
- El gestor del anfitrión en el invitado (X, `0x50E20CC3F00`) tiene `+0x231 3`. Si `Q+0x180` fuera el anfitrión, 0x11
  pasaría. No se propone (§6, alternativas descartadas).
- Escritores de `+0x231`, ya conocidos (`p8-raiz-invitado.md` §4.1):
  - 0 en `0x77AA8B`;
  - 1 en `0x77B0CE`;
  - **2 solo en `0xB87A67`**, el manejador del 2322 `0xB87890`, que resuelve el actor del paquete y su gestor;
  - 3 en `0x77D061`, que es posterior a 0x11.

### 2.2 Lo que mandó el invitado tras el 2437 [V, captura del invitado]

| Opcode | ¿Lo mandó? | Comentario |
|---|---|---|
| 2873 `PreLoadSequencerReq` | **sí, 1** (t=178,369) | fase D con `P+0x163≠0`, como el nativo |
| 2549 | no | correcto en esta variante (informe §10.1) |
| 2714 | no | el nativo tampoco lo manda |
| 2445 | **no** | se manda en 0x17; está parado en 0x11 |
| 2556 de su Kliff | no tras t=171,013 | durante la carga el cliente no manda movimiento propio (§4) |

Siguió mandando lo de fondo: 3067 ×cientos, 2249, 2928, 2758/3089 de gimmicks, 2547 ×34 y 2223/2734 de su actor.

### 2.3 Por qué el 2322 fue al anfitrión: el 2873 en el servidor [V estático + captura]

`troc_messages_2976.csv`:
- `PreLoadSequencerReq,2873,…,handler 0x2a5f2b0`;
- `PlayAllPreLoadSequencerAck,2322,…,handler 0xb87890`.

```
0x2A5F2B0   E9 6B 8C A7 0D            jmp 0x104D7F20            ; cuerpo en .xtls
0x104D7FC6  mov rsi,[r8]                                       ; remitente (el Kliff del usuario 2)
0x104D7FD9  call 0x17FA6C0                                      ; su campo
0x104D8014  mov rcx,[rbp-0x40] ; mov rax,[rcx] ; call [rax+0x48] ; ServerField::vf48 0x2B00CE0 -> gestor del DUEÑO
0x104D801E  mov r8,rsi                                          ; <- dirección de retorno de esa vf48
0x104D8034  call 0x2895EF0(gestor, &res, remitente)
0x104D8044  mov eax,0x3F5 …                                     ; si res≠0: Nak 1013
```

```
0x2895EF0: 0x1F6C540(gestor,…) ; 0x206AE30([remitente+0xA0],…) ; 0x2BD1920([[remitente+0x68]+0x1A0], …, remitente, …, 1)
0x289603E  mov r8,[rsi+0x18]          ; el personaje DEL GESTOR, no el remitente
0x2896049  call 0x287EF60(gestor, ctx, r8, r9)
0x287EF60: [gestor+0x689]=1 … prepara secuenciadores/temporizadores …
0x287F1C1  call [r14+0xE8]            ; sesión de r8
0x287F1D4  call 0x2999D80             ; 2322 ToTarget a r8
```

Es el mismo defecto de clase que el 2283 de la S14: la Req del usuario 2 se resuelve con el gestor del dueño.

Pruebas dinámicas:
- Captura del anfitrión: entre t=559 y t=562 su cliente manda 2556, 3067, 2223… pero **ningún 2873**. Aun así recibe la
  ráfaga 2981/2661 (561,227) y el 2322 (561,538).
- Los desfases con el 2873 del invitado (561,078) son +149 ms y +460 ms. En la recarga nativa son +132 ms y +501 ms.
- `ShopProbe [stage] … 11:42:18.504 … 'cd_seq_contents_wanted_her_0010' … manager 0x508E01E0700 of character
  0xA0100001`.
- `User2Funnels` (resúmenes de 11:41:45 y 11:42:45): bajo Reqs del usuario 2 aparecen por primera vez «2873x143» en
  vf48 y «2873x13004» en vf40 (vf40 solo se redirige en sus llamadores filtrados).

### 2.4 Candidatos descartados (V/E/H)

| Candidato | Veredicto | Evidencia |
|---|---|---|
| `guest_x_veto` V1 (Loading, `0x5F2A3A`) | **No** [V] | `GuestXVeto summary … V1 Loading in \| calls past its gate 0, VETOED 0` (10:42:33, 10:43:33, 10:44:33). Solo `UIStageGuide` vetado en X (esperado) |
| `guest_seq_route` | **No** [V] | 2322: «M's own character: native x2» (las dos entradas) y «ROUTED to X x1». No hay ninguno después: no llegó ninguno |
| `guest_sched_*` / `guest_schedule_load` | **No** [V] | Solo retenciones y 6 TIMEOUT de recursos de horarios; nada en la máquina de carga |
| Reanuncio del propio actor (2005/2756) | **No hace falta** [V] | 0x11 no lo mira, y la recarga nativa del anfitrión no recibe ningún 2005 de sí mismo |
| Recursos, `A+C2`/`A+CF` | **No** (en este punto) [V] | La fase D ya pasó (mandó 2873). El recurso de 0x14 y el `A+C3` de 0x17 están después |
| Un 2174 esperado | **No** [V] | La variante 2643/3066 llegó y la fase F pasó; el cliente no espera un 2174 |
| Alias 2174/2643 («se cree dueño») | **Es la razón de que espere el 2322 de su propio gestor**, por diseño [V] | Igual que en la entrada, donde `observer_milestone` le da ese 2322. Sin el alias, 0x11 miraría X (`+0x231 3`) |
| **Embudo vf48 del servidor en el 2873 del usuario 2** | **Causa** [V] | §2.3 |
| `observer_milestone` | Hizo lo que debía [V] | Solo sigue viajes con 2174; atribuyó el 3066 suelto al viaje viejo (`ObserverMilestone [3066] … trip E1.T2`), solo diagnóstico. `decisions 1: emit 1` (la entrada) |

## 3. Siguiente bloqueo probable: el estado 0x12 y la lista de espera [E fuerte]

**Qué hace 0x12** (`0x779E8B..0x779F25`, capstone) [V]:
- toma el cerrojo `[[P+0xB8]+0x10]`;
- con `C = [[P+0xB8]+8]`, repite: clave `[C+0x78][0]`;
- `0x464290 Find`;
- si el actor existe y `vf1D8` (`0x4FE240`: `+0xC2 && +0xCF`) es falso, **se queda en 0x12**;
- si no existe o está listo, lo saca (el último pasa al hueco) y sigue;
- con la lista vacía, pasa a 0x13.

**Qué contenía la lista** [V]:
- **En las dos entradas con 2174** (10:39:38–10:40:18) estaba **vacía** en 0x09–0x15 («wait list total 0»). Se llena al
  llegar a 0x18 con los 2005 (201 claves).
- **En la recarga no se vacía:** 217 claves a las 10:42:23 (201 + 16 nuevas). Entre ellas hay 2 que llevan
  `+5E 0x0008 C2 0 CF 0 ctrl 0x90100000 NOT READY` **desde las 10:40:33**:
  - `#100 0xB01000A3`: escena `0x20000014`, etapa enlazada en X, activa; `+0x3D8 0`, `+0x3D9 0`;
  - `#173 0xB010012D`.
- La réplica del mod (`gad::scan_wait_list`) da: «**state 0x12 waits for 0xB010012D (slot #173)**».
- Que el vaciado dependa del camino (el 2174 / `P+0x162` en la fase 7, `0x77AA40`) es [E].

**Consecuencia [E fuerte]:** aunque llegue el 2322, el invitado pasará a 0x12 y se quedará esperando a esos NPC.
- El anfitrión nativo no tiene ese problema: sus NPC están listos.
- La causa de fondo de esos dos NPC «NOT READY» no está resuelta. Es la familia de `p8-raiz-invitado.md` §4.3 y
  `p8-sesion6.md`.

## 4. Pregunta 2: el cuerpo del invitado en la pantalla del anfitrión

**Qué recibió el cliente del anfitrión sobre `0xA010046B` tras el 2437** (captura del anfitrión, t=556,5–600) [V]:
- 494 × 2145, 2 × 2404, 1 × 2991 y 1 × 2586;
- **ningún 2697, 2516 ni 2005**.

Antes del viaje (t=500–556) recibía **375 × 2697 `MoveActorAck`** de él, más 2642, 2991, etc.

**Qué haría lo nativo:** se ve en la recarga nativa del anfitrión, observada por el invitado (captura del invitado,
t=332–348) [V]:
- los 2697 del anfitrión (`01 00 10 A0`) se cortan a t=332,337, en la posición vieja `(−10709,…)`;
- vuelven a t=**343,163**, ya en el destino (`BD 09 26 C6` = −10626,43);
- eso ocurre **antes** de su 2445 (≈343,85);
- no hay ningún 2516 ni 2005 del viajero, ni otro aviso de teletransporte. Tras su propia recarga, el anfitrión
  tampoco recibe ningún 2005 del invitado.

Conclusión:
- en una recarga dentro del mismo campo, los demás ven al viajero solo cuando su cliente **vuelve a mandar
  `MoveActorReq` 2556** y el servidor lo reenvía como 2697;
- el invitado no lo hace porque no sale de 0x11: el último 2556 de `0xA010046B` es de t=171,013;
- el arreglo del §6 R1/R2 debería bastar [E];
- que el puppet del anfitrión acepte un salto de 136 m en un 2697 (`remote_motion`/`remote_interp`) es [H]: hay que
  comprobarlo.

**Por qué el overlay muestra el destino [V, código]:**
- `render/hud.cpp:181-186` pinta el marcador con `d.remote->pos`. Es la posición que manda el **mod** del invitado
  por el canal P2P (su `Local: pos (-10626.43, 621.26, -3813.26)`, leída de la memoria de su propio Kliff, que el 2643
  ya colocó en el destino).
- En el anfitrión:
  - `Net: … last remote (-10626.43, 621.26, -3813.26)`;
  - `RemoteMotion divergence #1 … key 0xA010046B | memory (-10713.04, 616.44, -3918.10) vs channel (-10626.43, …)
    136.07 m` durante 190 s, hasta el 2516 de la salida.

## 5. Efecto secundario en el anfitrión [V]

- Por el embudo, el 2873 del usuario 2 hizo en el servidor lo que haría una recarga **del anfitrión**:
  - `0x287EF60` sobre su gestor `0x508E01E0700`;
  - 21 2981 y 21 2661 a su cliente, con secuenciadores nuevos;
  - un 2322 a su cliente, en 0x18;
  - vf40 ×13 004 y vf48 ×143 en el servidor.
- Después, el cliente del anfitrión mandó 15 + 4 `SequencerEvadeTriggerLogoutReq` 2547 (t=561–562). En toda la sesión
  hay 2 en t=146, 1 en t=450 y 1 en su recarga nativa (t=723).
- `observer_seq_copy` copió esos 41 2661 a X en el invitado. Las tareas de carga de X subieron y volvieron a 0.
- No hubo ERR, 2679 ni `HOST TOUCHED` (el TS del anfitrión no se tocó), y el anfitrión siguió jugando y viajó después.
- Si el usuario vio algo hacia las 11:42:18 (escenas de «se busca», tiempo o NPC que se reinician), viene de aquí
  (§7).

## 6. Pregunta 3: arreglos propuestos, por prioridad

**R1. 2322 para el usuario 2 al terminar su 2873** (anfitrión; recomendado para la S16; mismo contrato que la entrada).
- Clave nueva, p. ej. `[session2] observer_milestone_reload = 0 | probe | 1`. Necesita `observer_milestone_send=1` y el
  sujeto de ExecuteTroc.
- **Dónde:** en `troc_detour` de `user2_funnels.cpp` (gancho de entrada ya existente en ExecuteTroc `0x2774B20`), **después**
  de `g_troc.unsafe_call` para `now.opcode == 2873` y `now.actor` = el Kliff del usuario 2. Es el hilo lógico del
  servidor, dentro de la Req: el mismo contexto en el que `0x2895EF0` emite el 2322 nativo.
  - Alternativa equivalente: en el desvío de vf48, cuando la dirección de retorno sea **`exe+0x104D801E`** (solo una
    comparación; no se engancha nada en `.xtls`).
- **Condiciones:**
  - campo compartido (`[campo+0x180]` = usuario del anfitrión);
  - el viaje del usuario 2 es de clase «recarga»: hay `TravelConsume` con la rama `+0x569`/`0x2BCA3D0`, se vieron
    2643 y 3066 hacia el usuario 2 y ningún 2174 tras su 2437;
  - todavía ningún 2322 con `0xA010046B` en la cola del usuario 2 desde ese 2437 (seguimiento de `take_frame`, igual
    que `observer_milestone_on_out`);
  - una vez por viaje; sin reintentos; se cancela con cambio de época o con logout.
- **Qué llama:** el emisor que `observer_milestone` ya verifica: `0x2A757A0(finder, Kliff del usuario 2)`, con la
  sesión = `vfE8()` y que tiene que ser la sesión 2 viva, y después `0x2999D80(Kliff del usuario 2, finder)`.
- **Línea:** `ObserverMilestone [reload] trip N: 2322 EMITTED to user 2 after its PreLoadSequencerReq 2873 (+ms)`,
  con la confirmación de `take_frame`.
- **probe:** solo decide y registra («WOULD EMIT»).
- No cambia nada más. El 2873 sigue llegando al servidor, así que el efecto del §5 sigue igual que en la S15
  (comprobado que no rompe nada visible). R4 lo quita.
- **Esperado:** en el invitado, `M+0x231` 1 → 2 (`GuestSeqRoute … 2322 … M's own character: native x3`), y
  `0xA97D60(M)` debería dar verdadero, igual que en las entradas (M sin etapas, `+0x174 0`). Después, 0x11 → 0x12.

**R2. El estado 0x12 en una recarga** (invitado; hace falta justo después de R1 [E fuerte]).
1. **Medir** (sin escribir): en `guest_actor_diag`, al entrar en 0x12, una línea con:
   - la clave que bloquea, su `+5E/C2/CF`, su controlador y desde cuándo está «NOT READY»;
   - si el viaje es de recarga.

   Ya existe `| in 0x12 for N s`; falta el desglose por viaje.
2. **Mitigación** (clave nueva, `guest_reload_wait_skip = 0 | probe | 1`):
   - **Dónde:** el gancho medio que ya existe en `0x7798D0` (lectura de `[P+0x15C]`, `RDI = P`, en el hilo principal
     del juego). Es el mismo hilo que procesa los 2005 que llenan la lista (T15084 en el log).
   - **Cuándo:** si `[P+0x15C] == 0x12`, el viaje es de recarga (2643 visto, sin 2174 desde el último 0x18) y la
     clave del frente es un actor **existente, no listo, controlado por otro usuario** (`+0x90 ≠` la clave local) que ya
     estaba «NOT READY» antes de empezar el viaje (> 30 s).
   - **Qué hace:** sacarlo de la lista **exactamente como 0x12 saca los listos** (`[C+0x78][0] = [C+0x78][n-1]`;
     `[C+0x80]-=1`, como en `0x779EF7..0x779F0B`).
   - **Límites:** como mucho N claves por viaje; cada una, con su línea; con lecturas y escrituras protegidas (SEH).
   - **Riesgo [H]:** el NPC sigue sin estar listo, que ya es su estado en 0x18 desde hace minutos.
3. **Fondo** (otra tarea): por qué esos NPC del anfitrión nunca llegan a `C2/CF ≠ 0` en el invitado. **Resuelto en §8.1**;
   la clave final se llama `guest_reload_wait_fix` (§8.2), no `guest_reload_wait_skip`.

**R3. El cuerpo en el anfitrión.** Por ahora no hace falta código.
- Al pasar el invitado de 0x15/0x17, su 2556 debería volver y el servidor reenviaría el 2697 en el destino, como en la
  recarga nativa (§4).
- Comprobar en la prueba:
  - 2697 de `0xA010046B` en la captura del anfitrión;
  - `RemoteMotion divergence … END`.
- Solo si el puppet no salta: estudiar un reanuncio nativo al anfitrión (2516 + `NotifyAppear 0x2B000C0`, como
  `child_reannounce`) [H]. **No** fabricar 2697.

**R4. Quitar el efecto del §5** (más adelante, con una sonda primero). Dos variantes:
- **R4a, la nativa:** F3 para el 2873. En el desvío de vf48, con retorno `exe+0x104D801E` y el sujeto = el usuario 2
  en el campo compartido, devolver `[[[sujeto+0x68]+0xA8]+0x18]` (el `ServerSequencerStageManager` del usuario 2,
  la misma cadena que F3).
  - Así `0x2895EF0 → 0x287EF60` preparan **su** gestor y emiten el 2322 nativo a él (R1 sobraría).
  - **Riesgo [H]:** sería la primera vez que `0x287EF60` corre sobre el gestor de un no-dueño (en la entrada, el
    servidor se lo salta en `0x2BC8473`). Además, las ~142 vf48 internas y las ~13 000 vf40 seguirían dando los
    gestores del dueño: habría mezcla.
  - Hace falta antes una sonda que registre las direcciones de retorno de vf48/vf40 bajo el 2873 del usuario 2.
- **R4b:** descartar el 2873 del usuario 2 en `inject` (como F2 con el 2283) y emitir el 2322 de R1 en el siguiente
  ExecuteTroc del usuario 2 (manda 3067 cientos de veces durante la carga).
  - Se salta también `0x2BD1920([[rem+0x68]+0x1A0], …)`, que depende del remitente y cuyo efecto es [H].

**Descartados:**
- escribir `M+0x231=2` en el invitado;
- quitar el alias del 2643 para que 0x11 mire X (cambiaría la vista de dueño del cliente a mitad de sesión y
  rompería las suposiciones de los módulos del invitado);
- fabricar 2174/2549;
- subir los tiempos de espera.

### Prefijos que habría que comprobar (build 2976)

- `0x2A5F2B0 E9 6B 8C A7 0D` (stub → `.xtls 0x104D7F20`).
- La ventana `.xtls 0x104D8014..0x104D8039`, **solo como comprobación de bytes** antes de aceptar la dirección de
  retorno:
  ```
  48 8B 4D C0 48 8B 01 FF 50 48 49 89 F0 B9 BB 49 33 9D 2B 0D AC A0 81 F9 48 8D 54 0D 00 48 89 C1 E8 B7 DE 3B F2
  ```
- `0x2895EF0 48 89 5C 24 08 48 89 74 24 10`.
- `0x287EF60 48 89 5C 24 10 4C 89 4C 24 20`.
- El estado 0x11/0x12 de `0x779E39..0x779F25`; `0x7798D0` ya lo comprueba `guest_actor_diag`.

## 7. Prueba siguiente y preguntas

1. **Una PC** (anfitrión + `cdmp_peer`): el guion de `p8-impl-viaje-rapido.md` §11.4, añadiendo `send 2873 0 -` tras
   el 3066.
   - **Éxito:** la línea `ObserverMilestone [reload] … EMITTED` y un 2322 `6B…`→ usuario 2 en el `.bin`.
   - Con R1 seguirá la ráfaga 2661/2322 hacia el anfitrión (§5).
2. **Shadow:** el mismo viaje a Hernand.
   - **Éxito por etapas:**
     - `M+0x231 1 -> 2`, `0x11 -> 0x12`;
     - después, `0x12` sin bloqueo o, con R2, `GuestReloadWait [0x12 DROPPED] …` (§8.3);
     - `0x13…0x17`, C>S 2445 y S>C 2762;
     - en el anfitrión, la fase de movimiento en `STABLE (… 2445 3, 2762 3)` y 2697 de `0xA010046B` en el destino.
   - **Abortar** si `0x12` dura más de 60 s sin R2, si llega un 2679 no pedido o si aparece un ERR del anfitrión.
3. **Para el usuario:**
   - ¿Notó algo en el anfitrión hacia las 11:42:18? Escenas que se reinician, NPC que «huyen» (2547) o el tablón de
     «se busca».
   - ¿El túnel del invitado mostraba ya el terreno del destino o solo el vacío?

## 8. El estado 0x12 en una recarga: causa raíz y `guest_reload_wait_fix` (11-10, sin abrir el juego)

### 8.1 Por qué `0xB01000A3` y `0xB010012D` no están nunca listos en el invitado

**Resumen [V captura + V código documentado; E en el detalle del bucle]:** son **jefes de grupo cuyo hijo llegó al
cliente del invitado en un 2005 anterior al suyo**. El cliente destruye a ese hijo huérfano y el jefe ya no puede
formar su grupo de login: se queda en `+5E 8` (esperando login), `C2 0`, `CF 0` para siempre.

- **El final de cada registro de actor del 2005** es `u32 padre (actor+0xAC) | u16 n | n × u32 hijos (actor+0xB0)`
  (`copias-grupo-invisibles.md` §1). En la captura del invitado:

  | Jefe (tipo) | Hijos | Llegada del hijo | Llegada del jefe | En el invitado |
  |---|---|---|---|---|
  | `B01000A3` (3) | `B01000A4` (7, pieza sin CC) | t=52,742 | t=63,635 (**+10,9 s**) | NOT READY desde 10:40:33 |
  | `B010012D` (3) | `B010012E` (3, NPC) | t=63,586 | t=63,735 (+0,149 s, otro 2005) | NOT READY desde 10:40:33 |
  | `B010012F` (3) | `B0100130`, `B0100131` | `131`: t=63,586, un 2005 antes | t=63,586 | NOT READY (y su miembro `130`) |
  | `B010141A` (3) | `B010141B` (7) | t=339,846 | t=340,613 (+0,77 s) | NOT READY a las 10:45:23 |
  | `B01014E6` (3) | `B01014E7`, `B01014E8` (7) | `14E8`: t=339,894 | t=340,630 (+0,74 s) | NOT READY (y su miembro `14E7`) |

  Todos los NOT READY de larga duración del log del invitado son un jefe con un hijo huérfano o un miembro de ese grupo.
- **Lo nativo no lo hace nunca [V]:** `analyze_capture.py <bin> orphans` (nuevo) da **0** registros huérfanos en la
  captura del cliente del anfitrión (la entrega nativa del servidor) y **75** en la del invitado de la S15 (106 y 60 en
  las dos de la S14). El orden lo rompe el mod en el anfitrión.
- **Quién lo rompe [V log del anfitrión]:**
  - `observer_load_defer` retuvo 330 NPC del anfitrión mientras el usuario 2 cargaba y los soltó tras su 2762
    (media 10 761 ms; el 2762 del invitado es t=63,389, la ráfaga de liberaciones t=63,40–63,75). **No retiene sus
    piezas** (`B01000A4` y otras 18 piezas de tipo 7 salieron a t=52,74–52,86) y suelta a padres e hijos **por clave**,
    no por familia (44 huérfanos en la ráfaga).
  - `observer_hold` (S3, hasta la primera acción) hace lo mismo en juego normal: las piezas `141B`/`14E8` salen
    0,7 s antes que sus NPC.
  - `observer_hold_pieces` estaba en probe y **no detectó ninguno** (`pieces without CC naming a parent seen 205
    (probe: WOULD be withheld 0)`): su condición («el padre está retenido ahí» cuando pasa la pieza) no cubre estos
    casos.
- **Qué hace el cliente con un huérfano [V código, `copias-grupo-invisibles.md` §2.2; E en el momento exacto]:**
  - el bucle de logins pendientes `0x8B7B50` mira `actor+0xAC`; si el padre no existe, **destruye al hijo**
    (`0x8B7D78`, `eErrNoParentsNotExist`). Reparte el trabajo por módulo de la clave entre fotogramas, así que un hijo
    que llega pocos ms antes puede sobrevivir (por eso no todos los 75 dejan un jefe atascado);
  - el jefe llega después y monta su grupo de login con sus hijos de `+0xB0` (`0x4FD530`); falta uno →
    `eErrNoInvalidActorKey`, y lo reintenta cada vuelta **para siempre**, sin error en el log. Nunca pasa de `+5E 8`
    ni pone `+0xCF` (solo lo pone `0x8B816B` al terminar el login);
  - los miembros del grupo (`+0xAC` = el jefe, que existe) se saltan en `0x8B7D78`: tampoco hacen login.
- **Cómo sale nativamente un actor no listo de la lista de 0x12:** cuando termina su login (`+0xC2` y `+0xCF`) o cuando
  se destruye (`Find` da nulo); `0x779EF7..0x779F0B` lo quita entonces. Estos jefes no hacen ninguna de las dos cosas
  mientras sigan en rango: en la S15 solo desaparecieron con el `RemoveActorAck` 2516 de t=337 (el anfitrión viajó).
- **¿Existen en el anfitrión? [V]** Sí: login normal a las 11:35:22 (`P8Probe [Restart]`, `SceneDiag [H1 flag] NEW KEY`),
  con sus `StartActionAck` 2991 reenviados al invitado cada ~3–7 s hasta t=337.
- **Es general [V/E]:** cualquier jefe con un hijo huérfano queda en `+5E 8` para siempre. En una entrada (con 2174) la
  lista de espera llega vacía a 0x11/0x12 y no molesta; en **cualquier recarga en el mismo campo** (2643/3066, la lista
  no se vacía) un jefe así que siga en la lista y en rango para la carga en 0x12. Quién añade claves a la lista sigue
  sin localizar (`.xtls` o un ayudante genérico, [E]); se llena con las creaciones de los 2005.

**Arreglo de fondo (ANFITRIÓN; implementado después como `observer_hold_family`, `p8-s14-viajeros.md` §7, rama `p8-family-order`):** que ningún registro con `+0xAC = P` llegue a una
sesión antes que el 2005 de `P`:
- retener las piezas **y los hijos con CC** mientras su padre esté retenido por `observer_hold` **o por
  `observer_load_defer`**, o mientras el padre todavía no se haya anunciado a esa sesión;
- soltarlos en el mismo `NotifyAppear` que el padre, detrás de él (como la entrega nativa).

`observer_hold_pieces=1` es el sitio natural, pero su condición actual no ve ninguno de los casos de la S15.

**Alternativa en el invitado (no implementada):** en `guest_deliver`, sacar de cada 2005 el registro de un hijo cuyo
padre (`0xB...`) no se ha entregado todavía en esta conexión, y entregarlo como un 2005 propio justo después del 2005 del
padre (con tope de espera; sin tocar los registros de tipo 0). Solo bytes de la red, sin llamadas al juego, pero cambia
el flujo de 2005 de todo el campo: mejor arreglarlo donde se rompe.

### 8.2 `[session2] guest_reload_wait_fix = 0 | probe | 1` (invitado; necesita `guest_actor_diag=1`)

Archivos: `src/mod/session2/guest_reload_wait{.h,.cpp,_logic.h}`, pruebas en `tests/optd_reload_wait_tests.cpp`.

- **Dónde corre:** dentro del gancho medio que ya existía en `0x7798D0` (`guest_actor_diag`, `movzx r14d,[rdi+0x15C]`,
  `RDI = P`), en **cada pasada** del hilo de la máquina de carga, **antes** del manejador del estado. No hay ningún gancho
  nuevo.
- **Bytes comprobados** (dentro de `.rsrc`, build 2976) antes de quitar nada, desde el hilo de diagnóstico:
  - el principio de 0x12, `0x779E8B` (39 B, con el cerrojo `[M+0x10]` vf1);
  - el bucle, `0x779ED0` (39 B);
  - el bloque de borrado y el vf2, `0x779EF7` (38 B);
  - `vf1D8 0x4FE240` (20 B);
  - `Find 0x464290` (26 B; solo se replica, no se llama).

  Línea: `GuestReloadWait: state 0x12 ... match build 2976 inside .rsrc: removals allowed`.
- **probe y 1:**
  - **Traza** (`GuestReloadWait [trace]`): cada cambio de `[P+0x15C]` dentro de un viaje, más una línea `[still]` cada
    5 s que dure un estado. Lleva:
    - `P+0x160..0x163`, `P+0x290`, `P+0x291`;
    - el número de claves de la lista;
    - `+C2/+CF/+C3` del personaje propio y `+0x231` de su propio gestor de etapas (`[[[A+0x68]+0xA8]+0x18]`);
    - la clase de viaje (`entry (2174)` / `RELOAD (2643, no 2174)`) y si llegó el 3066.

    Como mucho 800 líneas.
  - **Al empezar el viaje** (lo primero de dos: el C>S 2283 del cliente, visto en `guest_send`, o la salida de
    `[P+0x15C]` de 0x18): foto de la lista de espera (clave, actor, `+5E`, listo o no; hasta 1024 claves),
    `GuestReloadWait [trip N begins at the client's 2283 | (state left 0x18)] ... NOT READY: 0x... (not ready for >= N
    s)`. El 2283 sirve si se perdió la salida de 0x18 (gancho puesto tarde) o si llega una vuelta antes; un 2283 cuya
    carga no arranca en 10 s se descarta y la salida real de 0x18 abre otro viaje.
  - **En 0x12**, la clave del frente (a la que espera 0x12) si existe y no está lista (`[0x12 BLOCKER]` o `[0x12 WOULD
    DROP ...]`). Se escribe en cuanto cambia y a +2, +10, +30 y +60 s, y después cada 60 s. Lleva:
    - `+5E`, `C2`, `CF`, controlador, si su `vf1D8` es `0x4FE240`;
    - su edad como NOT READY (del recorrido de cada 5 s);
    - su padre `+0xAC` y sus hijos `+0xB0`, cada uno buscado (`MISSING HERE -> a child is missing: its login group can
      never form`);
    - cómo estaba en la foto del principio y el veredicto de `grw::judge`.
- **1:** si `grw::judge` da `Drop`, quita **esa** clave del frente **igual que 0x12 quita una clave lista**
  (`[keys][0] = [keys][n-1]; [C+0x80] -= 1`):
  - con el cerrojo que toma 0x12 (`[M+0x10]`: vf1 y vf2 de su tabla, como `0x779EA0`/`0x779F1A`; ambos dentro del
    código del juego, y la misma tabla toda la sesión);
  - comprobando otra vez, ya con el cerrojo, que el frente sigue siendo la misma clave, el mismo actor y no está
    listo.

  Línea: `GuestReloadWait [0x12 DROPPED]`. Después 0x12 sigue de forma nativa en la misma pasada.
- **`Drop` exige todo esto** (cada fallo tiene su texto en la línea):
  1. el viaje es una recarga en el mismo campo: un 2643 desde que empezó el viaje y ningún 2174;
  2. una clave `0xB...` (nunca `0xA...`: ni el Kliff propio ni el del anfitrión);
  3. controlada por **otro usuario** (`+0x90` = `0x9...`, distinto del usuario local, que tiene que ser conocido);
  4. `+5E` «esperando login» (8, sin `0x10`), no lista, y su `vf1D8` es `0x4FE240`;
  5. **en la foto del principio del viaje, NOT READY y con el mismo puntero de actor**: nunca una que pasó a no estar
     lista durante el viaje ni otra instancia con la misma clave;
  6. al menos **2 s** en 0x12: lo nativo va primero;
  7. como mucho **128 por viaje** (11-10, revisión: el tope era 16 y la S15 tuvo 75 huérfanos en una sesión; con más de
     16 jefes atascados en la lista, 0x12 habría seguido parado). Pasar de 16 en un viaje da un aviso
     (`WRN GuestReloadWait: trip N has dropped 17 ... (more than 16; cap 128)`), una vez por viaje, y no detiene nada.
- **Fallos:** si el cerrojo no es código del juego, falla una escritura o salta una excepción, se desactiva
  (`DROP REFUSED` / `DROP FAILED` y `DISABLED` en la línea de estado). Si el frente cambió bajo el cerrojo, solo se
  vuelve a mirar en la pasada siguiente.
- **Resumen** en la línea de estado del invitado: `GuestReloadWait [1] code ok | passes … | trips … | 0x12 blocker
  lines … [veredictos] | would drop … | DROPPED … (failed …; trips with > 16 drops …) | trips begun at the client's
  2283 …`.
- **Inis del invitado:** `cdmp.p8-s7-shadow.ini`, `cdmp.p8-s7-shadow-fase2.ini` y `cdmp.p8-s12-shadow-seguro.ini` llevan
  `guest_reload_wait_fix=1`. Con probe, la recarga se volvería a quedar en 0x12 y la prueba del arreglo R1 del anfitrión
  no llegaría a 0x13–0x17.

### 8.3 Qué mirar en la próxima prueba (con R1 del anfitrión puesto)

1. Al arrancar: `GuestReloadWait ON (1)` y `... match build 2976 inside .rsrc: removals allowed`.
2. Al viajar:
   - `GuestReloadWait [trip N begins]` con los NOT READY de antes (en la S15 serían `B01000A3` y `B010012D`);
   - las líneas `[trace]` con `RELOAD (2643, no 2174)`.
3. Tras el 2322:
   - `[trace] 0x11 -> 0x12` con `own M+0x231 2`;
   - `[0x12 DROPPED]` por cada jefe atascado, con su hijo marcado `MISSING HERE`;
   - `0x12 -> 0x13 … 0x17`, C>S 2445 y S>C 2762 (`[trace] … -> 0x18`).
4. **Éxito:** el invitado sale del túnel en el destino. En el anfitrión llegan los 2697 de `0xA010046B` (§4).
5. Los NPC quitados siguen como estaban (sin login, quizá invisibles o quietos), como antes del viaje: no es una
   regresión.

### 8.4 Abortar (volver a `guest_reload_wait_fix=probe`)

- `DROP FAILED`, `DROP REFUSED` o `DISABLED` en el resumen;
- `CapReached` en los veredictos (más de 128 en un viaje: algo distinto de los huérfanos);
- un `DROPPED` de una clave que no esté en la foto `[trip N begins]` como NOT READY (no debería ser posible);
- un crash o un cierre durante una carga, o un ERR del anfitrión justo después de un `DROPPED`.

Más de 16 `DROPPED` en un viaje (el `WRN ... more than 16`) **no** es motivo para abortar: solo dice que el anfitrión
sigue mandando hijos antes que sus padres (comprobar el arreglo de familias del anfitrión, `observer_hold_family`).

Si 0x12 sigue parado con `[0x12 BLOCKER]`, el veredicto dice por qué no se quitó (`kept: …`); por ejemplo,
`LoggedIn` es un actor con `+5E 0x10` y `CF 0` que espera su escena, y eso es otro problema.

### 8.5 Riesgos

- **[H]** Que algún sistema dependa de que 0x12 espere a esos actores. Nativamente nunca hay uno atascado así (cero
  huérfanos), y en las entradas el invitado ya pasaba de 0x12 a 0x17 con la lista vacía, sin esperarlos.
- **[E]** Llamar a vf1/vf2 del cerrojo desde el gancho: es el mismo hilo, la misma función y el mismo objeto que usa
  0x12 unas instrucciones más abajo. En `0x7798D0` este hilo no tiene el cerrojo (0x12 lo suelta antes de volver a
  `0x7798B6`).
- **[E]** La lista también la escribe otro código, que no está localizado. Con el cerrojo y la segunda comprobación,
  lo peor es saltarse una pasada.
- El arreglo no cura a esos NPC: siguen sin login en el invitado. La cura es el orden de familias en el anfitrión (§8.1).
