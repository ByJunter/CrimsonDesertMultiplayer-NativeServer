# P8 S3: retención por observador (anfitrión), sondas, captura de cierre y contención (invitado)

Implementación del 9 de octubre de 2026 sobre la rama `opcion-d` (HEAD `6f5de7f`). Build del juego
**1.0.0.2976**, base `0x140000000`, direcciones **RVA**. Responde a `p8-raiz-invitado.md` §5, §6 filas 3–5
y §7.3.

**[V]** = comprobado en bytes del PE, desensamblado, captura, log o test, con la evidencia citada.
**[E]** = inferencia o diseño no probado todavía en el juego. Nada de esto se ha ejecutado en el juego:
no se lanzó, no se copió nada a `bin64` y no se tocaron partidas ni ejecutables.

> **Revisión posterior:** ver §12, *Correcciones tras la revisión*. Sustituye partes de §1.2, §3, §6 y §9.

Las cuatro claves nuevas de `[session2]` vienen **apagadas**. Con las cuatro a 0 el comportamiento es el
de antes, byte a byte en lo que se envía (§6).

| Clave | Lado | Qué es |
|---|---|---|
| `observer_hold=1` | anfitrión | arreglo propuesto (§6 fila 3): NPC de un usuario con restauración 0 solo para su controlador hasta su primera acción |
| `guest_s3_diag=1` | invitado | sondas de solo lectura de la primera acción |
| `guest_crash_dump=1` | invitado | minidump local + historial en `cdmp.log` ante un acceso inválido |
| `guest_update_guard=1` | invitado | **contención**, no arreglo: `0x232D5C0` vuelve en seguida si la acción actual es nula |

Ini: `docs/research/ini/cdmp.p8-host.ini` (línea `;observer_hold=1` comentada) y
`docs/research/ini/cdmp.p8-shadow-s3.ini` (nuevo, a partir de `cdmp.p8-shadow-diag.ini`).

## 1. Anfitrión: retención por observador según el controlador (`observer_hold`)

### 1.1 Qué hace un NPC del anfitrión "utilizable" (verificación previa)

La pregunta era si el cliente del anfitrión manda `StartActionReq` (2223) por `0x296CAA0` para los NPC
que controla, porque de eso depende el disparador de liberación.

- **[V] Captura del anfitrión de la sesión 4.** Fichero
  `D:\SteamLibrary\…\bin64\cdmp_packets_20261009-180132.bin` (105.633.062 B, SHA-256 `AB861AEE…DBF7CD`),
  leída en solo lectura con `python -I`. Contiene el tráfico **del cliente del propio anfitrión** con su
  servidor: 197.076 registros C>S y 90.466 S>C.
  - De los 2005 que recibe el cliente del anfitrión, hay 1.269 primeras apariciones de actores con dueño 0
    y controlador de clase usuario (`rec+0x12`). Su byte de restauración: 956 a 0, 17 a 1 y 296 sin bloque
    de CC (gimmicks).
  - **Los 956 con restauración 0 reciben del cliente del anfitrión una 2223 con su clave.** El retraso va
    de 141,3 a 539,8 ms (mediana 161,0; p90 229,8; p99 364,0). Por tipo: 247 de tipo 3, 698 de tipo 6,
    4 de tipo 4 y 7 de tipo 5.
  - Crow `B01002A0`: 2223 del anfitrión a +187,9 ms de su 2005.
  - No hay 2723 ni 2991 hacia el cliente del anfitrión, como se espera: `ToNearExceptOneControl` excluye al
    controlador. Los relojes de las dos capturas no están alineados entre sí y no se ordenan sucesos entre
    PCs.
- **[V] Código.** `ServerStartActionLogic` compara el controlador del NPC con la clave del usuario que manda
  la Req: `0x2A78CF8 mov ecx,[rbx+0x90]` y `0x2A78CFE cmp [rax],ecx`. Por tanto `actor+0x90` es un u32
  con la clave de usuario, y el anfitrión tiene permiso sobre sus NPC.
  - La escritura del byte a 1 (`0x2328894`, en `vf89` desde `Broadcast`) ya estaba documentada en
    `opcion-d-accion-usuario2.md` §1.3; D-53 la comprueba al instalar (`kRestoreWrite`).
- **[V] Invitado, sesión 4.** Los 42 actores con CC de la ráfaga final reciben 2723
  (`p8-raiz-invitado.md` §5.2). La 2723 solo la emite el relevo `0x2A7A560` para una primera acción
  aceptada con el byte a 0, es decir, la que pone el byte a 1.
- **[V] D-53r en vivo.** El barrido tras `StartActionReq::vf2`, en el hilo del manejador, libera al hijo
  con restauración 1.

**Conclusión [V/E].** Para un NPC controlado por el anfitrión, el estado utilizable lo crea la
`StartActionReq` del cliente que lo controla. Es el mismo disparador que el de D-53. No hace falta un
disparador distinto.

Como respaldo, un byte puesto por otra vía (por ejemplo `ServerInitiateStartAction 0x2A79420`) también se
detecta en el siguiente barrido. Hay barridos tras **cualquier** 2223 (unas 40/s del anfitrión en esa
captura) y además el respaldo de `on_server_recv`.

### 1.2 Diseño

Extiende D-53 sin hooks nuevos. Son las mismas dos funciones, ya verificadas por D-53: `NotifyAppear
0x2B000C0` y `StartActionReq::vf2 0x296CAA0`.

1. **Predicado** (`hold_verdict_ex`, por clave a través del SAM). Una clave `0xB…` queda retenida para
   observadores si cumple todo esto:
   - dueño `actor+0xA0 == 0`;
   - controlador `actor+0x90` igual a la clave de usuario del anfitrión (con su puntero de usuario leído
     ahora) o a la del usuario 2;
   - cadena del CC válida (`[CC+8] == actor`);
   - byte `[[CC+0x40]+0x30] == 0`, leído igual que en D-53 (`child_restore_byte`).

   Si no se cumple, el actor sigue el camino nativo:
   - controlador 0 o desconocido → `CtrlNotUser`;
   - sin CC → `NoCc`;
   - byte 1 → `Restored`.
2. **Reparto** (`plan_split`, sin cambios). El "dueño" del grupo es el **puntero de usuario del
   controlador**.
   - Las sesiones del controlador lo reciben **en la misma llamada**, en un 2005 aparte: **nunca se
     retiene al controlador**.
   - Cada otro destinatario queda anotado como `(clave, sesión observadora)`.
   - Los personajes `0xA…` y los usuarios `0x9…` nunca se retienen, así que el personaje propio del
     observador no se retrasa.
3. **Tabla propia** `NpcHoldTable`, con tope de 256 entradas y bajo el mismo `g_mu`. La de hijos (64)
   no cambia.
   - Si no cabe en la tabla, el NPC **se anuncia de forma nativa con restauración 0**: es lo mismo que sin
     `observer_hold`. Se cuenta como `table full -> native` y se registra.
   - No se retiene sin entrada, porque entonces no aparecería nunca.
   - Un duplicado (el mismo NPC y observador, otra vez) conserva su primer instante.
4. **Liberación** (`npc_release_step`). Se ejecuta en el mismo barrido que D-53: tras
   `StartActionReq::vf2` en el hilo del manejador, y como respaldo en `on_server_recv`. El actor se
   resuelve **otra vez por clave**; nunca se sigue un puntero guardado.

   | Lectura | Paso |
   |---|---|
   | otra época del invitado (`server_epoch`) | soltar |
   | actor no está en el SAM | soltar ("despawned") |
   | **otro actor** tras la clave (puntero distinto del anotado, solo comparado) | soltar (generación) |
   | dueño `+0xA0` ≠ 0 | soltar (pasa a ser caso de D-53) |
   | sesión observadora sin usuario válido, o con otro usuario | soltar |
   | el observador ya no observa el sector (cambio de campo o de sectores) | soltar |
   | **controlador == usuario observador** | **liberar** (un controlador nunca se retiene) |
   | byte 1 | **liberar** |
   | byte 0, < 10 s | mantener (si cambió el controlador a otro usuario se anota una vez y se sigue esperando) |
   | byte 0, ≥ `kNpcTimeoutMs` = 10 s | **soltar con "TIMEOUT"**; nunca se libera con 0 |
   | cualquier lectura imposible | mantener; a los 30 s, soltar ("stale") |

   Liberar es llamar al trampolín de `NotifyAppear` con `[sesión observadora]` y las claves que vencen
   para esa sesión, en un único 2005. El motor **vuelve a serializar en ese momento**: el byte 1, el hash
   y el tiempo actuales. Nunca se reenvía el 2005 antiguo.
5. **Salida del usuario 2 y épocas (C4r).** Cuando el usuario 2 se va o se está yendo:
   - desde el tick de red del mod (sin llamar al juego) se sueltan las entradas **que observa el usuario
     2**;
   - un NPC que controlaba el usuario 2 y observa el anfitrión **se queda**. Se libera cuando el
     servidor pasa su control al anfitrión ("the observer is now its controller") o cuando llega su
     primera acción; si no, vence a los 10 s.

   El cambio de época suelta todo lo anterior en el siguiente barrido.

**[V] Plazo de 10 s.** El máximo medido es 540 ms y el p99 364 ms. 10 s es unas 20 veces el máximo,
pero sigue acotado.

**[E]** Un NPC que vence queda **invisible** para ese observador hasta que el motor lo vuelva a
anunciar, cuando el observador entra en sectores nuevos que lo contienen. Si para entonces sigue a 0, se
retiene otra vez. Es el fallo seguro; la alternativa insegura sería publicarlo con 0.

### 1.3 Paquetes de movimiento y acción de un NPC retenido

**Decisión: no se filtran, como en D-53.** Mientras dura la retención, el servidor sigue relevando al
observador las 2697/2991/2723/2404/2642 de ese NPC (P5 `relay_fix`).

- **[V] Sesión 3** (`p8-sesion3-shadow\cdmp_packets_20261009-161853.bin`, SHA-256 `BE3B70E2…`). El
  invitado ya recibía de forma nativa paquetes de **137 claves** antes de su primer 2005:
  - 57 × 2991;
  - 99 × 2697;
  - 80 × 2642;
  - 8 × 2404.

  En la sesión 4: 42 paquetes de una sola clave.
- **[V] Manejador de 2991** `0xB57720`. Busca el actor por clave en `0xB579C1` y, si el resultado es nulo,
  salta a `0xB57A5F` sin llamar a `0x48AD40` (`test rdx,rdx ; je 0xB57A5F` en `0xB579F9`).
- **[E]** Para 2723 y 3095, `companeros-publicacion.md` §3 dice lo mismo (`FindByKey` sin actor → nada).
  El resto de manejadores no se ha revisado uno a uno; esos paquetes se pierden igual que hoy.
- La 2723 de la primera acción sale **dentro** del manejador, antes del 2005 liberado, y se descarta por
  clave desconocida. Lo que importa (la acción y la posición) va en el 2005 nuevo, con restauración 1.
  Así se aplica por la vía nativa `0x8646D0` y no por 2723, que en un remoto devuelve éxito sin aplicar
  (§2).

### 1.4 Límites

- **[E]** No arregla S1. El 2005 liberado lleva byte 1 y el invitado restaura con `0x8646D0`. Los NPC que
  necesitan un gráfico dinámico (los 15 de S1) pueden fallar igual con `eErrNoInvalidActionChart`; solo
  se cierra la ventana "aparición con 0 → Update sin acción" (Crow y otros).
- Solo con `create_session=1`. Con `child_hold=0` y `observer_hold=1` se instalan los mismos hooks, pero
  los hijos no se retienen (`ChildrenOff`).
- No probado en vivo. El primer paso puede hacerse en un solo PC con `cdmp_peer`, porque el peer recibe
  los 2005 de NPC del anfitrión (§8).

## 2. Invitado: sondas de S3 (`guest_s3_diag`)

Solo lectura. No llama a ninguna función del motor. Cada suceso escribe un **registro fijo de 128 B**:
- QPC, hilo, época de la conexión del invitado (32 bits bajos), clave y controlador;
- datos propios del suceso.

Los registros van a un **anillo sin bloqueos** (`s3::Ring<8192>`, ~1,1 MB):
- los escritores no esperan nunca (un `fetch_add` y un `store` con *release*);
- el lector descarta las ranuras a medio escribir (secuencia antes y después de copiar);
- lo más antiguo se sobrescribe y se cuenta como "lost before printing".

Nada se formatea ni reserva memoria en los hooks. El texto se genera en el **tick de 5 s del bucle de
red** (`guest_s3_tick`), con un tope de 150 líneas por tick. El exceso se cuenta.

| Suceso | Dónde | Qué se guarda |
|---|---|---|
| 2005 recibido | `guest_deliver`, antes de encolar al cliente | por registro de tipo 1–6: tipo, clave, dueño, controlador `+0x12`, **byte de restauración** (tras la ruta `…app_xml`: u32 longitud + ruta + u64 sesión + u8) |
| 2723 / 2991 recibidos | ídem | clave `+0`, hash `+8`, tamaño |
| resultado de la restauración | salida `0x864B1D` de `guest_actor_diag` (llamada añadida, ver §7) | byte, código |
| primer `Update` | `0x85FBA0`, una vez por clave remota | capas (gráfico/actual/reproducción ×2), `+5E`, C1/C2/CF/C8, `CC+0xA8` |
| consumidor de 2723 | `0x48B120` | C8 |
| `vf188(5)` | `0x48B197`, AL | **AL** (0 = "éxito sin aplicar"), C8 |
| consumidor de 2991 | `0x48AD40` | flag R9B, `[[CC+0x60]+0x10]&1`, "descartada" |
| StartAction | detour de `0x863530` | resultado, llamador (2723 / 2991 / otro, por dirección de retorno), sesión, capas **justo después** |
| acción nula | `0x232D5C0` con `[r8+0x10]==0` (primera vez por clave) | capa, ambas capas `+0/+8/+10/+18/+110/+3C`, `CC+70/+78/+A8`, actor `+5E/+C1/+C2/+CF/+C8`, remoto o no, guardada o no |

Distingue los tres hechos que pedía el encargo:
- **recibido**: `rx 2005/2723/2991`;
- **devolvió éxito**: StartAction con resultado 0, o `vf188` AL=0, que devuelve 0 sin aplicar;
- **acción instalada**: `L0 cur` no nulo tras `0x863530`, o la recuperación de la guarda.

**Política de impresión.**
- Siempre se imprimen:
  - las acciones nulas;
  - `vf188` de remotos;
  - los 2005 con byte 0;
  - el primer Update con gráfico y sin acción;
  - las restauraciones fallidas;
  - las 2991 descartadas;
  - la primera StartAction por actor y consumidor, y toda StartAction que no instala.
- Cuando una clave se vuelve "interesante" (acción nula o guarda), se vuelca **su historial** (hasta 40
  registros del anillo) una sola vez.
- Además, cada tick, una línea de resumen con todos los contadores.

## 3. Invitado: captura de cierre (`guest_crash_dump`)

- **Manejador vectorizado** (primero de la lista). Actúa solo con `EXCEPTION_ACCESS_VIOLATION` cuya
  dirección está en **`.rsrc`** del ejecutable, el código real del juego. `.xtls` y el mod quedan fuera:
  las lecturas protegidas del propio mod y los trucos del protector no lo disparan.
  - Escribe una línea con la dirección (RVA), el tipo de acceso, la dirección leída y los registros.
  - Vuelca los **últimos 64 registros** del historial a `cdmp.log`.
  - Si un registro parece un CC (su `+8` es un actor con clave `0xA…`/`0xB…`), lo describe.
  - Pide el minidump a un **hilo dedicado** (como hacen los reporteros) y espera 30 s como máximo.
  - **Devuelve siempre `EXCEPTION_CONTINUE_SEARCH`**: nunca maneja la excepción. Los manejadores del juego
    y K1 siguen igual.
- **Filtro de nivel superior**, encadenado al anterior (`SetUnhandledExceptionFilter`, llama al previo y
  devuelve lo que este devuelva). Cubre una excepción no manejada de otro tipo. Si el juego lo sustituye
  después, queda el vectorizado.
- **Minidump** `cdmp_crash_AAAAMMDD-HHMMSS_<pid>_<n>.dmp` junto a `cdmp.asi`. Solo local; no se envía
  nada.
  - `dbghelp.dll` de System32, cargado al instalar.
  - Tipo: `IndirectlyReferencedMemory | ThreadInfo | UnloadedModules | IgnoreInaccessibleMemory`.
  - Con `MemoryCallback` se añaden el anillo S3 completo, 256 B alrededor de cada registro con puntero y,
    si hay un CC, el CC, el actor y las dos capas.
- **Topes:** 8 sucesos registrados, 2 dumps, y no se repite un dump por la misma dirección.

**[E] Riesgo:** una AV de primera oportunidad en `.rsrc` que el juego maneje él mismo daría un dump
espurio (el tope es 2). No hay precedente en el repositorio de VEH o UEF del mod.

## 4. Invitado: contención en `0x232D5C0` (`guest_update_guard`) — CONTENCIÓN, no arreglo

**Punto elegido: la entrada de `0x232D5C0`.** De las tres opciones del encargo es la más estrecha.

- **[V] Tiene un solo llamador.** Es `0x232D5AD`, dentro de `0x232D2A0`; el barrido rel32 de todas las
  secciones ejecutables no encuentra otro.
- **[V] Devolver en seguida es exactamente su rama "acción sin entradas":**

  ```
  0x232D5FB  mov rax,[r8+0x10]
  0x232D5FF  movzx eax,word [rax+0xE4]
  0x232D60B  test ax,ax
  0x232D60E  je 0x232E0D9    ; epílogo directo
  ```

  Antes solo hay el prólogo (guarda argumentos en el espacio *home* y empuja registros). El epílogo
  `0x232E0D9–0x232E0EC` solo restaura registros y retorna. La función es `void` (decompilado:
  `if (count != 0) {...} return;`), y `0x232D2A0` no usa nada que `0x232D5C0` escriba en ese camino:
  `local_218` sigue a su valor inicial.
- **Qué se salta:** el procesamiento de entradas/eventos de **esa capa en ese frame**, para un actor que no
  se controla aquí y no tiene acción actual. No hay nada coherente que procesar.
- **Por qué no las alternativas:**
  - Los sitios `0x85FC94`/`0x860110` son llamadas en medio de Update: habría que saltar la llamada y
    reconstruir sus efectos.
  - La barrera de Update `0x85FBA0` (D-52) salta todo el Update del actor: movimiento, estado y demás
    capas. Es más amplia de lo necesario.

**Predicado.** `snapshot+0x10 == 0` y `actor+0x90 ≠ clave local`. La clave local se lee cada segundo, como
en `guest_actor_diag`; mientras no se conoce, no se guarda nada. Los actores propios nunca se tocan.

**Registro.** Tabla sin bloqueos por clave (1.024 entradas) con:
- primer y último salto;
- número de saltos;
- **instante en que el actor vuelve a entrar en `0x232D5C0` con acción**, es decir, que recibió una
  acción.

Mientras quede alguna clave abierta, cada entrada con acción comprueba si es una de ellas.

En el tick se registran:
- `holding` para cada clave nueva (las 64 primeras);
- `has a current action again` en cada recuperación;
- **`STUCK`** para la que lleva más de 5 s sin acción. Así un actor atascado queda diagnosticado.

**No cubre** otros lectores de la acción nula:
- `0x1831598` (minimapa, 10 llamadores);
- `0x17F328F` (3095);
- `0x88F832` (PostUpdate);
- `0x1F63FE6`, que ya cubre `guest_layer_guard`.

### 4.1 Log de la guarda de capas existente

La guarda de `0x2329700` prueba `chart && (!current || !playback)` (`p8-raiz-invitado.md` §5.2). Ahora
`remote_puppet` guarda **por hilo** qué capa y qué puntero faltan, y la línea `GuestDiag guard: holding
actor …` dice:
- `layer N has a chart and NO CURRENT ACTION (+0x10; its playback +0x18 is set)`;
- `… NO PLAYBACK (+0x18; …)`;
- `… neither …`.

`puppet_guard_describe()` suma los contadores `chart && !current only`, `chart && !playback only` y
`both`; en el anfitrión no cambia nada, porque solo se imprime con clave local. La condición de la guarda
es la misma de antes: solo se leen los dos punteros antes del gráfico, dentro del mismo `__try`.

## 5. Tabla de hooks

**[V]** Todos los sitios están en `.rsrc [0x1000, 0x51EB000)`:
- bytes contrastados con `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (SHA-256
  `57DA440D…E1E7`, 384.521.624 B);
- límites de instrucción con capstone 5.0.7;
- `.pdata` del directorio de excepciones;
- **ningún** rel32 (E8/E9/0F8x) de ninguna sección ejecutable cae dentro de los bytes parcheados, ni
  ningún salto de la propia función (barrido lineal).

En ejecución, cada sitio:
1. compara los 14–16 bytes esperados;
2. exige `.rsrc` en la tabla de secciones en memoria;
3. crea el hook **deshabilitado** y compara `original_bytes()` de SafetyHook (la longitud que de verdad
   va a sobrescribir) con los bytes verificados.

Si algo falla: `GuestS3: … REFUSED` y la ruta original.

| Sitio | Bytes verificados (prefijo) | Parche | Tipo | Llamadores directos [V] | Coexistencia |
|---|---|---|---|---|---|
| `0x232D5C0` | `4C 89 4C 24 20 4C 89 44 24 18 88 54 24 10 48 89` (+ lectura `0x232D5FB 49 8B 40 10 0F B7 80 E4 00 00 00`) | 5 (`mov [rsp+20],r9`) | mid | `0x232D5AD` | Mismo sitio que la sonda de compañeros del anfitrión (C2-0, mismo parche de 5 B ya usado en vivo); en el invitado no puede estar (`guest=1` excluye lo del anfitrión) y, si lo estuviera, los bytes no cuadrarían y se rechaza |
| `0x85FBA0` | `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57` | 5 | mid | `0x8B5761`, `0x8B5868`, `0x8B5926` | `horse_diag` (D-52) también lo engancha, solo en el anfitrión; mismo rechazo por bytes |
| `0x48B120` | `48 89 5C 24 10 48 89 74 24 18 55 57 41 54 41 56` | 5 | mid | `0xB57E8C` | — |
| `0x48B197` | `84 C0 75 17 44 89 26 48 8B 8D 80 00 00 00` (+ `0x48B18F B2 05 FF 90 88 01 00 00`) | 7 (`test al,al ; jne rel8 ; mov [rsi],r12d`; SafetyHook reubica el `jne` corto como rel32) | mid | — | Solo con el de `0x48B120` |
| `0x48AD40` | `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55` | 5 | mid | `0xB57A5A` | — |
| `0x863530` | `48 89 5C 24 10 4C 89 44 24 18 48 89 4C 24 08 55` | 5 | inline (4 argumentos enteros, ninguno en pila: prólogo `0x863530–0x863557`) | `0x48AE9D` (2991), `0x48B267` (2723) | — |
| `0x2B000C0`, `0x296CAA0` | los de D-53 | — | inline (existentes) | — | `observer_hold` no añade hooks |

No hay solapamiento con los hooks del invitado:
- `guest.cpp`: Send y la bomba del NetworkThread;
- K1 (reportero);
- `guest_actor_diag`: `0x4FD491`, `0x8646FC`, `0x864B1D`;
- `guest_layer_guard`: `0x1F63F70`, `0x2329700`.

Nada en `.xtls` ni en thunks hacia ella.

Scripts de verificación (scratchpad de la sesión, `python -I`):
- `pe2976.py` (`sha`, `sites`, `callers`, `dis`);
- `s3cap.py` (anfitrión);
- `s3guest.py` (paquetes antes del 2005);
- `extract2005.py` (fixtures).

## 6. Invariantes

- **Sin las claves nada cambia.** Con `observer_hold=0`:
  - `prepare()` llama a `hold_verdict()`, el mismo código que antes (`hold_verdict_ex(...,true,nullptr)`);
  - la tabla de NPC está vacía;
  - el barrido y el tick tienen exactamente las mismas condiciones de salida para los hijos;
  - las líneas de log de D-53 no cambian.

  En el invitado, con las tres claves a 0, `guest_s3_start` no crea el hilo ni instala nada.

  Cambios de texto, no de comportamiento:
  - la línea `Config:` añade los cuatro valores;
  - `GuestDiag guard: …` dice qué puntero falta.
- **Retención:**
  - el controlador nunca se retiene;
  - nunca se libera con byte 0, salvo cuando el observador pasa a ser el controlador;
  - la liberación es la llamada nativa (2005 serializado de nuevo);
  - no se guardan punteros que se sigan: el puntero del actor solo se compara;
  - la tabla está acotada.
- **Sondas:** solo lectura, lecturas con SEH y ninguna llamada al motor. No se reserva memoria ni se
  formatea en los hooks.
- **Captura:** nunca se maneja la excepción.
- **Contención:** solo actores no controlados localmente, solo con la acción actual nula y solo esa capa en
  ese frame.

## 7. Cambios en ficheros

| Fichero | Cambio |
|---|---|
| `src/mod/session2/child_hold.{h,cpp}` | `observer_hold`: predicado de NPC, `NpcHoldTable`, `npc_release_step`, barrido y liberación agrupada por sesión, salida del usuario 2, describe; `HoldTable` pasa a plantilla (misma semántica) |
| `src/mod/session2/guest_s3.{h,cpp}`, `guest_s3_logic.h` | nuevos (sondas, captura, contención) |
| `src/mod/session2/guest.{h,cpp}` | `guest_start` con las tres claves; llamada en `guest_deliver`; describe |
| `src/mod/session2/guest_actor_diag.cpp` | **cableado trivial** (dos cambios): la salida de la restauración llama a `guest_s3_note_restore(...)` y la línea de la guarda incluye el motivo. Es inevitable: el punto `0x864B1D` y esa línea ya son de este módulo y no se pueden enganchar dos veces |
| `src/mod/net/remote_puppet.{h,cpp}` | motivo por hilo y contadores de la guarda |
| `src/mod/session2/session2.{h,cpp}`, `src/mod/config.{h,cpp}`, `src/mod/mod.cpp`, `CMakeLists.txt` | claves, instalación, tick de 5 s |
| `tests/optd_s3_tests.cpp`, `tests/data/p8s4_2005_{12334,1191}.bin` | tests y fixtures |
| `docs/research/ini/cdmp.p8-host.ini`, `cdmp.p8-shadow-s3.ini` | ini |

**Fixtures [V].** Son payloads 2005 completos extraídos de `p8-sesion4-shadow\cdmp_packets_20261009-170454.bin`:
- registro CDPK #12334: Crow `B01002A0`, 6.637 B, SHA-256 `6D6C2D03…4917`, restauración 0 en `+0x123B`;
- registro #1191: cuatro NPC `B0100078–7B`, 28.532 B, SHA-256 `C48F7053…8CA1`, restauración 1 en
  `+4808/4804/4828/4824`.

Son datos de la partida del usuario; el integrador decide si se versionan.

## 8. Líneas esperadas y cómo leerlas

**Anfitrión:**
- `Session2 observer hold ON (S3): …`.
- `Session2 observer hold: held NPC 0x… (type 6, controller host 0x90100000, owner none, restore 0) from
  session 0x… (user 2 0x…); its controller got it now; epoch N`.
- `Session2 observer hold: released NPC 0x… … after N ms, restore 1 (after StartActionReq; K key(s) in
  that 2005)`. Se espera N ≈ 0,15–0,6 s.
- `… dropped after N ms: despawned | observer out of range | another guest epoch | …` y `TIMEOUT: …`.
- En el describe de cada minuto: `observer hold (S3): NPCs held … released … in … NotifyAppear (by control
  …; mean … ms, max … ms) | dropped … (timeouts …) …`.

**Invitado:**
- `GuestS3 ON: probe …, UPDATE GUARD … | sites: 0x232D5C0 on | … | crash capture on | patched bytes checked
  inside .rsrc …`, o `GuestS3: … REFUSED`.
- `GuestS3 [hh:mm:ss.mmm T… e…] rx 2005 0x… ctrl 0x…: type 6 owner 0x00000000 restore 0`. Con
  `observer_hold=1` en el anfitrión, estas líneas deberían **desaparecer** para los NPC del anfitrión.
- `… 2723 vf188(5) 0x…: AL 0 (C8 0) -> returns success WITHOUT applying (remote control)` acredita el
  descarte de 2723.
- `… StartAction 0x…: from 2991 0x48AD40 … -> result 0x00000000 | after: L0 chart 1 cur 1 … -> action
  INSTALLED`, o `success WITHOUT an action on L0`.
- `… NULL ACTION at 0x232D5C0 0x…: layer 0 snapshot+10 NULL (not controlled here, GUARDED) | L0 … | CC+70 …`,
  seguido de `GuestS3 history of 0x…` con su secuencia: 2005 → restauración → primer Update → 2723/vf188 →
  2991 → StartAction.
- `GuestS3 update guard: holding 0x…`, `… has a current action again N ms after …`, `… is STUCK …`.
- `GuestS3: 2005 records … | … | StartAction installed/success-without-action/error: from 2723 a/b/c, from
  2991 …` (resumen).
- Si hay cierre: `GuestS3 crash capture (first chance, vectored…): exception 0xC0000005 at exe+0x232D5FF
  (read of 0xE4) …`, los 64 registros y `minidump … written (local only)`.
- `GuestDiag guard: holding actor 0x… (…): layer 0 has a chart and NO CURRENT ACTION (+0x10; its playback
  +0x18 is set)`.

**Prueba sugerida sin Shadow.** `cdmp.c4r.ini` con `observer_hold=1` y el guion del peer en un solo PC:
- deberían salir `held NPC … from session (user 2)` y, ~0,2 s después, `released …`;
- en la grabación del peer, el 2005 de esos NPC con byte 1;
- ningún evento 1000 en el anfitrión.

## 9. Coste

- **Hooks del invitado.** Un mid-hook de SafetyHook guarda todos los registros (también XMM) y llama a la
  función. **[E]** Cuesta del orden de 0,1 µs por llamada.
  - `0x85FBA0`: una vez por actor y frame. Con unos 300 remotos a 60 fps son ~18 k/s: unos 2 ms de CPU por
    segundo, repartidos entre los hilos de la tarea paralela. Para cada actor ya visto, el trabajo es 3
    lecturas y una sonda del conjunto.
  - `0x232D5C0`: una vez por capa con gráfico; carga parecida. Con acción, solo 1 lectura, más 3 lecturas
    y una búsqueda mientras quede una clave guardada sin recuperar.
  - `0x48B120`/`0x48B197`/`0x48AD40`/`0x863530`: por acción recibida (decenas a cientos por segundo).
    Despreciable.
- **Memoria.**
  - Anillo: 8.192 × 136 B ≈ 1,1 MB.
  - Cinco conjuntos de 8.192 claves: 160 KB.
  - Tabla de la guarda: 1.024 × 48 B.
- **Log.** Como máximo 150 líneas cada 5 s, más una línea de resumen si algo cambió.
- **Anfitrión.** Para cada clave `0xB…` de un `NotifyAppear`, una lectura más (`+0x90`) y la cadena del
  CC. Una entrada de la tabla por NPC y observador durante ~0,2 s. Por cada `StartActionReq` con
  entradas, unas lecturas por entrada.

## 10. Resultados de build y tests

`.\scripts\build.ps1` (RelWithDebInfo): **build OK, 16/16 tests**, ninguna advertencia en los ficheros
tocados. `optd_s3` cubre:
- predicado de NPC (controlador anfitrión/usuario 2/0/desconocido, sin CC, restaurado, hijos activados o
  no);
- reparto (el controlador recibe en la misma llamada; segunda llamada solo al usuario 2; solo el
  controlador → nada);
- tabla (añadir, duplicado, tope de 256, `forget_announced`, salida del usuario 2);
- liberación (byte 0 joven/vencido, byte 1, fuera de alcance con 0 y con 1, controlador cambiado,
  observador que pasa a controlador, despawn, otro actor tras la clave, dueño, época, observador,
  ilegible y "stale");
- lectura por clave con un SAM falso;
- anillo (límites y 4 escritores × 50.000 contra un lector, sin registros rotos);
- conjunto de claves y tabla de la guarda (recuperación única, lleno);
- predicado de la guarda y rangos de llamador;
- parser 2005 sobre los dos payloads reales (desplazamientos exactos) y casos sintéticos (registro de
  usuario, gimmick, longitud que no cuadra, byte ≠ 0/1, truncado).

## 11. Preguntas abiertas

1. **`observer_hold` en vivo:**
   - ¿coincide en el juego la liberación tras la 2223 del anfitrión con lo medido (0,14–0,54 s)?
   - ¿cuántos NPC vencen?
   - ¿qué carga añade en la ráfaga de entrada de campo? (2005 más pequeños y separados).
2. Si el servidor puede dar al cliente del usuario 2 el control de NPC (`+0x90` = usuario 2), su
   retención para el anfitrión depende de la latencia de Steam (1–2 s en P8).
3. Que el VEH/UEF convivan con Sentry y con el protector no está probado. Una AV de primera oportunidad
   manejada por el juego daría un dump espurio (tope 2).
4. La contención no cubre los otros lectores de la acción nula (§4). Si aparecen en el dump, son
   evidencia para el arreglo de activación (§6 fila 5), no para añadir más saltos.
5. S1 y S2 siguen abiertos (los trata el otro implementador). Un NPC liberado con byte 1 puede caer en el
   fallo de restauración de S1.
6. Los conjuntos "primera vez por clave" no se vacían al cambiar de época (C4r con el mismo proceso). Las
   claves del servidor no se reutilizan dentro de una partida del anfitrión **[E]**.

## 12. Correcciones tras la revisión (9 de octubre de 2026, sobre `6276700`)

Revisión independiente del código de S1/S2/S3. Esta sección **sustituye** lo que diga en contra §1.2 (pasos
4 y 5, tabla de liberación, tabla de 256), §3 (captura de cierre), §6 y §9. Nada se ha ejecutado en el juego:
solo build y tests (§12.6). Con `observer_hold=0` el anfitrión se comporta como antes (§12.5).

Evidencia en vivo que motivó los cambios (prueba de un solo PC del usuario, anfitrión + `cdmp_peer` como
usuario 2, `observer_hold=1`):
- tras cerrar el modal del anfitrión, **320 NPC retenidos** para el usuario 2 en ~2 min;
- en una prueba anterior se llenó la tabla de 256: `table full -> native 372`;
- liberaciones tras `StartActionReq`: media **2,6 s**, máximo **6,9 s**;
- las criaturas ambientales de tipo 6 siguen con restauración 0 aunque el cliente del anfitrión mande muchas
  `StartActionReq` para ellas. Por eso el plazo de 10 s **libera de forma nativa con restauración 0** (cambio
  del usuario en `6276700`, se mantiene);
- el juego no se trabó a simple vista, pero no se midió. Ahora el describe lo mide (§12.1).

### 12.1 I1 — coste del barrido de `observer_hold` en el hilo lógico del servidor

Antes, cada `StartActionReq` recorría hasta 256 entradas bajo `g_mu`, cada una con 20–40 `game::safe_read`
(un `VirtualQuery` por lectura), y `prepare()` tomaba `g_mu` en cada `NotifyAppear`.

Ahora:
- **Pasada dirigida.** El detour de `StartActionReq::vf2` lee la clave del actor de la propia Req **antes** de
  llamar al original y, después, solo evalúa las entradas de esa clave.
  - **[V] Cómo se identifica la clave** (`exports/decomp/0x296caa0.c`): `ctx` (r8) es el envoltorio del
    paquete. `lVar7 = param_3[3]` es el bruto en `[ctx+0x18]` = {u16 opcode, u8, u16 longitud, carga}. vf2
    exige `u16 [ctx+0x10] - 5 == u16 [bruto+3]` y deserializa desde `bruto+5`. La carga empieza por la
    clave u32 (`opcion-d-accion-usuario2.md` §2.2: `+0x00` clave). `start_action_req_key()` hace la misma
    comprobación de tamaños; si no cuadra, no hay pasada dirigida (queda la periódica).
- **Pasada periódica acotada.** Como mucho cada `kNpcPassMs` = 100 ms, y como mucho `kNpcPassBudget` = 256
  entradas por pasada, en turno rotatorio: una tabla llena se cubre en 0,4 s. Corre tras `StartActionReq` y
  también en la cola de los `NotifyAppear` del propio juego (§12.3).
- **Lecturas SEH directas.** La ruta de `observer_hold` usa `game::fast_read` (`__try` + carga `volatile`,
  sin `VirtualQuery`): `hold_verdict_ex`, `read_npc_release`, la búsqueda en el SAM
  (`sam_find_actor_with<FastReads>`, la misma lógica que `sam_find_actor`, ahora plantilla) y
  `server_realm()`. D-53 (`hold_verdict`, `read_child_release`) sigue con `safe_read`, igual que antes.
  - No se usan en pilas de hilos (una página de guarda tocada así perdería la guarda): actores,
    componentes, sectores y el mapa del SAM están en el montón.
- **Decidir fuera del candado.**
  1. bajo `g_mu` solo se **copian** las entradas;
  2. la decisión se toma fuera, con una validación de sesión por observador y pasada (no por entrada);
  3. se vuelve a tomar `g_mu` solo para aplicar, y solo a la entrada que sigue ahí sin cambios.
     `HoldEntry::stamp` cambia en cada alta o refresco; si no coincide, la entrada se decide en la siguiente
     pasada. Se aplica de mayor a menor índice: el borrado mueve la última al hueco.
  4. Las llamadas nativas van fuera del candado. Los búferes son estáticos (una pasada a la vez,
     `atomic_flag`; si otra está en curso, se salta y se cuenta): **sin asignar memoria**.
- **`prepare()`** solo toma `g_mu` cuando algún NPC de la llamada se va a retener (para reservar sitio,
  §12.2). `forget()` ordena una copia de las claves fuera del candado y la tabla las busca por bisección.
- **Capacidad de la tabla de NPC: 256 → 1024.**
  - **[V]** La captura completa del anfitrión de la sesión 4 tiene **956** primeras apariciones de NPC con
    controlador usuario y restauración 0, y con dos usuarios cada NPC tiene un solo observador. 1024 caben
    todas a la vez.
  - En vivo se llenaron 256 durante una ráfaga: las liberaciones tardaban 2,6 s de media y las de tipo 6
    esperan los 10 s.
  - Sigue acotada: unos 100 KB estáticos y pasadas de como mucho 256 entradas.
- **Medida** (describe de cada minuto):
  - `passes: targeted N, budgeted M (E entries; mean X us, max Y us; busy-skipped …; releases deferred …)`;
  - `split max Z us` (tiempo de `read_call` + `prepare` con `observer_hold`).

  Con esto la próxima prueba dirá si la carga es apreciable.

### 12.2 I2 — NPC invisibles por entradas soltadas

- **Sitio reservado dentro de `prepare()` bajo `g_mu`.**
  - Por cada NPC retenible se cuentan sus observadores. Si no caben en `capacidad − tamaño − reservado`,
    ese NPC se anuncia de forma nativa (`TableFull`, como sin `observer_hold`).
  - Lo reservado se devuelve bajo el mismo candado al registrar (`record`) o al abandonar el reparto (RAII
    `NpcReservation`).
  - Invariante: `tamaño + reservado ≤ capacidad`. El caso "table full **after** the split" ya no puede darse
    (se conserva el contador `without entry`, debe quedar a 0).
- **Soltar → anunciar de forma nativa** cuando el actor sigue existiendo y el observador lo sigue
  observando (`observes == true`). Nuevo `NpcRelease::Native`, en el log `announced natively instead of
  dropped: <motivo>`:
  - **otro actor tras la clave** → nativo;
  - **sin poder leer durante 3 × 10 s** ("stale") con el actor en alcance → nativo; fuera de alcance o sin
    saberlo → se suelta como antes;
  - **ahora tiene dueño (es hijo de un usuario):**
    - byte 1 → se libera (como D-53);
    - byte 0 y `child_hold=1` → **espera** (regla de D-53: nunca se anuncia con 0 a otro usuario) y a los
      30 s se suelta;
    - byte 0 y `child_hold=0` → nativo, como haría el motor.
  - Se siguen soltando, porque no dejan nada invisible en alcance:
    - otra época;
    - despawn;
    - observador sin usuario válido;
    - observador fuera de alcance (el motor lo vuelve a anunciar al entrar y se retiene otra vez si sigue
      a 0).
- **Observador = anfitrión** (NPC que controla el usuario 2). Liberar con restauración 0 es lo que hace el
  motor sin `observer_hold`. Lo cubre la contención de `Update` del anfitrión (**D-52**, `horse_diag`
  `0x85FBA0`), igual que hoy para cualquier actor remoto sin acción.
- **Duplicado (M6).** Un NPC retenido otra vez por un anuncio nuevo **refresca** su instante, época, actor,
  tipo y avisos: no vence antes de tiempo y no cuenta como "otro actor tras la clave". Los hijos de D-53
  conservan su primer instante, como antes.

### 12.3 M5 — las entradas avanzan sin `StartActionReq`

- **[V] Hilos.** `NotifyAppear` necesita el byte TLS de ámbito servidor (`0x2B06147`). Pero el hilo de
  `OnRecvPacket` (`server_thread_id`) **no** es el de la lógica del servidor, donde corren los manejadores y
  el propio juego llama a `NotifyAppear` (T29232, `opcion-d-diario.md` C2/D-53r).
  - Llamar a `NotifyAppear` desde el hilo receptor mientras el hilo lógico cambia actores y sectores no
    está verificado. **No se hace.**
- **Respaldo del hilo receptor** (`child_hold_server_tick`, cada `kNpcFallbackMs` = 250 ms, sin exigir
  `g_appear_tid`): pasada periódica que **solo suelta y anota**. Las liberaciones que tocan quedan
  **diferidas** (contador `releases deferred from the fallback`).
- **Cola de `NotifyAppear`.** La pasada periódica (cada 100 ms) corre también al final de cada
  `notify_detour`, en el hilo donde el juego llama a `NotifyAppear`. Allí liberar es legítimo, igual que las
  llamadas que ya hace el reparto. Así los vencimientos y las liberaciones diferidas avanzan aunque no llegue
  ninguna `StartActionReq`.
- El respaldo de los **hijos** (D-53) queda **como estaba**: exige el hilo de `NotifyAppear`, así que en la
  práctica no corre. No se ha tocado para no cambiar D-53 con las claves nuevas apagadas; queda anotado como
  pendiente.

### 12.4 M7 — nada de formato bajo `g_mu` ni en hilos del juego

- **`child_hold`.** Cada línea es un registro fijo (`p8p::Event`) en un anillo sin bloqueos de 1024: un push
  son unas pocas escrituras, válido incluso bajo `g_mu`. `child_hold_network_tick` lo formatea en el hilo de
  red del mod. Los textos son los de antes (más `released … announced natively instead of dropped: …`).
  - Los topes por tipo de línea se mantienen.
  - Si el anillo se llena, se cuenta y se avisa (`log line(s) lost`).
  - Afecta también a las líneas de D-53: mismo texto, escritas unos milisegundos después por el hilo de red.
    **Riesgo:** si el juego se cierra de golpe, puede perderse la última tanda de líneas.
- **`guest_actor_diag` `on_restore_exit`.** El registro (clave, controlador, vtables, byte, código, capas
  antes y después empaquetadas) va a un anillo de 256 y lo imprime el tick de 5 s (`restore_report`) con
  el mismo texto, más el hilo y la hora QPC. `error_name()` se resuelve en el tick.

### 12.5 Invitado: I3/M2 (captura), M3 (anillo), M4 (aviso), I4 (conjuntos)

- **I3 Captura de cierre** (sustituye §3):
  - El **VEH solo registra.** Un AV dentro de `.rsrc` → un `Record` `Ev::Crash` en el anillo (código, RVA,
    tipo de acceso, dirección, rax/rbx/rcx/rdx/rsi/rdi/r8/r9/rsp), como mucho 64.
    - No formatea, no toma candados, no hace E/S, no llama a `VirtualQuery` ni espera.
    - El tick de 5 s lo imprime siempre: el tick corre también con solo `guest_crash_dump=1`.
  - **Minidump + volcado del historial:**
    - desde el **filtro de nivel superior** (encadenado; `STATUS_STACK_OVERFLOW` pasa directo al filtro
      anterior);
    - desde el VEH **solo** en el sitio conocido `exe+0x232D5FF`, en lectura de dirección < 0x10000 (la
      acción nula de P8). Justificación: el juego no la maneja y el proceso termina. Además, su reportero
      (Sentry) puede sustituir nuestro filtro, así que ahí el VEH es el único sitio seguro.
  - **Se instala tras `code_ready()`** (el código del juego ya está; también con solo
    `guest_crash_dump=1`).
  - El VEH se registra **el último** (`AddVectoredExceptionHandler(0, …)`): los manejadores del juego y del
    protector ven antes sus excepciones.
- **M2:**
  - `s3::CrashGate`: una captura a la vez entre hilos (un cierre concurrente sigue sin esperar), nunca dos
    veces la misma dirección, como mucho 8;
  - `g_crash_on` solo si el VEH se añadió;
  - **rotación** de `cdmp_crash_*.dmp` en la carpeta de `cdmp.asi` (`bin64`): al instalar quedan los 2 más
    nuevos y antes de cada dump nuevo, 1. Se registra cuántos se borraron.
- **M3:** `Ring::push`/`read` copian el `Record` campo a campo (`copy_record`, como `p8p::copy_event`). Sin
  `memcpy` del CRT en los hooks.
- **M4:** con `guest_s3_diag=1` y `guest_actor_diag=0`, aviso al arrancar. Los resultados de restauración
  (`0x864B1D`) solo los aporta `guest_actor_diag`.
- **I4:** `s3::KeySet`, `s3::GuardTable` y `p8p::KeyCounter`:
  - sondeo acotado (`kMaxProbe` = 32);
  - carga máxima N/2;
  - al alcanzarla, una marca **pegajosa** (`closed`) que se mira primero: ya no se intenta insertar y solo
    queda la búsqueda acotada (las claves conocidas siguen contando).
  - Para conservar la capacidad útil: `KeySet<16384>` (8192 claves) y `GuardTable<2048>` (1024 actores).
  - `on_update` (`0x85FBA0`, una vez por actor remoto y frame) ya no recorre 8192 ranuras con el conjunto
    lleno.

### 12.6 Tests y build

`tests/optd_s3_tests.cpp`, con costuras de test en `child_hold.h` (`hold_test::Seams`: `NotifyAppear`,
SAM, reloj y ámbito falsos; el mod nunca las usa):
- `start_action_req_key`: tamaños que cuadran, que no cuadran, carga corta, `ctx` nulo;
- **integración `record()` + pasadas:**
  - reparto: la otra clave a todos y el Crow solo al controlador;
  - entrada con `stamp` y `g_npc_size` coherente con la tabla tras cada paso;
  - **liberación dirigida** tras la `StartActionReq` del Crow (no por la de otra clave);
  - **periódica limitada a 100 ms** (99 ms no, 100 ms sí);
  - periódica en la **cola de `NotifyAppear`** sin `StartActionReq`;
  - **vencimiento** con restauración 0;
  - **refresco del duplicado** (M6);
  - **otro actor tras la clave → nativo**;
  - fuera de alcance → soltado sin llamada;
  - **respaldo que difiere** la liberación y sí suelta (sesión perdida);
  - **sitio reservado** (tabla llena → nativo sin entrada perdida; con un hueco cabe justo y la reserva
    vuelve a 0);
  - salida del usuario 2;
  - líneas formateadas por el tick de red;
- reglas de liberación actualizadas (`NpcRelease`, nativo en vez de soltar, dueño con `child_hold` on/off,
  "stale" en alcance);
- conjuntos acotados (pequeño y de tamaño real, cierre, claves conocidas, 4 hilos);
- `CrashGate` (Busy/Repeat/Spent; 8 hilos a la vez: entra uno);
- `optd_p8_probe`: `KeyCounter` acotado (pequeño y 8192).

`.\scripts\build.ps1` (RelWithDebInfo + ctest): **build OK, 17/17 tests** (`optd_s3`, `optd_p8_probe`,
`optd_child_hold` y `optd_c4` incluidos), sin avisos nuevos en los ficheros tocados.

### 12.7 Pendiente / no hecho

- El respaldo de los hijos de D-53 sigue sin correr (§12.3), a propósito.
- El coste real de las pasadas en el juego no se ha medido; el describe lo da ahora (`mean/max us`).
- Las líneas de D-53 pasan por el anillo (§12.4): un cierre brusco puede perder las últimas.
- Lecturas SEH sin `VirtualQuery` solo en la ruta de `observer_hold`; la de D-53 no se ha tocado.
