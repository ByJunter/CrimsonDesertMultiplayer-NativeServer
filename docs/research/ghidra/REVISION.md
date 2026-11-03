NO CONECTAR

# Revisión independiente del entorno Ghidra — Crimson Desert 1.0.0.2976

Fecha: 9 de octubre de 2026, UTC−06. Inspecciones de host entre aproximadamente 02:48 y 02:55. Encargo: C:\Emulador CD-opcion-d\docs\PROMPT-REVISION-GHIDRA.md, leído completo. Revisión repartida en tres contrastes independientes: huella, análisis binario y capa de consultas; integración y comprobación de instalaciones por el revisor principal.

**Veredicto sobre el estado actual:** no conectar todavía para uso habitual por la sesión principal. El ejecutable copiado y las instalaciones tienen procedencia comprobada, y el análisis estático aporta resultados útiles. La capa necesita corregir límites, cierre y transporte de argumentos; además, falta una validación operativa compatible con las reglas de esta revisión. Este dictamen no significa que se haya encontrado código malicioso ni un endpoint de ejecución remota.

## Alcance y criterio de solo lectura

Se leyeron el encargo original, SETUP.md, CLAUDE.md y el contexto pertinente de OPCION-D-EMULADOR.md y opcion-d-diario.md. Se revisó todo el código de la capa y de su cadena de arranque antes de considerar ejecutarla.

**No se arrancó Ghidra, Java ni el servidor real; no se registró MCP; no se ejecutó, cerró o modificó el juego; no hubo commits, compilaciones, instalaciones ni descargas de archivos.** Se consultaron páginas oficiales de publicación. Las únicas escrituras de esta revisión son este informe y el script de análisis propio permitido:
<scratch>\revision-ghidra\verify_install.py.

Razón de no ejecutar consultas: la regla estricta dice «No modifiques nada de lo que revisas. Excepción única: escribe tu informe». Aunque el encargo permite herramientas descritas como de solo lectura, las existentes escriben logs e índices del proyecto, y la decompilación sobrescribe exportaciones. El propio SETUP reconoce los índices. Ante esa ambigüedad se aplicó la interpretación prudente que pide el encargo. La lectura de archivos del juego para hashes y fechas se limitó a las comprobaciones expresamente solicitadas en su punto 1.

Por ello, **“Ghidra” en las tablas siguientes significa evidencia conservada en CSV, C exportado y logs, no consultas nuevas al proyecto vivo**. Los comentarios importados desde el repositorio no se cuentan como una segunda prueba independiente. Se indica dónde la evidencia conservada no permite completar el contraste.

## Bloqueantes y condiciones antes de conectar

Son condiciones de aceptación de esta revisión, con la gravedad y el alcance delimitados:

| Prioridad | Qué y dónde | Efecto demostrado o inferido | Corrección y criterio de cierre |
|---|---|---|---|
| Alta: corregir antes de uso autónomo | Límite eludible en scripts\cdmp_mcp.py:163–169 y scripts\Cdmp_Query.java:223–231 | El código Python real genera “disasm 0x1317D40 401 1” para target="0x1317D40 401", count=1. Java toma 401. Números mucho mayores pueden recorrer millones de instrucciones. El recorte a 60.000 caracteres ocurre después de construir y recibir toda la respuesta. | Validar argumentos estructurados en Java, rechazar argumentos sobrantes y limitar instrucciones, referencias, entrada y bytes durante el procesamiento. Verificar rechazo de 401, números negativos y argumentos adicionales sin ejecutar pruebas de agotamiento. |
| Alta: corregir antes de uso autónomo | Limpieza y propiedad del proceso, cdmp_mcp.py:151–162,223–240; server-start.ps1:13–16; server-stop.ps1 | La prueba aislada con JSON “null” lanza AttributeError y omite shutdown. El arranque acepta cualquier respuesta al ping y puede confundir un servidor ajeno con propio. Timeout y parada no garantizan que Java haya terminado. | Validar objetos JSON; try/finally para limpieza; identificar instancia y árbol de procesos realmente creados; comprobar respuesta, PID y salida con timeout. Probar EOF normal, entrada errónea y fallo de arranque bajo supervisión. |
| Alta en la ruta headless; no es RCE del MCP/TCP | Construcción de comandos en scripts\_env.ps1:23–34, usada por cdmp-query.ps1 cuando no hay servidor | Las comillas internas y expansiones de cmd no están correctamente escapadas. Un argumento con metacaracteres puede convertirse en sintaxis de shell. No se ejecutó una carga de prueba. Las consultas MCP no pasan por esta ruta. | Eliminar la interpolación de consultas en código cmd; usar transporte estructurado validado. Hasta entonces, no usar wrappers con argumentos procedentes de texto libre. |
| Pendiente de aceptación operativa | Exportaciones anteriores a las correcciones finales; no hubo consultas nuevas | No están comprobados el estado vivo, los tiempos de las cinco herramientas, el cierre de un Java realmente iniciado ni el cuerpo de 0x143A580 por Ghidra. | Tras las correcciones, acordar una prueba que permita sus escrituras incidentales o preparar una vía que preserve los archivos auditados; comparar las nueve comprobaciones, reexportar coherentemente y medir arranque/consultas/parada. |

**Condición adicional de red y documentación:** revisar las dos reglas persistentes de cortafuegos para Java detalladas en §1; no son necesarias para loopback. Corregir o justificar su alcance y dejar constancia. Fijar literalmente 127.0.0.1 en el servidor y registrar la dirección real. Añadir autenticación por instancia o documentar expresamente que cualquier proceso local puede consultar y apagar el servidor. No se cambiaron reglas ni configuración durante esta revisión.

## 1. Huella y ausencia de cambios indebidos

### 1.1 Ejecutable, carpeta del juego y crashpad

Comprobación independiente mediante Get-FileHash -Algorithm SHA256, tamaños y fechas:

| Archivo | Bytes | SHA-256 |
|---|---:|---|
| D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe | 384.521.624 | 57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7 |
| D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe | 384.521.624 | 57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7 |
| C:\Emulador CD\backups\crashpad\crashpad_handler.exe | 670.104 | 222EC1F0B4ECBB8FEC3F724D1CC29D88FD8148C752EAF8607DC04792898FE6EC |

La copia del ejecutable es idéntica al original actual. PE timestamp 0x6AB28F00, image base 0x140000000. La copia se creó el 8/10 a las 23:06:53; ambos EXE conservan LastWriteTime del 3/10 a las 08:04:44. El backup de crashpad es anterior a Ghidra, creado el 8/10 a las 22:35:50.

**crashpad_handler.exe sigue ausente de bin64 del juego y presente en el backup; no se repuso.**

Se enumeraron 423 archivos del juego y se filtraron CreationTime o LastWriteTime en la ventana 8/10 23:06 → 9/10 02:43, tomada de SETUP y logs. Aparecen dos:

| Archivo | Bytes | LastWriteTime del 8/10 |
|---|---:|---|
| bin64\cdmp.asi | 3.941.376 | 23:30:13.760 |
| bin64\cdmp.pdb | 37.400.576 | 23:30:13.763 |

Las horas coinciden con trabajo concurrente de Session2 y el commit D-40, pero **no permiten atribuir la escritura a una conversación concreta**. Tampoco existe un inventario anterior que permita demostrar la ausencia de altas, bajas o cambios con fechas conservadas. No sería correcto certificar “ningún archivo cambió durante la preparación”.

### 1.2 Git y otros checkouts

Método: git --no-optional-locks -c core.fsmonitor=false -C <ruta> status --short; branch --show-current; log c502bde..HEAD --name-status. Solo lectura; algunas consultas requirieron ejecutarse en host porque el sandbox denegaba el acceso.

| Checkout | Estado observado |
|---|---|
| C:\Emulador CD-opcion-d | Rama opcion-d, HEAD 4bb8481. Único cambio sin seguimiento: docs/research/ghidra/. Se mantuvo así al cierre de la inspección de host. |
| C:\Emulador CD | Rama main, HEAD 649cf29. Seis archivos modificados y tres nuevos, todos con LastWriteTime del 5/10 entre 22:58 y 22:59, anteriores al trabajo Ghidra. |
| <onedrive>\Documentos\ChatGPT\Crimson Desert Multiplayer | Rama main, HEAD 24678f9. Único archivo sin seguimiento: docs/PROMPT-GHIDRA.md, creado/modificado el 8/10 a las 19:39:57, anterior a la preparación. |

Los seis modificados en C:\Emulador CD son CMakeLists.txt, src/common/clock_mapper.h, src/mod/game/offsets.h, src/mod/net/local_capture.cpp, src/mod/net/local_capture.h y src/mod/net/remote_puppet.cpp. Los tres nuevos son src/common/inject_clock.h, src/mod/game/game_clock.cpp y src/mod/game/game_clock.h. No se modificaron durante esta revisión ni se atribuyen a Ghidra.

Hay siete commits posteriores a c502bde:

| Commit | Fecha/hora local | Contenido |
|---|---|---|
| 4fd4665 | 8/10 22:54:42 | Save guard D-38/configuración B2 |
| 4d073bc | 8/10 22:55:03 | Documentación B1 |
| 75ebb2b | 8/10 22:59:30 | B2 sin Ack |
| 6d9c684 | 8/10 23:02:03 | B2, bloqueo al regresar al título |
| d99027d | 8/10 23:12:25 | Cola diferida Session2, D-39 |
| 861c9e0 | 8/10 23:30:34 | Cuenta all-ones/Session2, D-40 |
| 4bb8481 | 9/10 02:45:17 | Encargo PROMPT-REVISION-GHIDRA.md |

Ninguno incorpora docs/research/ghidra/. Su contenido corresponde al trabajo principal y al encargo; Git registra el mismo autor “ByJunter” y no identifica qué conversación lo produjo. No se puede afirmar “cero commits desde c502bde”, ni imputar esos siete a la preparación de Ghidra.

### 1.3 Ubicación y huella fuera de D:

D:\RE y D:\Tools son directorios normales, sin reparse points; creados el 8/10 a las 23:06:52 y 23:07:11. El proyecto principal está fuera de la carpeta compartida.

Sin embargo, no es literalmente cierto que todo esté en esas dos carpetas. Se encontró:

<scratch>\testproj

Contiene 20 archivos, 16.246.370 bytes: copia de msvcp140.dll, proyecto Test.gpr/Test.rep, logs y CSV. Creado desde las 23:15:05; D:\RE\CrimsonDesert_2976\logs\server.log:1 documenta ese proyecto de prueba a las 23:20:41, puerto 18491. SETUP no lo incluye en su inventario.

También existe %TEMP%\ghidra, vacío en la inspección. Los logs históricos señalan preferencias y application.log en AppData\Roaming\ghidra, pero esa carpeta no existe actualmente. No se confunde una ruta mencionada en un log con un archivo persistente presente.

### 1.4 Sistema y cortafuegos

Estado actual comprobado:

- JAVA_HOME de usuario, máquina y proceso: no definido.
- PATH de usuario/máquina: sin la instalación D:\Tools de Java/Ghidra/Temurin.
- Cero servicios coincidentes por nombre, descripción o ruta.
- 189 tareas programadas inspeccionadas: cero coincidencias por nombre/ruta/acción.
- Cero entradas relacionadas en Run/RunOnce de HKCU/HKLM y Run WOW6432Node; Startup de usuario y común solo contienen desktop.ini.
- Es una comprobación del estado actual, no una prueba de ausencia histórica de cambios transitorios.

**Sí hay dos reglas persistentes no documentadas en SETUP:**

| Propiedad | Valor |
|---|---|
| Programa | D:\tools\jdk-21.0.12.1+1\bin\java.exe |
| Nombre visible | OpenJDK Platform binary |
| Enabled / Direction / Action / Profile | True / Inbound / Allow / Private |
| Protocolos | Una regla TCP y otra UDP |
| Puertos y direcciones locales/remotas | Any |
| Almacén | Local |
| ID TCP | TCP Query User{52F0508E-B9BA-4293-90AB-02674FE7F92F}D:\tools\jdk-21.0.12.1+1\bin\java.exe |
| ID UDP | UDP Query User{CDE80E0F-9677-40DE-AE0D-E81905980CB2}D:\tools\jdk-21.0.12.1+1\bin\java.exe |

Método: Get-NetFirewallApplicationFilter, Get-NetFirewallRule, Get-NetFirewallPortFilter y Get-NetFirewallAddressFilter. La consulta de eventos 2004/2005 de Windows Firewall en la ventana no devolvió registros; **fecha y autoría no demostradas**. Las reglas no prueban que este servidor escuche en LAN, pero autorizan conexiones entrantes de red privada a programas ejecutados con ese Java. No se tocaron.

### 1.5 Registro MCP y procesos

Inspección de configuración sin volcar secretos: .claude.json no tiene referencias al MCP/proyecto/rutas y su mcpServers raíz está vacío; .claude/settings.json tampoco contiene referencias. No existe claude_desktop_config.json ni .mcp.json en los tres repositorios inspeccionados.

Snapshots de host a las 02:48:56, 02:50:25 y 02:54:52: sin Java/javaw o Python MCP relacionado, y sin listener 18490. Se usaron Get-CimInstance Win32_Process y Get-NetTCPConnection; los primeros resultados vacíos dentro del sandbox no se tomaron como evidencia.

No se inició ni se detuvo ningún proceso Ghidra. Las fechas y tamaños de índices y bases coinciden entre inicio y cierre de nuestra inspección: idata/~index.dat 02:41:26 (119 bytes), versioned/~index.dat 02:41:26 (63 bytes); db.9.gbf 00:07:42 y db.8.gbf 00:03:37, ambas 2.016.788.480 bytes. Esto comprueba metadatos sin cambios recientes; no equivale a hashes históricos de las bases.

## 2. Procedencia de las instalaciones

### 2.1 ZIP conservados y hashes oficiales

| Archivo local | Bytes | SHA-256 calculado |
|---|---:|---|
| D:\Tools\_dl\ghidra\ghidra_12.1.4_PUBLIC_20260921.zip | 569.649.598 | DDAC49F903DA9D5BAC833E5CC79395098B9C33CFD3279BE5F31BD00387D2D4DB |
| D:\Tools\_dl\jdk\OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip | 205.073.461 | F9D6E191AB098C0D416E7D588A24420A8621CD2F4720DAB2459B8B7B2D2D8B4E |

Ambos coinciden con los publicados oficialmente:

- [Ghidra 12.1.4, publicación oficial de NSA](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build), publicada el 21/9/2026; hash del ZIP indicado en la publicación.
- [Temurin 21.0.12.1+1, publicación oficial de Adoptium](https://github.com/adoptium/temurin21-binaries/releases/tag/jdk-21.0.12.1%2B1); digest del ZIP Windows x64 contrastado en la [lista oficial de archivos de esa publicación](https://github.com/adoptium/temurin21-binaries/releases/expanded_assets/jdk-21.0.12.1%2B1).

La descarga directa del pequeño .sha256.txt de Adoptium no respondió correctamente en el navegador de investigación; se verificó el digest del ZIP en la lista oficial de GitHub, no el hash del propio archivo .sha256.txt ni solamente una copia local.

Versiones leídas en los archivos de instalación:

- D:\Tools\ghidra_12.1.4_PUBLIC\Ghidra\application.properties: 12.1.4 PUBLIC, build 2026-Sep-21 1613 UTC, Java mínimo 21.
- D:\Tools\jdk-21.0.12.1+1\release: Eclipse Adoptium, 21.0.12.1+1-LTS, Windows x86_64.

### 2.2 Instalación contra contenido del ZIP

El script propio verify_install.py abrió los ZIP sin extraerlos, calculó SHA-256 de cada entrada y del archivo instalado correspondiente y enumeró archivos adicionales:

- **JDK:** 490 archivos idénticos; ninguno cambiado, ausente o adicional.
- **Ghidra:** 5.216 archivos idénticos; ninguno ausente; tres modificaciones y tres copias .orig adicionales.
- Las tres copias .orig coinciden con los originales del ZIP.

Las únicas modificaciones de Ghidra son:

| Archivo | Diferencia exacta respecto al ZIP |
|---|---|
| ghidraRun.bat | MAXMEM_DEFAULT vacío → 22G |
| support/analyzeHeadless.bat | MAXMEM_DEFAULT=2G → 22G |
| support/launch.properties | JAVA_HOME_OVERRIDE vacío → D:/Tools/jdk-21.0.12.1+1 |

Coinciden con la configuración documentada; no se encontraron otras diferencias en el contenido instalado. La aprobación histórica de descargar/instalar exigida por el encargo original no puede acreditarse mediante estos archivos: no se revisó la conversación preparadora. La procedencia binaria sí queda contrastada.

## 3. Fiabilidad del análisis

Método independiente: Python con -B y capstone 5.0.7; se leyó completo el lector <scratch>\rpt0\pe.py y se instanció PE con la ruta explícita de la copia D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe. No se importaron d.py/xref.py, cuyo código abre por defecto el EXE del juego. Las direcciones siguientes son RVA; VA = 0x140000000 + RVA.

### 3.1 Nueve comprobaciones críticas

| Dirección | Ghidra: evidencia conservada revisada | Capstone sobre la copia | ¿Coincide? |
|---|---|---|---|
| 0x1317D40 | exports/decomp/0x1317d40.c:10,30–31: void y retorno si primer byte es cero. functions.csv:42385: entrada/tamaño 0x1CB. | Prólogo exacto 48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57; cmp byte [rcx],0 en 1317D60; je 1317EF0 en 1317D63; ret en 1317F0A. Compatible con void; RAX residual no prueba un retorno válido. | Sí en la semántica C conservada. Prólogo/destino exactos revalidados solo por capstone, no por consulta nueva a Ghidra. |
| 0x2356420–0x2356439 | 0x2356270.c contiene la llamada al singleton, carga +0x1988, llamada al reportero y aumento del contador. Se contrastó código C, no sus comentarios importados. | call 1318E70; lea r8,[rsp+30] en 6425; mov rdx,r14 en 642A; mov rcx,[rax+1988] en 642D; call 1317D40 en 6434; inc byte [6D9CCE8] en 6439. | Sí en flujo y argumentos. Registros y direcciones exactas: capstone. |
| 0x1316226–0x131623B | imports.csv:412 asocia sentry_init, IAT 51EC950, a la llamada 1316226. No se conserva el desensamblado Ghidra de lo que sigue. | call [51EC950]; test eax,eax en 622C; jne 1317B37 en 622E; carga de RDI en 6234; mov byte [rdi],1 en 623B. | Parcial: llamada/importación confirmada por ambos; secuencia posterior solo por capstone. |
| 0x131602D | imports.csv:101 asocia GetFileAttributesW, IAT 51EB418, con 131602D entre nueve usos. | Confirma call; compara con −1 y prueba atributo 0x10; sentry_options_set_handler_pathw se llama después, en 131605C. | Parcial: llamada confirmada por ambos; orden posterior solo por capstone. |
| 0x44FD30 | 0x44fd30.c: entrada char*, bucle byte a byte, buffer wchar_t y wcsncpy_s. | cmp byte [rcx+r8],0 en 44FDB3; call 1420F60 en 44FDC2; el destino usa r8d en 1420F77. | Conversión coherente; el C exportado omite un tercer argumento real. Véase §3.5. |
| 0x2774076–0x2774087 | 0x2773c20.c:268–284: sin usuario, uVar7 = *(undefined4 *)param_3[0x11]; almacena +0x54. | 277407E: mov rax,[r15+88]; 2774085: mov eax,[rax]; 2774087: mov [r14+54],eax. | Sí: doble indirección. No es un u32 alojado directamente en sesión+0x88. |
| 0x143A580 | functions.csv:45045: cdmp_DeferredQueue_Dispatch, entrada/tamaño 0x16D; 0x2773c20.c:308–310 muestra llamada con cuatro argumentos. No existe C ni disasm conservado del cuerpo. | test r9d,r9d en A5A1; je en A5A4 hacia A609; clave no cero: and ecx,0xFFFFF en A5CC, duplica índice en A5D2 y lee [rax+rcx*8] en A5D8, sin comparación de límites previa en esta ruta. | Cuerpo no contrastado con Ghidra. El encargo sitúa imprecisamente la cola común: A5A4 es la instrucción de salto; destino A609. |
| 0x27748D0 | functions.csv:95830 identifica función contenedora 2774730, cdmp_ServerNet__ProcessAsyncRequest, tamaño 0x3ED. La etiqueta del names_report:1672 no prueba el cmp. | mov rcx,[rsi+88] en 48C6; mov eax,[rdx+54] en 48CD; cmp [rcx],eax en 48D0; jne 2774A80. | Instrucción no contrastada con Ghidra conservado. Capstone confirma comparación tras cargar el puntero de sesión+0x88. |
| 0x2773C20 / 0x35CA00 / 0x35C670 | functions.csv:95827,12954,12952 y los tres C exportados: ServerNet::OnRecvPacket, ClientNet::OnRecvPacket, ClientNet::PostPacket. | Inicios independientes en .pdata; prólogos coincidentes con los arrays de bytes actuales del mod. | Sí en entradas y correspondencia con las fuentes. Los nombres semánticos son anotaciones de investigación, no símbolos originales del fabricante. |

### 3.2 Secciones, permisos y exclusión

La tabla PE real coincide con exports/sections.csv en las 12 secciones y sus RVA. La diferencia intencional de permisos es .xtls:

| Sección | RVA | VirtualSize PE | Tamaño bloque Ghidra exportado | PE → Ghidra |
|---|---|---|---|---|
| .rsrc | 0x1000 | 0x51EA000 | 0x51EA000 | R-X → R-X |
| .xdata | 0x51EB000 | 0x16A8000 | 0x16A8000 | R-- → R-- |
| .rdata | 0x6893000 | 0x854000 | 0x854000 | RW- → RW- |
| .00cfg | 0x70E7000 | 0x2CD000 | 0x2CD000 | R-- → R-- |
| .tls$ | 0x73B4000 | 0x3000 | 0x3000 | R-- → R-- |
| .tls | 0x73B7000 | 0x30000 | 0x30000 | R-- → R-- |
| .xtext | 0x73E7000 | 0x1E2000 | 0x1E2000 | R-- → R-- |
| .xtls | 0x75C9000 | 0xF9466E2 | 0xF946800 | RWX → RW- |
| .text | 0x16F10000 | 0x1C3986 | 0x1C3A00 | R-- → R-- |
| .shared | 0x170D4000 | 0x248 | 0x400 | RW- → RW- |
| .impdata | 0x170D5000 | 0x8DDC | 0x8E00 | R-- → R-- |
| .trace | 0x170DE000 | 0x2CC250 | 0x2CC400 | R-- → R-- |

Ghidra añade Headers y tdb. Los tamaños diferentes se explican por tamaño bruto alineado cuando supera VirtualSize; no demuestran corrupción.

**.rsrc contiene código real y está analizada.** El directorio de recursos del PE apunta a RVA 0x73B7000, tamaño 0x2F388, dentro de .tls; el nombre .rsrc no fue tomado como ubicación de recursos.

Cdmp_PreAnalysis.java quita X a .xtls y activa Restrict Disassembly to Executable Memory. logs/export.log:112,118–120 registra 17.124.735 instrucciones en .rsrc y cero en .xtext/.xtls/.text. Es evidencia histórica conservada, no un recuento nuevo del proyecto.

Cdmp_RevertXtls.java devuelve X según la cabecera PE y desactiva la restricción. **No deshace RTTI, nombres ni todo el análisis.** La frase de SETUP:141–142 que presenta backup/project_tras_analisis como estado anterior a todo el análisis es incorrecta: ese backup es posterior. No se ejecutó la reversión.

### 3.3 Recuentos y coherencia de exportaciones

| Dato | Resultado independiente |
|---|---|
| Funciones de functions.csv | 221.113: .rsrc 200.707; .xtls 20.405; .xdata 1. |
| Entradas .xtls | Las 20.405 tienen tamaño 0x1: marcadores, no cuerpos analizados. |
| Entrada espuria .xdata | 0x5E30A78, pa::BaseLock::RTTI_Complete_Object_Locator: dato tratado como función; ya advertido por SETUP. |
| Cadenas | 88.354 filas; 60.815 con referencias. |
| RTTI | 16.339 filas: 15.931 nombres distintos de clase, 15.165 vftables. Varias tablas por clase explican la diferencia. |
| Importaciones | 957 símbolos exportados. PE: 961 slots = 854 normales + 107 diferidos de Vulkan. Cuatro filas agrupan dos punteros: WSAStartup, closesocket, WSAGetLastError y XInputSetState. No faltan cuatro importaciones. |
| names_report | 7.512 filas, con los estados detallados debajo. |
| Instrucciones | 17.124.735 respaldadas por export.log:110,112 y el recorrido de Listing en Cdmp_Inspect.java; no recontadas en vivo. |
| Símbolos cdmp_ | names.log:55 registra 7.573 antes de correcciones; no existe CSV íntegro de símbolos para recontar el estado final. |
| Nombres de función cdmp_ | functions.csv contiene 2.865 que empiezan por cdmp_, o 2.869 que lo contienen. No reproduce directamente las 2.866 citadas como finales. |

Estados de names_report: 2.737 funcion_renombrada; 175 etiquetas en funciones previamente nombradas; 1.017 etiquetas de código; 422 vtables; 339 datos; 434 cadenas; 1.115 vtables Troc y 1.115 instancias Troc; 148 x_code_sin_instruccion; 7 x_func_en_xtls; 3 x_func_no_es_inicio. Suman 7.512 y concuerdan con SETUP.

**CSV anterior a correcciones:** functions.csv, escrito a las 00:03:47, conserva nombres que SETUP y names.log:101–104 dicen haber reparado después:

- 0x359B50: AK::TempAlloc::cdmp_ClientCharacterControlActorComponent__vf73.
- 0x359BA0: AK::WriteBytesCount::cdmp_NwVirtualAsyncSession__vf6_ReturnFalse.
- 0x3600B0: AK::WriteBytesCount::cdmp_NwVirtualSyncSession__vf9_Socket.
- Thunk 0x41E1DE0: nombre heredado del primero.

El log respalda que se ejecutó una corrección de tres funciones, pero solo una consulta viva aclarará los nombres actuales y la propagación al thunk. No usar el CSV como fotografía final plenamente sincronizada. No se regeneró.

export.log:110 indica functions(total)=222019 usando FunctionManager.getFunctionCount(), mientras el exportador recorre funciones internas con getFunctions(true). Son universos distintos; la diferencia es compatible con funciones externas, cuyos 906 registros no se enumeraron en vivo. No se presenta como corrupción ni se oculta la discrepancia.

### 3.4 Muestras de nombres y mensajes

Los diez nombres siguientes tienen inicio en .pdata, entrada coincidente en functions.csv y respaldo en la fuente actual del worktree C:\Emulador CD-opcion-d. En Ghidra se escriben con prefijo cdmp_ y separadores __:

| RVA | Nombre del equipo | Fuente actual |
|---|---|---|
| 0x35CA00 | ClientNet::OnRecvPacket | src/mod/game/offsets.h:72 |
| 0x35C670 | ClientNet::PostPacket | src/mod/game/offsets.h:71 |
| 0x2773C20 | ServerNet::OnRecvPacket | src/mod/session2/offsets_s2.h:51 |
| 0x1437740 | NwVirtualAsyncSession::Send | src/mod/session2/offsets_s2.h:54 |
| 0x14380B0 | NwVirtualSyncSession::Send | src/mod/session2/offsets_s2.h:114 |
| 0x2A759B0 | ToNearExceptOneControl::vf1 | src/mod/session2/offsets_s2.h:72 |
| 0x1317D40 | SentryReporter | src/mod/session2/report_guard.cpp:19 |
| 0x9E89C0 | MinimapIconDecide | src/mod/game/offsets.h:119 |
| 0x2217290 | TargetComponent::SetCurrentTarget | src/mod/net/aggro_test.cpp:35 |
| 0x44FD30 | Utf8ToUtf16 | src/mod/session2/report_guard.h:21–22 |

Esto verifica colocación y procedencia, no rederiva por completo cada significado semántico. Fuera de la muestra: 0x349EA10 sí es una entrada, pero TreeComponent::SetBend solo tiene como respaldo el encargo original, según known_names_repo.csv:2560; sigue sin verificar semánticamente.

Cinco filas de C:\Emulador CD-opcion-d\docs\research\data\troc_messages_2976.csv contrastadas con u64(vtable+0x10)−0x140000000 en la copia y con nombres/RTTI exportados:

| Mensaje | ID decimal | Vtable RVA | Handler slot 2 RVA | Clase exportada y línea de rtti_classes.csv |
|---|---:|---|---|---|
| ActorLoadDataAck | 2005 | 0x5647900 | 0xCB25A0 | pa::TrocTrActorLoadDataAck, 12886 |
| MoveActorAck | 2697 | 0x560B7F0 | 0xB621F0 | pa::TrocTrMoveActorAck, 13369 |
| VaryHPAck | 2147 | 0x560B150 | 0xB75020 | pa::TrocTrVaryHPAck, 13960 |
| ActivateSpawningPoolReq | 2424 | 0x5B24678 | 0x29C3530 | pa::TrocTrActivateSpawningPoolReq, 12883 |
| StartActionAck | 2991 | 0x560BBF0 | 0xB57720 | pa::TrocTrStartActionAck, 13731 |

Los cinco slots coinciden. El handler de ActorLoadDataAck es un thunk hacia .xtls: localizarlo correctamente no permite entender el cuerpo virtualizado. AttackTargetAck no aparece en ese CSV; no se inventó una asociación.

### 3.5 Pseudocódigo y delimitación

**0x1317D40:** el C conservado es coherente con el retorno temprano y tipo void. El comentario de firma guardada dice undefined cdmp_SentryReporter(); el decompilador infiere otra firma al trabajar. No convertir undefined en un contrato de llamada.

**0x2773C20:** el C conservado reproduce la doble indirección de sesión+0x88, la escritura en +0x54 y la llamada a DeferredQueue_Dispatch con cuatro argumentos. Sigue conteniendo tipos genéricos e inferidos.

**0x143A580:** no hay decompilado conservado y no se generó uno nuevo por la restricción de escritura. Su lógica se comprobó con capstone; la decompilación solicitada queda pendiente. No se ha fabricado pseudocódigo atribuyéndolo a Ghidra.

**Error concreto en 0x44FD30:** exports/decomp/0x44fd30.c:42 imprime FUN_141420f60(local_1028,param_1), con dos argumentos. El llamador deja la longitud en R8 y el destino lee r8d en 0x1420F77. Hay un tercer argumento real omitido por la firma inferida. La consulta viva debe determinar si el estado actual conserva ese defecto.

Estimación de delimitación de .rsrc: se compararon todos los registros de excepciones del PE con inicios exportados. Hay 218.226 RUNTIME_FUNCTION en .rsrc; 46.018 tienen UNW_FLAG_CHAININFO y representan fragmentos encadenados. **Los 172.208 registros restantes tienen todos su inicio en functions.csv.** Es respaldo amplio de cobertura de funciones con unwind, sin demostrar que cada cuerpo, función hoja o fragmento esté delimitado perfectamente.

Las cuatro etiquetas .rsrc del names_report marcadas “sin instrucción” son direcciones de fuente erróneas, no huecos demostrados en Ghidra:

| RVA etiquetada | Decodificación independiente |
|---|---|
| 0x8B7D0E | Dentro de and r9d,0xFFFFF, iniciado en 0x8B7D0D; se omitió el prefijo REX 41. |
| 0x1323843 | Dentro de sub rsp,38, iniciado en 0x1323840. El cmp [rcx+1990],0 empieza en 0x1323844. |
| 0x235642E | Dentro del mov rcx,[rax+1988] iniciado en 0x235642D. |
| 0x277402C | Dentro del jmp iniciado en 0x277402A; la comprobación posterior empieza en 0x2774033. |

Fuentes: cdmp_names_report.csv:330,779,1528,1653. Las otras tres etiquetas A170B0, C06CC0 y 143B8B0 están dentro de funciones existentes; no sirven como entradas de hooks inline.

No se encontró evidencia de un fallo general de delimitación en .rsrc. Persisten casos aislados, tipos inferidos y anotaciones incorrectas. El tamaño exportado es getBody().getNumAddresses(), no necesariamente “final menos entrada”: una diferencia de tamaño frente a un intervalo unwind no prueba por sí sola una función partida.

## 4. Capa MCP y herramientas

Rutas base de esta sección: D:\RE\CrimsonDesert_2976\scripts y su copia C:\Emulador CD-opcion-d\docs\research\ghidra\scripts.

Se leyeron completos cdmp_mcp.py, Cdmp_Query.java, todos los PS1 y la cadena analyzeHeadless.bat → launch.bat → launch.properties. También se inspeccionaron los scripts de análisis/nombres/RTTI/reversión para distinguirlos de la ruta de consulta.

Hay 30 archivos en scripts de D: y 29 en la copia del repositorio. **Los 29 comunes son idénticos por SHA-256**; el adicional esperado es data/known_names_apply.csv.

- cdmp_mcp.py: SHA-256 9218CAD035805E3F30B9EB5AC9B470C8869CB21710DFBD3815D2F9813A4D28FA.
- Cdmp_Query.java: SHA-256 93F1443C9F65F1DA7222D4B9E95797E2EE162170CBCDE18E8AA6FC35379AEB25.

### 4.1 Operaciones y red

El dispatcher Java, líneas 100–115, admite decomp, xrefs, strings, vtable, class, imports, func, disasm, symbols y ping; shutdown se maneja en línea 76. **No se encontró eval, intérprete ni endpoint que ejecute comandos/scripts arbitrarios recibidos por la red.** Tampoco operaciones de renombrar, comentar, guardar o reanalizar el programa en esas rutas. El arranque lleva -process -noanalysis -readOnly.

Los scripts que sí modifican nombres/análisis están presentes, pero no son invocables por ese dispatcher. El subprocess de Python recibe nombres fijos de scripts y no los argumentos de consulta como código de shell. El riesgo del wrapper headless de §Bloqueantes no debe presentarse como ejecución remota del MCP.

MCP usa stdio y conecta al literal 127.0.0.1; CDMP_PORT permite variar el puerto. Java usa InetAddress.getLoopbackAddress(), que garantiza loopback pero puede elegir ::1. Se contrastó su implementación en lib/src.zip del JDK, java.net.InetAddress y Inet6AddressImpl. El log imprime siempre 127.0.0.1 aunque la dirección elegida fuera IPv6. **No hay evidencia de bind a 0.0.0.0 ni de exposición del servidor actual a LAN.** No se comprobó un listener en ejecución.

No hay autenticación. Cualquier proceso local con acceso al puerto puede consultar o enviar shutdown. Ping solo informa del nombre del programa; no verifica hash, build, instancia ni propietario. Loopback puede ser suficiente para un equipo de único usuario con todos sus procesos de confianza, pero no constituye autenticación.

La capa propia usa biblioteca estándar Python y APIs de Ghidra, sin dependencias adicionales descargadas. No se encontraron descargas ni conexiones externas explícitas en su código. No se realizó captura de tráfico del framework completo.

### 4.2 Escrituras y límites de “solo lectura”

| Escritura o efecto | Evidencia |
|---|---|
| exports/decomp/<rva>.c, crea carpeta y sobrescribe archivo | Cdmp_Query.java:192–195 |
| Archivo arbitrario out=<ruta>, solo al arrancar el script | Cdmp_Query.java:46,57 |
| Directorio decompdir=<ruta>, solo al arrancar el script | Cdmp_Query.java:47,193–195 |
| %TEMP%/cdmp_h_<guid>.cmd, normalmente borrado en finally | _env.ps1:32–34 |
| %TEMP%/cdmp_q_<guid>.txt, borrado al terminar normalmente | cdmp-query.ps1:14–19 |
| logs/server.log, append | server-run.ps1:3–5,9–10 |
| Ruta indicada por CDMP_MCP_LOG, si se configura | cdmp_mcp.py:27,38–44 |
| Preferencias/localización del JDK; logs/cachés e índices del proyecto | LaunchSupport -save en launch.bat:157,163; launch.properties:108–148; SETUP y logs |

out= y decompdir= no se aceptan como opciones de red: son argumentos locales de arranque. El nombre de salida de decomp se deriva de la dirección numérica, sin escape de ruta mediante target. exports y exports/decomp no son reparse points. CDMP_MCP_LOG no estaba definido en el entorno observado.

Por tanto, la afirmación de SETUP “lo único que escribe es el .c” es incompleta. -readOnly protege el programa de Ghidra frente a guardados, pero no impide las escrituras incidentales anteriores.

### 4.3 Pruebas aisladas realizadas

Se cargó el módulo Python original sin activar su entrada principal. Socket y subprocess quedaron bloqueados antes de la carga; ghidra/log se sustituyeron por funciones en memoria; después se bloqueó apertura de archivos y se usó StringIO. Python -I -B, sin archivos nuevos, sin red ni Java.

| Prueba | Resultado observado |
|---|---|
| call_tool real con target="0x1317D40 401", count=1 | Genera exactamente disasm 0x1317D40 401 1. La lectura equivalente al parser Java toma 401. |
| main real con entrada null y started_here=True simulado | AttributeError: 'NoneType' object has no attribute 'get'; llamadas shutdown=0; started_here permanece true. |
| Control de efectos laterales | Cero intentos de las operaciones de red/procesos/archivos bloqueadas durante las pruebas. |

Estas pruebas demuestran la formación de la consulta y el defecto de limpieza. No pretenden haber medido 401 instrucciones en Ghidra ni observado un Java abandonado en vivo.

### 4.4 Arranque, cierre y otros avisos de uso

server-start.ps1 inicia PowerShell oculto, que ejecuta server-run.ps1 y headless; este último fija heap 12G. El cierre usa shutdown sin esperar la salida del árbol de procesos. La propiedad es un booleano started_here, vulnerable a carreras entre clientes. Send-CdmpServer carece de timeout de lectura, aunque la conexión sí tenga límite; un servicio ajeno que acepte sin responder puede bloquear al wrapper.

**Las consultas headless sin servidor no fijan 12G:** heredan 22G del analyzeHeadless.bat modificado salvo variable de entorno. SETUP las presenta como consultas de 12G; corregir esa diferencia.

Otros límites relevantes:

- disasm devuelve instrucciones ya definidas; si la dirección no contiene una, puede saltar a la siguiente. No sirve por sí solo para probar que el byte solicitado era un inicio.
- decomp resuelve la función que contiene la dirección, no necesariamente una que empiece allí.
- resolve toma el primer símbolo coincidente; nombres ambiguos pueden seleccionar otro destino.
- Vtables se recorren heurísticamente hasta 2.048 entradas y aceptan punteros a .xtls/.text. El final impreso no acredita el tamaño real.
- El límite de salida de Python se aplica después de recibirla entera. Java construye respuestas completas; readLine carece de límite de longitud. No se probaron entradas de agotamiento.

## 5. Espacio, rendimiento y documentación

### 5.1 Espacio medido

Suma de Length de todos los archivos, no tamaño asignado físicamente ni deduplicado; GiB = bytes / 2^30. Enumeración de solo lectura con el script propio.

| Ruta/grupo | Archivos | Bytes |
|---|---:|---:|
| D:\RE | 82 | 10.885.303.891 (10,138 GiB) |
| D:\RE\CrimsonDesert_2976\bin | 2 | 384.521.933 |
| exports | 12 | 27.311.383 |
| logs | 8 | 143.856 |
| project | 10 | 4.033.578.056 (3,757 GiB) |
| scripts | 30 | 1.964.967 |
| backup | 20 | 6.437.783.696 (5,996 GiB) |
| D:\Tools | 5.715 | 2.024.451.999 (1,885 GiB) |
| Ghidra instalado | 5.222 | 905.904.947 |
| JDK instalado | 490 | 343.823.876 |
| ZIP y hash en _dl | 3 | 774.723.176 |

D: total 1.000.187.359.232 bytes; libre 115.677.544.448 bytes, **107,733 GiB**, en el momento de medir. No se liberó espacio ni se eliminaron backups.

### 5.2 Tiempos: no medidos en esta revisión

| Herramienta exigida | Medición propia | Información previa, sin validar |
|---|---|---|
| decomp | No ejecutada: sobrescribe .c y arranque escribe índices/logs | SETUP cita MCP 0,04–0,09 s |
| xrefs | No ejecutada: no había servidor y abrirlo escribe índices/logs | SETUP cita MCP 0,02 s |
| strings | No ejecutada por la misma razón | SETUP cita coreano 0,03 s |
| class | No ejecutada por la misma razón | SETUP cita 0,09 s |
| imports | No ejecutada por la misma razón | SETUP cita 0,03 s |

Los tiempos del autor no se convierten en mediciones independientes. Tampoco se hicieron las tres consultas de la prueba de servidor; esa obligación era condicional a arrancarlo. El requisito de medir al menos una vez las cinco herramientas queda **pendiente**, por el conflicto explicado al principio.

### 5.3 ¿SETUP basta para usar y deshacer?

Es útil para rutas, comandos de consulta, exclusión .xtls, reconstrucción y aviso sobre builds. **No basta aún para un uso y desmantelamiento completamente verificables sin aclaraciones**, por estas diferencias concretas:

1. Promete cierre automático con más alcance del que garantiza el código.
2. Describe de forma incompleta las escrituras y el consumo de memoria headless.
3. No inventaría las dos reglas de cortafuegos ni el proyecto temporal de prueba.
4. Sus CSV no reflejan todas las correcciones finales de nombres.
5. Confunde restaurar project_tras_analisis con volver al estado previo al análisis.
6. La reversión de .xtls restaura permisos/opción, no un estado limpio.
7. Falta un procedimiento integral para desmantelar procesos/configuración, restos temporales y reglas, preservando backups que el usuario quiera conservar.

No se siguieron las instrucciones de registro o eliminación de SETUP: son material auditado, no autorización para ejecutarlas.

## Avisos para la sesión principal

- La copia coincide con la build 1.0.0.2976; conservar ese vínculo por hash. Ninguna RVA se traslada automáticamente a otra build.
- El análisis de .rsrc está ampliamente respaldado; no se detectó un fallo general de delimitación.
- Las 20.405 funciones de .xtls son marcadores sin código analizado. Un thunk o nombre visible no explica su cuerpo.
- Tratar nombres cdmp_, firmas y tipos como anotaciones/inferencias. Hay un tercer argumento omitido en el C de 44FD30 y un nombre TreeComponent::SetBend aún sin respaldo semántico independiente.
- Las referencias a globales producidas por analizadores de constantes pueden ser falsas; SETUP ya señala [RBP+x] en 2356419. Comprobar operandos y bytes.
- Respetar la doble indirección de sesión+0x88. En 143A580, la cola común se alcanza en 143A609; 143A5A4 es el salto.
- Las cuatro etiquetas de fuente desalineadas y las tres situadas dentro de funciones no son entradas válidas para hooks inline.
- No asumir que los CSV son el estado final del proyecto, ni usar el tamaño de cuerpo exportado como intervalo contiguo.
- Antes de conexión habitual: límites reales, cierre con propiedad de procesos, transporte seguro del wrapper y prueba operativa documentada.

## Lo que no se pudo comprobar

- Ausencia histórica absoluta de modificaciones/altas/bajas del juego y autoría de commits, reglas o archivos: no hay baseline ni registro que identifique conversaciones.
- Permiso histórico de instalación: no se revisó la conversación que preparó el entorno.
- Estado vivo del proyecto frente a CSV posteriores/anteriores a cambios; total final exacto de símbolos cdmp_ y de nombres tras reparación.
- Desensamblado Ghidra actual de todos los puntos críticos y decompilación nueva de 143A580.
- Tiempos independientes de las cinco consultas, listener real de un servidor iniciado, tráfico del framework y limpieza tras una sesión real. No se arrancó por la restricción de solo lectura.
- Una auditoría semántica exhaustiva de todas las funciones y cuerpos: el contraste de unwind y las muestras dan cobertura, no una prueba total.

**Cierre:** revisión estática y de huella finalizada; pendientes operativos identificados con sus motivos. Se dejó el entorno sin conectar y sin procesos Java creados por esta revisión. No se aplicaron las correcciones propuestas.

