# U2: tenderos para el invitado (diseño y paso 1, build 1.0.0.2976)

Diseño de solo lectura (2026-10-13, desde `b8a3bb5`); el paso 1 (caso A) ya está implementado: §11. Sin juego.
Ghidra (`D:\RE\CrimsonDesert_2976`, servidor de consultas) y capstone (`C:\Emulador CD\build\research\cdpe.py`,
`xref_index.pkl`) sobre el PE; `.xtls` solo leído. Todas las direcciones son RVA (base `0x140000000`).

Marcas: **[V]** comprobado (bytes, desensamblado, log o captura); **[E]** leído en el código o inferencia fuerte;
**[H]** hipótesis sin medir.

Abreviaturas: `pj2` = personaje del usuario 2; `mgr2` = su `ServerSequencerStageManager`
(`[[[pj2+0x68]+0xA8]+0x18]`, vtable `0x5B17A58`, `[mgr+0x18] == pj2`); `mgrH` = el del anfitrión; M/X = gestores
del cliente del invitado (propio / copia del anfitrión).

Parte de: `pendientes/p8-s12-tenderos.md`, `p8-s12-tenderos-servidor.md` (§5.2 y §7, la gemela S2),
`p8-impl-tenderos-c1.md`, `p8-impl-tenderos-s2.md`, `s25/mundo-escenarios-propios.md` §5.9 y
`s25/u0-resultados-s28.md` §5 y §8.

---

## 0. Resumen

1. **Lejos del anfitrión (caso A), U-1 armado: sí, `mgr2` prepararía las tiendas él solo** si U-f dejara pasar los
   elementos de secuenciador `cd_seq_funcnpc_*` [E fuerte].
   - En `0x288A970` el dueño del `playData` es `[[stage+0x48]+0x60]`, el personaje de la stage. Para una stage de
     `mgr2` ese dueño es `pj2`, el mismo que llevan sus 2335.
   - `0x287C200` no compara con el dueño del campo. Su única validación previa (`.xtls 0x102BB110`) solo actúa con el
     tipo 3, y las stages son tipo 1 [V decompilado].
   - El 2661 sale ToTarget a `pj2` → M del invitado de forma nativa.
   - El tendero se enlaza **por el mapa del campo** (`0x20D5620`, `ServerField+0xB8`), no por el gestor. Si ya existe
     porque lo generó la stage del anfitrión, se enlaza ese mismo NPC.
2. **Lo que falta en el caso A es U2, la ruta por id de secuenciador.** Todas las Req de escena del invitado piden el
   gestor con `ServerField::vf48` (`0x2B00CE0`), que devuelve `mgrH` [V, tabla §2]. Hacen falta dos piezas pequeñas
   sobre ganchos que **ya existen**:
   - **U2-a** en el desvío de vf48 de `user2_funnels`: en los puntos de llamada de esos manejadores, si el id de la Req
     está en `mgr2` (y no en `mgrH`), devolver `mgr2`;
   - **U2-b** en el `create_mid` de ShopProbe en `0x2126A1F`, en el hilo del servidor: si el id de etapa de la entrada
     (`[r13+0x20]`) está en `mgr2`, `RAX := mgr2`. Es la comprobación del 2671 en el servidor.
   - **Corregido al implementar (§11.1):** el `SetInteraction` del servidor **no** depende de los 2591 del cliente.
     En las capturas nativas, el cliente no manda ningún 2591 de la propia escena de tienda (S12 carnicería `0x197`,
     S13 herrería `0x19D`) y, aun así, el servidor tenía su entrada (S14, `[server check] … FOUND`) [V paquetes, E
     causa]. U2-a sigue haciendo falta para las ramas (2436) y los eventos (2173/2767) de la escena del invitado: sin
     ellos, nadie abre la tienda.
3. **Cerca del anfitrión (caso B):** la escena de tienda es de `mgrH`.
   - C1 (redirigir el aviso a X) **no sirve como solución**. La captura de la S14 muestra por qué el invitado se quedó
     atascado: tras el 2916 su cliente entra en el estado de interacción (`ChangeAIStateReq`) pero no manda **ningún**
     2436/2591. La escena que tenía que reaccionar estaba en X, y X no conduce escenas en el invitado [V captura].
   - Recomiendo la **gemela S2 reescrita sobre U2**:
     - crearla **diferida** a la salida del `ExecuteTroc` más externo (no dentro de `0x288AAEF`);
     - borrarla en espejo, también diferido y con espera mientras el invitado esté en la tienda;
     - enrutar con U2-a/U2-b;
     - C1 obligatoriamente en `0`/`probe`.
   - Alternativa: un «modo cercano» de U-1 que solo deje pasar `funcnpc`. Plan provisional sin riesgo: tiendas solo
     lejos del anfitrión.
4. **Vaciado de U-1 con la tienda abierta:**
   - `0x287D4C0(mgr2)` borra también sus escenas (`0x2879440` en `0x287D8B5` [V]) → 2810 al invitado en mitad de la
     tienda. Hace falta una **espera de vaciado** acotada mientras el invitado esté «en tienda».
   - Una gemela viva bloquea además el armado de U-1: `arm_verdict` exige `[mgr+0x30] == 0` (`ListsNotEmpty`).
5. **Un PC (`cdmp_peer`)** basta para todo el lado del servidor: preparación en `mgr2`, el 2661 propio con el NPC
   enlazado, la ruta, 2663/2916 y la rama 2981/2524. Para que el peer interactúe hacen falta un guion nuevo y una
   directiva de aprendizaje filtrada (§6).
   **Shadow** hace falta para lo que solo ve el cliente del invitado: el aviso, la interfaz de la tienda, comprar y
   vender, el NPC, el vaciado con la tienda abierta y dos jugadores a la vez.

---

## 1. Caso A: ¿prepararía `mgr2` las escenas de tienda?

### 1.1 Camino [V/E]

```
2043 del usuario 2 (U-b: su gestor) → 0x287DBB0 → 0x288B7A0 (U-a 0x288B87A) → 0x28818F0 (U-a2 0x2911435)
  → 0x2882C70 → 0x2885BD0 → 0x288760C call 0x288A970(mgr2, &res, stage, elem, idx)   ← U-f (inline en la entrada)
      [elem+8] != 0 (secuenciador): playData{dueño=[[stage+0x48]+0x60], tipo 1, [elem+0x1F0], [elem+0x1F8],
                                   hash del nombre de fichero, idx, posición [elem+0x20C..0x218]}
      → 0x287C200(mgr2, &res, &playData, [elem+0x200], &inst, &o2)
          .xtls 0x102BB110: solo tipo 3 → con tipo 1 devuelve 0 [V]
          0x2A82150(dueño) → actor; SetFieldTime_PreCheck; 0x17FA6C0 → el ServerField compartido
          GetStageManager (vf40 del campo) → misiones del anfitrión (la regla del proyecto: bien)
          .xtls 0xDE16970 → instancia en mgr2, id nuevo del contador global (0x1F6B6E0)
          0x2348230([elem+0x200], dueño) → variante; .xtls 0xDE21450 → 0x1F70E50 → 0x1F73570
              tipos de nodo 1/4: 0x20D5620(campo, clave de personaje) → el MISMO NPC  (§7.2 del doc. del servidor)
          0x2A4DEF0 → 2661 ToTarget a pj2
```

- Hoy U-f (`user2_stages.cpp` `elem_detour`, `u2s::skip_element`) salta **todos** los elementos de secuenciador del
  gestor armado (`*res = 0`). Medido en `a5f550a`, en Hernand: 21 vistos y 21 saltados [V, u0 §8.12].
- `0x287C200` no tiene ninguna comparación con `[campo+0x180]` y no llama a vf48: recibe el gestor como parámetro
  [V decompilado]. El cerrojo `[mgr+8]` solo se toma alrededor de `.xtls 0x102BB110` (T2 de S2, cerrado).
- **Hilos (revisión M3):** el servidor reparte sus manejadores y temporizadores en **varios** hilos (en
  `cdmp_20261013-210033.log`, los `[ARMED]` salen en T41636 y T47568). «El hilo del servidor» en este documento quiere
  decir «un hilo del servidor». Cualquier lectura de las tablas de un gestor puede coincidir con otro hilo que las cambia
  (§8).
- Dentro del tick armado del usuario 2, toda vf48 ya devuelve `mgr2` (`user2_stages_timer_vf48`). Por eso lo que
  cuelga de `0x287C200` usa un solo gestor.

### 1.2 Qué cambiar en U-f

- Clave nueva del anfitrión, `[session2] user2_stages_elements = schedule | schedule+funcnpc` (ya prevista en
  `mundo-escenarios-propios.md` §5.1 y sin implementar [V grep]).
- En `elem_detour`, para `Element::Sequencer` de `mgr2` armado, leer el nombre base del recurso igual que ShopProbe P:
  `[[[elem+8]]]` → `char*`, SEH, ≤160 bytes, base tras la última `/` o `\`.
  - Dejar pasar al original **solo** si empieza por `cd_seq_funcnpc_` y el modo es `schedule+funcnpc`.
  - Los demás (`cd_seq_node_*`, minijuegos, misiones, clima) se siguen saltando.
- En `probe`, registrar los nombres de los elementos de secuenciador saltados (los primeros N por armado): no sabemos
  cuáles son los 21 de Hernand [H]. Si aparece un `cd_seq_node_*` del que dependa la tienda, se decide entonces.
  - `p8-s12-tenderos-servidor.md` §5.2 lo sospechaba («y las node_* que las activan») [H].
- No duplicar con una escena viva del anfitrión. Lejos (≥ 10 celdas), las `funcnpc` del anfitrión están ligadas a la
  celda (ventana de 2–4 celdas) y ya están cerradas [E, ventanas de S25]. En `probe`, contar «el anfitrión tiene viva
  una instancia con el mismo hash de recurso» recorriendo `[mgrH+0x28]`/`+0x30` (lista de secuenciadores de
  `kMgr_SeqList`; la disposición de cada entrada está por confirmar [H]).

### 1.3 El tendero único: cómo comparten un NPC dos gestores [E fuerte salvo lo marcado]

- **Generación:** registro de personajes únicos del campo (`ServerField+0xD8/+0xDC/+0xE8/+0xF0`; consulta
  `.xtls 0xE2B9EE0`, alta `0xE2B7070`, baja `0xE2B8B50` desde `0x2817E80`/`0x2BB4020`; `0x2817130` no genera uno ya
  registrado).
  - Si la stage de horario del anfitrión ya generó al tendero, la de `mgr2` no genera otro.
  - Con U-f2, además, `mgr2` ni crea esa stage si el anfitrión la tiene viva en la misma posición.
- **Escena:** el enlace sale del mapa del campo `+0xB8` (`0x20D5620`). La escena de `mgr2` enlaza el NPC que ya hay.
- **Doble enlace (T3):** `0x1F73570` (`0x1F73B17–0x1F73C4F`) solo quita al NPC de escenas antiguas que encuentra
  **en el gestor que prepara**. Con dos escenas vivas, el NPC queda enlazado a las dos. Lejos no ocurre, porque la del
  anfitrión ya está cerrada.
- **Vida:** el NPC pertenece a la stage que lo generó.
  - Si es del anfitrión y este la cierra (su carga, motivo 19), el NPC sale (2516) y la escena de `mgr2` se queda sin
    él hasta que `mgr2` vuelva a evaluar esa celda [H].
  - Lejos, lo normal es que lo haya generado la propia stage de `mgr2`, con el controlador = el anfitrión
    (`0x2792A70`: U-e sin implementar).
- **Controlador:** las tareas de IA de escena (`CompleteSequencerAITaskReq` 2170) las cierra el cliente que controla
  al NPC, que es el del anfitrión.
  - Ese cliente no conoce las escenas de `mgr2`. Si la escena de tienda espera una tarea de IA del tendero, se quedaría
    esperando [H].
  - Señal a favor: en toda la S11 solo hay 4 × 2170 [V], así que las tiendas no parecen depender de ellas.
  - Se mide con Shadow.

---

## 2. U2-a: las Req de escena del invitado y su vf48 (tabla exacta)

Todas leen el `u64` id de secuenciador en `payload+0`, con la misma lectura que ya hace R1 en
`observer_seq_copy_logic.h` (`kOp*`) [V]. Excepciones: 2044 (`paquete+3`) y 2671/2720 (por entrada). La vf48 es
`call [rax+0x48]` sobre el campo de `0x17FA6C0(actor de TrocParams)`.

| Op | Nombre | Manejador | vf48: retorno | Usa el gestor en |
|---|---|---|---|---|
| 2436 | StageBranchStateReq | `0x2A5C4F0` | **`0x2A5C6CB`** | `0x2891FB0` (→ `.xtls 0x102D2B70`) `(mgr,&r,campo,id,rama,u8,u8)`; Nak `0x3F5` si falla |
| 2591 | UpdateStageChartEventReq | `0x2A5CC70` → `.xtls 0x104C9B00` → `0x2C4A8A0` | **`0x2C4A8FC`** | `0x1F6C190(mgr,id)` en el sitio y luego `0x2936060(inst[0xB],&r,[mgr+0x18],…)`: **ejecuta el gráfico con dueño `[mgr+0x18]`** (con la ruta, `pj2`) |
| 2105 | StageBranchStateDelayEndReq | `0x2A5C920` | **`0x2A5CA68`** | `0x2892210` |
| 2704 | UpdateStageChartDelayedEventReq | `0x2A5CF40` | **`0x2A5D0E1`** | `0x28928D0` |
| 2173 | UpdateSequencerGamePlayDataEventReq | `0x2A5C090` | **`0x2A5C283`** | `0x2891CE0` |
| 2276 | DeleteSequencerReq | `0x2A5B590` | **`0x2A5B71D`** | `0x2879320` (→ `.xtls 0x102B8040`) |
| 2767 | SequencerTriggerVolumeInOutReq | `0x2978C20` | **`0x2978E20`** | `0x1F6C190` en `0x2978E6E` |
| 2138 | SequencerStageFailReq | `.xtls 0x104D2330` → `0x2C4AB80` → `.xtls 0x1089F230` | **`.xtls 0x1089F284`** | — |
| 2888 | ChangeSequencerPivotReq | `0x2A5E240` | **`0x2A5E401`** | `0x28952F0` |
| 2325 | StageQuitReq | `0x2A5DC70` | **`0x2A5DDF1`** | `0x28934F0` |
| 2406 / 2968 | SubTimelinePause / Resume | `0x2A5D530` / `0x2A5D8D0` | **`0x2A5D6B1`** / **`0x2A5DA51`** | `0x2892FE0` / `0x2893260` |
| 3020 | SetNextBranchTypeFromAIReq | `0x2A5BBE0` | **`0x2A5BF4D`** | `0x2895DC0` |
| 2044 | StageBranchStateOnceTimer (temporizador) | `0x2C33830` → `.xtls 0x1082FB80` | **`.xtls 0x1082FBE7`** | `0x2895820(mgr, paquete+3 = id, u32 paquete+0xB)` |
| 2054 | DeleteSequencerOnceTimer (temporizador; añadido después, §12.2) | `0x2C34300` → `.xtls 0x10835BF0` | **`.xtls 0x10835C57`** | `0x2879440(mgr, &r, paquete+3 = id, u8 paquete+0xB)`: el borrado diferido de una escena |
| 2170 | CompleteSequencerAITaskReq | `0x2A5B900` → `.xtls 0x104C9260` → `0x2BB4470` → `.xtls 0x106C1CA0` | **sin vf48** [V capstone] | va por el componente del NPC (`vf298`, `0x1F5C940`). No se toca en U2 [H] |
| 2671 | InteractionPreemptionStepReq | `0x29E8FA0` → `.xtls 0x1067B660` → `0x2B36EF0` | `0x2126A1F` **dentro de `0x2126920`** | la búsqueda `0x1F6C190` de `[entrada+0x20]` en `0x2126A6A` (U2-b, §3) |
| 2720 | InteractionDoStepReq | `0x29E9490` → … → `0x2B36B30` | no llega a `0x2126920` | valida con `vf190 0x21258B0`. `0x2126920` tiene un solo llamador, `0x212046B` (en `0x211FCB0`) [V xref] |

No son de escena (no se tocan): 2100 `SyncStageCandidatePostionReq` (posiciones, sin id; es de U-1, no de tiendas),
2873, 2454, 3046 y 2241.

### 2.1 La ruta (diseño)

- **Marco:** en `troc_detour` (`ExecuteTroc 0x2774B20`, ya enganchado), además del sujeto actual, leer con SEH para
  los opcodes de la tabla:
  - el opcode (`troc+0xC`);
  - el paquete (`params+0x18`) y su tamaño (`params+0x10`);
  - el id: `paquete+5` en las Req y `paquete+3` en el temporizador 2044.
  - Se hace **también cuando el sujeto no es el usuario 2**: el actor del 2044 lo pone quien lo encola [H]. La
    pertenencia del id a `mgr2` ya basta para discriminar, porque solo el cliente del invitado conoce esos ids
    (ToTarget).
- **Decisión,** una vez por Troc, guardada en el marco `t_u2route`. Con todo esto, el gestor a devolver es `mgr2`:
  - el campo de la Req es del anfitrión (`[campo+0x180]` = clave del anfitrión);
  - `mgr2` es legible (vtable `0x5B17A58`, `[mgr2+0x18] == pj2`) y su tabla tiene cubetas;
  - el id está en `mgr2`, según `isl::find_stage` (la réplica SEH de `0x1F6C190` de `stage_lookup_logic.h`, sin
    cerrojo; vale para el servidor porque es la misma función);
  - el id **no** está en `mgrH`. Si está en los dos: ERR y no se enruta. No debería pasar nunca: el contador es global.
  - El libro `own_` de `observer_seq_copy` (2661 propios del usuario 2) sirve de filtro previo barato, nada más.
- **vf48:** en `vf48_detour`, si el marco tiene `t_u2route` y el retorno está en la lista de la tabla → `mgr2`.
  - Las demás vf48 del mismo Troc se **cuentan** por (opcode, retorno), como M3 de U-1. El modo 1 de la primera
    versión las deja con el original.
  - Si la sonda muestra llamadas anidadas del gráfico (dentro de `0x2936060`), se decide como en el tick armado:
    todas a `mgr2`.
- **vf40** no cambia: misiones del anfitrión, por la regla.
- **Escrituras:** ninguna en memoria del juego. Solo cambia el valor devuelto.

---

## 3. U2-b: la comprobación del 2671 en el servidor

```
0x2B36EF0 → 0x2124060([[NPC+0x68]+0x98], dl=es jugador, &clave)   ← primera entrada con esa clave de info
         → vf170 = 0x211FCB0 → 0x212046B call 0x2126920(entrada, J)
              0x2126A1C call field.vf48 ; 0x2126A1F mov rdi,rax ; mov [rbp+0x8E8],rax   ← create_mid de ShopProbe (S/A)
              0x2126A6A call 0x1F6C190(rdi, &out, [r13+0x20])
```

- `.xtls 0xE402CC0` (SetInteraction) **añade al final** de `comp+0x88` (`count-1`); no reemplaza [V decompilado].
  `0x2124060` devuelve la **primera** con la clave [V]. Con dos escenas vivas sobre el mismo tendero, la entrada
  elegida en el servidor es la más antigua, sea del anfitrión o del invitado.
- **Regla U2-b (en `on_sa` de ShopProbe, rama de servidor = campo con vtable `ServerField 0x5B29A60`):** si
  `[r13+0x20] & 0x1FFFFFFF` está en `mgr2` y no en `RAX` (= `mgrH`) → `RAX := mgr2`.
  - Las dos instrucciones reubicadas copian `mgr2` a `rdi` y a `[rbp+0x8E8]`, así que el cerrojo, la búsqueda,
    `0x1F6FC40` y la liberación son coherentes (mismo razonamiento que C1, `p8-impl-tenderos-c1.md` §2.1).
  - **Independiente de quién pide:** una entrada de una escena de `mgr2` se valida contra `mgr2` también cuando pide el
    anfitrión. Eso cubre el caso de la entrada del invitado primero y el anfitrión hablando con el mismo tendero
    durante el traspaso, que hoy daría 3000 al anfitrión.
- **Comparte el gancho:** en el proceso del anfitrión, `0x2126A1F` ya lo tiene ShopProbe (no puede haber dos
  `create_mid` en el mismo punto). U2-b va dentro de `on_sa`, como U-a va dentro de `on_g` [V `shop_probe.cpp:551`].
  Requiere `shop_probe=1` o un modo «solo S» con los mismos bytes.
- Lo que sigue en `0x2126920` usa `0x1AC10F0(vf40, [inst+0x70])`: misiones **del anfitrión**, como pide la regla.

---

## 4. Caso B: cerca del anfitrión

### 4.1 Por qué C1 dejó al invitado atascado (S14) [V captura]

- Invitado `p8-sesion14-shadow\cdmp_packets_20261011-011426.log`, t = 140,85:
  `2671 A010036B → B010016C F4321` → `2663` → `2720` → `2916`. Después, en la misma ventana, el cliente manda
  `ChangeAIStateReq` (entra en el estado de interacción), movimiento y gimmicks, y **ningún 2436/2591**.
- Anfitrión (`p8-sesion14-host\cdmp_20261011-021131.log`, 02:17:15): `[server check] … stage id 0x19A … FOUND …
  result 2`, 2663 y 2916 encolados. No hay 2525 para F4321.
- El invitado reinició (log nuevo a las 01:18:22, personaje nuevo `A0100A4C`).
- **Lectura [E]:** el aviso se construyó con la entrada de **X**. Tras el 2916, el paso siguiente lo da la escena dueña
  de esa entrada, que es la del anfitrión. En el invitado, X solo se reproduce desde lo que manda el servidor
  (`guest_seq_play`) y no emite Req, y R1 descartaría su 2436 de todos modos. Nadie mueve la rama y el cliente se queda
  esperando.
- Referencia nativa (S11, `p8-sesion11-host`): `2916` a t = 415,024 y, 12 ms después, `2436 0x196 rama 6` + 7 × 2591
  del propio cliente [V].
- **Consecuencia:** el aviso del invitado tiene que salir de una entrada de **su** escena (M). C1 en `1` es incompatible
  con cualquier diseño de tiendas.

### 4.2 Opciones

| Opción | Qué es | Pros | Contras / riesgos | Veredicto |
|---|---|---|---|---|
| B0 C1 | Aviso con la entrada de X | Ya existe | Atasco [V] | **No** (queda solo como sonda) |
| **B1 Gemela S2 sobre U2** | En P `0x288AAEF`, por cada `funcnpc` del anfitrión preparada, encolar una gemela; se crea a la salida del Troc más externo con `0x287C200(mgr2, …, playData con +0 = pj2 y +8 = 0, [elem+0x200])`. Ruta U2-a/U2-b. Borrado en espejo (`0x2879440`), diferido y con espera | 1:1 con la escena del anfitrión; el tendero existe seguro (lo generó el anfitrión); no evalúa celdas cerca del anfitrión; T1 y T2 cerrados (`p8-impl-tenderos-s2.md` §3) | `mgr2` en estado 0/1 [H: ¿tabla de instancias usable tras el reinicio `.xtls 0x102C4060`?]; T3 (dos escenas en el NPC); un 2043 que llegue a `mgr2` en estado 1 la descarga; bloquea el armado de U-1 (`ListsNotEmpty`) hasta que se borre; variante de `0x2348230` evaluada con `pj2` (mismo tipo de personaje → misma [E]) | **Recomendada** |
| B2 «Modo cercano» de U-1 | `mgr2` armado también cerca, con U-f = solo `funcnpc` | Mismo código que el caso A; escenas alrededor del **invitado** | Evalúa stages cerca del anfitrión (arranques dobles, 2264/2165 al invitado, el doble de llamadas de evaluación de misiones: R2 de U); hace falta un vaciado selectivo o volver a armar en cada cambio de modo; hueco 4–10 celdas sin tendero | Alternativa si B1 falla en T-estado |
| B3 C2 espejo local | X reproduce la rama de tienda solo en el invitado | Sin servidor | Estado inventado; X diverge | No |
| B4 Solo lejos | Cerca del anfitrión no hay tiendas para el invitado (la sonda C1 dice por qué) | Cero riesgo | Funcionalidad parcial | **Provisional** mientras B1 no esté |

### 4.3 B1 en detalle

1. **Crear.** En P (`on_p`, ya instalado), con resultado 0, `funcnpc`, dueño = el personaje de `mgrH`, `pj2` en el
   mismo campo y U-1 **no** armado: copiar los ~0x48 B del `playData` (`[rbp-0x51]`) a una cola del hilo, con el
   puntero `[elem+0x200]` y el id del anfitrión `[rbp-0x49]`.
   - A la salida del `ExecuteTroc` más externo del hilo del servidor (`t_troc_depth == 1`, sea quien sea el sujeto;
     hoy `troc_detour` solo actúa a la salida para el usuario 2), poner `+0 = clave de pj2` y `+8 = 0` y llamar
     `0x287C200(mgr2, &res, &copia, bind, &o1, &o2)` con SEH. Se liberan las referencias como en `0x288AAEF…`.
   - Libro: id del anfitrión ↔ id de la gemela (`[copia+8]` tras la llamada, §7.6 del doc. del servidor).
   - **Diferido y no en P:** aunque T2 está cerrado, así no queda ninguna llamada dentro de la pila de evaluación del
     anfitrión. Es el mismo patrón que U-c/U-d.
2. **Borrar.** Gancho inline en la entrada de `0x2879440`: `48 89 5C 24 18 | 44 88 4C 24 20 | 48 89 54 24 10`
   [V bytes]; hay que barrer los saltos que caigan en los 5 primeros bytes.
   - Si `rcx == mgrH` y `[r8]` está en el libro, encolar el borrado de la gemela con el mismo `r9b`. Se ejecuta a la
     salida del Troc, **salvo** que el invitado esté «en tienda» con ella (§5): entonces se pospone, con un tope de
     120 s.
   - El gancho también cuenta los borrados de `mgr2`: los vaciados de U-1 del caso A.
3. **Antes de armar U-1:** borrar las gemelas vivas (o dejar que el vaciado A6 de sus cargas lo haga); si no,
   `ListsNotEmpty`.
4. **Ruta:** U2-a y U2-b, igual que en el caso A (las gemelas viven en `mgr2`).
5. **Cliente del invitado:** nada nuevo.
   - La gemela llega como 2661 propio → M (`guest_seq_route`/`gsr::gate` dejan pasar lo propio) → `SetInteraction`
     con ids de M → aviso nativo.
   - Requisito: `guest_x_interaction_stage` = `0` o `probe`, para que las entradas de X fallen en `0x2126920` y no
     se elijan. Opcional: vetar en X los `SetInteraction` de `funcnpc` (lo de la S11) para que el NPC solo tenga las
     de M.
   - Con `1` y U2 a la vez: WRN y C1 se queda en probe.
6. **Cliente del anfitrión:**
   - los 2981/2524 ToNear de la gemela llevan un id que su M no tiene → se ignoran [E, como los ajenos nativos];
   - `observer_seq_copy` no los copia (actor = `pj2`) [E];
   - contarlos (sumidero U-h ampliado).
7. **Dos a la vez (T5):** si los dos hablan con el mismo tendero, las dos escenas lo mueven. Medir; si molesta, vetar
   en el cliente el aviso del segundo.

---

## 5. Interacción con el vaciado de U-1 y con el traspaso

- **El vaciado borra las escenas:** `0x287DA50` → estado 1 → 2043 → `0x287D4C0(mgr2)` → `0x2879440` (`0x287D8B5`)
  → 2810 al invitado. Con la tienda abierta, la escena desaparece por debajo. De forma nativa no pasa (no se puede
  viajar con la tienda abierta). El resultado en el invitado es [H]: cierre limpio o atasco.
- **«En tienda»** (lado del servidor; el anfitrión lo ve todo):
  - empieza con un 2916 encolado al usuario 2 para un NPC enlazado a una escena `funcnpc` de `mgr2`;
  - sigue mientras haya Req de escena del usuario 2 sobre ese id en los últimos 20 s;
  - termina con un 2436 de ese id a la rama 0 (o la de reposo que muestre la sonda [H]: en la S11 se ve `0x196` rama 6
    al abrir y rama 0 a los 1,5 s y a los 6 s), o con un tope de **120 s**.
- **Espera de vaciado (U-d hold):** el `[DRAIN requested]` por distancia no se ejecuta mientras esté «en tienda»
  (línea `[drain held: in a shop]`); al terminar o al tope, se vacía.
  - Los vaciados por **carga o logout** del invitado (2283/2362/2437/2001) **no** esperan: son nativos.
- **Traspaso A → B:** tras el vaciado, el anfitrión crea sus `funcnpc` (como en el 8.14, 2 s después) → B1 crea las
  gemelas.
  - Ventana sin tienda: el 2043 del anfitrión más el diferido (≈ 5–10 s).
  - Si las dos escenas coinciden un momento sobre el NPC, U2-b valida cada entrada contra el gestor que la tiene.
- **Traspaso B → A:** borrar las gemelas antes del armado (§4.3-3).

---

## 6. Plan de pruebas

### 6.1 Claves (propuesta)

- Anfitrión:
  - `user2_stages_elements = schedule | schedule+funcnpc` (U-f);
  - `user2_shop_route = 0 | probe | 1` (U2-a, U2-b y la espera de vaciado);
  - `funcnpc_twin = 0 | probe | 1` (B1; hoy sigue forzado a probe).
- Requisitos del `1`:
  - `user2_stages=1` y `user2_timer_route=1` (caso A);
  - el desvío de vf48 y el sujeto de `user2_funnels`;
  - el punto S de ShopProbe (U2-b).
- Invitado:
  - `guest_x_interaction_stage=probe`. Su resumen ya cuenta las entradas `in M (native)`: es la medida del lado del
    invitado sin código nuevo.

### 6.2 `probe` (no cambia nada)

- U-f: `[element] … sequencer 'cd_seq_funcnpc_…' WOULD PASS (U2)` con el nombre, el hash, el índice estático, la
  posición y si `mgrH` tiene viva una instancia con ese hash. Más los nombres de los demás secuenciadores saltados.
- U2-a: `[route] would` por opcode y por retorno, con el id y el resultado de buscarlo en `mgr2` y en `mgrH`.
  - Hoy debería dar `would` solo con escenas propias de tipo 6 (como la `0x19D` de la S12, cuyos 12 × 2591 y 1 × 2436
    se perdieron).
- U2-b: `[server check] … would route (stage id in user 2's manager)`.
- B1: lo que ya da `FuncnpcTwin [would create]`, más el estado de `mgr2` (+0x6A0), cubetas y cuenta de la tabla de
  instancias (`[mgr2+0x108]`/`+0x10C`) y `[mgr2+0x30]`.
- Espera de vaciado: `[drain would wait: in a shop]`.

### 6.3 Un PC con `cdmp_peer` (anfitrión + peer)

Posiciones de las pruebas de U-1 (`u0-resultados-s28.md` §8.8): el anfitrión en su partida (ranura 1) y el peer
(ranura 108) en Hernand, a 62 celdas.

1. **A1, preparación** (`cdmp.p8-u1-local-1.ini` + `user2_stages_elements=schedule+funcnpc`, `user2_shop_route=probe`;
   guion `u1_idle5.troc`). Se da por bueno si:
   - `[ARMED]`, `[lists built]` y `[element] sequencer 'cd_seq_funcnpc_*' → to the original`;
   - `ShopProbe [funcnpc] … owner 0xA0…(peer) … result 0x0 | manager … of character (peer)`;
   - en `--record`, 2661 con dueño @5 = el personaje del peer, hash @27 de tienda (carnicería `0x978345CA`…) y un
     enlace con clave `B01…`;
   - el 2005 de ese NPC antes del 2661 o como mucho junto a él;
   - ningún 2661 de esos ids al cliente del anfitrión;
   - el despido del peer: `[DRAINED now]` y 2810 al peer para esos ids (lo cuenta el gancho de `0x2879440`, si ya
     está);
   - ningún ERR, `HOST TOUCHED` ni `ABORT`, y los buffs en cientos.
2. **A2, ruta** (`user2_shop_route=1`). Hace falta un guion de tienda (herramienta nueva, fuera del repo hasta que se
   apruebe):
   - una directiva **`learn_2661 SEQ NPC HASH`**: un 2661 con dueño = `PLAYER_KEY` y hash = `HASH` da `SEQ`
     (@9) y `NPC` (el primer enlace con `clave>>28 == 0xB`). La `learn` normal no filtra por dueño ni por hash. Va en
     `troc_script.h`, con tests (hecho: §11);
   - caminar del punto del peer al tendero (≈ 140 m hasta la herrería: el servidor comprueba distancia y ángulo en
     `0x2120CC0`);
   - reproducir la charla nativa de **ese mismo recurso** (S13, herrería `0x19D`, t = 541,37–552,77: 2436 y 2173; el
     cliente no manda ningún 2591 de la escena de tienda), con `SEQ` puesto;
   - `2671 PEER → NPC 0x000F4321 … 01` (la clave de tienda es la misma en S11, S12 y S14 [V]), `2720`, y
     `2436 SEQ rama 6`.
   - **Se da por bueno si:**
     - `[route] … 2591 … id in user 2's manager → user 2's` (y los demás opcodes);
     - `ShopProbe [server check] … looked up in manager (user 2's, routed) … FOUND | result 1/2`;
     - `[2671] USER 2 … ACCEPTED`, 2663/2916 al peer y, tras el 2436, 2981/2524 de `SEQ` en la grabación;
     - ningún 3000 ni Nak `0x3F5` para esos ids;
     - en el cliente del anfitrión no cambia nada (su M ignora el id).
3. **B1, gemela** (anfitrión en Hernand, a ≤ 2 celdas del peer; `funcnpc_twin=1`). Se da por bueno si:
   - `FuncnpcTwin [created] host 0x197 → twin 0x2xx` y el 2661 de la gemela al peer, con el **mismo** NPC que el 2661
     del anfitrión;
   - el anfitrión habla con ese tendero y **su** tienda se abre como siempre (lo ve el usuario en su PC: es la
     regresión clave);
   - el anfitrión viaja lejos: 2810 de su escena y luego 2810 de la gemela al peer; el armado de U-1 después, sin
     `ListsNotEmpty`.
4. **Regresión sin invitado en las tiendas:** el anfitrión usa dos tiendas con `user2_shop_route=1`. Las líneas `[server
   check]` del anfitrión tienen que seguir en «field owner's … FOUND».

### 6.4 Lo que necesita Shadow (avisar al usuario antes)

- **A:** el invitado en un pueblo lejos del anfitrión (≥ 10 celdas, U-1 armado):
  - aparece el aviso de tienda (guest `in M (native)` en `GuestXInter`);
  - R abre la interfaz; comprar y vender un objeto (dinero e inventario propios); salir;
  - el tendero se comporta normal (T-controlador, tareas de IA);
  - con la tienda abierta, el anfitrión se acerca a ≤ 7 celdas: `[drain held]` y el cierre limpio al salir o al
    tope.
- **B:** juntos en la ciudad:
  - el invitado usa la tienda (gemela), sin atasco;
  - el anfitrión no ve abrirse nada suyo ni pierde cámara;
  - después usan los dos el mismo tendero (T5);
  - el anfitrión se aleja con el invitado en la tienda (espera del borrado de la gemela).
- Las Req de compra y venta (`SellItemToItemStoreReq 2366`, `BuyStockFromStoreReq 2789`, `ExchangeTradeItemReq
  2458`…) son por usuario de forma nativa [E, `p8-sistemas-juego.md`]. Que alguna compruebe el estado de interacción o
  la escena es [H] (T6): se ve en Shadow por su Nak.

---

## 7. Criterios para abortar (modo 1 → probe hasta el final de la partida, con una línea ERR)

- `HOST TOUCHED` o cualquier `ABORT` de U-1 (los de siempre).
- Un id que esté a la vez en `mgr2` y en `mgrH` (no debería existir: contador global).
- **Atasco:** un 2916 al usuario 2 para un NPC de una escena `funcnpc` sin ningún 2436/2591 suyo sobre un id de `mgr2`
  en los 5 s siguientes. Es la firma de la S14.
- Un 3000 `PreemptionFail` o un Nak `0x3F5` al usuario 2 sobre un id enrutado.
- El anfitrión: un 3000 en su propio 2671, o un `[server check]` del anfitrión resuelto con `mgr2` y rechazado.
- `mgr2` prepara un elemento de secuenciador que **no** es `funcnpc` (U-f roto).
- Una gemela creada con resultado distinto de 0 dos veces seguidas, o `0x287C200` lanzando excepción
  (`game_fault_continue`: se apaga).
- Lo que no detecta el módulo y tiene que mirar quien prueba (Shadow): el anfitrión ve abrirse su diálogo o su tienda,
  o pierde cámara o control, cuando habla el invitado; el invitado pierde interacciones que antes tenía; el juego se
  cierra o se congela al abrir o cerrar la tienda.

---

## 8. Lecturas protegidas que añade U2

- `params+0x18`/`+0x10` (paquete y tamaño), el id en `+5`/`+3`, con el tamaño comprobado.
- La cadena `pj2 → mgr2` y la de `mgrH` (vtable, `[+0x18]`).
- `isl::find_stage` (todo SEH, sin cerrojo). El servidor tiene varios hilos (M3): otro hilo puede estar cambiando la
  tabla. Una tabla a medio cambiar da «no encontrada» (no se enruta) o una respuesta vieja; nunca una escritura.
- `[r13+0x20]` en `on_sa`.
- El nombre del recurso en U-f (≤160 bytes, byte a byte).
- `[campo+0x180]` y la vtable del campo.
- Llamadas al juego, solo en B1: `0x287C200` y `0x2879440` (bytes de entrada comprobados con los demás, SEH, a la
  salida del Troc más externo).
- Caso A: **ninguna llamada nueva al juego**. Solo cambian el valor devuelto por vf48, `RAX` en `0x2126A1F` y el
  filtro U-f.

---

## 9. Confianza

| Afirmación | Nivel |
|---|---|
| `mgr2` prepara `funcnpc` si U-f lo deja; 2661 a `pj2`; mismo tendero por el mapa del campo | [E fuerte] |
| Los retornos de vf48 de la tabla §2 | [V] capstone |
| `0x2126920`: un único llamador; el 2720 no la vuelve a ejecutar | [V] xref / [E] |
| SetInteraction añade al final; `0x2124060` toma la primera | [V] decompilado |
| El servidor añade su entrada dinámica sin los 2591 del cliente (corrige lo que decía este borrador) | [E] (§11.1) |
| Por qué falló C1 (no hubo 2436 tras el 2916) | [V] captura / [E] interpretación |
| La tienda se abre y deja comprar en el invitado | [H] |
| Tareas de IA del tendero y el controlador = anfitrión | [H] |
| Efecto del 2810 con la tienda abierta | [H] |
| `0x287C200` sobre `mgr2` en estado 0/1 (gemela B1) | [H] (medir en probe la tabla de instancias) |

## 10. Siguiente paso recomendado

1. U-f con `schedule+funcnpc`, U2-a/U2-b en probe y luego en 1, más la espera de vaciado: caso A, en un PC.
2. `learn_2661` y el guion de tienda del peer.
3. Shadow, caso A.
4. B1 (gemela diferida) en probe y luego en 1, en un PC; Shadow, caso B.
5. Hasta entonces, cerca del anfitrión: B4.

---

## 11. Implementado: paso 1, caso A (`user2_shop_route`, `user2_stages_elements`)

Build 1.0.0.2976. Solo en el anfitrión. Código:
- `src/mod/session2/user2_shops.{h,cpp}`: el módulo, sin ganchos propios;
- `user2_shops_logic.h`: la lógica pura, con tests en `tests/optd_user2_shops_tests.cpp` (`ctest -R optd_user2_shops`).

Lo llaman los dueños de los ganchos (nada se engancha dos veces):
- `user2_funnels.cpp`: `ExecuteTroc 0x2774B20` lee el id de las Req de escena; `ServerField::vf48 0x2B00CE0` es U2-a;
- `shop_probe.cpp`: S/A `0x2126A1F` es U2-b; P `0x288AAEF` lleva el libro de las escenas de tienda de `mgr2`; C
  `0x29E9282` da el resultado del 2671 del anfitrión;
- `user2_stages.cpp`: U-f `0x288A970` y la espera de vaciado;
- `server.cpp` (`take_frame`): 2916, 3000 y Nak 1013 encolados al usuario 2.

### 11.1 Lo que se corrigió del diseño al implementar

- **2591:** el `SetInteraction` del servidor no depende de los 2591 del cliente (§0, punto 2).
  - Herrería S13 (`0x19D`, la escena del Kliff del anfitrión): tras el 2916, el cliente manda `2436 rama 6`, cuatro
    2173 y, en 11 s, las ramas 0, 3 y 1 con sus 2173, más 2767 (volúmenes de NPC). Ningún 2591 con ese id [V].
  - Carnicería S12 (`0x197`): solo 2767 y dos 2173 [V].
- **La escena de la S13 era la herrería**, no la tienda de comestibles: ShopProbe la nombra `cd_seq_funcnpc_smithy`
  `0x19D`, hash `0xA7132561` [V]. `p8-impl-tenderos-s2.md` §1.2 decía «grocery».
- **El fin de la tienda no es la rama 0:** en la S13 la herrería pasó de la rama 6 a la 0 **1 s** después de abrir. La
  espera de vaciado acaba al tope (120 s desde el 2916) y no con una rama.
- **Revisión de `96dada0`** (independiente; nada bloqueaba la sonda). Corregido en el commit siguiente:

  | Hallazgo | Cambio |
  |---|---|
  | A1 (ALTA) la espera de vaciado no tenía suelo de distancia ni tope acumulado | Solo a > 5 celdas (a ≤ 5, vaciado en el acto), una vez por armado (otro 2916 no la renueva), ninguna tienda NUEVA en U-f a < 10 celdas ni mientras espera; la ventana vuelve a cero en cada armado, vaciado, cambio o salida del personaje y aborto |
  | M1 la sonda no ejercitaba la ruta | Paso A2: `user2_stages_elements=schedule+funcnpc` con `user2_shop_route=probe` prepara las tiendas y la ruta solo dice `would` |
  | M2 vf48 anidadas | La primera de cada (opcode, retorno) dentro de un Troc enrutado es WRN `[nested vf48]`; el paso B pasa sin ninguna |
  | M3 el servidor tiene varios hilos | Redacción de §1.1, §8 y §11.2; las líneas llevan el hilo; `InBoth` se queda |
  | M4 | La decisión exige U-1 armado y la despedida del usuario 2 no pedida (`U-1 not armed`) |
  | M6 (registro) | `[prepared]` dice si el anfitrión tiene viva una escena del mismo recurso |
  | B1 | Preparar exige los sitios 2436 / 2173 / 2767 y los puntos S y P de ShopProbe |
  | B2 | El seguimiento guarda la PRIMERA Req tras el 2916 |
  | B3 | La ventana solo se abre para un 2916 justo después de una comprobación U2-b de una entrada propia |
  | B4 | El aborto por Nak solo cuenta Nak de Req de escena (el opcode en `+7`) |
  | B5 | Un nombre ilegible no es «no es tienda»; solo con el usuario 2 en el campo del anfitrión; `n > 15` en ShopProbe como en `is_funcnpc` |
  | B6 | `learn_2661 … [OWNER [NODE]]` (el enlace del tendero), SEQ solo como u64 y NPC solo como u32, comentario de offsets (@9); el guion espera 2663/2916 (salvo `--no-expect`) y dice que la altura se interpola |

### 11.2 Las piezas (tras la revisión de `96dada0`)

| Pieza | Dónde (dueño del gancho) | Qué hace |
|---|---|---|
| U-f | `elem_detour`, `0x288A970` (user2_stages) | Del gestor del usuario 2 armado: lee el nombre base del recurso de cada elemento de secuenciador (`[[[elem+8]]]`, SEH, byte a byte, máx. 160; se registran 40). Veredicto (`u2sh::element_verdict`): un `cd_seq_funcnpc_*` va al original (`LET THROUGH`) solo con `user2_stages_elements=schedule+funcnpc`, la ruta **capaz de preparar** (`user2_shop_route` probe o 1, sin abortar, sus requisitos dentro) y el usuario 2 a ≥ 10 celdas del anfitrión **sin** la espera de vaciado en curso. Si no, saltado con el motivo (`would pass with …`, `cannot prepare it`, `< 10 cells … or the drain waits`). El resto, como antes. Todas las líneas `[element]` de secuenciadores llevan el nombre |
| Marco de escena | `troc_detour` (user2_funnels) | Para los opcodes de la tabla §2 (y los temporizadores 2044 y 2054), **sea cual sea el sujeto**: lee con SEH el paquete (`params+0x18`, tamaño `+0x10`) y el `u64` id (`+5` en las Req, `+3` en los temporizadores) |
| U2-a | `vf48_detour` (user2_funnels) | En un Troc con id de escena: llama al original y, si el retorno es el del manejador (§2), U-1 está armado, la despedida del usuario 2 no está pedida (M4) y el id está vivo en `mgr2` y **no** en lo que dio el original: devuelve `mgr2` (modo 1) o lo anota como `would` (probe). Las demás vf48 de ese Troc conservan el original, se cuentan y la primera de cada (opcode, retorno) sale como línea `[nested vf48]`: WRN si el Troc fue enrutado (M2) |
| U2-b | `on_sa`, `0x2126A1F` (shop_probe, solo `ServerField`) | Con las mismas condiciones: si `[r13+0x20]` está vivo en `mgr2` y no en `RAX`, `RAX := mgr2`, pida quien pida. La línea `ShopProbe [server check]` dice `user 2's own, ROUTED by U2-b` |
| Libro | `on_p`, `0x288AAEF` (shop_probe) | Escenas que prepara `mgr2`: las de tienda (resultado 0) se apuntan. Un secuenciador cuyo nombre **se leyó** y no es de tienda, con U-1 armado y el usuario 2 en el campo del anfitrión, es un criterio de aborto (B5: un nombre ilegible no cuenta; el prefijo solo no es tienda en ningún lado). M6, solo registro: si el anfitrión tiene viva una escena del mismo recurso (hash del playData `+0x20`), la línea `[prepared]` lo dice (WRN) |
| Ventana de tienda | `take_frame` (server.cpp) | Un 2916 de la interacción `0x000F4321` encolado al usuario 2 **justo después** (≤ 5 s) de una comprobación U2-b de una entrada **suya** (enrutada, o `would` en probe) abre la ventana (B3). Si en 5 s no hay ninguna Req suya enrutada a su gestor, es la firma del atasco de la S14. El seguimiento guarda la **primera** Req (B2) |
| Espera de vaciado | `control` (user2_stages) | El vaciado **por distancia** espera mientras dure esa ventana (120 s desde el 2916), **solo** con los jugadores a más de 5 celdas, y **una sola vez por armado**: un 2916 posterior no la renueva (A1). A ≤ 5 celdas (o distancia desconocida): vaciado en el acto (`[drain NOT held]`, WRN). Otro campo, sus cargas y su logout no esperan nunca |
| Reinicio | user2_stages → `user2_shops_reset` | La ventana y la espera vuelven a cero en cada armado, en cada vaciado, cuando cambia o se va el personaje del usuario 2 y en el aborto de U2 (A1). Línea `[window reset]` si había algo abierto |

- **Comprobación de bytes:** los 15 sitios: los 3 bytes antes de cada retorno tienen que ser `FF 50 48` (`call
  [rax+0x48]`). Un sitio que no lo cumple no se enruta y lo dice la línea de arranque.
- **Requisitos para preparar tiendas** (`user2_shops_may_prepare`; si falta uno, U-f no deja pasar ninguna):
  - `user2_shop_route` probe o 1, sin abortar;
  - los sitios **críticos** 2436, 2173 y 2767 comprobados (B1);
  - los ganchos de vf48 y sujeto de `user2_funnels` vivos;
  - los puntos S y P de `shop_probe` instalados (B1). Sin `shop_probe=1`, `user2_shop_route` no arranca.
  - La ruta real (modo 1) además pide `user2_shop_route=1`.
- **Hilos (M3):** el servidor reparte sus manejadores en **varios** hilos (los `[ARMED]` del 13-10 salieron en T41636 y
  T47568).
  - Todo es atómico y las búsquedas son réplicas de solo lectura sin cerrojo: una tabla cambiando en otro hilo da «no
    encontrada» y no se enruta.
  - Las líneas `[route]` y `[server check]` llevan el hilo (`T…`).
  - El aborto `InBoth` se queda.
- **Nada escribe memoria del juego ni llama a funciones del juego:** solo cambian el valor que devuelve vf48, `RAX` en
  `0x2126A1F` y qué elementos llegan al original.

### 11.3 Criterios para abortar (modo 1 → probe hasta el final de la partida; línea `User2Shops [ABORT]`, ERR)

- Un id vivo en los dos gestores (`InBoth`).
- **Atasco:** un 2916 de tienda de su propia escena (B3) sin Req suya enrutada en 5 s, con la ruta viva.
- Un 3000 de tienda al usuario 2 en los 2 s siguientes a un 2671 enrutado por U2-b.
- Un Nak 1013 al usuario 2 **de una Req de escena** (el opcode en `+7` del Nak, B4) en el segundo siguiente a una Req
  enrutada.
  - El formato sale del decompilado (`SendNak(…, 0x3F5, &buf, 0x11)`: `u16 0x3F5 | u8 0xFF | i32 error | u16 opcode @7
    | u64`) [E]. Aún no se ha capturado ningún Nak de escena: la línea `[to user 2] Nak 1013` escribe sus bytes y el
    opcode leído, para comprobarlo en el paso A2.
- El 2671 **del anfitrión** rechazado en el segundo siguiente a una comprobación enrutada para él.
- `mgr2` (armado, en el campo del anfitrión) prepara un secuenciador cuyo nombre se leyó y no es de tienda (B5).
- Además siguen los de U-1, y lo que tiene que mirar quien prueba (§7).
- El aborto vacía la ventana: el vaciado deja de esperar en el acto.

### 11.4 Prueba en un PC (anfitrión + `cdmp_peer`): pasos A, A2 y B

**Posiciones:** las de 8.14:
- el anfitrión carga su partida (ranura 1), a 62 celdas del peer, así que U-1 se arma solo;
- el peer (ranura 108) aparece en Hernand (−10749; 616; −3938) y anda **141 m** en línea recta, con la altura
  interpolada, hasta el mostrador de la herrería (−10637,7; 613,4; −3850,7), donde estaba el anfitrión al hablar en
  la S13.

**Paso A del 14-10 (`ca1830a`, `cdmp_20261014-081527.log`): sin ninguna tienda.**
- El gestor del usuario 2 evaluó Hernand dos veces: al armarse en la celda (−168,−62) y tras andar hasta el mostrador,
  en (−168,−61) y (−167,−61). Salieron:
  - 63 elementos de horario;
  - 19 de secuenciador: minijuegos, viento, `cd_seq_quest_hernand_safezone_2500`…, **ningún `cd_seq_funcnpc_*`**.
- El paseo funcionó (el servidor tenía al peer en (−10638; 613; −3851)).
- El anfitrión, con la partida **nueva** de la ranura 1, sí preparó una tienda **suya** (`cd_seq_funcnpc_church` a las
  08:34:05).
- **Lectura [E]:** las stages de las tiendas de Hernand no se aceptan con el progreso del anfitrión.
  - La evaluación (`0x28818F0`) solo acepta un id que ya esté activo, que sea uno de los dos ids del contexto, o cuyo
    estado sea 2 o 5 en el gestor de misiones del contexto, que por la regla del proyecto es **el del anfitrión**.
  - Con la partida antigua del anfitrión (S11–S14), esas mismas tiendas se preparaban al entrar en Hernand.
  - No es la hora: en la S13 se prepararon a las 00:31.
  - Es el comportamiento que pide la regla: el invitado no tiene tiendas que el anfitrión aún no ha desbloqueado.
- **Para probar el caso A en un PC:** el anfitrión necesita una partida con Hernand avanzado y estar lejos de él.
  1. Copiar `backups\host-slot1-antes-20261013-1821\sandbox\{lobby,save}.save` en la ranura 1 del sandbox
     (`%LOCALAPPDATA%\Pearl Abyss\CD\cdmp_save_sandbox\<id-cuenta>\slot1\`), guardando antes la nueva en `backups\`.
  2. Esa partida empieza en Hernand, junto al peer: el anfitrión hace un **viaje rápido** a un punto a ≥ 11 celdas
     (≥ 704 m) y se queda allí antes de arrancar el peer.
- **Arranque tardío:** el túnel no se conectó hasta las 08:34:28; el armado a las 08:34:42 es el normal (10 s de espera
  más una Req).
- Las líneas `U-1 not armed` de antes eran las Req de escena **del anfitrión**. Desde `aa438bf` se cuentan
  como `not in user 2's manager`, y `not armed` queda solo para un id del usuario 2.

**Preparación** (desde la raíz del repo, con el juego cerrado):
1. `.\scripts\build.ps1` y `.\scripts\deploy.ps1`.
2. Copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups\` (U-1 escribe en la memoria del juego).
3. Los tres guiones (no se suben al repositorio):
   ```
   py -I -B tools\scripts\make_shop_script.py docs\research\data\grapple_s25_g1_walk_138_217.troc build\e2e\u2_walk.troc --no-talk
   py -I -B tools\scripts\make_shop_script.py docs\research\data\grapple_s25_g1_walk_138_217.troc build\e2e\u2_shop_probe.troc --no-expect
   py -I -B tools\scripts\make_shop_script.py docs\research\data\grapple_s25_g1_walk_138_217.troc build\e2e\u2_shop.troc
   ```
   - `u2_walk.troc` (A): login, el paseo (235 pasos de 0,6 m), 180 s quieto en el mostrador, 150 s más y la despedida.
   - `u2_shop_probe.troc` (A2) y `u2_shop.troc` (B) añaden:
     - `learn_2661 SEQ NPC SHOP_HASH PLAYER_KEY SHOP_NODE` (hash `0xA7132561`, nodo `0x20000008`: el enlace del herrero en
       el 2661 de la S13);
     - `expect_learned SEQ/NPC` (si `SEQ` no llega en esos 180 s, el guion se para ahí: la escena no se preparó);
     - la charla nativa de la herrería de la S13: 2671 → 2720 → ramas 6/0/3/1 con sus 2173.
   - `u2_shop.troc` espera el 2663 tras el 2671 y el 2916 tras el 2720 (5 s cada uno); `u2_shop_probe.troc` no los
     espera, porque en probe el 2671 recibe un 3000.

El peer, siempre con el mismo comando y otro guion y otra grabación:
```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script build\e2e\<guion>.troc --var ACCOUNT=1 --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\<nombre>.bin --start-after 3000 --linger 30000 --seconds 900
```
Lanzar el juego, cargar la ranura 1, quedarse quieto y arrancar el peer.

**Paso A: probe sin escenas** (`bin64\cdmp.ini` = `docs\research\ini\cdmp.p8-u2-local-probe.ini`; guion
`u2_walk.troc`, grabación `u2-a.bin`). Pasa si:
- sale `User2Shops (user2_shop_route=probe): U2-a at the vf48 call of 15 of 15 scene handlers`, sin `left out` ni
  `CRITICAL`;
- salen `[ARMED]`, `[lists built]` y líneas `User2Stages [element] … element 'cd_seq_funcnpc_…' -> SKIPPED (a shop
  scene: would pass with user2_stages_elements=schedule+funcnpc)`. Ahí se ven los nombres de todos los secuenciadores
  de Hernand;
- el resumen de U-1 trae `its sequencers by U2's verdict (user2_stages_elements=schedule): [… would pass …] N`, con N > 0;
- `User2Shops summary` dice:
  - `may prepare yes` (los ganchos y puntos dentro);
  - en las rutas, solo `not in user 2's manager` o `U-1 not armed`;
  - `user 2's manager prepared 0`;
  - ningún ABORT;
- no hay ERR, `HOST TOUCHED` ni `[ABORT]` de U-1, y los buffs siguen en cientos.

**Paso A2: probe con escenas** (`cdmp.p8-u2-local-probe-a2.ini`, con `user2_stages_elements=schedule+funcnpc` y
`user2_shop_route=probe`; guion `u2_shop_probe.troc`, grabación `u2-a2.bin`). El gestor del usuario 2 **prepara** sus
tiendas de verdad. Sus Req llegan al gestor del anfitrión como hasta ahora: se pierden o reciben un Nak, sin daño. La
ruta solo dice `would`. Pasa si:
- salen `[element] … 'cd_seq_funcnpc_smithy…' -> LET THROUGH` y `User2Shops [prepared] … booked`, sin la marca M6 (`the
  host's manager has this resource alive too`);
- el peer imprime `SEQ = 0x…, NPC = 0xB…` (`node 0x20000008`), y en la grabación, el 2661 de `SEQ` con dueño = el peer y
  el 2005 del herrero antes;
- `User2Shops [route] … 2436 … 2173 … would be user 2's manager (probe)` y `[server check] for user 2 … would be user
  2's manager`;
- tras el 2916 (o el 3000):
  - si llega un 2916, `[to user 2] 2916 … a shop of its OWN scene: the window opens` y luego `[shop NOT followed] …
    probe: expected`;
  - lo normal en probe es un 3000, sin ventana;
- los `[to user 2] Nak 1013` que haya, con sus bytes: apuntar si el opcode en `+7` es el de la Req (comprueba B4);
- no hay ABORT, ERR ni `HOST TOUCHED`, y el anfitrión no ve nada.

**Paso B: modo 1** (`cdmp.p8-u2-local-1.ini`; guion `u2_shop.troc`, grabación `u2-b.bin`). Pasa si:
1. **Preparación:** lo mismo que en A2.
2. **Ruta:**
   - `User2Shops [route] … 2436 … (a shop scene of user 2's own manager) … ROUTED to user 2's manager`, y lo mismo para
     2173;
   - `ShopProbe [server check] … for USER 2 … (user 2's own, ROUTED by U2-b): FOUND … result 1/2`;
   - `ShopProbe [2671] USER 2 asked … 0x000F4321 … ACCEPTED`, y el guion pasa sus `expect 2663` y `expect 2916`;
   - `[to user 2] 2916 … window opens` y, en menos de 5 s, `User2Shops [shop followed]`;
   - en la grabación, tras cada 2436: 2981/2524 de `SEQ` (`analyze_capture.py u2-b.bin scenes`);
   - en el resumen: `server checks (U2-b): [ROUTED …] ≥ 1`, `its own 1 (followed 1, not followed 0)`, `3000 0`, `Nak 0`.
3. **Sin líneas `[nested vf48]` WRN** (M2). Si sale alguna, se analiza (qué función pidió el gestor del anfitrión
   dentro de un Troc enrutado) **antes** de Shadow.
4. **Sin tocar al anfitrión:**
   - ningún `[ABORT]` (ni de U-1 ni de U2), `HOST TOUCHED` ni ERR;
   - el `[server check]` del propio anfitrión sigue en «field owner's».
5. **Borrados diferidos (2054, §12.2):** cada `User2Shops [deferred delete] … DeleteSequencerOnceTimer … ROUTED` de
   una escena del usuario 2 es un borrado que antes se perdía. Pasa si ninguna escena del usuario 2 queda huérfana:
   - ningún `[deferred delete] … not in user 2's manager` para un id que el resumen tenga como suyo;
   - tras el vaciado y la despedida, el `[state]` de U-1 da `seq 0` en el gestor del usuario 2;
   - en la grabación (`scenes`), toda escena propia del peer acaba con su 2810.
6. **Al final:** `[DRAINED now] | its logout`, `[window reset]` y los cierres.

**Queda fuera hasta verificar A1 en Shadow:** el viaje del anfitrión a Hernand durante la ventana de tienda (la espera
de vaciado), por si la espera se comporta mal y deja juntos a los dos gestores.

Qué buscar: `User2Shops|User2Stages \[(element|ARMED|lists built|DRAIN|DRAINED|ABORT)|ShopProbe \[(funcnpc|server check|2671)`.

**Si algo falla:**
- `SEQ` no aprendido en A2 o B, con `LET THROUGH` en el log: mirar `ShopProbe [funcnpc]` (¿resultado distinto de 0?) y
  el 2661 en la grabación (¿hash o nodo distintos de los de la herrería?).
- `SKIPPED (… cannot prepare it)`: `User2Shops summary … may prepare NO`. Mirar la línea de arranque (sitios, ganchos,
  puntos S/P).
- `[server check] … NOT in user 2's manager` en B: el servidor no tiene la entrada de la escena del peer. El supuesto de
  §11.1 (SetInteraction sin 2591) falla en el servidor: mirar qué 2591 manda el invitado real en Shadow.
- 3000, Nak o atasco en B: lo dice la línea `ABORT`; volver a `user2_shop_route=probe`.

### 11.5 Plan para Shadow (avisar al usuario antes)

Configuración:
- **Anfitrión:** la estable (`cdmp.p8-estable-host.ini`) con U-1 (`user2_stages=1`, `user2_timer_route=1`),
  `shop_probe=1`, `user2_stages_elements=schedule+funcnpc` y `user2_shop_route=1`. Si el paso B en un PC no salió
  limpio, empezar por `probe`.
- **Invitado:** la de siempre, con `guest_x_interaction_stage=probe` (**nunca 1**: §4.1).

Pruebas, en orden:
1. **El invitado se va lejos** (≥ 11 celdas, a una ciudad con tiendas) y espera `[ARMED]` en el log del anfitrión.
   - Se acerca a un tendero: el aviso de tienda (`GuestXInter … in M (native)`).
   - R abre la interfaz; compra y vende un objeto (dinero e inventario propios); sale.
   - Apunta la hora de cada paso.
   - Pasa con `[shop followed]`, sin `ABORT`, y con la interfaz abierta y cerrada sin atasco.
2. **Otra tienda** de la misma ciudad (pub, tienda general, ropa…).
3. **El tendero:** que se comporte normal (animación, mirada) mientras el invitado compra. Una tarea de IA esperando
   (`CompleteSequencerAITaskReq`) se vería como una rama que no avanza.
4. **Solo si 1–3 salen bien, la espera (A1):** con la tienda abierta, el anfitrión se acerca a 6–7 celdas.
   - Esperado: `[drain held]`, nada de vaciado mientras siga abierta (máx. 120 s), y luego el cierre limpio.
   - Si se acerca a ≤ 5 celdas: `[drain NOT held]` y vaciado en el acto. Apuntar qué ve el invitado (2810 con la tienda
     abierta).
5. **Abortar** (volver a `user2_shop_route=probe`) en cuanto:
   - el invitado se quede atascado tras pulsar R;
   - el anfitrión vea abrirse algo suyo o pierda cámara o control;
   - salga cualquier `ABORT`, `HOST TOUCHED` o ERR.

---

## 12. Caso B a fondo: la gemela S2 sobre U2 y U-1 (borrador para implementar tras el caso A en vivo)

Solo lectura (2026-10-13, noche). Ghidra y capstone, build 1.0.0.2976. **Es solo el plan:** `funcnpc_twin` sigue
retenido en probe y no se implementa hasta que el caso A pase en vivo. Lo único ya implementado es el sitio 2054
(12.2).

### 12.1 ¿Funciona `0x287C200` sobre `mgr2` en estado 0/1, sin armar U-1? [E fuerte]

- La cadena que crea la instancia **no lee** el estado (`+0x6A0`), ni la lista de ids (`+0x150/+0x158`), ni las tablas
  por categoría (`+0x400/+0x408`), ni la celda (`+0x680/+0x684`). Lo he comprobado buscando esos desplazamientos en los
  decompilados de [V grep]:
  - `0x287C200` y `.xtls 0x102BB110`;
  - `.xtls 0xDE16970` (la creación) y `mgr.vf[0x98]` (`.xtls 0x102D2E20`);
  - `.xtls 0xDE21450` y sus `vf[0xE0/0xE8/0xF0]` (`0x288D2D0`, `0x288E870`, `0x288F0D0`);
  - `0x1F6C540` y `0x1F74500`.
- **Lo que sí necesita:**
  - el personaje del gestor `[mgr+0x18]` y su usuario (`[[mgr+0x18]+0xA0]+0xD8`, en `vf E8/F0`), es decir, el usuario 2
    conectado;
  - el campo del dueño (`0x17FA6C0(pj2)`, el compartido);
  - los datos estáticos del recurso;
  - la tabla hash de instancias (`+0x108..+0x120`).
- **El reinicio** `vf[1]` = `.xtls 0x102C4060` (pasa el gestor a estado 0):
  - vacía la lista `+0x28/+0x30`: solo suelta referencias, **sin 2810**;
  - pone la tabla a `+0x10C = 0`; las cubetas `+0x118` siguen reservadas, puestas a cero;
  - la tabla sigue sirviendo para insertar [E];
  - si una gemela está viva durante un reinicio, queda huérfana en el cliente. Solo ocurre en las cargas del usuario 2,
    cuando su cliente también recarga [H, riesgo bajo].
- **Conclusión [E fuerte]:** la gemela se puede crear sin armar U-1 y sin `0x2896C60`/`0x287FA30`, con `mgr2` en
  estado 0 o en estado 1 ya descargado.
- **Pendiente [H, medio]:** si algo de la escena en el servidor necesita el tick 2043 del gestor (las instancias tienen su
  propio temporizador, el 2044). Lo dirá la prueba en un PC.

### 12.2 Borrado en espejo en `0x2879440` [V llamadores y bytes]

**El punto:**
- Entrada `48 89 5C 24 18` (`mov [rsp+0x18],rbx`): una instrucción entera de 5 bytes, apta para un gancho inline. Le
  siguen `44 88 4C 24 20 | 48 89 54 24 10 | 55 56 57 …`.
- Firma: `(mgr rcx, int* res rdx, const u64* id r8, u8 r9b)`.
- Dentro: cerrojo `[mgr+8]` (`0x393DF0` en `0x2879499`), búsqueda `0x1F6C190` (`0x28794A9`) y el 2810.
- Al implementar: barrido de saltos que caigan en `0x2879441..0x2879444`.

**Llamadores** (todos en hilos del servidor, y hay varios: M3):

| Llamador | Qué es | `r9b` |
|---|---|---|
| `0x287D8B5` en `0x287D4C0` | descargar todo (tick en estado 1, U-d) | `0xE` |
| `0x27FB895` en `0x27FB370` | cierre de stages de clase 2 | — |
| `.xtls 0x102B80DB` en `vf[6]` (`0x2879320` → `.xtls 0x102B8040`) | `DeleteSequencerReq 2276` y quien use `vf6` | el del Req |
| `.xtls 0x102C9DD0` en `vf[2]` (`0x287EDF0` → `.xtls 0x102C9D00`) | cierre interno del gestor (tras `0x287E7E0`) | — |
| `.xtls 0x10835C68` en **`DeleteSequencerOnceTimer 2054`** (`0x2C34300` → `.xtls 0x10835BF0`) | borrado diferido: `vf48` (retorno **`.xtls 0x10835C57`**), luego `0x2879440(mgr, …, paquete+3, [paquete+0xB])` | byte `+0xB` |

**Hallazgo que afectaba ya al caso A:** el 2054 pide el gestor con `vf48`.
- Un borrado diferido de una escena de `mgr2` iba al gestor del anfitrión y **se perdía**: la escena quedaba huérfana en
  `mgr2` y en el M del invitado.
- **Ya está en U2-a** (sitio número 15): retorno `0x10835C57`, id en `paquete+3` como el 2044, y los 3 bytes antes del
  retorno (`FF 50 48`) se comprueban.
  - Cada borrado de una escena propia sale como `User2Shops [deferred delete]` y se cuenta en el resumen.
  - No cuenta como seguimiento de la tienda.
  - Entra en los criterios del paso B (§11.4).

**Cómo funciona el espejo:**
1. En la entrada, si `rcx` es el gestor del anfitrión (vtable y `[+0x18]`) y `[r8]` está en el libro de gemelas
   (id del anfitrión ↔ id gemela), se encola «borrar la gemela con el mismo `r9b`».
2. **Nada se llama dentro del gancho.** Se ejecuta a la salida del `ExecuteTroc` más externo del mismo hilo.
   - Hoy `troc_detour` vuelve antes, sin lógica de salida, cuando el Troc no es del usuario 2.
   - Hay que añadir, para cualquier sujeto: «cola de gemelas del hilo no vacía y `t_troc_depth == 1` → hacerlo después
     del original». Es el mismo gancho, no hace falta otro dueño.
3. **Espera:** si el invitado está en la tienda de esa gemela (la ventana de §11, por su NPC), se pospone hasta que
   acabe la ventana, como mucho 120 s.
4. Antes de llamar, `find_stage(mgr2, id gemela)`: si ya no está (un vaciado o un reinicio), no se llama.
5. La llamada: `0x2879440(mgr2, &res, &id_gemela, r9b)` con SEH.

### 12.3 Un 2043 que llega a `mgr2` en estado 1 borra las gemelas [V decompilado]

**Qué pasa:** `0x287DBB0` en estado 1 llama a `0x287D4C0`. Este recorre `[mgr+0x28]` (cuenta `+0x30`) y borra con
`0x2879440(…, 0xE)` **todas** las instancias, salvo:
- las de tipo `+0x60 == 3`;
- las que están en la lista de conservar `[mgr+0xC8]` (cuenta `+0xD0`, el `kMgr_KeepCount` de U-1).

Las gemelas son tipo 1: se borran, con 2810 al invitado.

**¿Cuándo llega un 2043 a `mgr2` en estado 1?** Con `user2_timer_route=1`, todo 2043 del usuario 2 llega a `mgr2`:
- el inmediato que pone U-d (`0x287DA50`): descarga y **la cadena termina**, porque en estado 1 no reprograma;
- después no llega ninguno hasta que U-c vuelva a armar;
- en estado 0 (nunca armado) la cadena sigue, pero `0x288B7A0` sale en `0x288B7FF` sin descargar nada.

**Cómo conservarlas sin tocar `+0xC8`:**
- crear la gemela solo con `mgr2` en estado 0, o en estado 1 **con la descarga ya hecha**: el punto U de U-1 ha visto
  `0x287D4C0(mgr2)` después del último vaciado y la cadena está «ended»;
- si no se cumple, «no twin: user 2's manager not settled»;
- una gemela borrada por un unload inesperado da WRN `[twin lost]`, que se cuenta.

### 12.4 Traspaso B → A: las gemelas vivas bloquean el armado [V]

`arm_verdict` exige `[mgr+0x30] == 0` (`ListsNotEmpty`). El orden limpio cuando el invitado se aleja:
1. La histéresis dice ON (≥ 10 celdas durante 10 s). Las tiendas del anfitrión están a 1–3 celdas de él, así que el
   invitado no puede estar en una tienda gemela.
2. A la salida de la siguiente Req del usuario 2: borrar todas las gemelas del libro con `0x2879440(mgr2, …, 0xE)`.
3. En la Req siguiente, con `[mgr2+0x30] == 0`: armado normal (U-c y U-c2).
4. Si tras 3 Req quedan gemelas: `[ARM not asked] … twins alive` (WRN). A los 30 s, aborto de la gemela.

Al revés (A → B, el anfitrión se acerca):
- el vaciado de U-1 borra las escenas propias de `mgr2` (§5);
- las gemelas llegan cuando el gestor del anfitrión prepara sus `funcnpc` (P), con `mgr2` ya asentado (12.3).

### 12.5 Un tendero con dos escenas (T3): qué ve el anfitrión

**Servidor:**
- `0x1F73570` (`0x1F73B17–0x1F73C4F`) solo quita al NPC de las escenas que encuentra en el gestor que prepara. La gemela
  no le quita la escena del anfitrión ni al revés: el NPC queda enlazado a las dos [V/E].
- Las entradas dinámicas: una por escena con la misma clave `0x000F4321`, añadidas al final [V `.xtls 0xE402CC0`].

**El 2671 del anfitrión:**
- `0x2124060` toma la primera entrada con esa clave, normalmente la de su escena (es la más antigua).
- Si la primera fuera la de la gemela (su escena recreada después), **solo U2-b** la valida contra `mgr2`. Sin U2-b, el
  anfitrión recibiría un 3000.
- Por tanto, **la gemela exige `user2_shop_route=1`** [E fuerte].

**Cliente del anfitrión [E]:**
- no recibe el 2661 de la gemela (va ToTarget al usuario 2);
- los 2981/2524 ToNear de la gemela traen un id que su M no tiene: los ignora, como los ajenos nativos;
- su aviso y su tienda salen de su propia escena: sin cambios.

**Riesgos [H]:**
- funciones del gráfico de la gemela que el servidor aplica al NPC compartido (`BlockInteraction`, categoría de IA,
  mirada): el anfitrión vería al tendero «ocupado» o girado;
- los dos comprando a la vez (T5);
- **regresión a vigilar:** que el anfitrión siga abriendo su tienda con la gemela viva.

**Cliente del invitado:** el NPC está en X (la copia de la escena del anfitrión) y en M (la gemela). Con
`guest_x_interaction_stage` en 0 o probe, las entradas de X fallan en `0x2126920` y el aviso sale de M (§4.3).

### 12.6 Plan de implementación (B1)

**Clave:** `funcnpc_twin = 0 | probe | 1` (ya existe; hoy se queda en probe). Requisitos del 1:
- `user2_shop_route=1` vivo (U2-a y U2-b);
- `shop_probe=1` (P y S);
- `user2_stages` distinto de 0 con `user2_timer_route=1` (gestores, distancia, puntos U y L);
- el sujeto de `user2_funnels`;
- el 2054 añadido a los sitios.

**Puntos** (todos existen salvo `0x2879440`):

| Punto | Dueño | Bytes | Qué hace |
|---|---|---|---|
| P `0x288AAEF` | shop_probe (`create_mid`) | `90 48 8B 4D 77` | Para cada `cd_seq_funcnpc_*` que prepara el anfitrión con resultado 0, si se cumple todo lo de la nota 1, copia a una **cola del hilo**: `0x80` bytes de `playData` (`[rbp-0x51]` hasta `rbp+0x2F`, dentro del marco), `[rdi+0x200]`, el id del anfitrión `[rbp-0x49]` y el hash `[rbp-0x31]` |
| Salida de `ExecuteTroc` | user2_funnels (inline, ya existe) | — | Con profundidad 1, cualquier sujeto y la cola no vacía, crea la gemela (nota 2) |
| `0x2879440` entrada | **nuevo**, inline (5 B) | `48 89 5C 24 18` | Espejo diferido (12.2) |
| U2-a 2054 | user2_funnels / user2_shops | retorno `.xtls 0x10835C57`, id en `paquete+3` | Ya implementado (caso A) |

Nota 1, condiciones de la copia en P:
- el dueño es el personaje del anfitrión;
- U-1 **no** está armado;
- `mgr2` está asentado (12.3);
- el usuario 2 está en el campo del anfitrión y no se está despidiendo.

Nota 2, la creación a la salida de `ExecuteTroc`:
1. Pone `+0 := clave de pj2` y `+8 := 0` en la copia.
2. Llama `0x287C200(mgr2, &res, &copia, bind, &o1, &o2)` con SEH.
3. Suelta `o1` como hace `0x288AAF0..0x288AB2A` [V capstone]: si el byte `[o1+0x14]` está puesto,
   `lock xadd [o1+0x10],-1`; si no, `dec`. Al llegar a 0, `(*[o1])(o1, 1)`.
4. Apunta en el libro: id del anfitrión ↔ `[copia+8]` (el id nuevo).

**Probe** (`funcnpc_twin=probe`): nada se llama ni se escribe. Salen estas líneas:
- `FuncnpcTwin [would create]`, con la copia, el estado de `mgr2`, si está asentado y, si no, el motivo;
- `[would delete]` en el espejo;
- `twins that would block the arm`.

**Abortar** (B1 pasa a probe y borra las gemelas a la salida de la siguiente Req):
- dos creaciones seguidas con resultado distinto de 0, o una excepción en `0x287C200` o `0x2879440`;
- el 2661 de la gemela no enlaza el **mismo NPC** que el del anfitrión (se compara en `take_frame` con el registro de
  `observer_seq_copy`);
- un 2810 de una gemela con el invitado en su tienda: el primero es WRN, el segundo aborta;
- gemelas que bloquean el armado durante más de 30 s;
- los criterios de U2 (`InBoth`, 3000 o Nak tras una ruta, el anfitrión rechazado);
- humano: el anfitrión no puede abrir su tienda, o ve abrirse algo suyo.

### 12.7 Prueba en un PC (anfitrión + `cdmp_peer`; el usuario hace viajar al anfitrión a Hernand)

1. **Preparación:** ini `cdmp.p8-u2-local-1.ini` con `funcnpc_twin=probe` (luego 1). Guion `u2_shop.troc` generado con
   `--wait-s 600`, para que el peer espere en el mostrador mientras el anfitrión viaja.
2. **Caso A primero:** con el anfitrión lejos, U-1 arma y la tienda propia del peer se prepara. Esto ya debe pasar.
3. **El anfitrión hace un viaje rápido a Hernand**, a ≤ 2 celdas del peer. Debe salir:
   - el vaciado de U-1 (2810 de la tienda propia al peer);
   - `ShopProbe [funcnpc] … owner 0xA0100001`;
   - en probe, `[would create] … settled yes`;
   - en 1, `[created] host 0x… → twin 0x…` y un 2661 al peer con el **mismo** `B01…` que el del anfitrión.
4. **La charla con la gemela.** Si el peer ya aprendió `SEQ` de la tienda propia, ese id es de una escena borrada:
   relanzar el peer o añadir al guion un segundo `learn_2661` tras el viaje (`--relearn`, pendiente). Las líneas
   esperadas son las del paso B de §11.4.
5. **Regresión:** el anfitrión habla con el mismo herrero y su tienda se abre en su pantalla. Debe salir
   `ShopProbe [server check] … for the host … field owner's … FOUND`.
6. **El anfitrión vuelve lejos:**
   - su escena se borra y la gemela se borra en espejo (2810 al peer);
   - `[ARMED]` sin `ListsNotEmpty`.
7. Ningún `[twin lost]`, ABORT ni ERR.

### 12.8 Shadow (después de 12.7)

- Juntos en la ciudad, el invitado usa la tienda (gemela): aviso desde M, R, comprar y vender, sin atasco.
- El anfitrión abre la misma tienda antes, durante (T5) y después de la compra del invitado.
- El tendero, mirado en los dos clientes (T3).
- El anfitrión se aleja con el invitado en la tienda:
  - el espejo espera;
  - la tienda se cierra limpia al acabar la espera;
  - el NPC puede desaparecer antes si se cierra la stage de horario del anfitrión [H].
- El invitado se aleja: las gemelas se borran, U-1 arma y aparecen sus tiendas propias.

### 12.9 Confianza

| Afirmación | Nivel |
|---|---|
| `0x287C200` no necesita estado, listas ni celda; funciona en estado 0/1 sin armar | [E fuerte] |
| El reinicio deja la tabla usable; una gemela viva en un reinicio queda huérfana en el cliente | [E] |
| Llamadores de `0x2879440`, incluido el temporizador 2054 con su `vf48` | [V] |
| El 2054 perdía los borrados de escenas de `mgr2` (corregido: sitio 15 de U2-a) | [E fuerte] |
| La descarga en estado 1 borra las gemelas; tras el vaciado, la cadena 2043 del usuario 2 termina | [V] / [E] |
| Las gemelas vivas bloquean el armado | [V] |
| El anfitrión sigue usando su tienda con la gemela viva si U2-b está encendido | [E] |
| Efectos del gráfico de la gemela sobre el NPC compartido | [H] |
| Las instancias no necesitan el tick 2043 de `mgr2` | [H, medio] |
