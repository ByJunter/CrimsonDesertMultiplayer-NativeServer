# Pendientes (16-10)

Lista viva de lo que queda abierto, por prioridad. Cada línea apunta a sus datos.

## Prioritarios

1. **Tenderos** (primero: desbloqueado por S44 y S45).
   - El trabajo de la otra IA está en la rama `opcion-d-tenderos` `a294fac` (`docs/research/s25/s41-tenderos-p2.md`).
   - Base para retomarlo: S44 (`s44-escenas-del-anfitrion.md`) y S45 (`s45-escenas-ajenas.md`): las escenas del
     anfitrión ya no mueven su copia en el invitado (S44) ni actúan sobre la cámara, la entrada o la interfaz del
     invitado (S45, R-A + R-B). La tienda del anfitrión es inerte en el invitado; la del invitado es su escena propia.
   - El servidor ya crea una tienda por jugador. Lejos del anfitrión, las escenas del invitado llegan antes que sus NPC.
   - La «gemela» cerca del anfitrión borró la tienda del anfitrión (`backups\p8-2inst-tenderos-p2b\`).
2. **Enemigos del invitado con vehículo.**
   - La familia se parte por el carro y el control vuelve al anfitrión (S42, `s42-enemigos-puertas.md`).
   - Se puede reproducir en el punto `0xF6088`, a unos 580 m de la taberna de Hernand: `s43-datos/` y el informe del encuentro en `s42-datos/`.
3. **Puerta del invitado: segunda comprobación.**
   - `creacion-fallida-6CF7028` en `0x282DC80` → `0x28308E0` (`s43-resultado-vivo.md`, prueba de la pose).
   - El recurso estaba listo y solo el invitado falla.
4. **Zona muerta de 4–9 celdas** (16-10, `backups\p8-2inst-s44-regresion\`, informe `scratchpad\s45\enemigos.md`).
   - Con el invitado a 4–9 celdas del anfitrión, nadie crea las stages de esa zona:
     - el gestor del anfitrión solo crea a 1–3 celdas de su personaje;
     - U-1 solo se arma a ≥ 10 celdas.
   - Visto con el campamento de bandidos al sur de la taberna de Hernand, a 6 celdas: no aparecen hasta que llega el
     anfitrión.
   - No es una regresión de S43/S44; ya estaba descrito en `mundo-escenarios-propios.md` §3.3.
   - Propuesta: umbrales en el ini; armar a ≥ 4 y vaciar a ≤ 2–3 celdas con ~3 s de espera, apoyándose en U-f2
     (`dup_skip`) para el solape.
5. **Carga nativa del anfitrión con DL≠0.**
   - Borra los SceneLevels listos que el invitado sigue usando: 173 en `p8-2inst-s43-activo`.
   - `scene_level_share` solo lo observa.
6. **Objetos del escenario destruidos que no vuelven** (16-10, `backups\p8-2inst-s45-paso3\`, hacia las 08:20–08:22).
   - El anfitrión rompió bancos y otros objetos peleando con NPC en la plaza de Hernand, para que lo arrestaran.
   - Esos objetos no reaparecieron: los NPC se quedan sentados en el aire. Además desapareció el árbol del centro de la
     plaza.
   - Se ve en las dos ventanas (anfitrión e invitado).
   - Falta saber si es nativo (el juego quizá regenera los objetos con el tiempo o al recargar la región) o si lo causa el
     mod: `scene_level_share`, el vaciado de U-1 o las retenciones de gimmicks.
7. **S45 entre dos PC (Shadow).** Validado con dos copias en un PC (`s45-escenas-ajenas.md` §11); falta la red real.

## A investigar

- ~~**Escenas del anfitrión que aplican control al personaje del invitado**~~ — **resuelto** (S45,
  `s45-escenas-ajenas.md` §1): los 11 casos de vf21/vf22 de `p8-2inst-s44-probe` eran escenas **propias** del invitado
  (su entrada en el mundo). La tienda vacía y la cinemática de la cárcel, que eran lo que se veía, quedan resueltas por
  S45 (R-A + R-B, validado en vivo el 16-10, pasos 2 y 3).
- **S44 H-2:** el dueño sigue siendo alcanzable por `[stage+0x98]` (`desc+0`). Con el modo 1 no hubo síntoma visible.
- La cinemática de carga `0x8CD771B1` sigue denegada por lista: recarga el campo del cliente, no actúa sobre el actor.
  Con R-A (`Loading` cortado) la lista podría sobrar si no usa `StartLoadingSequencer`; sin probar.
- **S45, cabos sueltos:** clases de evento sin sonda (`CameraSetFollowTarget`, `CameraParameterPreset`,
  `CameraEffectComponent`, `SubtitleGroup`, `ChangeFocusActor`), el modo cinemática y el campo de cámara de S-D, que no
  refleja la cámara de las escenas (`s45-escenas-ajenas.md` §3.2.4, §11).

## Opcionales (cosméticos)

- **Mascota en el hombro del anfitrión** (vista del invitado):
  - el invitado ve la mascota en el hombro del anfitrión cuando en la partida del anfitrión ya está en el suelo;
  - no se arregla con un viaje rápido; se quitó sola más tarde, quizá al hablar con el vendedor de armas;
  - visto el 15-10 en `p8-2inst-s44-activo2`;
  - al usuario no le importa ahora.
- **NPC que miran al vacío** cuando su escena apuntaba al anfitrión neutralizado (S44 H-4). No observado.
