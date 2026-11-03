# S46 — Tenderos: la cadena nativa del comercio en el servidor (solo lectura)

Build 1.0.0.2976 (PE `0x6AB28F00`, base `0x140000000`). Todas las direcciones son RVA. No se ha compilado, desplegado ni
abierto el juego. Ghidra headless (`cdmp-query.ps1`) y capstone sobre el exe. Los decompilados de `.xtls` salen legibles
porque Ghidra usa el código original. Son lectura; ningún gancho va ahí.

Marcas: **[V]** decompilado o desensamblado `.rsrc`, bytes, xref o log; **[V-x]** decompilado legible de `.xtls`;
**[E]** inferencia fuerte; **[H]** hipótesis por medir.

Scripts (en `scratchpad\s46\nativo\`):
- `re.py`: vtables que contienen un puntero, `fn`, `dis`, `slot` y `callers`;
- `errname.py`: nombre de un `eErrNo` a partir de su variable `.rdata`, leyendo el registro en `0x21FA720`/`0x2209C20`;
- `stscan.py`: escrituras `[reg+disp]` en un rango;
- `jt.py`: saltos que caen dentro de un rango de bytes, para validar puntos `create_mid`.

Desensamblados guardados: `f2b36ef0.asm`, `f211fcb0.asm`, `f21f2780.asm` y `f2369360.asm`.

Abreviaturas:
- `mgrH` / `mgr2`: `ServerSequencerStageManager` (vtable `0x5B17A58`) del anfitrión / del usuario 2;
- «comp. de interacción»: `ServerInteractionActorComponent` (vtable `0x5B2AC00`) = `[[actor+0x68]+0x98]`;
- «seqcomp»: `[[actor+0x68]+0xB0]`.

---

## 0. Resumen (lo que cambia el diseño)

1. **El servidor no compara en ningún punto «dueño de la escena == quien pide» al aceptar un 2671 o un 2720 [V].**
   - La entrada se elige por la clave de interacción: la **primera** del vector dinámico del NPC.
   - La escena se busca en `field.vf48`, que es el gestor del dueño del campo (`mgrH`).
   - Si el invitado pide sobre la entrada del anfitrión, el servidor lo acepta: 2663 y 2916 con **su** clave.
2. **Hallazgo nuevo y crítico: la compra y la venta sí comprueban la escena (`0x21F2780`, común a 2789, 2366 y a otras 5
   operaciones de tienda) [V].** Exigen que la etapa a la que está enlazado el NPC (`[[0x1F5C940(seqcomp)+0x10]+0x30]`)
   sea **la etapa «actual» del gestor del dueño del campo** (`[mgr+0xA0]`, o `[mgr+0x90]` si `+0xA0` es 0). Si no lo es,
   el error es `eErrNoInvalidStageInfo`.
   - La etapa «actual» la pone `0x2045BC0` al entrar en una rama con la marca `+0x75` (la de la tienda abierta [E]). Se
     borra en `0x1F6FF40` / `0x1F709A0`.
   - **Consecuencia:** con la ruta U2 actual, el invitado **no podría comprar ni lejos**. Su `vf48` en ese punto devuelve
     `mgrH`, cuya etapa actual no es la escena del invitado. Hace falta un sitio nuevo de ruta, **U2-c**: el retorno de
     `vf48` `0x21F28A1`.
3. **El dinero, el inventario, el stock y las condiciones son del que pide [V/E].**
   - Todo cuelga del `ServerStoreActorComponent` de la sesión que manda el paquete.
   - El personaje que recibe es el `focus actor` de ese usuario.
   - Ninguna Req de comercio mira el estado de interacción del que pide.
4. **«Ocupado» nativo [V-x]:** `.xtls 0xE401150` aplica la exclusividad según la `InteractionInfo` de la clave
   (`+0x13` y `+0xBC`). Si la clave no exime a los jugadores, el segundo recibe 3000 `eErrNoAlreadyPreemptedActor`. Para
   `0x000F4321` no se puede leer sin el juego: sonda P5.
5. **Entradas `SetInteraction` (respuestas a H1–H3) [V/V-x]:**
   - el único que borra entradas dinámicas es `0x2369867` (`call 0x21245B0` → `.xtls 0xE404A10`): un solo xref;
   - en el servidor, `vf[0x120]` / `vf[0x128]` de la etapa son `ret` (`0x359B50`);
   - todo pasa bajo el cerrojo del actor;
   - nadie guarda índices.
   - El arreglo R0, que intercambia la entrada propia con la primera antes del borrado nativo, es viable en un punto
     verificado.
6. **Una sola instancia nativa por campo [V]:** la evaluación de celdas (`0x288B7A0`) sale por la puerta `0x288B87A` si el
   usuario del personaje no es el dueño del campo. No existen escenas `funcnpc_*_extra`. El modelo nativo es «una
   instancia de la escena de tienda por campo, la del dueño».
7. **Mínimo propuesto:** (b), escena propia en `mgr2`, más U2-c, lejos primero. (a) no sirve: la compra exige que la
   escena del anfitrión sea la actual de `mgrH`, lo que obliga a mover la escena del anfitrión y su `2981` ToNear llega
   también al anfitrión. Cerca: (b) con R0, o con la entrada del anfitrión (c), condicionado a la sonda P1.

---

## 1. La cadena del comercio en el servidor

### 1.1 2671 `InteractionPreemptionStepReq` [V salvo marca]

```
0x29E8FA0 (manejador)  lee: u32 solicitante, u32 NPC, u32 clave (0x000F4321), u32, u8, u32
  → comp. de interacción del SOLICITANTE ([[sol+0x68]+0x98])
  → .xtls 0x1067B660 [V-x]: usuario del solicitante (fallo eErrNoNotHaveController); clave → u16 índice de InteractionInfo
       (tabla DAT_146D69AB8); busca el NPC (si no existe: 3000 con error 0)
     → 0x2B36EF0(compSol, &err, NPC, idx, …, &res{2,…})
     → si err ≠ 0: compSol.vf1B0 (reinicio) + 0x29E7D10 = 3000 ToTarget al solicitante con @12 = err
0x2B36EF0
  0x2A829D0([sol+0x90])                 usuario (eErrNoNotHaveController)
  0x2124060([[NPC+0x68]+0x98], esJugador, info, &entrada)   PRIMERA entrada con esa clave (eErrNoInvalidInteractionInfo)
  compSol.vf160 = 0x211F930             estado del solicitante: muerto, cargando, QTE, eErrNoInteractionDelayTime,
                                        eErrNoAlreadyPreemptingActor si [compSol+0xD8] ≠ 0
  compSol.vf168 = 0x211FA90(NPC)        estado del NPC: Dummy, Hidden, Thrown ([[NPC+0x68]+0x70]+0x40), gimmick,
                                        eErrNoSequencerNotInteractableState (0x1F5B0F0(seqcomp NPC), que da 1 si el NPC
                                        tiene cualquier entrada dinámica con escena)
  compSol.vf170 = 0x211FCB0(NPC, &entrada, 3, res)  ← validación común (la misma que el cliente)
       0x2124060 otra vez (eErrNoInvalidDynamicInteractionData), tipos de NPC, InteractionInfo+0x12 == 1 → el NPC
       debe «pertenecer» al solicitante (mascota/montura; no es el caso de un tendero), acciones, condición vf1E8,
       0x2126920(entrada, sol) (eErrNoInvalidStageChartInteractionCondition) y distancia/ángulo 0x2120CC0
       [r15] tiene que acabar en 2 (0x2B3708C: cmp byte [r15],2)
  .xtls 0xE3F84D0 [V-x]  RESERVA: 0xE401150(compNPC, sol, idx, p5) → si falla eErrNoAlreadyPreemptedActor;
                          si pasa: 0x2123090 apunta al solicitante en el NPC y [compSol+0xD8] = clave del NPC
  .xtls 0xE3F7140         guarda en compSol: +0x20 NPC, +0x28 idx, +0x2C, +0x30, +0x38, +0x40, +0x48
  .xtls 0xE4130D0         procesador de la interacción (vf1E8 → vf28), acción del NPC, compSol+0x3C/+0x24
  [compSol+0x2A] = 1 ; si es jugador, 0x1572250(…,8)
  0x29E7760 → 2663 ToTarget al solicitante: claves del solicitante y del NPC, InteractionInfo, p5, p6. SIN id de escena
```

**`0x2126920(entrada, solicitante)` (la comprobación de escena):**
1. `[entrada+0x20] & 0x1FFFFFFF == 0` → 2: entrada estática, se acepta.
2. Sin usuario (`[sol+0xA0] == 0`) → 0.
3. Campo del personaje del usuario → `vf48` (`0x2126A1C`; el retorno `0x2126A1F` es el punto S de ShopProbe/U2-b) =
   gestor del **dueño del campo**; `vf40` = sus misiones.
4. `cdmp_NodeHashTable_Find(gestor, [entrada+0x20])`: sin instancia, con `[inst+0x160] ≠ 0` o con `0x1F6FC40` → 0.
5. Si `[inst+0x70] ≠ -1` (misión de la etapa): tipo de personaje del solicitante (`[[[sol+0x68]+0x20]+0x30]`), lista de
   personajes y `0x1AC10F0(misiones, id)`.
6. Condición de datos `[entrada+8]` con `CompareSequencerTargetParameter`: 1 o 2.

**No hay comparación con el dueño de la etapa ni con el solicitante [V].**

### 1.2 2720 `InteractionDoStepReq` [V]

`0x2B36B30`:
- comprueba el paso del comp. del solicitante (`+0x2A < 5`, `+0x28 ≠ -1`, `+0x20 ≠ 0`, si no
  `DAT_146CF6CC8`), lo pone a 3, busca el NPC y su InteractionInfo;
- `vf190 = 0x21258B0`:
  - `0x2124AD0`;
  - en el NPC: `[compNPC+0x80] = 0`; si `[compNPC+0x98] ==` solicitante, `+0xA4 = 1`; si no, marca su fila en las listas
    `+0xA8` / `+0xB8`;
  - `0x2124A40`, `vf198`, `vf1A0` y `vf188(5)`; acción en el solicitante.
- Paso 4 y **2916** ToTarget al solicitante (`0x29E8130`): claves e InteractionInfo, **sin id de escena**.

**Ni el 2671 ni el 2720 ejecutan nada en la etapa de `[entrada+0x20]`** (ni evento, ni rama, ni `BlockInteraction`, ni
2981/2524) [V para `.rsrc` y `.xtls` legibles]. La lectura del 2671 es solo búsqueda y condición. La rama la pide el
cliente dueño de la escena con 2436 [V S11, `p8-s12-tenderos-servidor.md` §3.2].

### 1.3 Compra y venta [V]

| Op | Manejador | Proceso | Comprobación de tienda |
|---|---|---|---|
| 2789 `BuyStockFromStoreReq` | `0x2A06F50` | `0x2BFFB00(storeComp, …)` | `0x21F2780` en `0x2BFFDC5` |
| 2366 `SellItemToItemStoreReq` | `0x2A07AD0` → `0x2C44F40` | `0x2C04880` | `0x21F2780` en `0x2C04B00` |
| otras de tienda | — | `0x2C002D0`, `0x2C06340`, `0x2C080A0`, `0x2C0AA50`, `0x2B47250` | `0x21F2780` |

En el cliente, `0xA1D880`, `0xA1EA80`, `0xA1F550` y `0xA1FBC0` también la llaman: es la comprobación previa de la
interfaz.

Notas:
- El manejador solo acepta una sesión de jugador (`[[ses+0x88]+1] == 1` y `[ses+0x96]`); si no,
  `eErrNoSelfPlayerActor`.
- `storeComp = [[ses+0x68]+0xD0]` = **`ServerStoreActorComponent` del usuario que manda** (vtable `0x5B2ECA8` [E]).
- Formato del 2789: `u32 actor que recibe, u32 NPC de contacto, u32, u16 tienda, u32 índice de stock, u64 cantidad`.
- En `0x2BFFB00`:
  1. `focus actor` = `[[[storeComp+8]+0xA0]+0xD8]` (el personaje del usuario que pide; si no, `eErrNoInvalidFocusActorKey`).
  2. Tienda y stock estáticos (`eErrNoInvalidStoreInfo` / `InvalidStockInfo`) y condición del stock para el que pide
     (`0x25C9E90`, `0x21F70E0` → `eErrNoDontUseStoreCondition`).
  3. NPC de contacto (`eErrNoInvalidContactNpcKey`) y **`0x21F2780(storeComp, &err, NPC, &tienda)`**
     (`eErrNoInvalidStoreNpc` si no).
  4. Actor que recibe: si no es una sesión de jugador, distancia (`eErrNoInvalidInteractionDistance`).
  5. `0x2C00850` → `0x21F3360`: precio (`0x21F3E30`), **stock restante por usuario** (`0x21F6F20` lee la tabla hash del
     propio `storeComp`, `+0x28/+0x38/+0x40`), dinero (`0x25CA080` con el dueño del `storeComp`) e inventario
     (`0x212A150` del dueño).

**`0x21F2780(storeComp, &err, NPC, &tienda)`, la comprobación de escena que no se conocía:**
1. Campo del dueño del `storeComp` (`0x17FA6C0`); NPC → `NpcInfo+0x6C` → tienda `+0x14`.
2. `0x21F289E call [rax+0x48]` = **`field.vf48` → gestor del dueño del campo**. Retorno `0x21F28A1` (bytes
   `FF 50 48 | 48 8B D8 48 8D 50 08`).
3. `r12 = [mgr+0xA0]` si `& 0x1FFFFFFF ≠ 0`; si no, `[mgr+0x90]`. Si es 0 → `eErrNoInvalidStageInfo` (`0x21F2917`).
4. Distancia entre el `focus actor` del usuario y el NPC ≤ `DAT_146CF8AB8`.
5. `0x1F5C940(seqcomp NPC)` → `[[r+0x10]+0x30]`: la etapa a la que está enlazado el NPC. La función devuelve el registro
   `+0x78` (si `+0x80`), si no `+0x278`, si no `+0x238`.
6. **`0x21F2C45 cmp [rsp+0x28], rbx`**: si son distintas → `eErrNoInvalidStageInfo`.
7. `0x21F2F30`: nivel de búsqueda (`eErrNoDontUseContentsWantedLevel`).

Qué son `[mgr+0x90/+0x98/+0xA0]` [V]:
- `CommonSequencerStageManager::vf1` (`0x1F6B860`) los pone a cero.
- `0x2045BC0` (transición de rama, desde el procesador `ServerSequencerStage_StageChartProcessor::vf8 = 0x2042A60`): si
  la rama nueva tiene `[[datos+0x88]+0x75]`, pone `[mgr+0xA0] = id`, `[mgr+0x90] = 0` y llama a `vf108`.
- `0x1F6FF40` lo deshace: `+0xA0 = 0` y `+0x90 = +0x98`.
- `0x1F709A0` (desde el borrado `0x2879440`) limpia si la etapa borrada era la actual.
- Es «la etapa con foco del jugador dueño del gestor».

Consecuencias:
- **El 2789/2366 del invitado se valida contra la etapa actual de `mgrH`.** Lejos (escena propia en `mgr2`), `mgrH`
  tiene otra etapa actual (o ninguna) → `eErrNoInvalidStageInfo` [E fuerte; no hay ninguna compra del invitado medida].
- **U2-c (nuevo):** en el `vf48_detour` de `user2_funnels`, el retorno `0x21F28A1`, en un Troc de comercio del usuario 2
  (2789, 2366, 2458, 2308, 2940…: los que llegan a `0x21F2780`), → `mgr2`.
  - Condiciones: `mgr2` tiene una etapa actual (`[mgr2+0xA0]`) que es una `funcnpc` suya, o, mejor, la etapa enlazada al
    NPC está en `mgr2` y no en `mgrH`.
  - En el detour de vf48 no se ve el NPC (está en `rsi` del llamador). Alternativa precisa: un `create_mid` en
    `0x21F28A1`, con 7 B (`mov rbx,rax`; `lea rdx,[rax+8]`), dos instrucciones enteras y ningún salto dentro [V `jt.py`].
    Ahí `rsi` = NPC, `r13` = `storeComp` y `rax` = gestor, y se puede hacer `rax := mgr2`.
- **Respuesta a «¿quién paga y quién recibe?»:**
  - el usuario de la **sesión que manda la Req**: su `ServerStoreActorComponent`, su dinero, su inventario y su stock
    restante;
  - recibe el actor `u32 @0`, que normalmente es su propio personaje [V/E].
  - **Nada es del dueño de la escena.**

### 1.4 La pregunta crítica: ¿el invitado sobre la entrada de interacción del anfitrión?

| Paso | Resultado | Marca |
|---|---|---|
| 2671 sobre la entrada del anfitrión | `0x2126920` busca en `mgrH` → FOUND → aceptado; 2663 al invitado (salvo «ocupado», §3) | [V] |
| 2720 | aceptado, 2916 al invitado | [V] |
| Abrir la tienda | la abre la rama del cliente dueño de la escena (2436); X en el invitado no emite y R1 la cortaría → atasco (S14) | [V captura] |
| 2789 / 2366 | solo pasan si la etapa actual de `mgrH` es la escena del anfitrión y el NPC está enlazado a ella. Eso exige que esa escena esté en su rama de foco: o el anfitrión está en la tienda, o el invitado mueve la escena del anfitrión, y entonces sus 2981/2524 ToNear llegan al anfitrión y cambian su etapa actual | [V código] / [E efecto] |
| ¿3000 por no ser el dueño? | **No existe esa comprobación.** Los 3000 posibles son de ocupado, condición, distancia o escena no encontrada | [V] |

**Conclusión:** el servidor «abre» la interacción con el dinero e inventario del invitado. La tienda, sin embargo, solo
funciona si la escena enlazada al NPC es la actual del gestor que da `vf48`. Con la escena del anfitrión, eso solo
ocurre tocando la escena del anfitrión. (a) se descarta.

---

## 2. El registro de interacciones `SetInteraction` (H1, H2, H3, H8, H14)

### 2.1 Quién lo escribe y cómo [V/V-x]

- `StageChart_Function_SetInteraction` (vtable `0x59851E0`):
  - `vf40 = 0x235E0F0` (`mov r8b,1; jmp 0x2369360`): **alta al entrar** en la función;
  - `vf48 = 0x235E100` (`xor r8d,r8d; jmp 0x2369360`): **baja al salir**.
  - Son los únicos xref de `0x2369360` [V].
- `0x2369360(func rcx, ctx rdx, alta r8b)`:
  - `[ctx]` = etapa (`[etapa+0x30]` = id, `[etapa+0x58]` = procesador);
  - `ctx[9]` = parámetro de la función;
  - actores destino con `0x2049B90` (`thunk 0xE0314C0`).
- Datos de la función:

  | Campo | Qué es |
  |---|---|
  | `[func+0x20]` | selector de destino |
  | `[func+0x30]` | cadena de condición (`checkNone()` por defecto) |
  | `[func+0x38]` | u16 índice de `InteractionInfo` |
  | `[func+0x3B]` | marca de registro en la etapa |
  | `[func+0x3C]` | u8 que va a la entrada `+0x2C` |

- **`[func+0x3B] ≠ 0` → `stage->vf[0x120](etapa, procdata+0x38)` en el alta y `vf[0x128]` en la baja (F2).**
  - **En el servidor, `ServerSequencerStage` (vtable `0x5B1FBE0`) tiene `vf120 = vf128 = 0x359B50 = ret`** [V]: no hay
    registro por etapa.
  - En el cliente, `ClientSequencerStage` (vtable `0x558BD80`) tiene `vf120 = 0x605A40` y `vf128 = 0x606030`: registro
    del cliente (interfaz) [V por puntero; no seguido].
- **Alta:**
  - `0x236969C call 0x2124370` → `jmp .xtls 0xE402CC0(compNPC, &cond, u16 idx, u8 [func+0x3C], u64 id de etapa, u32,
    u8 [func+0x3B])`;
  - **añade al final** (`0xE422F80` = push_back) y escribe la última entrada;
  - compila la condición en `+8`; si es inválida, la quita;
  - llama a `0xE411950`.
- **Baja:**
  - `0x2369859 lea rcx,[r14+0x38]` → `0x236985D call 0x3888B0` (= `jmp 0x87E65F0`) → `0x2369862 mov edx,[rax]` (clave
    u32) → `0x2369865 mov rcx,r12` (compNPC) → **`0x2369867 call 0x21245B0`** (= `jmp .xtls 0xE404A10`);
  - **borra la primera entrada con esa clave y mueve la última al hueco (F1)**; después, `0xE411950`.
- **`0xE411950` [V-x]:**
  - suma uno al contador de versión `comp+0x124` (aritmética ofuscada);
  - recalcula `comp+0x128` (alguna entrada con `InteractionInfo+0x12 == 2`) y `comp+0x129` (alguna con `+0x58`);
  - no guarda índices.
- **`0x87E65F0` (thunk `0x3888B0`) [E]:**
  - `u16 idx → InteractionInfo` estática;
  - `[info+0]` = clave u32 (`0x000F4321`);
  - se usan: `+0x12` (tipo; 1 = de propiedad), `+0x13` (modo de reserva), `+0x58`, `+0x5A`, `+0x64`, `+0x80`, `+0xBC`
    (jugadores exentos de exclusividad) y `+0xC0/+0xC1/+0xC2`.
  - Con `+0x10` (u16 de la entrada) da su clave; con `+0x38` (u16 de la función) da la clave que se borra.

### 2.2 La entrada de 0x30 B [V-x]

| Off | Tipo | Contenido |
|---|---|---|
| +0x00 | objeto cadena | texto de la condición |
| +0x08 | puntero (con cuenta de referencias) | condición compilada (la usa `0x2126920`, `[entrada+8]`) |
| +0x10 | u16 | índice de `InteractionInfo` (la clave sale de `[info]`) |
| +0x14 | u32 | -1 en las dinámicas; en las estáticas, `info+0x80` o -1 |
| +0x18 | objeto cadena | vacío |
| +0x20 | u64 | **id de etapa** (`& 0x1FFFFFFF`; 0 = estática) |
| +0x28 | u32 | de una tabla indexada por los datos del procesador |
| +0x2C | u8 | `[func+0x3C]` |
| +0x2D | u8 | `[func+0x3B]` |

Vector: `comp+0x88` (puntero), `comp+0x90` (cuenta), `comp+0x94` (capacidad).

### 2.3 Quién lo lee [V]

- `0x2124060(comp, esJugador, info, &copia)`:
  - toma el cerrojo de lectura `0x393DF0([actor+8])`;
  - `0x2123710` arma una **copia**. **Si el que pide es jugador y hay al menos una entrada dinámica, solo cuentan las
    dinámicas, en el orden del vector**; si no, las estáticas del personaje o de su nodo de IA;
  - devuelve una copia de la **primera** con `[info] == clave`.
- Lo usan el 2671 (`0x2B36EF0`, dos veces) y `0x211FCB0` (común: también el aviso del cliente, que es el mismo código
  [E]).
- `0x1F5B0F0` solo pregunta si existe alguna con escena.

### 2.4 H1: el camino exacto del borrado al cerrar una etapa

- **Hecho [V]:** el único llamador de `.xtls 0xE404A10` es `0x2369867` (por el thunk `0x21245B0`). Las etapas del servidor
  no tienen registro propio (`vf120/128` = `ret`).
- **Por tanto, toda entrada dinámica se quita con la salida de su función `SetInteraction`.** También se va si muere el
  componente del NPC.
- **Camino [V en los extremos, E en el medio]:**
  - `0x2879440(mgr, campo, &id, motivo)`:
    - busca la instancia;
    - `0x1F709A0` si era la etapa actual;
    - si `.xtls 0xE0623D0(inst, motivo)`: `0x20532B0(inst, motivo)` = parada de la etapa (procesador, desenlace de los
      actores con `0x1F55140(seqcomp, &etapa, 0xF)`), `.xtls 0xDE1B770` (fuera del gestor) y el 2810;
  - la parada termina el gráfico → `func.vf48` de cada función viva → `0x235E100` → `0x2369360(r8b=0)` → `0x2369867`.
  - Vale para todos los llamadores de `0x2879440`: `0x287D8B5` (descarga, motivo 0xE), `vf6`/2276, `.xtls 0x10835C68`
    (2054), `0x27FB895` (clase 2).
  - El tramo «parada → salida de la función» no está seguido instrucción a instrucción.
  - La reconstrucción de p2b (el borrado de la gemela se lleva la entrada del anfitrión) lo confirma de forma empírica
    [V datos].
- **Sonda P2 (más abajo)** en `0x2369867`: registra cada baja de servidor con el id de etapa (`[[rsi]+0x30]`).

### 2.5 H3: cerrojo y movilidad

- Alta y baja dentro de `0x2369360` bajo `cdmp_Actor_Lock([NPC+8])`:
  - alta: cerrojo en `[rbp-0x48]`, tomado en `0x23695C2`;
  - baja: cerrojo en `[rbp-0x38]`, tomado en `0x2369853`.
- La lectura (`0x2124060`, `0xE411950` → `0x2123710`) usa `0x393DF0` sobre el mismo `[NPC+8]` [V].
- `0xE402CC0` y `0xE404A10` no toman cerrojo propio: siempre se llaman con él [V].
- **Las entradas se mueven ya de forma nativa** (la última pasa al hueco) y todos los lectores trabajan sobre copias
  (`0x2126490` / `0x2126290`). La dirección `entry 0x1A1EFF868` de ShopProbe era una copia. Nadie guarda índices ni
  punteros [V/E].
- **R0 es viable:** en la baja, antes del `call`, intercambiar los 0x30 B de la entrada (clave = `edx`, `+0x20` = id de
  esta etapa) con la primera entrada de esa clave. El cerrojo ya está tomado. Los punteros con cuenta de referencias se
  mueven con sus bytes; es un intercambio de valores, como el que hace el propio juego.
  - **Punto:** `create_mid` en `0x2369867` (`E8 44 AD DB FF`, 5 B, una instrucción). El único salto a ese bloque es
    `0x23695AE je 0x2369845`: ninguno cae en `0x2369868..0x236986B` [V `jt.py`].
  - Registros: `rcx = r12 =` compNPC, `edx =` clave, `rsi =` ctx (`[[rsi]+0x30]` = id de la etapa), `r14 =` función,
    `r13b = 0` (baja).
  - Alternativa: `0x2369862` (`8B 10 | 49 8B CC`, 5 B), antes de cargar `edx`.
  - **Ojo, regla del proyecto:** R0 escribe en memoria del juego (el vector del NPC). Primero como sonda (P2), con copia de
    las partidas.
- Alta, por si hace falta: `0x236969C call 0x2124370` (5 B; sin saltos dentro). Registros: `rcx` = compNPC, `r8w` = idx,
  `[rsp+0x20]` = id de etapa, `r9b` = `[func+0x3C]`.

### 2.6 H8: `BlockInteraction`

`StageChart_Function_BlockInteraction` (vtable `0x5985240`):
- `vf40 = 0x235E1B0` → `0x23698F0` → `.xtls 0xEFE0C60` [V-x];
- para cada actor destino: `[seqcomp+0x180] = id de etapa` (bloquear) o `0` (desbloquear), bajo el cerrojo;
- **una sola ranura, gana el último que escribe, y el desbloqueo no comprueba quién bloqueó.** Mismo riesgo que F1:
  una escena B que desbloquea quita el bloqueo de la A.
- Quién lee `+0x180`: no se ha seguido [H]. En la S12 no hubo ninguna llamada de `BlockInteraction` en las tiendas.

### 2.7 H14: la etapa del servidor y su procesador

- Etapa: `+0x30` id, `+0x48` → `+0x60` dueño (lo usa `0x288A970`), `+0x58` procesador, `+0x160` (si ≠ 0 se rechaza en
  `0x2126920`), `+0x70` misión, `+0x28B` «es la actual», `[+0x48]` datos de rama (`+0x88` → `+0x75` marca de foco).
- Gestor de la etapa: no hay un puntero directo comprobado [H]; `0x5DB630` usa `ctx+8` = gestor.
- **Hay un único procesador de servidor:** `ServerSequencerStage_StageChartProcessor` `0x5B1FE10` (RTTI; las otras
  clases son `Client…` `0x558C080` y `Common…` `0x590DF78`) [V].

### 2.8 ¿Coexisten de forma nativa dos escenas sobre el mismo NPC?

- **Estructuralmente sí:** el seqcomp tiene tablas hash de etapas enlazadas (`+0x18…` y `+0x2B8…`; `0x1F55140`
  desenlaza por id), y el vector de entradas admite varias con la misma clave.
- **Pero el juego no las separa:** la elección es por la primera clave, el borrado por la primera clave, `BlockInteraction`
  tiene una sola ranura y la etapa «actual» del NPC (`0x1F5C940`) es una sola.
- El juego nunca lo hace: una instancia por campo (§4). R0 corrige el borrado. La elección y la etapa actual del NPC
  siguen siendo únicas.

---

## 3. «Ocupado»: los dos a la vez sobre el mismo tendero [V-x salvo marca]

- **NPC: `.xtls 0xE401150(compNPC, solicitante, idx, p5)`:**
  1. Si el solicitante es jugador, `info+0xBC ≠ 0` e `info+0x13 ≠ 3` → **se acepta siempre** (sin exclusividad para
     jugadores).
  2. `info+0x13 == 0` → se acepta solo si `0x21249E0(compNPC) == 0` (ranura única `+0x98` = solicitante, `+0x9C` =
     clave).
  3. `== 1` → exclusividad **por clave**: rechazo si `+0x98/+0x9C`, `+0xD0`, `+0xE0/+0xE4` o `+0x118` están ocupados, o si
     la clave ya está en las listas `+0xA8` o `+0xF0`.
  4. `== 2` → por (clave, `p5`) en las listas `+0xB8` / `+0x100`.
  - Si falla → **3000 `eErrNoAlreadyPreemptedActor`** (`DAT_146CF7808`).
  - Si pasa, `0x2123090` apunta al solicitante en la ranura o lista de su modo.
- **Solicitante:** `[compSol+0xD8]` = el NPC que está reservando. Un segundo intento sin terminar →
  `eErrNoAlreadyPreemptingActor`.
- **Al hacer el 2720** (`0x21258B0`): `compNPC+0x80 = 0` y la fila del solicitante se marca como hecha (`+0xA4` /
  `+0xC`). Sale de la ranura en el reinicio de la interacción (`vf1B0` / 2743).
- **`0x000F4321`:** la `InteractionInfo` es dato del juego y no está en el exe. **Por medir con P5.**
  - Si `+0xBC` está puesta, los dos pueden hablar a la vez.
  - Si no, el que llega segundo recibe un 3000 mientras el primero no termina. Es el comportamiento nativo y es aceptable;
    no hay que inventar un «ocupado».

---

## 4. Creación de la escena de tienda por jugador [V]

- **Puerta de dueño `0x288B87A`:** `cmp [r15+0x180], eax ; jne 0x288BDEE`, con `eax = [[rbp+0xA0]+0x60]` (la clave del
  usuario que evalúa).
  - Si no es el dueño del campo, `0x288B7A0` **salta toda la evaluación**: ni `0x28818F0` ni `0x287DFE0`.
  - Antes sale también con el estado del gestor `+0x6A0 < 2` o `+0x68A ≠ 0`.
- **No existe camino nativo para una segunda instancia** de `funcnpc` del segundo usuario en el campo del dueño:
  - en `funcnpc.json` (156 recursos) no hay variantes `_extra`;
  - la única forma es que el mod abra la puerta (U-a con U-1 armado).
- **El diseño nativo es una instancia por campo, la del dueño.** Lo que es por jugador es el modelo de datos: dueño de la
  etapa, 2661 ToTarget, tienda, stock y dinero por usuario (§1.3).
- **H9: re-creación tras un borrado.** `0x288B7A0` mete **siempre** la celda actual (rejilla de 64 m, `×0.015625`) en la
  lista y llama a `0x28818F0` para ella en cada tick armado [V]. Una etapa borrada de la celda actual se vuelve a evaluar
  en el siguiente 2043; que se cree otra vez depende de las condiciones y memorias de `0x28818F0` [E/H].
  - Sonda: tras un borrado G3, contar los `[prepared]` del mismo hash.
- **H15:** confirmado en p2b (`host.log:34206-34207`, `:34490-34491`, `reschedule 1` cada ~104 ms en estado 1 con
  `user2_timer_route=1`) [V, `tenderos.md` §2.3]. Ninguna escena vive en `mgr2` desarmado.

## 5. H6 y H7: dueño del actor del tendero

- **El tendero lo genera la escena `funcnpc`** (`comp+0x2E0` = id de la escena, `comp+0x2D8` = dueño):
  - p2b: `B01002EC` nace con `0x143` de `mgr2`, dueño `0xA0100066` (`host.log:12557`);
  - p1b: `0x293` del anfitrión [V].
  - Es único en el campo (registro `0x2817130`: no se genera dos veces).
- **Si A lo generó y B lo enlaza, y se borra A** [V, p2b]:
  - `0x2A2` (anfitrión, 15:33:01) enlazó el mismo `B01002EC` que había generado `0x143`;
  - `0x143` se borró en el segundo vaciado (~15:33:02.9);
  - `B01002EC` **siguió vivo** hasta la baja final (`host.log:38748`, 15:34:10, motivo 25), y el anfitrión le pidió 2671
    después.
  - El borrado de A solo lo desenlaza (`0x20532B0` → `0x1F55140`); no lo despawnea mientras exista.
- **H7b:** tipo 4, controlador = el anfitrión (`0x90100000`) en p2b (`host.log:12344`) [V]. U-e (`0x2792A70`) ya lo pone
  así de forma nativa.
  - Que lo toquen las familias B: no está estudiado aquí [H].
- **H7** (inventario de funciones de `smithy` / `butcher`): **no se ha resuelto.**
  - Los gráficos están en los datos (`.pab*`), no en el exe.
  - Datos parciales [V]: en la S11 solo hay 4 × 2170; en la S13 la herrería emite 2436/2173/2767 y ningún 2591.
  - Hace falta una sonda: registrar los tipos de función que ejecuta el procesador `0x5B1FE10` para esas etapas.

## 6. Otras preguntas

| # | Respuesta | Marca |
|---|---|---|
| H4b | 2663 (`0x29E7760`) y 2916 (`0x29E8130`) llevan solicitante, NPC, `InteractionInfo` y parámetros, **sin id de escena**. El cliente decide con sus propias entradas (`0x2124060` local) | [V] args |
| H5 | Stock y reposición: hash por tienda y stock dentro del `ServerStoreActorComponent` **del usuario** (`0x21F6F20`, `+0x28/+0x38/+0x40`). Precio con `0x21F3E30(storeComp)`. Dinero e inventario del dueño del `storeComp`. 2789/2366 **no** miran el estado de interacción, pero **sí** la escena actual del gestor de `vf48` (§1.3). 2458 no se ha seguido | [V/E]; 2458 [H] |
| H10 | `[elem+0x200]` es el descriptor estático de enlaces. La clave del NPC sale de buscar la clave de personaje en el mapa del campo `0x20D5620` (`ServerField+0xB8`), una búsqueda hash de solo lectura. Se podría replicar con SEH sin llamar al juego; sin escribir y medir | [E] |
| H11 | 2810 con la interfaz de tienda abierta en el cliente: no estudiado | [H] |
| H12 | Sí: 169 copias sin rechazo, 18 µs en p1b (`tenderos.md` §2.1) | [V datos] |
| H13 | El aviso del cliente usa el mismo `0x211FCB0`/`0x2124060` común → **primera entrada** con la clave. Que el resumen de `GuestXInter` cuente «52 entradas vistas» sugiere que antes se enumeran todas | [E] / [H] |

---

## 7. ¿Qué es lo mínimo para que el invitado comercie con lo suyo?

| Vía | Servidor | Cliente del invitado | Compra/venta | Veredicto |
|---|---|---|---|---|
| **(a)** escena y entrada del anfitrión | 2671/2720 aceptados [V] | X no emite 2436 → atasco (S14) | Exige que la escena del anfitrión sea la actual de `mgrH` → mover la escena del anfitrión (2981 ToNear al anfitrión, su etapa actual cambia) | **No** |
| **(b)** escena propia en `mgr2` | U2-a / U2-b (hechos) + **U2-c nuevo** (`0x21F28A1` → `mgr2`) | Escena en M, 2436 propio → U2-a → `mgr2` → `[mgr2+0xA0]` = su escena [E] | Pasa si la etapa actual del NPC es la del invitado: lejos sí (única escena) [E]; cerca, depende de `0x1F5C940` con dos escenas [H, P1] | **Sí, lejos primero** |
| **(c)** cerca: entrada del anfitrión + escena propia en M/`mgr2` (G1 de `tenderos.md`) | 2671 sobre la entrada de `mgrH`: aceptado | Su M mueve su escena (U2-a) | Igual que (b) cerca: hace falta que el NPC tenga como actual la escena del invitado mientras compra | Condicionada a P1 |

**Orden mínimo:**
1. Lejos: U-f con la lista de comercio, T2 (orden 2005 → 2661 en el anfitrión), U2-a, U2-b y **U2-c** (todo en `probe`
   primero).
2. P1 y P2 en `probe` para medir la etapa actual del gestor y la del NPC en cada 2789/2366, y las bajas.
3. Cerca: decidir entre R0 (dos escenas, borrado correcto) con U2-c, o (c), según lo que diga P1 sobre la etapa actual del
   NPC con dos escenas enlazadas.

## 8. Puntos verificados y sondas de solo lectura

Todos son `.rsrc` con `.pdata`, instrucciones enteras y ningún salto dentro de los bytes reubicados (`jt.py`).

| Id | Punto | Bytes | Registros útiles | Para qué |
|---|---|---|---|---|
| **P1** | `0x21F2C45` (en `0x21F2780`) | `48 39 5C 24 28` (`cmp [rsp+0x28],rbx`) + `0F 84` | `rbx` = etapa enlazada al NPC; `[rsp+0x28]` = etapa actual del gestor; `[rsp+0x38]` = gestor (vtable, `[+0x18]` personaje); `rsi` = NPC; `r13` = `storeComp` (`[+8]` → `[+0xA0]` usuario) | Cada comprobación de tienda (2789, 2366…, y la del cliente si se pone allí): quién, qué gestor, actual frente a NPC, igual o no |
| P1b | `0x21F2917` | `41 F7 C4 FF FF FF 1F` | `r12` = actual (0 → error); `rbx` = gestor | El caso «gestor sin etapa actual» |
| **U2-c** | `0x21F28A1` (retorno de `vf48`) | `48 8B D8 \| 48 8D 50 08` (7 B) | `rax` = gestor; `rsi` = NPC; `r13` = `storeComp` | Probe: «would route»; 1: `rax := mgr2` |
| **P2** | `0x2369867` (baja `SetInteraction`) | `E8 44 AD DB FF` | `rcx` = compNPC, `edx` = clave, `[[rsi]+0x30]` = id de etapa, `r14` = función | Lista antes de cada baja (n, clave e id por entrada) y posición de la primera coincidencia frente a la propia: mide F1 y es el punto de R0 |
| P2a | `0x236969C` (alta) | `E8 xx xx xx xx` | `rcx` = compNPC, `r8w` = idx, `[rsp+0x20]` = id de etapa | Altas con lista |
| P3 | (ninguno) | — | Leer una vez los dwords `.rdata` (rellenados en tiempo de ejecución) `0x6CF7808` (AlreadyPreempted), `0x6CF780C` (AlreadyPreempting), `0x6CF6CB0/CB4` (Info / DynamicData), `0x6CF6D48` (StageChartCondition), `0x6CF7260` (InvalidStageInfo), `0x6CF7B80` (distancia), `0x6CF7E90` (`SequencerNotInteractableState`), `0x6CF7E50` (WantedLevel), `0x6CE5118` (SelfPlayerActor) | Poner nombre al `@12` de los 3000 y al error de los Nak 1013 (el `0x58C8345D` del fixture de p2b) |
| **P5** | `0x2B36FB3` (en `0x2B36EF0`, tras el getter de `InteractionInfo`) | `48 8B F8 \| 48 8B 45 CF` (7 B) | `rax` = `InteractionInfo` | `[rax]` clave, `+0x12`, `+0x13`, `+0xBC`: el modo de «ocupado» de `0x000F4321` |
| P6 | `0x2B3708C` | `41 80 3F 02 \| 74 29` (6 B) | `[r15]` resultado de `vf170`, `[r15+4]` error | Por qué se rechaza un 2671 antes de la reserva |
| (existentes) | `0x2126A1F` (S/U2-b), `0x288B87A` (U-a), `0x2B00CE0` (`vf48`), `0x2879440` (Delete) | ya verificados en el repo | — | — |

**Prueba en `probe` recomendada (dos copias, un PC):**
1. El anfitrión compra solo: P1 tiene que dar «igual» con el id de su escena en `mgrH+0xA0`.
2. Invitado lejos con escena propia: el primer 2789 da P1 «distinto» (gestor `mgrH`), y U2-c dice «would route».
3. Juntos: P1 en las compras de los dos, P2 en los relevos y P5 una vez.

## 9. Lo que queda abierto

- [H] Qué etapa elige `0x1F5C940` como «actual» del NPC con dos escenas enlazadas (`+0x78/+0x80`, `+0x278/+0x280`,
  `+0x238/+0x240`). Lo decide P1 cerca.
- [H] Qué hace el gestor del campo del **cliente** (`vf48` del cliente) en el invitado para las comprobaciones previas de
  la interfaz (`0xA1D880`…): si es M, cuadra con (b).
- [E] El tramo «parada de la etapa → salida de `SetInteraction`» (H1, en medio).
- [H] H7 (funciones de los gráficos), H11 y quién lee `seqcomp+0x180` (`BlockInteraction`).
- [H] El valor de `+0x13`/`+0xBC` de `0x000F4321` (P5).
