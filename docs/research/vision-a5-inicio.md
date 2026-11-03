# Visión A5: empezar en el inicio de la exploración libre

Investigación terminada y revisada independientemente, 2026-10-08. Diez afirmaciones
estáticas confirmadas con alcance local; precisiones y reproducción de datos en §8.
Build **1.0.0.2976**, PE `0x6AB28F00`, base
`0x140000000`; todas las direcciones son RVA. No se ha ejecutado el juego ni leído una
partida completa. Los scripts/volcados propios se conservan en
`C:\Emulador CD\build\research\vision\a5\`.

Leyenda: **[V]** captura/vivo anterior; **[E]** desensamblado `.rsrc`; **[E-x]**
`.xtls` protegido; **[S]** inferencia o diseño; **[R]** fuente externa sin contraste.
Los datos previamente extraídos se marcan **[S] (tabla)**: su contenido se puede
reproducir, pero su interpretación no equivale a un comportamiento observado.

## 1. Veredicto y frontera que hay que fijar

[S] Una plantilla nativa tomada en el punto elegido es la vía prudente. Copiar una
posición o dar por terminada una misión no basta: el juego conserva misiones,
stages, tutoriales, inventario y datos de campo en componentes diferentes y mixtos
([vision-a3-guardado-mundo.md](vision-a3-guardado-mundo.md), §§2–3).

[S] (tabla) Existe una dependencia concreta que impide definir el comienzo únicamente
por el final de una cinemática: `SkillPoint_Kliff` (subnivel 521) requiere
`CompleteMission(Mission_Intro_Abyss_Tutorial)` según
[pendiente-recompensas-progreso.md](pendiente-recompensas-progreso.md), §2.3.
El lector propio `datos_inicio.py` ha reproducido la condición `0xF642E` y cinco
referencias binarias a ella en el registro 521; **no demuestra que esa misión sea
exactamente la frontera de exploración libre solicitada**.

[S] **Viable como objetivo condicionado a una plantilla certificada y al aislamiento
de A3/A4; todavía no hay un punto de inicio exacto demostrado.** No se puede afirmar
«terminar la misión X y aparecer en las coordenadas Y». La libertad de movimiento
puede anteceder a la habilitación de un sistema de progreso: si el punto real
precede a la misión del Abismo, se conserva su bloqueo de experiencia hasta cumplir
el requisito nativo. No se adelanta esa misión sólo para dar experiencia antes.

[S] Definición operativa para la futura prueba: **primer punto posterior a la
introducción obligatoria en que se puede explorar el campo normal, apartarse del
objetivo principal y recuperar ese estado al recargar**, sin cinemática, transición
o tutorial obligatorio a medio ejecutar. Registrar el instante anterior, el primer
control libre y el primer guardado seguro. Si no se puede guardar exactamente ahí,
documentar la diferencia; no llamarlo cumplimiento literal de «justo en el punto».

| Pregunta | Respuesta actual y evidencia | Riesgo | Prueba futura |
|---|---|---|---|
| Misión y flags exactos | [S] Dos quests introductorias y varias misiones/stages (§2); sin captura marcada de la transición | Saltar requisitos o comenzar tarde | A5-IN1 |
| Inventario inicial | [S] Debe salir de una plantilla nativa; `inventory` enumera contenedores (§2.3) | Objetos ausentes, premios anticipados o duplicados | A5-IN2 |
| Posición/región desbloqueada | [V] Hay posiciones de partidas cargadas, no de la frontera (§2.4) | Aparición en terreno alterado o sin navegación | A5-IN1/IN4 |
| Plantilla por jugador | [S] Adaptación coherente de identidad y autoridad mundial, pendiente A3/A4 (§3) | Colisión de identidades y reversión de mundo | A5-IN2/IN3 |
| Historia personal en mundo avanzado | [S] Compatibilidad explícita por contenido (§4) | Dos versiones físicas contradictorias | A5-IN4/IN5 |

## 2. Datos de arranque y misiones

[S] (tabla) Se han localizado `Quest_Intro` (`0x2711`), `Quest_IntroHernand`
(`0xF42BB`), `Mission_Intro_Abyss_Tutorial` (`0xF44AC`),
`Mission_Intro_After_Horse` (`0xF42E4`), `Intro_After_Start` (`0x8081`, secuenciador
`cd_seq_quest_intro_0500_dlc`) e `IntroComplete` (`0xF4252`, expresión
`CompleteQuest(Quest_Intro)`). Los nombres no fijan por sí solos el instante exacto
de libertad. Fuente: `datos_inicio.py` y `datos_inicio.json` en el directorio A5.

[E] `GameStartInfo` tiene un lector en `0x1508D80` con `_fieldInfo`,
`_startQuestInfo`, `_useInstanceSaveSlot` y `_characterSpawnDataMap`. [S] (tabla) El registro
`Main` previamente extraído tiene clave 64 y mapa de aparición vacío; no proporciona
coordenadas de la frontera buscada. [S] (tabla) `MainField_Multi` existe en
`fieldinfo` con clave 2; su nombre **no demuestra un campo cooperativo funcional**.

[E] `NewGameStartReq` (2178, `0x2A1DFB0`) exige payload vacío en `0x2A1E042` y
llama a `0x2B80C00` desde el componente del actor solicitante. No transporta
posición, inventario ni flags de prólogo. La rutina alcanza `0x2B7FDC0`, un thunk
a código protegido; no se propone usarla como API para saltarse la historia.

[V] Las capturas 112627 y 105149 contienen login y dos `MoveFieldAck` cada una, pero
ningún `NewGameStartReq`/`NewGameStartAck`. Son referencias de carga, no una
grabación certificada del comienzo libre. La 105149 recorta paquetes a 1.024 bytes;
la 112627 permite inspeccionar los paquetes completos de misiones. Fuente propia:
`capturas_inicio.py`/`capturas_inicio.json`.

### 2.1 Catálogo reproducible y sus límites

Fuente: tablas ya extraídas en `C:\Emulador CD\build\research\ids\out\gd\`.
`datos_inicio.py` comprueba longitud del índice, anchura de cuenta/clave, límites
de registro y coincidencia entre clave del índice y cuerpo. No importa extractores.
El lector anterior `p_rewards/r_sinfo.py` sólo acepta cuenta u16/clave u32: falla
con `questinfo`/`stageinfo` (cuenta u32), `regioninfo`/`inventory` (clave u16) y
`gamestartinfo` (cuenta/clave u8). **Eso no demuestra corrupción de esas tablas.**
[S] Método y datos reproducibles con el script propio.

Hashes SHA-256 de cabecera/cuerpo, claves, offsets y cadenas seleccionadas están
en [vision-a5-inicio-datos.json](data/vision-a5-inicio-datos.json). No se
reextrajeron ni revalidaron archivos empaquetados del juego. La correspondencia
entre estas copias y el estado cargado sigue pendiente; las ausencias se limitan
a estos archivos/filtros, no a todo el contenido del juego.

| Tabla y clave | Nombre/contenido leído | Interpretación prudente [S] |
|---|---|---|
| `questinfo`, `0x2711` / 10001 | `Quest_Intro` | Candidato de progreso del prólogo, no momento libre certificado |
| `questinfo`, `0xF42BB` / 1000123 | `Quest_IntroHernand` | Otra fase introductoria; no fusionarla con la anterior |
| `missioninfo`, `0xF42DD` | `Mission_Intro_Tutorial_I` | Familia de tutorial, con cinco entradas `_0`…`_4` |
| `missioninfo`, `0xF42E0` | `Mission_Intro_MainBattle` | Combate introductorio; evitar plantilla con stage activa |
| `missioninfo`, `0xF44AC` | `Mission_Intro_Abyss_Tutorial` | Requisito de progreso localizado; también hay `_0`, `_1`, `_2` |
| `missioninfo`, `0xF42E4` | `Mission_Intro_After_Horse` | Candidato a observar; el nombre no certifica montura entregada |
| `conditioninfo`, `0xF4252` | `IntroComplete` → `CompleteQuest(Quest_Intro)` | Condición identificable; no un byte global «mundo abierto» localizado |
| `conditioninfo`, `0xF4BE8` | `CompleteQuest(Quest_IntroHernand)` | La otra quest tiene una condición distinta |
| `conditioninfo`, `0xF642E` | `CheckCharacterKey(Kliff) && CompleteMission(Mission_Intro_Abyss_Tutorial)` | Dependencia de experiencia reproducida junto a `sublevelinfo` 521 |
| `stageinfo`, `0xF538E` | `Intro_Abyss_Tutorial` → `cd_seq_quest_intro_0610_dlc` | Stage distinta de la misión homónima |
| `stageinfo`, `0xF44EE` | `Intro_Abyss_Tutorial_III` → `cd_seq_quest_intro_1400_dlc` | Fase separada; no basta borrar la primera |
| `stageinfo`, `0x8081` | `Intro_After_Start` → `cd_seq_quest_intro_0500_dlc` | Candidato de observación, **no** alta recomendada por su nombre |
| `stageinfo`, `0xF4C67` | `Intro_After_Horse` → `cd_seq_quest_intro_0450_dlc` | Otro estado a comparar antes/después |
| `regioninfo`, 1 | `Region_Pywel` | Clave de datos; no región desbloqueada ni radio inicial demostrado |

[S] El filtro da 19 nombres `Intro/Tutorial` de 6.864 misiones, 68 de 52.082
stages y dos de 1.098 quests. Son recuentos de nombres, no 68 etapas obligatorias
ni un orden completo. Las claves `0xFFFFF4xx` se conservan como u32; no se eliminan
por parecer números negativos.

### 2.2 Lo que demuestra el arranque nativo

[E] En `GameStartInfo` (`0x1508D80`) se deserializan, en este orden, clave u8,
cadena, byte de bloqueo y segunda cadena; después campo de cuatro bytes
(`0x1508E13`), referencia de quest
mediante `0x1523BA0`, byte `_useInstanceSaveSlot` (`0x1508E53`) y mapa de aparición
mediante `0x1535830`. Las cadenas de error identifican los campos en
`0x577A530/580/5D0`, `0x577AC10` y `0x577AD50`.
El campo serializado u32 pasa por `0x780350`; se almacena su resultado u16 en
objeto `+0x20` (`0x1508E31–3D`). No confundir esos cuatro bytes del archivo con
la anchura de su representación interna. El lector de referencia de quest puede
producir `0xFFFFFFFF` y devolver éxito si no resuelve la búsqueda (`0x1523C23–36`).

[S] (tabla contrastada con el lector) El único registro extraído de
`gamestartinfo` es `Main`, clave 64, campo serializado 1, referencia de quest
serializada 1, byte de instancia 0 y mapa de aparición con cuenta 0. **No se
atribuye la referencia 1 a `Quest_Intro`**, cuya clave es 10001, ni se inventa su
resolución. El mapa vacío tampoco da coordenadas explícitas. Un registro de
arranque general no equivale a una partida posterior a la introducción.

[E/E-x] `NewGameStartReq` → `0x2B80C00` llega a
`0x2B7FDC0 → .xtls 0x1069F640` y contesta por escritor `0x2A1CB40` con
`ToTarget` (`0x2B80DD9`). En el tramo protegido, `0x1069F7C8` comprueba el gestor
global `+0x60` **en la rama cuyo argumento leído en `0x1069F791` no vale −1**.
Si está libre lo toma, llama a `0x186F920` (`0x1069F846`) y lo libera al volver
normalmente (`0x1069F861`); si ya estaba ocupado devuelve el error de
`[0x6CF7CA4]` (`0x1069F7EA–7F3`), sin encolar aquí la petición.
Requiere la coordinación de A3; no demuestra creación independiente
del campo actual ni permite especificar una plantilla avanzada con esa Req vacía.

### 2.3 Inventario, flags y stages: no hay una receta de un solo campo

[S] (tabla) `inventory` tiene 21 definiciones: `Money`, `Character`, `Quest`,
`CampWareHouse`, `WareHouse`, `InvisibleInventory` y otras. No enumera los objetos
del recién llegado. No hay una lista exacta demostrada de armas, consumibles,
dinero o habilidades del instante solicitado; tomarla de una partida avanzada o
de los premios de una misión adelantaría progreso. Fuente: `datos_inicio.py` y JSON.

[E] Quest carga por `0x2908090 → 0x290DFA0` y produce datos para guardar por
`0x29080D0 → 0x290F1F0`. Su cargador lee además el byte global del gestor `+0x61`
en `0x290E01B`; no es una asignación aislada del modo de carga. A3 identifica
`QuestStateData`, `MissionStateData`, `StageStateData` y
`QuestGaugeStateData_FactionOperation`, junto a ContentsMisc/Sequencer mixtos.
Fuente: volcados A5 y [vision-a3-guardado-mundo.md](vision-a3-guardado-mundo.md), §3.

[S] La plantilla conserva una combinación coherente de misiones, submisiones,
recompensas ya cobradas, stages terminadas, diálogos, guías, conocimiento y
desbloqueos. No marcar todas las quests introductorias completas, vaciar las stages
ni usar `CompleteQuestByCheatReq` para fabricarla: no está demostrado que reproduzca
efectos, premios y limpieza nativos.

[S] (archivo ya extraído) `p_boss/out/stages/cd_seq_quest_intro_0110_dlc.pastage`
contiene `set_noDie`, `set_loading_stage`, `BossPhase`, `Reset` y
`CheckLoadingStep(Done)`; `p_boss/stagestr.py` lo reproduce. Una stage introductoria
puede describir combate, inmortalidad temporal y cambio de nivel. Las cadenas no
prueban que todas esas ramas se ejecuten, ni habilitan una plantilla a medio combate.

### 2.4 Qué aportan las capturas y qué no

[V] Medición propia de ambos archivos con `src/common/capture_file.h` como formato:
en 112627 hay 81 `UpdateStageListAck` (2724), 208 `UpdateMissionListAck` (3009),
un `UpdateQuestListAck` (2808), un `RegistPlayGuideListAck` (2898) y dos
`NotifyQuestSaveDataAck` (2426). En 105149: 84, 298, cuatro, uno y dos. Ninguna
tiene 2178/2717. Hashes, tamaños y tiempos en el JSON; `capturas_inicio.py` no usa
ni escribe cachés de `pk.py`.

[V] El paquete 2808 de 112627 a 3,764841 s tiene 23.778 bytes completos y contiene
los cuatro bytes de `Quest_Intro` en offset absoluto 80; el de 105149 a 91,670864 s
declara 23.744 bytes y conserva 1.024. [S] Una coincidencia binaria de clave no
identifica estado ni prueba misión terminada; aquí no se descodifica todo el esquema
de listas. No atribuir a esas capturas el final del prólogo.

[V] Las posiciones de la segunda carga son `(-10329,832; 627,760; -3442,509)` a
37,338982 s y `(-10285,928; 649,634; -2823,175)` a 125,586806 s, respectivamente.
La primera carga de ambas usa `(0; 1000; 0)`. Fuente: A1, `MoveFieldAck`,
`data/vision-a1-capturas.json`. **Son posiciones grabadas, no propuestas de alta.**
No se enlazan a una lista certificada de regiones desbloqueadas.

## 3. Contrato de una plantilla de jugador nuevo

[S] Sería una generación inmutable producida por guardado nativo, con huella de
build, hashes de blobs necesarios, instante marcado, comprobaciones y versión de
política mundial. No se ha creado ninguna. No separar archivos suponiendo
`lobby.save = mundo` y `save.save = jugador`: A3 refuta esa equivalencia. Antes del
alta se debe demostrar conservación personal y exclusión del mundo del donante.

| Grupo | Tratamiento de diseño [S] | Evidencia/riesgo | Confirmación futura |
|---|---|---|---|
| SteamID, cuenta, catálogo y alias | Registro propio con identidad autenticada; no heredar donante | A4: campo u64 no prueba identidad durable; búsqueda protegida | IN2: A/B y relogin exacto |
| `characterNo` y referencias | Asignación/sustitución coherente sólo tras resolver A4; no cambiar ocho bytes aislados | A4: caché de equipo y búsqueda `0x8AEB30` confunden números repetidos | Ropa propia de A/B tras reconectar |
| `actorKey`, sesión, controladores, instancias activas | Recrear por ciclo nativo; no persistir claves efímeras de captura | A0/D0/A1: SAM y `actor+0x90` | Ack/permisos sin referencias a actores antiguos |
| `itemNo`, equipo y ranuras de uso | Resolver generación/alcance A4; conservar cantidades/tipos, recrear instancias por vía validada | A4: generador no localizado; `_reserveItemNo` es referencia de uso, **no contador** | Usar/equipar, guardar y recargar sin duplicación |
| Inventario, dinero, stats, habilidades, conocimiento | Valores exactos de la plantilla, sin premios posteriores | A3: componentes distintos; lista inicial pendiente | Comparación antes/después de carga y reinicio |
| Misiones, submisiones, stages, diálogos, guías | Coherencia completa; reiniciar sólo temporales de semántica demostrada | `0x2908090/0x29080D0`; A3: estado mixto | Primera misión funcional, sin prólogo/premios repetidos |
| Apariencia y edición semanal | Base válida, nuevo registro por SteamID conforme a A6 | Apariencia no implica reset narrativo | Personalizar sin cambiar misiones/inventario |
| Posición, orientación, campo y región | Punto certificado compatible con mundo actual; validar suelo/navegación y escena | §2.4 no certifica punto; B5/A1 | Aparición segura y salida de zona |
| Facciones, NPC, gimmicks, vivienda, niveles, stages mundiales | Una autoridad mundial; no aplicar copia antigua de plantilla | A3 ContentsMisc/Quest mixtos | IN4: entrada sin retroceso ni actores duplicados |
| Hora, clima y temporizadores | Reloj mundial del servidor; plazos nuevos reanclados según semántica | `0x219D8EC` puede sustituir FieldTime; A3; semana A6 usa otro reloj | Sin salto horario ni efectos expirados/revividos |
| Entitlements, logros, ranuras, UI local | No clonar derechos ni emitir logros por importación; slot no es identidad | A3/A4; `pendiente-recompensas-progreso.md` §2.4 | Dos cuentas sin concesiones ajenas ni título incorrecto |

[S] Esto es un contrato, no una receta de edición del blob cifrado. **Todavía no hay
una plantilla clonable segura certificada.** Un escalón es que cada cuenta produzca
su estado nativo en el mismo punto bajo control del usuario; reduce edición, pero
sigue requiriendo resolver `characterNo`, aislamiento mundial y persistencia.
La clonación idéntica sólo es un experimento controlado, no alta de producción.

[S] Flujo de alta: reservar identidad/ranura → preparar copia en raíz propia →
validar adaptación y compatibilidad mundial → cargar mediante cola global A3 →
completar dos ciclos nativos → verificar personaje, posición y permisos → publicar
primera generación. Un fallo revierte la transacción y sólo libera reservas tras
la limpieza demostrada de A2/A4. No reutilizar caché de otra cuenta; nunca slots
3 (CrossSave) o 206. No enviar `NewGameStartReq` al mundo activo como atajo de alta.

## 4. Historia personal dentro de un mundo compartido

[E] `StageBranchStateReq` (`0x2A5C4F0`) lee 8+4+1+1 bytes, toma el actor de
`TrocParams` en `0x2A5C673`, resuelve su field y llama a `0x2891FB0` en
`0x2A5C6F1`. Este destino es thunk a `.xtls 0x102D2B70`, no punto de hook legible.
`UpdateStageChartEventReq` (`0x2A5CC70 → .xtls 0x104C9B00`) lee 8+4+4+1 bytes y
llama a `0x2C4A8A0` con actor solicitante. [S] No tratar un evento de etapa como
un flag universal para todos los participantes.

[E] `SetAvoidByCutSceneAck` (`0xC09000`) busca el actor por clave con `0x8A67E0`
en `0xC092CA` y pasa contexto a `0xCB5630` en `0xC09329`. Su payload incluye
1/4/8/1 bytes y una lista restante de u32. **La llamada al procesador también se
alcanza cuando la búsqueda falla**: `0xC092D4` salta a `0xC092FF`. Sólo si se
encuentra se sustituye `TrocParams.actor` en `0xC092FC`; buscar no equivale a exigir
haber encontrado actor. [S] Eso no demuestra que todas las
cinemáticas sean privadas, ni que repetir el Ack conserve cámara, input y fase
en ocho clientes. Fuente: `c09000.asm`; `pendiente-jefes.md`, §§1–3.

| Contenido | Política prudente [S] | Riesgo/evidencia | Prueba |
|---|---|---|---|
| Tutorial/presentación ya superado | Estado completo de plantilla, sin repetir prólogo | Carga, control e inmortalidad en stages (§2.3) | IN1/IN2 |
| Diario, diálogo, aprendizaje sin efecto físico | Personal, con requisitos nativos | Quest/QuestDialog/Knowledge (A3) | IN5: A completa y B puede completarlo después |
| Misión que cambia puerta, NPC, terreno, facción o vivienda | Un resultado mundial del servidor; progreso personal compatible | ContentsMisc/Quest mixtos; spawns A1 | IN4: B antiguo no revierte campo |
| Jefe con fase/cinemática/nivel | Participantes/transición coordinados; excluir del prototipo si no es compatible | `pendiente-jefes.md`; reparto humano no demostrado A1 | IN5: entrada tardía, muerte y reconexión |
| Recompensa de misión mundial ya resuelta | Elegibilidad/cobro por jugador y política concreta, sin premios retroactivos automáticos | A1 no prueba reparto general | IN4/IN5: sin duplicación ni bloqueo del novato |

[S] Si una misión personal necesita un NPC o geometría de una versión anterior,
no caben ambos resultados contradictorios en un campo sin una solución nueva de
contenido. La primera versión debe restringir esa misión o adaptar sus requisitos
de forma validada; nunca rebobinar el mundo por el novato. Instancias privadas de
historia cambiarían el alcance literal de «un solo mundo»: son una alternativa
pendiente de decisión, no parte silenciosa de este diseño.

[S] El jugador desconectado conserva hechos personales y referencia a versión
mundial compatible. Al volver se comprueba antes de activar spawns/stages. No hay
evidencia de reconciliación nativa automática de historias distintas; llevar clave
de usuario en el protocolo no la demuestra.

### 4.1 Escalado del alta

| Personas | Sesiones con anfitrión jugador | Con ancla técnica adicional | Criterio A5 [S] |
|---|---:|---:|---|
| 2 | 2 | 3 | A avanzado y B nuevo; cerrar conflicto de historia |
| 4 | 4 | 5 | Altas sucesivas, progreso distinto, sin stages/cachés heredadas |
| 8 | 8 | 9 | Objetivo; altas/guardado serializados con mundo activo estable |
| 10 | 10 | Superaría la política de diez sesiones si todos fueran externos | Diez con anfitrión participante o sin ancla adicional |

Fuente: D-22/A1/A2; **no es un límite nativo demostrado**. Las 30 cuentas
registradas no requieren 30 sesiones cargadas [S, diseño A4]. La diversidad de
progreso introduce conflictos de contenido que no se resuelven sólo con CPU.
Se serializan altas incluso con dos personas por gestor global y caché por slot A3.

## 5. Pruebas futuras, ninguna ejecutada en esta investigación

Requieren antes las puertas A0 correspondientes y **RPT-0** de
[vision-a7-transversal.md](vision-a7-transversal.md) / D-28, antes de E1 y de
ensayos que provoquen fallos; es una dependencia transversal, no una comprobación
adicional realizada por el revisor A5. Adaptar/cargar/guardar copias
requiere A3/A4 y raíz de laboratorio aislada; **no desactivar P6 para probar la
plantilla**. El usuario hace las pruebas en vivo. No existe una herramienta de
alta implementada por describirla aquí.

| ID | Experimento | Evidencia a conservar | Éxito y decisión |
|---|---|---|---|
| **A5-IN1** | Recorrido nativo desde nueva partida: marcar antes, primera libertad y primer guardado recargable | Vídeo/marcas, misión visible; 2808/3009/2724/2898/2426; `MoveFieldAck`; inventario, posición/región; copias preparadas por usuario fuera de sus partidas reales | Fija frontera exacta y diferencia si no es guardable; decide plantilla |
| **A5-IN2** | Dos identidades de laboratorio con estados nativos equivalentes; adaptar una plantilla sólo tras A4 | Identidades, actorKeys, itemNo, equipo distinto, misiones/guías y hashes | Independencia, primera misión jugable, sin premios/tutoriales duplicados |
| **A5-IN3** | Salida/entrada, reinicio y primer guardado del nuevo jugador | Jugador/mundo antes y después, slot, FieldTime, epochs y timers | No vuelve al inicio por fallo de publicación, no hereda cachés, reloj estable |
| **A5-IN4** | A en mundo con puerta/NPC/campamento cambiado; B entra con plantilla antigua | Gimmicks, spawns, geometría, facción/hora y progreso personal | Mundo no retrocede ni duplica; B tiene vía narrativa válida |
| **A5-IN5** | Misión personal y encuentro con stage en fases distintas; B entra tarde y reconecta | Stage, cámara/input, daño, finalización y premios por cuenta | Dos progresos correctos y un resultado físico; si falla restringir contenido antes de escalar |

[S] IN1 es la siguiente prueba específica de A5; no sustituye E0/E0b/E1/E2. No
hacen falta ocho jugadores para fijar el punto, pero sí clientes reales para
certificar cámara/input/contenido a cada escala. El replay sintético no demuestra
que el novato pueda jugar su primera misión.

## 6. Diez afirmaciones para revisión independiente

No se propone ningún hook nuevo. Estas entradas son puntos de análisis; los thunks
y `.xtls` no son candidatos. La observación futura aprovecha sondas/captura
existentes; otro hook exigiría bytes/AOB únicos antes de proponerse.

| # | Afirmación y alcance exacto | RVA/volcado A5 | Confianza |
|---|---|---|---|
| A5-C1 | `GameStartInfo` lee clave u8, campo serializado de cuatro bytes convertido a u16, referencia quest, byte instancia y mapa; errores nombran los campos | `0x1508D80`, `0x1508E13/2C/3D/41/53/7A`; `1508d80.asm` | [E] |
| A5-C2 | `NewGameStartReq` exige payload vacío y usa componente del solicitante; no lee coordenadas/flags de prólogo | `0x2A1E042`, `0x2A1E05E–7F`; `2a1dfb0.asm` | [E] |
| A5-C3 | Arranque llega al thunk `0x2B7FDC0 → 0x1069F640`; rama protegida con argumento distinto de −1 toma gestor `+0x60` si está libre y llama `0x186F920`; ocupado devuelve error | `0x2B80DAC`, `0x1069F791–861`; `2b80c00.asm`, `2b7fdc0.asm`, `1069f640.asm` | [E/E-x] |
| A5-C4 | Quest carga y produce guardado por dos envoltorios que usan su objeto `+0x18` | `0x2908090 → 0x290DFA0`, `0x29080D0 → 0x290F1F0`; respectivos `.asm` | [E] |
| A5-C5 | Carga Quest lee byte `+0x61` del gestor `[0x6D696A0]+0x30` | `0x290E010–20`; `290dfa0.asm` | [E] |
| A5-C6 | StageBranchStateReq lee 8+4+1+1 bytes y toma actor solicitante para field y `0x2891FB0` | `0x2A5C58C–64A`, `0x2A5C673–6F1`; `2a5c4f0.asm`; thunk `.xtls 0x102D2B70` | [E], destino [E-x] |
| A5-C7 | UpdateStageChartEventReq salta a `.xtls 0x104C9B00`; allí lee 8+4+4+1 bytes y llama `0x2C4A8A0` con contexto de actor | `0x2A5CC70`, `0x104C9BA3–C9A`; respectivos `.asm` | [E-x] |
| A5-C8 | SetAvoidByCutSceneAck busca actor por clave recibida antes de pasar contexto al procesador; no es Ack vacío global y encontrar actor no es requisito para esa llamada | `0xC092BF–329`, `0x8A67E0`, `0xCB5630`; `c09000.asm` | [E] |
| A5-C9 | Rama habilitada copia 32 bytes al FieldTime servidor según TLS; byte de entrada cero salta esa copia | `0x219D82B–82F`, `0x219D8E1–8FE`, destino `0x6A6D238`; `219d7f0.asm` | [E] |
| A5-C10 | FactionSpawnStageManagerSaveData tiene `_stageList`; constructor y descriptor identifican tipo/propiedad, no sólo mensaje | `0x19F3FAE–FE7`, vt `0x58A8D20`; `0x18ED236`, vt `0x5851668`; `.asm` homónimos | [E] |

Solicitar además reproducción de los 27 bytes de `gamestartinfo/Main` (C1) y evitar
convertir el salto de una copia de hora (C9) en aislamiento global. Las coincidencias
de claves en capturas permanecen sin interpretación semántica de estado.

## 7. Archivos y reproducibilidad

- Informe: `docs/research/vision-a5-inicio.md`.
- Evidencia versionable: [vision-a5-inicio-datos.json](data/vision-a5-inicio-datos.json), con hashes, registros candidatos y medidas de capturas.
- Scripts propios: `datos_inicio.py`, `estatico.py`, `capturas_inicio.py`, `publicar_evidencia.py`, en `C:\Emulador CD\build\research\vision\a5\`.
- Volcados: `datos_inicio.json`, `exe_cadenas.json`, `capturas_inicio.json` y `.asm` citados en ese mismo directorio.
- Python de investigación con `PYTHONDONTWRITEBYTECODE=1`; sólo lectura de exe/capturas/tablas previas y escritura de los artefactos indicados.

[S] La incógnita principal es semántica: fijar el instante real de inicio libre y
demostrar que su estado personal puede entrar en un mundo avanzado sin aplicar una
segunda versión de ese mundo.

## 8. Revisión independiente contra el exe

Revisor distinto del autor, 2026-10-08. Lectura propia con `cdpe.PE`, Capstone,
RTTI leído desde vtable/COL/TypeDescriptor y un lector nuevo de índices de tablas.
No se han ejecutado el juego, scripts del autor ni extractores, ni leído partidas.
Se reproduce SHA-256 del exe
`57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`
y timestamp `0x6AB28F00`. **Resultado: diez confirmadas con los límites de esta
tabla; ninguna acredita una frontera de exploración libre ni una plantilla segura.**

Fuentes propias: `C:\Emulador CD\build\research\vision\verificacion\a5\`;
`verificar.py`, `ampliar.py`, `tablas.py`, `cadenas.py` y sus volcados. Evidencia
versionable en [vision-verifica-a5.json](data/vision-verifica-a5.json).

| Afirmación | Dictamen | Comprobación e intento de refutación |
|---|---|---|
| C1 | **Confirmada, precisada** [E] | `0x1508D80` lee u8/cadena/u8/cadena; `0x1508E13` consume cuatro bytes, pero `0x1508E31–3D` almacena u16 tras `0x780350`. Los lectores `0x1523BA0` y `0x1535830` consumen u32 de referencia y cuenta. Referencia no resuelta puede convertirse en −1 con éxito; no identifica `Quest_Intro`. |
| C2 | **Confirmada** [E] | `0x2A1E042` rechaza payload no vacío en la ruta habilitada; `0x2A1E05E–7F` usa `TrocParams.actor → +0x68 → +0x10`. El byte de deshabilitación del handler puede evitar procesar la petición; no hay datos de plantilla en su payload. |
| C3 | **Confirmada, precisada** [E/E-x] | Salto relativo real desde `0x2B7FDC0` a `.xtls 0x1069F640`. La rama `0x1069F791` permite omitir la operación de ranura; si entra y `+0x60` está ocupado, `0x1069F7EA` devuelve error. Toma/liberación y llamada a `0x186F920` son locales al camino normal, no una garantía de atomicidad o recuperación. |
| C4 | **Confirmada** [E] | RTTI de vtable `0x5B1F8F8` identifica `ServerQuestActorComponent`; vf50/vfD0 contienen `0x2908090/0x29080D0`. Ambos cargan objeto `+0x18` y llaman a `0x290DFA0/0x290F1F0`. Producir datos de guardado no demuestra escritura durable del archivo. |
| C5 | **Confirmada** [E] | `0x290E010–20` lee `[global 0x6D696A0]+0x30`, después byte `+0x61`. Esto demuestra dependencia compartida de modo, no por sí solo una carrera ni la semántica íntegra de ese byte. |
| C6 | **Confirmada** [E/E-x] | Lecturas 8/4/1/1 y comprobación de consumo exacto en `0x2A5C58C–65C`; actor en `0x2A5C673`, contexto de campo mediante `0x17FA6C0`, vf48 y llamada `0x2A5C6F1`. `0x2891FB0` salta a `.xtls 0x102D2B70`; no demuestra estado global idéntico en todos los usuarios. |
| C7 | **Confirmada** [E-x], continuación [E] | Thunk `0x2A5CC70 → 0x104C9B00`; lecturas 8/4/4/1 y llamada `0x104C9C9A`. Se pasa el contexto `TrocParams`, cuyo actor lee el receptor `.rsrc` en `0x2C4A8CF`; no un actor tomado directamente de esos 17 bytes. |
| C8 | **Confirmada, precisada** [E] | El u32 recibido se pasa a `0x8A67E0`. Si la búsqueda falla, `0xC092D4` salta a `0xC092FF` y aun así alcanza `0xCB5630`; sólo el éxito sustituye el actor de contexto. No usar el Ack como validación de existencia ni evidencia de cinemática privada. |
| C9 | **Confirmada** [E] | `0x219D82F` salta la copia con argumento cero. En la rama activa YMM copia 32 bytes; TLS `+0x1EC` selecciona `0x6A6D238` o `0x6A6D218` en `0x219D8EC/8F6`. El salto no bloquea otras escrituras de hora ni hace segura toda la carga. |
| C10 | **Confirmada** [E] | Constructor real `0x19F3F10` coloca vtable `0x58A8D20`; RTTI nombra `FactionSpawnStageManagerSaveData`. `0x18ED190` usa cadena `_stageList` (`0x57DECE0`) y vtable `0x5851668`, cuyo RTTI vincula vector y tipo. El descriptor no clasifica automáticamente cada stage como mundial o personal. |

[S] (tabla reproducida) Se han cotejado los hashes de cabecera/cuerpo de las
**11 tablas** del JSON, se dedujeron sus anchuras de índice mediante longitud y
coincidencia de claves, y se verificaron **160 registros seleccionados y 649
cadenas** contra los bytes de esas copias. Coinciden claves, nombres, offsets y
límites. `gamestartinfo/Main` consume exactamente **27 bytes**: clave 64, `Main`,
bloqueo 0, cadena vacía, campo serializado 1, referencia de quest 1, instancia 0 y
cuenta de apariciones 0. En `sublevelinfo` 521 se reproducen los cinco u32
`0xF642E` en offsets relativos **102, 123, 144, 165 y 186** y la expresión de esa
condición en `conditioninfo`. No se ha cotejado de nuevo el empaquetado del juego,
ni se deduce de estos bytes el orden efectivo de misiones, el punto libre o el
estado completado de ninguna misión. Las estadísticas de capturas de §2.4 no se
han recalculado en esta revisión y conservan la atribución a los lectores A5/A1.
