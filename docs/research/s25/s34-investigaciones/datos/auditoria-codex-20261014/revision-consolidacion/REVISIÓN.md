# Revisión independiente de la consolidación S34

Fecha: 2026-10-14. Revisión de `consolidacion/seccion-1.md`, `seccion-2.md`, `seccion-3.md`, `seccion-6-7.md` y del preámbulo `AUDITORIA-borrador.md`. Se leyeron los informes finalizados A1/A2/A3 y las fuentes necesarias. No se han leído las carpetas ni resultados de A4/A5. No se modificaron borradores, código, juego, partidas ni configuración; no se compiló ni ejecutó juego/peer/host.

## Dictamen de revisión

La consolidación recoge adecuadamente los hallazgos principales y los límites de A1/A2/A3. Antes de cerrar el texto corregiría dos sobreafirmaciones verificables (prohibición de hook en el thunk Logout y ausencia de CC propio del actor jerárquico), y precisaría el alcance de limpieza, postorden y contraejemplos de generación. No encontré una incompatibilidad nueva en los recuentos consolidados; no se repitieron los parsers grandes de captura.

## Correcciones concretas

### 1. §7: no convertir ausencia de pdata en prohibición de hook

Texto revisado: `0x2068340` es un trampolín sin pdata propia, «no ... proponer hook ahí».

**Corrección necesaria.** El thunk no tiene cuerpo semántico legible en esos cinco bytes, pero eso no acredita que sea inenganchable. Ya hay un mid O en él. Código exacto d835806 `src/mod/session2/user2_stages.cpp:78` registra `{0x2068340, 5, kOBytes,...}`; `:1026–1027` le asigna `on_logout`; `:3021–3024` instala los puntos mediante `p8_install_mid`. `:3012–3019` exige verificar la firma de limpieza **antes** de que O parchee el thunk. La frase actual se contradice con ese diseño existente.

Sustituir por: «Está en .rsrc y es un thunk E9 hacia 0xE0D9AF0, sin entrada pdata propia; esos bytes no son el cuerpo legible de Logout. Ya existe el mid O: reutilizarlo y verificar su contrato, sin un segundo parche. La semántica del resultado y el contexto seguro de llamada requieren evidencia separada».

Mi lector independiente del PE confirma timestamp `0x6AB28F00`, base `0x140000000`, offset físico `0x2067740`, bytes `E9 AB 17 07 0C` en .rsrc. Esto no pretende revalidar todos los destinos de salto ni autorizar ningún hook nuevo.

### 2. §2 y §6: el caso B0100578 no prueba que carezca de CC propio

Texto revisado: «un actor sin CC propio puede leer la capa de su padre», con referencia al cierre C.

**Corregir el alcance de la prueba.** El cierre sí demuestra que otro actor (`B0100578`, con +AC=0) puede consultar por jerarquía la capa de `B01004BF`. Los datos revisados no demuestran que `B0100578` carezca de su propio CharacterControl. Mi lector DMP independiente encuentra:

- clave `B0100578` en offset `0xB66B2`;
- `[actor+0x68] = 0x3F3DBA8B400`, offset `0xB66BA`;
- el slot CC `[components+0x40]`, VA `0x3F3DBA8B440`, **no está incluido** en las regiones del DMP;
- tampoco está el slot de jerarquía +0x1B8, aunque el objeto jerárquico usado por la pila sí está capturado y A1 lo vincula por su backlink.

La línea C/G:7063 indica AI/cerebro/estado, no una lectura fiable de CC. C/G:7058 es precisamente la interpretación errónea de RSI que A1 corrige. No usarla para deducir ausencia de CC.

Sustituir por: «NoCc no acredita ausencia ni seguridad. El cierre C demuestra una dependencia de otro actor por jerarquía, incluso con +AC=0; comprobar la ausencia de CC propio no sustituiría comprobar las dependencias».

La parte principal del hallazgo **sí está confirmada**: `child_hold.cpp:62–69` devuelve nullopt por actor/componente/state ilegible o backlink incorrecto; `:146–149`, `:214–218` y `:314–317` clasifican nullopt como NoCc. Es un fallo del contrato de clasificación aunque ningún crash concreto se atribuya todavía a esa rama.

### 3. §6 problema 4: limpieza limitada no implica fracaso en todos los casos

Texto revisado: «Una limpieza de64 por llamada no termina por definición si el invitado ya no emite y quedan actores».

**Precisar.** Con 64 o menos y una pasada exitosa sí puede terminar; también puede existir otra retirada nativa. Lo probado es que el plan propuesto no garantiza completar el resto si hay más de 64 o no se progresa y no llega otro Troc.

Sustituir por: «La limpieza de hasta 64 actores por llamada no garantiza terminar si quedan más actores, la pasada no progresa o se interrumpe y no llega otro Troc del invitado. Hace falta una continuación verificable en contexto seguro del servidor».

Fuentes: `user2_stages.cpp:1681–1688` limita la selección; `:1692–1709` puede saltar toda la pasada por SAM/ModLock; `:1982` llama una sola vez en logout; `:3476`, `:3491–3500` dependen del flujo de Troc. La propuesta de viajeros `3-viajeros.md` §4.6 dice expresamente siguiente Troc. El censo de 91/81 anunciados sin baja es evidencia de capacidad insuficiente plausible, no un censo exacto SAM ni prueba de fuga observada.

La fila equivalente de seccion-3 ya dice «[F] como garantía» y está bien limitada.

### 4. §2: localizar correctamente el problema de postorden

La recomendación de hojas a raíz es correcta, pero el texto no debe implicar que **la contención local actual** carece de ese orden.

`npc_activation_logic.h:393–414` añade un actor sólo después de que su padre esté en Family (`out.contains(a.parent)`); por construcción, el orden obtenido es topológico para las aristas seleccionadas. `user2_stages.cpp:1937–1945` lo recorre a la inversa: allí sí se intenta hijos antes que padres. Esto sigue siendo cierto para `B0100473→474→475` mientras los vínculos seleccionados sean correctos.

El contraejemplo demostrado afecta a las **dos pasadas propuestas para ctl_cleanup** por I2 §3.8 (`2-familias-npc.md:283–285`), porque «miembro» incluye padre intermedio y nieto, sin orden entre ellos.

Sustituir por: «Conservar el orden inverso de family_into que ya usa la contención; llevar ese orden al drenaje/logout en lugar de dos pasadas miembros/raíces. Definir además qué ocurre si falla la retirada de un descendiente antes de intentar la de su padre».

Este último punto sigue abierto en el código actual: el bucle de retirada continúa tras resultados distintos de 0/1 salvo el resultado3 (`user2_stages.cpp:1947–1962`). No se afirma que esto haya causado un cierre ni que Logout nativo no limpie dependencias: es un contrato pendiente para retirada parcial.

### 5. §2 y §6: generación del padre, contraejemplo válido pero no incidente observado

El hallazgo estático es correcto. `Actor` tiene `parent` y `birth` propio, sin `parent_birth` (`npc_activation_logic.h:50–60`). `family_into:409` une por clave de padre y época; no compara generación del nodo ni nacimiento del padre. `npc_add_member` copia época/generación (`user2_stages.cpp:3231–3237`), pero eso no vincula la relación a un birth concreto y family_into tampoco compara esa generation.

Mantener la formulación condicional: «si sobrevive un registro hijo de una encarnación previa y la clave del padre se reutiliza dentro de la época, el selector puede incorporarlo a la raíz nueva». El modelo lógico reproduce la insuficiencia del selector. No hay en estos informes una pareja de captura que demuestre que tal reutilización ocurrió en S34 ni su cadencia. No presentarlo como causa adicional comprobada de los cierres.

Parent_birth/root_birth o un token equivalente son requisitos propuestos, no la única representación posible. Para hijos tardíos que no tuvieron nacimiento rastreado, debe definirse cómo se obtiene ese vínculo fiable; inventar un token local nuevo al observar el 2005 no prueba el nacimiento nativo original.

### 6. §3: llamar Req a la métrica verificada

La fila «IA rápida, pero sin locomoción lejos» ya aclara que no demuestra acción aplicada. Cambiar el rótulo a «Emisión de Req rápida, sin locomoción observada lejos» para que el veredicto [V] no parezca certificar IA correcta. A3 mide alta→primera Req examinada, no el resultado de aplicación.

Conservar las limitaciones explícitas ya incorporadas: 0/82 significa cero movimientos registrados, con sólo78 claves con posiciones; control73/76 sólo tipos3/6 y76/79 incluyendo tipo5; C utiliza captura del host que continúa tras el cierre. Si se resume esa cifra en el ejecutivo, trasladar también el límite de cobertura.

### 7. Política común de unidad familiar: no mezclar integridad con igualdad universal de controlador

§2 admite conservar tipo7 en host **condicionado** a contrato de publicación y dependencias probado; A3 rechaza por ahora activar raíces de caravanas dejando tipo7 host sin ese contrato. Eso es compatible.

Evitar que el ejecutivo convierta «unidad íntegra/coherente» en «todos los miembros siempre tienen el mismo controlador» y a la vez admita la excepción anterior. El requisito común es política completa por unidad, dependencias seguras y ausencia de asignación parcial por error. La homogeneidad de controlador puede ser requisito conservador de la primera cohorte; debe presentarse como esa decisión. No existe aún prueba de seguridad de la excepción para una nueva cohorte.

La reserva anticipada también debe tener alcance explícito: reserva del subárbol conocido antes de asignar; los hijos tardíos/jerárquicos fuera de ese preexamen necesitan una política posterior. «Familia completa antes del mid» no puede convertirse en contrato universal cuando la propia auditoría prueba `B0100578` tardío.

## Hallazgos nuevos importantes confirmados, sin corrección de fondo

- **NoCc ambiguo:** comprobado directamente como arriba; separar lectura indeterminada, ausencia demostrada y componente presente.
- **cleanup_one no estricto por defecto:** `user2_stages.cpp:1643` default `expected_parent=UINT32_MAX`; `:1652` establece local=false; `:1660–1661` pasa ese booleano a logout_result; `npc_activation_logic.h:121–124` convierte error desconocido en Done cuando strict=false. La referencia precisa de local es1652, no1653. Además, `:1627–1637` muta estados de otros subsistemas y continúa la excepción nativa. Reutilizarlo literalmente contradice el aislamiento prometido en I3; no se arregla ocultando la excepción.
- **Marca +3CD no exclusiva:** comprobado el bytecode `66 C7 80 CC 03 00 00 01 01` en RVA `0x28995E9`, .rsrc, offset físico `0x28989E9`. `0x28981b0.c:334–357` construye el descriptor mediante `278EA70`; `:379–380` puede poner +3B8 desde el personaje; `:836–888` lee AutoSpawnOwnerData, copia +3C8 y escribe +3CC=0x101. Es una vía adicional de construcción/restitución, por lo que la marca no identifica un 2550 actual.
- **Atribución a la hipótesis original:** la nueva seccion-3 lo hace correctamente: «[F] para esa hipótesis», explicando que el autor la etiquetó [H] tras buscar827 exports. Mantener [NV] para que el predicado completo req_key/req_ptr pase en vivo; no inferir ya una asignación errónea ni que los cuatro insectos provengan necesariamente del productor adicional.
- **Separación de ocho Timeout y ocho entregas:** la consolidación conserva bien A2+B0+C6 frente a A2+B1+C5 y la entrega B fuera de TimeoutNPC. No fusionar ambos conjuntos en el ejecutivo.

## Preguntas y verificación final pendiente

Las secciones1–3 responden con límites explícitos a las preguntas de cierres, familias y viajeros. Se mantienen abiertas apropiadamente: alternativa común de protección, capacidad real de tipo6/7 bajo Shadow, mismo Troc universal, predicado nativo de locomoción, procedencia completa, salida de región andando y resultado de misiones para nuevas cohortes.

Antes del cierre final debe comprobarse que el ejecutivo y el orden de implementación distingan **investigación aprobada con cambios** de **diseño literal todavía no implementable**, preserven las pruebas de misión del anfitrión y recojan las respuestas de A4/A5 a sus preguntas específicas. Esas secciones no se revisaron aquí mientras sus autores seguían trabajando.

## Reproducción de esta revisión

Archivo propio `verificar_primarias.py`: lector mínimo independiente de streams5/9/3 del minidump y de cabeceras/secciones del PE. No importa código de los investigadores ni de A1/A2/A3. Produce `verificaciones-primarias.json`. Comando:

```powershell
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '%TEMP%\codex-s34-auditoria-20261014\revision-consolidacion\verificar_primarias.py'
```

Las otras comprobaciones fueron lecturas de las líneas enumeradas de la instantánea `codigo-d835806` y de exports Ghidra. Se inspeccionó también el cuerpo PE de `0x2791B80` por pdata/Capstone; no se generó un export nuevo. Esta revisión no rehace las grandes tablas de capturas ni el unwind de A1.

## Adenda: comprobación pedida del mid O / thunk Logout

`verificar_thunk.py` confirma una sola coincidencia de los cinco bytes `E9 AB 17 07 0C` en todo el archivo PE (RVA `0x2068340`, .rsrc). Se enumeraron en .rsrc los patrones relativos directos E8/E9 rel32, Jcc 0F80..8F rel32 y los JMP/Jcc/LOOP/JRCXZ cortos que pueden alcanzar el bloque de cinco bytes. Resultado: **151 candidatos al primer byte, todos validados como instrucciones CALL por desensamblado desde sus entradas pdata; cero candidatos al interior 0x2068341..0x2068344**. Los detalles y RVAs quedan en `thunk-logout.json`.

No hay pdata propia: entradas vecinas `[2067E40,2067E99)` y `[20685E0,20685F9)`. Por ello este resultado no debe describirse como «búsqueda intrafunción» de Logout: es una búsqueda de ramas relativas directas en .rsrc hacia ese bloque. No enumera destinos indirectos, saltos construidos dinámicamente ni ramas desde .xtls, ni prueba dominancia semántica de Logout. Tampoco valida por sí sola reentrada, ABI, contexto de cerrojos o seguridad del callback existente.

Reproducción:

```powershell
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '%TEMP%\codex-s34-auditoria-20261014\revision-consolidacion\verificar_thunk.py'
```
