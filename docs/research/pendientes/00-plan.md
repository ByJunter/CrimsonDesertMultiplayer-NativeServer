# Plan de análisis de pendientes — opción D

9-10-2026. Build 1.0.0.2976; RVA relativas a 0x140000000. Encargo leído íntegramente; contexto común revisado antes del reparto. Diagnóstico y diseño, sin implementar.

## Reparto y revisión

Por el límite de cuatro agentes activos incluido el orquestador, se trabaja en cuatro frentes simultáneos: agente reloj, agente identidad, agente salida y orquestador compañeros. Solo el orquestador arranca y para Ghidra. Revisión cruzada prevista: salida revisa reloj; reloj revisa identidad; identidad revisa compañeros; orquestador revisa salida. Cada revisión intenta refutar las tres afirmaciones centrales y se incorpora al informe correspondiente.

| Problema | Hipótesis inicial, sin dar por probada | Evidencia prevista |
|---|---|---|
| Reloj | La entrada no dueña aplica o reinicializa estado temporal compartido; separar instantánea FieldTime, reloj en marcha y pausa del campo. | Xrefs de 0x6A6D238 y escritores; 2BC0A30/20E55A0/21A1CC0/219D7F0; logs C1e–C1g frente a B3 y capturas Req/Ack. |
| Identidad | El lobby sintetiza characterNo fijo y las búsquedas por número colisionan; no asumir que cambiarlo allí basta para todas las identidades. | 2B7F4D0, 8AEB30, cachés cliente/servidor, CharacterStatAck, capturas y guardián actual. |
| Salida | La caída del túnel carece de transición nativa de cierre, dejando usuario y campo retenidos. | Cliente FlushSendQueue, protocolo interno, cierre nativo en .rsrc, captura 105149 y estado de Session2. |
| Compañeros | La visibilidad llega antes de cumplir dependencias del mercenario; una segunda carrera afecta a actores remotos sin primera acción. | 8AF580/206B2D0, ActorLoadData y acciones en C1g/112627, carga propia frente a observadores y contenciones D-44/D-46. |

## Reglas de evidencia

- [E]: código .rsrc verificado ahora; [E-x]: bytes legibles .xtls, sin proponer hooks allí; [V]: captura/log concreto con tiempo o número de paquete; [S]: inferencia o diseño aún no probado.
- No elevar una anotación cdmp_ ni una afirmación previa a evidencia independiente. Contrastar instrucciones importantes con capstone sobre la copia de D:\RE.
- Informes: reloj.md, identidad.md, salida.md, companeros.md y resumen.md. Evidencias auxiliares solo dentro de esta carpeta; scripts propios solo en <scratch>\pendientes.
- No código, tests, commits, juego ni modificaciones semánticas del proyecto Ghidra. No registrar MCP.

## Ghidra y alcance de escrituras

El encargo autoriza expresamente usar y cerrar el servidor Ghidra de consultas con -noanalysis -readOnly. Se conserva esa ruta y el script Java existente. Para respetar la ubicación de resultados se usa un lanzador temporal propio equivalente al server-run, que dirige el log y los .c a la carpeta temporal permitida; no se sobrescriben exports ni scripts instalados. La parada será con el server-stop.ps1 existente y el id de instancia.

Las escrituras incidentales del framework al abrir/cerrar (índices y cachés) están documentadas en SETUP §7 y son inherentes al uso solicitado; no se cambia el programa analizado, nombres, firmas o comentarios. Se comprobará el estado del programa y la ausencia de java.exe al cerrar. Si el servidor no está disponible, los agentes no recurren a headless ni arrancan otro: avisan al orquestador.
