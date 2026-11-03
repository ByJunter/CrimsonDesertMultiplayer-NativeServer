# P8 · Lectura de `.xtls`: paso a 3, login de NPC de secuenciador, actualización de escenas, rango y botín

10 de octubre de 2026, rama `opcion-d`. Build **1.0.0.2976**, direcciones en **RVA** (base `0x140000000`). Solo
lectura: no se tocó `src/`, no hubo commit, no se lanzó el juego ni se tocó su carpeta. El código de `.xtls` se lee para
**entender**. El mod no lo parchea ni lo engancha, y este informe no propone hacerlo.

**[V]** = comprobado (instrucciones con capstone o pseudocódigo de Ghidra contrastado con el listado).
**[E]** = interpretación. Las constantes que salen de un XOR o una suma con un global de `.xtls` (sección escribible)
son **valores del fichero**, no de ejecución: siempre van con **[E]**.

## 0. Método y preservación

- Ejecutable: `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, SHA-256 `57DA440D…FDE1E7`, comprobado antes y después **[V]**.
- Proyecto original `D:\RE\CrimsonDesert_2976\project`: se calculó el SHA-256, el tamaño y el mtime de sus 10 ficheros
  antes y después de trabajar. Son idénticos **[V]**.
  - Se trabajó con una copia propia en el scratchpad (3,8 GB), que se borró al terminar.
  - No quedó ningún proceso `java` vivo.
- Ghidra 12.1.4, sin interfaz, con `-readOnly -noanalysis` sobre la copia. Script propio `XtlsRead.java` (en el
  scratchpad, no se conserva). Hace lo mismo que `SelectiveXtls.java` del experimento:
  1. quita la restricción de memoria ejecutable solo en la sesión;
  2. aplica `DisassembleCommand(entrada, [Begin, End) de RUNTIME_FUNCTION, true)`;
  3. crea o amplía la función y la decompila;
  4. `-readOnly` descarta los cambios al cerrar.
- Capstone 5.0.7 con Python 3.14 `-I`, para:
  - barridos de llamadores `E8`/`E9` en `.rsrc` y `.xtls`;
  - escrituras a `[reg+disp]`, confirmadas desensamblando desde el inicio de su RUNTIME_FUNCTION;
  - RTTI y vtables.
- **Límite de los barridos de `.xtls`:** solo cubren los rangos con RUNTIME_FUNCTION (26 218 rangos, ~4,2 % de la
  sección). Si un escritor no aparece en `.xtls`, eso **no prueba** que no exista **[V del límite]**.
- Las 19 entradas de `.xtls` leídas dieron C completo (`completed=true`). Las 6 constantes ofuscadas de §1–§4 dan en el
  fichero exactamente el valor que usa el código claro vecino: `+8` (×2), `1`, `0x40000000` (×2), `−1` **[E]**.

## 1. Pregunta 1 — `0xA97B30` y el paso de `+0x231` de 2 a 3

### 1.1 Qué es [V]

- `0xA97B30` = `E9 AB 81 4F 09` = `jmp 0x9F8FCE0`: es exactamente la función del experimento.
- Rango RUNTIME_FUNCTION `0x9F8FCE0–0x9F8FF27`: 144 instrucciones, sin saltos indirectos.
- Tiene **un solo llamador**: `0x77D061`, dentro de `0x77CFB0`, que es el estado `0x14` de `0x7797F0`. No hay punteros a
  `0xA97B30` en ninguna sección.

```c
// 0x9F8FCE0(gestor G, float pos[3])                       (rcx = G, rdx = &{x,y,z})
FieldAttacher fa; 0x1F6C540(G, &fa);        // campo del actor [G+0x18]: [[a+0x68]+0x1A0]->vf140, AddRef 0x14343A0
if (fa.ok /*[rsp+0x48]*/) {
  G->b231 = 3;                                             // 0x9F8FD19
  for (i = G->n1C8 - 1; i >= 0; --i) {                    // lista de precarga +0x1C0, de atrás adelante
    SharedLock l(*(G + (DAT_14DA35958 ^ 0xD8F74809)));     // = [G+8] (contexto) [E: XOR en el fichero = 8]
    q = fa.campo->vf40();                                  // misiones del DUEÑO del campo (0x9A5B70); sin comprobar nulo
    play = 0x1AC2830(q, e)                                 // escena «del dueño» (q+0x186/+0x188 frente a e+0x60/+0x70/+0x30/+0x68)
        || e->b61 == 5
        || (e->b60 ∈ {0,1} && pos ∈ AABB[e+0xD8 .. e+0xEC]);  // 0x9F8FDD2–0x9F8FE29
    l.unlock();
    if (play) { 0xA9D860(G, &r, fa.campo, &slot);          // reproducir (0x9F8FE61)
                liberar(slot); quitar por intercambio con el último; G->n1C8--; }
  }                                                        // lo que no cumple, sigue en +0x1C0
}
~fa;                                                       // 0x1434880
```

- Hay un ruido de protección **[E]**: en `0x9F8FDC3`–`0x9F8FDC9`, `mov ecx,[0x15686DCD0]` y luego
  `mov [0x147A486E5],rcx`. Es una escritura a un global de `.xtls` sin uso aparente. Ninguna otra escritura sale del
  gestor ni de la instancia.

### 1.2 Respuestas [V salvo donde se indica]

- **Condición para escribir el 3:** solo que el `FieldAttacher` del actor `[G+0x18]` salga bien.
  - El actor tiene que tener un campo vivo (`0x14343A0`: no destruido y AddRef).
  - No mira `+0x174`, `+0x1B0` ni la fase anterior.
  - Las comprobaciones de fase las hace el llamador: `0x77CFB0`, solo para M.
- **Qué escribe:**
  - `+0x231 = 3`;
  - la lista `+0x1C0`/`+0x1C8`: quita lo que reproduce y marca `slot+0x16 = 1` en el elemento que mueve;
  - las reproducciones, a través de `0xA9D860`.
  - **No toca** contadores (`+0x260`), tiempos (`+0x2D0/+0x2D4`), `+0x174`, `+0x1B0/+0x1D0` ni otras listas. No
    registra callbacks.
  - Esto contesta al §7 de `p8-s8-escenas.md`, que preguntaba si haría «más cosas que el 3»: **no** las hace.
- **Qué lee:**
  - del gestor: `[G+8]` (contexto, cerrojo compartido), `[G+0x18]` (actor dueño del gestor) y `+0x1C0/+0x1C8`;
  - de cada instancia: `+0x30`, `+0x60`, `+0x61`, `+0x68`, `+0x70` y la caja `+0xD8…+0xEC`;
  - del campo: la vf40 (`0x9A5B70`): `FindUser([campo+0x180])` → `user+0xD0` → `[[c+0x68]+0x138]+0x18`.
- **¿Depende de que sea el gestor del dueño?** **No, por dentro.** Usa el G que recibe.
  - El «solo M» lo pone el único llamador (`0x77D03C call 0x9A5BE0`).
  - La única parte ligada al dueño es `0x1AC2830`: compara la instancia con las misiones del **dueño del campo**. En el
    invitado, el dueño es el propio invitado. Solo decide si una escena se reproduce **ya** o se queda para después.
- **El 1 tampoco está solo en `.rsrc`:** la vf20 del gestor (`vtable 0x55C9720+0x20 = 0xA95090` → `.xtls 0x9F7ED70`) hace,
  bajo el cerrojo exclusivo de `[G+8]`:
  - `G+0x20 = arg`;
  - `G+0x231 = 1`;
  - `G+0xD0 = 0`.

  En `.rsrc` siguen `0x77AA8B` (0), `0x77B0CE` (1) y `0xB87A67` (2).

### 1.3 Qué hace la actualización `0xA97300` con el 3 [V]

Esto es lo que hace equivalentes «escribir 3 y dar el tick» y «llamar a `0xA97B30`»:

- Fase 1 o 2:
  - entrada lista sacada de `+0x1B0` → la mete en `+0x1C0` (`0xA977C5`);
  - entrada lista sacada de `+0x1D0` → la reproduce.
- Fase 3:
  - la entrada lista se reproduce directamente (`0xA977D9`);
  - y en `0xA97885` reproduce también lo que haya en `+0x1C0`.
  - **Con freno** (`0xA97503`–`0xA9754C` y `0xA978CA`–`0xA9790A`): como mucho **una reproducción correcta por
    llamada**.
  - Una instancia normal solo puede reproducirse en la llamada en que `instancia+0x30 % 30 == G+0x260 % 30`
    (`G+0x260` se incrementa en cada llamada).
  - Las de tipo `+0x61 ∈ {2,3,4,5}` pueden reproducirse en cualquier llamada.
  - No mira la caja `+0xD8…`: con el 3 y el tick, **todo** `+0x1C0` acaba reproduciéndose.
- Tiempo estimado: con 22 instancias y una actualización por fotograma, entre 22 y unos 60 fotogramas **[E]**.

### 1.4 ¿Escribir `X+0x231 = 3` a mano? Qué se salta

| Lo que hace `0xA97B30` | Escribir 3 a mano y dar el tick `0xA953C0(X, Q, modo)` |
|---|---|
| Exige `FieldAttacher` del campo de `[X+0x18]` | Se salta. Si se escribe desde `0x8B6144`, el campo Q ya está retenido por el gancho nativo (`0x8B60C6`/`[rbp-0x49]`), y ese es el que usa `0xA9D860` |
| Reproduce **ya**, sin freno, las de la caja / tipo 5 / misiones | Se salta: las reproduce la actualización con freno (1 por llamada). Es más gradual y más prudente |
| Escribe 3 | Igual (escritura de un byte; el nativo tampoco toma el cerrojo) |
| Nada más | — |

- Conclusión **[V código / E ejecución]:** con el tick, escribir 3 a mano equivale a la llamada nativa salvo por la
  ráfaga inicial.
- **Sin el tick, el 3 no hace nada.** Nadie recorre `+0x1C0` de X.

### 1.5 Si se llamara a la función nativa (no recomendado)

- Firma **[V]:** `void 0xA97B30(Gestor* X, const float* pos3)`, con `rdx` → 12 bytes `{x, y, z}`.
  - En el nativo son `[P+0x150]` (8 B) y `[P+0x158]` (4 B): la posición del estado de carga **[E]**.
  - No devuelve nada que se use.
- Hilo **[V]:** el de la actualización principal. `0xAD2380` (`MainLoop_NetUpdate`) llama a `0x7797F0` en `0xAD2748` y a
  `0x8B6060` en `0xAD277F`. Este último es el que contiene el tick (`0x8B6150`).
- Requisitos **[V]:**
  - `[X+0x18]` es un actor vivo con campo;
  - la vf40 del campo no es nula: `0x1AC2830` lee `[rcx+0x186]` sin comprobarlo y sin él se cierra el juego.
    - En el invitado lo cumple su propio personaje.
    - El mismo requisito ya existe en la reproducción nativa `0x602940`.
- Por qué no: ejecuta código protegido desde un sitio que no es el suyo (con la escritura de ruido de §1.1), y no aporta
  nada que no dé el 3 con el tick.

## 2. Pregunta 2 — quién decide `+0xCF` y por qué los NPC de secuenciador no terminan el login

### 2.1 La cadena [V estático]

1. **Escritor de `+0xCF`:**
   - En `.rsrc`, el escritor de actores es `0x8B816B` (`ProcessPendingLogins 0x8B7B50`), como ya decía
     `p8-actores-anfitrion.md` §3. Los otros 23 escritores de byte en `+0xCF` no se revisaron uno a uno; el único
     mirado, `0x4139A0`, es de otra clase.
   - En `.xtls` (rangos con RUNTIME_FUNCTION), solo hay escrituras en `0xCC89A08`, de otra estructura **[E]**.
   - Condición: `+0xC1 != 0` y **ningún componente** del actor (lista `actor+0x78`, número `+0x80`, paso 16) devuelve
     verdadero en su **vf88**:

     ```c
     for (c in actor->comps) if (c->vf88()) goto aun_no;    // 0x8B813B
     actor->bCF = 1;                                         // 0x8B816B
     ```
2. **Componentes con vf88 no trivial:** de las 51 clases `Client*ActorComponent` (RTTI), **50** apuntan su vf88 a
   `0x359BA0` (`xor al,al; ret`).
   - **Solo `ClientSequencerPlayDataActorComponent`** (vtable `0x55BEE78`, el componente `[[a+0x68]+0xB0]`) tiene una
     propia: `0x9EE850`, en `.rsrc`.

   ```c
   bool vf88(comp) { f = comp->b3D8;                          // bajo cerrojo compartido del actor
     for (k = actor->u32AC; k; k = padre->u32AC)               // sube por los padres (+0xAC)
       if (padre = Find(k)) f = padre->comps[0xB0]->b3D8; else break;
     return f; }                                                // manda el flag del ancestro más alto encontrado
   ```
3. **Escritores de `comp+0x3D8`** (por la vtable de la clase):

   | Dónde | Qué | Cuándo |
   |---|---|---|
   | vf78 → `.xtls 0x9C8C000` | `= 1` si `comp+0x320 != 0` (bajo cerrojo exclusivo; el `+8` del XOR es **[E]**) | Al crear o restaurar el actor **[E]**: la firma `(this, &out)` coincide con las llamadas `[rax+0x78]` de `0x8ADCDE`/`0x8AFB11` (`ClientActorManager`) |
   | vf20 → `.xtls 0x9C7D8A0` | `dword +0x3D8 = 0` (y `+0x3DC/+0x3DD`) | Reinicio del componente |
   | **`0x9EE710` → `.xtls 0x9C8BC70`** | **`= 0`** (cerrojo exclusivo) | Ver 4 |
4. **Quién llama a `0x9EE710` (poner a 0):**
   - **(a) `0x602940`**, en `0x602C60`/`0x602D06`. Es el arranque de una instancia de secuenciador y su **único
     llamador** es la reproducción `0xA9D860` (`0xA9D91A`), que a su vez llaman solo `0xA97300` y `0x9F8FCE0`:

     ```c
     for (b in inst->enlazados /*+0x120, n +0x128, paso 0x28*/) {
       a = FindActor(b.clave);
       c = a->comps[0xB0];
       if (c->u64_2E0 == inst->u64_30) 0x9EE710(c);   // flag → 0 si el NPC es de ESTA escena
     }
     ```
   - **(b) `.xtls 0x8F80300`** (desde `0x8F803F9`). Engancha un actor a una escena que **ya** se está reproduciendo:
     - busca la escena;
     - si el actor tiene `+0x5E & 0x10`, pone el flag a 0;
     - después, si `vf1D8` (`+0xC2 && +0xCF`), lo une (`0x2053B50`).
     - Lo llaman, por el stub `0x606420`:
       - `0xA9E480`, desde el manejador del **2524** (`0xC0819B`, con el gestor de la vf48 del campo en `0xC08175` =
         M);
       - la cola de enganches pendientes `G+0x190` de `0xAA3E70` (`0xAA41CA`; §3).

### 2.2 Conclusión

- **[V estático]** Un NPC enlazado a un secuenciador (`comp+0x320 != 0`) no puede terminar el login (`+0xCF`) hasta que
  pasa una de estas dos cosas:
  - su escena (`comp+0x2E0 == instancia+0x30`) **se reproduce** por `0xA9D860 → 0x602940`;
  - o se **engancha** a una escena ya viva por `0x8F80300` (2524 o cola de `0xAA3E70`).
- Esto convierte en código la hipótesis «esperan a que su escena se reproduzca» de `p8-s8-escenas.md` §4.1.
- En el invitado ninguna de las dos ocurre para X:
  - X no tiene tick;
  - el 2524 se queda en M;
  - nadie ejecuta `0xAA3E70(X)`.
- **[E]** Coherente con G5 (`+0xCF 0` en los 71 enlazados, `+0xCF 1` en el único sin secuenciador) y con que
  `comp+0x2E0` de Rhett, Tina y Turnali valga el id de su secuenciador (`0x19A`, `0x1D0`, `0x1CF`).
- **[E] Consecuencias:**
  - El tick y el 3 de X deberían poner `+0xCF = 1` en los NPC presentes al reproducirse su escena.
  - Los NPC que aparezcan **después** necesitarán el enganche: el 2524 enrutado a X, o `0xAA3E70(X)`.
  - Si un NPC tiene padre (`+0xAC`), manda el flag del padre.
- **Medida barata** (sonda G5, solo lectura): `[[a+0x68]+0xB0]` → `+0x3D8`, `+0x320`, `+0x2E0` y `a+0xAC`. Confirma la
  causa antes de tocar nada.

## 3. Pregunta 3 — actualización por fotograma de las escenas de horario (2335)

### 3.1 Quién la llama [V]

- `0x8B268E` está en un trozo encadenado (`0x8B2686–0x8B26C1`, `UNW_FLAG_CHAININFO`) de **`0x8B2660(this, dt)`**:

  ```c
  if (this->actor50 && (actor50->b5E & 0x10) && (M = 0x9A5BE0(*this->campo70)) && this->modo80->b50 != 2)
  { 0xAA3350(M); 0xAA3E70(M, dt); }
  ```
- `0x8B2660` la llaman `0x8B63DE` y `0x8B67F3`, ambas dentro de `0x8B6060`, la misma función que el tick `0x8B6150`.
  - Las dos llamadas dependen de que el byte global `[0x6CC0218]` valga 0.
  - Hay además un `jmp` en `0xAD4218`.
- Mismo hilo y mismo fotograma que el tick de secuenciadores; **solo para M**.

### 3.2 `0xAA3350` [V]

- Es `ClientSequencerStageManager::updateStageManager_SyncStageCandidatePositionList` (nombre RTTI de su tarea local).
- Toma la posición del **usuario local** cada 3 s, o cuando se aleja más de un umbral.
- Lanza `FindStageCandidatePositionTask` y guarda el resultado en `M+0x248/+0x250`.
- No llama a `.xtls`.
- **No hace falta para X [E]:** es la búsqueda de candidatos alrededor del jugador local.
  - Por el nombre, es la fuente de `SyncStageCandidatePostionReq` 2100 **[E]**: ejecutarla para X duplicaría ese C>S.

### 3.3 `0xAA3E70(G, dt)`: la actualización de las escenas vivas del gestor (no solo de horario) [V]

1. **Cola de enganches pendientes** `G+0x190`:
   - por cada escena con `+0x28B`, llama a **`.xtls 0x8F80300`** (`0xAA41CA`) para cada registro de 12 B;
   - lo que falla vuelve a la cola;
   - esto es el (b) de §2.1.
2. **Escenas vivas** `G+0x28`: por cada una con `+0x28B` y sin `+0x441`:
   - avanza su lógica (`0x600730`, `0x5DBEA0`, `0x5DC5B0`, `0x5F1D30`, `0x5DBA30`, `0x5DE800`, `0x5DEA00`, `0x5DD3B0`,
     `0x5DE4C0`);
   - cuando se cumple una espera de rama: `stage+0x28A = 1` y **`.xtls 0xA4D1A10`** (`0xAA4907` por `0xC037D0`), que
     **manda `StageBranchStateDelayEndReq` 2105** (opcode `0x839` en el código) **[V]**;
   - temporizadores `stage+0x178`: al vencer, **`.xtls 0x9FDA4F0`** (`0xAA4A2E` por `0xAAF750`). Quita de la lista
     `[[0x6D691E0]+0x118]+0x198` (paso 0x20) las entradas de esa escena.
3. **Parte del usuario local:** `0xA96070(G, local)`, temporizador `G+0x2F0`, `0xA9A1F0`, `0xA9A410`; luego `0xAA5040`
   y `0xAA5400`.

- **[E]** Es la «vida» de las escenas que ya se reproducen: ramas, cartas y esperas, sean de secuenciador o de horario.
- El tick `0xA953C0` solo **arranca** instancias (`0xA97300`) y procesa borrados diferidos (`+0x1F0`/`+0x1E0`,
  `0x1F6D320`). No avanza escenas vivas.
- Una escena de X reproducida sin `0xAA3E70(X)` probablemente se quedaría en su primer estado **[E]**. Aun así, el
  arranque ya basta para `+0xCF` (§2).

### 3.4 Respuesta

- **Sí interviene `.xtls`:** enganche de actores (`0x8F80300`), envío del 2105 (`0xA4D1A10`) y temporizadores
  (`0x9FDA4F0`).
- Para X, añadir `0xAA3E70(X, dt)` (no `0xAA3350`) desde el mismo punto, con la misma condición `modo != 2`. Pero es una
  **fase 2**, porque genera C>S (§6).

## 4. Pregunta 4 — rango, permisos y botín

### 4.1 `GrantRankPermit` (`0x2220850` → `.xtls 0xE6B74D0`, 459 B) [V]

```c
ExclusiveLock l([C+8]);                                    // cerrojo exclusivo del actor
if (clave ∉ C+0x140[0..n148)) {
  push_u32(C+0x140, clave);                                // capacidad +0x14C
  for (t = 0; t < 4; ++t)                                  // 4 tipos de orden
    if (i = buscar(C+0x18+16t, clave)) {
      push_u64(C+0x58+16t, elem);                          // elem de 8 B: u32 clave | u8 marca
      quitar_por_intercambio(C+0x18+16t, i);               // el «1» de n−1 sale de una suma ofuscada [E]
      break;
    }
}
```

- Si la clave ya tenía permiso, no mueve nada.
- No llama a nada fuera de crecer vectores (`0x3D2AF0`, `0x3D4130`).
- **Si la clave no está en espera, solo añade el permiso.** La siguiente `InsertAttacker` la meterá directamente en
  `+0x58`.

### 4.2 `InsertAttacker` (`0x221CBD0` → `.xtls 0xE69FF80`, 361 B) [V]

- **No toma cerrojo:** lo hace el llamador (SetTarget).
- Si la clave ya está en cualquiera de los 4 tipos de la lista que toca (`+0x58` con permiso, `+0x18` sin él), no hace
  nada.
- Si no, añade `{clave, marca}` al tipo `t`: en `+0x58+16t` si tiene permiso y en `+0x18+16t` si no.

### 4.3 Quitar el permiso (`0x2220370` → `.xtls 0xE6ADAC0`) [V]

- Con cerrojo exclusivo:
  1. quita la clave de `+0x140` por intercambio;
  2. llama a `0x221B9C0(C, clave, 0)`, que la baja de `+0x58` a `+0x18`.
- Tiene la misma clase de escritura de ruido a un global (`[0x15686DF2C]`).

### 4.4 Para la llamada directa del §11.1 (2) de `p8-sistemas-juego.md` [E]

- `0x2220850(C_G, &clave)` es corta y autocontenida. Toma el cerrojo de `[C+8]` (reentrante, pero cerrojo).
- Llamarla dentro de SetTarget del mismo actor podría reentrar. Fuera, desde el hilo de IA del objetivo, es lo nativo.
- El parche del `vf188` sigue siendo preferible: no ejecuta `.xtls` desde otro sitio.

### 4.5 Botín (`0x20DEE60` → `.xtls 0xE2BF850(campo, out)`, 260 B) [V]

```c
mgr = TLS[+0x1EC] ? [0x6D69500] : [0x6D694F8];             // global según una bandera del hilo
[mgr+8]->vf30(&tmp, [campo+0x180], campo, 0);              // búsqueda del usuario dueño
out = ScopeAttacher<CommonUserActor>{ +8 usuario, +0x10 ok, +0x11, +0x18 };   // se queda con tmp; tmp queda vacío
if (!ok) out = {vt, 0, 0, 0};                              // y destruye tmp
return out;
```

- Del campo solo lee `[campo+0x180]` (lo pasa también como 4.º argumento).
- **`0x2A82590` (`FindUser`) es la vf30 de `ServerActorManager`** (vtable `0x5B25988+0x30`) y **no usa `r9`**.
- **Diferencia con el mod:** `kill_loot_per_user` sustituye esto por `FindUser([[0x6D696B8]], out, clave)`.
  - Los 6 llamadores de `.rsrc` revisados de `0x2A82590` usan `[[0x6D696B8]]`.
  - El cuerpo de `.xtls` usa `[[0x6D69500]+8]`, o `[[0x6D694F8]+8]` si la bandera TLS `+0x1EC` es 0. Que sean el
    mismo objeto en el hilo del servidor es **[E]**.
  - **Recomendación:** que el resumen de `User2Funnels` registre una vez los dos punteros.
- El handle de salida es del mismo tipo que el de FindUser: `+8` usuario, `+0x10` bloqueado. Lo libera el llamador con
  `0x1434880` **[V]**.

## 5. Recomendación para el paso a 3 (y el tick)

En el **invitado**, todo en `.rsrc`, desde el gancho intermedio ya planeado en **`0x8B6144`**:

- **hilo principal**, dentro de `0x8B6060`;
- Q retenido por el `FieldAttacher` nativo;
- `rax` = M, `[rbp-0x41]` = Q, `r8` = `[rdi+0x80]`.

Por cada X conocido de `guest_seq_route` (vtable `exe+0x55C9720`, `X ≠ M`, `[X+8]` y `[X+0x18]` no nulos):

1. **Tick:** si `[[rdi+0x80]+0x50] != 2`, llamar a `0xA953C0(X, Q, modo)`.
2. **Paso a 3, una vez por X** (escritura de un byte, sin llamar a `.xtls`). Condiciones:
   - `X+0x231 == 2`;
   - `X+0x174 == 0`;
   - `M+0x231 == 3` (la carga propia ya terminó);
   - hay algo en `+0x1B0` o `+0x1C0`.

   Se escribe antes de la llamada del tick de ese fotograma.
3. **Medir:**
   - `X+0x1C0` y `+0x1B0` bajan (≤ 1 reproducción por fotograma);
   - `P8Probe BodyAnim` con claves `0xB…`;
   - en G5, `comp+0x3D8 → 0` y luego `+0xCF → 1`;
   - `GuestS3 … INSTALLED` en Rhett, Dahlia y Grover.
4. **Fase 2, después y por separado:**
   - `0xAA3E70(X, dt)` en el mismo punto (`dt` = `xmm1` de `0x8B2660`, o el del fotograma);
   - el enrutado del 2524 a X (`0xC08175`).
   - Antes, **ampliar el filtro C>S** (§6, R1).
5. **Reinicio:** devolver X a su estado cuando el anfitrión se vaya o X se destruya.
   - Las escrituras de 0 y 1 nativas solo tocan M (`0x77AA8B`, `0x77B0CE`, vf20).
   - Si un X se reutiliza, el mod debe volver a evaluar las condiciones del paso 2.

**¿Por qué escribir y no llamar?** Con el tick, el efecto es el mismo (§1.4). Escribir:

- no ejecuta `.xtls` fuera de su sitio;
- no depende del `FieldAttacher` del Kliff remoto;
- evita la ráfaga sin freno.

Llamar a `0xA97B30` solo tendría sentido si el 3 a mano no reproduce nada y hace falta descartar que la diferencia esté en
la ráfaga. Y aun así, con `pos` = posición del jugador local y solo desde este mismo punto.

**Prueba siguiente:** necesita el cliente del invitado (**Shadow**) con anfitrión.

## 6. Riesgos

| Id | Riesgo | Mitigación |
|---|---|---|
| R1 | **C>S sobre escenas del anfitrión que el filtro actual no cubre.** El filtro de `observer_seq_copy_logic.h:35` (en el anfitrión, sobre los C>S del usuario 2) cubre 2767/2591/2436/2138/2283/2173, según la cabecera. **No** cubre:<br>– **2276 `DeleteSequencerReq`**: la manda `0xA95530` (borrado con código, p. ej. 6 desde `0xA9D860` si el recurso no está listo) por `0xC02CB0`, opcode `0x8E4` **[V]**;<br>– **2105 `StageBranchStateDelayEndReq`**: la manda `0xAA3E70` por `.xtls 0xA4D1A10` **[V]**.<br>Con el tick de X, la primera se vuelve posible; con la fase 2, la segunda | Añadir 2276 y 2105 al filtro (sus primeros 8 B son el id de escena: **[V]** para 2276 por `0xC02D1E`, **[E]** para 2105). Hacerlo antes de la prueba, o vigilar `G4` |
| R2 | Cierre en `0x1AC2830` si la vf40 del campo es nula (personaje del dueño del campo ausente): pasa en `0x602940` y en `0x9F8FCE0` | Llamar al tick solo con el personaje local listo (`M+0x231 == 3` y sin carga en curso) |
| R3 | Ráfaga: ~22 escenas del Kliff del anfitrión arrancan en 0,5–1 s con él como «jugador» (cámara, entrada e interfaz del invitado) | El freno nativo ya limita a 1 por fotograma. Mantener el criterio de aborto de `p8-s8-escenas.md` §7 |
| R4 | Escenas reproducidas sin `0xAA3E70(X)` se quedan sin avanzar | Es esperado en la fase 1. La fase 2 lo resuelve con R1 cubierto |
| R5 | NPC que aparecen después de la reproducción siguen con `+0x3D8 = 1` | Fase 2 (2524 a X o cola de `0xAA3E70`). Sonda de `+0x3D8` en G5 |
| R6 | `+0xCF` puede seguir a 0 por un padre (`+0xAC`) cuyo flag no se limpia | La sonda lo muestra (`a+0xAC` y el flag del padre) |
| R7 | Lecturas basadas en el código del fichero: las 5 constantes ofuscadas y el ruido de `.xtls` podrían valer otra cosa en ejecución | Ninguna recomendación depende de ellas. Los desplazamientos `+8` se repiten en `.rsrc` |
| R8 | Barrido de `.xtls` con cobertura de solo ~4,2 % | Cualquier «no hay más escritores en `.xtls`» es **[E]** |

## 7. Mejora propuesta para `Cdmp_Query.java` (sin aplicar)

**Problema [V]:** `cmdDecomp` (líneas 323–325) usa `getFunctionContaining(a)` y responde «No hay función» cuando la
función de `.xtls` tiene un cuerpo de 1 byte o no tiene objeto `Function`.

**Propuesta:** resolver la entrada en este orden y decir de dónde sale. Todo dentro de una transacción que se **aborta**
al terminar, aunque el programa ya esté abierto con `-readOnly`.

1. `getFunctionContaining(a)` con cuerpo mayor de 1 byte → **«cuerpo Ghidra»**.
2. **Stub de 5 B** `E9 rel32` en `.rsrc` → seguir al destino una vez → **«thunk → destino»**.
3. **RUNTIME_FUNCTION:** leer una vez el directorio de excepciones del bloque `Headers` (directorio 3 del encabezado
   opcional; triples `Begin/End/Unwind`).
   - Búsqueda binaria `Begin ≤ rva < End`.
   - Si `UNWIND_INFO.flags & 4` (`CHAININFO`), subir a la entrada principal: el RUNTIME_FUNCTION va tras los códigos de
     desenrollado, alineado a 4.
   - Origen **«RUNTIME_FUNCTION [b,e) (encadenada desde …)»**.
4. `getFunctionAt(entrada)` existe aunque tenga un cuerpo de 1 byte → decompilar **esa entrada** (el decompilador
   recalcula el flujo) → **«entrada conocida»**.
5. Si no hay `Function` o el C sale vacío, en una transacción abortable:
   1. desactivar la restricción de memoria ejecutable;
   2. aplicar `DisassembleCommand(entrada, AddressSet[b,e), true)`;
   3. `createFunction`/`setBody` con las instrucciones del rango;
   4. `flushCache`, decompilar;
   5. `endTransaction(tx, false)`.

   Origen **«cuerpo temporal acotado»**.
6. Solo si todo falla: «No hay función (sin RUNTIME_FUNCTION ni entrada conocida para …)».

Esbozo:

```java
private Address[] pdataRange(long rva) { /* caché long[][] del directorio 3; búsqueda binaria; seguir CHAININFO */ }
private String resolveOrigin(Address a, Address[] out) {
  Function f = getFunctionContaining(a);
  if (f != null && f.getBody().getNumAddresses() > 1) { out[0] = f.getEntryPoint(); return "cuerpo Ghidra"; }
  Address t = followJmpStub(a); if (t != null) a = t;                  // E9 rel32 en .rsrc
  Address[] r = pdataRange(a.getOffset() - BASE);
  if (r != null) { out[0] = r[0]; out[1] = r[1]; return "RUNTIME_FUNCTION"; }
  if (getFunctionAt(a) != null) { out[0] = a; return "entrada conocida"; }
  return null;
}
// cmdDecomp: int tx = currentProgram.startTransaction("cdmp tmp");
//   try { ...crear cuerpo acotado si hace falta, decompilar... } finally { currentProgram.endTransaction(tx, false); }
// Cabecera: "// origen: RUNTIME_FUNCTION 0x9F8FCE0–0x9F8FF27 (.xtls), cuerpo temporal acotado, no persistente".
```

- Límites: el `DECOMP_TIMEOUT_S` actual, un máximo de bytes por rango (p. ej. 64 KiB) y ningún análisis automático.
- `Cdmp_PreAnalysis.java` no cambia: el permiso de ejecución sigue quitado en la base; la excepción es solo por consulta.

## 8. Correcciones a documentos anteriores

| Afirmación | Ahora |
|---|---|
| `p8-s8-escenas.md` §3.3/§6.2: «el camino nativo (`0xA97B30`) es un thunk a `.xtls`: no se usa; hace falta saber qué hace» | **[V]** `jmp 0x9F8FCE0`. Hace el 3 y una primera ronda de reproducción, con una sola condición: que el campo del dueño del gestor esté vivo. Nada más (§1) |
| `p8-s8-escenas.md` §7: «puede que haga más cosas que el 3 (tiempos `+0x2D0/+0x2D4`)» | **[V]** No las hace |
| `p8-s8-escenas.md` §4.1: que `+0xCF` espere a la escena es «[E fuerte], se decide en `.xtls`» | **[V estático]** Lo decide la vf88 (`.rsrc 0x9EE850`) sobre `comp+0x3D8`, que pone a 0 el arranque de la instancia (`0x602940`) o el enganche (`.xtls 0x8F80300`) (§2) |
| `p8-s8-escenas.md` §3.2: «`0xAA3350`/`0xAA3E70` (para las escenas de horario de `+0x148`) [E]» | `0xAA3350` es la búsqueda de posiciones candidatas del usuario local. `0xAA3E70` es la vida de **todas** las escenas vivas del gestor (`G+0x28`), con enganche de actores y C>S 2105 (§3) |
| `p8-s8-escenas.md` §6.1: «tick = `0xA953C0`» | Basta para arrancar instancias y para `+0xCF`. Para que las escenas avancen hace falta además `0xAA3E70(X)` **[E]** |
