# S42 — enemigos: capturas y logs (en curso)

Build 1.0.0.2976. Análisis exclusivamente de disco; sin juego, cambios de código, compilación ni despliegue.

## Hallazgos primarios iniciales

- [V] El parser independiente `analizar.py` recorre cuatro CDPK v2 completos. Las capturas guardan QPC absoluto, no `capture t=` relativo: se resta el primer registro y se suma la mediana de anclas `(~hh:mm:ss) capture t=` de cada log. En Shadow se añade una hora como aproximación de pared; la alineación fina entre PC exige cotejar paquetes iguales.
- [V] Local: 685.811 registros host y 621.501 invitado; 79 y 13 huecos explícitos, cada uno de un registro. Sin registros truncados. Cinco tramas iniciales de conexión no encajan con el encabezado Troc de cinco bytes. No se interpretan como pruebas de actores.
- [V] Los 81 `2922` de resultados de impacto llegan a ambos clientes. La familia atacada es `Bandit`, tipos 3: raíz `0xB010164D`, hijos `164E`, `164F`, `1650`… El nacimiento contiene controlador `0x90100000` (anfitrión) en los dos extremos, sin procedencia de carretera (campos candidato requester/road/sub a cero).
- [V] `0xB0101650` recibe 42 resultados 2922 del personaje invitado `0xA0100066`; `164D` recibe 12, `164E` cuatro y `164F` cinco. Estos contadores son resultados de impacto, NO equivalen automáticamente a daño positivo de vida.
- [V] Hay una muerte nativa: `2530` de `0xB010164E`, killer `0xA0100066`, a 12:40:47.571 host (offset binario 247502386), 12:40:47.577 invitado (237927414). Refuta que todos sean absolutamente inmortales.
- [V] El único `2740` de delito visible en el invitado corresponde a `0xB0101656` (`Priden Wagon Horse`, tipo 5, padre `0xB0101654`), a 12:41:06.985 (offset 246453700), junto al impacto sobre ese caballo. No identifica un bandido como víctima del delito.
- [V] El cliente anfitrión origina las `2223` de esos bandidos: 35/27/33/68 para D/E/F/1650; el invitado no origina ninguna para esas claves. Esto refuta atribuir directamente este episodio a una asignación de control a usuario 2.
- [V] S38 contiene sólo nueve 2922 en cada captura y todos son `0xA010027B` contra sí mismo. Sus marcas no prueban impacto contra un NPC; se necesita separar percepción de hostilidad del daño confirmado.

Resultados completos, con offsets y payloads: `local-host.json`, `local-guest.json`, `s38-host.json`, `s38-guest.json`. Script de reproducción: Python del venv con `-I -B analizar.py`.

Pendiente: descriptor y mecanismo de nacimiento de la familia, vida/daño neto, relación de hostilidad y cronología fina Shadow. No se concluye aún causa raíz general ni se identifica minimapa en estas capturas.

## Ruta del nacimiento: causa demostrada de la pérdida de controlador

[V] No es una cohorte 2550. La stage propia del invitado se crea en host.log:60280, recurso 0xC12CB783, stage 0x200002F6. guest.log:34665 resuelve el recurso como stageseq/sequencerspawn/npc/cd_seq_spawn_docu_npc_extralarge_0021.paschedule.

[V] host.log:60959: U-e asigna primero la raíz 0xB0101636, descriptor CreateServerActorDesc_NPCSchedule, nacida en el 2045 del invitado, a 0x90100065. host.log:60911 registra esa familia PARTIDA: siete nodos, veinte nacimientos, seis miembros asignados, tres conservados por el anfitrión, once partidas y trece tardíos; motivos «el padre no tiene el controlador de la unidad; tipo fuera de la cohorte o inesperado». Contención retira la unidad (60912–60955).

[V] La reposición raíz 0xB010164D conserva el anfitrión por la política de nodo ya fallido (host.log:60956: «su nodo ya partió una unidad»). Los 2005 de ambos PC y sus primeras Req prueban que ese fallback se materializó. El carro 0xB0101654 y caballos 1655/1656 son tipo 5; además hay gimmicks tipo 7. El NpcFamily controlador 90100065 de la línea de censo describe el contexto de la unidad, no prueba el controlador final de todos sus actores; los 2005 sí son inequívocos.

[E] Esto explica que **esta familia** deje de simularse en el invitado pese a nacer alrededor suyo. No demuestra por sí solo que el control anfitrión sea la causa de neutralidad, ausencia del minimapa o ausencia de daño: faltan medidas del vínculo de IA, relación efectiva y decisión de daño.

## Daño, muerte y delito

[V] Todos los bandidos D/E/F/1650 comparten facción numérica 1000128 en el registro 2005 +0x1A sin el prefijo u16 de tamaño (equivalente +0x1C con prefijo). Desplazamiento contrastado por el agente estático con el escritor de registros; el parser conserva bytes originales para auditarlo. El nombre viene del contador UTF-16 +0x1E y bytes +0x22, no de una inferencia por tipo.

[V] No se capturó ningún VaryHPAck 2147 para D, F ni 1650, pese a sus 12, 5 y 42 resultados 2922. E tiene exactamente uno en ambos extremos: HP=0, máximo=50000 en escala ×1000, secuencia=1, fuente A0100066, seguido del 2530. Layout de 2147 usado: docs/research/combate-coop.md:96; +00 clave, +04 stat 1000000, +08 HP i64, +18 máximo i64, +40 secuencia, +54 fuente. Este lector no ha vuelto a desensamblar el writer de 2147; la muerte 2530 y bytes están conservados independientemente.

| Prueba | Host (offset binario) | Invitado (offset binario) |
|---|---:|---:|
| E: HP0 | 247502265 | 237927293 |
| E: muerte | 247502386 | 237927414 |
| E: baja motivo25 | 247538267 | 237970301 |
| Delito por caballo1656 | no recibido por este cliente | 246453700 |

[V] La baja de D/F/1650 a 12:41:30.451 lleva motivo19; no se interpreta como muerte. El caballo1656 tiene baja44. El tipo7 Game Mechanic 175F recibió cinco 2922, tampoco es un bandido.

[H] Es plausible que el vínculo de IA de una escena del invitado ejecutada por el cliente anfitrión quede incompleto: host.log:61208/61210/61212/61214 encuentran el actor dueño A0100066 pero la instancia de escena es NULL. Es una pista de causa del comportamiento, no una prueba de causalidad del daño.

## Facciones: contraste que limita la hipótesis

[E] El handler .rsrc RVA 0xB92C80 (D:\RE\CrimsonDesert_2976\exports\decomp\0xb92c80.c) lee 2597: prefijo byte, actor u32, reset u8 y entradas de 13 bytes (facciónA u32, facciónB u32, relación u8, dato u32). La ausencia de entradas de una facción no implica neutralidad: hay que evaluar el fallback nativo.

[V] Local: el invitado recibe una instantánea a 12:31:59.594, offset 4179971, reset1, diez pares. No hay 2597 en la captura anfitrión; eso no prueba que carezca de tabla en memoria ni qué contenía una partida previamente cargada.

[V] S38: anfitrión recibe veinte pares a 12:57:12.473, offset 5171087; invitado veinte pares a ~12:59:54.286 hora anfitrión, offset 6116834. **Son idénticos byte por byte salvo actor destino** (host A0100001, guest A010027B). En ambos: 1000000→1000138 y →1000010 relación3, entre otros. La facción1000128 de los bandidos locales no figura en ninguna de esas tablas. No es válido concluir que falta sincronizar relaciones en S38 solamente por el síntoma visual.

[V] S38 no contiene ningún actor con nombre Bandit en los 2005 leídos, ni 2922 de golpe entre NPC y jugador: sus nueve 2922 son A010027B→A010027B. No se identifica una clave enemiga concreta en sus marcas con esta evidencia. Las marcas del anfitrión (13:03:28.610/36.269) y Shadow (12:02:18.807/12:03:03.298) son pulsaciones distintas; no son puntos simultáneos para ajustar el reloj.

## Qué no está demostrado y siguiente medida

- [H] Hostilidad/minimapa: medir la relación nativa efectiva (par consultado, tabla/fallback y resultado), clasificación del NPC y bind de escena; una imagen y el 2005 no bastan.
- [H] Vida: los 42 resultados sin 2147 no identifican todavía rechazo de daño, protección de escena, invulnerabilidad, reacción sin daño o regeneración. Medir decisión de daño y HP nativo para la misma clave/vida.
- [V] Una prueba con carro debe distinguir la primera familia asignada y cancelada de su reposición nativa en fallback; no sumar ambas generaciones.
- [V] No procede interpretar el único delito como prueba de que los bandidos se marcaron civiles: la víctima registrada es un caballo.
- [H] Corregir familias puede eliminar la caída al anfitrión y mejorar la IA; la mejora de hostilidad y daño requiere prueba en vivo separada. Mantener sonda para no prometerla como causalidad demostrada.

## Reproducción y artefactos

Ejecutar con C:\Emulador CD\build\research\venv\Scripts\python.exe -I -B, en este orden: analizar.py, finalizar.py, archivar.py. Sólo leen capturas/logs y escriben esta carpeta.

- cohorte.json y tabla-cohorte.md: resumen auditable de claves, facción, controlador, impactos, vida, muerte y offsets.
- evidencia-minima.json: bytes originales pertinentes (2005, 2147, 2530, 2740, 2597 y primera Req) para revisión independiente.
- extractos-log.txt: líneas primarias de marcas, stage, familia partida y fallback.
- manifiesto.json: SHA-256 de las ocho fuentes originales y scripts/resultados compactos.
- Los cuatro JSON completos (~100 MB cada uno) son material local regenerable; **no es necesario meterlos en Git**.

## Alineación Shadow por paquetes idénticos

[V] Los nueve 2922 emparejados byte por byte sitúan el reloj Shadow+1h entre 0,618 y 0,634 s después del host (mediana 0,627 s). Este delta mezcla reloj residual y tránsito; no se presenta como latencia ni se resta como reloj exacto. Ver alineacion-shadow.json, offsets y SHA256 de cada payload. Las pulsaciones Ctrl+F12 no se usan como sincronizador. Reproducir además alineacion.py tras archivar.py.
