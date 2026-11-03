# Revisión final de consolidación — ejecutivo, contención y guardado

Fecha: 2026-10-14. Ámbito: resumen ejecutivo y §§4–5 de `consolidacion/AUDITORIA.md`, contraste con `a4-contencion/INFORME-A4.md` y `a5-guardado/AUDITORIA-5.md`, y lectura de las condiciones transversales/orden y del nuevo `seccion-9.md`. No se rehizo la auditoría1–3. Las fuentes y hashes leídos se conservan en `verificacion-final.json`.

## Resultado

**La síntesis de4/5 y el ejecutivo representan correctamente los dictámenes finales.** No encontré otra discrepancia material en recuentos, cronología o estado de aprobación. Permanecen separados diagnóstico verificado, riesgo de diseño y prueba futura; la publicación real continúa bloqueada. Se comprobaron fuentes primarias puntuales, sin implementar, compilar, probar en vivo, escribir partidas ni tocar los borradores del coordinador.

Dos precisiones enviadas al coordinador deben incorporarse antes de entregar:

1. **Offset del primer2005 posterior al rearmado B.** En la versión revisada, §4 decía «entrega BIN registro146811672». Es el comienzo del registro **CDPK/paquete**. El registro de actor empieza en **146811703**. Mi lectura directa de los64 bytes en esa posición confirma timestamp480938895, actor tipo3, claveB010103A. Usar «paquete/CDPK@146811672, actor@146811703». No encontré el hexadecimal inventado en el consolidado revisado. Fuente: BIN `shadow-p1b-p2b/cdmp_packets_20261014-175057.bin`; A4 tabla de rearmado y su precisión final. No modifica21,402565s por recepción ni21,408760s por entrega.
2. **SEH no implica contención universal de excepciones.** §6 problema3 afirmaba «SEH evita que el mod propague una excepción». Debe acotarse a los fallos de lectura que el lector decide manejar. `src/mod/game/game_fault.cpp:30–37` retorna `EXCEPTION_CONTINUE_SEARCH`; `user2_stages.cpp:1637` lo usa desde cleanup_fault. Las excepciones nativas no se ocultan. Propuesta: «Las lecturas protegidas pueden contener fallos de acceso del lector, pero no demuestran ausencia, consistencia ni vida útil; las llamadas nativas conservan su política de propagación». Esto mantiene el razonamiento sobre lectura desconocida sin contradecir la política real del código.

Precisión editorial opcional: la whitelist0–2/100–108 «es adecuada» (§5) es un criterio de alcance recomendado, no un hecho experimental [V]. Puede rotularse «Política propuesta»; sí están verificadas las rutas host que pasan otras ranuras y el riesgo separado de CrossSave. No cambia el bloqueo ni el orden recomendado.

## Comprobaciones de alcance superadas

### Contención

- Se conservan843 líneas y el denominador de sonda, sin convertirlas en todas las llamadas del motor.
-110 diagnósticos/109 con aplicación previa y71/60/65 antes del corte se transcriben sin equiparar diagnósticos con bajas ni acción con desplazamiento.
- El contrato incorrecto de param4 se contrasta con `npc_action_probe.cpp:41–42`, `offsets_s2.h:35` (+90) y `0x2a78ca0.c:39–44` (vf170 y comparación con actor+90). La solución no convierte actor+90 en emisor observado ni recomienda una llamada virtual nueva desde worker.
- La rama281 no se presenta como ejercitada por las843 líneas ni como éxito de locomoción/misión.
- Se incorpora la cola A de22 aplicaciones>5s y las20 sin hijo6 publicado identificado;8s permanece provisional.
-21,402565s se define por recepción→recepción del mismo invitado, y se mantienen las fases de entrega y los límites de sincronización.
-173,609ms para55 stages y la diferencia de cohortes321/483 quedan claras. Los241/56 y209 no se mezclan entre salida y llegada.
- La velocidad del relevo no se convierte en ausencia universal de duplicados; el salto7→10 no se acepta como prueba de viaje rápido.
- Las carreras de Req entre selección/ejecución y la degradación hacia el vigilante antiguo quedan recogidas; one-shot y cadena periódica permanecen separados.

### Guardado

- El mecanismo prepare_sandbox se comprobó directamente en `save_hooks.cpp:571–606`: borrado de_prev, renombrado de caja y copia de real. La síntesis limita correctamente la pérdida al segundo arranque exitoso posterior y no dice que la partida se borre en el primer arranque.
- La narración histórica de autoguardados A18:21/18:36 se mantiene[NV], con el límite real18:10:02. No se sustituyó el inventario histórico ausente por acceso actual a partidas.
- La tabla3/3,4/4,3/3 Load;0/1 Save en los tres;0/1,1/1,1/1 Flush;30 redirects por sesión coincide con A5. Se corrige el Flush host de C y no se llama archivos a las30 operaciones.
- Se comprobó `classify_owner` en `guest_save_logic.h:454–458`: fallbackHost sin identificación positiva cuando no hubo guest. El consolidado impide reciclarlo para real.
- Se leyó `0x1871930.c:53–87,114–134`: el camino opción8/pendiente−1 no escribe slot_out ni llama CommitSlot; el otro camino confirma datos pendientes y llama PostSave con el actor de la llamada. La síntesis preserva la diferencia entre invocador y procedencia del buffer.
- Apertura, completitud, pareja de generación, autoría, hash y coherencia semántica se mantienen como propiedades diferentes. Los contramodelos no se atribuyen a una partida real observada.
- El consolidado no presenta new→slot como commit durable y conserva la posibilidad de restaurar backup viejo. Las rutas de recuperación consultadas sostienen ese límite; el momento respecto a la instalación del mod sigue[NV].
- Las escrituras globales estáticas no se convierten en prueba de corrupción causada por guest en S34. Se mantiene la prohibición de revertir globales ciegamente y de borrar progreso legítimo compartido.
- La whitelist no se usa como protección de CrossSave; configuración Cloud efectiva y exclusión de backups siguen sin verificarse. No se inventa sincronización dinámica en este juego.
- M1 y M2 son previas a real, y prueban riesgos diferentes. La alternativa de exportar generaciones inmutables es futura y no se anuncia implementada.

### Ejecutivo, orden y reproducción

El ejecutivo distingue investigación útil de diseño literal listo para programar, mantiene real bloqueado y no reclama pruebas en vivo. El orden conserva seguridad de publicación antes de ampliar autoridad; etapas peer/Shadow dicen qué no pueden validar. Misiones del anfitrión y diario propio guest sin avanzar aparecen como aceptación obligatoria.

`seccion-9.md` enumera los lectores propios y explicita que algunos conservan rutas absolutas/dependencias; no promete portabilidad automática. El ejemplo de VA usa suma de base y RVA en vez de concatenación textual. La entrega debe incluir el README de reproducción y manifiesto a los que remite; esta revisión no comprueba una copia final que todavía no se había publicado.

## Producto propio de esta revisión

`verificar_revision_final.py` lee únicamente64 bytes del BIN para confirmar el offset/clave y calcula hashes de los textos revisados. Produce `verificacion-final.json`.

```powershell
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '%TEMP%\codex-s34-auditoria-20261014\revision-consolidacion\verificar_revision_final.py'
```

No se reejecutaron parsers ajenos ni grandes recuentos ya reproducidos. El juicio final de esta revisión es **sin objeciones materiales adicionales una vez corregidas las dos precisiones indicadas**, dentro de la evidencia y de los límites que expresa el consolidado.
