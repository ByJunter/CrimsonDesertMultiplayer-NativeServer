# P8 · Ficheros de horario de NPC en el invitado (`guest_schedule_load`)

10 de octubre de 2026, rama `opcion-d` (sobre `3710b14`). Build del juego **1.0.0.2976**, direcciones en **RVA**
(base `0x140000000`). Aprobado para implementar todo lo pendiente. No se lanzó el juego ni se desplegó
nada. `.xtls` no se parchea ni se engancha; solo se lee y se llama a funciones de `.rsrc`.

**[V]** = comprobado: capstone sobre el PE de solo lectura (`build\research\cdpe.py`, `cddis.py`, `callers.py`), los
logs y capturas de la sesión 8, compilación y pruebas. **[E]** = interpretación o pendiente de la prueba en el juego.

Base: `p8-arquitectura-observador.md` §2.4 (el contenedor vacío en el invitado), `p8-s1-escenas-npc.md`,
`p8-npc-tiendas-animales-escenas.md`, `INFORME-RAIZ-OBSERVADOR.txt` (G1: 434 actores con *FILE NOT LOADED*).

## 0. Resumen

- **El cargador `0x1864140` no necesita nada del servidor [V].** Recibe el gestor, un handle de salida y el **nombre**
  del fichero (un `String`). Solo usa el gestor (`+8` contenedor con su cerrojo, `+0x90` sistema de ficheros) y el
  montón del juego del hilo que llama. No lee la bandera de reino TLS `+0x1EC` ni toca el `ServerModule`.
- **El nombre está en el propio cliente [V].** Lo da la tabla *schedule context* del gestor (`mgr+0x10`), que el
  cliente también carga al arrancar (`NPCScheduleManagerLoadAsync`). Está indexada por la misma clave `u32` que traen
  el 2335 y el bloque de IA del 2005. Los llamadores del servidor le pasan `&entrada+0x10` (`0x281B83A`).
- **La clave es el hash del nombre [V].** `0x1364780` = lookup3 `hashlittle` con valor inicial `0xC5EDE`, sobre el
  nombre base sin extensión (`0x1422F90`). El mod lo recalcula y solo carga si el nombre vuelve a dar la clave.
- **Implementado** `[session2] guest_schedule_load = 0 | probe | 1` (solo invitado):
  - la carga se hace en el **hilo de red del cliente**, justo **antes** de entregar al juego el 2005, 2335 o 2874 del
    anfitrión que nombra el fichero. Es el orden de un proceso de un jugador: su servidor carga antes de enviar;
  - las búsquedas `0x1869D70` y `0x1862A80` se **vigilan** en solo lectura, para la verificación.
- **Comprobado offline con la sesión 8 [V]:** el escaneo de los 2005 encuentra el recurso de los **461 de 461** NPC
  que G1 vio con recurso de horario (25 ficheros distintos). Los 36 × 2335 nombran 27 recursos más.
- **Inis de Shadow (`cdmp.p8-s7-shadow.ini` y `-fase2.ini`): `guest_schedule_load=1`** (§4). La autocomprobación
  del anfitrión (§8) confirmó dos de los tres datos de ejecución; el tercero (el tiempo de cada carga) lo vigilan los
  límites y el bloqueo a `probe` tras una carga de más de 250 ms. Revisión del código: §9.
- **Instantánea de los 43 × 2335 previos a la entrada:** diseño en §5; implementada en el anfitrión
  (`observer_sched_copy`, `p8-impl-horarios-snapshot.md`).
- **Sesión 12 (§10):** el handle de un fichero que nombra un 2335 se retiene hasta que ese 2335 corre. Antes, el
  barrido de inactividad del contenedor lo quitaba durante los 3,2 s que el cliente aplazaba el 2335.

## 1. Investigación estática [V]

### 1.1 `0x1864140` (RUNTIME_FUNCTION `0x1864140–0x18643A8`)

```c
Handle* 0x1864140(NPCScheduleManager* mgr, Handle* out, const String* name, u8 /*sin uso*/)
  base = 0x1422F90(**name)            // tras el último '/' o '\\', máx. 0x104 caracteres, corta en el último '.'
  key  = 0x1364780(base, strlen)      // lookup3 hashlittle(base, len, 0xC5EDE)
  if (0x186A9A0(mgr->container /*+8*/, &key, .., &h)) { *out = h; h->b16 = 1; return out; }   // ya estaba
  path = "%#/%#" ("sequencer/binary__", *name)   // 0x5C13C0
  return 0x18643B0(mgr, out, path, 0)
```

- El cuarto argumento (`r9b`) se guarda en la zona de inicio y lo sobrescribe la clave (`0x18641F0`): no se usa.
- **`0x18643B0(mgr, out, path, solo_indice)`** (`0x18643B0–0x1864CD8`):
  1. `[mgr+0x90]->vf40` lee el fichero entero, **de forma síncrona**;
  2. reserva el `NPCScheduleData_File` (0xE0 B) en el montón del juego. Usa `0x1447EFA78` (montón global con
     sección crítica) o `0x1447EF9B0` (montón propio del hilo, tipo mimalloc, con liberación entre hilos), según el
     byte TLS `+0x1FD`;
  3. lo deserializa (`0x18610E0`);
  4. con `solo_indice = 0`: `0x1864CE0` (ruta de recarga de desarrollo; sale enseguida si `[0x6CE3DF8] != 0`) e
     inserta con `0x186AB90`, que salta a `.xtls` y usa el cerrojo del contenedor;
  5. con `solo_indice = 1`: solo añade los nodos al índice `mgr+0x30` y suelta el fichero.
- **El handle** se libera con `0x38AD90(&h)` (cuenta en `+0x10`, atómica si `+0x14`; `vf0(1)` al llegar a 0). El
  contenedor guarda su propia referencia.
- **Errores:** fichero ilegible o que no se deserializa → `0x35DE50` (línea de log del juego en coreano) y `out = 0`.

### 1.2 De dónde salen la lista y los nombres

- **No hay lista por nivel, campo o sector.** La carga es **perezosa, por clave**: el código del servidor busca primero
  (`0x1862A80`) y, si falta, llama a `0x1864140` con el nombre de la entrada de contexto de esa clave.
- **Arranque, cliente y servidor** (`NPCScheduleManagerLoadAsync`, vtable `0x55C6728`, `vf1 0xA4B340 → 0x1862B60`).
  La crea el cliente en `0xA498A2` (`IGameClientModuleAsyncWork`) y el servidor en `0x27C1825`:
  1. `0x1862570` (cuerpo en `.xtls`): limpieza **[E]**;
  2. enumera `sequencer/binary__/*.paschedule` con `fs->vf58` (capacidad inicial 3000) y llama a
     `0x18643B0(.., solo_indice = 1)` por cada uno: solo construye el índice nodo → fichero (`mgr+0x30`);
  3. deserializa `sequencer/binary__/scheduleContext.paschedulectx` en `mgr+0x10` (la tabla de contexto) y en
     `mgr+0x50`/`+0x70`.
- **La tabla de contexto** (`mgr+0x10`):
  - cubos `+0x10`, usados `+0x14`, cubos de 0x100 B en `+0x20`, entradas en `+0x28` (como el contenedor);
  - entrada: `+4` clave, `+8` valor (`0x1861AC0`: `u8`, `String` en `+8`, …). El nombre es `[[entrada+0x10]]`
    (`char*`);
  - la leen `0xA9FEE0` (2335) y `0x281B700` (servidor) con la misma clave que el contenedor.

### 1.3 Quién llama a `0x1864140` en el servidor y cuándo

Seis llamadores, todos del servidor (`p8-arquitectura-observador.md` §2.4). Cada uno elige el gestor con la bandera
TLS `+0x1EC` (`[[0x6D69518 | 0x6D69520]+0x40]`) y lo llama **en el momento de crear lo que necesita el horario**:

| Llamador | Quién lo llama | Cuándo [E por el contexto] |
|---|---|---|
| `0x281B700` | `ServerQuestManager` vf8 (`0x2925760`) | arranque o actualización de un horario ligado a misión; pasa `&entrada+0x10` (`0x281BA3F`) |
| `0x288A970` | `0x2885BD0` ← `0x2882C70` | creación de stages (la familia 2264/2165); pasa `[obj+0x10]+8` |
| `0x27EA160` | `0x27E81E0`, `0x27E8C90`, `0x27E9B40`, `0x27EE3E0` (×4), `0x27F29D0` | activación de *spawning pools* |
| `0x2848CE0` / `0x2849400` | `0x2849FC0`, `0x28431A0`, `0x2844000`, `0x2855C70` | facción |
| `0x292A410` | `0x2929F20` | misiones |

- En un jugador, el hilo del servidor carga y el cliente encuentra el fichero porque **el objeto es el mismo**: un
  solo constructor (`0x1861F00`) y `[0x6C8CDA0]`.
- El anfitrión de la sesión 8 llegó a 36–50 ficheros en su contenedor (G2 *host*); el invitado se quedó en 0.
- **Qué estado del servidor necesita:** ninguno, aparte del gestor compartido. Los raíces `0x6D69518` (cliente,
  `+0x90 = 0`, escrito en `0xAD0B03`) y `0x6D69520` (servidor, `+0x90 = 1`, escrito en `0x27C0E85`) son dos objetos
  distintos. Que su `+0x40` apunte al mismo gestor es **[E fuerte]**: un solo constructor, y el cliente del anfitrión
  encuentra lo que carga su servidor. El modo `1` lo exige en ejecución (§3.2).

### 1.4 Alternativas en el cliente

- **Volver a llamar a `0x1862B60`: no.** Limpia (`0x1862570`), recarga todo con `solo_indice = 1` (no deja nada en el
  contenedor) y vuelve a deserializar el contexto.
- **`0x18643B0` directo: no.** Es `0x1864140` sin la búsqueda previa (podría duplicar) y necesita la ruta ya formada.
- **El 2335 no carga [V]:** `0xC0AFC0 → … → 0xA9FEE0 → 0x1862A80` solo busca. Lo mismo el 2874 (`0xC0CE50 →
  0x1869D70`) y el bloque de IA del 2005 (`0x1EB88C0 → 0x1869D70`).
- **Conclusión:** la vía más nativa es la del propio servidor: `0x1864140(mgr, &h, &entrada_contexto+0x10, 0)`.

### 1.5 Hilo

- **No en un hilo del mod:** la reserva usa el montón del juego.
- **No en el hilo principal:** la lectura es síncrona; los manejadores de 2005, 2335 y 2874 corren en el principal
  (T13152 en la sesión 8, el mismo que el 2322).
- **Elegido: el hilo de red del cliente** (T15952 en la sesión 8). Es un hilo del juego (montón propio) donde el mod ya
  llama al juego (`ClientNet::OnRecvPacket` desde `deliver_all`). Se carga **antes** de entregar el paquete, así que
  el manejador del hilo principal encuentra el fichero.
- **Quién llama de forma nativa a `0x1864140` fuera del hilo principal [V]:** solo los hilos del servidor (los 6
  llamadores de §1.3, con el byte TLS `+0x1EC = 1`). Ningún llamador del cliente.
- **Montón [V]:** el byte TLS `+0x1FD` solo elige entre dos asignadores, y los dos admiten cualquier hilo:
  - `0x1447EFA78`: montón global con sección crítica (`0x6C169C8`);
  - `0x1447EF9B0`: montón propio del hilo (índice TLS `[0x6C88C50]`, se inicia solo si `+0x98 == 0`);
  - la liberación `0x1447F02BC` compara el hilo dueño de la página (`gs:[0x30]` frente a `[segmento+0x100]`) y, si
    es otro, va por la liberación entre hilos (`0x1447EFFF4`).

  El valor de `+0x1FD` en el hilo de red no se puede leer en estático: lo pone código de `.xtls`. Cualquiera de los
  dos es válido **[V]**. El juego ya reserva en ese hilo: la copia de cada paquete que hace `OnRecvPacket`.
- **Cerrojos al llamar [V]** (`opcion-d-p8.md` §2):
  - `0x35BAD4` está en el bucle del hilo de red, después de la espera de Q1 (ya soltada) y antes de vaciar la cola de
    salida, que no bloquea;
  - el propio bucle llama a `OnRecvPacket` (`0x35BB53`) más adelante en la misma vuelta, y el mod ya lo llama desde
    ahí;
  - el cargador toma, dentro, el cerrojo del contenedor y los del sistema de ficheros, y ninguno de los dos lo tiene el
    hilo de red en ese punto.
- **El sistema de ficheros en el hilo de red [E fuerte]:** lo usan ya varios hilos (el trabajador asíncrono del
  arranque, el hilo del servidor y el cliente para la transmisión). El primer `[load]` lo confirma; un fallo apaga el
  módulo (filtro SEH).
- **Coste:** una carga retiene el hilo de red y retrasa los paquetes de esa vuelta (entrada y salida del invitado).
  Por eso hay límites, el histograma y el bloqueo de §3.2.
- **El contenedor** es `ThreadSafeRefCountedContainer`. Su búsqueda nativa `0x186A810`:
  - toma el cerrojo de lectura (usuarios `+0x68`, objeto de cerrojo `vf40`, `vf18`/`vf20`);
  - pone a 0 el contador de inactividad de la entrada (`holder+0x10 = 0`);
  - devuelve el fichero con una referencia, **o 0 si se está descargando** (`+0x15`).

## 2. Lo que faltaba en la sesión 8 [V]

- **G2:** 25 recursos con NPC, todos con «container empty» toda la sesión. Los que más NPC tienen:

  | Recurso | NPC |
  |---|---:|
  | `0xAC00B3BB` | 54 |
  | `0x5FB728ED` | 32 |
  | `0x79AF6B66` | 29 |
  | `0xAD5D3465` | 21 |
  | `0x3F8972FE` | 15 |
  | `0x6FF7E5A6` | 14 |
  | `0xA6BD9C7B` | 10 |
  | `0xF56759DA` | 9 |

  `0xF56759DA` es el de las ovejas `12C`/`134` y la escena `2000018B`.
- **G1:** 2354 NPC leídos; 461 con recurso de horario, los 461 *FILE NOT LOADED*.
- **36 × 2335** (taberna, GC 600 s): 27 recursos. Algunos coinciden con los de G1 (`0xF56759DA`, `0x3F8972FE`,
  `0xAD5D3465`…) y otros no (`0x54AC45AA` ×5…). Ningún 2874.
- **Escaneo offline** (script en el scratchpad sobre `cdmp_packets_20261010-093514.bin`; no se conserva):
  - en los 4712 × 2005, cada registro de NPC (`0xB…`) contiene el recurso que G1 leyó: **461/461**;
  - la mitad de los registros lo llevan dos veces (la segunda con nodo 0): es la misma clave y no cambia qué fichero
    se carga;
  - los registros miden ~5–7 KB; el bloque está en `+0x142F…+0x1C2D`.

## 3. Implementación

### 3.1 Clave y archivos

| Clave | PC | Valores |
|---|---|---|
| `guest_schedule_load` | invitado | `0` (nada instalado) \| `probe` (decide y escribe, no llama) \| `1` (carga) |

- **Lógica pura:** `src/mod/session2/guest_sched_load_logic.h`. Pruebas en `tests/optd_sched_load_tests.cpp`
  (`optd_sched_load`; con la revisión, 37/37 suites pasan):
  - hash lookup3 con sus valores publicados y la semilla del juego;
  - nombre base;
  - réplica de la tabla de contexto y lectura del `String`;
  - conjunto de claves;
  - claves de 2005/2335/2874 sobre registros de la sesión 6, paquetes que no cuadran y desbordamiento;
  - libro por clave con varios hilos, conjunto de reintento y límites;
  - **`gsl::process_key` / `process_retry` con un juego simulado** (`StubEnv`: contenedor, tabla de contexto,
    resultado y duración del cargador, reloj). Es la costura pedida: toda la decisión del hilo de red se prueba sin
    el juego.
- **Módulo:** `guest_sched_load.{h,cpp}`. Cambios de una línea en `guest.cpp` (arranque, `deliver_all`, línea de
  estado), `guest_diag_worker.cpp` (tick), `config.*` y `mod.cpp` (línea de configuración).

### 3.2 Carga (hilo de red, `deliver_all`, antes de `guarded_on_recv`)

1. **Puertas, antes de tocar nada** (`gsl::early_gate`): modo, código verificado y sin fallo, hilo de cliente (TLS
   `+0x1EC == 0`), jugando en el anfitrión, gestor válido (vtable `0x57DB090`, contenedor, `fs` y el gestor del cliente
   igual a `[0x6C8CDA0]`). Si una falla, no se toca ni el libro, ni los límites, ni el conjunto de reintento.
2. **Claves del paquete:**
   - 2335: recurso en `+9`;
   - 2874: recurso en `+8`;
   - 2005: en cada registro de NPC, todo `u32` desde `+5` que sea clave de la tabla de contexto. Filtro de 64 Kbit por
     los 16 bits altos y búsqueda binaria;
   - hasta 512 claves por paquete (128 hasta la sesión 13); las demás pasan al **conjunto de reintento** (256, del
     hilo de red) y cuentan como `LATE` (§11);
   - un 2005 cuyos registros dejan de cuadrar conserva las claves de los registros enteros anteriores (se cuenta como
     *untiled*).
   - El conjunto de claves lo construye el hilo de diagnóstico y lo **reconstruye** cuando cambian la generación
     (`mgr+0x1C`) o el número de la tabla (32 reconstrucciones como máximo).
3. **Por clave, `gsl::process_key`** (lógica pura; las pruebas la ejecutan con un juego simulado):

   | # | Condición | Si falla |
   |---|---|---|
   | 1 | fichero ausente (búsqueda nativa `0x186A810`, su cerrojo de lectura; desde la revisión de `b90e32a`, «descargándose» **no** cuenta como presente: §11.7) | **ya presente** |
   | 2 | no es `BadName` fijado; en `probe`, no decidida ya; menos de 3 fallos (o, sin sitio en el libro, menos de 16 fallos de claves sin seguimiento) | decidida / se rinde |
   | 3 | entrada de contexto | **sin contexto** (no se fija: se vuelve a mirar en el siguiente paquete) |
   | 4 | el nombre vuelve a dar la clave | **mal nombre** (fijado) |
   | 5 | `probe` o bloqueado a `probe` | **WOULD LOAD** |
   | 6 | tope de sesión (1024; 256 hasta la sesión 13) | tope |
   | 7 | clave del paquete: tope de tiempo del paquete (500 ms); clave del conjunto de reintento: límites de tiempo del paquete y del segundo (§11) | **aplazada** al conjunto de reintento |
   | 8 | justo antes de llamar: misma generación y número de la tabla, misma entrada, mismo `String` y hash correcto | **contexto cambiado**, al conjunto de reintento |
   | — | todo cumple | **carga** |

4. **Llamada:**
   - `0x1864140(mgr, &h, entrada+0x10, 0)` y `0x38AD90(&h)`, con `t_busy` puesto por RAII;
   - filtro SEH que apaga el módulo, escribe una línea y **deja pasar** la excepción;
   - después, búsqueda nativa: con handle y fichero → `LOADED`; si no → `FAILED` (3 intentos, mientras tanto en el
     conjunto de reintento).
5. **Después de las claves del paquete**, hasta 8 claves del conjunto de reintento, con los mismos pasos.
6. **Código verificado antes de llamar:** FNV-1a de los cuerpos enteros, todo dentro de `.rsrc`:

   | Función | FNV-1a |
   |---|---|
   | `0x1864140` | `0xE06FA55A0E6EA986` |
   | `0x18643B0` | `0x90E1F0C9AAC28DB5` |
   | `0x186A810` | `0x8D729B4530462A50` |
   | `0x38AD90–0x38ADEC` | `0x9F799744CF2F1875` |

7. **Límites** (`gsl::k*`), **comprobados antes de cada carga.** Desde la sesión 13 son de **tiempo**, no de número,
   y nunca aplazan una clave que nombra el paquete que se va a entregar (§11):
   - claves del paquete: 500 ms de cargas en ese paquete y, desde la revisión de `b90e32a` (§11.7), 500 ms en ese
     segundo;
   - claves del conjunto de reintento: 20 ms por paquete y 200 ms por segundo;
   - por sesión: 1024 cargas.

   *Hasta la sesión 13:* 16 cargas y 120 ms por paquete, 32 cargas y 200 ms por segundo, 256 por sesión.

   **Cota real:** una carga no se puede interrumpir, así que el peor caso es el tope más una carga.
8. **Histograma y bloqueo:**
   - tiempos `< 1 / 5 / 20 / 50 / 100 / 250 ms` y más, el máximo y el total en el resumen; «lentas» > 50 ms;
   - **una sola carga de más de 250 ms** pasa el módulo a `probe` para el resto de la sesión, con una línea ERR
     `GuestSchedLoad [LATCH]`. Lo ya cargado se queda.

### 3.3 Verificación (ganchos de solo lectura)

- **Ganchos intermedios** en la entrada de:
  - `0x1869D70` (`mov [rsp+0x10],rdx`, 5 B; `rdx = recurso | nodo << 32`; bloque de IA del 2005 y 2874; gestor
    `[0x6C8CDA0]`);
  - `0x1862A80` (`mov [rsp+0x18],r8d`, 5 B; `r8d` = clave, `rcx` = gestor; 2335 por `0xA9FEE0` y otras búsquedas).
- **Coste:**
  - cada llamada se cuenta con una suma relajada (llamadas por segundo en el resumen);
  - el camino completo (byte TLS, réplica, libro) se hace **1 de cada 8 llamadas por hilo**, y siempre para una clave
    cargada por nosotros que aún no se ha visto encontrada.
- **Qué hacen:**
  - releen el contenedor con la réplica sin cerrojo: es diagnóstico y una lectura rota solo cuenta mal;
  - «descargándose» cuenta como encontrado;
  - cuentan `found` / `missing`;
  - **`found after our load`** (la verificación) y `missing after our load` (debería ser 0);
  - primer fallo muestreado de cada clave: una línea `[lookup miss]`.
- **Durante nuestras llamadas** (`t_busy`), las búsquedas de ese hilo no se cuentan.

### 3.4 Líneas (hilo de diagnóstico, cada 5 s; resumen cada 60 s)

| Línea | Cuándo | Contenido |
|---|---|---|
| `GuestSchedLoad ON|PROBE …` | al instalar | cuerpos verificados o rechazados; ganchos 2/2; límites y bloqueo |
| `GuestSchedLoad [context]` | al construir o reconstruir el conjunto de claves | generación y número antes y después; muestra de nombres |
| `GuestSchedLoad [state]` | al cambiar, y cada 60 s | gestor, vtable, gestores del cliente y del servidor, contenedor, `fs`, generación y número del contexto, «ready for loads» |
| `[would load]` / `[load]` | por clave | clave, nombre, paquete o conjunto de reintento, tiempo, resultado, intentos, ficheros |
| `[not loaded]` | por clave (con límite de líneas) | puerta / sin contexto / mal nombre / límite / tope / se rinde / contexto cambiado |
| `[LATCH]` (ERR) | una vez | la carga lenta que pasó el módulo a `probe` |
| `[lookup miss]`, `[thread]` | por clave / por hilo | primer fallo muestreado; hilo de red con TLS `+0x1EC` distinto de 0 |
| `GuestSchedLoad summary` | cada 60 s | paquetes (y *untiled*), claves nombradas y desbordadas, decisiones por motivo, cargas, **histograma**, conjunto de reintento, presentes descargándose, libro y libro de desbordamiento, búsquedas por segundo, **VERIFY**, claves con fallos |

También se añade un tramo a la línea de estado del invitado: `| sched load …`.

## 4. El ini: `1`

Lo estático cubre la llamada: firma, argumentos, ausencia de estado del servidor, montón, cerrojos (§1.5). De los tres
datos de ejecución:

1. **[V, autocomprobación del anfitrión]** La tabla de contexto está llena y sus nombres dan la clave: 4210/4210.
2. **[V, autocomprobación del anfitrión]** El gestor del cliente y el del servidor son `[0x6C8CDA0]`. El contenedor
   del anfitrión tenía 38 ficheros, 16 de ellos de los 25 recursos de la sesión 8. Es la misma build y el mismo
   código de arranque que en el invitado; aun así, el modo `1` vuelve a comprobar ambas cosas en el invitado y no
   carga si fallan.
3. **[E]** El tiempo de cada carga desde el hilo de red: lo dan el histograma y `[load] … ms`. Una carga de más de
   250 ms bloquea el modo a `probe`.

**Qué mirar en la prueba:**

- `[state] … ready for loads: yes`;
- `[load]` de los ~25 recursos de NPC al entrar y de los de los 2335;
- histograma con casi todo por debajo de 20 ms;
- `VERIFY: found after our load` creciendo y `missing after our load` a 0;
- en pantalla, niños, barrenderas y ovejas con su escena.

**Abortar:** cierre, tirones al entrar o al cruzar zonas, `[LATCH]`, `faults` distinto de 0 o `SWITCHED OFF` →
`guest_schedule_load=probe` (decide y escribe, no carga nada); si sigue mal, `0` (no se instala nada).

**Atajo sin Shadow (§8):** los datos 1 y 2 son del proceso cliente, que también existe en el PC del anfitrión.
`schedule_load_selfcheck=1` los mide allí, en solo lectura.

## 5. Instantánea de los 2335 anteriores a la entrada (diseño)

> **Implementado** (10-10, `observer_sched_copy`): ver `p8-impl-horarios-snapshot.md`. Diferencias con este diseño:
> el libro guarda también los 2958/2909 posteriores de cada escena (el estado, no solo la creación), y la copia en
> vivo de 2126/2958/2909 deduplica contra los nativos por payload (sesión 11: 67 de 189 × 2126 llegaron nativos).

- **El hecho [V]:** el anfitrión recibió 43 × 2335 vivos (2335 menos 2126) en su campo antes de que entrara el
  invitado. El 2335 es ToNear (le llegan al invitado solo los que se crean con él cerca); el 2126, ToTarget (solo al
  dueño).
- **Diseño (en el anfitrión, sobre el libro de `observer_seq_copy`, sin cambiar su contrato):**
  1. **Libro:** desde la captura del cliente propio (`observer_seq_copy_sniffed`, hilo escritor del *sniffer*),
     guardar por id de escena (`u64` en `+14`) los bytes del último 2335. Quitarlo con su 2126 (`u64` en `+4`) o con el
     2411 que cierre la escena. Vaciarlo en el 2174 propio, como ya se hace con los 2661.
  2. **Viaje:** en el 2174 del usuario 2 (`take_frame`, viaje de observador), copiar el libro a pendientes. Se envían
     byte a byte por `push_outgoing_extra` con la misma puerta que los 2661 (2322 propio del invitado o 30 s tras su
     2144), sin orden por actores: el 2335 no enlaza actores por clave. Un 2335 que el invitado ya recibió de forma
     nativa en este viaje (hash del payload) no se repite.
  3. **En vivo:** copiar al usuario 2 el **2126** de las escenas que tenga (copiadas o nativas). Si no, las escenas de
     horario creadas en el invitado no se borran nunca (`p8-arquitectura-observador.md` R3). Lo mismo, según se vea,
     para 2909/2958/2499/2127/2839.
  4. **Invitado:** nada nuevo. `guest_schedule_load=1` carga el fichero antes de entregar el 2335 copiado (§3.2) y el
     2335 va a M por `vf48`, como el nativo.
- **Por qué no se implementa aquí** (revisado tras la fusión con `opcion-d` `9c1ae13`; sigue sin ser pequeño ni aislado):
  - toca el libro, los viajes y la puerta de `observer_seq_copy` (en manos de otro agente). Haría falta un libro
    nuevo en `observer_seq_copy_logic.h` con sus pruebas, la instantánea por viaje, el hash anti-duplicado del nativo
    y la copia en vivo del 2126;
  - antes hay que ver con `1` qué hace el invitado con los 36 × 2335 nativos: escenas en M, C>S que generan (§6, R2).

## 6. Riesgos

| Id | Riesgo | Mitigación |
|---|---|---|
| R1 | La lectura síncrona del fichero retiene el hilo de red (latencia de entrada y salida del invitado). Los límites se comprueban **antes** de cada carga: el peor caso es la cota más una carga | Límites por paquete, segundo y sesión; histograma y máximo en el resumen; `SLOW` > 50 ms; **una carga de más de 250 ms bloquea `probe`** (ERR `[LATCH]`) |
| R2 | **Con ficheros, el invitado aplica los 2335 en M** (escenas de horario del anfitrión en su gestor propio, que sí tiene tick). Su 2126 no le llega (ToTarget), así que quedan huérfanas. M puede mandar C>S (2105, 2100…) sobre escenas `0x2000…` que el filtro del anfitrión no conoce (solo filtra ids de secuenciador) | Primera sesión con `1` y `guest_scene_diag=1` (G4 cuenta los C>S de escena del invitado). Si aparecen, filtrar en el anfitrión por ids `0x2000…` del libro de 2335 (§5) y copiar el 2126 |
| R3 | NPC creados **antes** de cargar su fichero (presupuesto agotado, o escaneo que no lo vio) mantienen el handle inválido: el bloque de IA solo se resuelve al leer el 2005 | La carga va antes de la entrega. Un `[lookup miss]` con estado distinto de `LOADED` lo señala. La clave aplazada pasa al conjunto de reintento y se carga tras las siguientes entregas, aunque ningún paquete la vuelva a nombrar. **Sesión 13:** el presupuesto ya no aplaza claves del paquete que se entrega; una que falte al entregarlo da `[LATE]` (§11) |
| R4 | El contenedor tiene 2 cubos de 31 entradas como máximo en el anfitrión (llegó a 50). Si la inserción (`.xtls`) no crece, una carga puede fallar | Se relee el contenedor tras cada carga (`FAILED`, 3 intentos) y hay tope de 256 |
| R5 | Falso positivo del escaneo: un `u32` de un registro de NPC que coincide con una clave de contexto sin serlo | Solo cargaría un fichero de horario válido de más (memoria). Probabilidad ≈ N/2³² por posición |
| R6 | Excepción dentro del cargador (cerrojo del contenedor o del `fs` tomado) | Filtro que apaga el módulo y la deja pasar: un cierre real donde ocurre, como la política I1 |
| R7 | Que el sistema de ficheros no admita el hilo de red **[E]** | El primer `[load]` lo dice. Si falla o se cuelga: `guest_schedule_load=0` |

## 7. Correcciones

| Antes | Ahora |
|---|---|
| `p8-arquitectura-observador.md` §7.5: «cargar … con `0x1864140` o su camino de carga asíncrona» | La carga asíncrona (`0x1862B60`) solo construye el índice y suelta los ficheros. La vía es `0x1864140` con el nombre de la tabla de contexto (§1) |
| `p8-arquitectura-observador.md` §2.4: «el gestor del servidor `[[0x6D69518 \| 0x6D69520]+0x40]` es el mismo objeto [E fuerte]» | Las raíces son dos objetos distintos (`0xAD0B03` y `0x27C0E85`); la igualdad de su `+0x40` sigue **[E fuerte]**, y la línea `[state]` la mide |

## 8. Autocomprobación de solo lectura (`schedule_load_selfcheck`)

| Clave | PC | Valores |
|---|---|---|
| `schedule_load_selfcheck` | anfitrión **o** invitado | `0` \| `1` |

- **Qué es:** un hilo propio (arranca en `mod.cpp` junto a `p8_probes_start`, en los dos papeles) que espera a
  `WorldSystem` y, cada 30 s, escribe al cambiar (y cada 10 min):
  - `ScheduleSelfCheck [state]`: el gestor `[exe+0x6C8CDA0]` y su vtable, el del cliente `[[0x6D69518]+0x40]` y el del
    servidor `[[0x6D69520]+0x40]` («the same» / «DIFFERENT» / «not set»), el contenedor (ficheros), `fs` y
    «ready for loads» (la misma condición que exige `guest_schedule_load=1`);
  - `ScheduleSelfCheck [context]`, una vez: claves de la tabla de contexto y cuántas, de **todas**, tienen un nombre
    que vuelve a dar la clave (lookup3 `0xC5EDE` del nombre base, como `0x1864140`). Cuatro ejemplos con nombre y,
    si las hay, las discordancias;
  - `ScheduleSelfCheck [keys]`: cuáles de los 25 recursos que le faltaron al invitado en la sesión 8
    (`0xAC00B3BB` … `0xF56759DA`) tiene el contenedor de **este** cliente; de los ausentes, cuáles tienen entrada de
    contexto (cargables) y cuáles no.
- **Garantías:** solo lecturas protegidas con SEH desde un hilo del mod (reserva solo en el montón del mod). Sin
  ganchos, sin llamadas al juego, sin escrituras. No depende de `guest_schedule_load`.
- **Qué se espera en el anfitrión:**
  - `[context]` con todos los nombres que dan su clave (comprobación 1 de §4);
  - `[state]` con el gestor del cliente igual y, con su servidor activo, el del servidor también igual (comprobación 2);
  - `[keys]` con parte de los 25 presentes una vez cargada la zona de la sesión 8 (el anfitrión llegó a 36–50
    ficheros). Apoya el diagnóstico: el cliente del anfitrión sí los tiene porque los cargó su servidor.
- **Inis:** `schedule_load_selfcheck=1` en `cdmp.p8-noche-local.ini` y `cdmp.p8-s7-host.ini`.
- **Resultado en el anfitrión (10-10, PC del usuario) [V]:**
  - `[context]`: 4210/4210 nombres que dan su clave;
  - `[state]`: gestor del cliente y del servidor iguales;
  - contenedor con 38 ficheros, 16 de ellos de los 25 recursos de la sesión 8.

  Por eso los inis de Shadow pasan a `guest_schedule_load=1` (§4).

## 9. Revisión del código (sobre `cb3bebe`)

| # | Hallazgo | Respuesta |
|---|---|---|
| 1 | Estado de cerrojos en `deliver_all`, byte de montón TLS `+0x1FD` en el hilo de red, sistema de ficheros desde otro hilo | **[V]** solo los hilos del servidor llaman de forma nativa a `0x1864140` fuera del principal. Los dos asignadores que elige `+0x1FD` admiten cualquier hilo, y la liberación entre hilos existe (`0x1447F02BC` → `0x1447EFFF4`). En `0x35BAD4` no hay cerrojo tomado: espera de Q1 soltada, cola de salida sin bloqueo, y el propio bucle llama luego a `OnRecvPacket`. **[E fuerte]** sistema de ficheros desde el hilo de red. Todo en §1.5 |
| 2 | Leer el contenedor con el cerrojo de lectura del juego; «descargándose» = presente | **Hecho.** La decisión usa la búsqueda nativa `0x186A810` (cuerpo verificado) y suelta con `0x38AD90`. Esa búsqueda devuelve 0 para un fichero que se está descargando, y entonces la réplica lo distingue y cuenta como presente (`present while being unloaded`). Efecto secundario de la búsqueda nativa: pone a 0 el contador de inactividad de la entrada, como cualquier búsqueda del cliente. Los ganchos de verificación siguen con la réplica sin cerrojo: son contadores, y tomar el cerrojo en cada búsqueda del juego costaría más |
| 3 | El documento prometía de más sobre la cota de bloqueo; medir lo lento; bloqueo a `probe` | **Hecho.** Cota corregida en §3.2 y R1. Histograma de 7 tramos, máximo y total en el resumen. Una carga de más de 250 ms pasa a `probe` para la sesión y escribe la línea ERR `[LATCH]` |
| 4 | Puertas antes de tocar el libro y los límites | **Hecho.** `gsl::early_gate` en `before_deliver` y al empezar `process_key`; prueba `test_engine_gates`: libro vacío, ni búsqueda, ni límite, ni reintento |
| 5 | Puntero a la tabla de contexto sin cerrojo ni generación | **Hecho**, más el razonamiento. La tabla tiene un contador de generación en `mgr+0x1C` (`inc` en `0x186C036` por inserción y en `0x1863190` tras un llenado entero). Justo antes de llamar se releen la generación, el número, la entrada, el `String` y el hash; si algo cambió, la clave va al conjunto de reintento. **[V]** el servidor nativo pasa el mismo puntero sin cerrojo (`0x281BA3F`): durante el juego la tabla se trata como inmutable; solo se llena al arrancar cliente o servidor (`0x1862B60`). Queda **[E]** una ventana de microsegundos entre la comprobación y la llamada |
| 6 | RAII de `t_busy` | **Hecho** (`BusyGuard`) |
| 7 | Claves sin sitio en el libro: contador de fallos y fin de los reintentos | **Hecho.** `OverflowBook` (256, del hilo de red) guarda estado e intentos. Si también se llena, un contador común: tras 16 fallos de claves sin seguimiento, se rinden. Prueba `test_engine_untracked` |
| 8 | Coste de `on_lookup` en todos los hilos del juego | **Hecho.** Una suma relajada por llamada (llamadas por segundo en el resumen). El camino completo se hace 1 de cada 8 llamadas por hilo, y siempre para verificar una clave que cargamos |
| 9 | Reconstruir el conjunto de claves al cambiar la tabla | **Hecho.** Por generación o número; 32 reconstrucciones como máximo; línea `[context] REBUILT` |
| 10 | Más de 32 claves por paquete; desbordamiento a un conjunto de reintento | **Hecho.** 128 por paquete; las demás pasan al conjunto de reintento (256, del hilo de red), que también recibe las aplazadas, las de contexto cambiado y los fallos. Tras cada entrega se procesan hasta 8 |
| 11 | Análisis parcial de un 2005 | **Hecho.** Se conservan las claves de los registros enteros anteriores a la ruptura (`ok`, `untiled` contado) |
| 12 | No fijar `NoCtx` | **Hecho.** Solo se fija `BadName`. `NoCtx` se anota una vez y se vuelve a decidir |
| 13 | FNV de todo `0x38AD90`; `g_base` una sola vez antes de los hilos | **Hecho.** FNV de `0x38AD90–0x38ADEC` (las dos salidas). `g_base` es una constante inicializada en la inicialización estática de la DLL, antes de cualquier hilo del módulo |


## 10. Sesión 12: retener el fichero hasta el 2335

10 de octubre de 2026, rama `p8-sched-load-hold` (sobre `f76ab6b`). Build **1.0.0.2976**. No se lanzó el juego.
Base: `p8-s12-npc-familias.md` §4, familia (c): 21 × 2335 con «NO stage in X», 33 NPC (el pastor de `0x20000175`
con 18 ovejas y 10 cabras).

### 10.1 El fallo [V]

- `guest_schedule_load` cargó 35 ficheros en t 44,56 (hilo de red) y soltó el handle en el acto con `0x38AD90`.
- El cliente aplazó los 2335 hasta el final de la carga del campo: su manejador corrió en t 47,80 (3,2 s después).
- Entre medias, el contenedor pasó de 35 ficheros a **15**. Sobrevivieron los que retenía un NPC presente.
- Sin fichero, `0xA9FEE0` no crea la etapa (`missing after our load 21`).

### 10.2 Cómo se retiene y cómo se suelta un fichero [V estático]

- **El fichero lleva la cuenta.** `pa::NPCScheduleData_File` (ctor `0x1860C10`, vtable `0x57DB080`, base
  `pa::IRefCounted`):
  - `+0x10`: número de referencias (`u32`);
  - `+0x14 = 1`: cuenta atómica (`word [rcx+0x14] = 1` en el ctor);
  - `+0x15`: «descargándose»;
  - `+0x16`: se pone a 1 al buscarlo **[E: «usado»]**.
- **El handle es una referencia.** Lo devuelven `0x1864140` (carga) y `0x186A810` (búsqueda: `lock inc [file+0x10]`).
  `0x38AD90` lo pone a 0 y resta 1 (`lock xadd`). Al llegar a 0 llama a `vf0(1)` (destructor y liberación).
- **El contenedor guarda su propia referencia.** Su entrada apunta a un *holder*: `+0` fichero, `+8` vida en
  segundos (`float`), `+0x10` plazo (`double`). Cada búsqueda nativa pone el plazo a 0 (`0x186A95B`).
- **El barrido de inactividad es `0x186AE50`** (método del contenedor, al que se llega desde `.xtls`, `0xCBCEA8C`).
  Por cada entrada:
  - si `[file+0x10] != 1` (alguien más tiene una referencia), **la salta** (`0x186B0FC`);
  - si el plazo es 0, lo arma: `ahora + vida × 1000` (`0x186B12A`; constante `1000.0` en `0x5D733E0`);
  - si el plazo ya pasó, la entrada es candidata (`vf20`). Luego la marca `+0x15 = 1` (`0x186B51A`) y la quita
    (`vf30`).
- **[E]** La vida de estos ficheros es de unos 3 s: encaja con 35 → 15 en menos de 3,2 s.
- **Conclusión:** la forma nativa de retener el fichero es **no soltar el handle** que ya nos da el juego. Mientras lo
  tengamos, la cuenta es ≥ 2 y el barrido no lo toca. Para soltarlo se usa la misma llamada nativa, `0x38AD90`.
- **El 2335 se ve sin gancho nuevo:**
  - `0xA9FEE0` tiene un solo llamador, `0xC0B619`, en el manejador del 2335 `0xC0AFC0`;
  - `0xA9FEE0` llama a `0x1862A80` en `0xA9FFC3` (`E8 B8 2A DC 00`), y el módulo ya engancha la entrada de
    `0x1862A80`;
  - en esa entrada, `[rsp] == 0xA9FFC8` quiere decir «este 2335 está corriendo ahora».
- **[E fuerte] La etapa retiene su fichero:**
  - `0xA9FEE0` pasa `&handle` al ctor de `ClientNPCScheduleStage`, `0x15EE420` (ofuscado: salta a `.xtls`);
  - después suelta su propio handle (`0xAA0453`);
  - en la sesión 12, el contenedor se quedó estable en 15 ficheros de 21:55 a 21:59.

### 10.3 El cambio (`guest_sched_load*`)

- **Qué se retiene.** Solo con `guest_schedule_load=1` y sin bloqueo a `probe`, las claves que nombra un 2335:
  - las que nombra ahora;
  - las que nombró antes y vuelven del conjunto de reintento (`op 0`; marca `by2335` en el libro).

  En esos casos el handle **no se suelta**:
  - si falta: el de la carga `0x1864140` (`LoadOutcome::kept`), solo si después el contenedor lo tiene (`LOADED`);
  - si ya está: el de la búsqueda nativa `0x186A810` de la comprobación de presencia.
- **Tabla acotada** (`gsl::HoldTable<128>`, solo el hilo de red): clave, fichero, desde cuándo y el último 2335 que lo
  nombró.
  - Si otro 2335 nombra una clave ya retenida, se actualiza ese último 2335 y el handle nuevo se suelta en el acto.
  - Si la tabla está llena, el handle se suelta como antes (`table full`).
- **Cuándo se suelta** (`gsl::service_holds` y `gsl::hold_due`):
  - **30 s** después de que el gancho vea el 2335 (`0x1862A80` desde `0xA9FFC8`), si llegó **después** del último
    2335 que lo nombró;
  - si no, **120 s** después de ese último 2335 (`TIMEOUT`).
  - Elegí 120 s y no 60 s porque retener solo cuesta la memoria de unos pocos ficheros, y una carga de campo lenta
    podría aplazar el 2335 más de 60 s. Las constantes están en `guest_sched_load_logic.h`.
- **Dónde y cómo se suelta:**
  - en el **hilo de red**, al principio de `guest_sched_load_before_deliver`, con cualquier paquete del anfitrión y
    como mucho una vez por segundo;
  - solo si el código está verificado, el módulo no se apagó por un fallo y el hilo es el del cliente (TLS `+0x1EC`);
  - es el mismo hilo que tomó el handle, con la misma llamada que el módulo ya hacía allí, `0x38AD90` (cuerpo
    verificado con FNV, filtro SEH, `t_busy`).
- **Antes de `0x38AD90`**, para un handle **retenido** (solo lecturas; `release_handle(h, false)`), se comprueba:
  - que el retén es de esta conexión, de este gestor y de este contenedor (§10.8);
  - vtable `0x57DB080`;
  - `+0x14 != 0`;
  - `[file+0x10] > 1`.

  Si la cuenta es 1, somos la **última** referencia: el contenedor ya lo soltó, por ejemplo con un vaciado del
  gestor. En ese caso `0x38AD90` ejecutaría el destructor en el hilo de red, así que **no se suelta**. Se pierde la
  memoria de un fichero y se cuenta (`last reference`).
- **Por qué es seguro soltar en el hilo de red:**
  - es el mismo caso que ya existía: soltar justo después de cargar;
  - la cuenta es atómica **[V]**;
  - el caso «última referencia» se evita.
- **Gancho:** ningún gancho nuevo. El de `0x1862A80` (entrada, solo lectura) lee además `[rsp]`, una lectura con
  SEH como en `remote_puppet`. Si los 5 bytes de `0xA9FFC3` no coinciden al instalar, los handles se sueltan solo
  por tiempo.
- **Un handle tomado en la misma llamada** (otro 2335 con la clave ya retenida, tabla llena, o una carga `FAILED`
  cuyo fichero no está en el contenedor) se suelta **en el acto y sin comprobar la cuenta**
  (`release_handle(h, true)`), como hacía el módulo antes de la retención. En el caso `FAILED` nuestro handle es su
  única referencia y `0x38AD90` destruye el fichero en el hilo de red. El propio cargador hace lo mismo en el hilo
  que lo llama: en su camino de error, tras la línea `0x35DE50` en `0x18646EF`, suelta el fichero nuevo y llama a
  `vf0(1)` (`0x1864705–0x1864736`) **[V]**.
- **Sin tocar `guest_sched_bind*` ni `guest_seq_route*`.** Los inis no cambian: es parte de `guest_schedule_load=1`.

### 10.4 Red de seguridad (solo registro)

- **Al cargar** (`[released early]`, contador `RELEASED BEFORE THEIR 2335 RAN`): una clave que nombró un 2335 y que el
  contenedor tenía (`LOADED`/`present`) ya no está, y desde ese 2335 el gancho no vio su búsqueda. Es decir, se soltó
  antes de que corriera su 2335. Con la retención debería ser 0, salvo `TIMEOUT`.
- **Al correr el 2335** (`[2335 lookup miss]`): la búsqueda de `0xA9FEE0` no encuentra fichero para una clave que
  nombró un paquete. Es la medida directa de la familia (c).
- **Línea `GuestSchedLoad [hold]`** tras cada resumen (cada 60 s), con:
  - retenidos ahora y el pico;
  - tomados, renombrados y tabla llena;
  - soltados tras el 2335 y por `TIMEOUT`;
  - no soltados (última referencia, fichero raro, módulo apagado);
  - búsquedas del 2335: encontradas (y de ellas, retenidas) y que faltan;
  - soltados antes de su 2335.
- **Línea `[hold end]`** por cada handle soltado.

### 10.5 Pruebas

- `test_hold_due`: los plazos (visto / no visto / búsqueda anterior al último 2335) y la tabla (añadir, renombrar,
  llena, barrido).
- `test_engine_hold` (con el stub, que cuenta las referencias de cada handle falso):
  - un 2335 carga y retiene;
  - un 2335 repetido suelta el handle nuevo;
  - un 2005 no retiene, pero el 2335 que llega después sí (por presencia);
  - el barrido se ejecuta como mucho una vez por segundo;
  - se suelta 30 s después de la búsqueda y por `TIMEOUT` a los 120 s;
  - la red de seguridad cuenta el fichero perdido tras un `TIMEOUT`, pero no el que ya había pasado su 2335;
  - la «última referencia» no se suelta;
  - una clave de 2335 que vuelve del conjunto de reintento también se retiene;
  - `probe` y el bloqueo a `probe` no retienen;
  - un handle de una carga `FAILED` se suelta en el acto, aunque sea su última referencia (el stub modela la cuenta:
    nuestras referencias más la del contenedor; el fichero se destruye).
- `test_engine_hold_full`: 131 claves → 128 retenidas, 3 soltadas en el acto; al final, todas devueltas.
- `test_engine_hold_owner` (§10.8): otra época, conexión terminada, otro gestor y otro contenedor → `DROPPED` sin
  llamada; una clave renombrada en la conexión siguiente sustituye al retén viejo (que se abandona); el módulo
  apagado y `drop_holds` no llaman a nada. La «última referencia» de un retén se prueba vaciando el contenedor del
  stub, no con un resultado forzado.

### 10.6 Riesgos

| Id | Riesgo | Mitigación |
|---|---|---|
| H1 | Que la etapa **no** retenga su fichero **[E fuerte]**: al soltarlo, el barrido lo quita (el plazo puede estar ya vencido) | La etapa ya existe para entonces: el fallo era crearla. Si hiciera falta, subir `kHoldGraceUs`. Vigilar `[2335 lookup miss]` y el número de ficheros del contenedor tras los 30 s |
| H2 | Carrera: el contenedor suelta su referencia **entre** la lectura de la cuenta (2) y `0x38AD90`; el destructor correría en el hilo de red | Solo pasaría con un vaciado del gestor (el barrido no toca ficheros con cuenta ≠ 1). Aun así sería correcto: los caminos del contenedor también sueltan por la cuenta (§10.8), así que el fichero sigue vivo hasta nuestra llamada, y la liberación entre hilos del montón existe **[V]** (§1.5). Ventana de microsegundos |
| H3 | Un aplazamiento del 2335 de más de 120 s | `TIMEOUT` en `[hold]` y `[released early]` cuando vuelva a nombrarse. Subir `kHoldMaxUs` |
| H4 | Sin paquetes del anfitrión, los handles no se sueltan (no hay otro hilo seguro donde hacerlo) | Lo normal es que lleguen varios por segundo. Al desconectarse, quedan como mucho 128 retenidos; en la conexión siguiente se **abandonan** sin llamada (§10.8), es decir, una fuga contada de como mucho 128 ficheros por desconexión |
| H5 | Memoria: hasta 128 ficheros vivos más tiempo | El anfitrión tenía 36–50 ficheros vivos a la vez. **[E]** Cada uno ocupa poco (una estructura de 0xE0 B más sus datos) |

### 10.7 Qué mirar en la siguiente prueba de dos PC

- `GuestSchedLoad [hold]`:
  - `taken` ≈ los recursos distintos que nombran los 2335;
  - `after the 2335 ran` ≈ `taken`;
  - `TIMEOUT` ≈ 0;
  - `MISSING` ≈ 0;
  - `RELEASED BEFORE THEIR 2335 RAN` = 0.
- `GuestSchedLoad summary`: `missing after our load 0`.
- `GuestSchedRoute`/`guest_scene_diag`: sin «NO stage with that key in X+0x148 after 0xA9FEE0» para `0x20000175` y el
  resto de la tabla de la familia (c).

### 10.8 Revisión de `2997dbb`: retenes ligados a la conexión y al gestor

- **Hallazgo 1 (medio-bajo):** los retenes no estaban ligados a la conexión ni al gestor.
  - `deliver_all` solo entrega los paquetes de la conexión **actual**, y el servicio corre desde ahí. Por eso los
    retenes de una sesión A se soltaban en la sesión B, quizá en otro mundo.
  - `release_handle` reconocía el fichero solo por su vtable y por `cuenta > 1`.
  - Si un vaciado del gestor liberase ficheros saltándose la cuenta, la dirección podría reutilizarse para otro
    `NPCScheduleData_File`, y restaríamos una referencia ajena.
- **Arreglo:**
  - cada retén guarda su dueño (`gsl::HoldOwner`): época de la conexión (`guest_connection_epoch()`), gestor
    (`[exe+0x6C8CDA0]`) y contenedor (`mgr+8`);
  - `service_holds` compara el dueño de cada retén con el actual, y también comprueba `guest_remote_live()`;
  - si algo cambió, o la conexión ya no juega en el anfitrión, el retén se **abandona**: sale de la tabla **sin**
    llamar a `0x38AD90`. Es una fuga contada: `DROPPED` en `[hold]` y una línea `[hold end]`;
  - una clave que se vuelve a nombrar con otro dueño mientras el retén viejo sigue en la tabla lo sustituye
    (`Replaced`): el viejo se abandona y el nuevo se retiene;
  - con el módulo apagado tras un fallo (`g_disabled`), `drop_holds` vacía la tabla sin llamar a nada.
- **«Soltarlos todos al desconectarse»:**
  - el camino de desconexión (`end_remote`) está en `guest.cpp`, fuera de este módulo, y no corre en el hilo de red;
  - la tabla es del hilo de red, que deja de llamar a este módulo al terminar la conexión;
  - por eso el abandono ocurre en la primera entrega de la conexión siguiente (época distinta), sin llamada al juego;
  - entre una conexión y otra, los retenes son solo nuestras referencias: ningún código del mod las toca.
- **Comprobación estática [V]: el contenedor nunca libera un fichero saltándose la cuenta.** Vtable de
  `ThreadSafeRefCountedContainer<u32, NPCScheduleData_File>`: `0x57DB030`. Su destructor `0x186BC50` y el de la base
  `0x186CD70` llaman a `0x186BD40` («vaciar todo»).
  - **`0x186BD40`** (10 llamadores, entre ellos `0x186AE10` y `0x2D76400`…`0x2D78BA0`): por cada entrada llama a
    `vf30`, luego a `0x38AD90(&holder->file)` (`0x186BDC3`/`0x186BE24`, la referencia del contenedor por la cuenta) y
    libera el *holder* (`0x47AA00`). Nunca libera el fichero directamente.
  - **El barrido `0x186AE50`:** marca `+0x15`, y después hace `0x38AD90(&holder->file)` y `0x47AA00(holder)` por cada
    expulsado (`0x186B626`).
  - `vf30` del contenedor es `ret 0` (`0x359B50`).
  - **Consecuencia:** mientras tengamos una referencia, el fichero no se destruye y su dirección no se reutiliza. El
    caso «el gestor lo vació» solo hace que nuestra referencia sea la última, y eso lo cubre «último → no se suelta».
  - **[E]** La limpieza `0x1862570`, que llama `0x1862B60` al arrancar, tiene el cuerpo en `.xtls` y no se puede
    leer en estático. Si no pasa por `0x186BD40`, la comprobación de dueño la cubre igual cuando el gestor o el
    contenedor cambian de dirección. Si se reutiliza el mismo contenedor, la cubre la cuenta.
- **Hallazgo 2 (bajo):** el handle de una carga `FAILED` se perdía en vez de soltarse: `cuenta = 1` daba `Kept`.
  - Se recupera el comportamiento de antes: `release(h, true)` suelta sin comprobar la cuenta (§10.3).
  - El stub de las pruebas modela ahora la cuenta: nuestras referencias más la del contenedor.
- **Ini:** `cdmp.p8-s12-shadow-seguro.ini` (plan B) avisa, solo en un comentario, de que con `guest_schedule_load=1`
  también retiene, y de que para quitarlo hay que poner `guest_schedule_load=probe`.

## 11. Sesión 13: presupuesto

### 11.1 El fallo [V]

Log del invitado `backups\p8-sesion13-shadow\cdmp_20261010-233225.log`, líneas `GuestSchedLoad`, y
`p8-s13-npc-familias.md` §4.4.

- **Resumen final:**
  - `load x85; already present x590; budget (kept for a retry) x956`;
  - `retried 945`, conjunto de reintento vacío al final;
  - 85 cargas en **21,6 ms** en total, máximo **2,83 ms**, ninguna lenta.
- **t 44,99–45,00:** llega una ráfaga de 2335 en el mismo segundo, uno por paquete. Se cargan 32 ficheros en 10 ms y
  se agota el límite de **32 cargas por segundo** (`kWindowLoads`).
- **t 45,0018:** desde ahí, todas las claves de ese segundo se aplazan:
  - `0xF56759DA` (`cd_m11_m04_npc_farm_00_0090`);
  - `0x268D4087` (`cd_seq_spawn_doc_animal_bird_medium_idle_02_schedule`);
  - `0x5E8B5B7A`, `0x8FADCDF8`, `0x54AC45AA`, `0x31BC2167` y otras.

  Cada entrega repasa además el conjunto de reintento, que también se aplaza: de ahí los 956.
- **t 45,19:** el juego lee el bloque de IA del 2005 de 13 NPC sin el fichero (`INVALID: FILE NOT LOADED`):
  - 6 ovejas `0xB010013D`–`142`, escena `0x2000018B`;
  - 7 aves `0xB0100143`–`149`, escena `0x20000192`.
- **t 45,81:** en la primera entrega del segundo siguiente, el reintento carga los dos ficheros. Es tarde: el nodo no
  se vuelve a resolver, y son los 13 «still 0» del re-bind.

**Por qué existía el presupuesto (§3.2):** la carga es síncrona y corre en el `NetworkThread` del cliente, antes de
entregar el paquete. Un disco lento podía retener ese hilo. El límite **por número** no protegía de eso: 32 cargas de
0,3 ms son 10 ms. Lo que de verdad mide el coste es el **tiempo**. Además, el límite contaba cargas de la sesión de
cualquier paquete, así que en la ráfaga de entrada dejaba sin fichero paquetes que ya se estaban entregando.

### 11.2 El cambio (`guest_sched_load*`)

- **Las claves del paquete que se va a entregar nunca se aplazan por presupuesto** (`gsl::process_named`,
  `process_packet`):
  - se cargan todas antes de la entrega, en el orden del paquete;
  - es el orden del proceso de un jugador solo: su servidor carga el fichero antes de enviar el paquete que lo nombra;
  - la única cota es un **tope de tiempo por paquete**, `kOwnPacketCapUs` = **500 ms** de cargas. Es generoso: unas 170
    veces el peor paquete de la sesión 13, y el doble del bloqueo por una sola carga (`kLatchUs`, 250 ms);
  - al llegar al tope, el resto pasa al conjunto de reintento (`Why::OwnCap`) y cuenta como `LATE`. Es un caso
    patológico (disco).
- **El conjunto de reintento** sigue para los fallos de verdad:
  - carga fallida, con 3 intentos;
  - tabla de contexto cambiada;
  - claves por encima del tope de un paquete.

  Sus claves se trabajan después de las del paquete, con límites de **tiempo**: no se empieza una si las cargas de esta
  entrega ya suman **20 ms** (`kRetryPacketUs`) o las del segundo **200 ms** (`kWindowLoadUs`). Todas las cargas,
  también las del paquete, gastan la ventana del segundo. Siguen como mucho 8 claves por entrega.
- **Límites por número que se quitan:** 16 cargas por paquete y 32 por segundo. `gsl::Budget` pasa a ser solo de tiempo.
- **Tope de sesión:** de 256 a **1024** cargas. Con 256, una sesión larga por el mundo (4210 claves de contexto,
  recargas tras el barrido) acabaría dejando sin fichero a paquetes del anfitrión.
- **Tope de claves por paquete:** de 128 a **512**. En la sesión 13 hubo 686 claves en 13234 paquetes 2005, es decir,
  unas pocas por paquete.
- **Contexto cambiado justo antes de la llamada,** en una clave del paquete: se decide otra vez en el acto, una sola
  vez (`ctx_redo`). Antes esperaba al reintento.

### 11.3 Registro: `LATE` (debe ser 0)

- **Qué cuenta como `LATE`:** en modo `1`, con las puertas abiertas y sin bloqueo a `probe`, una clave que nombra el
  paquete que se entrega y que **no** está en el contenedor en ese momento. Una clave está en el contenedor si ya estaba
  presente o si la acabamos de cargar y la búsqueda nativa la encuentra.
- **Motivo (`Why`):**
  - `OwnCap`;
  - cargada pero el contenedor no la tiene (`FAILED`);
  - sin contexto;
  - mal nombre;
  - se rinde;
  - tope de sesión;
  - contexto cambiado dos veces;
  - fallo dentro de la llamada.
- **No cuentan:** `probe`, el bloqueo, y las claves que solo vienen del conjunto de reintento (no las nombra ningún
  paquete que se esté entregando).
- **Líneas:**
  - `GuestSchedLoad [LATE]` (WRN) por clave, con recurso, nombre, paquete, motivo y estado. Van fuera de `kMaxLines`,
    con un tope propio de 200 líneas y limitador de 32 cada 5 s;
  - otra `[LATE]` por paquete con claves por encima del tope de 512;
  - `GuestSchedLoad [before delivery]`, tras cada resumen: el total `LATE` por motivo, las claves por encima del tope,
    `ctx_redo` y la entrega más larga en cargas;
  - `describe()` añade `LATE n`.

### 11.4 Pruebas (`optd_sched_load`)

- `test_budget`: `Budget` solo de tiempo (64 cargas de 1 ms caben en 100 ms) y `PacketBudget::allow_own` /
  `allow_retry`.
- `test_engine_budget_and_failures`:
  - 20 claves de un paquete y 20 paquetes más en el mismo segundo se cargan **todas**;
  - las claves del reintento (`op 0`) sí esperan por los 20 ms del paquete o por la ventana del segundo gastada.
- `test_engine_session13_burst`: 40 paquetes 2335 en el mismo segundo y después el 2005 de las 13 ovejas y aves
  (`0xF56759DA`, `0x268D4087`). Cada fichero está en el contenedor **antes** de entregar su paquete. Ningún
  `Budget` / `OwnCap`, conjunto de reintento vacío, `LATE` 0.
- `test_engine_late`: qué es `LATE` (`OwnCap`, carga fallida, sin contexto, tope de sesión, contexto cambiado dos
  veces, claves por encima del tope) y qué no (presente, puertas cerradas, `probe`, bloqueo, claves del reintento
  aplazadas); el contexto cambiado una vez se carga en el acto.
- Se adaptan `test_engine_hold` (una clave de un 2335 aplazada por `OwnCap` sigue retenida cuando se carga desde el
  reintento) y la prueba del tope de claves por paquete (`kMaxKeysPerPacket + 22`).

### 11.5 Riesgos

| Id | Riesgo | Mitigación |
|---|---|---|
| B1 | El `NetworkThread` puede quedar retenido hasta 500 ms por paquete, más una carga, si el disco se vuelve lento (antes: 120 ms) | Una carga de más de 250 ms sigue pasando el módulo a `probe` para toda la sesión. En la sesión 13, la ráfaga entera costó 10 ms. **Revisión de `b90e32a` (§11.7):** tope de 500 ms por segundo también para las claves del paquete, `probe` tras 4 cargas de más de 50 ms, y medida del tiempo del hilo por segundo (`[BLOCK]`) |
| B2 | Si el anfitrión nombra muchos ficheros nuevos en poco tiempo, el hilo de red de este lado hace el trabajo que hacía el servidor del jugador solo | Es el mismo orden y el mismo coste que en un juego local (las mismas funciones). El tope de sesión sube a 1024 |
| B3 | Más cargas (sin aplazar) dan más ficheros vivos y más retenes de 2335 | Las claves son las mismas: antes se cargaban un segundo más tarde. La tabla de retenes sigue en 128 (`table full` en `[hold]`) |
| B4 | Un fichero que nunca carga (`FAILED`) da una línea `LATE` por cada paquete que lo nombra, hasta rendirse | 3 intentos, y después `gave up` (también `LATE`, con su motivo). Tope de 200 líneas |
| B5 | Las claves por encima del tope de 512 solo se cuentan si entran nuevas al conjunto de reintento | En la práctica es 0: hubo 686 claves en toda la sesión 13 |

### 11.6 Qué mirar en la siguiente prueba de dos PC

- `GuestSchedLoad [before delivery]`:
  - `NAMED BUT NOT IN THE CONTAINER AT DELIVERY (LATE, should be 0): 0`;
  - «keys beyond the per-packet cap» 0;
  - «the longest delivery's loads» en pocos ms.
- Ninguna línea `GuestSchedLoad [LATE]`.
- `summary`:
  - `budget (kept for a retry)` cerca de 0, y nunca para una clave de un paquete;
  - `missing after our load 0`;
  - sin `keys with misses` para recursos que se nombraron antes de su lectura.
- `GuestSchedBind` / re-bind: ningún «still 0» por `FILE NOT LOADED`. En la sesión 13 fueron las 6 ovejas y las 7 aves
  de `0xF56759DA` / `0x268D4087`.
- Desde la revisión de `b90e32a` (§11.7):
  - ninguna línea `[BLOCK]` ni `[LATCH]`;
  - en `[before delivery]`, «NetworkThread time in this module: max per second» en pocos ms y «slow loads» en 0.

### 11.7 Revisión de `b90e32a`

- **M3 (medio): sin tope por segundo para las claves del paquete.**
  - Solo había un tope de 500 ms por paquete, y el bloqueo a `probe` solo saltaba con una carga de más de 250 ms. Las
    cargas de 50–249 ms no lo activaban nunca.
  - Con un disco lento (el disco en la nube de Shadow, la caché de los pak en frío), unas 85 cargas de unos 150 ms
    retendrían el `NetworkThread` varios segundos al entrar al campo. Eso retrasa los paquetes C>S del invitado,
    incluido su 2445, y alarga la carga: el anfitrión retiene ahora los NPC hasta el 2762 del invitado, con un máximo
    de 60 s. Además, los ~540 NPC que se sueltan tras el 2762 se cargarían ya en juego.
  - **Arreglo:**
    - las claves del paquete tienen ahora también un tope de **500 ms de cargas por segundo** (`kOwnWindowLoadUs`).
      Usa la misma ventana que `gsl::Budget`, que gastan todas las cargas. Al superarlo: `Why::OwnCap`, `LATE`, y la
      clave pasa al conjunto de reintento. Así el hilo queda libre al menos la mitad de cada segundo (más una carga);
    - **`probe` tras `kSlowLatchCount` = 4 cargas de más de 50 ms** en la sesión, además de tras una sola de más de
      250 ms. La línea `[LATCH]` dice cuál de los dos casos fue. Con 85 × 150 ms, el bloqueo se queda en
      4 × 150 = 600 ms;
    - **tiempo del `NetworkThread` en el módulo** (`gsl::BlockMeter`): todo `guest_sched_load_before_deliver`
      (retenes, búsquedas y cargas), sumado por segundo. En `[before delivery]`: el máximo por segundo de la sesión y
      desde el resumen anterior, la entrega más larga y los segundos que pasaron de 250 ms. La primera vez que un
      segundo llega a 250 ms se escribe una línea `GuestSchedLoad [BLOCK]` (WRN, como mucho 4 por minuto).
  - **Coste:** con un disco lento de verdad, el módulo pasa a `probe` y los NPC siguientes se quedan sin horario,
    como en la sesión 8. Se prefiere eso a retener el hilo de red. Si `[LATCH]` aparece en Shadow con cargas de
    50–100 ms, se puede subir `kSlowLatchCount` o `kSlowLoadUs`.
- **L4 (bajo): un fichero «descargándose» contaba como presente.**
  - `present()` devuelve 2 cuando el barrido de inactividad `0x186AE50` ya marcó `+0x15` y está quitando el fichero.
    Las búsquedas del juego ya no lo encuentran. Aun así se tomaba como **ya presente**: un 2005 que lo nombrara no lo
    cargaba ni contaba como `LATE`.
  - **Arreglo:** `Why::Unloading`.
    - No se llama al cargador encima: su `find` `0x186A9A0` podría devolver el fichero que se está destruyendo, o
      insertar un segundo con la misma clave mientras la entrada vieja sigue ahí **[E]**.
    - La clave pasa al conjunto de reintento y su estado no cambia.
    - `process_packet` la decide **otra vez al final de las claves del paquete** (el barrido es corto). Si ya no está,
      se carga antes de la entrega. Si sigue descargándose, cuenta como `LATE` con su propio motivo («file being
      unloaded»), y el reintento la carga cuando desaparezca.
  - En `[before delivery]`: «files being unloaded met» y «decided again at the end of their packet».
- **Pruebas:**
  - `test_review_m3`:
    - cargas de 40 ms: el tope del segundo deja pasar 13; las otras 7 cuentan como `LATE` (`OwnCap`) y el reintento
      las retoma en el segundo siguiente;
    - cargas de 60 ms: `probe` tras la cuarta, con una sola nota `Latch`;
    - `BlockMeter`: suma por segundo, un aviso por segundo, máximos y «desde el último resumen».
  - `test_review_l4`:
    - el barrido termina entre las dos pasadas: la clave se carga antes de la entrega y no hay `LATE`;
    - sigue descargándose: `LATE` (`Unloading`), y el reintento la carga después;
    - en `probe` no cuenta.
  - `test_engine_load`: «descargándose» ya no es presente; queda en el reintento y se carga cuando desaparece.
