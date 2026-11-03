# S35, fase E: el guardado del anfitrión se exporta antes de rehacer la caja

14 de octubre de 2026. Worktree `C:\Emulador CD-s35-save`, rama `opcion-d-s35-save`. Build del juego
**1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). No se lanzó el juego, no se desplegó nada y no se leyó ni
escribió la carpeta real de partidas: todo lo nuevo se probó con carpetas falsas en `%TEMP%`.

Marcas: **[V]** comprobado (código, tests, disco); **[NV]** no verificado; **[H]** a medir en vivo.

Base: auditoría S34 `s34-investigaciones/AUDITORIA.md` §5, §6 y §8 punto 6; dictamen completo
`s34-investigaciones/datos/auditoria-codex-20261014/a5-guardado/AUDITORIA-5.md`; investigación
`s34-investigaciones/5-guardado-anfitrion.md`; desvío S24 `../pendientes/p8-guardado-desviado.md`.

---

## 0. Resumen

- **El problema [V]:** con `save_guard=redirect` la caja (`cdmp_save_sandbox`) se rehace desde la carpeta real en
  cada arranque (`save_hooks.cpp` `prepare_sandbox`): se borra el `_prev` viejo, la caja pasa a `_prev` y se copia la
  real. Lo que el anfitrión guardó en una sesión se perdía al segundo arranque.
- **Lo que hace ahora:** nueva clave `[session2] host_save = sandbox | export | probe`.
  - `sandbox` (por defecto): exactamente lo de antes.
  - `export`: **antes** de borrar o mover nada, la caja y su `_prev` (y cualquier caja apartada antes) se copian a una
    **generación inmutable** fuera del árbol de partidas, se verifican (tamaño + SHA-256 releídos de la copia) y se
    escribe un manifiesto. Solo entonces sigue la rotación de siempre. Si la exportación falla **no se borra nada**:
    la caja vieja se aparta con otro nombre y `_prev` se queda como estaba.
  - `probe`: lo mismo que `export` y además clasifica cada ranura exportada como `candidate` / `not-candidate` /
    `unknown` con sus motivos, usando un diario de los `SavePlayerToSlot`/`FlushPendingSaveData` de la sesión que la
    escribió. Es **observación**: no autoriza nada.
  - `real` **no existe**: si alguien lo escribe, línea de error y se usa `sandbox` (la auditoría lo bloquea).
- **Nada de esto escribe la carpeta real.** Solo se **lee** para comparar (`probe`: “idéntica a la real”).
- **Restaurar** una generación a la carpeta real es un paso **manual, fuera del juego**, con
  `scripts/host_save_restore.ps1` (juego y Steam cerrados, `-Generation`, `-Slot` y `-Confirm` explícitos).

---

## 1. Configuración

```ini
[session2]
save_guard=redirect      ; obligatorio: sin la caja no hay nada que exportar
guest_own_save=1         ; o probe: sin esto no hay ganchos de Save/Flush y probe dirá "unknown"
host_save=probe          ; sandbox (defecto) | export | probe     ("real" se rechaza)
host_save_dir=C:\Emulador CD\backups\host_save_exports   ; RECOMENDADO (vacío = <bin64>\cdmp_save_exports)
host_save_keep=20        ; generaciones válidas que se conservan (1-500; la más nueva nunca se poda)
```

Ini de la prueba de un PC con estas claves: `docs/research/ini/cdmp.p8-s35-local-guardado.ini`
(= `cdmp.p8-s33-local-1.ini` + `host_save=probe` + `host_save_dir` en `backups\`). Una raíz de unidad (`C:\`) se lee
como tal, no como `C:` (revisión L13); de todos modos se rechaza si contiene la carpeta `save`.

`host_save` solo actúa con `save_guard=redirect` activo; con otro modo (o si la copia de la caja falla y el guardián
cae a bloqueo) sale `Session2 host save: host_save=X needs save_guard=redirect ...: nothing is exported in this run`.
Código: `src/mod/config.{h,cpp}` (`s2_host_save`, `s2_host_save_dir`, `s2_host_save_keep`), configurado en
`session2.cpp` justo antes de `save_hooks_install`.

---

## 2. Dónde van las exportaciones y por qué

**Por defecto `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\cdmp_save_exports\`** (la carpeta del exe del
juego, junto a `cdmp.log`). Motivos:

| Requisito | Por qué se cumple |
|---|---|
| Fuera del árbol de partidas | La caja, su `_prev` y la carpeta real están en `%LOCALAPPDATA%\Pearl Abyss\CD`; el mod **rechaza** cualquier `host_save_dir` dentro de `%LOCALAPPDATA%\Pearl Abyss`, que contenga la carpeta `save` o que se solape con las cajas (`hsv::export_dir_problem` + comprobación en `host_save_archive`). **Revisión M1:** no solo por el texto: se abren la carpeta (o su antecesor existente más cercano) y todos los de encima y se comparan **volumen + file id** con `save` y `Pearl Abyss` (seguidos si son enlaces) y con todos los antecesores de `save`; se rechaza si ella o una carpeta de encima es un **reparse point** (junction, symlink, punto de montaje), y las reglas de texto se repiten sobre la ruta resuelta (`GetFinalPathNameByHandleW`). Así no cuelan una junction, un nombre 8.3, una unidad `SUBST`, `\\localhost\C$` ni una `save` que sea una junction (`hsv::resolved_dir_problem`). Se repite tras crearla, y un `runs\` que sea un enlace también la invalida. |
| Fuera de una raíz plausible de Steam Cloud | Steam escribe `steam_autocloud.vdf` en las raíces Auto-Cloud que sincroniza; el del juego está en `save\<cuenta>\` (p8-guardado-desviado §1). **[V] 14-10:** no hay ningún `steam_autocloud.vdf` en `Crimson Desert\` hasta 3 niveles (`find -maxdepth 3`). El mod **rechaza** una carpeta con `steam_autocloud.vdf` en ella o en cualquier carpeta superior, y cualquier ruta bajo `Steam\userdata` (las copias locales de Cloud). **[NV]:** la configuración Cloud efectiva no se ha leído (Valve permite raíces y patrones; auditoría §5 “Steam Auto-Cloud”): esto reduce el riesgo, no lo demuestra. |
| Otro disco que las partidas | `D:` frente a `C:` (la partida): un disco lleno en `C:` no impide exportar, y al revés. |
| El mod sabe la ruta sin configurar nada | Es la carpeta de `CrimsonDesert.exe` (`GetModuleFileNameW`), la misma donde el usuario ya lee `cdmp.log`. |
| No la borran las herramientas del proyecto | `deploy.ps1 -Remove` solo quita `cdmp.asi`, `cdmp.pdb` y su `winmm.dll`. |

**Riesgos de esa ubicación:** desinstalar el juego desde Steam borra su carpeta (y con ella las exportaciones);
“verificar integridad” no debería tocar archivos ajenos **[NV]**. Por eso **lo recomendado** (doc e ini de la prueba,
revisión L14) es `host_save_dir=C:\Emulador CD\backups\host_save_exports` (la carpeta `backups/` del repositorio, fuera
de git); el valor por defecto sigue siendo `bin64` para que funcione sin configurar nada. No usar `Documentos` ni
carpetas sincronizadas (se sincronizan; el mod no lo detecta).

El `steam_autocloud.vdf` de la caja **no se copia** a la exportación (nota en el manifiesto): es la marca de Steam y
no hace falta para restaurar.

---

## 3. Qué pasa en cada arranque (`prepare_sandbox`, antes de crear ningún gancho)

1. `host_save_archive(save, caja)` (`host_save_export.cpp`), solo con `export`/`probe`:
   1. Comprueba la carpeta de exportación (§2) y la crea. Si está mal: con algo que archivar → **fallo** (paso 3);
      sin nada → no hay exportación ni diario en esta sesión (línea de error).
   2. Fuentes: `cdmp_save_sandbox` (`sandbox`), `cdmp_save_sandbox_prev` (`prev`) y cada
      `cdmp_save_sandbox_unexported_<gen>` (`aside-<gen>`). Recorre cada una (un enlace/reparse point es un fallo,
      nunca se sigue) y calcula el SHA-256 de cada archivo.
   3. **Deduplicación:** una fuente byte a byte igual (rutas, tamaños, SHA-256) a una fuente copiada de una
      generación válida no se copia otra vez (`source=...|identical_to=<gen>/<etiqueta>`). Si todo es idéntico no se
      crea generación: así los arranques sin cambios no empujan fuera de la retención a las generaciones buenas.
      **Revisión M2:** antes de dar una fuente por “ya exportada” se releen en disco los archivos de esa generación
      (existen, tamaño, SHA-256); si no cuadran, la generación se trata como inválida en ese arranque (línea
      `generation G is damaged (...): treated as invalid`) y la fuente se copia de nuevo.
   4. Crea `<nombre>.partial\`, copia cada fuente nueva archivo a archivo **hasheando lo que lee**, hace
      `FlushFileBuffers`, copia las fechas, **relee la copia** y compara tamaño y SHA-256. Copia también el diario de
      la sesión que escribió esa caja (`journals\<etiqueta>.txt`, si lo hay). En `probe` clasifica cada ranura (§5).
   5. Escribe `manifest.txt` (con `FlushFileBuffers`), lo relee y lo vuelve a interpretar, y renombra
      `<nombre>.partial` → `<nombre>` (`MOVEFILE_WRITE_THROUGH`). Una carpeta sin `.partial`, con manifiesto que
      termina en `state=complete` y cuyo `generation=` coincide con el nombre es una **generación válida**.
   6. **Retención:** borra las generaciones válidas más viejas hasta dejar `host_save_keep`, **nunca** la más nueva
      válida, nunca la recién hecha, nunca una a la que una fuente de este arranque era idéntica y **nunca una a la
      que se refiere (`identical_to`, transitivamente) una que se queda** (revisión L3). “Más vieja/nueva” se decide por
      `seq=` del manifiesto, un número monotónico de la carpeta de exportación, no por el nombre en hora local (un
      reloj atrasado o el cambio de hora no reordenan nada; revisión L2). Lo inválido, `.partial` o una carpeta que
      sea un enlace no se poda nunca (se avisa: “delete them by hand”); el borrado se niega ante cualquier reparse point
      (revisión M1). Los diarios en `runs\` se podan a los 200 más nuevos.
2. Plan de reconstrucción (`hsv::plan_rebuild`):

| Resultado del archivo | Qué se hace con la caja y `_prev` |
|---|---|
| `sandbox` (Off), nada que archivar, archivado bien | Lo de siempre: borrar el `_prev` viejo, caja → `_prev`, copia nueva de la real. |
| **Falló** | **No se borra nada:** la caja se renombra `cdmp_save_sandbox_unexported_<gen>` y `_prev` se queda. Se hace la caja nueva y el desvío sigue. Si ni el renombrado sale, el guardián cae a bloqueo (como antes con cualquier fallo de la caja): ningún dato perdido. |
| **Falló con 3 cajas ya apartadas** (`hsv::kMaxAside`, revisión L1) | No se mueve ni borra nada y esta sesión cae a **bloqueo** (vuelve el aviso “Failed to save data”), con la línea `the export failed and 3 sandboxes are already set aside (the limit)`. Arreglar la exportación o mover a mano las `cdmp_save_sandbox_unexported_*`. |

3. Las cajas apartadas que ya están en una generación verificada se borran **después** de la rotación (línea “is in
   a verified generation now: deleted”).
4. Tras copiar la caja nueva: `host_save_run_started` abre el **diario de esta sesión**
   `<exports>\runs\<aaaammdd-hhmmss>-p<pid>.txt` con el id de carpeta (volumen + file id NTFS) de la caja nueva. Un
   renombrado en el mismo volumen conserva ese id, así el arranque siguiente sabe qué sesión escribió la caja (y el
   `_prev` del otro).

Coste: la caja pesa ~2,7 MB (S34: 7 archivos, 2 686 266 bytes); copiar, releer y hashear unos pocos MB se hace en
el hilo de arranque tardío, antes de los ganchos, fuera de `DllMain`.

### Estructura en disco

```
cdmp_save_exports\
  20261014-190232\                 una generación (el nombre: hora local del arranque que la hizo; -2, -3... si coincide)
    manifest.txt
    sandbox\<cuenta>\slot0\lobby.save, save.save, ...   copia de cdmp_save_sandbox tal como quedó en la sesión anterior
    prev\...                       copia de cdmp_save_sandbox_prev (si no era idéntica a otra generación)
    aside-20261013-101500\...      una caja apartada por un fallo anterior
    journals\sandbox.txt           el diario de la sesión que escribió esa caja
  runs\20261014-190232-p1234.txt   el diario de cada sesión (se va escribiendo mientras juega)
```

### El manifiesto (`hsv::manifest_text` / `parse_manifest`, ida y vuelta probada)

```
cdmp host save generation (P8 S35 phase E)
format=1
game_build=1.0.0.2976
pe_timestamp=0x6AB28F00
mod_build=0x1d99ed0a dirty          (build_id.h del mod)
generation=20261014-190232
created=2026-10-14 19:02:32.125
created_utc=2026-10-14T17:02:32Z
seq=7                               (monotónico en la carpeta: el orden de "más nueva")
run=20261014-190230-p1234           (la sesión que exportó; run_start=su arranque)
mode=probe
source=sandbox|<ruta>|<sesión que la escribió o unknown>|<id de carpeta>|<identical_to o vacío>
account=sandbox|<id-cuenta>
file=sandbox|<id-cuenta>\slot0\save.save|1944571|<sha256>|<FILETIME de escritura>
slot=sandbox|<id-cuenta>|slot0|host                 (host / crosssave / guest-pool / outside / leftover ...)
probe=sandbox|<id-cuenta>|slot0|candidate|<motivos>  (solo probe)
note=...
state=complete                                     (última línea; nada puede ir detrás)
```

Un campo nunca contiene `|`, CR ni LF (se cambian por `_`).

---

## 4. Lo que observa `probe` durante la sesión (el diario)

Con `guest_own_save=probe` o `1`, los ganchos de entrada de `SavePlayerToSlot 0x186E450` y
`FlushPendingSaveData 0x1871930` (`guest_save_host.cpp`, sin ganchos nuevos) añaden una línea por llamada al diario
de la sesión (`host_save_observe_*`, una escritura corta bajo un mutex; el propio Save escribe ~2 MB en ese hilo):

- **Save:** dueño (`host`/`user2`/`unidentified`), **identidad positiva** (`hsv::positive_host`: usuario anfitrión
  validado o sesión propia del juego; **nunca** el “no hay sesión 2” de `classify_owner`, que la auditoría marca
  [F]), llamador (autosave / 2910 / OTHER), ranura pedida, **ranura escrita** (la del original tras el remapeo; `-`
  si se rechazó), acción, `*res` leído con protección (no RAX), **profundidad** de Save/Flush abiertos en el mismo
  hilo, y `save_hooks_redirect_failed()` en ese momento.
- **Flush:** dueño, identidad positiva, llamador, `mgr+0x5C` antes de la llamada (`-1` = nada pendiente), `*res`. El
  “committed slot” **no** se apunta: sin pendiente el juego no lo escribe (auditoría: `1163343936` era basura).
- Una línea `hooks=save:1 flush:1` dice que esos ganchos estaban puestos. Tope 4000 eventos (después `truncated`).
  Una línea cortada por un cierre del juego también cuenta como diario incompleto.

---

## 5. Los veredictos de `probe` (`hsv::classify_slot`)

Primero lo que **excluye** (`not-candidate`), luego lo que deja la duda (`unknown`), y `candidate` solo si todo lo
comprobable se cumple:

| Regla | Veredicto |
|---|---|
| No es carpeta `slotN`, o es resto del commit nativo (`slotN_new`, `_backup`, `_backup_*_delete`, `slotN_*`) | not-candidate |
| Ranura 3 (CrossSave), 200-207 (grupo del invitado; **204 = la del invitado**), fuera de 0-2 / 100-108 | not-candidate |
| Falta `lobby.save` o `save.save`; cabecera inválida (`gsv::check_header`) | not-candidate |
| Ambos archivos idénticos a los de la carpeta real | not-candidate (“nothing new”) |
| El diario registra un Save de user 2 o no identificado **escrito** en esa ranura | not-candidate |
| Sin diario de la sesión que la escribió; Save sin ganchos (`guest_own_save=0`); diario incompleto | unknown |
| No se pudo leer la ranura real para comparar | unknown |
| Un Save anidado en otro Save/Flush; un Save del anfitrión a esa ranura con `*res≠0` o ilegible | unknown |
| Un Flush encontró esa ranura pendiente en `+0x5C` (buffer de procedencia desconocida); un pendiente ilegible | unknown |
| Alguna llamada desviada falló en esa sesión (`FAILED n`) | unknown |
| `lobby.save` y `save.save` escritos con más de 10 s de diferencia | unknown |
| Cambiada pero sin Save del anfitrión observado, o solo con identidad por el fallback | unknown |
| Todo lo anterior bien y ≥1 Save del anfitrión con identidad positiva, `*res=0`, sin anidar | **candidate** |

Los motivos de `candidate` dicen siempre lo que **no** está demostrado: cobertura completa de E/S, contextos anidados
entre hilos, pareja indivisible y aislamiento del mundo (M1/M2). **Cabecera y SHA-256 no prueban autoría** (auditoría
§5): un `candidate` es una ranura que merece revisión, no una partida segura.

### Los puntos obligatorios 1-4 de la auditoría para `real`, en esta fase

| Punto (auditoría §5) | Implementado aquí (observación) | Pendiente |
|---|---|---|
| 1. Identidad positiva y procedencia | identidad positiva aparte del fallback; `*res` leído con protección; ranura pedida y escrita; Flush con su pendiente | cuenta/mundo/época por evento; salud efectiva de `guest_own_save` por evento; procedencia del buffer de `mgr+0x5C` (qué serialización lo llenó) |
| 2. Contextos anidados | profundidad Save/Flush por hilo (TLS); anidado ⇒ unknown | pila RAII por generación y contaminación versionada entre hilos; un unwind SEH puede dejar la profundidad alta (lado conservador: unknown) |
| 3. Cobertura de E/S | `FAILED n` del desvío ⇒ unknown; restos `_new/_backup` ⇒ not-candidate | contar rutas que ya llegan a la caja (`redirected()` sale en `!hit`), WriteFile/Flush/CloseHandle, handles previos/duplicados, escritura diferida o mapeada |
| 4. Pareja indivisible | ambos archivos presentes y con cabecera válida; diferencia de escritura ≤10 s | instantánea lobby/save de la misma generación bajo exclusión; la exportación copia al arrancar, con el juego sin escribir la caja, pero eso no prueba que la sesión anterior dejara una pareja coherente |
| 5-6. Publicación durable, M1/M2 | no aplica: no se publica nada | todo |

---

## 6. Líneas del log

| Línea | Cuándo |
|---|---|
| `Session2 host save: host_save=sandbox (the sandbox is rebuilt ...)` | modo por defecto |
| `Session2 host save: host_save=probe \| the previous sandbox is exported to '<dir>' before each rebuild (verified, keep N generations) \| ... \| the real save folder is never written` | export/probe |
| `ERR Session2 host save: host_save='real' (publishing to the real save folder) is NOT implemented: ...; using sandbox` | `real` escrito |
| `Session2 host save: host_save='x' is not sandbox, probe or export; using sandbox` | valor desconocido |
| `Session2 host save: '<ruta>' (prev, N files, B bytes, written by run R) is identical to the verified generation G/sandbox: not copied again` | deduplicación |
| `Session2 host save: nothing new to export (...)` | todo idéntico |
| `Session2 host save probe: <gen>/sandbox <cuenta>\slot0 = candidate: ...` | una por ranura (probe) |
| `Session2 host save EXPORT: generation '<gen>' made in '<dir>' and verified (...); valid generations N (keep K), pruned: ...` | exportación hecha |
| `ERR Session2 host save: the export FAILED (<motivo>): nothing is deleted; ...` | fallo |
| `Session2 host save: '<caja>' set aside as '<...>_unexported_<gen>' (not exported, NOT deleted); '<_prev>' kept as it was` | tras un fallo |
| `Session2 host save: '<apartada>' is in a verified generation now: deleted` | arranque siguiente |
| `Session2 host save: this run is '<run>', journal '<ruta>' (sandbox id V:F)` | diario abierto |
| `Session2 host save: no journal for this run ...` | carpeta rechazada |
| `Session2 host save: generation G is damaged (...): treated as invalid, '<ruta>' is copied again` | deduplicación contra una generación dañada |
| `Session2 host save: '<ruta>' is a link (reparse point): not used as a generation` / `: not deleted` | enlace en la carpeta de exportación |
| `ERR Session2 save redirect: the export failed and 3 sandboxes are already set aside (the limit): ... falls back to block` | límite de cajas apartadas |
| periódica: `save guard REDIRECT (to '...', writes W, reads R, queries Q; host save probe: generation G exported at start, journal N events)` | cada resumen |

---

## 7. Restaurar a mano: `scripts/host_save_restore.ps1`

**Fuera del juego, con el juego y Steam cerrados.** Sin `-Confirm` solo comprueba y enseña el plan.

```powershell
# 1) Ensayo: verifica TODA la generación y dice qué haría (no cambia nada)
.\scripts\host_save_restore.ps1 -Generation 20261014-190232 -Slot 0
# 2) De verdad (ranura 0 de la caja de esa generación)
.\scripts\host_save_restore.ps1 -Generation 20261014-190232 -Slot 0 -Confirm
# 3) Una ranura que probe no dio por candidate (unknown, o exportada con host_save=export): también -AllowUnverified
.\scripts\host_save_restore.ps1 -Generation 20261014-190232 -Slot 0 -Confirm -AllowUnverified
# Opciones: -Source prev|aside-<gen> (defecto sandbox), -Account <id>, -ExportDir <carpeta>, -BackupDir <carpeta>
# Con host_save_dir=C:\Emulador CD\backups\host_save_exports: -ExportDir 'C:\Emulador CD\backups\host_save_exports'
```

Se niega si: el juego está abierto; Steam está abierto (salvo `-SteamRunningOk`, a sabiendas: Cloud puede subir o
traer una copia rival); el manifiesto no es completo o es de otra build (`1.0.0.2976` / `0x6AB28F00`); cualquier
archivo de la generación no coincide en tamaño y SHA-256 (o sobra alguno en la fuente elegida); la ranura es 3, está
en 200-207 o fuera de 0-2 / 100-108; falta la pareja `lobby.save` + `save.save` o su cabecera no es válida; `probe` la
clasificó `not-candidate`; no existe la carpeta de esa cuenta en la carpeta real; o la carpeta real tiene restos
`slotN_new` / `slotN_backup*` de esa ranura (la recuperación nativa `0x2358EF0` los usaría: puede restaurar el backup
viejo sobre la ranura nueva). Con `unknown` o sin veredicto el ensayo avisa, y con `-Confirm` hace falta además
`-AllowUnverified` (revisión L8). La cabecera se comprueba con los mismos límites que el C++ (16 MB por archivo, 64 MB
y 255:1 de tamaño descomprimido; revisión L10). `-ExportDir` se resuelve al empezar y se niega si no existe (L11).

Con `-Confirm`: copia **toda** la carpeta real a `backups\host_save_restore-<hora>\save` y la verifica por SHA-256
antes de tocar nada; copia cada archivo a `<archivo>.cdmp_restore_tmp`, lo verifica, lo renombra encima, le pone la
fecha del manifiesto y relee todo. Si algo falla, devuelve esa ranura desde la copia, **comprueba cada archivo devuelto
contra la copia** y quita la carpeta de ranura si la había creado él (revisión L9). Deja `restore.txt` en la carpeta
de la copia. **Deshacer:** con juego y Steam cerrados, copiar `backups\host_save_restore-<hora>\save` sobre la carpeta
real. Contra la carpeta real no se ha ejecutado nunca; su test (`tests/host_save_restore_tests.ps1`, en ctest) lo
ejecuta contra una carpeta falsa en `%TEMP%` con `LOCALAPPDATA` redirigido (§10).

Después de restaurar, al abrir Steam, Auto-Cloud puede subir la partida restaurada: es la decisión que se toma al
restaurar. Probarlo primero con una partida de laboratorio (auditoría §5: “Cloud al final con datos de laboratorio”).

---

## 8. Lo que sigue bloqueado y por qué (auditoría §5)

- **`host_save=real` (publicar en la carpeta real durante el juego): bloqueado.** Faltan: identidad y procedencia
  completas (también del buffer diferido de Flush), contextos anidados con contaminación versionada, cobertura de E/S
  demostrada, pareja indivisible, publicación durable con recuperación probada ante cortes, y **M1 y M2 ampliadas**
  (procedencia/concurrencia; aislamiento semántico del mundo: un archivo bien atribuido al anfitrión puede llevar
  estado global cambiado por la carga del invitado, p. ej. `0x6C8CD98`, `_currentRestoreCodeVersionXXX`).
- **Recuperación nativa:** con `_backup` viejo y ranura nueva, `0x2358EF0` puede restaurar la vieja; por eso el script
  se niega si hay restos y no imita la secuencia de renombrados del juego.
- **CrossSave:** ni la caja ni la lista blanca paran la subida HTTP de la rama 3 de `0x186E450`; solo la evita que el
  invitado se remapee/rechace antes. La ranura 3 nunca se restaura.
- **Steam Cloud [NV]:** no se ha leído su configuración efectiva; la exportación evita las raíces plausibles pero no lo
  demuestra. La promoción manual con Steam cerrado reduce la concurrencia, no la elimina.
- **Atribución:** “candidate” no es autoría demostrada (§5).

---

## 9. Plan de prueba en un PC (para el usuario)

Ninguna de estas pruebas escribe la carpeta real (la exportación solo la lee). Aun así, regla del proyecto: copia antes
`%LOCALAPPDATA%\Pearl Abyss\CD\save` a `backups\`.

1. Apunta la carpeta real:
   `Get-ChildItem -Recurse "$env:LOCALAPPDATA\Pearl Abyss\CD\save" | Select-Object FullName, Length, LastWriteTime`.
2. En el `.ini` del anfitrión de la prueba de un PC (contra `cdmp_peer`), en `[session2]`: `save_guard=redirect`,
   `guest_own_save=1` (o `probe`), `host_save=probe` (opcional `host_save_dir=C:\Emulador CD\backups\host_save_exports`).
   Compila y despliega (`.\scripts\build.ps1`, `.\scripts\deploy.ps1`).
3. **Arranque 1:** en `cdmp.log` deben salir `host_save=probe | the previous sandbox is exported to ...`, la
   exportación de la caja que hubiera (o `nothing new` / `nothing to export`) y `this run is '...', journal '...'`.
   Juega hasta un autoguardado (unos 15 min) o guarda a mano; sal del juego.
4. **Arranque 2:** debe salir `Session2 host save EXPORT: generation '<gen>' ... verified` y una línea
   `Session2 host save probe:` por ranura: la del autoguardado del anfitrión (`slot0`/`slot2`) `candidate` o `unknown`
   con motivo; `slot1` (sin cambios) `not-candidate ... identical to the real folder`; `slot204` `not-candidate`.
   La línea periódica lleva `host save probe: generation <gen> exported at start, journal N events`.
5. En disco: `cdmp_save_exports\<gen>\manifest.txt` termina en `state=complete`;
   `sandbox\<cuenta>\slotN\save.save` tiene la hora del arranque 1; `journals\sandbox.txt` tiene líneas `save|...`.
6. Con el juego **y Steam** cerrados, ensayo del script **sin** `-Confirm`:
   `.\scripts\host_save_restore.ps1 -Generation <gen> -Slot 0` → “Verifying N files”, el plan y “Dry run: nothing was
   changed”.
7. Repite el paso 1: la carpeta real debe tener **las mismas fechas y tamaños**.
8. (Opcional, fallo) `host_save_dir=%LOCALAPPDATA%\Pearl Abyss\CD\x` (ruta completa: el `.ini` no
   expande `%LOCALAPPDATA%`): el arranque debe decir
   `the export FAILED (... is refused ...)` y `set aside as '...cdmp_save_sandbox_unexported_...'`, la caja apartada debe
   existir y `_prev` seguir igual. Quita la línea: el arranque siguiente la archiva (`aside-...`) y la borra.

**Si algo no cuadra:** `host_save=sandbox` vuelve exactamente al comportamiento anterior.

---

## 10. Código y pruebas

| Archivo | Qué |
|---|---|
| `src/mod/session2/host_save_logic.h` | puro: modos (`real` rechazado), lista blanca, nombres de generación, retención, plan de reconstrucción, carpeta de exportación, manifiesto, árboles, diario, veredictos |
| `src/mod/session2/host_save_export.{h,cpp}` | archivo antes de la reconstrucción, verificación, manifiesto, poda, diario y su observación |
| `src/mod/session2/save_hooks.cpp` | `prepare_sandbox`: archivo → plan → rotación o apartado; borrado de lo apartado ya archivado |
| `src/mod/session2/guest_save_host.cpp` | eventos Save/Flush para el diario (identidad positiva, profundidad, `*res`) |
| `src/mod/config.{h,cpp}`, `session2.cpp` | claves `host_save`, `host_save_dir`, `host_save_keep` |
| `scripts/host_save_restore.ps1` | restauración manual fuera del juego |
| `tests/optd_host_save_tests.cpp` (`optd_host_save`) | puro + archivo real sobre carpetas falsas: sin cambios ⇒ sin generación, fallos (carpeta en Pearl Abyss, bajo `steam_autocloud.vdf`, archivo bloqueado) sin restos, apartados, retención (por `seq`, referencias), diario y veredictos; revisión: ids resueltos (`resolved_dir_problem`), generación dañada ⇒ se copia otra vez, fallo inyectado al escribir el manifiesto y al renombrar ⇒ sin restos, junction dentro de una fuente, junction delante de la carpeta de exportación, `runs\` como junction, una “generación” que es junction (ni se usa ni se poda), nombre 8.3 de la carpeta del juego, y la carpeta `save` falsa **idéntica byte a byte** (nombres, tamaños, atributos, fechas, SHA-256) antes y después de todas las exportaciones y `probe` |
| `tests/optd_hook_redirect_tests.cpp` (`optd_hooks_redirect`) | cinco procesos hijos con los ganchos reales: export antes de la rotación; export fallido ⇒ caja apartada, `_prev` intacto, desvío activo; arranque siguiente (probe) ⇒ lo apartado se archiva y se borra; export fallido y renombrado imposible (archivo abierto sin compartir) ⇒ bloqueo sin mover nada; 3 cajas apartadas ⇒ bloqueo sin mover nada |
| `tests/host_save_restore_tests.ps1` (`host_save_restore_script`) | el script contra una carpeta falsa en `%TEMP%` (`LOCALAPPDATA` redirigido): el ensayo no cambia nada; rechazos (3, 204, 205, 109, restos `_new`/`_backup`/`_backup_*_delete`, archivo de la generación alterado, tamaño del manifiesto alterado, manifiesto incompleto, otra build, `not-candidate`, `unknown` y sin veredicto sin `-AllowUnverified`, juego abierto simulado con una copia de `ping.exe` llamada `CrimsonDesert.exe`, y Steam abierto si lo está de verdad); restauración verificada con su copia; devolución tras un fallo inyectado (`CDMP_RESTORE_TEST_FAIL`) de una ranura existente y de una nueva (carpeta quitada) |

`.\scripts\build.ps1`: **67/67 tests** (65 de antes + `optd_host_save` + `host_save_restore_script`).

### Riesgos conocidos

- La exportación vive en la carpeta del juego: desinstalarlo la borra (usar `host_save_dir` en `backups\`).
- El diario se escribe desde el hilo del servidor dentro del Save/Flush (una línea corta): si el disco se cuelga, esa
  llamada espera. El propio Save del juego escribe en ese mismo hilo.
- El id de carpeta NTFS vincula diario y caja; en otro sistema de archivos sin ids estables `probe` diría `unknown`.
- Una caja apartada por fallos repetidos ocupa ~2,7 MB por arranque, como mucho 3; después la sesión se bloquea.
- El veredicto depende de lo que vieron los ganchos de `guest_save_host`; lo que el juego escriba por otras rutas no
  queda en el diario (por eso “cambiada sin Save observado” es `unknown`).

---

## 11. Revisión independiente de la fase E (“GO with fixes”) y qué se hizo

Sin BLOCKER ni HIGH. Correcciones aplicadas en esta rama (sin juego, sin desplegar, sin tocar la carpeta real):

| Hallazgo | Qué se hizo |
|---|---|
| **M1** la validación de la carpeta de exportación era solo de texto (junction/symlink, 8.3, `SUBST`, `\\localhost\C$`, una `save` que sea junction); la poda de diarios y el borrado del `.partial` podían acabar dentro de la carpeta real | `resolved_problem` (`host_save_export.cpp`) + `hsv::resolved_dir_problem`: ids volumen + file id de cada antecesor existente frente a `save`, `Pearl Abyss` y los antecesores resueltos de `save`; rechazo si ella o un antecesor es reparse point; reglas de texto otra vez sobre `GetFinalPathNameByHandleW`; se repite tras crearla; `runs\` enlace ⇒ rechazo; una generación que es enlace ni se lee ni se poda; `delete_own_tree` se niega ante cualquier reparse point (ya no lo quita). Tests: puros + junction delante, `runs\` junction, generación junction, junction dentro de una fuente, nombre 8.3 |
| **M2** la deduplicación se fiaba del manifiesto | `generation_source_intact`: existe + tamaño + SHA-256 en disco antes de marcar `identical_to`; si no, generación inválida en ese arranque y se copia. Test: generación dañada ⇒ nueva generación verificada, la dañada se queda (lo inválido no se poda) |
| **M3** el script de restauración no tenía test | `tests/host_save_restore_tests.ps1` registrado en ctest (`host_save_restore_script`), contra `%TEMP%` con `LOCALAPPDATA` redirigido (lista en §10) |
| L1 cajas apartadas sin límite | `hsv::kMaxAside = 3`; con 3 y otro fallo ⇒ bloqueo, nada se mueve. Test hijo `export_limit` |
| L2 “más nueva” por nombre en hora local | `seq=` monotónico y `created_utc=` en el manifiesto; `generation_older` ordena por `seq` (el nombre solo desempata) |
| L3 podar una generación referida por `identical_to` | `plan_prune` devuelve a la lista cualquier generación referida (transitivamente) por una que se queda. Tests |
| L6 la lectura de la carpeta real en `probe` | `hash_file(..., share_all)` con `FILE_SHARE_READ|WRITE|DELETE` |
| L8 restaurar ranuras no verificadas | `-AllowUnverified` obligatorio con `-Confirm` para `unknown` o sin veredicto. Test |
| L9 la devolución no se comprobaba | cada archivo devuelto se compara con la copia; la carpeta de ranura creada por el script se quita. Tests (ranura existente y nueva) |
| L10 límites de cabecera en PowerShell | 16 MB / 64 MB / 255:1 como `gsv::check_header` |
| L11 `-ExportDir` relativo o inexistente | `Resolve-Path` al empezar; se niega si no existe |
| L13 `host_save_dir=C:\` acababa en `C:` | `config.cpp` no quita el separador de una raíz de unidad |
| L14 recomendar `backups\` | §1, §2 y el ini nuevo `docs/research/ini/cdmp.p8-s35-local-guardado.ini` |
| L4 la relectura “verificada” sale de la caché del sistema, no del disco (`copy_verified`) | Documentado, sin cambio de código. **Alcance de “verified”:** cada copia se escribe con `FlushFileBuffers` (el sistema dice haberla mandado al disco) y se relee por la API normal, que puede servirla desde la caché de Windows. Eso prueba que lo escrito coincide byte a byte con la fuente; **no** prueba que el medio físico la conserve tras un corte de luz ni detecta un fallo silencioso del disco. Para eso queda el script de restauración, que vuelve a verificar toda la generación (tamaño + SHA-256) antes de usarla, normalmente en otro arranque del PC. `FILE_FLAG_NO_BUFFERING` no se usa (exige lecturas alineadas al sector y tampoco salta la caché del propio disco). |
| L5 los diarios de `runs\` solo se podaban al crear una generación | Arreglo trivial: `prune_journals` también se llama cuando no hay nada nuevo que exportar (los 200 más nuevos se conservan). |
| L7 `append` hace un `WriteFile` síncrono bajo `g_mu`, con reservas de memoria (`std::format`/`to_string`), en el hilo del servidor; un unwind SEH a través de `DepthScope` deja la profundidad alta | Documentado, sin cambio de código. Es una línea corta por Save/Flush, dentro de una llamada en la que el propio juego escribe ~2 MB en ese mismo hilo, y todo va en `try`/`catch` (una reserva fallida no sale del detour). Pero si el disco se cuelga, ese Save/Flush espera a `WriteFile`, y quien tenga `g_mu` (la línea periódica) también. Un unwind SEH a través de `DepthScope` (`guest_save_host.cpp`, en los dos detours) deja la profundidad alta en ese hilo: los Save posteriores salen como anidados y `probe` dice `unknown`, que es el lado conservador. Si hiciera falta, la alternativa es una cola en memoria que vacíe el hilo de diagnóstico. |
| L12 al restaurar se conserva la fecha de modificación antigua (`LastWriteTimeUtc` del manifiesto) | Documentado, sin cambio de código. El script pone a cada archivo restaurado la fecha que tenía en la caja, para que la pareja lobby/save siga siendo coherente. **[NV]** No se sabe cómo resuelve Steam Cloud los conflictos al reabrir Steam (fechas, tamaños, su propio registro de cambios): una fecha más antigua que la copia en la nube podría llevarle a preferir la de la nube o a preguntar. Antes de restaurar una partida que importe, probarlo con una de laboratorio y mirar qué propone Steam al reabrirlo. Si estorba, la alternativa es poner la hora actual a los dos archivos. |
| Tests que faltaban | renombrado del apartado imposible ⇒ bloqueo (`export_block`); fallo al escribir el manifiesto y al renombrar (inyección solo de tests, `host_save_test_inject`); reparse point dentro de una fuente; deduplicación contra una generación dañada; carpeta `save` falsa idéntica byte a byte antes y después de exportar y de `probe` |

Resultado: `.\scripts\build.ps1` **67/67**.