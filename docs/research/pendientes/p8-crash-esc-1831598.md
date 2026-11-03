# P8 en un PC: cierre del anfitrión "al pulsar ESC" en `exe+0x1831598` (2026-10-10 00:56)

Análisis de solo lectura. No se lanzó el juego, no se tocó código y no hay commit.

**Fuentes:**
- Log del anfitrión: `C:\Emulador CD\backups\logs\cdmp_20261010-0055_esc-crash-host.log`.
- Volcados de WER: `C:\Emulador CD\backups\esc-crash-20261010-0056\CrimsonDesert.exe.29736.dmp` (cabecera
  00:56:56) y `…(1).29736.dmp` (00:57:01).
- PE estático `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`.
- PDB del `cdmp.asi` desplegado: `bin64\cdmp.pdb`, de las 00:51. El de `build\` es de las 01:02 y ya
  no coincide.

Marcas: **[V]** verificado en el volcado, el log o el código; **[E]** deducción.

## 0. Respuesta corta

- **El cierre no lo provoca el ESC.** [V]/[E fuerte]
  - El juego cayó hacia las **00:56:49,2**, unos 0,35 s después de que el personaje del usuario 2
    (`0xA0100268`) entrara en tu campo **sin acción**.
  - Eso es casi 3 s antes del primer ESC.
  - Desde ese momento el proceso estuvo congelado por WER mientras escribía el primer volcado
    (hasta las 56,45). Por eso **el primer ESC "no hizo nada"**.
  - Al terminar el volcado, el desenrollado ejecutó la destrucción del juego y este volvió a caer en
    el mismo sitio. Ese segundo volcado es el de las 57:01, y entonces "desapareció".
- **Quién lee el nulo:** una pieza adjunta al personaje del usuario 2 (actor `0xB01002DA`, sin CC; por
  la clave y el momento, probablemente equipo) en su actualización de componentes en el **hilo
  principal**:
  - `ClientGimmickActorComponent` vf+0xC0 →
  - `ClientHierarchyActorComponent` vf+0x150 →
  - `0x1831560`, que lee la acción actual del **padre**, `0xA0100268`, y es nula.
  - Ese consumidor está fuera de la barrera D-52/D-44/D-46 (`horse_diag`, en `0x85FBA0`/`0x860D30`).
- **Por qué no tenía acción:** el guion del peer era `login_e3lc00act_112627.troc`, la variante D-44
  de las 03:53, **anterior a D-48**. [V]
  - No envía la `StartActionReq` del ciclo 1 (paso 4b).
  - Su única `StartActionReq` es el paso 14: al final, 5 s después de la segunda carga, hacia las
    56,9. Para entonces el juego ya se había caído.
  - Así que el byte de restauración llegó a 0 (`HorseDiag RESTORE … restoreByte=0`).
  - `child_hold` no retiene personajes por diseño ("users and characters are never held", D-48).
  - Es la causa de C1–C1f, reintroducida por el procedimiento de `p8-heap-inyeccion.md` §9, que
    receta ese guion.
- **Ni el arreglo de inyección (`0x35BAD4`) ni el hilo de red del mod intervienen.** [V]
  - El bucle de 923 ms es una consecuencia del fallo (WER).
  - El espejo copió e inyectó 12 Req del anfitrión con la clave del usuario 2, pero no creó ni
    quitó la acción. [V/E]

## 1. Pregunta 1: función, instrucción, nulo, actor, pila e hilo

### 1.1 Instrucción [V]

`0xC0000005` de lectura en `0x10B`, hilo **13080**, el **hilo principal**: su pila nace en
`exe+0x4895C3E` (entrada CRT) y la base de la pila es `0x13F…`. Los dos volcados fallan en el mismo
hilo y la misma instrucción.

```
FUN_141831560(this)                      ; 10 llamadores (0x439B80, 0x70E29F, 0x944CB5, 0x9E916B minimapa, …)
  1831569 mov rcx,[rcx]                   ; actor padre
  183156F call [rax+0x1D8]                ; ¿es personaje?
  1831579 mov rax,[rbx]
  183157C mov r8,[rax+0x68]               ; tabla de componentes del actor
  1831580 mov rdx,[r8+0x40]               ; CC
  1831584 mov rax,[rdx+0x30]
  1831588 movzx ecx,byte [rax+0x1B]       ; capa activa
  183158C mov rax,[rdx+rcx*8+0x88]        ; capa (CC+0x88+8*i, la misma que mira horse_diag)
  1831594 mov rcx,[rax+0x10]              ; acción actual = 0
  1831598 movzx edx,byte [rcx+0x10B]      ; <== AV (flags de la acción)
```

Registros del primer volcado:
- `rcx=0` (acción actual);
- `rax=0x38B576617C0` (capa);
- `rdx=0x38B577CD700` (CC);
- `r8=0x38B57CAD600` (tabla de componentes del padre);
- `rbx=0x38B57197D90` (`this`);
- `r13=0xA0100268`;
- `r14=0x38B57661680` (el actor padre en el llamador).

### 1.2 Actor y dueño [V]

El volcado de WER **no trae el montón**, solo pilas y segmentos de datos de los módulos, así que no se
puede leer el actor. La identidad sale de los registros recuperados al desenrollar.

- **Padre, el actor sin acción:** `0xA0100268`, el personaje del usuario 2 (actor cliente
  `0x38B57661680`).
  - En el marco de `0x88D05F0` del segundo volcado, `ebx = [hierComp+0x1C] = 0xA0100268`: la clave
    del padre guardada en el componente de jerarquía.
  - `r13 = 0xA0100268` en `0x439AB0`.
  - Coincide con `g_extra_copy = 0xA0100268` y `g_owner = 0x90100267` de `horse_diag` en el volcado.
- **Quien lo consulta:** el actor `0x38B57CE8B80`.
  - Su componente de jerarquía es `0x38B56451A00` (`[[actor+0x68]+0x1B8]`) y su clave propia es
    `0xB01002DA` (`[[hier+8]+0x60]`, `rsi` en `0x15ECA10`).
  - Es la clave siguiente a `0xB01002D9`, el primer actor de mundo que el anfitrión cargó con
    referencia al personaje del usuario 2 (`Capture: body template (body 0xB01002D9…)` a las 49.014).
  - [E fuerte] Es una pieza del propio usuario 2: equipo o adjunto creado en su carga. En C1e se le
    vio con escudo. No es un NPC ni el hijo retenido `0xB0100269`.

### 1.3 Pila completa del primer fallo [V]

Desenrollado x64 con `.pdata` del PE estático. Las clases salen del RTTI de la vtable.

| # | Retorno | Función | Qué es |
|---|---|---|---|
| 0 | `0x1831598` | `0x1831560` | lector "¿el padre está en una acción con flag 8/4?" |
| 1 | `0x439B85` | `0x439AB0` | `ClientHierarchyActorComponent` vf+0x150 (vtable `0x5578BB8`); padre = `rdx` |
| 2 | `0x15ECF73` | `0x15ECA10` | busca el actor padre por clave y llama a vf150 (17 llamadores) |
| 3 | `0x99A3454` | `0x99A31B0` | `.xtls`, alcanzado por el thunk `0x8F7260` (`jmp`) |
| 4 | `0x8F7990` | `0x8F7780` | |
| 5 | `0x8DA224` | `0x8D8940` | `ClientGimmickActorComponent` vf+0xC0, la actualización (vtable `0x55B7898`) |
| 6 | `0x4FE095` | `0x4FDDF0` | despachador: vf+0xC0 de cada componente del actor |
| 7 | `0x98CB108` | `0x98CAEA0` | `.xtls` (usa `SharedGlobalAttacher`) |
| 8–12 | `0x140F1C8`, `0x141145D`, `0x13F885D`, `0x13F50E1`, `0x140496F` | | tareas y bucle principal |
| 13–19 | `0x6173E9`, `0x3F412E6`, `0x14C8803A`, `0x3F44B28`, `0x86D2ADC`, `0x4895C3E` | | arranque del hilo principal |
| 20–21 | `kernel32+0x2CD87`, `ntdll+0xACAEC` | | |

- **Ningún marco es de `cdmp.asi`**, ni de UI, menú, cámara o retrato.
- Es la actualización de mundo de los componentes del actor.

### 1.4 Por qué hay dos volcados [V]

**Primer volcado (00:56:56).** El hilo 13080 está dentro del despacho del fallo de 0x1831598:
`KiUserExceptionDispatcher` → `RtlDispatchException` → `__C_specific_handler` (ntdll) → filtro de
`RtlUserThreadStart` → **`UnhandledExceptionFilter` → `WerpLaunchAeDebug`**, esperando a WerFault.
- Ningún marco del juego aceptó la excepción.
- La página `KUSER_SHARED_DATA` del volcado se capturó a las **00:56:56,4487**.

**Segundo volcado (00:57:01).** El filtro devolvió "ejecutar el manejador" y empezó el desenrollado
hacia `RtlUserThreadStart`.
- `RtlUnwindEx` → `__CxxFrameHandler4` → `0x86D5850`, el destructor de un objeto propio del marco
  principal (llama a su vf0).
- → `0x359C10` → `0x3F40F30` → `0x61A520` → **`0xACD400`** (destrucción del juego, ya vista en
  `companeros-cierre-salida.md`) → `0x1404410`.
- → **otra vuelta del bucle** (`0x13F4F50` …) → `0x4FDDF0` → `0x88D05F0`, que es otro componente
  vf+0xC0 de la misma pieza `0x38B57CE8B80` → el mismo `ClientHierarchyActorComponent::vf150` →
  el mismo AV.

Es el patrón "dos fallos por proceso, separados unos 5 s" de la tabla de `companeros-cierre-salida.md`
(06:49:22/27, 06:53:48/53). Corrijo lo que allí era [S]: el primer fallo **no** lo recoge un manejador
del bucle principal (Sentry); llega al UEF de WER y el desenrollado posterior ejecuta la destrucción.

## 2. Pregunta 2: ¿por qué ahora? ¿Lo provoca el ESC?

### 2.1 Cronología

| Hora | Hecho | Marca |
|---|---|---|
| 46.901–47.399 | C2: el login del usuario 2 escribe la identidad `0x…F002` | [V] |
| 47.433 | Viaje E1.T1 a su campo propio, dueño él: `MoveFieldAck` 2174 | [V] |
| 48.167–48.518 | 2549 → 2144 → 3066 → **2322 nativo** → 2445 → 2762. **Ninguna `StartActionReq`** (el guion no tiene paso 4b) | [V] log y guion |
| 48.750–48.842 | E3b H1/H2: entra en tu campo (`users 2 user actors 3`). E1.T2: 2144, 3066 y **nuestro 2322** (ObserverMilestone EMIT) | [V] |
| 48.840 | `child hold: held 0xB0100269` (su hijo de tipo 6; correcto) | [V] |
| 48.956 | `HorseDiag RESTORE(0x8646D0) USER 2 0xA0100268 … chart=true … restoreByte=0` en el hilo principal: `0x8646D0` no restaura | [V] |
| 48.992 | Primera actualización del CC sin acción; la barrera D-52 la salta (`skipped 1`) | [V] |
| 49.014 | El anfitrión carga `0xB01002D9` (y, por la clave, `0xB01002DA`) | [V]/[E] |
| **≈49.16–49.5** | **Primer fallo** en `0x1831598` | [E fuerte], ver 2.2 |
| 49.52–50.443 | Una vuelta del hilo de red del mod tarda 923 ms ("state/hud") | [V] |
| ≈50.5–56.45 | **Todos los hilos del mod, congelados**: WER escribe el volcado 1 | [V]/[E] |
| ~52 y ~55 | Los dos ESC del usuario, con el proceso congelado | (usuario) |
| 56.4487 | WER captura la página KUSER | [V] |
| 56.461 | Los hilos del mod reanudan y sueltan lo pendiente: `peer timed out after 5000 ms`, P8Probe, `skipped 10` | [V] |
| ≈56.5–57.0 | El desenrollado ejecuta `0xACD400`, segundo fallo y volcado 2 (cabecera 00:57:01) | [V] |

### 2.2 Pruebas de que el fallo es anterior al ESC

1. **Contador de `Present`** (`cdmp::render::g_presents`, leído del volcado con el PDB desplegado).
   [V]
   - El volcado da **7224**, igual en los dos.
   - El log daba 7113 a las 47.239, a 57,7 fps (6824 → 7113 en 5,0 s).
   - 111 cuadros más son ≈1,9 s, así que el **último cuadro presentado fue hacia las 49,16**. Ninguno
     después: el proceso no volvió a dibujar ni entre los dos volcados.
2. **Saltos de `horse_diag`** (`g_skipped = 10`, `g_skipped_post = 10` en el volcado). [V]
   - Solo hubo **10 actualizaciones** del personaje del usuario 2 desde las 48.992, es decir, unos
     10 cuadros (≈0,17 s a 58 fps).
3. **Líneas que faltan.** [V]
   - El `Net:`/`Session2:` de cada 5 s debía salir hacia las 52,24.
   - El vaciado de P8Probe a las 50,69 y 55,69.
   - El aviso `HorseDiag` de cada ≥2 s a partir de las 50,99.
   - Ninguna sale hasta 56.461, 13 ms después de que WER capturase KUSER.
   - Los hilos del mod estuvieron suspendidos por WerFault.

### 2.3 Por qué cae ahora

- La pila es la actualización de mundo de una pieza `Gimmick`/`Hierarchy` adjunta al personaje
  (§1.3).
- **El ESC no abre otro camino** (UI, cámara, retrato o inventario). [V pila, E tiempos]
- La consulta "¿en qué acción está mi padre?" corre en cuanto esa pieza aparece y se engancha al
  personaje del usuario 2. Fue 0,3 s después de su entrada en tu campo.
- El estado sin acción es el de siempre (D-48). [V]
  - El servidor publica al personaje con byte de restauración 0, porque nunca aceptó una acción suya
    en el campo de aparcamiento.
  - `0x8646D0` sale sin `RestartLayerAction`.
  - Las capas quedan con chart y sin acción actual.
- El guion **no** "falló al aplicar su acción en el campo del anfitrión": **no la envió**. Su
  `StartActionReq` es el último paso, tras `wait 5000` y la segunda `LoadingCompleteReq`, hacia las
  53,9 o más tarde. [V]
- En C1g, con el guion `login_e3lc00_112627.troc` (paso 4b), llegó `restoreByte=1`, las capas tenían
  acción a los 50 ms y no hubo cierre en 64 s.
- El ObserverMilestone y nuestro 2322 de E1.T2 van solo a la sesión 2, el peer. No tocan el cliente
  del anfitrión ni el estado de acción. [E]
- **Origen del error de procedimiento:** `p8-heap-inyeccion.md` §9 receta
  `cdmp_peer --script …login_e3lc00act_112627.troc --linger 900000`, y ese guion es la variante
  obsoleta.
  - Tampoco tienen 4b `actearly`, `actfast` y `cut3`; `cut3` corta antes a propósito.
  - Sí lo tienen `e3`, `e3lc00`, `bye`, `bye_child`, `soak_child`, `cut8`, `cut9` y `e3opt`.
- Es la cuarta vez que se registra `0x1831598`:
  - C1e (06:49) por el minimapa `0x9E8D10`;
  - dos procesos en `companeros-cierre-salida.md`;
  - y esta, por la jerarquía `0x439AB0`.
  - Es el mismo lector con otro llamador: de los 10 llamadores de `0x1831560`, la barrera D-52 no
    cubre ninguno.

## 3. Pregunta 3: el bucle de 923 ms ("state/hud")

**Qué hace esa fase** (`src/mod/mod.cpp`, ~360–400). [V]
- `LocalSource::sample` → `PlayerReader::read`: lecturas con `safe_read`, que hace un
  `VirtualQuery` por lectura y lee con SEH.
- Después, `render::hud_update` toma `g_mutex` un instante, el mismo que `hud_draw` en el hilo de
  `Present`.
- No llama a nada del juego ni escribe en él.

**Por qué tardó.** [E]
- Empezó hacia las 49,52, cuando el hilo principal ya estaba en `UnhandledExceptionFilter` →
  `WerpLaunchAeDebug` (último `Present` ≈49,16).
- WerFault abre el proceso, lo suspende y lee la memoria. Un `VirtualQuery` que compite por el
  espacio de direcciones mientras tanto se queda esperando.
- Medio segundo después, todo el proceso quedó suspendido hasta las 56,45.

**Relación con el cierre.** Ninguna como causa. [V]/[E]
- La pila del fallo no tiene marcos del mod.
- El hilo de red del mod solo lee con guarda.
- En el volcado, todos los hilos del mod están ociosos: el 23120 en `recv`, y P8Probe, horse tick,
  clock, observer y probe en `Sleep`/espera.
- `peer timed out after 5000 ms` también es consecuencia: al reanudar, el hilo vio casi 6 s sin
  datagramas.

## 4. Pregunta 4: ¿contribuyó `mirror_host=1`?

**Qué hace** (`server.cpp` 490–525 y 1278–1300). [V]
- En el gancho `ServerNet::OnRecvPacket` copia las Req `MoveActorReq` 2556, `StartActionReq` 2223 y
  `RotateActorReq` 2828 que manda el cliente del anfitrión para un actor de clase `0xA` distinto del
  usuario 2.
- Les cambia la clave por la del personaje del usuario 2 (`g_user2_player`, aprendida del
  `MainPlayerDataStartAck`).
- `mirror_thread` las entrega con `server_inject` 1000 ms después, si el usuario 2 está presente y la
  época coincide.
- Sí: **inyecta movimientos y acciones del anfitrión como si fueran del usuario 2**, en el servidor.

**En este proceso** (globales del volcado con el PDB desplegado). [V]
- `g_user2_player = 0xA0100268`.
- `g_mirror_copied = 12`, `g_mirror_dropped = 0` y `g_mirror` vacío (`_Mysize 0`), iguales en los dos
  volcados.
- Se copiaron 12 Req y las 12 salieron de la cola hacia el servidor antes de congelarse.

**Efecto.** [V]/[E]
- No dieron acción al personaje: `horse_diag` no registró ningún cambio de forma tras las 48.992 y
  siguió saltando. [V]
- Lo probable es que el servidor las rechazara porque el usuario 2 aún no había completado su carga
  en tu campo (`eErrEnterToStageNotCompleted`), o que llegaran tarde. [E]
- **El espejo no puede crear el estado sin acción.** Como mucho, una 2223 aceptada lo habría
  arreglado.
- No contribuyó, pero confunde: añade Req del usuario 2 que el guion no manda y en momentos que no
  controla.
- Para diagnosticar esta clase de fallos conviene `mirror_host=0`. Para la prueba de inyección de
  `p8-heap-inyeccion.md`, el espejo puede seguir, pero con un guion con 4b.

## 5. Pregunta 5: arreglo de raíz, contención y el arreglo de inyección

### 5.1 El arreglo de inyección no interviene [V]

- No hay líneas `INJECT`.
- `in: handed 3 refused 0 blocked 0 exceptions 0 seen by the server 3` (47.239).
- El NetworkThread del juego (35756) está ocioso en `exe+0x35B9D8`.
- El gancho `0x35BAD4` no aparece en ninguna pila.
- El fallo es del hilo principal, en código de mundo.

### 5.2 Arreglo de raíz: que el personaje llegue a los demás con acción

**A. Inmediato, ya existe: el guion D-48.** [V]
- Repetir la prueba de `p8-heap-inyeccion.md` §9 con `login_e3lc00_112627.troc`, o con
  `bye`/`soak_child` si se quiere logout o estancia.
- Corregir esa línea del §9 y marcar como obsoletos `login_e3lc00act_112627.troc`, `…actearly…` y
  `…actfast…`, por ejemplo con una cabecera "pre-D-48, no usar con E3b".
- En P8 real no hace falta nada: el cliente manda su 2223 a unos 10 ms del primer 2144.

**B. Garantía del servidor, independiente del cliente (recomendada como red general).**
Extender D-53 al personaje: en `child_hold` (`verdict_with`), un actor `0xA…` cuyo dueño es un usuario
y cuyo byte de restauración es 0 se retiene para los receptores que no son su dueño.
- Se libera con su primera `StartActionReq` aceptada, el mismo gancho `0x296CAA0` de D-53r.
- Lleva tope de tiempo y anuncio nativo como reserva, como en S3.
- Hoy el código lo excluye a propósito (`child_hold.cpp` ~139: "users and characters are never held",
  porque D-48 confiaba en el ciclo 1).
- Cubre los guiones viejos y el riesgo R3 de `opcion-d-accion-usuario2.md`: el byte tras muerte, viaje
  rápido o recarga.
- Sus piezas adjuntas sin CC, como `0xB01002DA`, no se retienen (`NoCc`). Pero con el padre ausente,
  el código nativo no lee la acción: `0x15ECA10` pasa `rdx = 0` si no encuentra el padre, y
  `0x439AB0` salta a `0x439BB8` (`test rdx,rdx`), que usa el propio actor. [V en el código; E en la
  conducta]

**C. Red del cliente única, en lugar de una por lector:** la (c′) de `opcion-d-accion-usuario2.md`
§4.1.
- En la entrada de `0x8646D0` (`.rsrc`): actor remoto (`+0x90` ≠ usuario local), byte 0 y capa con
  chart y hash → tratar el byte como 1.
- El motor arranca por su ruta la acción por defecto que ya está en las capas, antes de cualquier
  consumidor.
- Cubre `0x1831598` y los demás lectores (`0x232D5FF`, `0x88F832`, `0x1F63FE6`, `0x17F328F`).

**Orden propuesto:** A ya, B como garantía y C solo si en P8 aparecen remotos sin acción que B no
cubra.

### 5.3 ¿Hace falta una contención temporal en el consumidor nuevo para probar?

**No** si se usa el guion con 4b. En C1g, la barrera estuvo a 0 durante 64 s.

Si se quiere seguir probando con un guion sin acción mientras llegan B o C, hay una contención mínima
y desactivada por defecto:
- un mid-hook en `.rsrc` en `exe+0x1831594`, el `mov rcx,[rax+0x10]` previo, que cubre los 10
  llamadores de `0x1831560`, minimapa incluido;
- si `[rax+0x10] == 0`, poner `rip = 0x1418315EB` (`xor al,al`, epílogo; la pila es coherente:
  `push rbx` + `sub rsp,0x20`) y contar por clave.

Solo como red de prueba, nunca como arreglo, y fuera de `.xtls`.

## 6. Qué cambiar (resumen)

| Pieza | Cambio |
|---|---|
| `docs/research/pendientes/p8-heap-inyeccion.md` §9 | guion `login_e3lc00_112627.troc` (o variante con 4b); opcional `mirror_host=0` al aislar |
| `docs/research/data/login_e3lc00act*_112627.troc` | marcar obsoletos (pre-D-48) |
| `companeros-cierre-salida.md` §3 | el primer fallo lo recoge el UEF de WER, no Sentry; el desenrollado (destructor `0x86D5850` → `0xACD400`) provoca el segundo |
| Lista de lectores de la acción nula | `0x1831598` por un llamador nuevo: `ClientHierarchyActorComponent` vf150 `0x439AB0`, desde `ClientGimmickActorComponent` vf+0xC0 `0x8D8940` |
| Diseño | B (retención del personaje sin acción) y/o C (`0x8646D0`) |

## 7. Método

Scripts en el scratchpad de la sesión, con Python `-I` y capstone:
- `md.py`: lector de minidump (flujos 3, 4, 5, 6, 24) con desenrollador x64 por `.pdata` y respaldo
  en el PE estático;
- `cdx.py`: límites de función y desensamblado;
- `vt.py`: vtable y RTTI;
- `pdb.py`: lector MSF/DBI para las globales del mod;
- `a1.py`–`a4.py`: excepción, pilas, registros por marco, KUSER y globales.

Limitaciones:
- El volcado de WER no incluye el montón: los objetos del juego no se pueden leer, solo pilas,
  registros y `.data` de los módulos.
- El PDB de `build\` ya no coincide con el `cdmp.asi` desplegado; se usó `bin64\cdmp.pdb`, cuyos
  valores son coherentes (`g_skipped = 10`, como en el log).
