# Integración — cuatro pendientes de opción D

9-10-2026, build 1.0.0.2976. Investigación independiente con un servidor de Ghidra compartido, tres agentes especializados y el orquestador en compañeros. Se contrastaron decompilaciones con desensamblado de la copia en `D:\RE` y datos CDPK/logs existentes. **No se implementó ningún arreglo, no se ejecutó el juego y no hay pruebas nuevas en vivo.**

Informes: [reloj](reloj.md), [identidad](identidad.md), [salida](salida.md), [compañeros](companeros.md). Cada informe conserva evidencia, diseño, condiciones de fallo y revisión por otra persona del equipo. [E] código `.rsrc`, [E-x] fragmento legible `.xtls` sin hooks, [V] captura/log, [S] inferencia o propuesta. Los números de paquete del informe compañeros y de la revisión del orquestador son desde cero; reloj/identidad y las tablas principales de salida declaran su propia convención. Usar también fichero y timestamp para cruzarlos.

## Qué cambió respecto a las hipótesis anteriores

| Problema | Confirmado | No confirmado / límite | Confianza |
|---|---|---|---|
| Reloj | [E/V] La primera carga del invitado, en su aparcamiento privado, puede publicar en el único reloj servidor por ser dueño de ese campo. Su día0/hora24/escala0 se normaliza a día1/medianoche/escala0; el segundo viaje compartido recoge el reloj ya alterado. 2144 ya transporta ese valor antes del segundo LoadingComplete. | [S] Falta la traza de la instrucción escritora en vivo y medir reloj/cielo del anfitrión. No se detienen todos los temporizadores monotónicos. | Alta en causa; diseño sin ejecutar. |
| Identidad | [E/E-x/V] Hay **dos asignaciones independientes** de `7FFFFFFFFFFFFFFF`: lobby y carga Status. Búsquedas/caché por characterNo toman la primera coincidencia. Cambiar solo lobby o paquetes no basta. | [S] Normalizar tras la carga completa debe acreditar que ningún consumidor interno usó antes la identidad provisional. No están inventariadas todas las referencias de objetos/compañeros ni su persistencia. | Alta en colisión; media en diseño. |
| Salida | [E/E-x/V] El cierre nativo utiliza Req2001 de 3 B y Ack2003/2002 de 7 B; el sniffer descarta <5 B y el túnel presupone tramas de 5 B. La caída solo detiene el relevo; C1g deja al usuario y sus actores en el campo después de BROKEN. | [S] No se ha ejecutado la retirada/reconexión del usuario sintético. Finalizar un Ack no demuestra que SAM, campos, tareas y referencias ya estén limpios. | Alta en causa y formato; media en recuperación completa. |
| Compañeros | [E/V] Creación del hijo y restauración de acciones son fallos separados. C1g arregla Kliff, pero su mercenario llega con restauración0. El anfitrión normal ya tenía su principal desde el aparcamiento; un vecino lo acaba de crear. | [S] El log no decide entre usuario no adquirible, fábrica/slot, ConfirmSlot o mapa. El estado no listo es una hipótesis, no una rama probada. Tampoco hay un retardo universal de 0,3–0,6 s ni prueba de ausencia de acción en cada frame. | Media en carrera; baja en rama exacta; alta en contrato de restauración. |

### Hallazgos transversales que importan para P8/P9

- [E/E-x] **2358 de login es privado**, mediante `ToTarget(actor.vfE8)`. Si se reenvía al anfitrión, su handler puede modificar una global, pero eso no demuestra que el reenvío ocurriera en C1g.
- [E/V] La global **`6C8CD98` es la versión de restauración/migración** (`CharacterStatusSaveData+60`), y ya la cambia la carga común `1791B98`. Capturas muestran versiones15 y5. Una partida propia del invitado no queda aislada porque se cambie characterNo o se mantenga privado2358. No habilitar partidas incompatibles hasta contextualizar sus lectores; restaurar una global al terminar la carga deja ventanas de concurrencia y migraciones erróneas.
- [E] **2790 ReserveShutDownServerReq** encola apagado global: no reenviarlo de invitado a anfitrión. **2004**, con datos internos/punteros, no pertenece al túnel. Admitir 2001/2002/2003 exige excepciones exactas, no abrir todos los mensajes internos.
- [V] C1g entrega a la sesión2 al anfitrión y su mercenario dentro del **mismo 2005**. Retener el paquete completo esperando que se cree su padre puede impedir para siempre crear ese padre. Una barrera de dependencias debe demostrar soporte para paquetes mixtos.
- [E/S] La revisión independiente localizó además `ScopeSenderAck`2042: su handler puede llamar directamente al vf2 del submensaje. Una barrera solo en `35D2A0` y solo para opcode2005 no cubre automáticamente cargas anidadas. C1g no contiene2042; es una limitación material para generalizar a P8.
- [V/S] El peer reproduce Req; no ejecuta IA ni dibuja actores. La ausencia de acción del mercenario del peer no se convierte en una promesa de que P8 la recibirá en 0,4 s. La animación, los punteros cliente y el ciclo visual requieren Shadow.

## Orden de implementación y dependencias

1. **[S] C4: ciclo de vida y cierre.** Un carril de control validado extremo a extremo y cierre nativo por sesión/usuario/época, con admisión de reconexión solo tras comprobar retirada. Permite cancelar de forma segura los fallos de los demás cambios. Usa claves nativas y relación de sesión, no characterNo; por ello no necesita esperar a C2. No reabrir una sesión antigua poniendo a cero flags.
2. **[S] Reloj.** Separar autoridad mundial de dueño de campo **desde el primer aparcamiento**, entradas `.rsrc 2BC0A30` y `219D7F0`; política adicional en `2BC8970` para cambios explícitos de hora. Copiar estado al componente invitado sin publicar global, mantener la ruta del anfitrión. No guardar/restaurar el reloj después de la escritura.
3. **[S] Identidad.** Registro estable de perfiles/sesiones, lobby por `2B7F4D0`, Status por envoltorio de `20688A0`, invariantes en `2941020` y consumidores. Lobby=3093=Status=publicación. C4 cancela una identidad parcial y evita reutilizarla mientras persisten actores/cachés. Aislamiento de versión de migración y propietarios es una condición aparte para P9.
4. **[S] Compañeros.** Primero observar la rama exacta de creación en `.rsrc`, luego aplicar la corrección que corresponda. En paralelo, asegurar acción inicial válida antes de uso: autoridad nativa/instantánea restaurable, o restauración cliente acotada de datos serializados válidos según el diseño condicionado del informe. P5 debe entregar las acciones posteriores; una espera fija o contenciones de puntero no resuelven el origen.

[S] Los cuatro arreglos deben usar `(conexión admitida, época, usuario, clave/generación de actor)`. Hasta tres invitados independientes para aforo4; no un global «usuario2» ni identidades inferidas por nombre. C4 invalida colas de compañeros y asociaciones de caché/identidad; reloj permanece del mundo del anfitrión al salir o reconectar cualquier invitado. P6 y el guardián siguen activos en todas las pruebas.

## Pruebas propuestas — una sesión discriminante por arreglo

No se ejecutaron. Los informes contienen firmas, puntos de observación, mensajes esperados y extensiones; esta tabla resume los criterios que determinan si seguir.

| Prueba | Secuencia y éxito | Parada |
|---|---|---|
| **C4 salida/reconexión** | [S] C1g con peer: despedida2001, verificar retirada completa, reconectar; corte abrupto, repetir retirada/reconexión. Campo y actores anfitrión se conservan, sin entradas de usuario viejo en sectores/campo/SAM ni tramas de época anterior. | Estado/propiedad inválidos, cierre que no progresa, contador incoherente, escritura persistente, efecto sobre anfitrión o petición global no permitida. Sin reutilización forzada. |
| **Reloj** | [S] Medir5s, ambos viajes del peer, una finalización con flag01 para cubrir P8, observar60s. Global mundial intacto, getter avanza, escala12 normal, pausa anfitrión intacta y segundo2144 con hora correcta. | Escritura mundial por invitado, escala0 en campo normal, salto no explicado, pérdida de Ack o cambio de pausa. Cerrar medianteC4. |
| **C2 identidad** | [S] Perfiles de prueba compatibles, número invitadoF002 propuesto; capturar lobby/Req/Status/Acks y provocar búsquedas nativas de caché para ambos. Salida y reentrada del mismo perfil sin duplicados. | Publicación **o consumo interno** de número provisional, caché resuelve otro actor, cambia versión de restauración del anfitrión, perfil ambiguo o falla el guardián. |
| **Compañeros** | [S] Observar retorno exacto de creación y estados; con el diseño correspondiente, exigir hijos únicos, ranuras correctas, acciones válidas y NPC nuevos sin congelación. La prueba puede detenerse en observación si refuta la hipótesis; no se fuerza una corrección improvisada. | Rama inesperada, paquete mixto no soportado, estado/ABI desconocido, timeout3s, crecimiento de colas o alteración de actor propio. C4 retira la época. |

[S] Tras cada sesión discriminante, P8 real debe comprobar ambas pantallas y estado cliente; luego ampliar a tres invitados solapados, salida de uno y reconexión. Son validaciones necesarias de alcance, no pruebas que el peer actual ya haya pasado. El guardado de perfiles distintos sigue siendo P9 y no queda autorizado por estos resultados.

## Qué no está listo para declarar resuelto

[S] No hay pruebas de un arreglo instalado. Reloj/identidad/salida tienen causas suficientemente localizadas para implementar cambios instrumentados; su recuperación concurrente sigue pendiente de validación. **Compañeros no tiene aún una causa de rama exacta**, ni una ABI demostrada para dividir/reemitir snapshots mixtos: el informe ofrece una prueba que decide esas lagunas y una corrección condicionada, no una implementación general certificada.

## Revisión y cierre

Revisión cruzada terminada: agente salida revisó reloj; agente reloj revisó identidad; agente identidad revisó compañeros; orquestador revisó salida. Las secciones «Revisión» contienen tres intentos de refutación, consultas/datos independientes y correcciones de alcance. Se corrigieron también el contenedor de lobby, la diferencia entre publicación y consumo interno de identidad, la validación del índice cliente de2002 y la falta de atomicidad demostrada de una adaptación temporal del flag de restauración. La variante cliente de compañeros queda experimental y condicionada; no es una receta de implementación certificada.

[V, entorno] **Ghidra detenido a las 08:19:51 del 09-10-2026 (UTC−06)** mediante `server-stop.ps1 -Instance 5d304309945e44dc9df4d99a54ae465a`. Respuesta `bye 15540`, `PARADO=si`, cero procesos de esa instancia. Comprobaciones del host: **Get-Process java=0**, javaw=0, CIM java.exe/javaw.exe=0, escuchas en el puerto 18490=0. Evidencia: [cierre-ejecucion.txt](cierre-ejecucion.txt).

[V, entorno] El archivo principal del programa Ghidra `db.9.gbf` conserva tamaño 2016788480 y LastWriteTimeUtc `2026-10-09T06:07:42.7694691Z`, iguales al inicio; el [log](ghidra-servidor.log) acredita apertura `read-only` y parada. Es una comprobación de metadatos y modo de apertura, no una comparación criptográfica de todo el proyecto. No se cambiaron nombres, comentarios, firmas ni análisis. Los resultados de consultas y log se dirigieron al temporal autorizado; las escrituras incidentales de índices/cachés del framework se distinguieron en 00-plan.

[V, entorno] `git --no-optional-locks -c core.fsmonitor=false status --short` mostró únicamente `?? docs/research/pendientes/`. No se hicieron commits, staging, cambios de rama, compilación ni instalación; no se accedió al directorio del juego. Las únicas escrituras de trabajo fueron informes/evidencias aquí y scripts/resultados temporales en la carpeta autorizada. Las pruebas de juego quedan descritas para la conversación que implemente.
