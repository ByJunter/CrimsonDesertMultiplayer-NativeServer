# P-RELOJ — el aparcamiento del invitado sustituye el reloj mundial

Build **1.0.0.2976**, base `0x140000000`; todas las direcciones siguientes son RVA. Análisis independiente de 2026-10-09, sin ejecutar el juego ni modificar código. **[E]** desensamblado/decompilado `.rsrc`; **[E-x]** tramo legible de `.xtls`; **[V]** captura o log existente; **[S]** inferencia o diseño pendiente de prueba. Los nombres `cdmp_*` son anotaciones, no símbolos del fabricante.

## Resumen

- **[E + V → S, confianza alta]** El problema empieza en la **primera carga, el campo privado de aparcamiento**, antes del campo compartido. `0x2BC0A30` considera autorizado a quien sea dueño de su campo; el invitado es dueño del aparcamiento y esa función instala su hora fija en el **único global servidor**. La segunda carga compartida toma la rama no-dueño y conserva la hora que ya se ha sustituido. Compartir el segundo campo no protege el primero.
- **[V]** El valor no es todo ceros: es **día 1, 00:00:00.000, día de semana 1, escala 0, ancla 0**. El aparcamiento se anuncia como **día 0, hora 24, escala 0**; la normalización nativa da exactamente día 1 a medianoche y conserva escala 0.
- **[E]** No es una instantánea meramente informativa: `GetCurrentFieldTime` deriva de ella la hora de los campos no fijos y multiplica el avance por la escala. **Escala 0 congela esa hora**, aunque el reloj monotónico y los paquetes periódicos continúen. `UpdateFieldNightAndDay` consume esa hora y cambia `field+0x2E0`.
- **[S]** Arreglo: la autoridad del reloj debe ser **el dueño del mundo**, no todo dueño de cualquier campo. En la entrada `.rsrc` `0x2BC0A30`, toda sesión invitada debe obtener su hora sin publicar en el global; en `0x219D7F0`, su finalización de carga tampoco debe publicar hora ni despausar el campo compartido. El anfitrión conserva su ruta nativa.
- **[V/S]** No hay medición visual ni reloj cliente del anfitrión en estas pruebas. La causa tiene confianza alta por código, valor distintivo y comparación B3; el momento exacto de la escritura y sus efectos visuales requieren la única prueba instrumentada de §6.

## 1. Material y método

**[E]** Se consultó el Ghidra vivo compartido mediante el helper de solo socket `<scratch>\pendientes\query.py`. No se arrancó ni paró desde este agente. Las consultas se guardaron en [reloj/](reloj/): 17 decompilaciones, referencias y contraste capstone. Las decompilaciones consultadas tardaron entre 0,00 y 0,44 s según el servidor; no son pruebas del juego. `.xtls` se usó exclusivamente como lectura: ningún enganche propuesto allí.

**[E]** Capstone leyó únicamente `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, mediante `PE(path)` explícito. Scripts nuevos: `reloj_scan.py` y `reloj_queries.py`, solo en el temporal autorizado; ejecución `python -I -B`. No se leyó el ejecutable original. Se leyó también el código de la sonda: `src/mod/session2/server.cpp:676–682` solo copia 32 B y los imprime.

**[V]** Se procesaron de nuevo cuatro CDPK completos: `D:\cdmp-grabaciones\c1e.bin` (7.474 registros), `c1f.bin` (7.485), `c1g.bin` (13.913), `e3.bin` (38.098). Las direcciones 1/2 se interpretaron con `tools/scripts/analyze_capture.py`. Los números de paquete son **1-based**, los segundos relativos al primer registro de cada fichero. Evidencia reproducible: [capturas-reloj.txt](reloj/capturas-reloj.txt), con tiempo absoluto de captura en microsegundos y cargas hexadecimales.

**[E/S]** Se leyó el contexto común del encargo y las notas `hora-del-dia`, `opcion-d-e3-riesgos`, `opcion-d-campo-compartido`, `vision-a3-guardado-mundo`, `vision-a5-inicio` y `opcion-d-accion-usuario2`. Se verificaron sus afirmaciones relevantes, no se usaron sus etiquetas como prueba. Ejemplo: Ghidra llama a `0x4A2010` `cdmp_FieldInterestNode_GetSessionList`, pero el uso concreto aquí obtiene información estática de nivel; ese nombre no justifica interpretarlo como una lista de sesiones.

## 2. Qué son los 32 bytes y quién los escribe

### 2.1 Estructura y relojes distintos

| Desplazamiento | Tipo y significado | Evidencia |
|---|---|---|
| `+0x00` | `u32` día | [E] `0x20D3360` / `0x20D3460` |
| `+0x04` | `u32` hora; normalmente 0–23, pero el valor fijo del aparcamiento usa 24 | [E] conversiones; [V] primer 2144 en las cuatro grabaciones |
| `+0x08`, `+0x0C`, `+0x10` | `u32` minuto, segundo, milisegundo | [E] `0x20D3360` |
| `+0x14` | `u8` día de semana, recalculado como día módulo 7 | [E] `0x20D3360` |
| `+0x15` | relleno, no viaja por el cable | [E-x] deserialización `0x20CF3F0 → 0xE2B2260` |
| `+0x16` | `u16` escala del reloj; 12 en los valores normales, **0 en el aparcamiento** | [E] multiplicación `0x20E55A0`; [V] capturas |
| `+0x18` | `u64` ancla en la base de tiempo usada por el servidor | [E] `0x219D7F0`, `0x20E55A0`; [V] anclas de 2144 |

**[E/E-x]** Son 32 B en memoria y 31 en el cable: `<5IBHQ`, cinco `u32`, un `u8`, un `u16` y un `u64`. La decompilación de `0xE2B2260` conserva dos constantes ofuscadas y no debe copiarse literalmente como código; las conversiones normales y las cuatro capturas confirman el formato. Fuentes: [fromRaw](reloj/ghidra_20d3360.c), [toRaw](reloj/ghidra_20d3460.c), [deserializador](reloj/ghidra_20cf3f0.c).

**[E]** El global cliente está en `0x6A6D218`; el servidor en `0x6A6D238`. TLS `gs:[0x58] → primer puntero → +0x1EC` elige uno u otro. `0x20E55A0(field,out)` no actualiza el global: devuelve una **copia calculada**. Por eso un snapshot puede permanecer idéntico durante minutos mientras la hora del juego avanza normalmente.

**[E]** Para nivel sin hora fija y campo sin pausa (`field+0x17C == 0`):

```
now = FrameClock_NowMs() + (en cliente ? desfase_cliente_servidor : 0)
raw_actual = toRaw(global) + max(0, now - ancla) * escala
resultado = fromRaw(raw_actual); resultado.ancla = now cuando now > ancla
```

**[E]** El cálculo tiene una escala nocturna adicional: un día raw son 75.600.000 unidades, y la noche avanza al doble. **No altera la conclusión sobre escala 0**. Si el campo está pausado se devuelve el global sin avanzar; si el nivel tiene `info+0x6A != 0`, se devuelve `info+0x48` antes de leer el global. No existe en este getter un reloj mundial independiente almacenado en cada `ServerField`; hay override fijo por nivel, pausa por campo y la referencia global. `TransformSync+0x450` es otra copia del componente, no la fuente ordinaria del getter.

**[E/V]** `0x1416B80` y `ServerTimeInfoAck`2188 pertenecen al reloj de proceso/sincronización, no al día de juego. En C1g siguen llegando 2188 a 4,952; 9,959 y 14,950 s, con ticks crecientes mientras la hora de campo es escala 0. No sería correcto decir que se paran todos los temporizadores del servidor. Evidencia: [GetCurrentFieldTime](reloj/ghidra_20e55a0.c), [capstone](reloj/capstone_20E55A0.txt), [reloj de proceso](reloj/ghidra_1416b80.c).

### 2.2 Inventario de escrituras encontradas

| Entrada / instrucción que publica | Condición y alcance | Marca |
|---|---|---|
| `0x2BC0A30` / **`0x2BC0C50`** | Viaje del dueño de **ese campo**; copia hora fija del nivel o tabla personal `TS+0x5D0`, normaliza y publica 32 B. Cliente alternativo `0x2BC0C6F`. | [E] |
| `0x219D7F0` / **`0x219D8EC`** | `LoadingComplete` con quinto argumento no cero; ancla `TS+0x468=now`, publica `TS+0x450` y pone `field+0x17C=0`, sin comparar dueño. Cliente alternativo `0x219D8F6`. | [E] |
| `0x21A1CC0 → 0xE52E900` / **`0xE52E970`** | Siempre copia al componente `+0x450`; si tiene campo y TLS servidor, publica global solamente si `field+0x180 == [actor+0xA0]+0x60`. Cliente alternativo `0xE52E97E`. | [E-x] |
| `0xA38B00` / **`0xA391A5`** | Aplicación de hora al cliente; rama sin actor actual puede escribir directamente el global seleccionado por TLS. Con actor/campo llama al setter anterior. Alternativa cliente `0xA391AF`. | [E] |

**[E/E-x]** Ghidra encuentra tres escrituras `.rsrc` y una lectura directa de `0x6A6D238`; la búsqueda independiente RIP-relative de capstone añade la escritura legible `.xtls`. Se revisaron también referencias al interior del bloque y a su copia cliente. [Referencias Ghidra](reloj/xrefs_6a6d238.txt), [barrido capstone](reloj/referencias-capstone.txt), [setter legible](reloj/capstone_E52E900.txt).

**[E-x]** El barrido también encuentra `lea rdx,[...0x6A6D23F]` en `0xCFBD160`. No es una escritura de hora: es aritmética de un salto ofuscado que después suma `0x654FF36`. Las falsas instrucciones `adc` del barrido son decodificaciones iniciadas dentro de una instrucción AVX; se descartaron al contrastar límites con Ghidra/capstone desde la entrada real.

**[S, límite]** Este es el inventario de escritores **directos encontrados**, no una demostración de que no exista ningún alias, dirección calculada o camino virtualizado adicional. No se instrumentó memoria del juego. Debe existir una comprobación de invariantes en la prueba para detectar escritores desconocidos.

**[E]** El setter `0x21A1CC0` tiene tres llamadas directas conocidas: `0xA390C1` (cliente), `0x2BC0CD2` (viaje) y `0x2BC8A21` (cambio explícito). La última está dentro de `.rsrc 0x2BC8970`; esa función también exige que el usuario sea dueño del campo y luego difunde `SetFieldTimeAck` por `ToNear`. Fuente: [xrefs](reloj/xrefs_21a1cc0.txt), [SetFieldTime](reloj/ghidra_2bc8970.c).

## 3. Qué carga y qué petición originan el valor

### 3.1 Cadena causal

1. **[V]** El guion manda `LoginCharacterToFieldServerReq`3093 y el primer viaje es a `FieldInfoKey 3`, campo propio del invitado; lo registra H1. En C1g: petición #8 a 0,239691 s y H1 a **07:40:16.553**.
2. **[E]** En `MoveFieldOnceTimer`, el campo se adquiere antes de llamar `0x2BC0A30` (`0x2BC0019`, documentado y contrastado en el cuerpo de viaje). Como su dueño es el invitado, `cmp [rdx+0x180],eax` en `0x2BC0A78` toma la rama de publicación. La hora fija se toma en `0x2BC0ADB–AE5`; `toRaw` y `fromRaw` en `0x2BC0C1C/0x2BC0C32` la normalizan antes de la escritura `0x2BC0C50`.
3. **[V/E → S fuerte]** El primer 2144 muestra `(día 0, hora 24, min 0, seg 0, ms 0, semana 0, escala 0, ancla 0)` en las cuatro grabaciones. `toRaw(0,24:00)=75.600.000`; `fromRaw` da `(día1,00:00,semana1)`, y no escribe escala/ancla. El resultado reproduce **exactamente** los 32 B malos de la sonda. La ejecución de esa instrucción concreta no está trazada en vivo, pero el valor distintivo y el camino convergen.
4. **[V/E]** Las dos `LoadingCompleteReq`2445 de cada guion llevan `00`. `0x219D82B/82F` omite todo el bloque que publica el tiempo. Por ello **no es `LoadingCompleteReq=01` la causa de estas pruebas**. El problema ya existe antes de la segunda finalización de carga.
5. **[V/E → S fuerte]** Después de `GameLoadingStartReq`2437, el segundo viaje entra en el campo compartido. El viajero ya no es dueño, salta a `0x2BC0CBD`, lee `GetCurrentFieldTime` del campo del anfitrión y lo pone en el componente. Ese getter usa el **global ya pisado**. La comprobación local de dueño es correcta, pero no restaura la autoridad mundial perdida en el aparcamiento.
6. **[V/E → S fuerte]** En B3 cada viaje usa un campo propio. La segunda pasada por la rama de dueño instala de nuevo la hora de la partida normal, lo que **oculta** la sustitución del primer viaje en las sondas espaciadas. No prueba ausencia de salto transitorio.

### 3.2 Prueba de red que separa las dos hipótesis

**[E + V]** `MoveFieldCompleteAck`2144 tiene 51 B: `u32 actorKey` + `vec3`12 B + `FieldTime`31 B en **payload+0x10** + `u32` final. La lectura del handler `.rsrc 0xB87B90` confirma la secuencia; los tipos/campos se contrastaron con los cuatro ficheros. No se buscó una secuencia de bytes sin estructura.

| Grabación | Primer 2144: paquete / segundos / hora | Segundo 2144: paquete / segundos / hora | LoadingCompleteReq #1 / #2 |
|---|---|---|---|
| `c1e.bin` | #783 / 0,886843 / día0 24:00, escala0, ancla0 | #7199 / 1,461077 / **día1 00:00, escala0**, ancla116880 | #789 0,886923 `00`; #7474 6,476163 `00` |
| `c1f.bin` | #783 / 0,868249 / mismo valor fijo | #7207 / 1,430770 / **día1 00:00, escala0**, ancla102783 | #927 0,868581 `00`; #7377 1,431421 `00` |
| `c1g.bin` | #783 / 1,113439 / mismo valor fijo | #7375 / 2,131244 / **día1 00:00, escala0**, ancla379435 | #792 1,317309 `00`; #10013 7,136333 `00` |
| `e3.bin` (B3) | #783 / 0,939632 / mismo valor fijo | #945 / 1,464211 / **día6 13:30:27.744, escala12**, ancla109282 | #789 0,939672 `00`; #38085 6,470707 `00` |

**[V]** En ninguna de estas cuatro grabaciones aparece `SetFieldTimeReq`2125 ni `SetFieldTimeAck`2816. El segundo 2144 corrupto llega **antes** de `LoadingCompleteReq #2` en C1e, C1f y C1g; esto refuta atribuir el primer cambio a esa segunda Req. En C1e el fichero termina con la segunda Req, sin su Ack: no se le atribuye un éxito que no quedó grabado.

### 3.3 Qué dicen realmente los logs

**[V]** En `cdmp_20261009-0733_c1g.log`, línea605, **07:40:14.111**, el global es día6 **13:37:44.760**, escala12, ancla `0x18A12`. H1 aparcamiento es 07:40:16.553 (línea615), H1 compartido 07:40:17.789 (616), H2 07:40:17.798–799 (617–618). La sonda siguiente, **07:40:24.128** (642), ve día1/medianoche/escala0/ancla0. El mismo bloque sigue a 07:41:14.146, 07:42:14.165 y 07:43:14.186. Esto acota la transición; no da resolución para decir que ocurrió justo en H2.

**[V]** C1e no demuestra por sí solo esa transición: su sonda de **06:48:31.046**, anterior al peer de 06:49:19.577, ya muestra el valor fijo (el anfitrión también usa aparcamiento al cargar). C1f tiene una sonda normal **06:53:09.294**, pero no una sonda global posterior al invitado en ese log; sí está el 2144 con escala0 en la grabación. Se corrige así una lectura demasiado fuerte del diario.

**[V]** B3 muestra día6 13:30:17.724 antes (**03:08:34.345**) y después (**03:09:24.364**) del login, con ancla `0x740C → 0x1A79F`. El 2144 segundo devuelve hora calculada 13:30:27.744. Que la sonda conserve fecha/hora base no significa continuidad del reloj efectivo: la nueva ancla puede reiniciar desde la hora guardada. Sin sondas durante el primer viaje no se debe afirmar que B3 nunca alteró el reloj. [Extracto con líneas](reloj/logs-reloj.txt).

## 4. Qué se rompe y qué no está demostrado

**[E] Reloj mundial servidor.** La escala0 hace que `0x20E55A0` devuelva día1 a medianoche para todo campo no fijo que use ese global. Cambia el ancla del resultado a `now`, pero multiplicar por cero no avanza la fecha. No hace falta observar el cielo para demostrar que este es el dato que obtiene la lógica servidor.

**[E] Ciclo día/noche servidor.** `0x2C3BCD0`, llamada por `UpdateFieldNightAndDay`, llama al getter en `0x2C3BE2B`, lee hora `+4`, calcula `unsigned(hora-4)>16`, escribe `field+0x2E0` en `0x2C3BE44` y, si cambia, recorre actores y llama una ruta de componente. Medianoche selecciona la rama nocturna. La consecuencia downstream completa pasa por `.xtls 0x107A8B90`; no se afirma que se haya observado un NPC concreto cambiar su rutina. [Decompilado](reloj/ghidra_2c3bcd0.c), [bytes](reloj/capstone_2C3BE20.txt).

**[E/S] Temporizadores y guardado.** Ghidra enumera 162 referencias al getter, incluidas rutinas de spawn, `CurrentScheduleDataReq` y guardado de ContentsMisc. Eso expone consumidores a una hora mala; no prueba que todos fallen. Los plazos que usan `FrameClock_NowMs` directamente no se convierten a medianoche por este cambio. Separar en la prueba la hora de calendario, las marcas monotónicas y los plazos absolutos serializados.

**[E/E-x/S] Cliente real invitado.** Recibe el `FieldTime` en su 2144. `.rsrc 0xB87B90` lo deserializa y llama a `.xtls 0x9CE7940`; el tramo legible `0x9CE7B03` llama a `.rsrc 0xA38B00` con esa hora para aplicarla. **P8 heredaría el valor incorrecto**; el `cdmp_peer` actual solo lo graba. No hay pantalla P8 de esta prueba, así que el efecto visible sigue siendo inferencia. [Handler](reloj/ghidra_b87b90.c), [cliente](reloj/ghidra_9ce7940.c), [call confirmado](reloj/capstone_9CE7AA0.txt).

**[E/S] Cliente anfitrión.** Tiene su propio global `0x6A6D218`. Cambiar el global servidor no es una escritura directa en él, ni el 2144 exclusivo del invitado debe entregársele. Puede seguir mostrando el día anterior mientras el servidor cree que es de noche. Las capturas aquí son del túnel invitado, no certifican todos los paquetes dirigidos al anfitrión. Los logs no registran el getter cliente ni su imagen del cielo. **No está confirmado que el cielo del anfitrión salte, ni que permanezca igual.**

## 5. Arreglo de raíz propuesto — diseño, no implementado

### 5.1 Invariante y enganches

**[S] Invariante:** mientras el mundo del anfitrión está activo, solo su autoridad puede iniciar/reanclar el reloj mundial. Un invitado no obtiene autoridad por poseer un campo temporal. La identidad se determina por **sesión + usuario validado + época**, no por `characterNo` (hoy repetido), nombre, ranura o índice actor. Aplicar a toda la lista de invitados, hasta tres; no fijar la clave del usuario2.

| Entrada `.rsrc` | Qué hacer exclusivamente para invitado | Qué preservar |
|---|---|---|
| **`0x2BC0A30(FM, campoDestino)`**, función completa, no thunk | Sustituir su operación de tiempo: obtener `ft=GetCurrentFieldTime(campoDestino)` desde el global mundial intacto, respetando hora fija del destino; copiar **solo** los 32 B a `TS=*FM; TS+0x450`, con el lock nativo del actor y punteros/propiedad comprobados. **No llamar al original ni a `0x21A1CC0` en este caso**, porque ambos pueden publicar global cuando el invitado posee un campo. | El anfitrión llama original. No cambiar `field+0x180`, ni tabla de tiempo del anfitrión, ni su pausa. Para el aparcamiento el componente puede mantener su hora fija, pero nunca publicarla mundialmente. |
| **`0x219D7F0(TS,res,field,actor,flag,now*)`** | Invocar el original con **quinto argumento 0** aunque el P8 auténtico mande 01; conservar la Req, su deserialización, el resto de la finalización y el Ack. Si la carga es de campo privado del invitado y la semántica original exigía despausarlo, hacerlo por separado solo tras verificar dueño/época y que no es el campo del anfitrión. | Compartido: `field+0x17C` debe conservar su estado. No convertir una Req completa en un falso éxito ni eliminar las demás tareas de carga. |
| **`0x2BC8970`**, cambios explícitos de hora | Política del servidor: una petición de dormir/cambiar hora de invitado no debe publicarse por el mero hecho de ser dueño de un campo privado. Inicialmente rechazar/registrar para invitado, o arbitrarla hacia la autoridad mundial como operación distinta del login. | No emitir 2816 de un cambio que no se aplicó. La ruta normal del anfitrión conserva efectos y difusión. |

**[E/S]** La primera sustitución replica el propósito del final nativo `0x2BC0CBD–0x2BC0CD2` pero evita el setter con efecto global; el getter en sí es de lectura. Para esta build no se observa otra tarea no temporal dentro de `0x2BC0A30`, aparte del lock, tabla de hora/normalización y asignación del componente. **La rama original también puede insertar un registro en la tabla personal `TS+0x5D0` si falta el nivel**; sustituirla omite deliberadamente esa inicialización de hora personal. No se debe ocultar ese efecto: comprobar en P9 el guardado de TransformSync y decidir si esa tabla debe guardar una copia subordinada de la hora mundial. No usar su contenido previo como autoridad. La nueva copia se hace en el hilo del servidor que ya ejecuta la carga, nunca en el worker del mod. Entradas contrastadas con capstone: [viaje](reloj/capstone_2BC0A30.txt) y [LoadingComplete](reloj/capstone_219D7F0.txt).

**[S]** El arreglo debe estar activo **antes de admitir LoginCharacterToFieldServerReq**, incluidos los viajes privados. Activarlo únicamente cuando H1 decide compartir el segundo campo llega tarde. No sirve copiar y restaurar el global después de la función o del guion: entre ambos instantes los otros hilos y usuarios pueden consumir la hora incorrecta.

**[S]** El tratamiento separado de pausa privada está condicionado a comprobar qué actor/campo se finaliza. La rama que se omite también actualiza el ancla del componente. En el campo compartido no se debe congelar el tiempo mundial durante la carga del invitado: el `ft` preparado por el getter ya tiene un ancla consistente. Esta parte requiere instrumentación de `TS+0x450/468`, `field+0x17C` y ambos ciclos en la prueba; no se presenta como validada por C1g.

### 5.2 Validaciones y fallo seguro

**[S]** Resolver solo build2976 con hash/firma y entrada exacta `.rsrc`; comparar los primeros 40 B de las funciones elegidas contra la copia analizada. Los primeros 24 B de muchos prólogos se repiten: no son una firma única. No enganchar `0x21A1CC0` ni el handler `0x29C61A0`, ambos thunks a `.xtls`.

**[S]** Antes de ejecutar el guion verificar: todos los hooks de reloj instalados como unidad; sesión1/usuario del anfitrión coherentes; invitado registrado en el mapa de sesiones; `TS+8` actor, `actor+0xA0` usuario y vuelta a sesión correctos; campo vivo y generación de `FM`/campo válidas cuando corresponda; accesibilidad de cada lectura; TLS servidor. Validar nivel y sus datos de hora fija en la rama del getter. No exigir hora<24 al valor fijo bruto del aparcamiento; validar la forma normalizada cuando sea reloj mundial.

**[S]** Si falla una validación previa, no admitir la carga del invitado y desactivar opción D, dejando la ruta anfitrión normal. Si falla durante un viaje ya admitido, no ejecutar la escritura original para ese invitado, detener su inyección y pasar al cierre nativo de P-SALIDA; registrar la incidencia. No inventar retorno de éxito ni liberar objetos a mano. Si se pierde el invariante de hora mundial, parar la prueba y evitar guardados: restaurarlo a posteriori ocultaría el escritor que falta. Esta protección no sustituye P6/el guardián.

### 5.3 P8, partida propia y cuatro jugadores

**[S]** P8 y P9 no deben importar la hora de la partida personal al reloj autoritativo. La lectura de componentes personales puede seguir; la publicación mundial de esos componentes se bloquea por usuario/época. El campo de aparcamiento puede tener hora fija solo para sí; el campo normal compartido toma el reloj mundial actual.

**[E/S]** Aprovechar el **2144 nativo** para dar al invitado la hora al entrar. El `ServerTimeInfoAck`2188 alinea los ticks remotos: su handler `.rsrc 0xBDF310` calcula `tickServidor - FrameClock_NowMs()` y escribe el desfase `0x6D69438`, ver [decompilado](reloj/ghidra_bdf310.c). Mantenerlo en P8, sin reescribir el ancla a una base incompatible. No se necesita inventar un protocolo periódico de hora para resolver este login. La nota anterior de que solo 2816 transmite hora era demasiado limitada: 2144 también la transmite. Para dormir/cambiar hora con jugadores alejados, `ToNear` de 2816 no garantiza alcance mundial; su difusión global queda como operación autoritativa explícita, no como permiso indiscriminado a cada invitado.

**[S]** Con cuatro jugadores el reloj sigue siendo uno; tres aparcamientos no deben reiniciarlo. No usar una pareja global "guardar/restaurar" por login: cargas solapadas perderían cambios. El predicado debe funcionar por sesión y época, incluso al reconectar con otro ActorKey. Viajes del anfitrión y hora fija de mazmorras exigen pruebas aparte de este login; no se certifica aquí toda la política de viajes o persistencia.

## 6. Una prueba que confirme causa y arreglo

**[S] Preparación:** una ejecución C1g equivalente, guardián y controles de las pruebas de opción D activos, anfitrión quieto en campo normal no pausado, mundo ya cargado antes de conectar peer. Mantener el paso4b de acción que resolvió C1g. Medir 5 s de referencia. El cambio a implementar debe registrar en entradas `.rsrc` `0x2BC0A30` y `0x219D7F0` y al salir: usuario/época, nivel, owner del campo, campo/generación, global servidor y cliente, `TS+0x450`, pausa, `now`, hora calculada. Sondas de solo lectura adicionales en `0x20E55A0` y `0x2C3BCD0` pueden confirmar el consumidor.

**[S]** Un mid-hook **solo de lectura** en `0x2BC0C50` y `0x219D8EC` puede registrar intentos de publicación nativa; no modificar estos puntos ni usarlos como inline hook. El cambio de comportamiento se instala únicamente en las entradas anteriores. Para el escritor `.xtls`, observar sus llamadas en `.rsrc` `0x2BC0CD2`, `0x2BC8A21` y `0xA390C1`; nunca enganchar el cuerpo virtualizado.

**[S] Secuencia en la misma prueba:**

1. Ejecutar el guion C1g, dejando ambos ciclos; probar `LoadingCompleteReq #2=01` de forma deliberada y documentada para cubrir P8, mientras el servidor la transforma semánticamente a la rama no publicadora. El resto del guion sigue igual. La primera puede seguir `00` para separar variables; la política servidor debe aplicarse a ambas.
2. En la primera llamada de viaje invitado debe verse una línea propuesta `ClockGate guest=<key> level=3 owner=guest policy=component-only global_unchanged=1`.
3. En el segundo viaje: `ClockGate guest=<key> level=1 owner=host policy=component-only scale=12 global_unchanged=1`.
4. Finalización: `ClockGate LoadingComplete guest flag_in=1 flag_effective=0 host_pause_unchanged=1`. Debe llegar 2762; no se pierde la finalización real.
5. Grabar 60 s. El segundo2144 debe llevar día/hora cercanos al reloj calculado del anfitrión, **escala12**, ancla del servidor, y avanzar unos 12 s de juego por segundo real de día (24 de noche). La diferencia permitida debe depender del RTT/tick; una tolerancia inicial de 2 s de juego sirve con loopback, no se debe copiar sin más a Shadow.

**[S] Éxito:** no aparece ninguna publicación del global atribuida al invitado; la base/escala mundial siguen intactas y el getter continúa avanzando; pausa y `field+0x2E0` del anfitrión no cambian por el login; 2144 del invitado contiene la hora autoritativa del campo normal; guion completo, C1g estable y sin guardado del anfitrión. Registrar hora cliente/servidor y un punto de referencia visual del anfitrión; así se cierra la laguna visual en la misma sesión.

**[S] Parada:** primera publicación inesperada, escala mundial0 en un campo normal, salto de día/hora fuera del avance esperado, modificación de pausa anfitrión, error de cabecera/propiedad, login sin Ack o persistencia no autorizada. No seguir hacia combate ni guardar para "ver si se arregla". Conservar la evidencia y deshabilitar la opción D para nuevos invitados; la limpieza de la sesión admitida depende de P-SALIDA.

**[S] Extensión P8/4 jugadores:** después de esta prueba discriminante, repetir en Shadow con partida de otra hora y los dos valores de LoadingComplete reales; comparar ambos relojes y2144. Finalmente tres invitados entrando de forma solapada y reconectando. Una prueba peer demuestra el servidor y los bytes, no demuestra el cielo del cliente remoto real.

## 7. Riesgos y lagunas cerradas / pendientes

- **[E/V] Cerrado:** formato, escala0, global compartido, rama de dueño de aparcamiento, equivalencia día0/24→día1/00, 2144 corrupto antes de LoadingComplete#2, y B3 como contraste.
- **[S] Pendiente de traza:** instante exacto/stack de la primera escritura en el juego; el diagnóstico identifica la cadena causal pero no sustituye un hook de observación.
- **[S] Pendiente:** ausencia de escritores indirectos desconocidos; política completa de dormir, viajes del anfitrión, niveles fijos y pausas privadas en P8; tipos precisos de todos los plazos de guardado.
- **[V/S] Pendiente:** salto visual del anfitrión y del cliente Shadow; los datos disponibles no tienen dos relojes cliente/servidor muestreados durante el evento.
- **[S] No recomendado:** restaurar snapshot al acabar el login, forzar solo `LoadingComplete=00`, elevar todos los dueños de campos a dueños del mundo, cambiar owner de un campo para engañar el setter o enganchar `.xtls`.

## Revisión

Revisor: agente P-SALIDA, independiente del autor de este informe, 9 de octubre de 2026. Consultó de nuevo Ghidra para `0x2BC0A30`, `0x219D7F0`, `0x20E55A0`, `0x20D3360` y `0x20D3460`; desensambló desde entradas reales con capstone sobre la copia autorizada y releyó los cuatro CDPK crudos mediante un script distinto. Evidencia reproducible: [revision-salida.txt](reloj/revision-salida.txt). No ejecutó el juego ni probó el arreglo.

1. **Intento de refutación: «la segunda LoadingComplete pone el reloj a cero». [E/V] Rechazado para estas capturas.** Se confirmó independientemente que las dos 2445 llevan `00` y que el segundo 2144 ya es día1/medianoche/escala0 antes de la segunda Req: C1g #7375 a 2.131244 s frente a #10013 a 7.136333 s; la misma desigualdad se reproduce en C1e/f. La entrada `0x219D7F0`, instrucciones `0x219D82B/82F`, omite con ese flag el bloque de hora y pausa. `0x2BC0A30` compara dueño, normaliza la hora fija y escribe el global; la comparación e instrucciones son reales, no solo comentarios de Ghidra. **[S] Se mantiene la atribución al primer viaje como inferencia fuerte**, no como una instrucción capturada en ejecución. B3 vuelve a hora normal en el segundo viaje propio, compatible con una sustitución temporal previa. El getter del nivel fijo y el setter final añaden matices de orden/estado de campo: la traza propuesta debe registrar ambas publicaciones potenciales, no solo una.

2. **Intento de refutación: «solo cambia una instantánea inerte; el reloj útil no depende de ella». [E/V] Rechazado.** Se confirmó en `0x20E55A0` la lectura del global elegido por TLS, la multiplicación del delta por el `u16 +0x16` y el retorno de una copia; escala0 impide avance en un campo normal no pausado. El formato de red se redecodificó sin copiar los resultados del autor: primeros2144 `(0,24,0,0,0,0,0,0)`, segundos C1e/f/g `(1,0,0,0,0,1,0,ancla)`, B3 `(6,13,30,27,744,6,12,109282)`. Los ticks de 2188 siguen aumentando, por lo que «todos los temporizadores se detienen» también sería falso. **[S] No se eleva a confirmado el cielo del anfitrión**, cuya copia cliente es distinta y no está registrada.

3. **Intento de refutación: «forzar flag0 salta toda la carga o basta por sí solo para arreglar el reloj». [E] Ambas lecturas son incorrectas.** La rama flag0 converge en `0x219D941`, que todavía marca el actor, ejecuta otras tareas de carga y termina con resultado0; no es un retorno temprano de toda la función. Pero el escritor del primer viaje sigue existiendo, así que flag0 por sí solo no resuelve C1g. **[S] Se acepta el diseño combinado como propuesta razonada, no validada**: intervención desde el primer viaje, copia de componente sin setter publicador, tratamiento separado de pausa privada y conservación del resto de LoadingComplete. Quedan obligatorias la traza de `TS+0x450/468`, propietario/campo, instante de escritura, y la prueba de que el campo privado termina su carga. Omitir la inicialización de tabla personal y no reanclar el componente tienen efectos ya explicitados; tampoco se ha demostrado ausencia absoluta de escritores indirectos.

**Resultado:** las tres conclusiones principales resisten esta revisión con su nivel de confianza original. No se encontró un dato que obligue a cambiar la causa propuesta. Se refuerza la distinción entre mecanismo probado, orden inferido por datos y arreglo pendiente de ejecución. La prueba de cuatro jugadores/P8 continúa siendo una ampliación necesaria, no un resultado de los cuatro CDPK de `cdmp_peer`.
