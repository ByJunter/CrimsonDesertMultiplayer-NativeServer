# Visión A4: identidad y partida de cada jugador

Build **1.0.0.2976**, PE `0x6AB28F00`, base `0x140000000`; direcciones RVA. Investigación del 2026-10-08, exclusivamente documental y estática. **Informe y verificación independiente terminados (§9): diez afirmaciones confirmadas dentro del alcance indicado, con precisiones de condiciones y límites**. No se ejecutó el juego, no se modificó el mod ni se leyeron partidas completas.

Leyenda: [V] observación histórica/captura identificada; [E] desensamblado `.rsrc`; [E-x] `.xtls`, aunque resulte legible; [S] hipótesis o diseño; [R] fuente externa no contrastada contra esta build. Estado de código significa lectura del repositorio, no prueba viva.

**Actualización transversal de las pruebas:** [S, D-28] antes de la parte de E1 que provoca un guardado rechazado y de cualquier ensayo deliberado de blob inválido, disco lleno, corte de guardado o crash, debe completarse **RPT-0** de [A7 §4.3](vision-a7-transversal.md). P6 bloquea escrituras, pero no aísla lecturas ni adjuntos de reportes. [E/E-x] La ruta `0x2355700 → 0xEF51850 → 0x2356270 → 0x1317D40` puede solicitar adjuntos de la ranura fallida al SDK de Sentry, bajo condiciones; no se observó transmisión. Esta precondición completa todas las secuencias de pruebas que aparecen después, sin ejecutar ni habilitar ninguna.

## 1. Veredicto y contrato con A3

[S] Treinta cuentas registradas y hasta diez sesiones activas requieren un catálogo externo por **SteamID64 completo**, separado de las ranuras nativas. Una cuenta en disco no necesita un actor cargado. La identidad que llega del transporte debe vincularse a la sesión nativa y sustituir la cuenta u64 de `LoginUserToFieldServerReq`; un número que envíe el cliente dentro del paquete no autentica al jugador. Fuentes: `segunda-sesion.md` §1.4, `opcion-d-login.md` §4, `src/mod/session2/server.cpp:717` (hoy reescribe una sola cuenta configurada).

[E/S] Cambiar la cuenta no resuelve todas las colisiones: `characterNo` repetido es especialmente peligroso en el **cliente**, donde `0x8AEB30` busca el primer actor por número y `EquipSlotListCache` usa `(characterNo, serie)` (`apariencia-estado-coop.md` §5.3). El login servidor limita su búsqueda a los hijos del usuario (`0x2C3E030`), pero esto no demuestra ausencia de tablas globales en otras rutas. Se investiga de nuevo abajo.

[S] Se adopta el contrato de `vision-a3-guardado-mundo.md` §4 y sus correcciones §8: conservar blobs nativos; componentes mixtos exigen política explícita; `lobby.save` no es la partida mundial; una ranura arrendada por sesión; cola global para carga/guardado/transiciones; mantener la barrera TLS `+0x4460`. Prohibidas 3 y 206. **No rotar ranuras entre SteamID hasta demostrar invalidación completa**, ni interpretar dos llamadas a `0x2069640` como dos commits durables. Un retorno correcto de `0x2355700 → .xtls 0xEF51850` puede no haber escrito archivos.

## 2. Identidad, login y reconexión

### 2.1 Cinco identificadores que no deben confundirse

| Identificador | Función y evidencia | Riesgo / prueba necesaria |
|---|---|---|
| SteamID64 | [R] Identidad única de Steam de 64 bits; el transporte entrega `m_identityPeer`. [S] Clave del catálogo propio; representación decimal sin conversión por coma flotante. | No aceptar el nombre de perfil ni un u64 arbitrario del payload como identidad. Probar dos cuentas Steam distintas y una petición que declare el ID ajeno. |
| Cuenta u64 de LoginUser2490 | [V] Payload `+0` vale 0 en capturas112627/105149; [E] llega a búsqueda `0x2C48C6B → 0x2A838E0`, cuyo thunk salta a `.xtls 0x10534880`. | Semántica del núcleo protegida: la retención real del valor no está demostrada (§2.2). E2 debe observar usuario/cuenta antes y después. |
| actorKey | [E] Identidad transitoria de actor/usuario del motor; SAM indexa por bits de clave (`0x2A83820`: `&0xFFFFF`, luego stride16). | No sirve como ID permanente ni entre reinicios. Reasignar actorKey dentro de cada época; no renombrar archivos con él. |
| characterNo | [V] `0x7FFFFFFFFFFFFFFF` en los lobbies/login capturados; [E] búsqueda por hijos servidor y caché cliente global (§3). | Distintas cuentas no garantizan distintos characterNo; probar equipo y cachés con ambos visibles. |
| slotNo | [E] Selecciona `slotN` en un almacén compartido, no un SteamID (`0x186F570`, `.xtls 0xEF537A0`). | Una ruta distinta con el mismo slot puede servir el plano anterior. Arrendamiento e invalidación (§4). |

[R] Fuentes oficiales consultadas el 2026-10-08: [Steam, identificación y autenticación](https://partner.steamgames.com/doc/features/auth), [SteamNetworkingMessage_t](https://partner.steamgames.com/doc/api/steamnetworkingtypes#SteamNetworkingMessage_t). Valve documenta el SteamID64, tickets firmados y la identidad del remitente en los mensajes. Su contrato no prueba qué validaciones realiza esta versión del mod. **Estado del repositorio:** `src/mod/net/steam_transport.cpp:364–367` comprueba tipo SteamID y conserva `m->peer.steam_id64` en la bandeja de entrada; `src/mod/session2/server.cpp:717–718` reescribe una sola cuenta configurada. El vínculo verificable SteamID→usuario nativo para N sesiones todavía debe construirse.

[S] Catálogo mínimo propio: `{SteamID64, worldId, characterIdServidor, versionEsquema, generacionPublicada, ultimaPersonalizacion, estadoAdmisión}`. Tabla en memoria: `{SteamID64, epocaTransporte, epocaServidor, NwSession*, ServerUserActor*, actorKey, slotArrendado, estadoCarga}`. El puntero se valida con época y enlace bidireccional; nunca se reutiliza como identidad persistente. El ID de carpeta de Steam usado por el juego es una representación distinta del SteamID64 completo (`opcion-d-p9.md` §1.3); la nueva raíz usa el completo, generado por el servidor y validado como número, sin rutas suministradas por el cliente.

### 2.2 El login nativo busca una cuenta, pero su retención no está cerrada

[E] En `0x2C48C13–16` se copia el u64 recibido al bloque local de login. `0x2C48C59–6B` entrega su dirección a `0x2A838E0`; cuando devuelve una referencia válida, `0x2C48C79–90` pasa al componente UserLogin de ese usuario y llama `0x2C17AB0`. El cuerpo de búsqueda sigue opaco [E-x]; **no se interpreta su algoritmo ni se propone llamarlo directamente**. Fuente: volcados nuevos `a4/2c48be0.asm`, `2a838e0.asm`; inventario A0 §5.

[E] La rama de relogin comprueba varios campos; en `0x2C17B84–8C` compara `componente+0x78` con el u64 `bloqueLogin+8`. Si todo coincide, `0x2C17BF5–BFC` sustituye `usuario+0x1F8`, `0x2C17C6F` llama al enlace protegido `0x1437400`, y `0x2C17CA3/0x2C17CBD` puede desenlazar y cerrar la sesión anterior por vf3. **Encontrar el mismo número no equivale a un relogin autorizado e inocuo:** puede reemplazar la sesión anterior o tomar una rama de error.

[E] Precisión del revisor: las discrepancias convergen en `0x2C17B8E`; si el código global leído de `exe+0x6CDB948` vale0, `0x2C17B96` también permite continuar hacia la sustitución. Por tanto, la comparación de cuenta **no demuestra un rechazo incondicional de toda discrepancia**. No se ha medido ese global en ejecución ni demostrado explotación; el catálogo y la admisión propios deben rechazar identidades duplicadas antes de esta ruta.

[E] Hay una advertencia nueva: en `UserLogin 0x2C17520`, si el estado `usuario+0x5E` no es 2, `0x2C1759D` escribe **−1 en ese `componente+0x78`**; en `0x2C175A1` también pone −1 en `+0x18`, que el camino un jugador sustituye por 1 en `0x2C1767F`. `CreateUser 0x2A83000` no recibe la cuenta como argumento visible en `0x2C48D99–DA0`. No se ha encontrado una copia del u64 solicitado a `+0x78` en el cuerpo inspeccionado de UserLogin. **[S] Esto impide dar por probada su retención nativa; no demuestra que falle**, porque quedan búsqueda VM, componentes y llamadas indirectas. Tampoco confirma la antigua inferencia «cuenta0 siempre expulsaría al anfitrión». E2 debe resolverlo antes de dar identidad persistente al número configurado.

[S] Política prudente: reescribir la cuenta desde el SteamID autenticado, conservando el catálogo externo como autoridad, y verificar que el motor acepta ese dominio u64. Si requiere otro dominio, usar asignación propia inyectiva persistente `SteamID64→cuentaNativa`, reservando 0/valores especiales; no truncar a 32 bits ni aplicar un hash con colisiones. **No se propone un parche de `+0x78` a ciegas** ni una llamada a la VM. Una nueva ruta para registrar cuenta exigiría investigación y firma propias.

[S] Para cuentas duplicadas, el primer diseño rechaza una segunda conexión mientras la primera tenga sesión o transacción pendiente; una reconexión legítima espera el cierre y crea época nueva. No usar el relogin como mecanismo automático de expulsión. Prueba **ID-1**: A entra→sale→entra con la misma identidad; B entra con otra; intento de duplicar A mientras está conectado. Registrar el u64 recibido, `componente+0x78`, usuario buscado/creado, ambas sesiones y destinatarios de Ack, sin cambiar progreso mundial. Éxito: A recupera solo A, B no reemplaza A y una duplicada se rechaza antes del motor.

### 2.3 Autenticación y admisión

[S] El contrato de admisión requiere identidad validada por el transporte Steam, canal y época vinculados a esa identidad, lista de miembros permitidos y una sola conexión activa por SteamID. **Un paquete UDP de replay no satisface esa autenticación**: los usuarios sintéticos son únicamente un instrumento de laboratorio. Propuesta para entornos que no acrediten autenticación suficiente: tickets de sesión nativos, con validación asíncrona del resultado antes de abrir datos persistentes; nunca claves privadas del editor ni servicios reservados al desarrollador del juego.

[R] La API oficial [tickets de Steam](https://partner.steamgames.com/doc/features/auth#client_to_client) distingue la comprobación inicial de `BeginAuthSession` del resultado posterior por callback. [S] Para proteger archivos personales, una validación pendiente espera en la admisión; el servidor no atribuye una partida hasta tener resultado. Esta política es más estricta que la continuidad de partida sugerida por Valve, y debe comprobarse compatible con el transporte existente. No se ha ejecutado ninguna API de autenticación en este trabajo.

[R] [ISteamNetworkingMessages](https://partner.steamgames.com/doc/api/ISteamNetworkingMessages) describe aceptación de sesiones, aceptación implícita al enviar y cierre por inactividad; no proporciona una prueba de logout del mundo. [S] Mantener estados separados `autenticado→en cola→cargando→en mundo→cerrando→guardando→liberado`; un temporizador de red no libera inmediatamente actor, identidad ni ranura. Riesgo: dos propietarios sobre el mismo checkpoint; prueba ID-1 y caída entre guardar/cerrar.

## 3. Números de personaje y objeto: alcance de las tablas

### 3.1 characterNo: aislamiento parcial del servidor y colisión concreta del cliente

[E] `0x2C3E072–76` toma `[[usuario+0x68]+0x18]`; `0x2C3E0B7–DD` recorre **sus hijos**, obtiene `pNumero=[componenteStatus+0x70]` y compara `*pNumero` con el characterNo solicitado. Precisión de punteros: `+0x70` contiene un puntero al dato comparado, no se ha probado que sea el u64 directo. SAM registra actores por actorKey/serie (`0x2A837E0`, `0x2A84F00`), lo cual permite distinguir los actores de usuarios aunque repitan characterNo en esta búsqueda. **No demuestra ausencia de otras tablas globales por characterNo.** Fuentes: volcados A4 y `opcion-d-e3-riesgos.md` §5.

[E] El cliente contiene una colisión demostrable de clave lógica: `0xAA8E88` compara characterNo con `entrada+0x18`, `0xAA8E8E` compara la serie `entrada+0x20`; no usa actorKey en esas comparaciones. `EquipSlotListCacheData::Apply 0xAA7F20` resuelve globalmente el actor en `0xAA7F41 → 0x8AEB30`. Esta búsqueda recorre una lista del gestor y compara en `0x8AEC6C–73` el valor apuntado por Status+0x70; al primer coincidente entra en validación/devolución, sin distinguir usuario. Si ese coincidente no supera la validación posterior, puede devolver vacío en vez de seguir buscando. Fuente: `a4/aa8e40.asm`, `aa7f20.asm`, `8aeb30.asm`.

[V histórica/S] `apariencia-estado-coop.md` §5.3 registra characterNo repetidos en el usuario/cuerpo de la plantilla y itemNo de equipo iguales al inventario local; los fallos visibles de equipo que relata son históricos, **su atribución causal a la caché sigue siendo hipótesis**. Con dos personajes visibles que compartan characterNo existe ambigüedad estática real; con cuatro/ocho/diez crece el conjunto ambiguo, no desaparece.

[S] Diseño necesario antes de E3b jugable: un identificador de personaje estable y único por miembro del mundo, conservado por el catálogo aunque se desconecte. **Queda por validar qué valores admite el motor y dónde persistirlos.** Debe aplicarse coherentemente al lobby, hijo, personaje, ActorLoadData, cachés y peticiones relacionadas; no basta cambiar dos offsets en una plantilla ni hacer reemplazo ciego de ocho bytes dentro de paquetes variables. Preferir identidad nativa coherente si el cargador/serializador admite el cambio; una traducción solo en la red necesitaría un inventario completo de todos los mensajes que la referencian y el encaminamiento inverso de ClientCacheReq. Ninguna está implementada o aprobada como hook nuevo en este informe.

### 3.2 itemNo: qué sabemos y qué se ha refutado

| Ruta o dato | Hallazgo y alcance | Riesgo / prueba |
|---|---|---|
| `_itemNo`, string `0x57DC180` | [E] Propiedad reflejada de **ItemSaveData**; setter `0x1884860`, descriptor `0x1884A80`, RTTI `0x6B46F10`. Identifica la instancia, no el tipo de objeto ni por sí solo el generador. | Duplicar una plantilla conserva las referencias originales. ID-2: comparar todas las instancias ya existentes de A/B. |
| `_reserveItemNo`, string `0x57DD010` | [E] Setter `0x189D5B0` escribe `objeto+0x48`; el descriptor `0x189D7D0` usa vtable `0x5891460`, cuyo RTTI es `SimpleReflectPropertyBind<UseItemReserveSlotElementSaveData,ItemNo,...>` (nombre `0x6B43290`, TypeDescriptor `0x6B43280`, COL `0x5EA9FE8`). | **Corrección de E3 §5:** no es evidencia de un contador de nuevos objetos. Es una referencia de una ranura de uso reservado. No modificarla para «separar rangos». |
| Inventario/equipo guardados | [E] Carga `0x212A8D0`, guarda `0x212CDB0`; A3 identifica ItemSaveData, slots, sockets/tintes y referencias en componentes distintos. | El que el inventario pertenezca a un actor no garantiza exclusividad de las instancias al pasar por contenedores compartidos. ID-2 incluye equipar, vender/recomprar, recoger y reiniciar. |
| `AquireItemNodeIdAck`2535 | [E] `0xC06D11/95/DC6` lee u32/u64/u32; `0xC06E09` resuelve actorKey; `0xC06EA2→0xA9DF20` busca el u64 mediante `0x1F6C190` y agrega un u32 a una lista del objeto `+0x1A8`. | No asigna un nuevo itemNo en este cuerpo. La semántica exacta del u64 y el alcance compartido del contenedor requieren seguir vf140/vf48 del contexto de campo (`0x17FA6C0`). No llamarlo generador global. |
| Tabla `0x1F6C190` | [E] Tabla del objeto receptor: hash de8B, buckets `+0x108/+0x118`, lista `+0x120`; compara clave64 en entrada+8 (`0x1F6C217`). | Se llega a ella desde un contexto asociado al actor/campo; esto **no** demuestra tabla privada por jugador ni tabla global única. No atribuirle el tipo ItemNo solo por el Ack. |
| Generador nativo de nuevos itemNo | **No localizado con evidencia suficiente.** [S] No se sabe si la unicidad depende de proceso, componente, guardado, tiempo o contador; se revisaron cadenas de identificadores y rutas anteriores, no todas las llamadas indirectas/VM. | ID-2 debe crear objetos nuevos alternadamente A/B y después de reiniciar, además de observar el productor antes del Ack. Es un bloqueo para intercambio fiable, aunque el movimiento funcione. |

[S] Política prudente de persistencia: el catálogo de auditoría registra objetos como `(SteamID, itemNoNativo, generacion)` mientras no esté demostrado el dominio nativo. Esto distingue **registros propios**, pero no arregla una colisión dentro del motor. Para intercambio o loot compartido se necesita un ID de instancia mundial único y una conversión completa de referencias; definir ese espacio y su contador durable solo después de localizar el productor. No borrar inventario ni sustituir números sin actualizar equipo, sockets, mercenarios, slots de uso, recompra y demás referencias. Hasta la prueba, bloquear intercambio entre jugadores en el prototipo; no prometer ausencia de duplicación al recoger botín.

[S] **ID-2**: A/B con un objeto distinguible cada uno, misma plantilla inicial y luego partidas distintas; hacer nuevas adquisiciones alternadas, equipar/desequipar, vender/recomprar, caída/reinicio y reentrada. Capturar itemNo por destinatario, conjunto por contenedor, propietario efectivo y referencias de equipo. Repetir con orden de login invertido y con tres miembros rotando por una ranura. Resultado exigido: ninguna instancia de B resuelve al objeto de A; la serie/caché de A no cambia por equipar B; el mismo bien no reaparece en dos generaciones. Si no hay transacción de intercambio implementada, ese subcaso queda pendiente, no se simula su éxito.

## 4. P9 invertido: el servidor conserva las partidas

### 4.1 Flujo de autoridad del servidor

[S] P9 invertido conserva las primitivas nativas y cambia la procedencia: **el servidor** selecciona la última generación válida del SteamID autenticado; el cliente no sube una partida ni elige un archivo arbitrario del anfitrión. Para un miembro nuevo se prepara la plantilla aprobada por A5 y su identidad propia. Flujo propuesto:

1. Autenticar y admitir; verificar build/protocolo, cuota de sesiones, mundo y generación. No reservar actores para treinta miembros en reposo.
2. Obtener un arrendamiento nativo y staging asociado a `{SteamID, época, slot}`; materializar allí el par cifrado de la generación publicada, fuera de partidas personales.
3. Antes de permitir `LoginUser2490`, completar la preparación: [E] una rama de modo un jugador de `UserLogin0x2C17520` selecciona la ranura más reciente de **0–2/100–108** (`0x2C17756–72`) y carga con `0x2C17881→0x186F570`, antes de la selección de personaje. Depende de las opciones y metadatos disponibles; no garantiza una carga en todo login (§9, C9). Redirigir solo LoginCharacter3093 llega demasiado tarde para esta ruta.
4. En las dos cargas, la tabla de sesión autenticada determina slot y usuario; la ranura del payload del cliente se valida/remapea, nunca autoriza a leer otro miembro. No invocar `0x2355EC0`/otras rutas `.xtls` directamente: la creación nativa de la entrada debe ocurrir por un flujo auditado del gestor, todavía pendiente para ranuras nuevas.
5. Serializar guardados y transiciones con el coordinador A3; redirigir usuario/lobby y personaje/save al mismo arrendamiento, y aplicar política de componentes mixtos para que el mundo no retroceda.
6. Publicar la generación solo cuando se comprueban pendientes, ambos archivos, hashes y recuperación; el servidor conserva la partida canónica. Un archivo enviado al cliente es como máximo una caché de presentación/exportación, sin autoridad para sobrescribir progreso.

[E/S] Riesgos: el modo un jugador `0x6CEEDE8` permanece a1; cambiarlo abre rutas MMO/ODBC (`segunda-sesion.md` §1.5). La cuenta u64 no selecciona por sí misma los archivos: la selección visible es por slot. Compatibilidad del blob entre dos cuentas sigue siendo **hipótesis**; igualdad de `S+0x78` no descarta dependencia dentro del cifrado VM (A3 §1.3). Prueba: G3a/G3b en raíz aislada y dos cuentas, con inventarios distintos, sin escribir partidas reales.

### 4.2 Ranuras: capacidad numérica frente a capacidad segura

[E] El validador de `0x186F570` acepta **0–2**, **3**, **100–108** y **200–207**: comparaciones `0x186F5D7`, `0x186F5DD–F0`, más excepción3. Son **21 números** admitidos por esta función. [E] 206 (`0xCE`) tiene una ruta posterior hacia borrado (`0x186F850–867 → 0x1871040`): se alcanza tras carga correcta, `[[actor+0x88]+1]==1` y `[actor+0x96]!=0` (`0x186F78F–793`, `0x186F811–826`). La llamada **no prueba borrado consumado en todas las cargas**: `0x1871040` comprueba ocupación del gestor y existencia de entrada antes de llamar a `0x2356000` (`0x1871102–12A`, `0x1871200–24F`). 3 tiene una ruta a CrossSave desde guardado (`0x186E6CD`, llamada `0x186EE49→0x15AC100`, A3 §4), también con condiciones propias. **Ambas prohibidas antes de cualquier operación por contener estas rutas.** Los otros números admitidos no quedan automáticamente certificados para uso arbitrario.

[S] Conservar 0–2 fuera del pool de trabajo porque la rotación global de autoguardado las usa; tampoco usar el slot100 real del anfitrión. El pool candidato de A3 **100–108 y207** tiene diez números, siempre virtualizados bajo raíz propia. Esto alcanza aritméticamente diez arrendamientos, pero requiere certificar las nueve ranuras manuales y la semántica de207; si207 resulta incompatible, ese pool baja a **nueve sesiones** y, con ancla, **ocho personas externas**. Los200–205 son otros candidatos de investigación con semántica de instancia desconocida; no ampliar el pool por mera aceptación del validador.

[E] `.xtls 0xEF537A0` (obtener/crear ranura) obtiene la longitud del nombre, calcula su hash y consulta **S+0x18** en `0xEF53811–26`; si encuentra entrada, reutiliza su objeto en `0xEF53839–3F`. No incluye SteamID en esa clave visible. [S] Por tanto, una sola207 que cambie de ruta para todos es insegura: puede compartir plano, archivos pendientes y flags aunque el directorio externo sea distinto. Fuente: `a4/ef537a0.asm`; cachés `+0x69C/+0x6A0` y `fileObj+0x120/+0x138` contrastadas en A3.

[S] Arrendamiento propuesto: `libre→preparado→cargando→activo→cerrando→guardando→pendientesCero→invalidando→libre`. Solo el final libera número. **La invalidación completa aún no se ha demostrado**: el catálogo de30 registrados es factible en disco, pero no autoriza rotar indefinidamente30 identidades por10 números durante un mismo proceso. Sin invalidación, agotar los números ya utilizados bloquea nuevas admisiones aunque queden plazas de red; reiniciar el proceso tras checkpoint y recuperación es una salida operativa provisional, no un servidor continuo de30 miembros. Prueba PERS-4/ID-3: A→B→A en el mismo número y diferentes blobs, con pendientes y todos los planos observados vacíos entre usos. Logout no basta como evidencia de limpieza.

### 4.3 Puntos legibles admisibles

Solo se proponen los dos candidatos ya certificados en [vision-firmas-2976.json](data/vision-firmas-2976.json), entradas `.rsrc` según `.pdata`; jamás hook en `.xtls` ni inline a mitad de función. El actor se resuelve contra la tabla autenticada y su época. Las firmas exactas tienen una coincidencia en todo el exe2976; no se extrapolan a otra build.

| Entrada | Primeros16B | AOB exacta única para2976 | Función propuesta y riesgo |
|---|---|---|---|
| `0x186F570` | `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 44` | `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 44 89 44 24 18 55 41 54 41 55 41 56 41 57 48 8D AC 24 50 FF FF FF 48 81 EC` | [S] Remapear r8d, según ownerActor de r9, a arrendamiento preexistente. Debe cubrir carga previa al título. |
| `0x186E450` | `4C 89 4C 24 20 44 89 44 24 18 48 89 54 24 10 48` | `4C 89 4C 24 20 44 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 78 FE FF FF` | [S] Remapear r8d según personaje r9→usuario+0xA0 y época. No deducir commit durable por res==0. |

[S] P6 conserva la protección de partidas reales. La virtualización debe cubrir apertura, atributos, enumeración, archivos temporales, copia, borrado y **ambos extremos** de renombrado. No basta un cambio en CreateFileW: el motor llega por FileSystem/CRT y otros caminos (D-4/A3 §4). La raíz concreta se define al implementar, fuera de Steam Auto-Cloud. Prueba ID-3: ni el catálogo del anfitrión ni su carpeta real cambian mientras se guardan usuarios distintos; hash antes/después de las copias de laboratorio.

### 4.4 Pantalla de título: ver «mi personaje del servidor»

[V histórica] En105149, durante unos90s de título no apareció `SaveDataSlotRefreshReq`; `opcion-d-p8.md` §2.3 concluye lectura local de ranuras **[V+S]**, no traza exhaustiva de toda la UI. [E] La rutina local `0x186D9A0` recorre metadatos de ranuras y selecciona0–2/100–108 (`0x186D9DF–DA1D`). No se certifica aquí que un `CharacterLobbyDataAck` remoto reemplace todo ese catálogo.

[S] Primera experiencia prudente: un acceso del mod «Entrar al mundo» muestra resumen canónico recibido por sesión autenticada (nombre, apariencia/ranura lógica y fecha del checkpoint) y permite un único personaje del servidor. Evitar que «Cargar partida» local determine la partida remota. El lobby nativo recibido aporta el modelo/estado donde lo acepte; el resumen propio no promete que todas las miniaturas/menús nativos se actualicen solos.

[S] Si se necesita conservar íntegra la pantalla nativa, habrá que virtualizar su catálogo/lectura para una **caché local fuera de `Pearl Abyss\CD\save`** y resolver su caché de componentes; es trabajo adicional sin punto de hook nuevo validado en este informe. No escribir la partida canónica en el save real ni enviar30 perfiles a cada cliente. Prueba **ID-4**: el título de A solo muestra su personaje del servidor, incluso con partidas locales distintas, luego B entra en ese equipo y no ve la caché de A. El cliente sin red debe mostrar indisponibilidad y no iniciar su partida local como si fuera el mundo remoto.

## 5. Capacidad 2/4/8/10 y pruebas necesarias

[S] Distinguir **H personas** y **U sesiones nativas totales**. Si el anfitrión juega, U=H; si existe un ancla técnica adicional aparcada, U=H+1. Bajo la política prudente U≤10, ocho personas necesitan nueve sesiones con ancla; diez personas con ancla exigirían once, fuera del alcance. No es un límite nativo probado. Fuente: D-22 y `vision-a0-base.md`.

| Objetivo H personas | U si anfitrión juega | U con ancla adicional | Arrendamientos candidatos necesarios con ancla | Dictamen de identidad/persistencia |
|---:|---:|---:|---:|---|
| 2 | 2 | 3 | 3 | [S] Hito inicial después de E2/E3b/G2 y separación de characterNo; PERS-3 verifica dos partidas. |
| 4 | 4 | 5 | 5 | [S] Aumentar solo con transiciones serializadas, desconexión y rotación de ranura demostradas. |
| 8 | 8 | 9 | 9 | [S] Objetivo compatible con pool100–108 si se certifica; no depende aritméticamente de207. Coste de pausa/carga por medir en A2/A3. |
| 10 | 10 | 11, fuera del alcance | 11, fuera del alcance | [S] Con ancla y U≤10: máximo9 externos si207 es seguro; si no,8 con el pool propuesto. Diez humanos requerirían que el propietario juegue o quitar la sesión técnica, aún no demostrado. |

[S] P9 §1.2 mide **1.182.846 B de un único `save.save`**, incluida cabecera; no mide la pareja con `lobby.save`. Redondeando ese ejemplo a 1,2 MB, treinta archivos `save.save` sumarían aproximadamente 36 MB, y tres copias de esos treinta archivos aproximadamente 108 MB. **Faltan los archivos lobby, el mundo, manifiestos, temporales y crecimiento**: no son tamaños de treinta partidas completas ni de tres generaciones completas. Es aritmética ilustrativa, no presupuesto máximo ni medición de treinta cuentas. El catálogo no consume101 claves por registrado; sí cada **usuario cargado** (A0). Aforo de identidad no certifica CPU, streaming, ancho de banda ni contenido.

| Orden | Prueba posterior, nunca ejecutada aquí | Criterio que habilita avance |
|---|---|---|
| ID-1, tras E2/G2 | Cuentas A/B, retención+0x78, reconexión y conexión duplicada (§2) | Vínculo autenticado inequívoco y ninguna expulsión/partida ajena. |
| ID-2, tras E3b | characterNo/caché/equipo visibles, luego nuevos itemNo y referencias (§3) | Identidad canónica coherente; la duplicación de plantilla no contamina A/B. |
| ID-3/PERS-1→4 | Dos blobs, guardado aislado, reinicio, reutilización A→B→A del slot | Archivos y planos exclusivos; admisión de30 en ciclos, no solo diez iniciales. |
| ID-4 | Título/catálogo remoto y cambio de cuenta del cliente (§4.4) | Solo personaje propio y cero escritura personal. |
| PERS-5/6 | Autoguardado+respawn+login, corte entre fases y disco lleno | Durabilidad y recuperación común sin duplicar bienes. |
| PERS-7/8 | U=2→4→8→10, y objetivo H=8 con U=9 cuando haya ancla;24–72h | Colas/pools estables, identificación correcta en cada reentrada y pausas medibles. |

[S] En todos los escalones, mantener fijos los30 registros mientras se rotan conectados. No confundir registrar30 usuarios, abrir30 actores simultáneos y superar10 sesiones; solo lo primero forma parte de la visión.

## 6. Copias, versiones y migración

[S] Cada generación referencia la huella del exe, build2976, versión del protocolo, versión del esquema propio y de la cabecera SAVE, worldId, SteamID64 completo, characterId, número de generación, hashes del par, origen de plantilla y motivo. Conservar por lo menos la generación publicada anterior y otra copia independiente del proceso; una copia en el mismo disco no protege de fallo de ese disco. La retención concreta se ajusta al crecimiento real, no al número de sesiones. Fuente técnica: A3 §§1/4, manifiesto común `data/vision-manifiesto.json`.

[S] Publicar mediante manifiesto transaccional que referencia **mundo y jugadores afectados**; no restaurar cada fichero por fecha más reciente. Una transferencia puede tocar dos inventarios y mundo: restaurar A antiguo y B nuevo puede duplicar/perder objetos. El cierre semanal de personalización pertenece al registro del SteamID (A6), debe sobrevivir a cambios de ranura, reconexión y restauración de blobs; conservar el historial de administración para que un rollback del mundo no reinicie automáticamente ese derecho.

[E/S] La cabecera que admite versiones1–2 (`0x2357DA0`) no prueba compatibilidad del esquema interno entre parches. Una build nueva bloquea admisión/carga/escritura del servidor hasta revalidar firmas, opcodes por nombre, serializador, campos mixtos e identidades (A7). Nunca migrar sobre el único archivo vigente. Mantener copia intacta del checkpoint anterior y trabajar sobre copia bajo la nueva build legítima, con el juego cerrado en las futuras operaciones de archivos autorizadas.

[S] Migración por etapas: detectar nueva huella; comparar esquema/RTTI; comprobar carga y guardado nativo de copias en una raíz de laboratorio; validar inventarios, identidades, quests y mundo; crear una generación nueva con versión; solo entonces publicar. No asumir conversión inversa: si falla una migración, volver al entorno compatible con la **copia anterior**, sin intentar cargar el archivo nuevo con el ejecutable antiguo. Prueba: una cuenta nueva, otra avanzada y objetos compartidos; restauración conjunta tras fallo deliberado de publicación, siempre fuera de partidas reales.

## 7. Diez afirmaciones prioritarias para refutar

Scripts/volcados nuevos: `C:\Emulador CD\build\research\vision\a4\contraste.py`, `explorar.py`, `cadenas.py`, `cerrar.py`; todos leen el exe en disco, con `PYTHONDONTWRITEBYTECODE=1`. Los xrefs históricos sirven como **pistas**; cada afirmación se comprueba desensamblando desde entrada `.pdata` o el destino protegido. Datos entregables: [vision-a4-evidencia.json](data/vision-a4-evidencia.json). Ningún volcado `.xtls` autoriza engancharlo. Los puntos de intervención de §4.3 ya tienen AOB en el catálogo común.

| ID | Afirmación prioritaria del autor y alcance exacto | Contraste a repetir independientemente |
|---|---|---|
| A4-C1 | [E/E-x] El u64 recibido llega a una búsqueda protegida y el resultado válido permite rama de relogin; no se conoce su algoritmo. | `0x2C48C13–16`, `0x2C48C59–90`, thunk `0x2A838E0→0x10534880`. |
| A4-C2 | [E] Retención no demostrada: UserLogin escribe−1 en+78 si estado!=2; relogin compara+78 con cuenta recibida y puede cerrar sesión previa. | `0x2C17585–9D`, `0x2C17B84–8C`, `0x2C17BF5–BFC`, `0x2C17CA3/CBD`. Buscar una escritura indirecta que refute la inferencia; no afirmar pérdida probada. |
| A4-C3 | [E] LoginCharacter recorre hijos del usuario; el número comparado está tras un puntero Status+70; SAM usa actorKey en el registro examinado. | `0x2C3E072–76`, `0x2C3E0B7–DD`, `0x2A83820–2A`. No ausencia global. |
| A4-C4 | [E] Caché de equipo cliente compara characterNo/serie; Apply busca el primer characterNo coincidente en lista global del gestor. | `0xAA8E88/8E`, `0xAA7F41`, `0x8AEC56–73/8B`. Revisar validación del primer coincidente. |
| A4-C5 | [E] `_reserveItemNo` pertenece a UseItemReserveSlotElementSaveData, no prueba contador de generación. | `0x189D5B0`, `0x189D7D0`, vt`0x5891460`, RTTI`0x6B43290`. Contrastar contra inferencia antigua E3§5. |
| A4-C6 | [E] AquireItemNodeIdAck lee u32/u64/u32 y agrega nodeId a objeto buscado; su cuerpo no genera un itemNo. | `0xC06D11/95/DC6`, `0xC06EA2`, `0xA9DF61/AA/AD`, `0x1F6C190`. No atribuir sin prueba tipo/alcance al u64. |
| A4-C7 | [E] El validador admite los21 números enumerados y206 puede alcanzar la ruta posterior de borrado, bajo las condiciones de§4.2. | `0x186F5D7–F0`, excepción3, `0x186F811–867`; `0x1871102–24F`. Aceptación no certifica semántica de207 ni borrado consumado. |
| A4-C8 | [E-x] Obtener/crear ranura busca por nombre en S+18 y reutiliza entrada existente, sin SteamID en esa clave visible. | `.xtls0xEF537DF–0xEF5384B`; contrastar hash/consulta/reuso. No demostrar invalidación por existir destructor. |
| A4-C9 | [E] UserLogin elige más reciente0–2/100–108 y carga antes del LoginCharacter. | `0x2C17756–72`, `0x2C17874–81`; captura/login previo al título en `opcion-d-login.md`. |
| A4-C10 | [E] Entradas de carga/guardado propuestas son.rsrc y sus40B son AOB únicas en el exe2976; slot3 alcanza CrossSave. | Catálogo común y exe íntegro: `0x186F570`, `0x186E450`; `0x186E6CD`, `0x15AC100`. No extrapolar firma a parche. |

## 8. Incógnitas que continúan abiertas

No está identificado el generador nativo de itemNo ni cerrado el inventario de todos sus índices; no está demostrada retención nativa de cuenta ni el dominio de characterNo alternativos; faltan invalidación de slots, preparación nativa de una ranura nueva y actualización completa de título. Estas lagunas impiden afirmar «30 miembros con persistencia continua» aunque la arquitectura propuesta sea razonable. Las pruebas ID-1/2 y PERS-4 las discriminan antes de aumentar aforo. La revisión independiente de§9 no resuelve estas incógnitas mediante observación en vivo.

## 9. Verificación independiente de diez afirmaciones

Otro agente leyó directamente el exe, regeneró25 desensamblados propios y resolvió RTTI/COL sin usar la caché ni los volcados del autor. Scripts y evidencia: `C:\Emulador CD\build\research\vision\verificacion\a4\revisar.py`, `adicional.py`, `metadatos.json`, `adicional.json`; resultado entregable: [vision-verifica-a4.json](data/vision-verifica-a4.json). Huella SHA256 reproducida: `57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`; tamaño384521624B, PE `0x6AB28F00`, base `0x140000000`. Se revisaron cuerpos completos legibles y las ramas que podían contradecir la tesis, no únicamente instrucciones aisladas.

| ID | Dictamen | Contraste, intento de refutación y límite |
|---|---|---|
| A4-C1 | **Confirmada** [E/E-x] | Se siguió `r8→bloque+8→0x2A838E0` y el resultado hasta `0x2C17AB0`. El thunk salta a `0x10534880`, que vuelve a saltar a `0x15F5EE70` en código protegido. Confirma el paso del argumento y la rama; no el algoritmo ni la retención de cuenta. |
| A4-C2 | **Confirmada con precisión** [E] | `r14=−1` precede a `0x2C1759D`; la rama estado2 omite esa escritura. Se revisaron CreateUser, UserLogin, relogin y sus llamadas indirectas. El vf68 del RTTI ServerUserLoginActorComponent (`vt0x5B2FB00`, COL `0x5F9C7A0`) apunta al thunk `0x35EA90`; no ofrece una carga específica legible que cierre la retención. Se añadió el camino de discrepancia con código global0 en§2.2. No se demuestra pérdida ni registro durable. |
| A4-C3 | **Confirmada** [E] | La búsqueda parte de `usuario+0x68→+0x18`, recorre stride16 y dereferencia Status+0x70 antes de comparar (`0x2C3E0B7–DD`). Se comprobó máscara `0xFFFFF` y stride16 del registro SAM (`0x2A83820–2A`). El ámbito de otros índices sigue abierto. |
| A4-C4 | **Confirmada** [E] | El caché compara número/serie en `0xAA8E88/8E`; Apply llama a `0x8AEB30` con ese número. El primer coincidente sale del bucle en `0x8AEC73`; si falla bit0x10 deestado, termina vacío (`0x8AEC8B–90→0x8AECF0`), sin buscar el siguiente. No prueba por sí sola el fallo visual histórico. |
| A4-C5 | **Confirmada** [E] | Resolución propia `vt0x5891460→COL0x5EA9FE8→TD0x6B43280` da UseItemReserveSlotElementSaveData/ItemNo. Setter `0x189D5D2` copia el valor recibido a+0x48; no incrementa un contador. Queda refutada la inferencia antigua de usar esta propiedad para reservar rangos. |
| A4-C6 | **Confirmada** [E] | Lecturas4/8/4B y llamada `0xC06EA2→0xA9DF20`; búsqueda `0x1F6C190` por u64 recibido, inserción del u32 en vector+0x1A8 (`0xA9DFAA/AD`). Se recorrió el cuerpo completo: no produce un u64 nuevo. El generador real y ámbito del contenedor no quedan identificados. |
| A4-C7 | **Confirmada con condiciones** [E] | Aritmética independiente:3+1+9+8=21 números. Se siguieron las ramas previas y la función de borrado: se corrigió§4.2 para no afirmar eliminación incondicional o consumada de206. El riesgo destructivo justifica mantener su prohibición. |
| A4-C8 | **Confirmada localmente** [E-x] | `0xEF537DF–E9` mide el nombre; `0xEF53811–26` calcula hash y consulta S+0x18; `0xEF53839–3F` reutiliza objeto no nulo. No aparece SteamID en esa clave visible. La parte mutada posterior no permite certificar semántica completa ni invalidación. |
| A4-C9 | **Confirmada con alcance** [E] | Filtro0–2/100–108, máximo de campo+8 y carga con usuario r9 en `0x2C17874–81`. Es una ruta del modo un jugador con metadatos disponibles: opciones y ausencia de candidato pueden omitir la carga. El valor elegido inicialmente es0 y el máximo arranca en0; no prueba que todo login cargue siempre una partida válida. |
| A4-C10 | **Confirmada** [E] | Ambos40B reaparecen exactamente una vez en todo el exe y coinciden con entradas `.pdata` de`.rsrc`. La rama slot3 alcanza `0x186EE49→0x15AC100`; cadenas propias de ese cuerpo mencionan UploadCrossSaveFileAsync (`0x57A8DE0`, `0x57A8FC0`). Esto prueba ruta de subida, no envío consumado ni disponibilidad del servicio. |

Las diez quedan confirmadas **solo con estos límites**. No se certifican treinta cuentas rotando indefinidamente, unicidad de itemNo, compatibilidad criptográfica entre cuentas ni seguridad de207. Permanecen [S] los arrendamientos y el catálogo; las firmas certifican localización en2976, no corrección ni seguridad de un hook futuro.
