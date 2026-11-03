# P8 · Sesión 9: por qué `guest_schedule_load=1` dio «LOADED 0 failed 204»

10 de octubre de 2026, rama `opcion-d` (HEAD `c4995c1`). Build **1.0.0.2976**, direcciones en **RVA** (base
`0x140000000`). Solo lectura: no se tocó `src`, no se lanzó el juego, no hay commits.

**[V]** comprobado (capstone sobre el PE con `build\research\cdpe.py`, log del invitado de la sesión 9, código).
**[E]** interpretación. **[H]** hipótesis sin comprobar.

Fuentes: `backups\p8-sesion9-shadow\cdmp_packets_20261010-151331\cdmp.log` (invitado, 15:13–15:28),
`src/mod/session2/guest_sched_load.cpp`, `guest_sched_load_logic.h`, `p8-impl-horarios.md`.

## 0. Resumen

- **Las cargas no fallaron. Funcionaron todas [V].** El cargador nativo `0x1864140` cargó e insertó en el
  contenedor del invitado **68 ficheros distintos** desde el hilo de red (T3168). Lo que falló es la
  **comprobación posterior del mod**, que siempre responde «ausente».
- **Causa raíz (error del mod) [V]:** `GameEnv::present()` llama a `0x38AD90(&file)` para soltar la referencia y
  **después** mira `if (file) return 1;`. Pero `0x38AD90` pone el handle a 0 (`mov qword ptr [rax], 0` en
  `0x38AD9F`) antes de bajar la cuenta. Así que `file` siempre vale 0 al comprobarlo y `present()` devuelve 0 aunque
  la búsqueda nativa `0x186A810` haya encontrado el fichero.
- **Consecuencias:** cada clave se marca `FAILED`. Se reintenta 3 veces (68 × 3 = **204 llamadas**, el número del
  resumen). Los reintentos los responde la caché del propio cargador en 0,00 ms. Luego se rinde (`gave up` ×484) y
  gasta presupuesto por paquete (`budget` ×196). Los contadores `VERIFY` se quedan a 0, porque ninguna clave llega a
  `Loaded`.
- **Ni hilo, ni TLS, ni sistema de ficheros, ni formato del nombre tienen la culpa [V]** (§2).
- **Arreglo:** una línea en `GameEnv::present()`: guardar `file != 0` **antes** de soltar la referencia (§3).

## 1. Pruebas de que la carga funcionó [V]

| Prueba | Dónde (log del invitado) | Qué significa |
|---|---|---|
| Las 204 líneas `[load]` dicen `0x1864140 -> handle set` | todas | `0x1864140` escribe `*out` solo cuando encuentra el fichero (`0x1864218`) o lo carga (`0x18643B0`). En los fallos de lectura o deserialización deja `out = 0` (`p8-impl-horarios.md` §1.1) |
| `container files` sube 1 → 2 → … → 49 con cada primer intento | `[load] … attempts 1; container files N` y `[state] … (files N)`: 0 (15:15:13) → 12 → 35 → 49 (15:26:08) | El fichero se inserta (`0x186AB90`) en el contenedor que lee el mod (`mgr+8` → `0x37DB3D9DE80`) |
| Primer intento entre 0,01 y 14,93 ms; intentos 2 y 3 **siempre 0,00 ms** (ninguno por encima de 0,02 ms) | histograma: `<1ms 196 <5ms 5 <20ms 3` | El 2.º y el 3.º los responde la búsqueda interna del cargador (`0x186A9A0` en `0x186420F`), sin leer el disco: el fichero ya estaba en el contenedor |
| Búsquedas del propio juego en el cliente (réplica, muestreadas) | `0x1869D70`: 66 encontradas de 75; `0x1862A80`: 32 de 35 | En la sesión 8, con el contenedor vacío, no encontraban nada. Ahora el bloque de IA (2005/2874) y el 2335 **sí encuentran** los ficheros |
| `faults 0`, ningún `[LATCH]`, máximo 14,93 ms | resumen final | La llamada es segura y barata en el hilo de red |

Los 68 ficheros incluyen 16 de los 25 recursos que le faltaron al invitado en la sesión 8: `0xAC00B3BB`, `0x5FB728ED`,
`0x79AF6B66`, `0xAD5D3465`, `0x3F8972FE`, `0x6FF7E5A6`, `0xA6BD9C7B`, `0xF56759DA` (ovejas), `0x57360156`,
`0x6C4DE020`, `0xB9F63F22`, `0xB1AB5208`, `0xF47139E7`, `0xA8B92663`, `0x5A97CE56`, `0xFC6638C8`… Hay además animales
(`baseseq/animallife/…`), `sequencerspawn/…` y granjas.

## 2. El código implicado [V]

### 2.1 `0x38AD90` suelta la referencia y **anula el handle**

```
38ad94: mov rax, rcx          ; rax = &handle
38ad97: mov rcx, [rcx]        ; rcx = handle
38ad9d: je  38ade5            ; nulo: nada
38ad9f: mov qword ptr [rax], 0   ; *(&handle) = 0   <-- aquí
38ada6: cmp byte [rcx+0x14],0 ; atómica o no
...     dec [rcx+0x10]; a 0 -> vf0(1)
```

### 2.2 El mod mira el handle después de soltarlo

`src/mod/session2/guest_sched_load.cpp`, `GameEnv::present()` (líneas 252–258):

```cpp
uintptr_t file = 0;
{
    BusyGuard busy;
    if (!guarded_lookup(mv.container, &file, &key)) return 0;
    if (file && !guarded_release(&file)) return 0;   // 0x38AD90: file = 0
}
if (file) return 1;                                  // nunca se cumple
if (replica_lookup(...) == FileUnloading) ...        // solo «descargándose»
return 0;
```

- `GameEnv::load()` lo hace bien: guarda `o.got = handle != 0` **antes** de `guarded_release(&handle)`. Por eso las
  líneas dicen `handle set`.
- En `process_key` hay dos llamadas a `present()`:
  1. **Antes de cargar** (`logic.h:705`): siempre «ausente». El mod llama al cargador aunque el fichero ya esté, y el
     cargador lo devuelve de su caché.
  2. **Después de cargar** (`logic.h:763`): siempre «ausente». `state_after_load(true, false)` da `Failed`, y la clave
     va a reintentos hasta `kMaxAttempts = 3` (`gave up`).
- Las pruebas no lo detectaron: `StubEnv::present()` (`tests/optd_sched_load_tests.cpp:438`) devuelve el estado de un
  `std::map`. El código que lo une al juego (búsqueda + liberación) no tiene prueba.

### 2.3 `0x186A810` es correcta tal como la llama el mod

- Firma `(container, File** out, const u32* key)`: `r8` → `r15`, clave `[r15]` en `0x186A8D4`; `rdx` → `r14`, salida.
- Tabla: `+0x20` cubos, `+0x24` usados, `+0x30` cubos de 0x100 B, `+0x38` entradas. Es la misma disposición que
  `scd::ScheduleLayout` (`scene_diag_logic.h:398`).
- Con el fichero encontrado: `holder+0x10 = 0`, `*out = file`; si `file+0x15 != 0`, entonces `out = 0`; si no, cuenta
  `+0x10` +1 (atómica si `+0x14`) y `file+0x16 = 1`.
- Al encontrarlo, el `*out` vale el fichero **hasta que el mod lo suelta**. El fallo está en el orden del mod, no en
  esta función.

### 2.4 Lo que se sospechaba y queda descartado

| Sospecha | Veredicto |
|---|---|
| Hilo de red del cliente / TLS `+0x1EC` | Descartado [V]: `[thread]` no aparece y la puerta `client_realm` pasó. La llamada carga (ficheros insertados) desde T3168 |
| Byte TLS `+0x1FD` (montón) | Descartado [V]: el cargador lo usa para elegir la liberación de sus cadenas temporales (`0x1864309`/`0x1864372` → `0x1447F03BC` o `0x1447F02BC`), y no hubo ningún fallo |
| Sistema de ficheros `[mgr+0x90]->vf40` desde el hilo de red | Descartado [V]: primera carga en 14,93 ms; las demás, entre 0,01 y 7,7 ms; ninguna excepción |
| Formato del `String*` (`entrada+0x10`) | Descartado [V]: `0x18641B3 mov rcx,[r8]; 0x18641BA mov rcx,[rcx]`. Es el mismo doble acceso que hacen los llamadores del servidor (`0x281BA3F`). Las 4210 claves dan su hash y los ficheros se insertan bajo esa clave (la caché del cargador los encuentra) |
| Comparación con el servidor del anfitrión | Innecesaria para la causa: el anfitrión no pasa por `GameEnv::present()`. Su contenedor tenía 38 ficheros en la autocomprobación, y el del invitado llegó a 49 en esta sesión |

## 3. Arreglo concreto

`GameEnv::present()` (`guest_sched_load.cpp` ~252):

```cpp
uintptr_t file = 0;
bool got = false;
{
    BusyGuard busy;
    if (!guarded_lookup(mv.container, &file, &key)) return 0;
    got = file != 0;                                  // antes de soltar: 0x38AD90 anula el handle
    if (got && !guarded_release(&file)) return 0;
}
if (got) return 1;
```

- **Prueba que falta:** sacar la secuencia «búsqueda → guardar → soltar» a una plantilla de `guest_sched_load_logic.h`
  (`native_present(lookup, release)`). Probarla con un `release` simulado que **pone el handle a 0**, como `0x38AD90`.
  Añadir esta norma al comentario de `kRelease`: «`0x38AD90` escribe 0 en `*handle`».
- **Nada más cambia:** ni el hilo, ni los argumentos, ni la función nativa.
- **Qué esperar en la siguiente sesión con el mismo recorrido:**
  - unas 68 llamadas en vez de 204, y `LOADED ≈ 68 failed 0`;
  - `gave up` casi a 0, y muchas menos `budget` (cada reintento gastaba carga del paquete);
  - `keys: present` > 0 en las claves ya cargadas;
  - `VERIFY: found after our load` creciendo.
- **Tope de sesión:** con el error, 256 llamadas alcanzaban para unas 85 claves. Con el arreglo alcanzan para 256
  claves.

## 4. Riesgos y pendientes

| Id | Riesgo | Estado |
|---|---|---|
| S9-1 | **Expulsión por inactividad:** el número de ficheros baja a ratos (35 → 34 → 32, 40 → 38, 47 → 45). Lo que carga el mod puede descargarse si nadie lo referencia | [E] Con el arreglo, una clave `Loaded` que vuelve a faltar se recarga cuando otro paquete la nombra (`present()` va primero y `Loaded` no se fija). Gasta tope de sesión: vigilar `calls` |
| S9-2 | **Contenedor a 0 a las 15:26:13**, aún jugando. Coincide con un cambio de estado de carga (`GuestDiag loading: state 0x02 (was 0x18)`) | [E] Limpieza del mundo o del contenedor en una transición. Tras ella no hubo más `[load]` (15:26:05 fue el último) porque la sesión terminó enseguida (`connection local BROKEN`). Hay que verlo en una sesión más larga: con el arreglo, las claves se recargan cuando vuelven a nombrarse |
| S9-3 | `0x268D4087` (pájaro): 0 encontradas y 9 fallos en la réplica | [E] La primera falta (15:15:15.116) es **anterior** a su carga (15:15:15.756; la clave estaba aplazada por presupuesto). Las otras 8 no tienen hora: puede ser expulsión (S9-1). [H] que la llamen con otro gestor o nodo; `other manager 0` lo hace poco probable |
| S9-4 | La primera comprobación, siempre falsa, obligó a llamar al cargador con ficheros ya presentes | Sin daño [V]: devuelve la caché (`0x1864218`) y marca `+0x16 = 1`, como cualquier búsqueda del cliente. El arreglo lo elimina |
| S9-5 | Las métricas de presupuesto y el histograma de la sesión 9 incluyen 136 llamadas de 0,00 ms | Interpretarlas así: el coste real de E/S es el de los 68 primeros intentos (total 54,1 ms, máximo 14,93 ms) |

**Siguiente prueba (Shadow):** con el arreglo y el mismo ini (`guest_schedule_load=1`). Mirar `LOADED`, `VERIFY`, si
los ficheros sobreviven en el contenedor y, en pantalla, niños, barrenderas y ovejas con su escena (R2 de
`p8-impl-horarios.md`: C>S de escena del invitado con `guest_scene_diag=1`).
