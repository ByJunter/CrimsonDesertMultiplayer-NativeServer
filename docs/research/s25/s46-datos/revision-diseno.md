# S46: revisión adversarial del diseño T1–T6 (16-10)

Revisión de solo lectura, hecha por un agente de diseño (Plan). Resumen fiel de su informe: el original no se pudo
escribir en disco desde esa sesión. El diseño revisado está en `s46-datos/tenderos.md`.

## Veredicto

| Pieza | Veredicto |
|---|---|
| T1 (U-f lejos con lista blanca, por hash exacto `0xA7132561` / `0x978345CA`) | Necesaria; va a la raíz del caso lejos |
| T2 (orden 2005 → 2661 en el anfitrión) | Necesaria; va a la raíz del caso lejos |
| T3 (G1–G4) | **No implementar**: es un síntoma y puede reproducir el daño de p2b |
| T4 (concepto) | Correcto: escena del invitado con la evaluación nativa de `mgr2` |
| T4 (reutilizar `g_armed` cerca) | Peligroso: cambia el significado de unos 10 consumidores validados |
| T5 | Probablemente necesaria cerca (depende de H13); se decide **por etapa**, con un libro de altas y bajas, nunca por llamada |
| T6 | Diagnóstico útil; un 3000 suelto **no** es aborto (hay 3000 nativos por distancia o ángulo) |

Condiciones previas que el diseño original no recoge:
- T1 necesita R0 también lejos: el relevo ya junta dos escenas sobre el mismo NPC, y en p2b hubo 1 s de solape.
- T2 debe retener todo lo que lleve el id de la escena, no solo el 2661, y leer los 2005 compactados o en lote.

## Hallazgos nuevos

- **F1.** El borrado nativo `.xtls 0xE404A10` quita la primera entrada con la clave y mueve la última a su hueco: el orden
  por antigüedad no se conserva. Esto descarta G3 y cualquier razonamiento FIFO.
- **F2.** `0x2369360` llama a `stage->vf[0x120]` / `vf[0x128]` si `[func+0x3B]`: hay un registro a nivel de etapa cuyo
  significado no se conoce. Saltarse el alta (G1) también se salta ese registro.
- **F3.** El dueño del tendero es la escena `funcnpc` que lo genera (`SpawnCharacter`), y el registro de únicos impide dos.
  Si la escena del invitado nace antes, el NPC es suyo, y al vaciarse esa escena el tendero del anfitrión puede
  desaparecer.
- **F4.** Los 2005 se compactan y van en lote (`observer_hold_acks.cpp:227`).
- **F5.** La rama de la otra IA no se puede fusionar: parte de `ef5a6b5` y borraría S42–S45. Solo se portan a mano
  cabeceras puras: el parser de 3000 con su fixture, `shop_troc_logic.h`, y los formatos 2981 (`21+8n`) y 2524
  (`18+11n`).
- **F6.** Stock, recompra y reposición: si viven en el NPC o en la partida del anfitrión, una venta del invitado altera
  la tienda del anfitrión (H5).

## Diseño mínimo propuesto

- **R0, borrado que tiene en cuenta la etapa.** Un `create_mid` en el `call 0xE404A10` de la baja de `0x2369360`, ya bajo
  el cerrojo del actor.
  - Si la entrada propia de la etapa es la primera, no se hace nada.
  - Si no existe, la clave se cambia por un centinela y el borrado no quita nada.
  - Si es otra, se intercambian los dos bloques de 0x30 B antes del borrado nativo.
  
  Sustituye a G1–G4. Actúa en el servidor del anfitrión y en el cliente del invitado (X frente a M).
- **R1, elección por solicitante.** Solo si H4b lo exige. Sobre la copia temporal de la entrada en ShopProbe
  `0x2126A1F`.
- **Carril de tienda cerca** en lugar de T4: un estado `g_shop_lane` con su propio latch de aborto.
  - Lo consultan: U-a, U-a2, la vf48 del tick, U-f (solo `funcnpc` de la lista blanca **con la escena del anfitrión del
    mismo recurso viva**), U2-a, U2-b y M4.
  - No lo consultan: U-e, U-f2, D, el vaciado en P, `check_abort` ni las familias o carreteras.
  - Transiciones síncronas con lejos.
  - Plan B sin riesgo (B4): sin tiendas cerca; el invitado se aleja ≥ 10 celdas.
- **Fases:**
  0. RE: H1–H15.
  1. Solo sondas: el libro P-I, T2 en probe, T1 solo veredicto y la sonda de comercio.
  2. R0 activo, sin tiendas del invitado (regresión).
  3. Lejos.
  4. T5.
  5. Carril cerca.
  6. Shadow.

## Puertas de diseño

- **H1** (¿quién quita las entradas al borrar una etapa?): si es el destructor o el registro de la etapa, R0 se rediseña.
- **H5** (¿almacén por usuario o compartido?): si es compartido, la compra del invitado se bloquea hasta tener un diseño
  de almacén por jugador.
- **H6** (dueño del tendero): si el NPC muere con la escena que lo generó, la regla de la escena del anfitrión viva es
  obligatoria también lejos, en el relevo.

## Aborto

Cualquiera de estos detiene la prueba:
- una entrada huérfana en el libro;
- un 3000 al anfitrión **con** `server check NOT found`;
- dinero, inventario, stock o recompra que cambian en el jugador equivocado;
- un cambio de progreso del anfitrión;
- desaparece el tendero del anfitrión;
- cualquier `ABORT` de U-1 o U2;
- un `dup_skip` del anfitrión con el carril activo;
- un bucle de 2661/2810;
- el atasco S14;
- cualquier desviación de los resúmenes validados frente a la línea base.
