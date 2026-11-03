# Revisión independiente S42 — terminada para las fuentes observacionales

Revisor independiente; lectura de fuentes, sin abrir el juego, desplegar, compilar ni tocar partidas/bin64. Base: effae477f74302fe2759ce5be272c37387b2169b. Fuentes finales revisadas: **c7e6239**. Dictamen: aprobadas para observación; la reparación funcional sigue pendiente. Ensamblado final y SHA-256 del binario cotejados independientemente; detalle de identificación al final.

## Verificaciones completadas

- [V] `EnemyDamageProbe` instala sobre la entrada `.pdata` RVA **0x209CB30**, final 0x209CD2C, unwind 0x640F8A4. Lectura independiente del PE 2976: sección `.rsrc`, firma inicial de 40 B con una sola coincidencia en esa sección. Bytes iniciales: `48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20`. Capstone verifica tres instrucciones completas de 5 B. `prepare_inline` acepta parche de 5 o 15 B y comprueba exactamente sus bytes.
- [V] El decompilado `D:/RE/CrimsonDesert_2976/exports/decomp/0x209cb30.c` confirma cinco argumentos y retorno bool; el detour conserva los cinco y devuelve el resultado original sin modificación. Las lecturas de actor/contexto pasan por `game::fast_read`.
- [V] `enemy_damage_probe=0` retorna antes de instalar el gancho. La instrumentación `probe/1` observa la inmunidad: **no prueba daño aplicado**, ni modifica facción, HP o muerte. El registro incluye validez por campo y vtable para evitar asumir que un cero ilegible es un controlador conocido.

## Estado de cierre

- Revisión del diff y del cableado de las tres opciones: terminada sobre c7e6239.
- Familias: comprobada la separación real/sombra, reservas y registros previos; retiradas todas las ramas activas nuevas. La transferencia de vehículos no se aprueba ni se implementa como activa.
- Puertas e inmunidad: entradas, firma, ABI, lecturas protegidas, taps y ausencia de mutación revisados.
- Tests, NDEBUG, documento e INI: comprobados; detalle al final.
- Build final 78/78, objetos, ensamblado e identificación del ASI: cotejados. Sin pendientes para la entrega observacional.

## Hallazgos comunicados durante la revisión

1. **P1 — rechazo incompleto de la preparación del hook de puertas.** `door_trace_install` comprobaba únicamente `!h`; `game::prepare_inline` puede devolver un hook presente cuyos bytes no acepta el predicado al agotarse los reintentos. Exigido volver a aplicar `accept(h->original_bytes())` antes de habilitar. Corrección confirmada en la revisión final de c7e6239.
2. **P1 — probe de familias podía modificar una decisión real.** En `family_child`, la nueva lectura del vector para `vehicles` sobrescribía `cls` de un hijo reservado incluso con `npc_family_vehicles=probe`. Si esa nueva lectura fallaba, `m.cls` dejaba de ser la clase demostrada por el preexamen; la llamada efectiva a `member_verdict` podía partir una unidad que antes se asignaba. Exigida separación de entrada baseline y clasificación sombra, con prueba de regresión de reservado válido + nueva lectura fallida.
3. **P2 — campos nativos de puertas ilegibles impresos como cero conocido.** Se ignoraban retornos de `fast_read` de clave, controlador y dueño de campo. Exigida máscara de validez en el evento/log, sin alterar la petición.
4. **Riesgo de ruta mid — inicialización CRT de 1024 B.** `door_trace_ingress`, invocada por `on_server_recv` (mid), inicializaba todo el buffer de 1024 B antes de `fast_read`. Se solicita no inicializar bytes que la lectura protegida va a rellenar, y comprobar ensamblado compilado frente a `memset`/CRT que pueda clobber YMM superiores.

## Verificación primaria de puertas

[V] Lectura independiente del PE: `0x282DC80..0x282E694` coincide con `.pdata`, unwind `0x6503FD8`. Firma inicial de 47 B única en `.rsrc`. Instrucción inicial de 5 B: `48 89 5C 24 08` (`mov [rsp+8],rbx`). El decompilado `0x282dc80.c` confirma cinco parámetros (gestor, salida error, actor, recurso u32, lista de escenas) y retorno del puntero de error; usar `uintptr_t` mantiene ABI x64 sin desreferenciar ese puntero fuera de lecturas protegidas. La sonda llama una vez y conserva el retorno; no abre puertas ni suplanta a otro usuario.

## Cierre de revisión de fuentes (alcance observacional)

**Aprobada con los cambios de revisión incorporados para medir, no para anunciar que los fallos están reparados.** No queda un hallazgo P1/P2 abierto en el diff observacional leído. El primer objeto, el ensamblado/binario final y ambos resultados de tests están comprobados. No se extiende esta aprobación a una reparación funcional.

- El P1 de `prepare_inline` está corregido con doble validación de `original_bytes` y test de longitudes/bytes no permitidos (`doortrace::verified_patch`). El hook que falla al habilitar no se informa como instalado.
- El P1 de `probe` de familias está corregido mediante `actual_class`, usado por producción: reservado conserva la clase del preexamen; sólo el tardío de unidad viva usaba y sigue usando la lectura actual. La regresión de lectura sombra fallida se prueba para los tres valores solicitados.
- `npc_family_vehicles=1` **se degrada explícitamente a probe** en `effective_mode`, usado por el setter. Se retiraron las ramas activas nuevas: no hay cambio de controlador, reserva prospectiva, retirada ni barrera parcial nueva. Las decisiones `member_verdict/late_verdict`, los registros efectivos y las reservas de B conservan su ruta previa. El censo advierte `capacidad_sombra_no_reservada=1`: clasificar todos los miembros no demuestra que una asignación completa sea admisible.
- Campos privados `type/projected` del marco se inicializan en cada alta y se reinicializan al reutilizarlo; no se transmiten punteros ni se alteran mensajes. La observación usa el gancho existente 0x2792A7B, sin instalar otro encima.
- El P2 de lectura de puertas está corregido con máscara de validez y estados no medidos. Se añadió otro P2 de integración: `rope_probe=1` instala el fan-out también en el invitado; sin filtro, sus mensajes se etiquetaban como anfitrión. Corregido con filtro por rol `observe_tap`, con tests.
- El buffer de `door_trace_ingress` ya no se inicializa antes de copiarlo bajo SEH. Las rutas mid (`door_trace_ingress`, `door_trace_packet`, `family_child`) del primer objeto se comprobaron sin nuevas llamadas CRT que afecten YMM; el cotejo final consta al cierre del informe.
- `DoorTrace` observa los mensajes ya aceptados en colas y entregados por el cliente, más el resultado nativo en la entrada 0x282DC80. Ninguna de esas observaciones prueba apertura física de la puerta. El parser no debe inventar semántica del u32 de 2928 no demostrada por el nativo.
- `EnemyDamageProbe` conserva el retorno de inmunidad. Su valor false no prueba daño; los bits de lectura y la vtable se necesitan para interpretar cada muestra.

### Límite que impide aprobar asignación de vehículos en esta entrega

`on_logout -> logout_drain` es una observación previa al Logout original; `logout_drain` no detiene el nativo. La ruta de entrada de 2001/2283 también continúa después de solicitar un drenaje del mod. Por ello, comprobar asientos y bloquear solamente `guarded_load/guarded_unload` **no demuestra** que una stage o un vehículo montado no se destruya por otras rutas nativas. Tampoco una lectura «libre» anterior a liberar un Scope autoriza una destrucción posterior sin exclusión de cambios de montura. Esta revisión solicitó contrastar esa frontera; se decidió conservar sólo medida. No se ha probado un cierre inevitable, ni se justifica afirmar seguridad de una activación aún no implementada.

### Validación mínima antes de un futuro modo activo

Demostrar ciclo de vida de stage/familia en viaje rápido, 2001, Logout y llegada del anfitrión; exclusión real entre validar asientos y Logout; orden hoja-raíz con publicaciones retenidas; ausencia del actor en SAM antes de vaciar el gestor; carreras con temporizadores; registros/reservas completos por nacimiento y época. Un `cdmp_peer` sin IA no valida esas condiciones en cliente. Antes de eso, la prueba actual sólo debe confirmar diagnóstico y ausencia de regresión frente a la base; la primera partición/fallback conocida seguirá siendo esperable.

## Comprobación del primer objeto compilado (15-10-2026)

[V] Lectura independiente de `build/research/s42/compilacion/door-trace-asm.txt`: `door_trace_ingress` (líneas 21914–21979, reserva de pila 0x440), `door_trace_packet` (22135–22256, 0x80), `parse` (22635–22746) y `push` (23089–23145) no contienen llamadas a `memcpy/memset/memcmp/__chkstk` ni instrucciones YMM/vzeroupper. La comparación de UUID se materializa en comparaciones qword; la copia del evento usa SSE legacy. La función `fast_read` emplea lecturas volatile escalares o bucle volatile para tamaños arbitrarios (`src/mod/game/safe_memory.cpp:93`), no el `guarded_copy` que usa memcpy.

[V] `user2-stages-asm.txt`: revisados `family_child` (100879–102451, pila 0x200), `fam_close` (99590–100134), `npc_control_impl` (115865–117011), `in_vector` (54607–54768), `read_char_link` (68049–68160) y el bloque de `road_ride`. Sin CRT de copias/ceros/comparaciones, `__chkstk` o instrucciones YMM/vzeroupper en esos bloques. Esto verifica los objetos examinados; se debe mantener o reconfirmar con los objetos del build final si se recompila una fuente distinta.

[V] `build/research/s42/compilacion/inicial.log` termina con **78/78 tests pasados**, incluidos los tres nuevos y `optd_npc_family`/`optd_user2_road`. No los ejecutó este revisor: se comprobó el resultado de la compilación del integrador. `git diff --check -- CMakeLists.txt src tests` finalizó correctamente durante esta revisión.

**Dictamen final de este pase:** fuentes y primer build aprobados para las sondas descritas, con 1 de familias degradado a probe. Ningún hallazgo P1/P2 pendiente en ese alcance. La solución funcional completa y su activación continúan sin demostrarse; este dictamen no las aprueba.


## Tests y configuración de compilación (fuentes c7e6239)

[V] Se leyeron los proyectos MSBuild `build/cdmp_tests_optd_enemy_damage_probe.vcxproj`, `cdmp_tests_optd_npc_family_vehicles.vcxproj` y `cdmp_tests_optd_door_trace.vcxproj`: **RelWithDebInfo define NDEBUG**, Optimization=MaxSpeed y RuntimeLibrary=MultiThreaded. `CMakeLists.txt:22` sólo añade /W4 mediante `cdmp_strict`; no revoca NDEBUG. Esto no vacía estas pruebas: familias y puertas usan una macro CHECK con `if`, contador de fallos y salida 1; inmunidad usa una lambda check con contador y salida 1. Esos mecanismos no dependen de assert ni NDEBUG. La búsqueda `rg -n '\bassert\s*\(' tests` no encuentra llamadas assert; tampoco hay inclusiones de cassert/assert.h en tests.

[V] `CMakeLists.txt:764–775` registra los tres ejecutables reales. Sus pruebas llaman los helpers incluidos desde los mismos headers que usa producción: `actual_class/effective_mode/decide` de familias, `sample/mode` de inmunidad y `parse/frame/verified_patch/observe_tap` de puertas. Las regresiones de lectura sombra ilegible, modo 1 degradado, longitudes/bytes del hook, roles y buffers truncados son comprobaciones ejecutables, no comentarios o asserts eliminados.

**Límite de cobertura:** los tests puros no ejecutan los detours, la instalación real de hooks ni el juego. La integración se ha revisado leyendo los cuerpos y su cableado; la seguridad frente a clobber CRT se comprobó además en los bloques ensamblados descritos. Ninguna de esas medidas prueba IA, daño aplicado, apertura física o drenaje nativo de vehículos. No corresponde interpretar 78/78 como validación en vivo ni como aprobación del arreglo funcional.

[V] `build/research/s42/compilacion/final.log` termina con **78/78, 0 fallos, Build OK**, incluidos los tres nuevos y los tests existentes de familias/carretera. El revisor comprobó el log del integrador; no recompiló ni ejecutó el juego. El cotejo del objeto final consta al cierre del informe.

## Documento y cuatro INI finales

[V] Revisado `docs/research/s25/s42-enemigos-puertas.md`: el estado inicial, secciones 3 y 5 y cierre declaran reparación pendiente, familias 1 degradado a probe y las otras dos opciones como detalle observacional. Distingue 2005 de render, 2922 de daño aplicado y Ack/Nak de apertura física. La partición/fallback conocidos seguirán ocurriendo; no los presenta como reparados.

[V] Comparación independiente de claves por sección: los cuatro `cdmp.2inst-s42-{probe,activo}-{host,guest}.ini` conservan todas las opciones de sus bases (`cdmp.2inst-host-carros-1.ini` y `cdmp.2inst-guest.ini`). Sólo añaden, una vez cada una en [session2], las tres claves S42. Host probe=probe/probe/probe; host activo=1/1/1; invitado probe=0/0/probe; invitado activo=0/0/1. Sus primeras líneas explican que «activo» significa diagnóstico ampliado y no reparación. Las opciones heredadas activas no han sido revalidadas globalmente por esta auditoría.

**Estado de este cierre:** no hay P1/P2 abiertos en las fuentes observacionales c7e6239 ni promesa indebida en documento/INI. La revisión no autoriza asignación de vehículos ni un arreglo de daño/minimapa/puertas. La identificación de la compilación final revisada consta a continuación.


## Cierre del build final e identificación del artefacto

[V] Fuentes finales: **c7e62393537fc0d1771692daab2942cb116fe852**, sin diferencias posteriores de código frente a ese commit al comprobarlo. `final.log`: **78/78, cero fallos, 35,07 s, Build OK**. Lectura independiente y SHA-256 calculado por el revisor sobre los archivos presentes:

| Archivo | SHA-256 |
|---|---|
| `build/cdmp_mod.dir/RelWithDebInfo/door_trace.obj` | `512c7afdd35f51b33e5e7a011160e0a7c1b88ce8c3617310daf96f1bd43801f2` |
| `build/cdmp_mod.dir/RelWithDebInfo/user2_stages.obj` | `25ac435b39921c3000014d4246bf4fa2f83638bb66d778878458f50a3280dabf` |
| `build/bin/RelWithDebInfo/cdmp.asi` | `0b373a026b7630335c0a818c5dc31e13babec8cdb0cd0f1f1a7d9a858a8f0f5c` |

[V] Reexaminados `door-trace-final-asm.txt` y `user2-stages-final-asm.txt` bajo `build/research/s42/compilacion/`. Frente a sus volcados iniciales, cada uno sólo cambia una línea del tamaño de `.debug$S`; las instrucciones de los bloques examinados permanecen iguales. Comprobados otra vez `door_trace_ingress` (21914–21979), `door_trace_packet` (22135–22256), `parse` (22635–22746), `push` (23089–23145), `family_child` (100879–102451), `fam_close` (99590–100134), `npc_control_impl` (115865–117011), `road_ride` (141053–141242), las tres instancias `in_vector` (54607–54768, 54769–54930, 54931–55100) y `read_char_link` (68049–68160). Sin llamadas `memcpy/memset/memcmp/__chkstk` ni instrucciones YMM/vzeroupper en esos bloques. Se conserva el contrato de `fast_read` volatile descrito antes; no se equipara la búsqueda de nombres sobre todo el objeto con una prueba de la ruta ejecutada.

**Dictamen independiente terminado:** aprobada la entrega observacional de c7e6239 y el ASI identificado arriba, con `npc_family_vehicles=1` degradado explícitamente a probe. No hay P1/P2 pendientes en este alcance. El dictamen no prueba funcionamiento dentro del juego ni aprueba una transferencia activa de vehículos o una reparación de inmunidad, minimapa o puertas. No se abrió el juego, no se desplegó, no se tocaron partidas/bin64 ni se cambiaron fuentes por parte del revisor.
