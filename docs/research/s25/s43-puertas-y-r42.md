# S43 — Puertas (`scene_level_share`) y motivo 42

15-10-2026. Build 1.0.0.2976 (PE `0x6AB28F00`, base `0x140000000`). Rama `opcion-d-enemigos`.
**Estado: implementado y con tests; NO probado en el juego.** Nada de esto se ha desplegado. Incluye las correcciones
de la revisión independiente de `a4c2661` (P2-1, P2-2, P2-4 y los P3; ver §10).

Notación: **[V]** verificado en una fuente primaria (bytes del exe, `.pdata`, desensamblado, log o captura); **[E]**
fuerte por código (decompilación); **[H]** hipótesis; **[NV]** sin datos.

Informes completos copiados aquí:
- causa de las puertas: [`s43-datos/puertas-informe.md`](s43-datos/puertas-informe.md);
- motivo 42: [`s43-datos/motivo42-informe.md`](s43-datos/motivo42-informe.md).

Las rutas `scratchpad\...` que citan son de la carpeta temporal de la sesión de investigación; las decompilaciones
están también en `D:\RE\CrimsonDesert_2976\exports\decomp\`.

## 1. Causa (resumen del informe, con pruebas)

La puerta de la taberna de Hernand (recurso `0xDE7B52AA`, escena candidata `4fb9eeab…`, ver
[S42](s42-enemigos-puertas.md)) contesta siempre «recurso no listo» (`0x6CF701C`) a los dos jugadores. La causa es
**nativa** y propia de tener dos usuarios en un mismo campo. No es el vaciado de U-1: queda refutado (§3 del informe;
U-1 usa DL=0 y `0x2836110` solo borra entradas listas).

1. [E] `0xDE7B52AA` es el hash del **nombre de un SceneLevel** de tipo 0. El registro de SceneLevels es **por campo**
   (`[ServerField+0x80]`), con mapas por nombre en `+0x80+tipo*0x20`. Cada entrada tiene en `+0x40` el byte «listo».
2. [V]/[E] Solo `0x2830400` pone `+0x40 = 1`, y tiene una **puerta de dueño** en `0x283044D`
   (`41 39 80 80 01 00 00` = `cmp [r8+0x180],eax`, seguido de `jnz` a la salida). Solo pasa si el que carga es el
   dueño del campo (el anfitrión).
3. [E] `0x2830400` solo se llama justo después del **primer** registro del SceneLevel: en el 2598
   `SceneLevelLoadCompleteSyncSceneObjectListReq` (`.xtls 0x15027B1E0`) y en el Resync 2104 (`0x2836440`). Si el nivel
   ya está registrado, ninguno de los dos hace nada.
4. Por tanto, **si el invitado avisa primero** de que ha cargado el nivel, la entrada queda registrada **sin listo**,
   y el 2598 posterior del anfitrión se descarta («ya registrado»). Todas las 2928 (`0x29C1C80` → `0x282DC80`) de los
   dos dan `0x6CF701C` hasta que la entrada desaparezca.
5. [V] Las tres sesiones con captura cumplen esa secuencia:

   | Sesión | Primer 2598 de `DE7B52AA` | Resultado |
   |---|---|---|
   | `p8-2inst-pose-anfitrion` (mañana) | anfitrión, t 66.608 | 2928 → Ack2145: abre |
   | `p8-2inst-s42-probe` | invitado, 17:07:29.0 (anfitrión a 61 celdas) | 2928 → `6CF701C`; el 2598 del anfitrión a las 17:14:34.95 se descarta |
   | `p8-sesion38` (Shadow) | Shadow, 13:00:41.8 | 39 Nak de 39; el 2598 del anfitrión a las 13:02:42.40 se descarta |

Segundo fallo del mismo origen, que aparece en cuanto se arregla el primero: [E] el borrado `0x2829E60` funciona
**por nombre**, sin contador de referencias y sin mirar el usuario. Lo disparan el 2834 `SceneLevelUnloadReq` de
**cualquier** cliente, `0x2836110` (todas las entradas listas, en la carga nativa del dueño con DL≠0) y el cargador del
campo (`0x2876420`). Cuando un jugador se aleja o carga, se borra el nivel que el otro sigue usando, y el cliente del
otro no vuelve a enviar 2598.

## 2. Diseño

Clave `[session2] scene_level_share = 0 | probe | 1`, **solo anfitrión**, por defecto `0`. Con `0` no se arranca el
hilo ni se instala ningún gancho: exactamente lo de antes. En el invitado cualquier valor solo deja una línea.
Código: `src/mod/session2/scene_level_share.{h,cpp}` y la lógica pura en `scene_level_share_logic.h`.

### A. «Listo» también cuando carga el usuario 2

- **Punto:** `safetyhook::create_mid` en `0x283044D` (vía `p8_install_mid`), dentro de `0x2830400`.
- **Verificación antes de instalar** (si algo falla: WRN, no se instala y A queda apagada):
  - base `0x140000000` y TimeDateStamp `0x6AB28F00`;
  - entrada de `.pdata` `[0x2830400, 0x28308D7)`;
  - la firma de 40 B en `0x2830432` (`48 8B 82 A0 00 00 00 … 41 39 80 80 01 00 00 0F 85 62 04 00 00`). [V] Aparece
    **una sola vez** en el exe (comprobado sobre el fichero);
  - el parche son exactamente los 7 B `41 39 80 80 01 00 00` (dentro de `.rsrc`; safetyhook tiene que reubicar justo
    esos 7 B). [V] Ninguna rama de `0x2830400` salta dentro de `0x283044E..0x2830453` (capstone sobre la función).
- **Contexto** [V] (desensamblado): `r13 = rcx` = registro, `rdx` = actor, `r8 = [rcx+0x210]` = campo,
  `eax = [[actor+0xA0]+0x60]` (clave de usuario), `rdi = r9` = &nombre.
- **Condición** (todas; lecturas protegidas; `scene_level_share_logic.h decide_gate`):
  - `eax ≠ [r8+0x180]` (si son iguales es el pase nativo del dueño: `[owner native]`);
  - el usuario 2 está conectado y se conocen las dos claves de usuario (las publica el hilo del módulo cada 100 ms);
  - `r8` es un `ServerField` (vtable `0x5B29A60`) y `[r8+0x180]` es la clave del anfitrión;
  - `eax` es la clave de usuario del usuario 2;
  - hay una Troc del usuario 2 en curso en este hilo (`user2_funnels_subject_now`), su sujeto es `rdx` (así `rdx` es
    su personaje principal, comprobado por el gancho de sujeto: clave, controlador y vtable) y su opcode es **2598** o
    **2104**. [V] 2104 es `ResyncSyncSceneObjectDataForReloadReq`, manejador `0x29C12C0` → `0x2836440`
    (`docs/research/data/troc_messages_2976.csv`).
- **probe:** cuenta y registra `[would ready]`. **1:** además `eax := [r8+0x180]` (un solo registro, sin escribir
  memoria del juego) y registra `[ready for user 2]`. El código nativo hace el resto: pone listo y activa los objetos
  (`obj+0xA5 = 1`, quita el bit 3 de `obj+0x248`). Solo **sincroniza al campo** (`0x282C3B0`, `ToFieldSendTargetFinder`)
  cuando `param_5 ≠ 0`, y eso pasa [V] solo en el Resync (`0x2836690`, `param_5 = 1`). En el 2598 `param_5 = 0`
  (`.xtls 0x1027B347 mov byte [rsp+20h],0`): no sincroniza.

### B. No borrar un SceneLevel que otro usuario sigue teniendo cargado

- **Punto:** gancho inline en la entrada de `0x2829E60`. Verificación: build, `.pdata` `[0x2829E60, 0x282A9CA)`, los
  16 B de entrada `44 88 4C 24 20 4C 89 44 24 18 48 89 54 24 10 55` dentro de `.rsrc`, y que safetyhook reubique
  exactamente los 5 B `44 88 4C 24 20` (`mov [rsp+0x20],r9b`). [V] Ninguna rama de la función vuelve a `0x2829E60..64`.
- **Firma** [E]: `void 0x2829E60(mgr, &nombre, &tipo, char, u8, u8, u8)`. Devuelve `void`.
- **Llamadores directos** [V] (búsqueda de `E8 rel32` en `.rsrc`):

  | Llamada | Retorno | Origen | Tipo |
  |---|---|---|---|
  | `0x29C3044` | `0x29C3049` | 2834 (`0x29C2D50`) | el del paquete |
  | `0x28363CF` | `0x28363D4` | `0x2836110` (carga del dueño, DL≠0) | 0 |
  | `0x287645B` | `0x2876460` | `0x2876420` (evento del cargador del campo) | 0 |
  | `0x2877140` | `0x2877145` | `0x2876E50` | 1 |
  | `0x281CAD7` | `0x281CADC` | — | 3 |
  | `0x2A9E723` | `0x2A9E728` | — | 2 |
  | `.xtls 0x1502503F7` | `0x1502503FC` | — (`0x1025036E mov byte [rsp+1A0h],3`) | 3 |

  El último no está en `.rsrc` (lo encontró la revisión). Cualquier retorno que no sea uno de los tres primeros cae en
  «otro»: llama **siempre** al original y no se registra.
- **Quién lo provoca** (`provoker_of`): en el 2834, el usuario 2 si corre su Troc 2834 (gancho de sujeto); con el
  gancho puesto, cualquier otro 2834 es del anfitrión (la sesión tiene dos usuarios). En `0x2836110`, el anfitrión (o
  el usuario 2 dentro de su Troc). El cargador no es de nadie. Sin gancho de sujeto: desconocido → original.
- **Hash del paquete** [V] (desensamblado de `0x29C2D50`): tras 7 `push`, `lea rbp,[rsp-0x60]` y `sub rsp,0x170`, así que
  `rbp = C+0x110`, donde `C` es el `rsp` en la llamada (= dirección del retorno en el gancho + 8). El hash del paquete
  está en `[rbp-0x38] = C+0xD8` (`0x29C2FF7 mov r8d,[rbp-0x38]`), `&tipo = C+0x98` y `&nombre = C+0x140`. Antes de
  instalar se comprueban los 12 B del prólogo en `0x29C2D60` (un prólogo común: 17 veces en el exe) y los 82 B de
  `0x29C2FF7..0x29C3049` ([V] únicos en el exe); en cada llamada se exige además que `rdx == C+0x140` y
  `r8 == C+0x98`. Si no casa, ese borrado no tiene hash de paquete y se
  borra de forma nativa.
- **Nombre → hash, el [NV] crítico, se mide en cada 2834:** se lee el nombre (`[[&nombre]]`, longitud en `[S+8]` o
  hasta el NUL, máximo 255 B, sin cruzar páginas en una lectura) y se calcula `lookup3 hashlittle(nombre, 0xC5EDE)`
  (lo que `0x1364780` guarda en `[S+0xC]`) y la misma función sobre el nombre base sin extensión. Una regla queda
  **demostrada en el proceso** cuando ha dado el hash del paquete al menos una vez y **nunca** ha fallado. Un solo
  fallo la descarta para todo el proceso. Sin regla demostrada no se omite nada.
- **Seguimiento por usuario** (anfitrión y usuario 2), en el gancho de `OnRecvPacket` (`server.cpp`, `ServerRunThread`,
  antes de que se maneje el paquete). Formatos [V] sobre las capturas `p8-2inst-s42-probe` y `p8-2inst-pose-anfitrion`
  (cabecera de 5 B: `u16 opcode`, `u8`, `u16 tamaño`):
  - 2598: `u32` (1, o 3 en los cuatro primeros de la carga del juego) + `u32 hash` → **suma**. El manejador lo registra
    siempre como tipo 0 (`.xtls 0x15027B1E0`, `IsRegistered(…, 0)`).
  - 2834: `u32 1, u8 tipo, u32 hash, u8 flag` → **resta** si el tipo es 0. Los 392 + 69 + 126 + 39 2834 de las cuatro
    capturas son de tipo 0, y ninguno es de un hash que no se hubiera sumado antes.
  - 2104: `u8 4` + n × `u32 hash` (345 B = 1 + 86 × 4) → **reemplaza** el conjunto. Medido: tras vaciar en la carga,
    todos los 2598 posteriores están en la lista del 2104 (0 fuera), y la lista trae 18–68 niveles que el cliente
    conserva cargados a través de la recarga sin volver a enviar 2598. Sin vaciar, el conjunto acumula 10–29 niveles
    viejos que el 2104 ya no trae.
  - 2283, 2362, 2437 y 2001 de ese usuario → **vacía** su conjunto. El hilo del módulo además vacía el del usuario 2
    cuando cambia la época, cuando el usuario 2 se va, y cuando sale del campo del anfitrión (2 s seguidos fuera según
    `user2_stages_user2_in_host_field`, para que una lectura fallida no cuente). Al reemplazarse la instancia del
    servidor se vacían los dos.
  - Estructura: tabla abierta de 1024 huecos por usuario (máximo 768), un solo escritor (con un `try` de un
    `atomic_flag`), lectores con `seqlock`. Una lectura que se cruza con una escritura, un desbordamiento o una
    actualización perdida dejan ese conjunto «no fiable» hasta su siguiente vaciado o 2104: nunca dan un «sí» viejo.
  - El conjunto del usuario 2 solo cuenta mientras está en el campo del anfitrión (necesita `user2_stages` activo; si
    no, nunca protege nada).
- **Decisión** (`decide_delete`; todo lo que falte → borrado nativo):
  1. modo ≠ 0, llamador conocido, tipo legible y 0, campo del anfitrión, quién lo provoca conocido, nombre legible;
  2. en el 2834: hash del paquete leído, regla demostrada y el hash del nombre igual al del paquete;
  3. en los otros dos llamadores: regla demostrada (el hash sale del nombre);
  4. **otro** usuario distinto del que lo provoca lo tiene en su conjunto (fiable). Si nadie más lo tiene, o su
     conjunto no es fiable: borrado nativo.
  5. en el 2834, la entrada tiene que estar **lista** (`+0x40 ≠ 0`; revisión P2-4). Se lee solo en este punto, con
     lecturas protegidas, recorriendo el mapa de tipo 0 como lo recorre `0x2836110` ([V] `0x2836186..0x283631F`):
     `[mgr+0x84]` nodos en el array `[mgr+0x98]`; en cada nodo, la clave (el objeto cadena) en `+0x10` y el byte listo
     en `+0x40`. Se compara primero el puntero de la cadena y después los bytes del nombre. Sin el cerrojo del registro:
     si el número de nodos, el array o el sello `[mgr+0x8C]` (que `0x2829E60` incrementa antes de borrar) cambian
     durante el recorrido, la respuesta es «ilegible». No lista → `entrada-no-lista`; ilegible → `listo-ilegible`; en
     los dos casos, borrado nativo. Así no se conserva una puerta rota (una entrada que A no llegó a poner lista).
  6. probe → `[would keep]` (con la misma comprobación de listo: dice lo que haría el modo 1); modo 1 → `[kept]`
     **solo si el llamador es el 2834**; los otros → `[would keep] (solo observa este llamador)`.

#### Decisión de B por llamador

| Llamador | Modo 1 | Por qué |
|---|---|---|
| 2834 (`0x29C3049`) | **omite** el borrado si se cumple todo (también: entrada lista) | [V]/[E] El manejador no usa el resultado (`void`; tras la llamada hay `nop; jmp`), libera el nombre él mismo (`0x29C304C lea rcx,[rbp+0x30]; call 0x140394DB0`) y devuelve 0. Omitir equivale a que ese 2834 no hubiera llegado: la entrada, sus UUID y sus objetos quedan como antes del 2834. Las tres llamadas previas de `0x2829E60` con el nombre (`[campo+0x228/+0x240/+0x230]`) tampoco se hacen, coherente con «sigue cargado». |
| `0x2836110` (`0x28363D4`) | **solo observa** | [E] Copia los nombres de todas las entradas listas y luego, para cada una, llama al borrado (omitir una no rompe el recorrido). Pero, pase lo que pase, vacía `+0x238` y `+0x258` (`0x2836FF0`, `0x2837100`), y su llamador `0x287BCE0` llama a `0x141EF8FE0` sobre `[[campo+0x60]+8]`, que es [H] el «todo» de lo que `0x2829E60` quita por nombre con `0x141EF8DB0`. Una entrada conservada quedaría a medio desmontar: no se puede demostrar que sea seguro. |
| cargador (`0x2876460`) | **solo observa** | Es el evento de descarga del propio cargador del campo; conservar la entrada dejaría el cargador y el registro en desacuerdo. Sin demostración. |
| otros | original, sin registrar | Tipos 1–3 o llamadas que no son de SceneLevels de tipo 0. |

Consecuencia: con `scene_level_share=1`, cuando el **anfitrión hace una carga con DL≠0** (viaje rápido, recarga) sus
entradas listas, también las que A puso listas para el invitado, se siguen borrando de forma nativa. El invitado vuelve
a tener la puerta rota hasta que salga de la zona y vuelva. La prueba lo medirá como `[would keep] (solo observa este
llamador)`.

#### Entradas conservadas («huérfanas»)

- Cuando el último usuario que tenía cargado el nivel envía su 2834, su conjunto ya no lo tiene (el paquete se ve
  antes de manejarse) y el otro tampoco: `nadie-mas-lo-tiene` → el borrado nativo pasa y la línea
  `[kept -> borrado]` lo confirma.
- Si el que lo conservaba carga o se va sin enviar 2834, la entrada queda registrada sin nadie. La limpian los caminos
  nativos que ya existen: la siguiente carga DL≠0 del anfitrión (`0x2836110`, borra todas las listas), el cargador del
  campo, o un nuevo 2598/2834 del nivel. El hilo lleva un libro de hasta 64 entradas conservadas y el resumen cuenta
  las que nadie tiene cargadas («posibles huérfanos»). El recuento es aproximado: el libro solo se limpia con los
  borrados de los tres llamadores conocidos con regla de hash demostrada. Hoy, sin el arreglo, el invitado ya deja
  entradas así en el campo del anfitrión (sus cargas no pasan por `0x2836110`).
- Una entrada conservada está siempre lista (punto 5), así que el que vuelve encuentra la puerta abierta aunque su
  2598 salga como «ya registrado». [NV] Que su cliente recupere los objetos de la puerta: lo mide el caso nuevo del
  plan de prueba (§6, paso 2.4).

### Qué no hace

No abre puertas, no escribe `+0x40`, no crea entidades, no llama a ninguna función del juego (ni de creación ni de
borrado) y no toca U-1. A cambia un registro; B solo decide si se llama al original.

## 3. Claves, requisitos e inis

- `scene_level_share = 0 | probe | 1` en `[session2]` del anfitrión (necesita `create_session=1`).
- Requisitos para que actúe: el gancho de sujeto de `user2_funnels` (cualquiera de sus claves; en los inis S42/S43
  están `quest_per_user=1`, `kill_loot_per_user=1`…) y `user2_stages` distinto de 0 (para «el usuario 2 está en el
  campo del anfitrión»). La línea de instalación dice si el sujeto está enganchado.
- Inis nuevos en `docs/research/ini/` (copias exactas de los S42 respectivos con cabecera nueva):

  | Paso | Anfitrión | Invitado |
  |---|---|---|
  | 1, probe | `cdmp.2inst-s43-probe-host.ini`: `door_trace=1`, `scene_level_share=probe` | `cdmp.2inst-s43-probe-guest.ini`: `door_trace=1` |
  | 2, activo | `cdmp.2inst-s43-activo-host.ini`: `door_trace=1`, `scene_level_share=1` | `cdmp.2inst-s43-activo-guest.ini`: sin cambios (ya tenía `door_trace=1`) |

## 4. Líneas de log (`cdmp.host.log`)

Todas salen del hilo del módulo (cada segundo); los ganchos solo escriben en un anillo sin bloqueo.

- Instalación: `SceneLevelShare: modo probe | A en 0x283044D (mid 7 B …; firma 40 B en 0x2830432 y .pdata … verificadas): … | B en 0x2829E60 (entrada 5 B …, marco del 2834 verificado): … | sujeto de ExecuteTroc (user2_funnels) enganchado`.
  Si algo no cuadra: `SceneLevelShare: A REFUSED …` / `B REFUSED …` (WRN) y esa pieza queda apagada.
- A: `SceneLevelShare [would ready]` / `[ready for user 2]` con nivel (nombre), `hash-nombre`, `base`, `cache`, actor,
  usuario, dueño del campo y opcode. `[owner native]` (pase nativo del anfitrión) y `[gate] <motivo>` (rechazos del
  usuario 2), limitados a 20 por minuto cada uno.
- B: `SceneLevelShare [delete] … llamador 2834 (ret 0x29C3049) tipo 0 nivel '…' hash-nombre 0x… base 0x… cache …
  paquete 0x… | provoca usuario2 (troc enganchada opcode 2834) | campo del anfitrion si | lo tienen: anfitrion si
  usuario2 no (otros que lo tienen: anfitrion) | entrada lista si | regla nombre -> [kept] (kept)`. La etiqueta final
  es `[kept]`, `[would keep]` o `borrado nativo` con el motivo (`entrada-no-lista` / `listo-ilegible` incluidos).
- Verificación del hash: `SceneLevelShare [hash] primera coincidencia nombre -> hash del 2834: '…' = 0x…` y, si pasa,
  `SceneLevelShare [hash] nombre NO da el hash del 2834: …`.
- Conjuntos: `SceneLevelShare [conjunto] … usuario2 2598 hash 0xDE7B52AA -> anadido (ahora N)` (solo para el nivel de
  la puerta, y siempre para listas 2104, vaciados y paquetes mal formados), y `[conjunto] usuario2 vaciado (…)` desde
  el hilo.
- `SceneLevelShare [kept -> borrado]`: un nivel conservado que por fin se borró de forma nativa.
- `SceneLevelShare [resumen]` cada 60 s: contadores de A por motivo, borrados por llamador (would keep / kept),
  motivos, coincidencias de hash por regla, estado del marco, tamaño y fiabilidad de cada conjunto, kept vivos y
  posibles huérfanos, estado del sujeto y del usuario 2, pérdidas del anillo e incidencias del ingreso.
- Presupuestos de líneas (revisión P3-3), cada uno con su contador de omitidas en el resumen:
  - 3000 para los borrados nativos de la carga del dueño (`0x2836110`) y del cargador, que llegan a cientos de golpe;
  - 50 000 para las esenciales: `[would ready]`, `[ready for user 2]`, todo `[would keep]`/`[kept]`, el nivel de la
    puerta (`0xDE7B52AA`) y las de conjuntos;
  - 20 000 para el resto.

  El libro de conservadas sigue cada evento aunque su línea se omita.

## 5. Criterios de aborto

Parar la prueba (y no pasar al paso 2, o volver a `probe`) ante cualquiera de estos:
- cualquier cierre o cuelgue del juego;
- `A REFUSED` o `B REFUSED`, o `sujeto … SIN GANCHO`;
- `[hash] … NO da el hash del 2834` para la regla en uso, o ninguna coincidencia tras varios 2834 (B no se puede
  activar: se queda en probe);
- `[would ready]` con un opcode que no sea 2598/2104, o sobre un actor que no sea el personaje del invitado;
- `[would keep]`/`[kept]` de un nivel que, según los conjuntos y lo que se ve en pantalla, ninguno de los dos tiene;
- posibles huérfanos que crecen sin parar en los resúmenes, o un conjunto que sale `NO fiable` de forma repetida;
- familias duplicadas, objetos de escena duplicados o puertas que se abren solas;
- con el paso 2: la puerta sigue con `recurso-no-listo` tras un `[ready for user 2]` del nivel `DE7B52AA`.

## 6. Plan de prueba (dos copias en un PC)

`scripts\launch-2inst.ps1` con `-HostIni`/`-GuestIni` de la tabla de §3. Copia de seguridad de las partidas antes de
cargar nada. Marcar con **Ctrl+F12** en las dos copias antes y después de cada intento (`=== MARK n (Ctrl+F12) ===`),
y apuntar la hora. Reiniciar los dos procesos entre pasos.

**Paso 1 (probe).**
1. **El invitado llega primero** a la taberna de Hernand con el anfitrión lejos (más de 60 celdas). El invitado intenta
   la puerta. Esperado: `[conjunto] usuario2 2598 hash 0xDE7B52AA -> anadido`, `[would ready]` del nivel con opcode
   2598 (o 2104) y actor = personaje del invitado, `DoorTrace [nativo] … recurso-no-listo-6CF701C`.
2. **Llega el anfitrión**; intentan los dos. Esperado: `[conjunto] anfitrion 2598 … anadido`, ninguna línea A para ese
   2598 (ya registrado) y `recurso-no-listo` para los dos.
3. **Uno se aleja** (primero el invitado; repetir con el anfitrión) y **el otro intenta**. Esperado: el 2834 del que se
   va da `[delete] llamador 2834 … provoca usuario2 … lo tienen: anfitrion si | entrada lista no … -> borrado nativo
   (entrada-no-lista)`, porque en probe A no pone lista la entrada que registró el invitado. La primera vez sale
   también `[hash] primera coincidencia nombre …`. En probe el borrado nativo pasa siempre.
4. (Control) Reiniciar y repetir con **el anfitrión primero**: debe abrir (`exito`, Ack2145) y salir `[owner native]`.
   Después llega el invitado y se aleja: ahora la entrada está lista y su 2834 da `entrada lista si … -> [would keep]`.
5. Si el anfitrión hace un viaje rápido con el invitado en la taberna: `[delete] llamador carga-dueno-0x2836110 … ->
   [would keep] …`.

Qué mirar: `SceneLevelShare [resumen]` (regla `nombre` usable, marco verificado, conjuntos fiables), que el nombre del
nivel con hash `0xDE7B52AA` sea el de la taberna, y que `[would keep]` solo aparezca cuando el otro está de verdad en
la zona.

**Paso 2 (activo)**, solo si el paso 1 cumple §5. El mismo orden:
1. Invitado primero: `[ready for user 2]` del nivel y su puerta da `exito` + Ack2145.
2. Llega el anfitrión: su puerta también da `exito`.
3. El invitado se aleja: `[kept]`; la puerta del anfitrión sigue abriendo. Después se aleja también el anfitrión: su
   2834 da `borrado nativo (nadie-mas-lo-tiene)` y `[kept -> borrado]`.
4. **El que se fue vuelve y abre la puerta** (revisión P2-4). Tras el `[kept]` del paso 3, el invitado vuelve a la
   taberna con el anfitrión todavía allí. Su 2598 sale como «ya registrado» (no hay línea A) e intenta la puerta.
   Esperado: `exito` + Ack2145, y que la puerta se vea y se mueva en **su** pantalla. Si el servidor acepta pero el
   invitado no ve la puerta, su cliente no recupera los objetos de una entrada conservada: abortar el modo 1 para B.
   Repetirlo al revés: se va y vuelve el anfitrión.
5. Al revés (se aleja primero el anfitrión): `[kept]` y la puerta del invitado sigue abriendo.
6. Viaje rápido del anfitrión con el invitado en la taberna: se espera que la puerta del invitado **vuelva a fallar**
   (B solo observa `0x2836110`); confirmarlo mide si hay que extender B.
7. Recarga del invitado (viaje rápido o revivir) cerca de otros niveles: su 2104 hace que A ponga listos niveles que solo
   tiene el invitado, con sincronización a todo el campo (P2-3, abajo). Mirar el log del cliente del anfitrión.

## 7. Lo que no está demostrado

- [NV] Que el hash del 2834 sea `lookup3(nombre)`: se mide en cada 2834 (regla `nombre` o `nombre-base`). Hasta la
  primera coincidencia, B no omite nada.
- [NV] Que el nombre de `0xDE7B52AA` sea el SceneLevel de la taberna (lo dirá la línea `[would ready]`).
- [H] Qué limpia `0x141EF8FE0` (llamado por `0x287BCE0` tras `0x2836110`) y qué son `+0x238`/`+0x258`: por eso
  `0x2836110` solo se observa.
- [NV] Si conservar una entrada cuyo cliente la descargó provoca envíos de sincronización de objetos a ese cliente
  (`ToFieldSendTargetFinder` del campo). Se espera que los ignore (son UUID que ya no tiene). Vigilar errores del
  invitado.
- **Riesgo a vigilar en vivo (revisión P2-3).** Con el modo 1, en el 2104 del invitado `0x2830400` corre con
  `param_5 = 1`, así que `0x282C3B0` envía a **todo el campo**, también al cliente del anfitrión, los objetos de
  niveles que el cliente del anfitrión **no tiene cargados**. Nativamente pasa lo mismo con el dueño, pero nunca se ha
  visto al revés. [NV] Lo más probable es que el cliente los ignore (UUID desconocidos). Vigilar errores o avisos del
  cliente del anfitrión y picos de envío justo después de una recarga del invitado.
- [NV] El caso de Shadow (red real, cargas a distinta velocidad) sin probar.
- [H] Que el 2001 de un usuario pase por `OnRecvPacket` (U-1 lo cuenta allí; si no llega, vaciarán su 2437 o su salida).
- El byte «listo» solo se lee para los 2834 que se conservarían (punto 5 de la decisión), sin el cerrojo del registro y
  con el sello del mapa como control. Para los demás borrados no se lee; `DoorTrace` da el resultado de cada puerta.

## 8. Motivo 42 (commit `8f827a2`)

Informe completo: [`s43-datos/motivo42-informe.md`](s43-datos/motivo42-informe.md).
- [V] El `CONTROL ABORT` de `user2_npc_control` del 15-10 (`p8-2inst-s42-probe host.log:57320`) fue un **falso
  positivo**. Contó 19 bajas de motivo 42 que eran despedidas nativas de la IA del cliente del invitado: un grupo de
  24 NPC de horario (datos `0x3537D7CE`) más 8 de `0x50BC12F0`, que no se repusieron. No había bucle. Después, como el
  aborto no se rearma, el siguiente armado de U-1 trajo de vuelta el bucle de la S31 (164 bajas de motivo 42 en un
  minuto, con el anfitrión de controlador).
- Arreglo: el bucle de la S31 es un NPC que **se repone en el mismo hueco de stage** (datos del horario, nodo) tras cada
  baja de motivo 42. Ahora la guarda cuenta solo las bajas que **se repiten en el mismo hueco** en 60 s
  (`R42SlotTable`, sin bloqueo y de tamaño fijo; un hueco desconocido o la tabla llena cuentan como repetición).
  Aborta con más de 10 repeticiones en el último minuto. El recuento bruto sigue en las líneas.
- Reproducido sobre los logs: el 15-10 da 0 repeticiones (no habría abortado); el bucle local de la S34 da 68–180 por
  minuto (aborta igual que antes). No cambia ninguna clave ni el protocolo.
- Revisión de `a4c2661` (P2-1): `r42_slot_id(datos, nodo)` no distingue dos instancias de la misma stage. Dos grupos del
  mismo horario que se van a la vez (fin del horario) contaban cada par como repetición. Ahora una baja solo cuenta como
  repetición si su clave **se puso después** de la última baja de motivo 42 de su hueco (`KeyTable::put_time`). Es la
  definición del bucle: baja, reposición, baja. Una hora de nacimiento desconocida cuenta como repetición, igual que un
  hueco desconocido (falla cerrado).
  - Test: 12 parejas del mismo hueco puestas antes de sus dos bajas dan 0 repeticiones. El bucle real (clave nueva
    cada 5,8 s, puesta tras la baja anterior) sigue llegando a 11 y abortando con `Reason42`.
  - El resumen de U-e cuenta aparte las bajas «mismo hueco pero puesta antes de su última baja».

## 9. Archivos, tests y commits

- `src/mod/session2/scene_level_share.{h,cpp}`, `scene_level_share_logic.h`; enganchado en `server.cpp`
  (`on_server_recv`), `session2.cpp` (arranque) y `config.{h,cpp}` (clave).
- `tests/optd_scene_level_share_tests.cpp` (registrado en CMake como `optd_scene_level_share`): parsers con los bytes
  reales de las capturas (2598, 2834 con flag 0 y 1, el 2104 de 350 B), conjuntos por usuario (contra `std::set` con
  20 000 operaciones aleatorias), decisión keep/delete (hash no verificado o distinto → borrado; último usuario →
  borrado; conjunto no fiable → borrado; los llamadores que solo se observan), vaciado al cargar, lista 2104 que
  reemplaza, la puerta de dueño, el escenario de la taberna de principio a fin, y el libro de conservadas.
- Commits: código y tests (`S43 scene_level_share: …`), este documento con los inis, y las correcciones de la revisión
  (§10).

## 10. Correcciones de la revisión independiente de `a4c2661`

Veredicto de la revisión: probe seguro; modo 1 aceptable para el paso 2 si el paso 1 cumple §5.
- **P2-1** (motivo 42, instancias simultáneas): ver §8.
- **P2-2** (`Tracker::apply`): la petición de vaciado se borra **después** de vaciar el conjunto
  (`load` → `reset` → `store(false)`). Así un lector nunca ve la petición borrada con el conjunto viejo todavía dentro.
  Con un solo escritor no se pierde ninguna petición: una que llegue durante el `reset` queda absorbida por él.
- **P2-3** (sincronización al campo en el Resync del invitado): riesgo a vigilar en vivo, §7 y §6 paso 2.7.
- **P2-4** (conservar una entrada no lista): punto 5 de la decisión de B, y caso nuevo en §6 paso 2.4.
- **P3:**
  - el 2598 no sincroniza (§2.A);
  - el llamador `.xtls 0x1502503F7` añadido a la tabla;
  - presupuestos de líneas propios (§4);
  - el comentario de `user2_stages_logic.h` apunta a `s43-datos/motivo42-informe.md`.
