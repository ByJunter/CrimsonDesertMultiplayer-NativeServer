# P-IDENTIDAD / C2 — dos fuentes de `characterNo`, no una

Build 1.0.0.2976; revisión independiente de 9-10-2026. Todas las direcciones son RVA, base `0x140000000`. **No se ha implementado ni probado ningún arreglo en el juego.**

Marcas: **[E]** lectura independiente de código/decompilación `.rsrc`; **[E-x]** instrucciones legibles de la copia `.xtls`, contrastadas con capstone; **[V]** captura indicada; **[S]** inferencia, diseño o riesgo pendiente de prueba. Los nombres `cdmp_*` son anotaciones, no evidencia por sí mismos. Las afirmaciones sobre el código C++ del mod describen el diseño vigente, no una medida nueva en vivo.

## Resumen y causa raíz

- **[E + E-x] Se confirma el número fijo, pero se refuta el arreglo previo de cambiar únicamente el lobby.** El lobby lo fija en `0x2B7F66C`; la carga de `ServerStatusActorComponent` vuelve a fijar **independientemente** `*Status+0x70 = 0x7FFFFFFFFFFFFFFF` en `.xtls 0x1042755C–0x1042756B`. No copia el número elegido en el lobby. Fuente: [decompilación de la carga Status](identidad-evidencias/decomp_0x293FE70.txt), [instrucciones de la segunda asignación](identidad-evidencias/capstone_10427558.txt).
- **[E + V] El choque existe dentro de un mismo servidor y cliente.** `FindByCharacterNo` selecciona la primera coincidencia; en C1g el anfitrión `A0100001` y el invitado `A010052C` se publican con `7FFFFFFFFFFFFFFF`. Hace falta normalizar **lobby, solicitud de login y Status** antes de que se publiquen o se consulten las cachés.
- **[E + V] `CharacterStatAck` de login se dirige a la sesión del personaje mediante `ToTarget`, no a los vecinos.** No demuestra un envío erróneo al anfitrión. Si tal envío ocurriera, el manejador sí actualiza el actor por clave y una global sin exigir que sea el jugador local.
- **[E] Nueva dependencia para partidas propias:** esa global, `0x6C8CD98`, es `_currentRestoreCodeVersionXXX` de `CharacterStatusSaveData`. Ya la sobrescribe la carga del invitado en `0x1791B98`, antes del Ack. No es una identidad, pero sí otra suposición de un solo jugador que **C2 no corrige**.
- **[S] Confianza:** alta en la causa de la colisión y en la insuficiencia del cambio de lobby; media en el diseño de normalización, pendiente de demostrar la ausencia de publicación durante la carga y de completar el aislamiento de partidas P9.

## 1. Qué se confirma o se corrige del informe anterior

| Afirmación previa | Resultado independiente |
|---|---|
| `0x2B7F4D0` asigna la constante al cargar lobby | **[E] Confirmada.** Entrada `.rsrc`, 0x37B bytes; `0x2B7F66C movabs rax,7FFFFFFFFFFFFFFF`, `0x2B7F676 mov [rsp+70],rax`; `R8=&[rsp+70]` al llamar `0x2143E70` en `0x2B7F6DB`. |
| El lobby recibe nombre de los datos estáticos | **[E] Confirmada en esta ruta.** `0x389570` → virtual +8 → `0x44FD30`; `Kliff` no nace de un identificador único del invitado. No se ha demostrado que todo nombre imaginable sea solo cosmético. |
| El 3093 valida el número contra el lobby propio | **[E-x] Confirmada.** `0x1042AB45–76`: lista `[lobby+18]`, entradas 0xD0, comparación `[entry]` con `*R14`; error en `0x1042AB7F` si no existe. `0x2940EE0` es el thunk de entrada; no se propone engancharlo. |
| Cambiar solo el 3093 es suficiente | **[E-x] Falso.** Se rechaza si no coincide con el lobby; además no evita la segunda constante en Status. |
| El número del lobby acaba sin más en Status | **[E-x] Refutada.** La carga Status asigna otra vez la constante después de la carga común. Se detalla en §2. |
| Cliente `0x8AEB30` toma la primera coincidencia | **[E] Confirmada con matiz.** `0x8AEC70 cmp [rax],rdi`; si coincide salta a `0x8AEC8B test byte [actor+5E],10`. Es una comprobación de **bit**, no igualdad exacta con 0x10. Si falla no busca otro actor con el mismo número. |
| Servidor `0x2A83F80` toma la primera coincidencia | **[E] Confirmada.** Recorre lista del gestor +B8, compara `*[[actor+68]+20]+70` mediante puntero y salta al primer igual en `0x2A840AC`. La condición adicional de referencia válida no convierte esto en búsqueda por dueño. |
| `EquipSlotLIstCacheServer` usa esa búsqueda | **[E] Confirmada.** `0x27779F0` pasa `*param_3`, recibe actor, toma EquipSlot `[comps+38]`, su serie +18 y lista con `0x20C42A0`. |
| Cachés cliente de equipo dependen de `characterNo` | **[E] Confirmada.** `0xAA7F20` aplica al actor devuelto por `0x8AEB30`; `0xAA8020` obtiene su serie; `0xAA8E40`/`0xAA90D0` gestionan entrada, serie y solicitud. |
| «Los dos resolverán siempre al anfitrión» | **[S] Demasiado fuerte.** La evidencia garantiza primera coincidencia, no que el orden de todos los gestores sea siempre anfitrión primero. Puede resultar actor incorrecto o referencia no válida. |
| «`characterNo` no se guarda en ninguna parte de la partida» | **[E/S] Hay que acotar.** No está entre los nueve campos propios reflejados de `CharacterStatusSaveData`, y la ruta de carga observada lo sintetiza. Eso no prueba ausencia en todos los componentes, referencias heredadas o formatos de guardado. No levantar P6 por esta afirmación. |
| El jugador local depende de `characterNo` | **[E] Refutada para el acceso estudiado.** `0x8B44E0` devuelve el puntero del gestor +50. El número, la clave de actor, la cuenta y el puntero local son identidades distintas. |

Evidencia auditable: [lobby](identidad-evidencias/decomp_0x2B7F4D0.txt), [capstone constante](identidad-evidencias/capstone_2b7f66c.txt), [búsqueda cliente](identidad-evidencias/decomp_0x8AEB30.txt), [servidor](identidad-evidencias/decomp_0x2A83F80.txt), [caché servidor](identidad-evidencias/decomp_0x27779F0.txt), [caché cliente](identidad-evidencias/decomp_0xAA90D0.txt).

## 2. Dónde se escribe `Status+0x70`: es un puntero y hay una segunda constante

**[E] Estructura:** `Status = [[actor+0x68]+0x20]`; `Status+0x70` contiene un **puntero a u64**, no el u64 en línea. El constructor común `0x178F1D0` reserva ese valor e inicializa -1. `+0x78` contiene la cadena de nombre. El lector de red `0x1790FA0` asigna el u64 deserializado al valor apuntado; el empaquetador `0x17908C0` escribe `**(u64**)(Status+0x70)`.

**[E + E-x] Cadena comprobada de la carga del personaje:**

1. `0x2C3E030` procesa login: recarga el lobby y llama `0x2940EE0`.
2. `0x2940EE0 → .xtls 0x1042AAF0` valida el número solicitado en el lobby del dueño.
3. `0x1042ABF3` llama `0x186F570` con el actor-personaje y la ranura. Esta ruta llama a `0x20688A0`, que recorre los componentes y llama su `vf(+0x50)` en **`0x20693E8`**.
4. La vtable `ServerStatusActorComponent`, `0x5B203F0`, tiene +50 = **`0x293FE70`**, thunk de cinco bytes que salta a **`0x10427500`**.
5. El cuerpo llama la carga común `0x1791980`; si falla devuelve error. Si tiene éxito: `0x10427558 mov rax,[rsi+70]`; `0x1042755C movabs rcx,7FFFFFFFFFFFFFFF`; **`0x1042756B mov [rax],rcx`**.
6. Al volver de `0x186F570`, el cuerpo de validación llama **`0x2941020`** en `0x1042AC0C`, que continúa las fases de login de los componentes. Este es un límite `.rsrc` legible después de la carga, útil para comprobar el invariante.

**[S] Consecuencia:** separar solo el lobby hará que 2869/3093 contengan el valor nuevo, pero el personaje cargado aún se publique con `7FFF…`. Podría pasar la validación del lobby y fallar exactamente la finalidad de C2. No se necesita provocar este estado en vivo para identificar la segunda asignación.

**[V] Corrección de offsets de red:** no usar «+0x17 del registro» como regla. En las capturas de esta build, `ActorLoadDataAck` concatena registros `u16 longitud` —excluye esos dos bytes—, `u8 tipo`, cuerpo. El registro de jugador tipo 1 tiene el primer `characterNo` en **registro +0x0C**. Con el registro de usuario de 11 bytes delante, cae en **payload +0x17**. En el ejemplo `Kliff`, la copia dentro del bloque Status está en **registro +0x38**, dependiente del contenido anterior; no generalizarla a otros nombres/variantes. C1g #944 tiene prefijo usuario; #1077 ya empieza por el jugador. Reescribir siempre payload+17 corrompería el segundo formato. Fuente: [capturas.json](identidad-evidencias/capturas.json).

**[E-x] Alcance `.xtls`:** aquí se leyeron bloques cortos concretos, no se reconstruyó toda la protección ni se inventaron límites de función. Ningún punto `.xtls`, ni su thunk de cinco bytes, forma parte del diseño de hook.

## 3. `CharacterStatAck`: destinatario y efecto real

**[E] Escritor identificado de forma independiente:** `0x2990270`; fija opcode `0x936` = 2358, slot FF, y serializa clave de actor, variante, nombre, dos bytes, characterNo, estadísticas y último u32. El único llamador recuperado por Ghidra está en `0x28F355D`, dentro de `0x28F31F0`.

**[E] Destinatario de la ruta de login:** inmediatamente antes construye el buscador mediante `0x2A757A0(actor)`. Esta función escribe vtable `ToTargetSendTargetFinder` y obtiene **`actor->vf(+0xE8)`**, la sesión destino. `0x2A757F0` añade solo esa sesión al vector. El escritor no inventa un `ToNear`: usa el buscador recibido. Es evidencia de esta ruta, no demostración exhaustiva sobre callers escondidos o indirectos en `.xtls`.

**[V] Capturas reanalizadas, numeración de paquetes desde 1 y tiempo desde el primer registro:**

| Archivo | Paquete / tiempo | Dato |
|---|---|---|
| `C:\Emulador CD\captures\cdmp_packets_20261003-112627.bin` | #8 / 0,242716 s; #10 / 3,436974 s | Lobby 2869 y login 3093: `7FFFFFFFFFFFFFFF`, ranura 1. |
| Mismo | #49 / 3,754098 s | Único 2358, actor `A0100001`, Kliff, `7FFF…`, último u32 15. |
| `D:\cdmp-grabaciones\e3.bin` | #6 / 0,261152 s; #8 / 0,261213 s; #62 / 0,367813 s | Lobby, 3093 y único 2358: `7FFF…`, ranura 108; 2358 para `A01002D5`, último u32 5. |
| `D:\cdmp-grabaciones\c1g.bin` | #6 / 0,239684 s; #8 / 0,239691 s; #62 / 0,321046 s | Misma secuencia para `A010052C`, último u32 5. |
| C1g | #944 / 2,115022 s; #945 / 2,115061 s | La sesión 2 recibe su actor `A010052C` y el anfitrión `A0100001`: ambos tienen `7FFF…` en cabecera y bloque Status. |
| C1g | #1077 / 2,119598 s | Nueva carga de `A010052C` sin prefijo de usuario; confirma la advertencia de offsets. |

**[V] Límites de esta comprobación:** las tres capturas tienen cero `ScopeSenderAck` 2042 y cero `ClientCacheReq` 2674; no había un contenedor que ocultara otros 2358 ni una solicitud de caché que permitiera demostrar un error de equipo de C1g. El túnel C1g observa la sesión 2, no todos los envíos a la sesión 1. No se afirma haber visto en C1g un 2358 del invitado recibido por el anfitrión.

**[E] Si el anfitrión recibiera un 2358 del invitado:** `0xB73D70` resuelve la **clave de actor** del payload, sin buscar por `characterNo`. Si el actor y sus datos estáticos son válidos, cambia nombre, estadísticas y `*Status+70` del actor encontrado; `0xB742B8` escribe el número y **`0xB74302` escribe global `0x6C8CD98`**, sin prueba de jugador local. No sustituye directamente `ClientActorManager+50`. Por ello reenviar este Ack a todos sería incorrecto, aunque los characterNo ya sean distintos.

### La global no es el número: versión de restauración compartida

**[E] Nueva identificación:** RTTI de `0x179E680` = `CharacterStatusSaveData`; registro de metadatos `0x187C490` nombra **`_currentRestoreCodeVersionXXX`**, etiqueta `ExecuteRestoreCodeVersion`, tipo uint32, **offset +0x60**. `0x1791B94` lee `[objeto+60]`; `0x1791B98` lo copia a `0x6C8CD98`. El escritor de 2358 vuelve a leer esa global en `0x28F3430`. [Metadatos](identidad-evidencias/decomp_0x187C490.txt), [escritura capstone](identidad-evidencias/capstone_1791b77.txt).

**[E] Lectores relevantes:** carga de inventario `0x212AED6` (rama por versión <6); carga de mercenarios `0x21479F8`, `0x214800E`, `0x2148A0D`; `MoveFieldComplete` `0x2BC7726/0x2BC7806/0x2BC78FB`. Ghidra encontró 17 referencias `.rsrc`; no constituye barrido exhaustivo de `.xtls`.

**[V/S] Los valores 15 y 5 sí aparecen en las capturas de partidas distintas, pero no se midió en esta revisión la global del anfitrión antes/después del login invitado.** El código demuestra la escritura compartida; las consecuencias concretas de migración duplicada, omitida o guardado incorrecto siguen siendo riesgo. Este dato impide prometer que «cargar su propia partida en Shadow evita los choques».

**[S] Medida prudente P9:** no restaurar a ciegas la global del anfitrión inmediatamente después de Status: los componentes posteriores del invitado leen su versión. Hacen falta un contexto por perfil/carga y acceso contextual a los lectores posteriores; una simple sustitución temporal de una global también es insegura frente a reentrada/concurrencia. Mientras no exista ese diseño verificado, rechazar antes de cargar una partida con versión incompatible. C2 puede probarse con perfiles de prueba compatibles y con el guardián activo.

## 4. Otras identidades: qué se separa y qué no

| Identidad | Evidencia y tratamiento |
|---|---|
| Cuenta del 2490 | **[V]** Anfitrión 0 en captura 112627; guion invitado 1 en e3/C1g. **[S]** Con cuatro usuarios, no repetir cuenta sintética 1: asignación por conexión autenticada y perfil, nunca aceptar la identidad declarada por el paquete como autoridad. Reutilizar cuenta puede entrar en relogin; no confundir con el id de cola de sesión, que debe seguir 0 por D-39. |
| Nombre y apariencia | **[E/V]** Ruta lobby usa datos estáticos y ambas capturas dicen Kliff; Status incluye variante, facción y nombre. **[S]** Nombres iguales no son prueba de choque funcional. Apariencia clonada por cargar la misma ranura se resuelve con P9, no cambiando arbitrariamente CharacterKey. Etiquetas visuales de comunidad deben mantenerse aparte de identidad nativa. |
| Ranura de guardado | **[V]** 3093 #8 en e3/C1g solicita 108, mientras referencia anfitrión usó 1. Ya hay igualdad de characterNo con ranuras distintas. **[S]** P8 por sí solo redirige la petición al anfitrión; «108» es una ranura de su servidor hasta que P9 entregue los datos del invitado. No basta tener otra partida en Shadow. |
| Claves de actor y dueño | **[V]** C1g muestra `9010052B/A010052C`, frente a `90100000/A0100001`; no colisionan. **[S]** Mantener las claves nativas del SAM, validar generación y no reutilizar punteros como identidad entre PCs. |
| `itemNo` y referencias de inventario | **[S]** El informe previo documenta coincidencias, pero esta revisión no ha reconstruido todos sus ámbitos ni probado una tabla global por itemNo. No afirmar que son todos globales ni remapear números por fuerza. La identidad de inventario de la capa del mod debe ser `(perfil/characterNo, itemNo)`; un remapeo nativo exigiría inventariar todas las referencias y reversión al guardar. Bloquear intercambio/persistencia entre perfiles hasta medirlo. |
| Dueño de objetos nuevos | **[E]** `0x2AE1010` construye tipo de dueño 2 y copia `*Status+70`. **[S]** El número separado cambia esos dueños desde ese momento; los objetos ya restaurados podrían contener referencias anteriores. No prometer que C2 migra objetos existentes. |
| Mercenario/caballo | **[V]** Sus actores tienen claves separadas en pruebas; **[S]** no basta para garantizar unicidad de sus identificadores persistentes internos, contratos o selección por usuario. Necesitan dueño/ranura por perfil y la revisión específica de compañeros. No atribuir el rechazo del mercenario a characterNo sin evidencia. |
| «Jugador local» | **[E]** `0x8B44E0` usa gestor+50; el Ack observado trabaja por clave. **[S]** cada cliente P8 debe recibir sus mensajes privados y su usuario, mientras ve las claves de los demás como remotas. Renombrar characterNo no convierte un remoto en local. |
| Versión de restauración | **[E]** Global compartida descrita arriba; aislamiento pendiente independiente de C2. |

## 5. Diseño de raíz propuesto, sin implementarlo

### 5.1 Invariante de identidad

**[S]** Una tabla del mod, por mundo y perfil admitido, debe ligar `{conexión autenticada, época, usuario nativo, clave/generación, personaje nativo, characterNo}`. El anfitrión conserva `7FFFFFFFFFFFFFFF`. Para la prueba se pueden reservar `7FFFFFFFFFFFF002`, `…F003`, `…F004`; son **valores propuestos del mod**, no identificadores oficiales. Excluir -1, 0, el valor anfitrión y `…F001` de la antigua marioneta, y comprobar ausencia en actores/lobbies antes de usarlos. No identificar al invitado con «todo el que no es el anfitrión»: debe ser una entrada exacta del registro de sesiones admitidas.

**[S]** Para reconexión conservar la identidad del mismo perfil hasta que C4 confirme la retirada completa del actor anterior; rechazar doble login. No entregar F002 a una persona/partida distinta mientras sobrevivan actores, mensajes o cachés de la primera. Para sesiones futuras, la identidad necesita un registro de perfiles estable o invalidación explícita de su caché; un hash truncado del SteamID sin control de colisiones no es suficiente.

### 5.2 Dos puntos de normalización, ambos entradas `.rsrc`

1. **[S] Lobby:** detour de entrada **`0x2B7F4D0`**, llamar al original, solo con resultado correcto y dueño registrado como invitado ajustar la entrada de su lobby al número asignado. Tomar el lock nativo del dueño antes de recorrer/modificar y soltarlo antes de llamar otras rutas. Validar `comp+8`, tipo/vtable y **`lista=*(comp+18)`, datos `*(lista+0)`, número `u32(lista+8)`**, stride 0xD0 y número esperado. No confundir `comp+20`, que recibe generateNo, con la longitud. Si hay cero o varias entradas ambiguas, no adivinar. Se instala antes del 2490 y opera también en la recarga del 3093.
2. **[S] Status:** detour de entrada **`0x20688A0`**, envolver la carga original completa y, **antes de devolver éxito**, normalizar `*Status+70` cuando el actor cargado es el personaje de ese invitado y no un compañero/NPC. Se exige relación actor→usuario→sesión y retorno de carga correcto; no usar la ranura para identificar al dueño. Esto deja que la carga nativa haga sus migraciones, pero sustituye la segunda constante antes de continuar el login. Su interacción con migraciones/objetos de §4 debe registrarse, no darse por resuelta.
3. **[S] Comprobación y cierre seguro:** en entrada **`0x2941020`**, comprobar que lobby, número asignado y Status ya coinciden antes de las fases posteriores; en entrada **`0x28F31F0`** y empaquetador **`0x17908C0`**, instrumentación de solo lectura verifica que nunca se publica una identidad provisional. Si la medida descubre publicación dentro de la carga original, este diseño post-retorno se **rechaza**: se necesitará normalización por componente en un envoltorio `.rsrc` antes de sus consumidores, no un parche tardío del paquete.

**[E] Entradas verificadas por Ghidra y capstone:** `2B7F4D0` comienza `48 89 5C 24 08 48 89 6C 24 10 56 57 41 54 41 56`; `20688A0` comienza `48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54`; `2941020` comienza `48 89 5C 24 08 44 89 4C 24 20 4C 89 44 24 18 55`. Estos bytes no constituyen un instalador ni autorizan copiar un número de bytes sin decodificar instrucciones. Validar build y cabecera completa antes de activar.

**[S] Fallo seguro:** si falla una cabecera, el guardián, una relación de dueño, la unicidad o un resultado nativo, la opción D no admite el login. No volver silenciosamente al número compartido, no publicar el personaje y no soltar memoria o vaciar el campo a mano. Si ya existe actor, iniciar la salida nativa de C4 y conservar P6 hasta finalizar la prueba; no continuar con una identidad parcialmente cambiada. Evitar inventar un código de error nativo: usar la negativa de admisión del mod y la ruta de cierre estudiada en salida.

**[S] Límite adicional del punto post-retorno:** no basta demostrar que nadie publica antes de volver de `20688A0`. Otro componente puede consumir el número provisional **durante** la carga para construir una referencia o un dueño, por ejemplo por `2AE1010`; ese dato sobreviviría a cambiar Status al final. La primera prueba debe observar `20693EB` después de Status y cada lector de identidad que alcance el resto de la carga. Si hay un consumo persistente previo, el post-retorno no es el arreglo completo: hay que normalizar antes de ese consumidor mediante una entrada `.rsrc` adecuada o adaptar su dato con ámbito de perfil. El inventario exhaustivo de esos consumidores sigue abierto.

### 5.3 Qué ve cada cliente y qué cambia en el guion

**[S]** 2869 del invitado publica el número nuevo y 3093 debe devolverlo; el guion ya dispone de `CHARACTER_NO`, pero la prueba debe comprobar sus bytes. P8 debe copiar el valor recibido de su lobby, no conservar el del servidor local. Todos los clientes reciben el mismo `characterNo` para ese personaje en sus registros; no existe una traducción por observador. El invitado recibe su 2358 privado; los vecinos reciben los registros públicos. Los buscadores nativos de caché deben resolver entonces a un solo actor.

**[S]** La opción de solo reescribir `ActorLoadDataAck` en el cliente queda descartada como arreglo de raíz: no arregla la validación del login, el Status del servidor ni la caché servida por `0x27779F0`, y además hay varios formatos/offsets de registro.

### 5.4 Caché, guardado y guardián

**[E]** `0xAA95E0` crea una ruta con patrón `clientCache/%#/%#/%#.cache` y números de la entrada; `0xAA90D0` puede leerla, invalidarla y borrar su fichero antes de pedir datos. Separar characterNo separa el componente de clave de esa caché; **no es una prueba de que el nombre físico completo sea solo characterNo** ni de su ubicación absoluta. [Constructor de ruta](identidad-evidencias/decomp_0xAA95E0.txt).

**[S]** No borrar la caché completa del anfitrión ni considerar inofensivo reutilizar un número invitado con la misma serie de otro perfil. Instrumentar la ruta resuelta y limitar invalidaciones a entradas creadas para ese perfil/época; una serie igual de un perfil distinto puede reutilizar datos equivocados sin solicitar nada al servidor.

**[S, lectura de código]** El guardián actual se configura para `%LOCALAPPDATA%\Pearl Abyss\CD\save` (`src/mod/session2/save_hooks.cpp:231–249`), y `Guard::block_path/block_create` solo bloquean bajo ese directorio (`src/common/save_guard.cpp:78–92`). Por tanto **no garantiza protección de la caché** si esta queda fuera. La ubicación real debe registrarse en la prueba. No extender la negación a toda `clientCache` sin estudiar la ruta de fallos del cargador.

**[E/S]** Los nueve campos propios de `CharacterStatusSaveData` examinados son `_characterKey`, `_factionKey`, `_level`, `_experience`, `_remainExperience`, `_remainSkillPoint`, `_currentHp`, `_currentMp`, `_currentRestoreCodeVersionXXX`; no incluyen characterNo. No demuestra que el guardado completo sea independiente del cambio: `0x2AE1010` ya usa el número como dueño de objetos. **Mantener P6 y no habilitar guardar** hasta que P9 compruebe ida/vuelta de propietarios e identificadores. Una función que no serializa characterNo directamente no elimina sus efectos secundarios.

## 6. Una prueba C2 reproducible, con criterio de éxito y de parada

**[S] Preparación de la prueba futura:** build comprobada, P6/K1 activos, dos perfiles de prueba con versión de restauración compatible o aislamiento ya resuelto, guion C1g con primera acción en aparcamiento, P5, C4 disponible para retirar/reconectar. No se ha ejecutado durante esta investigación.

**[S] Secuencia de una sesión:**

1. Registrar número, clave, serie de equipo y versión de restauración del anfitrión antes del 2490; admitir invitado y asignar F002.
2. Registrar cada carga de lobby y cada retorno de `20688A0`, después `2941020`: `C2 identity user=... epoch=... lobby=F002 status=F002 expected=F002`. El anfitrión permanece 7FFF.
3. Capturar 2869, 3093, 2358 y todos los registros de jugador de 2005, con offsets analizados por formato. Esperado: todas las copias del invitado F002; nunca una publicación provisional 7FFF; 2358 solo a su sesión.
4. Provocar una solicitud nativa `cacheEquipSlotList` de cada identidad mediante el guion/cliente y registrar `27779F0 → 2A83F80` y `AA7F20 → 8AEB30`: el valor anfitrión resuelve su clave y F002 la del invitado, incluso si el equipo visual inicial es idéntico. Comparar antes/después del equipo anfitrión y ruta/serie de caché. No basta «no se ve cambiado».
5. Salida nativa y reconexión del **mismo** perfil: nueva clave/generación de actor, misma identidad de perfil solo tras eliminar la anterior; repetir búsqueda de caché. Después, tres invitados en una sesión de aforo 4: F002/F003/F004 únicos, sin mensajes de la época anterior. Este subpaso necesita la extensión de sesiones múltiples, no existe por atribuir tres números.

**[S] Éxito:** igualdad lobby=Req=Status=publicación por perfil; unicidad en ambos gestores; destinatarios privados correctos; equipo/caché/versión del anfitrión sin variaciones ajenas; guardián activo y ninguna escritura de partida permitida. **Parada inmediata:** primer número provisional publicado, actor incorrecto de caché, cambio inesperado de la versión global, perfil ambiguo, cabecera distinta, referencia inválida, fallo de guardián o error nativo. Congelar admisión/relevo de ese invitado y salida C4; nunca arreglar el resto de paquetes «sobre la marcha».

**[S] Observación mínima si solo se instrumenta primero:** entradas `.rsrc` 20688A0/2941020, `28F31F0`, `17908C0` y mid-hooks **de lectura** en `20693EB` (componente R15 y resultado RAX), `1791B98` (valor de versión), `B74302` (global cliente). La llamada de Status se deja íntegra; no enganchar `.xtls`.

## 7. Límites, material y revisión

- **[E/V]** Evidencia local conservada en [identidad-evidencias](identidad-evidencias/): consultas Ghidra con tiempos medidos, desensamblado capstone y JSON de capturas. Se usó exclusivamente `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`; Python `-I -B`; no se leyó el EXE del juego.
- **[S]** No se auditó exhaustivamente cada acceso a itemNo, cada formato heredado de guardado, la migración completa de compañeros ni todos los callers virtualizados. La unicidad de cuenta, actorKey y characterNo no prueba por sí sola la seguridad de P9.
- **[S]** Los archivos `capstone_2b7f650.txt`, `capstone_28f34c0.txt` y `capstone_b742ae.txt` son intentos descartados por empezar fuera de un límite válido de instrucción. **No son evidencia usada.** Se conservan: la revisión automática rechazó retirarlos. Las lecturas válidas son `capstone_2b7f66c.txt`, `capstone_28f355d.txt` y `capstone_b742b0.txt`.
- **[S]** La decompilación que muestra cuerpo tras `0x293FE70` no convierte el thunk en una función `.rsrc` de carga. La escritura relevante se ha etiquetado **[E-x]** y verificado directamente sobre la copia.

### Revisión

Revisión cruzada realizada por el agente **P-RELOJ**, distinto del autor, el 2026-10-09. Se hicieron consultas nuevas al Ghidra vivo, se desensambló de nuevo la copia con capstone y se volvieron a procesar completas las tres capturas, sin usar el JSON del autor como resultado esperado. Evidencia independiente: [revision-reloj](identidad-evidencias/revision-reloj/).

1. **[E-x + V] Intento de refutación: «Status podría copiar el número del lobby». No se sostiene.** Tras volver de `0x1791980`, el tramo `0x10427558–6B` carga el puntero `Status+70`, carga un inmediato literal `7FFFFFFFFFFFFFFF` y lo escribe. No hay lectura del lobby ni del 3093 en ese tramo. La nueva lectura de C1g encuentra el mismo número en los jugadores de #944 (registro a payload+11), #945 (registro a payload+6189) y #1077 (registro a payload+0); reproduce la colisión y la advertencia de offsets. Esto confirma que normalizar solo el lobby es insuficiente. Fuentes: [bytes independientes](identidad-evidencias/revision-reloj/capstone.txt) y [capturas reanalizadas](identidad-evidencias/revision-reloj/capturas.json).
2. **[E + V] Intento de refutación: «2358 podría difundirse a los vecinos en la ruta examinada». No se sostiene para esta llamada.** En `0x28F34F2` se construye `ToTarget` con el actor `R14`; su resultado RAX se pasa en RDX al escritor de 2358 en `0x28F355D`. El constructor `0x2A757A0` obtiene la sesión mediante `vf+E8`, y `0x2A757F0` añade esa sesión. Capstone y Ghidra coinciden; las nuevas lecturas de captura encuentran un 2358 propio por login. Se conserva el límite del informe: no se demuestra ausencia de callers ocultos ni observación completa de la sesión 1 desde el túnel de sesión 2. [Desensamblado del destino](identidad-evidencias/revision-reloj/destino2358.txt), [constructor](identidad-evidencias/revision-reloj/decomp_0x2A757A0.txt).
3. **[E + V] Intento de refutación: «6C8CD98 podría ser otro contador o los valores 15/5 un error de offset». No se sostiene.** La nueva consulta de `0x187C490` enlaza expresamente `CharacterStatusSaveData`, `_currentRestoreCodeVersionXXX`, uint32 y offset `0x60`. Capstone confirma `0x1791B94` leyendo `[R11+60]` y `0x1791B98` escribiendo `0x6C8CD98`; `0xB74302` escribe la misma dirección desde el Ack. El reparse independiente da **15** en 112627 #49 a 3,754098 s, y **5** en C1g #62 a 0,321046 s y B3 #62 a 0,367813 s. **Son muestras de partidas/sesiones distintas, no una medida de que la global del anfitrión de C1g haya pasado de 15 a 5.** [Metadatos](identidad-evidencias/revision-reloj/decomp_0x187C490.txt), [capturas](identidad-evidencias/revision-reloj/capturas.json).

**[S] Corrección de alcance del diseño:** el post-retorno de `0x20688A0` sigue siendo un **candidato condicionado**, no una ubicación ya aprobada para normalizar. La función carga muchos componentes que pueden leer el número provisional, no solo publicarlo. La prueba de §6 debe registrar también los consumidores relevantes dentro de esa carga, en particular propietarios de objetos/cachés; si alguno usa el número antes del retorno, habrá que normalizar antes de ese consumidor mediante un envoltorio `.rsrc`. Un 2358 correcto al final no basta para validar todo el recorrido. No se propone un hook de Status en su thunk ni en `.xtls`.

**[E/S] Precisión adicional de estructura:** al revisar `0x2B7F4D0` se detectó que `[lobbyComp+0x20]` recibe el `generateNo` de `LobbySaveData`; por tanto no puede usarse como contador de un vector inline que empezase en `comp+18`. El diseño debe respetar la indirección indicada en el informe: `comp+18` es el **puntero al contenedor**; validar sus campos reales antes de recorrerlo. Esta revisión no ha ejecutado ninguna mutación para probar el diseño.
