# S42: enemigos del invitado, análisis estático (en curso)

Build 1.0.0.2976, PE 0x6AB28F00, base 0x140000000. Código propio en rama opcion-d-enemigos desde effae47. No se ha ejecutado el juego ni escrito partidas/bin64. Etiquetas: [V] fuente primaria reproducida, [E] inferencia fuerte, [H] hipótesis, [NV] no verificable aún.

## Hallazgos iniciales

- [V código] `combat_follow.cpp` `tick_v1` y `tick_v2` leen `[actor_cliente_anfitrión+0xC8]` y sólo sintetizan movimiento si vale 1 o 2. Los actores remotos (0) se contabilizan como `not_local` y se omiten. Por tanto, no basta decir que «todo actor 0xB» es sobrescrito por esta pieza; hay una puerta adicional explícita. Falta comprobar los valores del actor concreto en vivo.
- [V descompilado .rsrc] `0xA35B90`, ClientTargetActorComponent::vf52: usa la tabla del personaje local sólo cuando la clave del usuario local coincide con `[campo+0x180]`. No se ha medido el resultado de esa comparación en vivo. `server.cpp` reescribe el dueño del campo en los mensajes 2174/2643 hacia el usuario local del invitado; por ello es incorrecto afirmar que la comparación sea falsa por definición.
- [V lectura .xtls; NO punto de enganche] `0xE6984B0` legible: resuelve el campo del actor y el personaje dueño del campo, luego consulta `[[personaje+0x68]+0x188]+0x18`. Si falla obtener campo o personaje devuelve 5. **No se ha demostrado el valor efectivo de retorno ni la presencia de la tabla del anfitrión remoto en el invitado.**
- [V lectura .xtls parcial; NO punto de enganche] `0xC66E170` compara índices u16 de facción: índice FFFF retorna 5; iguales retorna 3. Tabla por índice con stride 0xD0, vector de relaciones +0x40/+0x48; relación 4 cae a grupos y puede acabar en 5. Contiene una instrucción mutada espuria en el decompilado: no usar todo el pseudocódigo como especificación.
- [NV] No se ha demostrado un fallo en las relaciones de facción. La captura S38 entrega los mismos 20 pares de 2597 a ambos clientes con su destinatario local. La hipótesis inicial de dueño de campo remoto no está sustentada y queda retirada. No se propone forzar hostilidad.
- [V .rsrc] `0x209CB30` comprueba inmunidad usando tipo, CharacterInfo+0x41C, buffs de Skill, bandera del contexto +4&0x20 y llamada final a C892330 (.xtls). No contiene comparación directa de controlador +0x90. Para interpretar «nunca mueren» hacen falta HP/AttackTarget/ApplyBuff de las claves afectadas.

## Fuentes y reproducción

- `powershell -NoProfile -ExecutionPolicy Bypass -File D:\RE\CrimsonDesert_2976\scripts\cdmp-query.ps1 decomp 0x140A35B90`
- Mismo comando para VA 0x14E6984B0, 0x14C66E170, 0x14209CB30.
- Exports originales: `D:\RE\CrimsonDesert_2976\exports\decomp\0xa35b90.c`, `0xe6984b0.c`, `0xc66e170.c`, `0x209cb30.c`.
- Documentación previa usada sólo para localizar fuentes, no como verificación: `companeros-mercenarios.md` §3, `opcion-d-notas/optD-combate.md` §2.6, `pieza4-dano-servidor.md`.


## Causa demostrada de la pérdida de control (15-10, actualización)

[V] En `C:\Emulador CD\backups\p8-enemigos-invitado-20261015\host.log`, 60872–60878 asignan la raíz B0101636 y seis miembros tipo3 a 90100065 (invitado), unidad NPCSchedule recurso C12CB783, nodo4. En 60879 su hijo B010163D tipo5 divide la unidad por «tipo fuera de la cohorte o inesperado»; 60881/60883 sus vehículos enlazados B010163E/F quedan con el anfitrión y se rechazan por el controlador del padre. 60886–60900: ocho piezas tipo7 B0101641…48, bajo el gimmick B0101640, también dividen por controlador del padre.

[V] La unidad original queda PARTIDA en 60911 y se retira en 60912–60955. En 60956 se repone B010164D con el anfitrión por «su nodo ya partió una unidad». No es una familia de AutoSpawnRoad: el registro 60959 la identifica NPCSchedule bajo el 2045 del usuario2. El recurso está en su stage 200002F6; el cliente anfitrión ve binding con instancia NULL (61208+). Las capturas independientes corroboran 2005 con controlador90100000 en ambos PC y acciones2223 originadas por el anfitrión.

[E] El reparto actual de fase B devuelve por diseño la familia de bandidos al cliente del anfitrión cuando contiene carro/enlaces/carga. Eso explica de forma directa que el invitado no dirija su IA; no demuestra cada causa del daño o del minimapa.

[V] La afirmación universal «nunca mueren» queda refutada en la captura disponible: B010164E recibe muerte2530 a12:40:47.571, asesinoA0100066. [NV] Los 42 ataques2922 aB0101650 sin2147 no identifican por sí solos dónde se descarta daño. La sonda de inmunidad209CB30 debe medirlo. [V] El delito2740 medido corresponde al caballoB0101656, no al bandido: no permite deducir facción civil de todos los atacados.

## Contrato estático de vehículos y límite de seguridad

[V .rsrc] `0x2797510` requiere tipo5 en CharacterInfo+0x68; crea hasta dos descriptores base con envoltorio `_LinkedVehicle`, enlaza `desc+0x2F0` y el vector del padre+0x2F8, y escribe `desc+0x490=[actor_padre+0x60]`. La propagación de datos de carretera es condicional: esta ruta también existe para un padre NPCSchedule. Fuente: export `D:\RE\CrimsonDesert_2976\exports\decomp\0x2797510.c`.

[E] Una extensión completa tendría que admitir tipo5 Schedule, comprobar LinkedVehicle+490 para cada enlace, y conservar gimmick+carga tipo7 enteros con el anfitrión. Ampliar sólo npc_family_types a5 deja rotos los enlaces y la carga. No cambiar desc+0x40, actor+0xA0, HP, facción ni progreso.

[V código] La protección del `cleanup_one` por sí sola no protege el drenaje: `drain_now` llama `guarded_load` (programa vaciado) y eventualmente `guarded_unload`. El control de asientos debe ser fresco dentro de ScopeAttacher antes de cada Logout, y la ausencia en SAM debe preceder a todo Unload nativo. Una lectura «libre» antes de soltar el scope no autoriza el Unload posterior.

[V código] Además, `logout_drain` es void e ignora el resultado de `drain_now`; `user2_stages_troc_enter` tampoco impide que el original2001/2283/2362 continúe cuando se difiere nuestro drenaje. [NV] No se ha demostrado que ese camino nativo desmonte de forma segura a un jugador sobre un vehículo perteneciente a una stage de otro usuario.

[V lectura .xtls parcial, no hook] `0x2068340` es un thunk .rsrc de5bytes aE0D9AF0; el código legible guarda motivo/error y llama vf+0x80/vf+0x110. No prueba desmontaje seguro. [V .rsrc] `0x287D4C0` recorre las escenas y delega la retirada de horario a thunk de .xtls10225FC0. No se ha cerrado toda la cadena de destrucción de usuario/gestor/vehículo.

Por esa razón la propuesta activa NO puede declararse segura sólo por resolver clasificación y pruebas puras. Hace falta demostrar esa frontera nativa o mantener la extensión en observación. No se ha ejecutado juego, deploy ni prueba que escriba memoria del juego.

## Entrega implementada y revisada: observación, no arreglo activo

La entrega conserva la política pura candidata en `src/mod/session2/npc_family_vehicles_logic.h` y añade una sonda en el mid compartido que ya existía, sin instalar otro gancho. `npc_family_vehicles=0` mantiene B; `probe` observa; pedir `1` produce advertencia `solicitado=1 efectivo=probe`. Se ha retirado la rama nueva de escritura, no sólo ocultado la recomendación de activarla.

La sonda se limita explícitamente al recurso medido C12CB783. Clasifica todos sus nacimientos mientras el marco Troc vive: tanto unidad admitida como reposición con NodeSplitBefore. Proyecta controlador del padre para no dar por mala una carga por el controlador que habría corregido la alternativa; registra tipo, descriptor, envoltorio, resultado real y resultado sombra, y conserva cargo/gimmick7 con el anfitrión. El censo es de CLASIFICACIÓN; `capacidad_sombra_no_reservada=1` advierte que no constituye admisión garantizada ni reserva prospectiva acumulada.

La revisión independiente encontró que una lectura extra fallida en probe podía reemplazar `r->cls` de un nodo reservado. Se corrigió con `actual_class`: sólo un hijo tardío de unidad viva usa la observación fresca como antes. Las pruebas cubren reservado válido + sombra ilegible para0/probe/1, modos efectivos, padres/controlador/identidades, enlaces+490, carga transitiva y reutilización de la memoria del marco. No hay pruebas que pretendan validar una retirada de vehículos que no está integrada.

Reproducción estática: ejecutar `C:\Emulador CD\build\research\venv\Scripts\python.exe -I -B C:\Emulador CD-enemigos\build\research\s42\enemigos-estatico\inspeccionar.py`. Salida `primarias.json`: SHA256 del exe, secciones, pdata (null cuando no existe), primeros24bytes y recuento de esa secuencia por sección. No todas esas secuencias son únicas: no se proponen como firmas de enganche. Desensamblados por RVA en la misma carpeta. Los análisis de capturas independientes están en `build/research/s42/enemigos-capturas/`.
