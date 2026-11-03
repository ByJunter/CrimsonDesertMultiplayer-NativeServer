# S34: cinco investigaciones (14-10-2026)

Build del juego **1.0.0.2976** (PE timestamp `0x6AB28F00`, base `0x140000000`). Rama `opcion-d-npc-s33-fix`
(worktree `C:\Emulador CD-npc-s33-fix`), código investigado en `d835806`. Las cinco son de **solo lectura**: nadie
tocó código, compiló, desplegó ni abrió el juego para hacerlas. Cada una la hizo un agente distinto.

| Archivo | Tema | Conclusión principal |
|---|---|---|
| [1-cierres-invitado.md](1-cierres-invitado.md) | Los dos cierres de Shadow (`exe+0x88F832`, `exe+0x1831598`) | Misma causa: U-e parte una familia (raíz con el invitado, hijos con el anfitrión), el cliente del anfitrión destruye los hijos huérfanos y el *timeout* de 10 s de `observer_hold` los manda al invitado con restore 0. Propone herencia familiar al nacer + nunca restore 0 hacia quien no controla. |
| [2-familias-npc.md](2-familias-npc.md) | Censo por tipo de lo que nace en las stages del usuario 2 y diseño de `npc_family_control=1` | Tipo 6 = animales del NPC; tipo 7 = `CreateServerActorDesc_InstantGimmick`. El padre se conoce por `[desc+0x2F0]` en el mismo `create_mid` `0x2792A7B`. Diseño con registro por Troc, exclusiones y contención por unidad. |
| [3-viajeros.md](3-viajeros.md) | Viajeros (AutoSpawnRoadReq 2550) quietos y diseño de `user2_road_control=1` | Confirmado: el cliente del anfitrión no inicia locomoción lejos de su personaje (0/82 del invitado se mueven, 73/76 propios sí). Procedencia por `desc+0x3C8/+0x3B8/+0x3CD` en el mismo `create_mid`. |
| [4-contencion-y-relevo.md](4-contencion-y-relevo.md) | Contención del paso 2b, ciudad vacía al irse el anfitrión, relevo a su llegada | Bug: el emisor de la acción se lee mal (`emisor=0` en 843/843), la contención retira NPC sanos. Hueco de 21,2 s al irse = 10,8 s de histéresis + 10 s esperando el 2043. Relevo a la llegada correcto. |
| [5-guardado-anfitrion.md](5-guardado-anfitrion.md) | El progreso del anfitrión no se guarda | El desvío `save_guard=redirect` reconstruye la caja en cada arranque: nada llega a la carpeta real. Propone `host_save=sandbox|probe|real` con publicación posterior y copias de seguridad. |

## Datos

Logs y capturas (reloj del invitado = anfitrión − 1 h):

| Ensayo | Anfitrión | Invitado (Shadow) |
|---|---|---|
| A (paso 1, primer intento; cierre `0x88F832`) | `C:\Emulador CD\backups\p8-sesion34\cdmp_20261014-180308.log` + `cdmp_packets_20261014-180319.*` | `C:\Emulador CD\backups\p8-sesion34\shadow\` (log `170438`, captura `170455`, `cdmp_crash_20261014-170638_1704_1.dmp`) |
| B (paso 1b, `npc_activation=probe`) | `...\p8-sesion34\paso1b\` (log `184959`, captura `185010`) | `...\p8-sesion34\shadow-p1b-p2b\` (log `175039`, captura `175057`) |
| C (paso 2b, `npc_activation=1`; cierre `0x1831598`) | `...\p8-sesion34\paso2b\` (log `190232`, captura `190242`) | `...\p8-sesion34\shadow-p1b-p2b\` (log `180502`, captura `180517`, `cdmp_crash_20261014-180710_6364_1.dmp`) |
| Pruebas en un PC con `cdmp_peer` | `C:\Emulador CD\backups\p8-s34npc-local\` | — |

Scripts de cada agente en [datos/](datos/). Algunos tienen rutas absolutas de donde se ejecutaron (`build\research\s34\`
o la carpeta temporal de la sesión); ajústalas si los vuelves a ejecutar. Python aislado: `-I -B`.

Documentos base: [../s33-npc-causas-y-diseno.md](../s33-npc-causas-y-diseno.md) (investigación S33) y
[../s33-npc-implementacion.md](../s33-npc-implementacion.md) (lo implementado y probado, con las revisiones).
