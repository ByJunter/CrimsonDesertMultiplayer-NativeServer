# P8 sesión 12: tenderos, lado servidor (quién prepara las escenas `cd_seq_funcnpc_*` y qué haría el servidor)

Build del juego 1.0.0.2976 (base `0x140000000`, direcciones en RVA). Fecha: 2026-10-10. Solo lectura: capstone sobre
`CrimsonDesert.exe` (`py -I`, con `C:\Emulador CD\build\research\cdpe.py`/`xref_index.pkl`), `.xtls` solo leído,
capturas de las sesiones 11 y 12. Sin juego, sin cambios de código, sin commit. Continúa `p8-s12-tenderos.md`.

Convenciones: **[V]** comprobado (instrucciones leídas o paquetes contados), **[E]** inferencia fundada, **[H]** sin
demostrar. `mgr(P)` = `ServerSequencerStageManager` del personaje P en el servidor, `[[[P+0x68]+0xA8]+0x18]`.
Anfitrión: usuario `0x90100000`, personaje `A0100001`. Invitado: usuario 2 `0x90100335`, personaje `A0100336`.

## 1. Resumen

1. **Quién decide [V].** Las escenas de tienda no las crea una lista por usuario ni una misión: las crea la
   **actualización periódica de stages** del gestor de secuenciadores de cada personaje, por proximidad (celdas de la
   rejilla alrededor del personaje), y **solo si ese personaje es el dueño del campo**:
   `UpdateStageOnceTimer 2043` → `.xtls 0x1082F900` → `0x287DBB0(mgr)` → `0x288B7A0` → **puerta de dueño
   `0x288B87A`** → `0x28818F0` (evaluación por celda) → `0x2882C70` → `0x2885BD0` (arranque de la stage, 2264) →
   `0x288A970` (elemento de la stage) → `0x287C200` (prepara el secuenciador) → `0x2A4DEF0` (2661, ToTarget al dueño
   del gestor).
2. **Por qué el usuario 2 no las recibe [V].** En `0x288B867–0x288B881` el gestor compara la clave de usuario de su
   personaje (`[[mgr.actor+0xA0]+0x60]`) con el dueño del campo (`[field+0x180]`) y, si no coinciden, **sale sin
   evaluar ninguna stage**. En el campo del anfitrión, `mgr(A0100336)` nunca pasa. Cuadra con los paquetes: el
   invitado recibió **0 × 2264, 0 × 2165, 0 × 2195** en toda la sesión 12 (el anfitrión, 36 354 / 755 / 350) y ningún
   2661 de tipo «elemento de stage». Su única escena propia (`0x19D`) es de otro tipo (§2.4).
3. **Qué haría el servidor con un 2671/2720 del usuario 2 a un tendero [V estático en `.rsrc`].** **No comprueba que
   la escena sea del que pide.** La validación del servidor es la misma función común que la del cliente
   (`ServerInteractionActorComponent::vf170 = 0x211FCB0` → `0x2126920`), pero en el servidor `field.vf48` es
   `ServerField::vf48 0x2B00CE0`, que siempre devuelve **el gestor del dueño del campo**: la etapa del anfitrión
   (`0x197`…) se encuentra y la entrada pasa. Respuestas 2663/2916/2525: ToTarget al que pidió (usuario 2). Lo que
   no se puede asegurar es la condición de datos de la entrada (`[entrada+8]`, `CompareSequencerTargetParameter`) [H].
4. **Pero la tienda no se abre por el servidor [V, sesión 11].** Tras el 2916 nativo, es **el cliente que reproduce la
   escena** quien manda `StageBranchStateReq 2436 (escena 0x196, rama 6)`; el servidor solo lo aplica y difunde
   2981/2524 ToNear. Con la escena del anfitrión, ese 2436 lo tendría que mandar el invitado sobre una escena ajena
   (hoy filtrado, R1), y si pasara, el cliente del anfitrión reproduciría la rama 6 **con el Kliff del anfitrión**.
5. **Arreglo:** levantar la puerta `0x288B87A` para el usuario 2 es lo «más nativo» pero **duplica el mundo** (las stages
   también generan sus NPC: el carnicero `B0100156` nace en el mismo paso que su 2661) → no. Propuesta en §5: primero
   la redirección del cliente (`0x2126A1F`) en sonda + sonda del servidor (se puede medir sin tocar nada); como arreglo
   de fondo, una **escena gemela** para el usuario 2 creada en el mismo punto donde el servidor prepara la del
   anfitrión (`0x288AAEF`), con el enrutado por id de escena en el servidor. Todo con interruptor y por fases.

## 2. Pregunta 1: qué crea las escenas de tienda en el servidor

### 2.1 Cadena desde el 2661 hacia atrás [V]

| Paso | Dirección | Qué hace |
|---|---|---|
| Escritor 2661 | `0x2A4DEF0` | Serializa y envía; su único llamador es `0x287D03C`, dentro de `0x287C200` |
| Preparación | `0x287C200(mgr, &res, playData, bindings, …)` | Valida (`0x287D1D0`), resuelve el actor dueño (`[playData+0]`, `0x2A82150`), su campo (`0x17FA6C0`), el gestor de misiones del dueño del campo (`0x2B00C70` en `0x287C72D`), crea la instancia en `mgr` (`0x1F6C710`), resuelve los enlaces y manda el 2661 (`0x287D03C`) |
| Llamadores de `0x287C200` | `0x288A970` (elementos de stage), `0x27EA160` (pools), `0x2ABD470` (acción de actor, usa `field.vf48` en `0x2ABD570`), `0x2ADDA60`, `0x2AF10F0`, `0x2B55160` (los tres, `field.vf48`), `0x2899820` (virtual), `.xtls 0x102CB550` (desde `0x287F7D0`, en la preparación `0x287EF60`) y `.xtls 0x102D1F30` | 9 sitios |
| Elemento de stage | `0x288A970(mgr, &res, stage, elemento)` | Si `[elem+8]` (recurso de secuenciador): construye `playData` con **dueño = `[[stage+0x48]+0x60]`** (`0x288A9F6`), tipo 1 (`[rbp-0x41]=1`), `[elem+0x1F0]`/`[elem+0x1F8]`, hash del nombre de fichero, y llama a `0x287C200(mgr, …, r9=[elem+0x200])` en `0x288AAEA`. Si `[elem+0x10]`: horario (carga `NPCScheduleData_File` con `0x1864140`, crea la stage de horario con `0x28078E0` → 2335). Si no, `0x27FAC50` |
| Arranque de stage | `0x2885BD0` ← `0x2882C70` ← `0x28818F0` | Emite 2264 (`0x2887035`) y recorre los elementos (`0x288760C` → `0x288A970`) |
| Evaluación por celda | `0x28818F0(mgr, field, actor, r9, …)` | Prepara el contexto con el gestor de misiones del **dueño del campo** (`0x2880BC0` → `vf40`), pasa por `0x29133B0`/`0x2911300`/`0x2913080` y decide qué stages arrancan (`0x2882C70` ×4) |
| Actualización | **`0x288B7A0(mgr, field, actor, …)`** | Requisitos: `mgr+0x6A0 >= 2`, `mgr+0x68A == 0`, `[[[actor+0x68]+0x1A0]+0x4F0]…+0x591 == 0` y **`[field+0x180] == [[actor+0xA0]+0x60]`** (§2.2). Calcula la celda de la rejilla a partir de la posición (`0x4F8220`, factor `[0x5D72CE4]`), la compara con la última (`mgr+0x680/+0x684`) y evalúa las celdas nuevas con `0x28818F0` (`0x288BBC6`) |
| Tick | `0x287DBB0(mgr, …)` | Campo del actor del gestor (`0x17FA6C0([mgr+0x18])`). Si `mgr+0x68A`: `0x287EF60` (preparación inicial). Si `mgr+0x6A0 == 1`: `0x287D4C0`. Si no: `0x288B7A0` (`0x287DE14`). Reprograma el temporizador `0x7FB` = 2043 para `[mgr+0x18]` (`0x287DF7D`) |
| Temporizador | `UpdateStageOnceTimer 2043`, manejador `0x2C33730` → `.xtls 0x1082F900` → `0x287DBB0` (`0x1082F993`) | Uno por gestor |

Conclusión: **la decisión es por gestor (por personaje), por proximidad a su posición y condicionada a las misiones
del dueño del campo**; no hay lista de «escenas de tienda por usuario». En un jugador, el único gestor que pasa la
puerta es el del jugador, así que todo el mundo (tiendas, horarios, facciones, misiones) cuelga de él.

### 2.2 La puerta que excluye al usuario 2 [V]

```
0x288B867  mov  rax, [r13+0xA0]          ; r13 = [mgr+0x18] = personaje del gestor; +0xA0 = su usuario
0x288B871  je   → eax = 0
0x288B873  mov  eax, [rax+0x60]          ; clave de usuario
0x288B87A  cmp  [r15+0x180], eax         ; r15 = campo; +0x180 = clave del dueño   (41 39 87 80 01 00 00)
0x288B881  jne  0x288BDEE                ; no es el dueño → sale sin evaluar nada (0F 85 67 05 00 00)
```

- Para `mgr(A0100336)` en el campo del anfitrión: `0x90100335 ≠ 0x90100000` → sale en cada tick. Ninguna stage del
  usuario 2 arranca en el mundo del anfitrión: ni tiendas, ni horarios, ni facción, ni misiones.
- Se suma a lo ya conocido de la entrada: `MoveFieldCompleteCore 0x2BC7670` salta la preparación `0x287EF60` del que no
  es dueño (`0x2BC8470`/`0x2BC8473`, `p8-observador-escenas.md` §2.5). La propia `0x287EF60` llama a `0x288B7A0`
  (`0x287F0C7`), así que aunque se forzara, la puerta volvería a cortar.

### 2.3 Paquetes que lo confirman [V]

| Opcode (sesión 12) | Anfitrión (`cdmp_packets_20261010-225154`) | Invitado (`cdmp_packets_20261010-215423`) |
|---|---:|---:|
| 2264 UpdateStageState | 36 354 (36 023 en la entrada al mundo, t≈55) | **0** |
| 2165 AddStageRandomRate | 755 | **0** |
| 2195 CreateFactionSpawnStage | 350 | **0** |
| 2661 PrepareSequencer (propios) | 34 (dueño `A0100001`) | 1 propio (`0x19D`) + copias del mod |
| 2724 UpdateStageList | 87 | 80 (es de misiones, ToTarget por usuario) |
| 2100 SyncStageCandidatePosition C>S | 246 | 211 (van al gestor del dueño por el embudo `vf48`) |

- El anfitrión recibe sus 36 k 2264 justo tras su `MoveFieldCompleteAck` del mundo (t=55,297). El invitado, ni en su
  campo inicial (tipo 3, t=14,95) ni en el del anfitrión (t=45,34).
- Las tiendas llegan **por zonas, a medida que el anfitrión se mueve**: 0x197–0x19B a t=56,69–56,70; banco/taberna
  (0x1A4/0x1A5) a t≈449; ropa/artesano (0x1BF/0x1C0) más tarde. Es la evaluación por celdas de §2.1.

### 2.4 Formato y tipo del 2661, y la escena `0x19D` del invitado [V]

`04 | actor | playData: dueño u32 | id u64 | tipo u8 | u8 [elem+0x1F0] | u64 [elem+0x1F8] | hash recurso u32 | … | enlaces`.

- Las de stage tienen **tipo 1**. Carnicería `0x197`: `01 00 5E 47 0F 00 …`, recurso `0x978345CA`; tendero `0x199`
  `[elem+0x1F8]` `0x05DA69FC`; herrero `0x19A` `0x41934310`; general `0x19B` `0x41934324`.
- La `0x19D` del invitado (t=185,57) es **tipo 6** (`06 03 5F 67 0F 00`, recurso `0x70AE382E`, un solo enlace
  `0x20000002 → A0100336`), igual que la `0x1C2` del anfitrión: sale de otro llamador (acción/objeto), no de la
  actualización de stages. Por eso «el servidor sí crea escenas para el usuario 2» no contradice §2.2 [E].
- El id de secuenciador es un **contador global del servidor** (el invitado recibió 0x19D entre la 0x19C y la 0x19E del
  anfitrión) → no hay colisiones de id entre gestores [E fuerte].

### 2.5 Las stages también crean sus NPC [V paquetes, E código]

En la captura del anfitrión, el carnicero `B0100156` aparece **por primera vez** en el 2005 de t=56,691, inmediatamente
antes del 2981 + 2661 de `0x197` (R47959–47961); el herrero `B010015A`, igual (R47972). Su controlador lo fija
`0x2791CD0` con `[actor+0x90] = [field+0x180]` (`0x2792A70`), es decir, el anfitrión. Consecuencia: si el gestor del
usuario 2 evaluara stages, **arrancaría sus propias copias y nacerían NPC duplicados** (dos carniceros, dos juegos de
NPC de horario, enemigos de facción por duplicado) [E fuerte].

## 3. Pregunta 2: qué haría el servidor con un 2671/2720 del usuario 2

### 3.1 2671 InteractionPreemptionStepReq [V `.rsrc`, salvo el envoltorio `.xtls`]

```
0x29E8FA0 (manejador 2671)
  r15 = personaje de la sesión (el que pide), r13 = NPC (0x2A82370(clave))
  0x29E927D → 0x2B36860 → jmp .xtls 0x1067B660 → 0x2B36EF0(compPeticionario, &res, NPC, clave, …)
0x2B36EF0
  0x2A829D0([pet+0x90])            usuario del que pide (fallo → 0x6CF7A08)
  pet.vf178()                       «es jugador» → dl
  0x2124060([[NPC+0x68]+0x98], dl)  busca la entrada por clave en el NPC (fallo → 0x6CF6CB0)
  comp.vf160 / vf168(NPC)           (0x211F930 / 0x211FA90)
  comp.vf170(NPC, entrada, 3, out)  = 0x211FCB0 (CanInteract común) → … → 0x2126920(entrada, pet)
  0x211E2A0, 0x211E1C0, 0x2125660   estado del componente del que pide
  0x29E7760                         2663 por ToTarget al que pide (finder 0x5B25438)
```

- `ServerInteractionActorComponent` (vtable `0x5B2AC00`) tiene en `+0x170` **`0x211FCB0`**, la misma función que el
  cliente alcanza por `0x9B94B0` [V]. El servidor ejecuta **la misma comprobación `0x2126920`**.
- Dentro de `0x2126920`, el gestor sale de `field.vf48` (`0x2126A1C`), con el campo del personaje del que pide
  (`0x17FA6C0` → `[[p+0x68]+0x1A0].vf140`). En el servidor el campo es un `ServerField` (vtable `0x5B29A60`) y su
  `+0x48` es **`0x2B00CE0`**: `FindUser([field+0x180])` → `[[[u+0xD0]+0x68]+0xA8]+0x18` = **gestor del anfitrión**
  [V]. Por tanto `0x1F6C190(gestorAnfitrión, 0x197)` **encuentra** la etapa.
- Lo que sigue (`0x2126AC0`–`0x2126B86`): `[etapa+0x160]==0`, `!0x1F6FC40`, personaje admitido por los datos de la etapa
  (`0x23505A0` con `[[pet+0x68]+0x20]+0x30`: el tipo de personaje; el invitado también es Kliff → pasa [E]) y
  `0x1AC10F0(vf40, …)` con las misiones **del anfitrión**. Después, la condición de datos `[entrada+8]`
  (`0x2239930` + `CompareSequencerTargetParameter`, `0x2126BB1`–`0x2126C2D`): árbol de condiciones con el que pide
  como objetivo. **Si ese árbol exige ser el dueño/el nodo enlazado, fallaría** [H]; en un jugador no se distingue.
- **No hay ninguna comparación «dueño de la escena == el que pide»** en `0x2B36EF0` ni en `0x2126920` [V]. La entrada
  dinámica del servidor existe porque el servidor también ejecuta `StageChart_Function_SetInteraction` (vtable
  `0x59851E0`, común: `0x2369360` → `0x2124370` → `.xtls 0xE402CC0`) cuando le llegan los eventos de gráfico del
  anfitrión [E fuerte: el 2671 nativo con clave `F4321` dinámica sale bien en la sesión 11].

**Predicción:** con la redirección del cliente, el 2671 del invitado → **2663** (no 3000), salvo [H] de la condición de
datos. Ver la sonda de §5.1.

### 3.2 2720 InteractionDoStepReq y lo que viene después [V `.rsrc` + lectura de `.xtls`]

- Manejador en `.xtls 0x104A8D90` → `0x2C44080` → `.xtls 0x10883D60` → **`0x2B36B30`**: comprueba el paso del
  componente del que pide (`+0x2A < 5`, `+0x28`, `+0x20`), busca el objetivo (`0x2A827B0`), valida con `comp.vf190`
  (`0x21258B0`), pasa a paso 4 y manda **2916 ToTarget al que pide** (`0x29E8130`). Nada mira la escena.
- La ejecución (temporizador `InteractionDoStepDoInteractionOnceTimer 2061`, `0x2C34CD0` → `0x2B37370` → `.xtls
  0x1067D300`): procesador de la interacción (`comp.vf1E8` → `vf30`), `comp.vf1F0`, `0x2125BA0` y 2525 ToTarget al que
  pide (`0x29E8630`). Tampoco toca la escena [V lectura; el contenido de los procesadores no se siguió].
- **La charla/tienda la mueve el cliente dueño de la escena [V, sesión 11, anfitrión]:**

```
415.010 C>S 2720  A0100001 → B0100153 000F4321
415.024 S>C 2916
415.036 C>S 2436 StageBranchStateReq  96 01 00 00 00 00 00 00 | 06 …   ← escena 0x196, rama 6, la pide el CLIENTE
415.036 C>S 2591 × 7 (eventos de gráfico de 0x194, 0x19C, 0x19F, …)
        S>C 2524 + 2981 (ToNear)  ← el servidor solo aplica y difunde
```

- Consecuencia para el usuario 2 con la escena del anfitrión:
  - El servidor le aceptaría 2671/2720 y le contestaría 2663/2916/2525 [E fuerte].
  - Pero **nadie cambia de rama**: en el invitado, X no reacciona sola, y si reaccionara, su 2436 sobre `0x196`
    (escena del anfitrión) es justo lo que el filtro R1 corta. Si se dejara pasar, el servidor lo aplicaría al gestor
    del anfitrión (el embudo `vf48`) y el 2981 de la rama 6 llegaría por ToNear **también al anfitrión**, cuyo
    cliente abriría el diálogo con **su** Kliff (el nodo `0x20000002` está enlazado a `A0100001`) [E].
  - **No ejecutaría la escena del anfitrión por su cuenta ni mandaría el diálogo al anfitrión solo por el 2720** [V
    para la parte `.rsrc`].

## 4. Otras comprobaciones útiles para el diseño

| Punto | Hallazgo |
|---|---|
| Ids de escena | Contador global del servidor: los de un gestor no chocan con los de otro [E fuerte] |
| Req de escena del usuario 2 (2436, 2591, 2704, 2105, 2276, 2173…) | Pasan por `ServerField::vf48 0x2B00CE0` → gestor **del anfitrión**. Las de una escena propia del usuario 2 no encuentran su id allí y se pierden (le pasó a la `0x19D`: 12 × 2591 y 1 × 2436) [E] |
| Evaluación de condiciones de stage | Usa el gestor de misiones del dueño del campo (`0x2880BC0`, `0x287C72D`): el progreso del mundo es el del anfitrión, coherente con el cooperativo |
| Controlador de los actores de stage | Siempre el dueño del campo (`0x2792A70`): el cliente del anfitrión simula a los tenderos |

## 5. Pregunta 3: arreglo

### 5.0 Opciones

| Opción | Qué es | Veredicto |
|---|---|---|
| **S1. Levantar la puerta** `0x288B87A` para el usuario 2 | Mid-hook en `0x288B87A` (`cmp [r15+0x180],eax`, 7 B): con el personaje del usuario 2 en el campo compartido, `eax := [r15+0x180]` | **No.** Su gestor arrancaría todas las stages cercanas del mundo: NPC duplicados, facciones dobles, 36 k 2264, misiones del anfitrión evaluadas dos veces por tick (`0x29133B0`/`0x2911300`/`0x2913080` sobre el gestor de misiones del anfitrión) [E fuerte]. Solo como experimento de 1 minuto para medir |
| **S2. Escena gemela** | Cuando el servidor prepara una `cd_seq_funcnpc_*` para el anfitrión, preparar la misma para el usuario 2 en su gestor, con dueño `A0100336` y los mismos enlaces | **La más nativa viable.** El invitado la recibe como 2661 propio → M → `SetInteraction` con ids de M → aviso, 2671/2720 y 2436 propios, tick nativo de M. Requiere 3 piezas en el servidor (§5.2) y tiene [H] |
| **C1. Redirección del cliente** `0x2126A1F` (la del colega) | El aviso busca la etapa en X cuando el id es de X | **Primer paso medible.** El servidor ya acepta (§3.1). No abre la tienda: falta la rama 6 (§3.2) |
| C2. C1 + rama local | C1 y, tras el 2916, que X reproduzca en el invitado la rama de tienda **solo en local** (su 2436 no sale; sus UIControl/InputBlock permitidos mientras dure la interacción del invitado) | Funciona sin tocar el servidor, pero es un espejo inventado: estado de X divergente del servidor y riesgo de que el anfitrión mueva la misma escena a la vez |

### 5.1 Paso 1: medir (sin cambiar el comportamiento)

1. **Anfitrión, sin Shadow (vale con `cdmp_peer`):** mid-hooks de solo lectura:
   - `0x288B87A`: por gestor, clave de usuario del personaje (`[[r13+0xA0]+0x60]`), `[r15+0x180]`, resultado de la
     comparación; contadores por usuario y cada 5 s. Esperado: el del usuario 2 cae siempre en «no dueño» [V a confirmar
     en vivo].
   - `0x288AAEF` (justo tras `call 0x287C200` en `0x288A970`; bytes `90 48 8B 4D 77`): nombre del recurso
     (`[[[rdi+8]]]`), dueño `[rbp-0x51]`, `[r14]` = resultado, `[rdi+0x200]` (enlaces). Confirma que todas las
     `cd_seq_funcnpc_*` salen de aquí.
2. **Con Shadow, junto a C1 en modo `probe`→`1`:** en el proceso del anfitrión, hilo del servidor:
   - resultado de `0x2B36EF0` (`[rsi]` a la salida, o el 2663/3000 que manda) para cada 2671 del usuario 2;
   - en `0x2126A1F` del **servidor** (mismo punto que C1; distinguir hilo por la bandera TLS `gs:[0x58]→[+0x1EC]` o por el
     hilo del servidor que ya conoce el mod): id de etapa y si `0x1F6C190` la encontró;
   - y si tras el 2916 llega algún 2436/2591 del invitado sobre `0x196`/`0x197` (debería quedar en el filtro).
   - Esperado: **2663 y 2916** para el invitado, sin rama. Si sale 3000, el culpable es la condición `[entrada+8]` [H].

### 5.2 Paso 2 (arreglo de fondo): escena gemela S2 [E/H]

Tres piezas, todas en `.rsrc`, con interruptor `[session2] funcnpc_twin = 0 | probe | 1`:

1. **Crear la gemela** — mid-hook en `0x288AAEF` (tras la preparación nativa del anfitrión).
   - Condiciones: `[r14] == 0` (preparó bien); el recurso empieza por `cd_seq_funcnpc_` (lista blanca); el usuario 2
     está en el mismo `ServerField` (viaje de observador vivo); `mgr(A0100336)` existe y `+0x6A0 >= 2`; aún no hay
     gemela para ese id.
   - Acción: copiar el `playData` de la pila (`[rbp-0x51]`, ~0x48 B) cambiando `+0` por la clave de `A0100336` y `+8`
     por 0, y llamar `0x287C200(mgr(A0100336), &res2, &copia, [rdi+0x200], &o1, &o2)` en el mismo hilo.
   - Efecto esperado: 2661 ToTarget a `A0100336` → invitado → **M de forma nativa**; el nodo `0x20000002` se resuelve
     al dueño (`A0100336`, como en la `0x19D`) y el del NPC al **mismo** `B0100156` del anfitrión, **si** los enlaces del
     elemento llevan la clave del NPC ya creada [H: hay que leer el bucle `0x287CDE0–0x287CFD2`; si los enlaces se
     resuelven contra la stage del gestor, en `mgr(A0100336)` no existe y fallaría].
   - Libro: id del anfitrión → id gemelo.
2. **Enrutar las Req del usuario 2 a su gestor** — gancho de entrada/salida en `ExecuteTroc 0x2774B20` que, para las
   Req de escena de la sesión 2 (2436, 2591, 2704, 2105, 2276, 2767) cuyo id está en el libro de gemelas, deje en una
   variable del hilo «gestor = `mgr(A0100336)`», y gancho de entrada en `ServerField::vf48 0x2B00CE0` que la devuelva
   mientras dure esa Req. Es la variante acotada del rango 3 de `p8-sistemas-juego.md` §11.7: solo ids gemelos, el resto
   sigue igual. El mismo criterio en `0x2126A1F` del servidor (entrada cuyo id es gemelo → `mgr(A0100336)`), que es el
   punto que usa el 2671 (§3.1).
3. **Borrar la gemela** cuando el servidor borre la del anfitrión (2810, `0x2879440`; firma por leer) y en la salida
   del invitado. Sin esto quedan escenas huérfanas.

Lo que **no** hace falta: tocar `0x288B87A`, ni la preparación `0x287EF60`, ni las copias 2661/2981 del anfitrión.

Riesgos de S2:

| Id | Riesgo | Nota |
|---|---|---|
| T1 | Enlaces del NPC no resolubles fuera del gestor del anfitrión | [H] Lo decide la lectura del bucle de enlaces (paso 3 de §5.3). El modo `probe` no puede contestarlo sin llamar: hará falta una llamada real en una prueba controlada |
| T2 | Llamar a `0x287C200` desde un gancho dentro de la preparación nativa | Reentrada en el mismo hilo del servidor; `0x287C200` toma sus propios cerrojos (`0x393DF0`) — comprobar que no hay uno del gestor del anfitrión tomado en `0x288AAEF` [H] |
| T3 | El mismo NPC enlazado a dos escenas | En el invitado, la de X y la gemela en M; el componente de secuenciador del NPC tiene un solo dueño (`comp+0x2D8`) [H]. En el anfitrión, su cliente nunca ve la gemela (ids desconocidos en su M) |
| T4 | Condición `[entrada+8]` de la gemela | Con dueño `A0100336` debería pasar por construcción (es su escena) [E] |
| T5 | Coherencia con la escena del anfitrión | Si los dos hablan con el mismo tendero a la vez, dos diálogos sobre el mismo NPC. Se puede vetar el aviso del segundo |
| T6 | Tienda en sí (compra/venta) | Los Req de tienda del usuario 2 los valida el estado de interacción **de su propio componente** (2720 aceptado) [H]; sin medir |

### 5.3 Recomendación y orden

1. **Sonda del servidor (§5.1-1)** en el PC del usuario con `cdmp_peer`: confirma la puerta y el origen de las
   `cd_seq_funcnpc_*`. No necesita Shadow.
2. **C1 en `probe` y después en `1`, con la sonda del servidor (§5.1-2):** necesita Shadow. Decide si el servidor
   acepta (2663) y si la condición de datos lo frena.
3. **Lectura estática corta** del bucle de enlaces de `0x287C200` (`0x287CDE0–0x287CFD2`) y de `0x287D1D0`, y la firma
   de `0x2879440`: decide T1/T2 antes de escribir S2.
4. **S2 en `probe` (solo libro y «crearía») → S2 en 1.** Si T1 sale mal, quedarse en C2 (espejo local) como plan B.
5. **No** usar S1 salvo como medida de un minuto.

## 6. Reproducción

- Capstone: `cdpe.PE()` + `xref.refs()` desde un script del scratchpad que añade `C:\Emulador CD\build\research` a
  `sys.path` (con `py -I` no se añade el directorio del script). Funciones por `.pdata`; vtables por RTTI
  (`rtti_cache.pkl`): `ServerField 0x5B29A60`, `ClientField 0x55BCEA0`, `ServerInteractionActorComponent 0x5B2AC00`,
  `StageChart_Function_SetInteraction 0x59851E0`.
- Recuentos: `cdmp_packets_*.log` de `backups\p8-sesion12-host` y `p8-sesion12-shadow` (columna de nombre), secuencia
  nativa de la tienda en `backups\p8-sesion11-host\cdmp_packets_20261010-201238.log` (líneas 323744–323900).
- `.xtls` solo se desensambló (manejadores 2720 `0x104A8D90`, ejecución `0x1067D300`, 2671 `0x1067B660` sin leer
  entero, temporizador 2043 `0x1082F900`). Ningún gancho propuesto cae en `.xtls`.

## 7. Lectura estática para S2: enlaces, reentrada y borrado (sesión 13, preparación)

Build 1.0.0.2976, capstone sobre el PE de solo lectura (`.xtls` solo leído). 2026-10-11. Responde al paso 3 de §5.3
antes de escribir S2. Convenciones de §1. La implementación de la medida está en `p8-impl-tenderos-c1.md`.

### 7.1 El bucle T1 `0x287CDE0–0x287CFD2` de `0x287C200` [V]

- No recorre los datos del elemento ni la stage: recorre una **copia local** (`[rbp-0x78]`, bucle `0x287CC30`) de las
  entradas de enlace **de la instancia recién creada** (`inst+0x120`, cuenta `inst+0x128`, 0x28 bytes cada una;
  `inst = [o1]` tras `0x1F6C710`/`0x1F70CF0`). Por entrada: `+0` puntero al nodo (clave `[nodo+0x30]`), `+8` u32 clave
  de actor, `+0xC` u8 tipo.
- Salta los tipos 0, 3, 6 y 7 (`0x287CDE3`–`0x287CDF9`). Para el resto resuelve la clave con **`0x2A82150`**, la
  búsqueda global de actores del servidor (la misma que resuelve el dueño de `playData` en `0x287C2A8`). Si no hay actor
  (`[rax+0x10] == 0`), la entrada **se omite sin error** (`0x287CE73`): el 2661 sale con un enlace menos.
- Si lo hay, escribe el registro de 12 bytes del 2661 (`0x287CF97`–`0x287CFA5`): clave de nodo, clave de actor, tipo,
  un indicador «el actor está en la lista `inst+0x158` (cuenta `inst+0x160`)» (`0x287CEB2`–`0x287CF82`) y
  `[[[actor+0x68]+0xB0]+0x198] & 0x1FFFFFFF != 0` (`0x287CF1D`).
- **Conclusión (el bucle) [V]:** no consulta ningún gestor; un actor que existe en el servidor se enlaza venga de donde
  venga la instancia.

### 7.2 Quién pone la clave del NPC en `inst+0x120` [V parcial, E]

- `0x287C200` → `0x2348230([elem+0x200], dueño)` (`0x287CA9B`): recorre entradas de 0x38 bytes en `[d+0x70]`
  (cuenta `[d+0x78]`) y evalúa sus árboles de condición con el **dueño** como objetivo (`vf8`/`vf10`): elige la
  variante. Su resultado va como 6.º argumento a `0x1F70CF0` (`0x287CAA9`).
- `0x1F70CF0` es un salto a `.xtls 0xDE21450`: llama a `0x1F74500`, a `mgr.vf[0xE0]`, a `vf[0xF0]` (si `[inst+0x70] !=
  -1`), a `vf[0xE8]` y a **`0x1F70E50(mgr, &res, campo, o1, variante, …)`**.
- `0x1F70E50`: por cada nodo del recurso (`[r+0x138]`, cuenta `[r+0x140]`) busca su clave (`[nodo+0x80]`) en la variante
  elegida y obtiene una **clave de personaje** (u16 en `+0x1A`, o sorteo `0x1F39E20` sobre la lista de `+0x1C`); después
  `0x1F731C0(mgr, campo, nodo, …)` → `0x1F73570`.
- `0x1F73570`: `switch` sobre el tipo de enlace `[info+0x60]` (11 casos, tabla `0x1F73DB4`): **solo los tipos 1 y 4**
  resuelven un actor (`0x1F735CA`); el resto va a `0x1F73D5E`, sin actor. Para 1/4: tabla global de personajes y
  **`0x20D5620(campo, &out, clave)`**, un mapa hash **del `ServerField`** (`+0xB8` cubetas, `+0xBC` cuenta, `+0xC8`,
  `+0xD0`) clave → u32 clave de actor, y luego el gestor global de actores (`vf28`). Con el actor, `0x2054000(inst, …)`
  añade la entrada de enlace.
- **Conclusión T1 [E fuerte]:** el NPC de una escena de tienda se encuentra **por el campo**, no por el gestor del
  anfitrión. Para una gemela con dueño `A0100336`, el campo es el de `0x17FA6C0(A0100336)`, el `ServerField` compartido
  mientras observa, así que se resolvería **el mismo** `B0100156`. Queda sin leer [H]: los demás casos del `switch` (por
  si algún nodo de tienda no es de tipo 1/4), `0x1F74500` y las `vf` E0/E8/F0 del `ServerSequencerStageManager`
  (vtable `0x5B17A58`).

### 7.3 Dónde sí interviene el gestor: T3 se confirma [V código, E efecto]

- En `0x1F73570`, con el actor ya encontrado (`0x1F73B17`–`0x1F73C4F`), se recorre la lista de enlaces vigentes del
  actor (entradas de 16 bytes {u64 id de secuenciador, u32 nodo}) y cada id se busca **en el gestor que prepara**
  (`0x1F6C190(mgr, …)` en `0x1F73B50`). Si está (con `[inst+0x28B]` y nodo de tipo 6), `0x1F55140` + `inst.vf108` quitan
  al actor de esa escena antigua.
- Para la gemela en `mgr(A0100336)`, la escena del anfitrión (en `mgr(A0100001)`) **no se encuentra** → no se le quita el
  NPC → el tendero queda enlazado a las dos (T3) [E]. Bien para que ninguno «robe» el NPC al otro; queda por ver qué hace
  el NPC con dos dueños de secuenciador (`comp+0x2D8`) [H].

### 7.4 T2: reentrada en `0x288AAEF` [V parcial, H]

- `0x287C200` toma el cerrojo de `[mgr+8]` solo alrededor de `0x287D1D0` (`0x287C25A` → se suelta en `0x287C283` /
  `0x287C294`). `0x288B7A0` toma y suelta el suyo antes de la puerta (`0x288B7F2` → `0x288B81B`).
- No se leyeron `0x28818F0`, `0x2882C70` ni `0x2885BD0` (la cadena entre la puerta y `0x288A970`) [H]. En el cliente los
  gestores comparten el objeto de cerrojo (`[[X+8]] = [[M+8]]`, `guest_seq_play`); si en el servidor también, y alguno de
  esos tres lo tiene tomado al llegar a `0x288AAEF`, una llamada a `0x287C200(mgr(A0100336), …)` lo pediría otra vez en
  el mismo hilo [H]. Antes de S2 en 1: leer esos tres o medirlo con una sonda en P.

### 7.5 Firma de `0x2879440` (borrado, 2810) [V]

- `0x2879440(mgr rcx, &res rdx, const u64* id r8, u8 r9b)`: toma `[mgr+8]`, busca `[r8]` con `0x1F6C190(mgr, …)` y, si no
  hay instancia, suelta y sale (`0x287954D`). El 2810 sale de `0x2879819`, dentro. Llamadores directos: `0x27FB895`
  (en `0x27FB370`), `0x287D8B5` (en `0x287D4C0`) y tres en `.xtls` (`0x102B80DB`, `0x102C9DD0`, `0x10835C68`).
- Para S2, pieza 3: borrar la gemela = `0x2879440(mgr(A0100336), &res, &id_gemelo, flag)` cuando entre en `0x2879440` la
  del anfitrión (gancho de entrada: `rcx` = su gestor, `[r8]` en el libro de gemelas). El significado de `r9b` está sin
  leer [H]: copiar el de la llamada del anfitrión.

### 7.6 Otros datos útiles para S2 [V]

- El id de secuenciador lo escribe la creación (`.xtls 0xDE16970`, la parte de `0x1F6C710`): si `[playData+8] &
  0x1FFFFFFF == 0`, `0x1F6B6E0(mgr, …)` da uno nuevo y lo guarda en `playData+8`. Por eso, tras `0x287C200`,
  `[rbp-0x49]` de `0x288A970` es el id (lo usa `shop_probe`), y una copia con `+8 = 0` recibiría un id propio.
- El resultado de `0x287C200` es `u32 [res]` (0 = bien: `0x287D04D mov [r12],esi` al final del camino bueno).
- `0x2B36EF0` solo tiene un llamador, `.xtls 0x1067B660` (el envoltorio del 2671): su resultado se ve a la vuelta, en
  `0x29E9282` (`mov ebx,[rbp-0x58]`), donde mira `shop_probe`. Con resultado ≠ 0 el manejador arma un paquete con
  `0x3F5` y el código (`0x29E928D`–`0x29E9319`); que sea el 3000 que vería el invitado está sin comprobar [H].
- Resultado de `0x2126920` para su llamador `0x211FCB0` (`0x2120470`): 2 = válida; 1 = válida (se anota en la salida);
  0 = rechazada (motivo `[0x6CF6D48]`) [V].

### 7.7 Veredicto para el riesgo principal de S2

- **T1 baja de «riesgo principal» a «probablemente bien» [E fuerte]:** los enlaces del NPC no dependen del gestor del
  anfitrión (§7.1, §7.2); se resuelven por el campo y la tabla global. Pendiente [H]: tipos de nodo distintos de 1/4 y las
  tres `vf` del gestor.
- **T2 pasa a ser el riesgo principal [H]:** reentrada del cerrojo compartido al llamar a `0x287C200` desde `0x288AAEF`.
- **T3 confirmado en el código [E]:** el NPC quedaría enlazado a la escena del anfitrión y a la gemela.
