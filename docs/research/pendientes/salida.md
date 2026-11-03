# P-SALIDA (C4): salida del usuario 2

Fecha: 9 de octubre de 2026. Build 1.0.0.2976. Revisión independiente, sin ejecutar el juego, sin modificar código ni el proyecto de Ghidra.

## Resumen

- **[E/V] Causa confirmada:** el corte del túnel solo detiene el transporte del mod; no solicita el logout nativo. C1g conserva al usuario 2, su personaje y la entrada de campo más de 40 segundos después del corte. Además, el transporte y el sniffer suponen cabecera Troc de 5 bytes; `LogoutSessionToServerReq` 2001 tiene **3 bytes**, por lo que la salida normal resulta invisible en esa captura y actualmente no atraviesa el túnel.
- **[S] Arreglo propuesto:** un ciclo de vida por invitado/época, que convierte salida voluntaria y pérdida del enlace en la misma solicitud nativa 2001, entregada por la cola P2 al usuario correcto. Esperar pruebas de limpieza nativa antes de reutilizar la sesión. No retirar manualmente al personaje de `field+0x2A0` ni invocar funciones de logout desde el hilo del túnel.
- **[E/E-x] El protocolo de usuario, el de personaje y el de apagado del servidor son distintos.** 2564 sale del personaje; 2001 sale del usuario/sesión; 2790 reserva apagar el servidor completo. Un cliente P8 que sale puede publicar 2790: debe quedarse local, nunca alcanzar el servidor del anfitrión.
- **[S] Confianza alta** en la causa, los formatos y los límites del transporte; **media** en el diseño completo de limpieza/reconexión, aún sin una ejecución del logout del usuario sintético. La ruta nativa contiene callbacks y referencias diferidas cuya terminación debe observarse.

## Convenciones y material

[E] = código `.rsrc` leído con Ghidra y contrastado donde importa con capstone; [E-x] = instrucciones legibles de la copia en `.xtls`; [V] = captura/log; [S] = inferencia o diseño pendiente de prueba. En fuente C++ del mod se usa [E-código], explicitando que no procede del ejecutable. Todas las direcciones son RVA, base `0x140000000`. Los nombres `cdmp_*` son anotaciones del equipo, no símbolos oficiales.

Se leyó el encargo y contexto común, C4, §3.4 de campo compartido, §4 de b2-login-pasos y P8. El análisis binario usa exclusivamente `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, `PE(path)` explícito y Python `-I -B`. Las consultas Ghidra son de lectura a la instancia del orquestador. Pseudocódigo y desensamblado quedan en [salida_evidencias](salida_evidencias/); los `.c` intermedios están en el temporal autorizado.

**Precaución metodológica:** una salida de Ghidra que llama a una dirección `.rsrc` de cinco bytes puede estar mostrando el cuerpo al que salta en `.xtls`. Aquí se etiqueta por el cuerpo real. Los ficheros capstone incluyen ventanas amplias; únicamente se interpreta el rango de instrucciones hasta el retorno pertinente, no basura posterior ni supuestas funciones deducidas de ella.

## 1. Qué manda el cliente normal y por qué no se veía

### 1.1 La solicitud realmente atraviesa PostPacket

**[E]** `0x8CAD10`, anotada `ClientNet_PublishLogoutSession`, recorre las sesiones cliente 2–16. Para cada conexión existente construye:

```
D1 07 FF
^^^^^ ^^ 
op2001 slot -1
```

No existe campo `u16 payloadLength`. `0x8CAD7A` escribe 2001, `0x8CAD80` escribe `FF`; `0x8CADAD` prepara tamaño 3 y `0x8CADB7` llama realmente a `ClientNet::PostPacket 0x35C670`, con canal 0 y flags `0xFF`. Pruebas: [decompilado](salida_evidencias/8cad10.c), [instrucciones](salida_evidencias/capstone_8cad10.txt).

**[E-código]** `src/mod/net/troc_sniffer.cpp`, `capture`, líneas 86–93: descarta `size < 5`. El hook no deja de ejecutarse; su función de captura excluye el paquete de 3 bytes. Esta es una explicación concreta del silencio de la captura, sin inventar una ruta alternativa que eluda PostPacket.

**[E]** `FlushSendQueue 0x35BE90` distingue opcodes internos locales 1001/1003 del resto y acaba usando el método `Send` de la sesión para los restantes, incluido 2001. Por ello P8, que intercepta el `Send` de `DummySessionClient`, sí debe tratar 2001. No debe retransmitir los internos locales 1001/1003. [Ghidra](salida_evidencias/decomp_0x35BE90.txt), [capstone](salida_evidencias/capstone_35be90.txt).

### 1.2 ACK internos y lo que demuestra la captura

**[V]** Captura `C:\Emulador CD\captures\cdmp_packets_20261003-105149.bin`, registros contando desde 1, salida al título según el encargo:

| Tiempo s | Registro | Dirección/opcode | Bytes completos |
|---|---:|---|---|
| 1163.584789 | 356144 | S→C 2679 | `77 0A FF 09 00 01 00 10 A0 07 C8 BB 0C A3` |
| 1163.588409 | 356188 | S→C 2003 | `D3 07 FF 04 00 00 00` |
| 1163.588416 | 356189 | S→C 2002 | `D2 07 FF 04 00 00 00` |
| 1163.636842 | 356315 | S→C 1004 | `EC 03 FF 04 00 00 00 8D 41 01 9B` |

[E/V] 2002/2003 tienen cabecera de **3 bytes + u32**, total **7**, no 5+4. La tabla `troc_messages_2976.csv` da min=max 7; 2001 min=max 3. El lector genérico que interpreta siempre cinco bytes ve `plen=4` pero solo dos bytes restantes y clasifica mal estos ACK. Se releyeron los registros CDPK crudos: [extracción reproducible](salida_evidencias/captura_logout_raw.txt). Entre 1163.5–1163.7 hay 168 Req capturadas, ninguna 2001, 2564 ni 2790. **La ausencia de 2001 es esperable por el filtro del sniffer, no prueba de que no se enviara.**

**[E-x]** En 2002 el `u32 +3 = 4` es el índice de sesión cliente al que se publica 1003: `0xA79CA6F` lo carga como tercer argumento de PostPacket y `0xA79CA8C` publica el paquete de tres bytes. No es correcto llamar a ese 4 «motivo de logout». El motivo nativo 4 aparece en otro argumento del manejador servidor. En 2003 se compara el valor con 2 y solo esa rama hace trabajo de autenticación; el caso 4 de esta captura retorna. [2002](salida_evidencias/capstone_a79ca40.txt), [2003](salida_evidencias/capstone_a79cb60.txt).

**[E/E-x]** 2679 contiene actorKey, byte y código de error, pero `0xB6F260` acaba llamando a `0x8C9F90`, que opera sobre el jugador local y limpia actores cliente. No se debe difundir el ACK personal de salida del invitado al cliente del anfitrión. La desaparición de su actor para observadores pertenece a `RemoveActorAck` 2516. [2679](salida_evidencias/decomp_0xB6F260.txt), [cliente](salida_evidencias/decomp_0x8C9F90.txt). [S] La prueba debe verificar destinatarios, no dar por supuesto que el filtro actual los conserva en todas las ramas.

### 1.3 Título y escritorio: rutas encontradas y límite de la observación

**[E/E-x]** El destructor/limpieza `0xACD400` llama a `0x8CAD10` en `0xACD497` si la red está activa; después espera de forma acotada, con iteraciones y `Sleep(100)`, antes de desmontar recursos. Lo llama `0x61A520`. Es una ruta de cierre de infraestructura cliente que utiliza el mismo 2001, pero no se ha demostrado qué botón de interfaz lleva a ella en cada modo. [ACD400](salida_evidencias/decomp_0xACD400.txt), [llamador](salida_evidencias/decomp_0x61A520.txt).

**[E/E-x]** La vf4 de `DummyTaskLogout`, `0xFA83D0`, publica para sesiones 2–16 `ReserveShutDownServerReq` 2790 mediante `0xBDE910` (`E6 0A FF 00 00`, tamaño 5). Después pide estado 4 por `0x8CABB0` → `.xtls 0x9936DA0`; esa rama llama a `0x8CAD10`. [tarea](salida_evidencias/decomp_0xFA83D0.txt), [bytes](salida_evidencias/capstone_fa83d0.txt), [emisor 2790](salida_evidencias/capstone_bde910.txt), [rama 4](salida_evidencias/capstone_9936da0.txt).

**[E]** El manejador 2790 `0x2A21400` no limita su acción al usuario llamador: construye el interno `ShutDownServerOnceTimer` **2038** (`mov r14d,0x7F6` en `0x2A214BE`) y lo encola en `ServerNet`. Es una operación del servidor completo, no el sustituto de 2001. [Ghidra](salida_evidencias/decomp_0x2A21400.txt), [capstone](salida_evidencias/capstone_2a21400.txt), tabla CSV 2038.

**[S] Límite:** hay una captura de salida al título y rutas estáticas de tareas/destrucción, pero no una captura emparejada «título versus escritorio» ni traza de cada pulsación. No afirmo que ambas salidas recorran exactamente los mismos pasos ni que la captura 105149 ejecutase DummyTaskLogout: 2790 no está en su ventana. El hecho común probado es el emisor nativo 2001 y su exclusión de la captura. Para cerrar la distinción basta una traza de lectura en las entradas `.rsrc 0xFA83D0`, `0xACD400`, `0x8CAD10`, `0x35C670` y `0x35BE90`, registrando modo, índice, tamaño, hilo y orden; no enganchar los thunks.

## 2. Qué hace el servidor al salir un usuario

### 2.1 No confundir personaje, usuario y conexión

| Operación | Resultado probado | Consecuencia de diseño |
|---|---|---|
| [E-x] 2564 → `0x1048DF70` → `0x10872CD0` | Exige actor de personaje tipo 1; llama al inicio de logout con motivo 2. Formato ordinario 5 B vacío. | No prueba desaparición del usuario ni cierre de su sesión; insuficiente para C4. |
| [E-x] 2001 → `0x1089A360` | Toma usuario del contexto; inicia logout con motivo 4. Si no hay usuario, cierra la sesión recibida. | Entrada natural para la salida completa del invitado. |
| [E-x/E] cierre de `DummySessionServer` | Desvincula usuario de sesión; notifica al usuario por vfA8 si procede. | Desconectar el transporte por sí solo no garantiza la destrucción del usuario. |
| [E] 2790 / timers de apagado | Planifican cierre global. | No autorizar desde el invitado remoto. |

[2564](salida_evidencias/capstone_1048df70.txt), [personaje](salida_evidencias/capstone_10872cd0.txt), [2001](salida_evidencias/capstone_1089a360.txt), [cierre de sesión](salida_evidencias/capstone_b8a9000.txt).

**[E]** `ServerNet::OnRecvPacket 0x2773C20` acepta cabecera mínima de **3** bytes, busca la definición del opcode y comprueba sus límites. Con slot negativo resuelve el usuario desde `session+0x90`; slot 0 seleccionaría un personaje. La cola/canal y ese contexto son parte del contrato: no basta llamar directamente al manejador con un puntero que parece válido. [Ghidra](salida_evidencias/decomp_0x2773C20.txt), [capstone](salida_evidencias/capstone_2773c20.txt).

### 2.2 Inicio y callbacks del logout

1. **[E-x]** `0x1089A360` carga `ctx+0` como usuario, obtiene motivo 4 y error global, y llama a `0x2068340` en `0x1089A3AE`. La dirección `.rsrc 0x2068340` es un salto a `.xtls 0xE0D9AF0`, no una función segura que deba engancharse o llamarse desde un worker. Esa función toma el lock del actor, valida estado, escribe motivo/error en `+0x9C/+0x98`, guarda estado anterior, llama vf80 y después vf110. [inicio](salida_evidencias/capstone_e0d9af0.txt).
2. **[E-x]** En `ServerUserActor`, vtable `0x5B257A8`, vf80 `0x2A7D410` → `0x10514F50` marca el estado de logout (`+0x5E = 0x20`). vf110 `0x2A7D4E0` → `0x1051B8D0` ejecuta la fase de componentes por `0x20683F0` → `0xE0DB5A0`, desvincula actor controlado mediante `0x206AEC0` y ejecuta vf120/vf118, que recorren otros callbacks de componentes. [vtable](salida_evidencias/vtable_0x5B257A8.txt), [fase usuario](salida_evidencias/capstone_1051b8d0.txt), [controlado](salida_evidencias/decomp_0x206AEC0.txt).
3. **[E]** La fase de logout de personaje `0x28F26C0` es una entrada `.rsrc` real. Incluye guardado condicionado por flags (`actor+0x96`), procesamiento de dependientes, y logout de otros personajes del usuario cuando el que sale es su principal (`user+0xD0`). Por eso el guardián P6 debe seguir activo hasta terminar la limpieza. No se debe sustituir esta fase por borrar únicamente el actor visible. [Ghidra](salida_evidencias/decomp_0x28F26C0.txt).
4. **[E-x]** vf50 de usuario `0x2A7D540` → `0x1051E5E0` construye **OnLogoutXxxToServerReq 2004**, 19 bytes, con punteros a +3 y +0xB, y lo encola por `0x27745F0`. Su manejador `0x2C48410` → `0x1089C4A0` consume punteros y referencias y llama vf58. **2004 es exclusivamente intraproceso; jamás permitirlo por el túnel.** [productor](salida_evidencias/capstone_1051e5e0.txt), [consumidor](salida_evidencias/capstone_1089c4a0.txt).
5. **[E-x]** vf58 `0x2A7D630` → `0x1051F190` llama a la finalización de componentes y transición de estado; guarda la sesión de `user+0x1F8`, la pone a cero en `0x1051F24E`, actualiza timestamp `+0x200`, emite 2002 de 7 bytes a la sesión guardada por `0x2773630` en `0x1051F2C8` y llama al cierre virtual de esa sesión en `0x1051F2D3`. [capstone](salida_evidencias/capstone_1051f190.txt).

**[S] Alcance:** estas piezas prueban una máquina nativa por fases y una finalización diferida; no constituyen una traza ejecutada del usuario 2. No se ha reconstruido exhaustivamente la inscripción de todos los componentes ni demostrado que la vtable personalizada de sesión del mod permita completar cada fase. Ésa es una condición de la prueba, no un hecho adquirido.

### 2.3 Campo, sectores, observadores y número de usuarios

**[E]** `LeaveField 0x2BBEAB0` tiene entrada real `.rsrc`, prologue contrastado. Sus argumentos relevantes son `FM`, salida de resultado, byte de incluir receptor propio y motivo; no es simplemente `LeaveField(FM)`. Obtiene actor y usuario, toma locks, retira el actor del sector y los observadores afectados, recopila receptores, limpia `FM+0x50` (sector) y `FM+0x10` (campo), y deja `FM+8 = 0x30000`. Publica desaparición mediante `0x2B00380` → `0x10650890` y llama a `RemoveActor` del campo conservado. [Ghidra](salida_evidencias/decomp_0x2BBEAB0.txt), [capstone](salida_evidencias/capstone_2bbeab0.txt).

**[E]** `0x2BBF140` llama a LeaveField en `0x2BBF257` cuando su contador/referencia `FM+0x5A` llega a cero y la condición virtual del actor permite retirarlo. `0x2BBE6D0` también es un callback `.rsrc` con llamada a LeaveField. Esto explica por qué adelantar manualmente la retirada podría saltarse el protocolo de referencias. [2BBF140](salida_evidencias/decomp_0x2BBF140.txt), [2BBE6D0](salida_evidencias/decomp_0x2BBE6D0.txt).

**[E-x]** `RemoveActor 0x2AFF430` es thunk a `0x1064CFF0`. Bajo locks decrementa el total, busca el **usuario del actor** en entradas de 16 B de `field+0x2A0` (longitud +0x2A8), reduce la referencia de esa entrada o la elimina intercambiándola con la última, y reduce `field+0x2B0` si encontró la entrada. No equivale a «poner users a 1». Después llama `CloseIfNoUserActors 0x2AFEA00`. [capstone](salida_evidencias/capstone_1064cff0.txt).

**[E]** `0x2AFEA00`, `.rsrc` real, retorna sin cerrar si `field+0x2B0 >= 1`. Solo la rama de cero limpia y cierra el campo, incluida su marca `+0x4A`. [Ghidra](salida_evidencias/decomp_0x2AFEA00.txt). **[S]** La retirada nativa de todos los actores del invitado preservará el campo si el anfitrión sigue registrado correctamente; se debe demostrar con el contador y la generación, no asumir que un valor «users=1» basta.

### 2.4 Sesión y SAM: ACK no es igual a destrucción completa

**[E-x/E]** `DummySessionServer::Close 0x1437600` → `0xB8A9000` guarda el usuario, pone `session+0x90=0` en `0xB8A9031`, limpia estado criptográfico y puede llamar a vfA8 del usuario. `ServerUserActor::vfA8 0x2A7D790`, `.rsrc` real, compara la sesión recibida con `user+0x1F8`; si coincide, la borra, anota timestamp y notifica componentes. No hay ahí una eliminación inmediata de todos los actores en SAM. [Close](salida_evidencias/capstone_b8a9000.txt), [vfA8](salida_evidencias/capstone_2a7d790.txt).

**[E/E-x]** `ServerUserActor::vf10 0x2A7D280` es `.rsrc` real: recorre y vacía sus slots de personajes bajo lock del SAM y libera éstos y después al usuario mediante la ruta nativa `0x10530180`. Esta última retira el actor del vector SAM (`+0xB8/+0xC0`), limpia colas, pone actorKey a cero en `0x1053024F`, devuelve el índice/generación al sistema de claves y retira su entrada hash. [vf10](salida_evidencias/capstone_2a7d280.txt), [liberación](salida_evidencias/capstone_10530180.txt).

**[S]** Recibir 2002, ver `session+0x90=0` o ver desaparecer el personaje son hitos distintos; ninguno por sí solo prueba que no queden referencias diferidas ni entradas SAM. La reconexión exige una barrera que compruebe los tres niveles. No se permite desreferenciar un puntero ya liberado para comprobar su estado: capturar snapshots mientras exista referencia nativa válida y comprobar después ausencia por clave.

## 3. Qué ocurre hoy en C1g y en el código del túnel

**[V]** En `C:\Emulador CD\backups\logs\cdmp_20261009-0733_c1g.log`:

- 07:40:17.798, línea 617: H2 inserta al usuario 2 en campo `0x5AB3ED17A10`, dueño `0x90100000`; línea 618: 2 usuarios, 3 actores de usuario.
- 07:42:33.973, línea 816: el túnel se detiene por enlace roto, reconexión u overflow; el log no permite elegir entre esas tres causas.
- 07:43:14.181, línea 867: sesión 2 aún tiene usuario `0x5ABBB6AFC80`, clave `0x9010052B`.
- 07:43:14.185, línea 868: **40.212 s después del corte**, anfitrión `0xA0100001` e invitado `0xA010052C` siguen en el mismo campo/sector, 2 usuarios, 4 actores de usuario, generación `0x81020001`.
- 07:44:03.440, línea 929: «user 2 logged in» y «TUNNEL STOPPED» coexisten; los totales acumulados son 90098 paquetes y 53351 retransmisiones P5. No se atribuyen todos esos paquetes al periodo posterior al corte.

[Extracto](salida_evidencias/c1g_salida_log.txt). **[V]** `D:\cdmp-grabaciones\c1g.bin` contiene 13913 registros interpretables, duración 17.140882 s; no contiene 2001/2564/2002/2003/2679. La ausencia en esta grabación ya filtrada no descarta un paquete excluido aguas arriba. [Resumen](salida_evidencias/capture_c1g.txt).

**[E-código]** `src/mod/session2/session2.cpp`, `kill_tunnel` y `tick`: establecen el estado muerto, razón y limpieza de cola temprana, pero no solicitan logout nativo. Ésa es la causa concreta del usuario huérfano. El vaciado de transporte no modifica ni `user+0x1F8` ni el campo ni SAM.

**[E-código]** Obstáculos adicionales, todos necesarios de corregir conjuntamente:

| Punto actual | Supuesto incompatible |
|---|---|
| `src/common/troc_frame.cpp::parse` | Mínimo 5 y longitud en +3; rechaza 2001 y ACK internos de 7 B. |
| `TunnelSender::push`, `decode_batch` | Invocan ese parser: no basta arreglar solo el extremo servidor. |
| `src/common/troc_script.cpp::make_packet` / `make` | `send ... 2001 ...` genera 5 B y no los 3 exigidos por el servidor. |
| `src/mod/session2/guest.cpp::frame_packet` | Requiere mínimo 5; `guest_send` termina el remoto ante fallo. |
| `src/mod/session2/server.cpp::take_frame` | Filtra el ACK personal 2002/2003 con el parser de 5 B. |
| `server_inject`, `load_channels` | Validación normal y lista solo kind 4 o excepción 2319; no existe excepción controlada para 2001. |
| `on_server_recv` / `g_session2` | El paquete marcado se redirige a la sesión global actual: una cola antigua no debe poder alcanzar una nueva época. |
| `guest.cpp` / `g_had_remote` | La política actual impide segunda conexión remota en la misma ejecución. |

No se cambió ninguno de estos ficheros. Los nombres de funciones permiten localizar el código aunque otra conversación desplace sus números de línea.

## 4. Diseño implementable: misma salida nativa, dos disparadores

### 4.1 Estado por usuario y frontera de transporte

**[S]** Mantener por invitado una identidad de transporte autenticada, una época monotónica del mod, el puntero de sesión nativa y la clave de usuario asociada. `ACTIVE → RETIRING → NATIVE_CLEAN → REUSABLE`. `FAILED_QUARANTINED` es salida segura si no se cumple la barrera. El anfitrión queda excluido por puntero/clave de usuario y no por `characterNo`, actualmente compartido.

**[S]** Al despedirse el peer o el P8, validar **exactamente** `D1 07 FF`, cerrar admisión de nuevos Req de juego de esa época y programar una única entrega de 2001. Ante pérdida del túnel, el servidor del anfitrión genera el mismo mensaje canónico tras detectar la rotura; no espera una despedida que ya no puede llegar. La pérdida es decisión del transporte; el logout se procesa en el motor por la cola nativa.

**[S]** Extender explícitamente la representación de paquetes: ordinarios `opcode/slot/u16 len/payload`, y control interno permitido `opcode/slot/campos fijos`, con tabla estricta por dirección/tamaño/estado. Excepciones mínimas: C→S 2001 de 3 B; S→C 2002/2003 de 7 B. Mantener internos 1001/1003 y 2790/2038 locales. **Nunca permitir todos los kind `0x7FFFFFFE`, ni 2004 con punteros.** Validar esto en captura, peer, colas, batch, P2/P3 y P8; no relajar simplemente `size >= 3`.

**[S]** Para P8, consumir 2790 como intención local de cierre de su servidor Shadow o bloquear su retransmisión sin impedir que el flujo posterior produzca 2001. Su reserva de apagado nunca se convierte en apagado del host. Confirmar en traza que el cliente completa su transición al título/escritorio y recibe los ACK personales destinados a él.

### 4.2 Punto/hilo correcto y conservación del host

**[E-código/S]** Reutilizar el carril P2 existente: `push_request` construye PB, marca la solicitud y la introduce en Q0; `on_server_recv` interviene en la entrada real `.rsrc 0x2773C20` y selecciona el `DummySessionServer` invitado. Añadir un mensaje de control tipado para 2001 con canal 0, slot `FF`, tamaño 3 y descriptor **inmutable por época**. No usar un viejo PB cuyo marcador resuelva posteriormente a una nueva `g_session2`. El worker del túnel solo encola la intención.

**[S]** El motor ejecuta su manejador, callbacks, LeaveField, ACK y liberación con sus locks y su orden. **No llamar directamente a `0x2068340`, `0x2AFF430`, `0x8CABB0` ni a cuerpos `.xtls`; no borrar entradas de campo; no forzar `session+0x90=0`.** Esas operaciones aisladas saltarían estado, guardado, actores dependientes y referencias.

**[S]** Mantener P6/guardias de guardado y contenciones de objetos hasta acabar. Los ACK de logout del invitado solo van a su destino; el anfitrión únicamente recibe las desapariciones y actualizaciones de observador que le correspondan. Mantener vivo el objeto/vtable de sesión del mod hasta una barrera demostrada de referencias; no liberar memoria de esa sesión por recibir un ACK ni asignar sesiones nuevas ilimitadamente para evitar la limpieza.

**[S]** En RETIRING detener nuevas entradas de juego y la retransmisión P5 del invitado retirado, pero continuar drenando/liberando los PB nativos del carril P3 y contabilizar el cierre aunque el peer haya desaparecido. Descartar copias de red con cola acotada; no bloquear Q2/Q3 ni dejar que un cliente muerto llene la cola del host. Paquetes tardíos de la época anterior se rechazan antes de tocar el motor.

### 4.3 Barrera y reconexión

**[S]** Registrar baseline del host y comprobar en el hilo/contexto apropiado:

1. La sesión invitada ya no está asociada a usuario; su antiguo usuario emitió la transición `+0x1F8 → 0` mientras aún era válido.
2. Las claves de usuario y personajes retirados ya no resuelven en SAM, y sus callbacks/operaciones diferidas han terminado. Compañeros/controlados se han retirado o transferido por su política nativa; si no, no dar limpieza por hecha.
3. No hay entrada del invitado ni observador residual en campo/sectores. `field+0x2A8/+0x2B0` vuelven al baseline ajustado a los actores legítimos; **no fijar un número de actores de usuario igual a 1**, pues el host puede tener varios.
4. El campo del anfitrión conserva puntero, dueño, generación y estado abierto; el host conserva control, equipo, guardado y acciones. Ningún ACK personal 2679/2002/2003 del invitado llegó a su cliente.
5. Las colas y estructuras del mod de la época retirada están vacías/canceladas, y la sesión personalizada puede reutilizarse según contrato comprobado, no por intuición.

**[S]** Solo después aceptar nuevo login con época y nonce de transporte nuevos; ejecutar el bootstrap nativo necesario de la sesión y 2319, sin heredar paquetes, claves, contadores ni referencias anteriores. El id nativo de sesión `*(session+0x88)` sigue bajo su contrato actual (D-39); **no inventar un id 1/2/3** para resolver la época, pues indexa otras estructuras. La época pertenece al mod.

**[S]** Si cabeceras, identidad o invariantes no coinciden: rechazar nuevas admisiones de opción D, poner al invitado en cuarentena, conservar guardias y dejar al host funcionando. No intentar «reparar» la lista del campo ni cerrar el servidor completo. Un timeout de prueba de 5 s es un presupuesto inicial de diagnóstico, no una garantía del juego; no reinyectar múltiples 2001 hasta demostrar idempotencia en todas las fases. Si falla, no anunciar reconexión soportada.

### 4.4 Dependencias P8 y cuatro jugadores

**[S]** La primera implementación C4 se identifica por sesión/usuario/época y puede preceder a C2, con P6 activo y sin añadir invalidaciones de caché por `characterNo`. Antes de validar P8 con perfiles distintos o anunciar seguridad completa de cachés, resolver C2: una limpieza de caché con clave duplicada podría afectar al host aun retirando correctamente el actor. P6 sigue impidiendo que el logout del invitado escriba sobre el slot guardado del anfitrión, pero no certifica las rutas de caché fuera de su directorio protegido. La persistencia del progreso del invitado es P9 y no queda resuelta por este diseño.

**[S]** Generalizar el estado de sesión global a registro por peer; tres invitados pueden retirarse de manera independiente. Cada control identifica su propia sesión nativa y época; los contadores de campo son del motor. Bloquear solo la época afectada, no todo el grupo. La prueba C4 con un invitado es necesaria pero no prueba interleavings de cuatro jugadores; antes de anunciar aforo, repetir con dos retiradas concurrentes y un tercero activo, verificando que el campo y destinatarios supervivientes siguen correctos.

## 5. Instrumentación de lectura y una prueba cdmp_peer

**[S]** Antes de implementar el flujo, validar bytes de entrada para esta build y registrar en entradas reales `.rsrc`: `0x8CAD10`, `0x35C670`, `0x35BE90` (cliente), `0x2773C20` (recepción), `0x28F26C0` (personaje), `0x2BBEAB0` (LeaveField), `0x2AFEA00` (decisión de cierre), `0x2A7D790` (desvinculación de sesión), `0x2A7D280` (liberación de usuario). Usar mid-hooks de solo lectura que no omitan la función; leer retorno solo con marco de llamada/thread validado. Registrar hilo, época, claves, sesión, campo, motivo y contadores, nunca invocar otra función del motor desde el logger. Las últimas tres entradas y LeaveField tienen prólogo real contrastado; no sustituirlas por sus thunks vecinos.

**[S]** Una prueba lógica C4, tras implementar el soporte interno de control en `cdmp_peer` (por ejemplo una orden tipada `logout_session`, **todavía inexistente**):

1. Cargar una vez al host, tomar baseline de campo/SAM/colas/guardado y ejecutar login C1g del peer. Esperar personaje y campo estable, no dormir un tiempo fijo como criterio de éxito.
2. Despedida voluntaria: emitir tres bytes 2001; esperar ACK 2003/2002 interpretados como siete bytes, y la barrera nativa completa. Host continúa caminando/actuando; no recarga partida ni se cierra su campo.
3. Reconectar con nueva época usando el mismo proceso de host; repetir login, luego finalizar abruptamente el proceso peer/cortar su conexión, sin 2001. Debe producirse **una** intención de logout generada por host y alcanzarse la misma barrera.
4. Tercer login confirma reutilización sin usuario/actor huérfano ni acumulación de colas. Cerrar limpiamente el tercero. Es una prueba de aceptación con tres ciclos y ambos disparadores, no tres reinicios del juego.

**[S] Logs propuestos, no observados:** `logout begin epoch=E user=U trigger=bye|link_lost`; `native 2001 accepted size=3 slot=-1 session=S`; `leave key=K field=F`; `field users=baseline userActors=baseline owner=HOST open=1 generation=G`; `native session detached`; `sam user/children absent`; `logout complete epoch=E`; `reconnect epoch=E+1 ready`. Registrar acuses por destino y cero escrituras de guardado del invitado al slot del host. Un éxito del manejador (`result=0`) no sustituye estos hitos.

**[S] Éxito:** se completan ambos cierres y reconexiones en la misma partida, baseline recuperado, host operativo, campo sin cambio de generación/propiedad, sin actor fantasma, sin desbordamiento y sin escritura de invitado al guardado del host. **Parada:** cerrar campo del host, modificar su sesión/control, recibir su cliente un ACK personal del invitado, escritura de guardado no autorizada, puntero/identidad/cabecera divergente, o no terminar la barrera en el presupuesto. En parada se desactiva admisión D y se conserva evidencia, no se hace limpieza manual agresiva.

## 6. Tres afirmaciones refutables y lagunas

1. **[E/V]** «La ausencia de 2001 en 105149 se explica porque tiene 3 B y `capture` descarta `<5`, aunque se llama PostPacket». Refutación: una traza de PostPacket/salida que muestre un formato diferente o un camino sin ese emisor. Evidencia actual: bytes en `0x8CAD7A–0x8CADB7`, filtro y captura cruda.
2. **[E-código/V]** «C1g deja vivo al usuario porque romper el túnel no dispara logout nativo». Refutación: demostrar una solicitud de logout en otra ruta activa, o limpieza nativa entre el corte y la sonda. Evidencia actual: `kill_tunnel`, sesión y campo 40.212 s después.
3. **[E/E-x/S]** «Usar el logout nativo de usuario es la vía que puede limpiar también campo y SAM sin cerrar el campo del host». Refutación: los callbacks del usuario sintético no alcanzan esas fases, o el contador/propiedad del host está corrupto y la rama de cierre global se ejecuta. Está probado el mecanismo y la condición `userActors >= 1`; **no** su terminación en vivo con la sesión personalizada ni su seguridad con identidades duplicadas.

**[S] Pendiente explícito:** clasificación exacta de rutas UI título/escritorio; secuencia completa de componentes y su relación con los companions concretos; prueba de referencias agotadas antes de reutilizar la sesión; destinatarios personales durante logout; guardado/cachés cuando se separa `characterNo`; concurrencia de retiradas con cuatro jugadores. No se ha ejecutado esta prueba ni se ha aplicado el arreglo.

## Revisión

Revisión cruzada realizada por **el orquestador**, distinto del autor. Consultas independientes al servidor Ghidra de `8CAD10`, `2068340`, `2BBEAB0`, `2AFF430`, `2AFEA00` y `2A21400`; nuevo Capstone sobre la copia y relectura cruda de 105149, además de lectura de `kill_tunnel`, `take_frame`, `load_channels` y `guest::frame_packet`. Evidencia nueva: [revision-orquestador.txt](salida_evidencias/revision-orquestador.txt). El revisor cuenta registros desde cero: sus #356187/#356188 equivalen a #356188/#356189 de la tabla principal.

1. **[E/E-x/V] Intento de refutación: «2001 podría usar el Troc normal de cinco bytes o no pasar por PostPacket». Refutado para la ruta observada.** La nueva decompilación y los bytes de `8CAD10` muestran tamaño3, opcode2001 y llamada `35C670`; la captura cruda confirma ACK2003/2002 de siete bytes, con `04000000` a +3. El handler2002 usa ese u32 como índice para publicar1003 local. La explicación del filtro `<5` se sostiene. No se extiende esta conclusión a todas las rutas UI posibles ni se convierte la ausencia filtrada en una prueba negativa.
2. **[E-código/V] Intento de refutación: «el corte ya podría estar disparando cierre por otra parte o la sonda ser anterior». No aparece tal transición en la ruta actual examinada.** `kill_tunnel` únicamente marca el corte y vacía cola temprana; `tick` deja de inyectar. La sonda de07:43:14.185 es40,212s posterior a07:42:33.973 y aún conserva ambas claves/campo. Esto sostiene la fuga observada, pero el log no permite escoger entre desconexión, overflow y reconexión como disparador concreto de BROKEN.
3. **[E/E-x/S] Intento de refutación: «LeaveField aislado basta o el logout siempre conservará el mundo». Ambas afirmaciones fuertes se rechazan.** Ghidra confirma que LeaveField retira sector/observadores, envía desaparición y después llama RemoveActor; este último mantiene referencias y contadores por usuario antes de CloseIfNoUserActors. La rama de cierre depende de `+2B0`, no solo de `+2A8`. El host se conserva **si** sus referencias/contadores siguen correctos. La ruta asíncrona hasta SAM y la sesión sintética deben medirse; ni2002 ni `result=0` acreditan por sí solos limpieza final. Se mantiene confianza media en reconexión.

**[S] Correcciones integradas en el diseño:** C4 básico no necesita characterNo único para seleccionar al usuario, pero la validación completa de cachés/P8 sí depende de C2/P9. Para P8 añadir al criterio de aceptación que el índice `u32 +3` del2002 coincide con la conexión cliente remota seleccionada antes de entregar el Ack: no confundirlo con el id de cola nativa `*(session+88)=0`, ni remapearlo por el número de invitado. No hay evidencia aquí para inventar un índice nuevo. Mantener2790/2038 bloqueados y2004 intraproceso; el nuevo Ghidra de2A21400 vuelve a confirmar que el apagado se encola globalmente.
