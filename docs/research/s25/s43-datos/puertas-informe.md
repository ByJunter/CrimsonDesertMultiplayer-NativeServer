# S43 — Puertas: por qué `0x282DC80` devuelve «recurso no listo» (6CF701C)

15-10-2026. Build 1.0.0.2976, PE `0x6AB28F00`, base `0x140000000`. Investigación de solo lectura: no se compiló, no se desplegó, no se abrió el juego y no se modificó ningún worktree.
Notación: **[V]** verificado en una fuente primaria (bytes del exe, `.pdata`, desensamblado o log/captura); **[E]** fuerte por código (decompilación de Ghidra); **[H]** hipótesis; **[NV]** sin datos.
Las funciones de `.xtls` se leyeron con la decompilación «xtls orig» del proyecto de Ghidra (`cdmp-query.ps1 decomp 0x15027b1e0`…). Sirven para entender el código, nunca como punto de enganche.

## Dictamen

**La hipótesis principal (el vaciado U-1 desregistra el recurso) queda refutada para esta puerta.** La causa es nativa y propia de tener dos usuarios en un mismo campo:

1. [E] El «recurso» `0xDE7B52AA` es el hash del **nombre de un SceneLevel** de tipo 0. Su entrada en el mapa `mgr+0x80` (por campo y por nombre) solo se marca **lista** (`entrada+0x40 = 1`) en `0x2830400`. Esa función tiene una **puerta de dueño**: `[[actor+0xA0]+0x60]` tiene que ser igual a `[campo+0x180]` ([V] `0x283044D cmp [r8+0x180],eax ; jnz salida`).
2. [E] `0x2830400` solo se llama justo después de **registrar por primera vez** el SceneLevel: en el 2598 `SceneLevelLoadCompleteSyncSceneObjectListReq` (`.xtls 0x15027B1E0`) y en el Resync (`0x2836440`). Si el SceneLevel ya está registrado, ninguno de los dos hace nada: ni registra ni lo marca listo.
3. Por tanto, **si el primer cliente que avisa de que ha cargado el SceneLevel es el invitado** (no es el dueño del campo), la entrada queda registrada pero **no lista**. Cuando después llega el 2598 del anfitrión, se descarta porque el SceneLevel «ya está registrado». Todas las 2928 de los dos, también las del anfitrión, devuelven `6CF701C` hasta que la entrada desaparece.
4. [V] Las tres sesiones con captura cumplen esa secuencia exacta: el positivo de la mañana (registró el anfitrión), la S42-probe y la S38 con Shadow (registró el invitado). Detalle en §4.

Hay un segundo fallo del mismo origen, que aparecerá en cuanto se arregle el primero. [E] El borrado `0x2829E60` funciona **por nombre, sin contador de referencias y sin mirar el usuario**. Lo dispara:
- el 2834 `SceneLevelUnloadReq` de **cualquier** cliente;
- `0x2836110`, que borra **todas las entradas listas** del campo cuando el dueño hace su propia carga (`0x287D4C0` con DL≠0);
- el descargador del campo.

Así, cuando un jugador se aleja o carga, se borra el SceneLevel que el otro sigue teniendo cargado (y se destruyen sus objetos). El cliente del otro no vuelve a enviar 2598, así que sus puertas quedan rotas hasta que salga de la zona y vuelva.

## 1. Qué es `param_1` de `0x282DC80`

- [V] El llamador es `0x29C1C80` (manejador del 2928). En `0x29C200D` llama `0x282DC80([campo+0x80], &err, actor, hash, span_uuid)`. Su único otro xref es `.pdata 0x17203AA8`.
  - El campo lo da `cdmp_DeadActor_ResolveGroupOwner(actor)` (`0x28FB8B0`): es el campo del sujeto. Para el invitado en la partida del anfitrión, es el **campo del anfitrión**: el DoorTrace mide `dueno-campo 0x90100000`.
- [E] `mgr = [ServerField+0x80]` es un objeto **por campo**, no por gestor de stages ni por usuario. Es el registro de SceneLevels y SyncSceneObjects del servidor. Lo usan los manejadores 2598, 2834, 2928, 2929, Resync, TransformSyncSceneObject y ResetSpawnDistance. [NV] El nombre de la clase y su vtable no se resolvieron.

| Offset de `mgr` | Contenido |
|---|---|
| `+0x08` | objeto de bloqueo (`cdmp_Actor_Lock`) |
| `+0x80 + tipo*0x20` (tipos 0..3) | mapas por nombre de SceneLevel. **El tipo 0 = `+0x80` es el de las puertas.** El tipo se calcula en `0x2829E60` (`(tipo+4)*0x20`) y en `.xtls 0x14CB90E30` |
| `+0x84` | número de entradas del tipo 0 |
| `+0x98` | cubos (se recorren en `0x2836110`) |
| `+0x1C0` | otras tablas por nombre, actualizadas en el registro y el borrado |
| `+0x218/+0x220` | lista de UUID |
| `+0x258` | otra tabla por nombre (`600`) |
| `+0x298` | otra tabla por nombre |
| `+0x210` | puntero de vuelta al `ServerField` |

- [E] Cada entrada del mapa de tipo 0 tiene esta estructura (leída en `0x282AE40`, `0x2829E60`, `0x2830400` y `0x2836110`):

| Offset de la entrada | Contenido |
|---|---|
| clave | cadena con el nombre |
| `+0x18` | puntero a la lista de UUID (16 B cada uno) |
| `+0x20` | número de UUID |
| `+0x24` | capacidad |
| `+0x28..+0x3C` | AABB (mínimos y máximos xyz; se usa en `0x282AE40`) |
| **`+0x40`** | byte **listo** |

## 2. Quién escribe en el mapa

### Registro (crear la entrada)
- [E] El registro lo hace `.xtls 0x150271680(mgr, nombre, aabb)`; si la entrada no existe, la crea con `.xtls 0x14CB94190`. **No pone `+0x40`.**
  - Lo llama `0x2875FA0`, al final de `0x28755C0` (el cargador `SceneLevelDataLoadingInfo`).
- [E] Hay tres disparadores:
  1. **2598** (manejador `0x29C1900` → `thunk 0x282DAE0` → `.xtls 0x15027B1E0`). Pasos:
     - comprueba que el actor sea personaje (`[actor+0x88]+1 == 1`);
     - resuelve el nombre;
     - llama a `IsRegistered(mgr, nombre, tipo 0)` (`.xtls 0x14CB90E30`, consulta del mapa);
     - **solo si NO está registrado**, carga y registra (`0x28755C0`, o `0x2874E30`) y después llama a `0x2830400(mgr, actor, …)`.
  2. **Resync** (`0x29C12C0` → `0x2836440`): para cada hash, con la misma regla «solo si no existe», carga, registra y llama a `0x2830400`. Aquí sí se ve en el desensamblado: `[rsp+20]=1`, `0x283669F`.
  3. **Cargador del campo**: el objeto `[campo+0x220]`, construido en `0x2874AC0` desde `FieldManager::Initialize 0x2B06E90`. Al cargar, llama a `0x2876400` → `0x28755C0(…,1,campo)`.

### Marcar lista (`+0x40 = 1`)
- [V]/[E] Solo lo hace `0x2830400`, cuyo `.pdata` es `[0x2830400, 0x28308D7)`. Al entrar:

  ```
  0x2830432  48 8B 82 A0 00 00 00   mov rax,[rdx+0xA0]
  0x283043C  0F 84 ...              jz salida            (sin usuario)
  0x2830442  8B 40 60               mov eax,[rax+0x60]   (clave de usuario)
  0x2830447  0F 84 ...              jz salida
  0x283044D  41 39 80 80 01 00 00   cmp [r8+0x180],eax   (r8 = [rcx+0x210] = campo; dueño)
  0x2830454  0F 85 62 04 00 00      jnz salida
  ```

  - Después busca la entrada por nombre (`r9/rdi`). Si existe y `+0x40 == 0`, la pone a 1 y activa sus objetos:
    - pone `obj+0xA5 = 1`;
    - quita el bit 3 de `obj+0x248`;
    - si `param_5` está activo, llama a `0x282C3B0`, que envía la sincronización a todo el campo (`ToFieldSendTargetFinder`).
  - [V] Los 40 bytes `0x2830432..0x283045A` aparecen **una sola vez** en el exe.
  - [V] La regla de dueño es la misma que ya documentan `docs/research/noche/control-npc-lejos.md` y `p8-s12-tenderos-servidor.md`: `[[actor+0xA0]+0x60]` es el usuario dueño y `[campo+0x180]` el dueño del campo.

### Borrado (quitar la entrada y destruir sus objetos)
- [E] El borrado es `0x2829E60(mgr, &nombre, &tipo, …)`, con `.pdata` `[0x2829E60, 0x282A9CA)`. Pasos:
  - comprueba `IsRegistered`;
  - vacía la lista de UUID y borra la entrada (`thunk 0x1493C2720`);
  - borra de `+0x218`, `+0x1C0`, `+0x258` y `+0x298`;
  - destruye los objetos de escena (vf `+0x2B0`, `+0x2C0`, `+0x2C8`).
  - **No hay contador de referencias ni comprobación de usuario.**
- Sus llamadores, con el retorno de cada CALL:

  | Llamada (retorno) | Origen | Tipo |
  |---|---|---|
  | `0x29C3044` (ret `0x29C3049`) | **2834 `SceneLevelUnloadReq`** de **cualquier cliente**. Formato del paquete: `u32≠0, u8 tipo(0..3), u32 hash, u8 flag` | el del paquete |
  | `0x28363CF` (ret `0x28363D4`) | **`0x2836110`**: recorre todo el tipo 0 y borra **cada entrada con `+0x40≠0`**. Lo llama `0x287BCE0` si su `param_5≠0`, y a este `0x287D4C0` (`0x287D954`). [V] `param_5 = [rbp+0x1008]`, que es el DL de entrada de `0x287D4C0` (guardado en `0x287D4C5`) | 0 |
  | `0x287645B` | `0x2876420`, evento de descarga del cargador del campo (si `DAT_146D07CB8==0`) | 0 |
  | `0x2877140` | `0x2876E50` ← `0x2879440` | 1 |
  | `0x281CAD7` | — | 3 |
  | `0x2A9E723` | — | 2 |

- Respuestas a las preguntas del encargo:
  - **No hay contador de referencias.** El registro y el borrado son por **nombre** dentro del campo: el mismo SceneLevel lo comparten todos los usuarios del campo.
  - No se registra al preparar stages (`0x287C200`/2661/2335). Se registra por la carga de SceneLevels que avisa el cliente (2598/Resync) o por el cargador del campo.

## 3. La hipótesis del encargo, frente al código

- **«El vaciado U-1 desregistra la entrada compartida»: refutada para esta puerta.**
  - [V] U-1 llama `0x287DA50(mgr, 0, 0)` y `0x287D4C0(mgr, 0, 0)`: ver los logs `[owner load] 0x287DA50(mgr, 0, 0)` (`host.log:71181` y `77371`) y `user2_stages.cpp:2635/2643`.
  - [V] Con DL=0, `0x287BCE0` no llama a `0x2836110`.
  - [H] Lo mismo vale para el 2043 inmediato que provoca `0x287DA50(…,0,0)`, si su DL viene de ahí.
  - Además, `0x2836110` solo borra entradas **listas**, y la de la taberna nunca estuvo lista en la ventana del fallo. Ni siquiera un vaciado con DL=1 explicaría el rechazo.
- **«El gestor del usuario 2 roba el registro» / «la entrada está ligada a una stage cuyo dueño cambió»: refutadas.** La entrada no pertenece a ningún gestor de stages. Pertenece al registro del campo y se identifica por nombre.
- **Lo que sí ocurre** [E]+[V]: el primero en registrar es el invitado (sin listo), y el registro bloquea todo 2598 posterior (§4).
- **Lo que sí existe y conviene vigilar** [E]: `0x287D4C0(mgr del dueño, DL=1)`, la carga nativa del propio anfitrión, borra todas las entradas listas del campo.
  - [V] Se ve en `host.log:76393` (17:14:31.890): `[owner load] 0x287DA50(mgr, 1, 0)` sobre el gestor del anfitrión.
  - Hoy no afecta a la taberna porque no estaba lista. **Con el arreglo A sí podría afectar a las zonas del invitado** (§5).

## 4. Cronologías con captura

Los tiempos se convierten a hora de reloj con las anclas `capture t=` de los logs:
- s42 anfitrión: origen 15:52:55.361 (`host.log` 15:52:58.807 = t 3.446);
- s42 invitado: origen 15:53:10.495 (`guest.log` 15:53:15.781 = t 5.286; mismo PC);
- s38: anfitrión origen 12:55:55.282. Shadow origen 11:59:22.997 + 3600.16 s. El desfase de 3600.16 s se calculó con «Steam: session … connected»: anfitrión `cdmp_20261015-125527.log:4611` 12:59:10.721, Shadow `shadow/cdmp_20261015-115835.log:204` 11:59:10.564.

### Positivo (mañana, `p8-2inst-pose-anfitrion`) [V]
| Hora / t | Quién | Evento | Fuente |
|---|---|---|---|
| t 66.608 | anfitrión | **primer** 2598 `DE7B52AA` → se registra y la puerta de dueño deja marcarlo listo | `cdmp_packets.host_20261015-083504.log:61551` |
| t 70.071 | anfitrión | 2928 → Ack2145 + 2005 de la puerta (informe S42) | `…host….log:102104` |
| t 121.113 | invitado | 2598 → ya registrado: no hace nada | `cdmp_packets.guest_20261015-083521.log:7567` |

### S42-probe (hoy, `p8-2inst-s42-probe`) [V]
| Hora | Quién | Evento | Fuente |
|---|---|---|---|
| 17:07:29.0 | invitado | **primer** 2598 `DE7B52AA` (anfitrión a 61 celdas) → se **registra sin listo** | `cdmp_packets.guest_20261015-155255.log:7674` (t 4458.501) |
| 17:07:31.75 | invitado | 2928 → `6CF701C` (y 5 más hasta 17:07:58.97) | `host.log:18073`… `26426` |
| 17:07:33.68 | — | U-1 `[ARMED]` (después del registro) | `host.log:18141` |
| 17:08:12.97 | invitado | **2834** tipo 0 `DE7B52AA` → `0x2829E60` **borra** la entrada | `…guest….log:75873` (t 4502.479) |
| 17:13:50.19 | — | U-1 `[owner load] (mgr,0,0)` + `[DRAINED now]` sobre el gestor del invitado (DL=0) | `host.log:71181`, `71241` |
| 17:13:53.24 | invitado | 2598 → no existía, así que se **registra otra vez sin listo** | `…guest….log:458791` (t 4842.741) |
| 17:13:55–17:14:36 | invitado | 2928 ×10 → `6CF701C` | `host.log:71454`… `76782` |
| 17:14:31.89 | anfitrión | su carga nativa `0x287DA50(mgr, 1, 0)` + `[unload]` (puede borrar las listas; la taberna no lo estaba) | `host.log:76393-76394` |
| 17:14:34.95 | anfitrión | 2598 `DE7B52AA` → **ya registrado: se descarta**, no se marca listo | `cdmp_packets.host_20261015-155239.log:577778` (t 4899.591) |
| 17:14:37.6–.82 | — | U-1 `[drain at host prepare]`, `[DRAINED now]` (DL=0) | `host.log:77370`, `77481` |
| 17:14:38.9–17:15:11 | anfitrión e invitado | 2928 → `6CF701C` para los dos | `host.log:78235`… `81929` |

### S38 con Shadow (`p8-sesion38`) [V]
| Hora (reloj del anfitrión) | Quién | Evento | Fuente |
|---|---|---|---|
| 13:00:41.8 | Shadow | **primer** 2598 → registro sin listo | `shadow/cdmp_packets_20261015-115854.log:6909` (t 78.651) |
| 13:00:42.36 | — | U-1 `[ARMED]` | `cdmp_20261015-125527.log:7437` |
| 13:01:23.9 | Shadow | 2834 tipo 0 → borrado | `shadow/…log:68250` (t 120.706) |
| 13:01:28.4 | Shadow | 2598 → registro sin listo | `shadow/…log:74981` (t 125.202) |
| 13:02:05.9 | Shadow | primera 2928 → Nak | `shadow/…log:125748` |
| 13:02:42.40 | anfitrión | 2598 → **ya registrado: se descarta** | `cdmp_packets_20261015-125539.log:278088` (t 407.117) |
| 13:02:45.05 | — | U-1 `[drain at host prepare]` (después, y con DL=0) | `cdmp_20261015-125527.log:33073` |
| 13:02:45.87 | anfitrión | 2928 → Nak (39 Nak de las 39) | `…125539.log:315988` |

- [NV] `p8-2inst-prueba1` (14-10) no tiene `.bin` ni DoorTrace, así que no se puede reconstruir el orden de los 2598.
- [V] En S42-probe nadie del lado del anfitrión envió 2834 para este hash. El invitado envió 1 en S42 y Shadow 2 en S38.

## 5. Arreglo de raíz propuesto

Una clave nueva `[session2] scene_level_share = 0 | probe | 1`, solo en el anfitrión y apagada por defecto. Las dos piezas usan puntos `.rsrc` ya verificados ([V] con `.pdata` y bytes leídos del exe; el script está en `scratchpad\py\pecheck.py`). No se fuerza ninguna apertura, no se crean entidades y no se llaman funciones de creación.

### A. «Listo» también cuando el que carga es el usuario 2 (la causa del caso medido)
- **Punto:** `safetyhook::create_mid` en **`0x283044D`**. Dentro de `0x2830400`, `.pdata [0x2830400,0x28308D7)`.
  - Bytes `41 39 80 80 01 00 00` (7 B, sin RIP-relativo), seguidos de `0F 85 62 04 00 00`.
  - Firma única de 40 B: `48 8B 82 A0 00 00 00 48 85 C0 0F 84 7A 04 00 00 8B 40 60 85 C0 0F 84 6F 04 00 00 41 39 80 80 01 00 00 0F 85 62 04 00 00`, empezando en `0x2830432`.
  - Es el mismo patrón que U-a `0x288B87A`, ya en uso.
- **Contexto:** `rcx/r13` = mgr, `rdx` = actor, `r8` = campo, `eax` = usuario del actor, `rdi/r9` = &nombre.
- **Condición (todas a la vez):**
  - el campo pasa `user2_stages_host_field(r8)`, es decir, `[r8+0x180]` es la clave del anfitrión;
  - `eax` es la clave viva del usuario 2;
  - `rdx` es su personaje principal;
  - hay una Troc del usuario 2 en curso (subject = `rdx`);
  - el opcode es 2598 o Resync, según el desvío ExecuteTroc de `user2_funnels`.
- **Efecto en modo 1:** `eax := [r8+0x180]` (solo un registro).
- **Efecto en probe:** línea `[would ready]` con el nombre (lectura protegida de `[[rdi]]`, ≤128 B), el hash del paquete y el usuario.
- **Resultado:** el 2598 del invitado marca la entrada lista igual que el del dueño, y se activan los objetos con la sincronización nativa de `0x282C3B0`. A partir de ahí, el orden de llegada da igual.
- **Riesgo:**
  - es una puerta de dueño de las que el mod ya relaja para U-a;
  - el resto de la ruta 2928 (`0x28308E0`, `0x2831070`, `0x2AF4430`) no tiene más comprobaciones de `+0x180` ([E], grep sin resultados);
  - hay una consecuencia nueva: las entradas listas por el invitado pasan a ser candidatas del borrado masivo `0x2836110` cuando el anfitrión hace su carga con DL=1. Eso lo cubre B.

### B. No borrar un SceneLevel que el otro usuario sigue teniendo cargado
- **Punto:** gancho inline en la **entrada de `0x2829E60`**, `.pdata [0x2829E60,0x282A9CA)`.
  - Prólogo `44 88 4C 24 20 | 4C 89 44 24 18 | 48 89 54 24 10 | 55`: el primer `mov [rsp+20],r9b` ocupa justo 5 B.
  - Primeros 16 B: `44 88 4C 24 20 4C 89 44 24 18 48 89 54 24 10 55`.
  - Este único punto cubre todos los caminos de borrado: el 2834 de cualquiera, el `0x2836110` de la carga del anfitrión y el descargador.
- **Tabla de seguimiento:** por usuario de la sesión, el conjunto de hashes de tipo 0 que su cliente tiene cargados. Se alimenta con la entrada del servidor (el gancho OnRecvPacket que ya existe en `server.cpp`):
  - 2598 `[u32][hash]` añade el hash;
  - 2834 `[u32][u8 tipo][u32 hash][u8]` con tipo 0 lo quita;
  - la carga o el cambio de campo de un usuario (su 2283/2001), el cierre de sesión o la salida del campo del anfitrión vacían su conjunto: es lo prudente, porque así se vuelve al comportamiento nativo.
- **Decisión en modo 1:** se omite el original (y se vuelve sin efecto, como hace la rama nativa «no registrado») solo si se cumplen todas estas condiciones:
  - `*param_3 == 0` (tipo 0);
  - el campo es del anfitrión y el usuario 2 está en él;
  - el nombre se traduce a un hash conocido;
  - **otro** usuario distinto del que provoca el borrado tiene ese hash cargado. El que provoca el borrado es:
    - el sujeto de la Troc 2834 en curso;
    - el anfitrión, si la llamada viene de `0x2836110` (retorno `0x28363D4`) dentro de su propia carga;
    - nadie, si viene del cargador (retorno `0x2876460`).

  Cuando el último usuario que lo tenía cargado envía su 2834, el borrado pasa con normalidad. Si falta cualquier dato, se ejecuta el original.
- **Traducción nombre→hash:**
  - [H] La cadena guarda su `cdmp_HashLittle` (`0x1364780`) en `[*param_2]+0xC`; `0x2829E60` lo usa en `FUN_141EF8DB0`.
  - [NV] Hay que **medir en probe** si coincide con el hash del paquete (`0xDE7B52AA`). Si no coincide, se aprende la relación en A (Troc 2598 con hash ↔ nombre de `rdi`) y en este mismo gancho cuando la llamada viene del 2834 (hash del paquete ↔ nombre de `param_2`).
- **Riesgo:** con B activo, el SceneLevel sigue registrado y sus objetos de servidor siguen vivos mientras un cliente lo tenga cargado. Es lo correcto para una entidad compartida.
  - El caso que puede quedar sin borrar es el de un cliente que descarga sin enviar 2834 (carga con pantalla negra): lo cubre el vaciado al cargar.
  - Si el conjunto queda vacío, B nunca bloquea.

### Por qué no hay otras opciones
- No se puede volver a llamar a `0x2830400` desde el 2598 del anfitrión. La rama «ya registrado» está en `.xtls 0x15027B1E0`, y llamar a `0x2830400` desde fuera exige construir el envoltorio del nombre.
- Tampoco se fuerza `+0x40` en memoria: sería escribir estado sin la activación nativa de los objetos.
- Cambiar U-1 no arregla nada, porque no es la causa.

## 6. Sonda previa (scene_level_share=probe) y prueba

Todo es de solo lectura, con lecturas SEH y anillo sin bloqueo, como en DoorTrace.

1. **A en probe** (`0x283044D`): cada rechazo de dueño. Registra:
   - el actor (`+0x60`), `eax`, `[r8+0x180]`;
   - el nombre (`[[rdi]]`), el hash cacheado `[[rdi]+0xC]`;
   - el opcode y el hash de la Troc en curso.

   Esperado: en la primera llegada del invitado a la taberna, `[would ready] DE7B52AA` y el usuario 2.
2. **B en probe** (entrada de `0x2829E60`): cada borrado. Registra:
   - el retorno: `0x29C3049` = 2834, `0x28363D4` = carga del dueño, `0x2876460` = cargador, otros;
   - el tipo, el nombre, el hash cacheado;
   - registrado/listo antes del borrado: lectura del cubo (si no se puede leer con seguridad, se llama a `cdmp_HashMap_Find 0x612620`, que es pura, y solo en probe);
   - los usuarios que lo tienen cargado según la tabla, y `[would keep]`.
3. **Entrada del 2598 `0x29C1900`** (`.pdata [0x29C1900,0x29C1C75)`, prólogo `48 89 5C 24 08`): usuario, hash y estado antes y después (existe/listo). Esta tercera sonda es opcional, porque las dos primeras y DoorTrace ya bastan.

**Prueba** (dos copias en un PC, la misma partida; `door_trace=probe`, `user2_stages` como hoy):
- **(a)** El invitado llega primero a la taberna, con el anfitrión lejos. Se espera `[would ready]` y un DoorTrace `recurso-no-listo`.
- **(b)** Llega el anfitrión. Se espera que su 2598 no deje línea A (ya registrado) y que siga fallando.
- **(c)** Repetir con el anfitrión primero: abre.
- **(d)** Con `scene_level_share=1`:
  - el caso (a) tiene que abrir para los dos;
  - después, el anfitrión se aleja o hace una carga (viaje rápido) mientras el invitado sigue en la taberna: B tiene que mostrar `[kept]` y la puerta del invitado tiene que seguir abriendo;
  - al revés también: el invitado se aleja y la del anfitrión sigue abriendo.

**Criterios:**
- Éxito: DoorTrace `exito` + Ack2145 en todos los órdenes de llegada, sin familias duplicadas y sin `[kept]` que se queden sin borrar cuando ninguno de los dos sigue cerca.
- Abortar ante cualquier cierre, ante una fuga de entradas que crezca sin límite o si el hash cacheado no se puede relacionar con el del paquete. En ese último caso, B sigue en probe.

## 7. RVAs (build 2976)

`0x282DC80` 2928 núcleo · `0x29C1C80` manejador 2928 · `0x29C1900` manejador 2598 → `0x282DAE0` thunk → `.xtls 0x15027B1E0` · `0x29C2D50` manejador 2834 · `0x29C12C0`/`0x2836440` Resync · **`0x2830400` marcar listo (puerta de dueño `0x283044D`)** · `0x28755C0`/`0x2875FA0` cargar y registrar → `.xtls 0x150271680` → `.xtls 0x14CB94190` · `.xtls 0x14CB90E30` IsRegistered · **`0x2829E60` borrado por nombre** · `0x2836110` borrado de todas las listas ← `0x287BCE0` ← `0x287D4C0` (DL) · `0x2874AC0` cargador del campo (eventos `0x2876400`/`0x2876420`) · `0x282C3B0` sincronización al campo · `cdmp_HashMap_Find 0x612620` · `cdmp_HashLittle 0x1364780` · `cdmp_DeadActor_ResolveGroupOwner 0x28FB8B0`.
Decompilaciones nuevas en `D:\RE\CrimsonDesert_2976\exports\decomp\` (`0x2830400.c`, `0x2829e60.c`, `0x2836110.c`, `0x1027b1e0.c`, `0x10271680.c`, `0xcb90e30.c`…) y copias en `scratchpad\dec\`.
