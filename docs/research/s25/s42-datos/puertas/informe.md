# S42 — puertas intermitentes: evidencia, límite causal y sonda

15-10-2026. Worktree `C:\Emulador CD-enemigos`, base `effae47`. Build 1.0.0.2976, PE `0x6AB28F00`, base `0x140000000`. [V] captura/log primario; [E] lectura estática; [H] hipótesis; [NV] dato no disponible. No se abrió el juego ni se desplegó ni se tocaron partidas.

## Dictamen

**No está demostrada aún una causa raíz que permita arreglar la puerta con seguridad.** Sí se ha aislado una diferencia primaria entre éxito y fallo: una escena bajo demanda del lugar de la puerta se crea en el ensayo positivo y recibe rechazos nativos reiterados en S38. El punto que falta medir es el código de error de `0x282DC80`; el 2953 enviado al cliente no lo incluye. No se fuerza la creación ni se cambia su controlador o progreso.

[V] El candidato es recurso `DE7B52AA`, UUID en bytes `4fb9eeab670f74563c343d640c000000`, posición `(-10668.78515625,617.0676879882812,-3838.27294921875)`. [H] Identificarlo visualmente con **la hoja de la puerta de la taberna** requiere una prueba marcada con la misma cámara/posición: ni el nombre genérico «Game Mechanic» ni una marca Ctrl+F12 aislada lo demuestran. La familia positiva y sus mensajes de constraint apoyan esa identificación; no se presenta como certeza.

[E] La primera divergencia observada ocurre antes de disponer de esta familia en S38, en la ruta `SceneLevelOnDemandGimmickSpawnReq 2928` → `0x29C1C80` → `0x282DC80` → `SpawnOnDemandGimmickNak 2953`. Esto hace insuficiente atribuirla sólo a un bloqueo del botón de interacción, al controlador de una puerta ya creada, a `child_hold`, o a `gimmick_event_dedup`.

## Fuentes y reproducción

Todas las rutas de capturas siguientes dependen de `C:\Emulador CD\backups\`:

| Id | Captura v2 | Log |
|---|---|---|
| poseH | `p8-2inst-pose-anfitrion/cdmp_packets.host_20261015-083504.bin` | `cdmp.host_20261015-083454.log` |
| poseG | `p8-2inst-pose-anfitrion/cdmp_packets.guest_20261015-083521.bin` | `cdmp.guest_20261015-083511.log` |
| s38H | `p8-sesion38/cdmp_packets_20261015-125539.bin` | `cdmp_20261015-125527.log` |
| s38G | `p8-sesion38/shadow/cdmp_packets_20261015-115854.bin` | `shadow/cdmp_20261015-115835.log` |

`p8-2inst-prueba1` contiene logs del 14-10 23:16, pero no `.bin`; [NV] no permite repetir allí un recuento de paquetes ni identificar inequívocamente el objeto fallido.

Los scripts propios están junto a este informe:

```powershell
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B build\research\s42\puertas\parse_capturas.py
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B build\research\s42\puertas\resumir_candidata.py
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B build\research\s42\puertas\verifica_hook.py
```

`parse_capturas.py` es un lector propio a partir de `src/common/capture_file.h`: header12, registro24 y trama5. `resumir_candidata.py` vuelve directamente a los `.bin` y reconoce UUID en sus campos correspondientes, no buscando una subcadena arbitraria en cualquier mensaje. Su salida `evidencia-resumen.json` contiene cada offset, payload, alta geométrica y actualización de la familia; es la referencia para las cifras de este informe. `resumen-capturas.json` permite localizar archivos y bases QPC; los JSON grandes de paquetes son datos auxiliares.

Los `t` de las tablas son **segundos desde el primer registro de cada archivo**, no tiempos ya alineados entre PC. Bases QPC en microsegundos: poseH `117869391168`, poseG `117887050709`, s38H `133504703959`, s38G `128621218`. En las dos instancias locales se puede comparar `base+t*1e6`; en Shadow no se deben restar QPC de máquinas distintas. Para S38 se usan las anclas de log `capture t=` más el desfase de reloj aproximado de una hora.

[V] Pérdidas declaradas por sniffer: poseH21, poseG0, s38H30, s38G0. No hay registros truncados. El lector estricto descarta respectivamente 2/2/5/5 tramas cuya longitud de cabecera no coincide; no se presume captura perfecta. Las ausencias se refieren a los registros válidos disponibles.

## Resultado primario por escena

| Captura | Req2928 | Ack2145 | Nak2953 | Sync2665 | Registros2005 tipo7 con la geometría exacta |
|---|---:|---:|---:|---:|---:|
| poseH | 1 | 1 | 0 | 4 | 2 |
| poseG | 144 | 0 | 144 | 3 | 2 |
| s38H | 39 | 0 | 39 | 2 | 0 |
| s38G | 45 | 0 | 45 | 2 | 0 |

**[V] Un Nak2953 por sí solo NO demuestra que falte la puerta.** poseG recibe los dos actores por snapshot y después recibe 144 Naks para esa misma escena. [E] «Ya existe/no queda candidato» es una explicación nativa compatible en ese caso; [NV] su código concreto tampoco fue capturado.

### Ensayo positivo

| Acontecimiento poseH | t | Offset decimal del registro |
|---|---:|---:|
| Sync2665 inicial, UUID+posición | 67.084465 | 20725781 |
| Req2928, recurso DE7B52AA | 70.071123 | 26130245 |
| Sync2665 con un byte de estado cambiado | 70.171375 | 26881139 |
| 2005 padre B0100393, tipo7, controlador90100000 | 70.171550 | 26882427 |
| 2005 hijo B0100394, tipo7, controlador90100000 | 70.171781 | 26889899 |
| Ack2145, sujeto A0100001, estado1 | 70.171790 | 26896307 |

Los registros internos2005 empiezan en offsets26882458 y26889930. poseH log4355–4357 muestra las altas y parentesco; el padre aparece además en la cola del registro del hijo. poseG recibe ambos en `t121.018676`, registro5804727, offsets internos5817576/5823735. La longitud del hijo cambia de6377 a6599 en snapshot; no se presupone estructura de tamaño fijo. El controlador se lee del registro2005+0x12.

[V] En poseH hay 3089 del hijo B0100394 hacia el padre B0100393. Cerca de la marca positiva: `t118.893727`, offset`0x37E99B8`; `t119.474611`, offset`0x387D985`; `t121.226596`, offset`0x394C9CA`. La marca del usuario es log14818, **08:37:19.932**, aproximadamente t119.963. [E] La actividad de constraint concuerda con un objeto físico activo; [NV] un 3089 no prueba por sí mismo el ángulo abierto ni que el jugador cruzó la puerta.

La primera petición posterior de poseG es t139.990534/offset22548565, y su primer Nak t140.025323/22951390. Ya tenía la familia antes de pedirla.

### S38 fallida

| Captura | Primera Req2928 | Primer Nak2953 | Última Req2928 | Último Nak2953 |
|---|---|---|---|---|
| s38H | t410.582565 /113339674 | t410.588392 /113361297 | t609.616687 /183234675 | t609.631561 /183235413 |
| s38G | t162.721433 /61411798 | t162.784048 /61428313 | t403.862640 /152292659 | t403.916830 /152338023 |

[V] El cliente ya reintenta nativamente, en el orden de segundos, durante unos199s host y241s Shadow. No hay evidencia de que añadir más reintentos arregle la condición que rechaza.

[V] El Sync2665 candidato existe en S38: H t285.986633/47449351 y332.386815/74468917; G t78.890814/11434353 y125.294549/40561651. Ambos contienen la posición exacta y la misma ventana de bytes payload24..39 que el Sync positivo previo al alta: `010000000002000000000000ffffffff`. En el positivo a t70.171375 esa ventana pasa a `010000000002010000000000ffffffff`. **No se asigna un nombre semántico a ese byte ni se propone escribirlo** sin mapear el serializador al miembro en memoria.

[V] No aparece ningún2005 tipo7 con la secuencia de12B de posición exacta del positivo en S38. Esto es una ausencia medida con alcance explícito: no demuestra que cualquier puerta a otra coordenada o representación sea inexistente.

Las marcas S38 son host log39616 **13:03:28.610** y40530 **13:03:36.269**, Shadow13710 **12:02:18.807** y17624 **12:03:03.298**. No se asigna una marca a puerta o bandido sin el vídeo/testimonio correspondiente. Sí están los personajes cerca: host log40640,13:03:37.483 `(-10669.29,617.03,-3839.85)`; posición del invitado vista por host log30073,13:02:17.449 `(-10668.69,617.05,-3839.03)`.

### Interacciones que no son puertas

[V] poseH2671 t86.456144/44420562 apunta a B0100314, **Beggar**; 2720 t86.478502/44424340 y2916 t86.492464/44429592. S38G2671 t147.730496/52145216 apunta a B01006EB **WanderingFreesword**, con3000 t147.801223/52159946. S38G2671 t355.969841/138570132 apunta a B0100BEF **Beggar**, con2720 t356.035120/138587864 y2916 t356.084943/138600507. Fuente: `*-packets.json` y `*-actors.json`. No usar estos Ack de interacción genérica como éxito/fallo de la puerta.

## Ruta estática y qué falta para cerrar la causa

Exports Ghidra en `D:\RE\CrimsonDesert_2976\exports\decomp\`; copias relevantes en `decomp/` junto a este informe. Cada función se ha consultado sobre la build indicada.

- [E] `0x29C1C80` (handler2928) deserializa el prefijo, un u32 no-cero, recurso y UUID16B; deriva cantidad de UUID de la longitud. Resuelve el campo del sujeto y llama a `0x282DC80([campo+0x80],out_error,sujeto,recurso,span_UUID)`. No se interpreta el u32 inicial como cuenta autoritativa: el nativo no exige igualdad con la longitud.
- [E] `0x282DC80` devuelve el puntero al error y escribe el código en `out_error`. Si no es0, el handler emite2953 mediante `0x29A2F70`; ese escritor serializa fragmento, sujeto y UUID, **no el código**. Ver handler líneas175–192 y escritor2953.
- [E] La resolución del recurso llama un thunk hacia `.xtls 0xE2D1610`; se considera opaca y no se propone engancharla. La clasificación posterior en `.rsrc` sí es legible.

| Rama observada estáticamente en0x282DC80 | Dato disponible sin vivo | Qué aún no permite concluir |
|---|---|---|
| Error global6CF7018: recurso no resuelto o sin nombre; líneas118–122 | S38 sí recibió Sync2665 del UUID y geometría | El Sync no contiene el resultado del resolver para DE7B52AA en el instante de cada Req; no descarta una diferencia de carga o recurso |
| Error6CF701C: mapa de recurso+0x80 sin entrada preparada, o entrada+0x40=0; líneas175–180/397 | Sync existe antes de la petición, y reintentos llevan minutos | No prueba que el estado del mapa/entrada siga listo; carga local del cliente anfitrión y preparación del servidor no son equivalentes |
| Error6CF7024: no hay candidatos; líneas184–236 | Sync S38 mantiene ventana previa; poseG muestra Naks pese a actores presentes | No distingue UUID no resuelto, objeto ya activo/flag+0x183, estado cambiado por otra sesión, o ausencia real. No convertir Nak en «ya existe» universal |
| Error6CF7028:0x28308E0 devolviófalse; líneas348–360 | S38 no mostró alta geométrica; el positivo sí | No distingue un fallo de constructor/alta de tabla ni permite reintentar con parámetros inventados |
| Otros códigos | Hay ruta de error de adjuntar campo y propagación de0x2AF4430 (líneas107–113/280–287) | Las cuatro constantes anteriores no son exhaustivas; la sonda conserva el valor bruto y categoría«otro» |

[E] `0x28308E0` consulta tablas/UUID del grupo y prepara creación con `0x2831070` y vf+0x88 del campo. No se encontró una ruta demostrada de recuperación que pueda llamarse sin conocer el error. La existencia de la escena sincronizada hace menos fuerte «faltan todos los datos», pero no prueba qué condición concreta falló.

[NV] El progreso del anfitrión, otra sesión o una carga anterior pueden alterar esa preparación; las capturas no contienen un snapshot completo de los mapas/flags en el momento del rechazo. No se cambian misiones, flags ni guardado para conseguir una apertura.

## Módulos sospechosos: alcance real

- [V] S38 tiene **cero2779** en ambas capturas (`resumen-capturas.json`). [E] `gimmick_event_dedup` sólo trata2779; no hay evidencia de que suprima estas2928 o cause directamente este rechazo.
- [E] `docked_gimmicks` filtra peticiones de claves etiquetadas como acoples locales. Esta Req2928 contiene recurso+UUID y no es ese filtro por actor. Cambiar su configuración no constituye prueba causal.
- [E] `child_hold`/`observer_hold`/publication gate son relevantes después del2005. En poseG la familia2005 está capturada; eso no prueba que ningún filtro posterior la retuviera. En S38 la ruta candidata falla antes del alta observada, por lo que forzar publicación no resuelve el dato primario.
- [E] En el nativo, tipo7 puede permanecer con controlador host. El positivo funcionó así (`90100000` en ambos2005), luego «dar todo tipo7 al invitado» no está justificado como arreglo de esta puerta.
- [E] El control sí importa a otras operaciones: `0x8E59D0` compara usuario local/controlador; `0x2AC1270` descarta eventos3082 de otro controlador. Pero `0x29E1E00`→`0x2A987C0` procesa3089 sin esa misma comprobación explícita en el handler. No generalizar la regla de3082 a toda interacción física.
- [NV] No hay correlación demostrada entre vaciado U-1 y el rechazo de esta escena concreta. La familia candidata no se etiqueta automáticamente como NPC de stage por compartir campo/tipo.
- [E] `docs/research/s25/gimmicks-dueno.md` documentó ping-pong2221/2928 en otra sesión. Aquí no se puede importar esa causa: hay rechazo repetido sin un alta/baja candidata observada en S38. No se añadió filtro2221 ni recreación automática.

## Implementación acordada: medir, sin fingir un arreglo

`door_trace = 0 | probe | 1`, apagado por defecto. **probe y1 son observación;1 NO abre puertas ni cambia el juego.**

- `0`: inerte, sin instalar el nuevo gancho.
- `probe`: contadores por punto de la ruta y resultado nativo de la candidata.
- `1`: lo anterior más detalles de paquetes acotados (máximo2000 líneas de eventos, contadores posteriores conservados).

Archivos propios: `src/mod/session2/door_trace.{h,cpp}`, `door_trace_logic.h`, `tests/optd_door_trace_tests.cpp`. Integración de config/CMake/taps realizada por el coordinador. La sonda distingue HostClientReq/Ack, GuestSend, GuestDeliver, ServerHostReq, ServerGuestReq y ServerGuestOut; el sniffer local invitado no se etiqueta erróneamente como host. Los taps de entrega/transmisión deben estar después de retenciones y envío aceptado, no antes.

El único hook nuevo es **entrada0x282DC80**, `.pdata [0x282DC80,0x282E694)`, `.rsrc`. Parche verificado de5B `48 89 5C 24 08` (`mov [rsp+8],rbx`). Firma47B única en el exe:

```
48 89 5C 24 08 4C 89 44 24 18 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 E0 F7 FF FF 48 81 EC 20 09 00 00 45 8B F1 4D 8B E8
```

`verifica_hook.py`/`hook-verificado.json` verifican sección, bytes, límites y unicidad. Se rechaza instalar si cambia build/base/firma/pdata o si SafetyHook entrega otro tamaño de parche, incluso si `prepare_inline` devuelve un objeto existente. No hay hook en `.xtls`, ni otro hook encima de0x2792A7B.

El detour llama al original exactamente una vez con los mismos argumentos, conserva retorno y error, y no escribe estado del mundo ni llama de nuevo a creación. Lee con `fast_read`/SEH sólo UUID, claves y resultado. Se registra **máscara de validez**:01 sujeto,02 controlador,04 campo,08 dueño,10 error,20 constantes; un bit ausente significa NO MEDIDO, no clave0 válida. Los errores ambiguos/constantes repetidas se clasifican«otro». Una lectura fallida no rechaza la petición nativa.

No hay formato/log ni asignación dinámica dentro del detour. ColaPOD256 con try-lock sin espera; contadores de pérdida, lectura fallida, variante sin decodificar y límite de presupuesto. Máximo32UUID por observación y1024B en tap ingress; excederlos sólo limita la medida, jamás el flujo nativo. El worker formatea fuera del cerrojo. El buffer ingress no se inicializa a1024B para evitar un memset innecesario desde el mid existente; la revisión de ensamblado final corresponde a la compilación conjunta.

### Pruebas puras y revisión

Tests de parser con Req/Ack/Nak reales, truncamientos completos, UUID distinto, longitudes, lotes, variantes2665 no medidas, separación por opcode y rol; pruebas de clasificación de error/ilegibilidad/ambigüedad; firma5B válida y negativas0–4/5modificada/15B. El nativo sigue siendo autoritativo: los tests verifican la observación, no la corrección de una puerta en vivo.

Revisión independiente por el agente `s42_revision_independiente`: corregidos revalidación del tamaño final del hook, validez de lecturas, inicialización del buffer ingress y filtro HostClient* por rol. No se ha ejecutado ninguna compilación desde este subagente; el resultado final de `scripts/build.ps1`, disassembly y revisión integrada lo informa el coordinador.

## Prueba mínima para resolver lo pendiente (la hará el usuario)

1. Mantener el mismo guardado/progreso y escena. `door_trace=probe` en ambos. Capturar desde antes de acercarse. Anfitrión solo: marcar Ctrl+F12 **antes de tocar la puerta**, intentar cruzar, marcar después; anotar posición y resultado. Entrar invitado y repetir con ambas ventanas visibles.
2. Cuando reproduzca el fallo, usar `door_trace=1` en la siguiente sesión equivalente para relacionar: envío invitado→ingress servidor→resultado nativo→salida→entrega. No alternar otras claves al mismo tiempo. Mantener far/near como pruebas separadas y registrar si hubo viaje rápido/vaciado U-1.
3. Buscar la candidata UUID, error bruto y máscara válida. Si sale éxito+2005 pero no pasa el jugador, la siguiente investigación es publicación/control/constraint de esa clave; si sale6CF701C, estudiar la preparación del mapa; si6CF7024, distinguir UUID ausente/activo con lectura específica del objeto; si6CF7028, seguir0x28308E0. «Otro» exige mapear ese código antes de diseñar cambios.
4. Éxito de **la medida**: al menos un caso positivo y otro negativo del mismo UUID con ruta completa, máscara1F/3F según constantes disponibles, sin pérdida ni eventos recortados en la ventana; correlación visual de la identidad. Éxito de **un futuro arreglo**: ambos cruzan tras abrir/cerrar repetidamente, cerca/lejos y tras relevo, sin resucitar progreso ni duplicar familia. La sonda sola no satisface ese criterio.
5. Aborto de la prueba: cierre, nueva incoherencia de familia, caída fuerte de rendimiento o error de instalación. No reintentar enganches a otra dirección. Si sólo faltan lecturas o presupuesto, la conclusión queda NV y se ajusta la medida offline.

Con `cdmp_peer` se verifica tránsito/decodificación/rechazos y los tests del parser; no reproduce carga visual, física ni una puerta que un cliente humano pueda usar. Shadow o dos clientes reales son necesarios para confirmar apertura. **El dato imprescindible ausente es el resultado real de0x282DC80 para este UUID durante el fallo; no hay base suficiente para prometer ya una solución definitiva.**