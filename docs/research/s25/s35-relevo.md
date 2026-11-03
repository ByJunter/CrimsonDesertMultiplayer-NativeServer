# Relevo S35/S36: auditoría S34 implementada en fases A, E y B (sin probar en el juego)

Fecha: 14-10-2026. Build del juego **1.0.0.2976**, PE timestamp **0x6AB28F00**, base **0x140000000**: todo RVA de
este documento vale sólo para esa build. Rama de trabajo **`opcion-d-s36`** (worktree `C:\Emulador CD-s36`).

Qué es: el plan para implementar la auditoría [AUDITORIA.md](s34-investigaciones/AUDITORIA.md) en cinco fases
(A cierres/contención, B familias, C viajeros, D rearmado/2043, E guardado del anfitrión) y cuatro kits (35: A+E,
36: B, 37: C, 38: D). **Están hechas A, E y B, sólo con pruebas unitarias.** C y D no están implementadas. Todo lo nuevo
va detrás de claves `0 | probe | 1` y `0` es el comportamiento anterior.

Reglas del usuario que siguen vigentes:
- Ahora mismo no importa el progreso propio del usuario como anfitrión: todo su tiempo libre es para pruebas.
- El invitado sigue el progreso de misiones del anfitrión y no tiene misiones propias.
- El usuario se comunica en español.

---

## 1. Estado de git

### Ramas y worktrees de este trabajo

| Rama | Worktree | Punta | Qué es |
|---|---|---|---|
| `opcion-d-npc-s33-fix` | `C:\Emulador CD-npc-s33-fix` | `11fd4e1` | La auditoría, en un commit con el mismo árbol (`b3019d79…`) y el mismo padre (`f79545c`) que `1d99ed0`. Limpio |
| `opcion-d-s35` | `C:\Emulador CD-s35` | `4ff4641` | = kit 35 (fases A + E). Incluye el merge de `opcion-d-s35-save` en `256f5a8`. Limpio |
| `opcion-d-s35-save` | `C:\Emulador CD-s35-save` | `a973fdb` | Fase E, ya unida en `256f5a8`. Limpio |
| **`opcion-d-s36`** | **`C:\Emulador CD-s36`** | la punta de la rama (código = `b5402d9`; después solo vienen este documento y el merge sin cambios de `opcion-d-npc-s33-fix`) | **Rama unificada: auditoría + A + E + B** |

Nada se ha subido al remoto. La rama original de la sesión, `opcion-d` (worktree `C:\Emulador CD-opcion-d`), es
anterior y **no** contiene este trabajo. Contiene `c7ae132` y todo lo anterior a `d835806`, que sí está en la historia
de s36. Su punta es `3d1e304`, un cambio de CLAUDE.md («sí se pueden ejecutar dos copias del juego en un PC») que
**no** está en s36: el `CLAUDE.md` de s36 todavía dice que no se puede.

### Commits de `f79545c` a `b5402d9` (orden cronológico; `|` = rama de la fase E)

| Commit | Qué hace |
|---|---|
| `f79545c` | (base) S34: encargo de la auditoría de las cinco investigaciones |
| `1d99ed0` | S34: auditoría independiente (`AUDITORIA.md` y `datos/auditoria-codex-20261014/`) |
| `637bb2d` | Fase A: GuestS3 nombra el `ClientHierarchyActorComponent` en una captura de cierre en vez de llamarlo CharacterControl |
| `8259901` | Fase A, código: evidencia H1b, contención con revalidación, degradación segura y `publication_gate` |
| `8007c12` | Fase A, documentación: `s35-fase-a.md`, claves, líneas, criterios de aborto, planes de un PC y Shadow, inis `cdmp.p8-s35-*` |
| \| `62e3773` | Fase E: exporta la caja de guardado del anfitrión antes de cada reconstrucción (`host_save=sandbox\|export\|probe`) |
| \| `48e7b96` | Fase E: script de restauración fuera del juego (`scripts/host_save_restore.ps1`) y `s35-fase-e-guardado.md` |
| \| `1396d12` | Fase E, revisión: comprobación de la carpeta resuelta (ids/reparse points), deduplicación verificada en disco, tests del script |
| \| `a973fdb` | Fase E, revisión L4/L5/L7/L12: documentadas; poda de diarios también en arranques sin cambios |
| `1431288` | Fase A, revisión: H1b con lectura de salida fallida, posposición acotada, dependencias y orden de la puerta |
| `f9c4bc0` | Fase A, documentación: sección de revisión, `npc_postpone_max_ms` en los inis, criterios de aborto de la puerta |
| `256f5a8` | Merge de `opcion-d-s35-save` en `opcion-d-s35`. **El `cdmp.asi` del kit 35 se compiló aquí** |
| `078363d` | Inis del kit 35: `host_save=probe` y exportaciones en `backups\` en los inis de anfitrión y de un PC |
| `4ff4641` | Diario: kit 35 (fases A y E). Sólo cambian inis y documentación respecto a `256f5a8` (código idéntico) |
| `93270ca` | Fase B: lógica pura de unidades familiares (reserva entera, marcos de Troc anidados, `parent_birth`, hojas a raíz) |
| `d0d595c` | Fase B: `npc_family_control 0\|probe\|1` en el `create_mid` compartido `0x2792A7B` |
| `992f5c6` | Fase B, documentación e inis: `s36-fase-b.md`, `cdmp.p8-s36-local/host-paso1` (probe) y `-paso2` (1), `cdmp.p8-s36-shadow.ini` = S35 |
| `b5402d9` | Fase B, revisión: marca de partida antes del registro, hijos fuera de marco, raíces sin hijos, fugas de reserva |

### Binarios

| Binario | Compilado desde | Tests | SHA-256 | Dónde está |
|---|---|---|---|---|
| Kit 35 | `256f5a8` (código = `4ff4641`) | 68/68 | `82AA0BEC3BE9CD3A3076DDBA59C06C565A408D52AD93779DFB5B4B27F7E87996` | `C:\Emulador CD-s35\build\bin\RelWithDebInfo\`, `dist\kit35\`, zip, y **bin64 del anfitrión** |
| Fase B | `b5402d9` | 69/69 | `E9F0CC58873E1308E2D7F90D29122F7C3B18584C7170C609DCADBE1E272E6A7C` | Sólo `C:\Emulador CD-s36\build\bin\RelWithDebInfo\`. **No desplegado en ningún sitio** |

Tests: 65 al empezar, 66 tras A (`optd_publication_gate`), 67 tras E (`optd_host_save` y `host_save_restore_script`),
68 en el merge y 69 tras B (`optd_npc_family`).

### Worktrees que se pueden borrar

| Worktree | ¿Borrar? |
|---|---|
| `C:\Emulador CD-s35-save` | **Sí**: `a973fdb` está en la historia de s36 |
| `C:\Emulador CD-s35` | **Sí, pero antes copiar `dist\`** (no está en git: `dist\kit35\` y `dist\cdmp-p8-shadow-s35.zip`). El zip ya tiene copia en la carpeta compartida; `dist\kit35\` no |
| `C:\Emulador CD-npc-s33-fix` | **Sí**: limpio, y `11fd4e1` ya es ancestro de s36 (se fusionó en s36 sin cambios de contenido, porque tiene el mismo árbol que `1d99ed0`) |
| `C:\Emulador CD-s36` | **Conservar**: es la rama de trabajo |

Hay otros worktrees que este trabajo **no** ha tocado:
- 37 worktrees viejos de agentes en `C:\Emulador CD\.claude\worktrees\`.
- `%USERPROFILE%\.codex\worktrees\npc-s33\…` (`opcion-d-npc-s33`), `D:\cdmp-wt\d09254d` y `D:\cdmp-wt\gimmicks`.
- **`C:\Emulador CD-2inst`** (`opcion-d-2inst`, `599b69c`): es otra línea de trabajo en paralelo (dos copias del juego en
  un PC). Une `opcion-d` y la fase B hasta `992f5c6` (sin `b5402d9`). Tiene **cambios sin commit** en `save_hooks.cpp`,
  `guest_save_guest.cpp`, `mod.cpp` y otros archivos, más inis `cdmp.2inst-*.ini`. No borrarlo. Al unirlo con s36
  habrá que revisar `save_hooks.cpp`, que la fase E también cambia.

---

## 2. Estado del PC del anfitrión

Carpeta `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64`:

| Archivo | Estado |
|---|---|
| `cdmp.asi` | Kit 35 (`82AA0BEC…`), instalado el 14-10 a las 21:50 con `scripts\deploy.ps1` desde `C:\Emulador CD-s35` (también `cdmp.pdb`) |
| `cdmp.ini` | = `docs/research/ini/cdmp.p8-s35-host-paso1.ini` (SHA-256 `3CC6150D…`, idéntico al de `C:\Emulador CD-s35`; la copia de s36 da otro hash `BC567A59…` sólo por los finales de línea CRLF; en git son iguales) |
| `cdmp.p8-s35-host-paso2.ini` | Copiado al lado para el paso 2 (SHA-256 `B39B446D…`) |
| Logs | El último `cdmp*.log` es de las 19:08 (S34): **el juego no ha arrancado con el kit 35** |

Copias hechas antes de instalar:
- Partidas: `C:\Emulador CD\backups\saves-antes-s35-20261014-2153`. Son 7 archivos y 2 686 266 bytes, una copia plana de
  `%LOCALAPPDATA%\Pearl Abyss\CD\save`.
- Ini anterior: `C:\Emulador CD\backups\cdmp.ini.antes-s35-20261014-2153`.
- Nota (comprobado el 14-10 con sólo lectura): en la carpeta real, `user_engine_option_save.xml` (21:58) y
  `<id-cuenta>\steam_autocloud.vdf` (21:56) se reescribieron **después** de la copia. Las ranuras `slot1` y `slot108`
  tienen los mismos tamaños y fechas que en la copia. No hay log del mod de esa hora ni se tocó la caja. Probablemente
  fue la prueba de dos copias del juego (`3d1e304`) **[NV]**.

**Fase E: todavía no ha actuado.**
- `C:\Emulador CD\backups\host_save_exports` no existe todavía.
- La caja actual, `%LOCALAPPDATA%\Pearl Abyss\CD\cdmp_save_sandbox` (9 archivos, 4 582 852 bytes, carpeta del 06-10),
  y su `_prev` son lo que exportará el primer arranque con `host_save=probe`.
- La fase E **nunca escribe la carpeta real de partidas**; sólo la lee para comparar.

**Restaurar una generación**, a mano y fuera del juego (`s35-fase-e-guardado.md` §7):
1. Juego **y Steam** cerrados.
2. Ensayo (no cambia nada):
   `.\scripts\host_save_restore.ps1 -Generation <gen> -Slot 0 -ExportDir 'C:\Emulador CD\backups\host_save_exports'`.
3. De verdad: lo mismo con `-Confirm`. Si `probe` no la dio por `candidate`, añadir `-AllowUnverified`. Opciones:
   `-Source prev|aside-<gen>`, `-Account`, `-BackupDir`.
4. Antes de tocar nada, el script copia **toda** la carpeta real a `backups\host_save_restore-<hora>\save` y la verifica
   por SHA-256. Se niega en estos casos:
   - el juego o Steam están abiertos;
   - el manifiesto está incompleto o es de otra build;
   - algún hash no coincide;
   - la ranura es 3, está en 200-207 o fuera de 0-2 / 100-108;
   - falta la pareja `lobby.save`/`save.save`;
   - la ranura es `not-candidate`;
   - hay restos `slotN_new`/`slotN_backup*`.
5. **Deshacer:** con el juego y Steam cerrados, copiar `backups\host_save_restore-<hora>\save` sobre la carpeta real.
   Al reabrir Steam, Auto-Cloud puede subir lo restaurado. Probarlo primero con una partida de laboratorio (§7, L12).

**Volver a antes del kit 35:** `.\scripts\deploy.ps1 -Remove` (quita `cdmp.asi`, `cdmp.pdb` y `winmm.dll`) y
recuperar el ini de `backups\cdmp.ini.antes-s35-20261014-2153`. `host_save=sandbox` devuelve la fase E exactamente
al comportamiento anterior.

---

## 3. Por fase

### 3.A Fase A — evidencia de progreso, contención, degradación y publicación segura (`s35-fase-a.md`)

**Claves `[session2]`** (§2). Valores literales; `0` = S34 exacto. Las tres primeras sólo actúan con `user2_stages` y
`npc_activation` distintos de 0; en el invitado no hacen nada.

| Clave | Defecto | `probe` | `1` |
|---|---|---|---|
| `npc_action_evidence` | `0` (emisor S34) | registra H1b; decide S34 | decide por H1b (§1.1) |
| `npc_containment_guard` | `0` | registra lo que pospondría | pospone por silencio/carga, gracia por Req, revalida al ejecutar (§1.2) |
| `npc_degradation` | `0` | cuenta los segundos en que habría evitado el aborto | no rearma el aborto de 5 s; pausa asignaciones (§1.3; exige `npc_activation=1`) |
| `publication_gate` | `0` | evalúa y registra todas las salidas; publica como antes | retiene lo incompleto (§1.4). Exige `observer_hold=1` y `create_session=1` |
| `npc_no_progress_ms` | `5000` | — | cota 1000..120000 (los inis del paso 2 usan 8000) |
| `npc_req_grace_ms` | `8000` | — | 0 = sin gracia; 500..120000 |
| `npc_guest_silence_ms` | `2000` | — | 0 = sin regla; 500..60000 |
| `npc_postpone_max_ms` | `60000` | — | tope total de la posposición; 5000..600000 |
| `publication_retain_max_ms` | `60000` | — | retención máxima antes de soltar **sin publicar**; 5000..600000 |

**Ganchos.** No hay ganchos nuevos: sólo cambia la lógica de los que ya estaban.

| RVA | Tipo | Comprobación |
|---|---|---|
| `0x2A78CA0` | inline en la entrada (pdata) | firma completa `kActionBytes` de 40 B, única (`npc_action_probe.cpp:22,136`; AUDITORÍA §7) |
| `0x2A79060` (H1b) | mid (7 B) | `kH1bBytes`, única (`host_scene_diag.cpp:63-65`; INFORME-A4, tabla binaria) |
| `0x281CEE0` | inline en la entrada | `kNodeBytes` de 32 B, única |

- La contención exige las tres sondas verificadas (línea `NpcActivation: sondas entrada acción=… H1b=… constructor nodo=…`).
- La limpieza sigue con `0x2A827B0` (SAM::FindActor), `0x1434880` y el mid O sobre el trampolín `0x2068340` (Logout).
- La puerta de publicación son costuras de `child_hold`/`observer_hold`, no ganchos.
- **El mod nunca llama a vf+0x170.**

**Líneas de log** (§3, anfitrión):

| Línea | Cómo leerla |
|---|---|
| `NpcActivation S35: npc_action_evidence=… npc_containment_guard=… npc_degradation=… \| npc_no_progress_ms=…` y `Session2: publication_gate ON\|PROBE` | arranque: modos efectivos |
| `NpcActivation [acción OnServer] … evidencia=<aplicada\|rama281\|rechazada\|retorno0\|otro_controlador\|otro_nacimiento\|retirada\|ilegible\|no_calculada> decide=<s34\|s34(probe h1b)\|h1b> … salida_discrepante=…` | `aplicada` = H1b de esa llamada; `emisor=no_medido` siempre |
| `NpcActivation [contención pospuesta] … motivo=<req\|silencio\|carga> … probe=<0\|1>` | la primera vez por unidad |
| `NpcActivation [retirada cancelada] … motivo=<progreso\|evidencia nueva\|posponer>` | revalidación al ejecutar; no es un rechazo |
| `NpcActivation [resumen S35] …` (cada 60 s) | evidencia por clase, `S34 y H1b distintas`, pospuestas, al ejecutar, degradación, `posposiciones_agotadas`, `degradada_sin_progreso` |
| `WRN NpcActivation [contención degradada] …` / `[posposición agotada]` | transición de degradación / tope de posposición |
| `Session2 publication gate: RETAINED, not published 0x… : <motivo>` o `WOULD RETAIN (probe…)` | motivos: `restore 0 (observer is not its controller)`, `CC unreadable (not proven absent)`, `actor unreadable`, `controller unreadable`, `its parent is withheld…`, `its parent was refused in this release` |
| `WRN Session2 publication gate: DROPPED WITHOUT PUBLISHING …` | invisible para el invitado (expiró o no había sitio) |
| `… observer hold\|family table full: … withheld WITHOUT an entry` | retenido sin entrada: vuelve sólo si el motor lo anuncia otra vez |
| `Session2 probe (minute N): … \| publication gate (on\|probe): releases evaluated … refused (restore0, cc_unknown, actor_unknown, controller_unknown, dependency, parent_in_batch) … \| probe shadow … would recover … would drop …` | balance de cada minuto |
| `WRN … a NotifyAppear of N key(s) went out UNCHECKED` | `prepare()` falló: salió sin puerta |
| Invitado: `GuestS3 crash capture: <reg> 0x… is the ClientHierarchyActorComponent (vtable exe+0x5578BB8) …` | sólo si cierra |

**Criterios de aborto** (§4). Ante cualquiera: parar, `deploy.ps1 -Remove` y devolver los logs.
- Cualquier cierre. Con la puerta a 1, un cierre en `0x88F832`/`0x1831598` invalida la hipótesis de cobertura.
- Paso 2:
  - un 2005 con restore 0 hacia Shadow de un actor que no controla (contarlo con `a1-cierres/audit_captures.py`);
  - una `retirada local` de una unidad con `evidencia=aplicada|rama281` previa;
  - un `[CONTROL ABORT] … no accepted StartActionReq 5 s` con degradación a 1;
  - `asignaciones_pausadas` > 0 sin `[contención degradada]`;
  - cualquier `UNCHECKED NotifyAppear`.
- `DROPPED WITHOUT PUBLISHING`/`would no entry` > ~20 en 10 min, o `CC unreadable … would` en tipos visibles: no pasar
  al paso 2.
- Paso 1:
  - `evidencia=aplicada` en 0 con Req del invitado;
  - `dependency`/`cc_unknown` > ~10 % de `releases evaluated`;
  - `would drop` alto frente a `would recover`.
- `posposiciones_agotadas` > 0 con el invitado jugando normal; `degradada_sin_progreso` > 0 durante minutos.
- Progreso en la partida propia del invitado, o una acción suya que no acredite la misión del anfitrión.

**Qué punto de la auditoría resuelve:**

| Cambio | Punto |
|---|---|
| Evidencia H1b (§1.1) | §4 tabla «param4 es la sesión [F]» y «Cambios obligatorios» (estados separados); §8 orden 1; §6 problema adicional 5 (lecturas del detour probadas con marco simulado, revisión 5) |
| Contención (§1.2) | §4 «Cambios obligatorios» (no retirar por cola/silencio/carga; Req suspende sin proteger para siempre; versión y revalidación al retirar, `user2_stages.cpp:1913`); INFORME-A4 puntos 1-8 |
| Degradación (§1.3) | §4 último párrafo; INFORME-A4 punto 7 |
| `publication_gate` (§1.4) | §1 «Cambios obligatorios» (todas las vías, no sólo Timeout); §6 problema adicional 3 (tres estados del CC); §8 orden 2 |
| GuestS3 (§1.5) | §1 tabla y §6 primer cruce (`B01004BF` es el actor y `B0100578` lo consulta) |

**Desviaciones respecto a la auditoría y por qué** (§6 y §8):
- **El emisor real no se mide** (`emisor=no_medido`): haría falta un mid nuevo en `0x2A78CF8` sin medida en vivo. La
  auditoría permite «dejarlo explícitamente desconocido».
- **La puerta no cubre todas las salidas.** Quedan fuera:
  - `character_hold` (`:697-716`), `child_settle` y D-53;
  - en `prepare`: controlador ilegible, actor fuera del SAM, `CtrlNotUser`, familia `partial`, `unlinked` y sesión
    ilegible.

  Se cuentan, pero no tienen política. La auditoría pedía «todas las rutas pertinentes».
- **La dependencia es de un nivel**, siguiendo el vínculo serializado o jerárquico; otros lectores no están
  enumerados.
- **Degradación:** las unidades ya asignadas sin progreso sólo se cuentan y se avisan (revisión 3). Falta diseñar una
  retirada acotada por unidad.
- **Ventana residual** entre la revalidación y los `Logout` (`child_hold_retire_family` no puede ir bajo
  `ActivationLock`).
- **No probado de extremo a extremo:** `npc_activation_cleanup`, `npc_control_impl` y `DropNoRoom`, porque no hay
  costuras de prueba. La lógica está extraída a funciones puras y probada ahí.
- **Plazos:** 8 s / 2 s / 8 s son puntos de partida, como dice la auditoría.

Revisión independiente: **«GO con correcciones», 15 hallazgos, todos tratados**. El 3 y el 15 quedan
parciales/documentados (§8).

### 3.B Fase B — familias NPC enteras (`s36-fase-b.md`)

**Claves `[session2]`** (§3, anfitrión). `0` = fase A exacta (`probe_changes_nothing`).

| Clave | Defecto | `probe` | `1` |
|---|---|---|---|
| `npc_family_control` | `0` | mide: preexamen, marcos y decisión por miembro con `can_reserve`; no escribe EAX (§2.9) | §2.2-2.8 |
| `npc_family_types` | `3,6` | — | lista de 3, 6, 7; otra cosa → WRN y `3,6`. **`7` no está en ningún ini** |

- `npc_family_control=1` exige `user2_stages=1`, `user2_npc_control=1`, `npc_activation=1`, `npc_action_evidence=1`,
  `npc_degradation=1` y `publication_gate=1`, y éste a su vez `observer_hold=1` y `create_session=1`. Si falta alguno
  sale `WRN NpcFamily: npc_family_control=1 necesita …: queda en probe`.
- En el invitado la clave sólo observa (`npc_probe_client`).
- Atención: en los inis S33-S35 ya figuraba `npc_family_control=probe`. Era la sonda de familias de S33; hasta la
  fase B no aceptaba `1`.

**Ganchos.** Ninguno nuevo. Se reutiliza el `create_mid` **`0x2792A7B`** de `docked_gimmicks`, que sigue siendo su único
dueño (`ctl_write_mid` → `user2_stages_npc_control`):
- AOB de 24 B `41 89 86 90 00 00 00 48 8B 06 4D 8B CE 4D 8B C5 48 8D 55 67 48 8B CE FF`, única (AUDITORÍA §7);
- `rsi` = descriptor interior, `r13` = campo, `r14` = actor.

Contrato leído de Ghidra (§2.1):
- `+0x2F0` = padre;
- vector `+0x2F8` de punteros de 8 B (`count +0x300`, `cap +0x304`);
- constructor `0x278EA70`, enlaces `0x281DBD0`/`0x2820D80`, `0x43DB70`, `0x281CEE0`;
- vtables de `a2-familias/rtti.json`.

Clases de nodo: `_Rider`, `_NPCScheduleGroupActionMember`, `_CatchTarget`, `_SavedAttached`, `_LinkedVehicle` y
`_Docking` quedan **excluidas**, y con ellas la unidad entera se queda con el anfitrión.

**Líneas de log** (§4):

| Línea | Cómo leerla |
|---|---|
| `NpcFamily S36: npc_family_control=<0\|probe\|1> (pedido …) npc_family_types=3,6 \| …` o `WRN … queda en probe` | arranque |
| `NpcFamily [censo] … raíz=0x… unidad=<admitida\|se admitiría\|con el anfitrión> motivo=… estado=<completa\|PARTIDA\|pendiente\|con el anfitrión> nodos= nacidos= miembros(_que_asignaría)= host-kept= partidos= tardíos= pendientes= …` | una por raíz con hijos, al cerrar el marco de su Troc |
| `WRN NpcFamily [partida] … motivo=…` | un miembro que no pudo seguir a su raíz |
| `NpcFamily [tardío en la publicación] … decisión=<vínculo distinto del mid\|tardío host-kept (tipo 7)\|tardío PARTIDO (otro controlador)\|tardío del mismo controlador>` | hijo que nació fuera de su marco |
| `WRN NpcFamily [bloqueo hoja-raíz] …` | un descendiente no se retiró; no se llama a `Logout` del padre |
| `NpcFamily [miembro sin acción] …` | diagnóstico; no retira |
| `NpcFamily [resumen] …` (cada 60 s, tras `[resumen S35]`) | admitidas, motivos de rechazo, miembros, tardíos, `huérfanos_al_nacer`, `marcos_agotados`, censo, `coste_mid` (`máx_us`, `medio_us`), `reservas vivas` |
| `User2Stages summary … [family unit not admitted] N` | veredicto nuevo `Ctl::Family` |

**Criterios de aborto** (§5):
- Cualquier cierre.
- Modo 1:
  - `estado=PARTIDA` o `[partida]` más de 1-2 veces en 10 min, o `partidas_marcadas` creciendo;
  - cualquier 2005 con restore 0 hacia Shadow (sobre todo de hijos de unidades admitidas);
  - cualquier `[CONTROL ABORT]`;
  - `HostDrives` sobre un perro o una cabra asignados.
- `reservas vivas` ≠ 0 sostenido, o `marcos_agotados` > 0.
- `coste_mid máx_us` > 2000 o `medio_us` > 200.
- Misión propia del invitado, o una acción suya que no acredite la misión del anfitrión.
- Un animal de una unidad admitida invisible más de unos segundos, o duplicado.

**Qué punto de la auditoría resuelve** (A2-n = «Cambios obligatorios» de AUDITORIA-2-FAMILIAS):

| Cambio | Punto |
|---|---|
| Padre conocido en el mismo mid (§2.1) | §2 tabla; A2 «Direcciones de enlace» |
| Reserva de la unidad entera antes de EAX (§2.2) | A2-1; §6 problema adicional 2 |
| Pila de marcos por Troc anidado (§2.3) | A2-2 |
| `parent_birth` y token de unidad (§2.4) | A2-3; §6 problema adicional 1 |
| CC/vínculo `Unknown` ≠ `Absent` (§2.5) | A2-4; §6 problema adicional 3 |
| Raíces no asignadas registradas, miembro sin veredicto propio, salidas por unidad, hojas a raíz, coste (§2.6) | A2-5, A2-6, A2-7, A2-10; §2 «`not type3` …» [F] |
| Cohorte 3/6 (§2.7) | §8 orden 3; §6 «Fronteras» |
| Sólo `actor+0x90` (§2.8) | §6 «Fronteras de autoridad» |

**Desviaciones respecto a la auditoría y por qué** (§2.7, §8):
- **Tipo 7 «host-kept».** Un hijo gimmick tipo 7 de una unidad admitida se queda con el anfitrión mientras su raíz va
  al invitado. Sólo se permite con `publication_gate=1`. Esto es **control mixto dentro de la unidad**.
  - La auditoría lo aceptaría sólo con el contrato demostrado. §6 «Fronteras»: «una excepción tipo 7 sólo puede
    aceptarse después de demostrar su contrato; hoy esa seguridad no está probada».
  - INFORME-A3, para la caravana: «no usar esta excepción como primera activación».
  - El documento se apoya en la puerta de la fase A como protección. **Es el punto que más merece la segunda
    revisión.**
- **Raíces tipo 6 sin padre:** siguen con el anfitrión (regla de U-e).
- **No se añadió** la pasada específica de I2 §3.8 para los host-kept tras un drenaje: los quita el drenaje nativo.
- **Desconexión sin siguiente Troc** (§6 problema adicional 4): sin cambios.
- **Un superviviente de otra encarnación** nunca visto en un mid sigue siendo indistinguible: es el residual de §6.1, y
  sólo afecta a la retirada.
- **No probado de extremo a extremo:** `npc_control_impl` y `family_child`, porque no hay costuras de prueba.

Revisión: **10 hallazgos de un revisor independiente lanzado por el propio autor de la fase**, corregidos en `b5402d9`
(§9). **La fase B no ha tenido la segunda revisión encargada por el coordinador, como sí la tuvieron A y E.**
Recomendación: encargarla antes del kit 36.

### 3.E Fase E — exportar el guardado del anfitrión antes de rehacer la caja (`s35-fase-e-guardado.md`)

**Claves `[session2]`** (§1):

| Clave | Defecto | Valores |
|---|---|---|
| `host_save` | `sandbox` (lo de antes) | `export`: copia verificada de la caja, `_prev` y lo apartado antes de la rotación. `probe`: además clasifica cada ranura (`candidate`/`not-candidate`/`unknown`) con el diario. **`real` se rechaza** (ERR, y usa `sandbox`) |
| `host_save_dir` | vacío = `<bin64>\cdmp_save_exports` | recomendado y usado en los inis: `C:\Emulador CD\backups\host_save_exports`. Se rechaza si está dentro de `Pearl Abyss`, si contiene `save`, si está bajo `steam_autocloud.vdf` o `Steam\userdata`, o si hay un reparse point |
| `host_save_keep` | `20` | 1-500; la más nueva nunca se poda |

Requisitos: `save_guard=redirect`. Con `guest_own_save=0`, `probe` dice `unknown`.

**Ganchos.** Ninguno nuevo.
- Se amplían los ganchos de entrada ya existentes de `guest_save_host.cpp`: `SavePlayerToSlot 0x186E450` y
  `FlushPendingSaveData 0x1871930`. Sus firmas completas de 47/43 B son únicas (AUDITORÍA §7). Escriben el diario.
- La exportación corre en `prepare_sandbox` (`save_hooks.cpp`), **antes** de crear ningún gancho, en el hilo de
  arranque tardío y fuera de `DllMain`.

**Líneas de log** (§6):

| Línea | Cómo leerla |
|---|---|
| `Session2 host save: host_save=probe \| the previous sandbox is exported to '<dir>' … \| the real save folder is never written` | modo activo |
| `Session2 host save EXPORT: generation '<gen>' made in '<dir>' and verified (…); valid generations N (keep K), pruned: …` | exportación hecha |
| `… nothing new to export` / `… is identical to the verified generation G/…: not copied again` | deduplicación (no hay generación nueva) |
| `Session2 host save probe: <gen>/sandbox <cuenta>\slotN = candidate\|not-candidate\|unknown: <motivos>` | una por ranura; `candidate` ≠ partida segura |
| `ERR Session2 host save: the export FAILED (<motivo>): nothing is deleted; …` + `set aside as '…_unexported_<gen>'` | fallo: la caja se aparta y `_prev` no se toca |
| `ERR Session2 save redirect: the export failed and 3 sandboxes are already set aside (the limit) … falls back to block` | límite: vuelve «Failed to save data» |
| `Session2 host save: this run is '<run>', journal '<ruta>' (sandbox id V:F)` | diario de la sesión |
| `… generation G is damaged …: treated as invalid` / `… is a link (reparse point) …` | integridad |
| periódica `save guard REDIRECT (…; host save probe: generation G exported at start, journal N events)` | resumen |

**Criterios de parada** (§9). No hay una lista de aborto propia; según el plan de §9:
- Si la carpeta real cambia de fechas o tamaños (paso 7 del plan), parar.
- Si sale un `export FAILED` no provocado, parar.
- Si no sale `this run is …`, parar.
- En cualquier duda, `host_save=sandbox` devuelve el comportamiento anterior.

**Qué punto de la auditoría resuelve.** §5 (diagnóstico y `sandbox/probe` aprobados, `real` bloqueado), §6 y §8 orden 6.
Los puntos 1-4 de §5 están sólo como **observación** (tabla de §5 del documento):
1. identidad positiva aparte del fallback;
2. profundidad anidada;
3. `FAILED n` y restos;
4. pareja con ≤10 s.

**Desviaciones y lo que sigue bloqueado** (§8):
- **`host_save=real` bloqueado.** Faltan:
  - procedencia completa, también del buffer `mgr+0x5C`;
  - contaminación versionada entre hilos;
  - cobertura de E/S;
  - pareja indivisible;
  - publicación durable;
  - M1 y M2 ampliadas.
- **Steam Cloud [NV]:** la configuración efectiva no se ha leído.
- **La ranura 3 (CrossSave)** nunca se restaura.
- **`candidate` no demuestra autoría.**
- **Revisión independiente: «GO con correcciones»**, sin BLOCKER ni HIGH: 3 MEDIUM y 14 LOW tratados (§11). **L4, L7 y
  L12 sólo documentados:**
  - L4: «verified» lee de la caché del sistema;
  - L7: `WriteFile` síncrono bajo `g_mu` en el hilo del servidor;
  - L12: el restore conserva la fecha antigua y no se sabe qué hará Cloud.

---

## 4. Qué está probado y qué no

| | Probado | No probado |
|---|---|---|
| Unitarias | `build.ps1` 69/69 en `b5402d9` (68/68 en el kit 35): lógica pura, matrices, fixtures A/B/C de S34, contraejemplos de A2, costuras de `child_hold`, exportación sobre carpetas falsas en `%TEMP%`, 5 procesos hijo con los ganchos reales de redirect, el script de restauración con `LOCALAPPDATA` redirigido | De extremo a extremo: `npc_activation_cleanup`, `npc_control_impl`, `family_child` y `DropNoRoom` (no hay costuras) |
| Un PC con `cdmp_peer` | — | **Nada**, en ninguna fase |
| Arranque del juego | — | **Nunca** con el kit 35 ni con la build de B: no se sabe si se instalan todos los ganchos |
| Shadow | — | Nada de A, B ni E |
| Carpeta real | Sólo lectura en tests falsos; el restore nunca se ha ejecutado contra la real | Exportación real, `probe` real, restauración real, Cloud |
| Revisiones | A y E: revisión independiente del coordinador. B: sólo el revisor del propio autor | Segunda revisión de B |

**Siguiente paso pendiente, en este orden:**
1. Un arranque en un PC para ver que todos los ganchos se instalan (el usuario tiene que pulsar Jugar). Ese mismo
   arranque hará la primera exportación de la fase E.
2. La prueba en un PC con el peer (`s35-fase-a.md` §5.1).
3. Shadow.

---

## 5. Kits

### Kit 35 (A + E)

**Ficheros:**
- Zip: `C:\Emulador CD-s35\dist\cdmp-p8-shadow-s35.zip` (3 900 152 bytes); copia en
  `<onedrive>\cdmp-shadow\invitado\cdmp-p8-shadow-s35.zip`.
- Contenido:
  - `cdmp.asi` (`82AA…`);
  - `cdmp.ini` = `cdmp.p8-s35-shadow.ini`, idéntico al ini de invitado S34b (`8e8240f5…`);
  - `cdmp.ini.planB` y `winmm.dll` (sin cambios, `031a3e55…`);
  - `aplicar-progreso-invitado.ps1`, `LEEME-P8.txt`, `LEEME-S35.txt` y `SHA256.txt`.
- `dist\kit35\` tiene además un `LEEME-S34.txt` que no va en el zip.

**Inis:**

| Uso | Ini | Claves relevantes |
|---|---|---|
| Anfitrión, paso 1 | `cdmp.p8-s35-host-paso1.ini` (ya en `bin64\cdmp.ini`) | `npc_activation=probe`; claves S35 en probe; `npc_no_progress_ms=5000` |
| Anfitrión, paso 2 | `cdmp.p8-s35-host-paso2.ini` (al lado en bin64) | `npc_activation=1`; claves S35 a 1; `npc_no_progress_ms=8000` |
| Un PC, paso 1 / 2 | `cdmp.p8-s35-local-paso1.ini` / `-paso2.ini` | `npc_activation=1` en los dos; S35 en probe / a 1 |
| Invitado | `cdmp.p8-s35-shadow.ini` | `npc_activation=probe`, `quest_per_user=0`; no cambia entre pasos |

- Todos los inis s35 de anfitrión y de un PC llevan `host_save=probe`,
  `host_save_dir=C:\Emulador CD\backups\host_save_exports` y `host_save_keep=20`, además de `save_guard=redirect`.
- Los de anfitrión llevan `quest_policy=probe`.
- `npc_family_control=probe` y `user2_road_control=probe` son las sondas S33/S34, no las fases B y C.
- Existe además `cdmp.p8-s35-local-guardado.ini` (S33 + fase E) para la prueba de guardado sola.

**Instrucciones de Shadow** (resumen de `C:\Emulador CD-s35\dist\kit35\LEEME-S35.txt`):
- Sólo cambia `cdmp.asi`. Shadow copia `cdmp.asi` y `cdmp.ini` encima; `winmm.dll` no cambia. En Shadow no se cambia de
  ini entre pasos: el paso se cambia en el anfitrión.
- **Paso 1** (sólo mide):
  1. El anfitrión, lejos. El invitado va a Hernand y pulsa Ctrl+F12 al llegar.
  2. 3 minutos por la ciudad buscando perros, cabras, un pastor con ovejas, una cría de ciervo y carros, con Ctrl+F12
     cerca de cada uno.
  3. El anfitrión viene a Hernand y se vuelve a ir.
  4. Salir por el menú y cerrar el juego.
- **Paso 2** (cuando se le diga):
  1. El anfitrión, lejos. En Hernand los NPC no deben aparecer y desaparecer en bucle, ni cerrarse el juego. Ctrl+F12
     al llegar, al minuto y a los 3 minutos.
  2. Mirar los animales: avisar del que no aparezca nunca o se quede congelado.
  3. El anfitrión viene, hace un viaje rápido y vuelve; el invitado sale al título y vuelve a entrar (Continuar).
  4. Salir y cerrar el juego.
- Si el juego se cierra, no reabrirlo. Mandar todos los `cdmp_2026*.log`, `cdmp_packets_*` y `cdmp_crash_*.dmp`.

**Pasos y qué mirar:**

| Paso | Cómo | Qué mirar / éxito | Parar si |
|---|---|---|---|
| 0. Arranque en un PC | Con el `bin64\cdmp.ini` actual (paso 1 del anfitrión). Juego por `steam://rungameid/3321460`; el usuario pulsa Jugar. Si se queda en el título, cerrar con `Stop-Process` | Instalación de cada gancho (`NpcActivation: sondas entrada acción=… H1b=… constructor nodo=…` todas verificadas), `NpcActivation S35: …`, `Session2: publication_gate PROBE`, `Session2 host save: host_save=probe \| …`, la exportación de la caja actual (`EXPORT: generation … verified` o `nothing to export`) y `this run is …, journal …`. Comprobar que existe `backups\host_save_exports\<gen>\manifest.txt` con `state=complete` | Un gancho sin verificar, `export FAILED`, cambios en la carpeta real |
| 1. Un PC + peer (`s35-fase-a.md` §5.1) | `bin64\cdmp.ini` = `cdmp.p8-s35-local-paso1.ini`, Hernand. Generar antes el guion: `py -I -B tools\scripts\make_u1_idle_script.py docs\research\data\grapple_s25_g1_walk_138_217.troc build\e2e\u1_idle.troc --minutes 15 --every-ms 1000` (`u0-resultados-s28.md` §8; **no existe todavía** en ningún `build\e2e`). Peer: `.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script build\e2e\u1_idle.troc --var ACCOUNT=1 --var SAVE_SLOT=108 --start-after 3000 --linger 30000 --seconds 1200`. Alejarse para armar U-1 y esperar 5 min. Después, el paso 2 con `-paso2.ini` | Paso 1: `[contención pospuesta] … motivo=silencio … probe=1`, `pospondría silencio` > 0, `publication gate (probe)` con `would` contado y sin cierre. Paso 2: ninguna `retirada local` por falta de activación, ningún `[CONTROL ABORT]` de 5 s, `retained`/`dropped unpublished` acotados; al volver junto al peer el drenaje es como antes. Con los mismos inis se cumple el plan E §9 (arranque 2: `probe` por ranura; la carpeta real no cambia) | §3.A criterios |
| 2. Shadow, paso 1 (mide) | Anfitrión `cdmp.p8-s35-host-paso1.ini`; Shadow con el zip | `evidencia=aplicada` > 0 en unidades con Req; `S34 y H1b distintas` ≈ unidades aplicadas; `CC unreadable … would` explicable; cada 2005 con restore 0 hacia Shadow aparece como `WOULD RETAIN` | Criterios de paso 1 de §3.A |
| 3. Shadow, paso 2 (actúa) | Copiar `cdmp.p8-s35-host-paso2.ini` sobre `bin64\cdmp.ini` (juego cerrado); Shadow igual | Cero restore 0 hacia Shadow de actores que no controla; cero retiradas con aplicación previa; posposiciones sólo con silencio o carga reales; retenciones que se resuelven solas; sin cierre. Anotar lo `DROPPED WITHOUT PUBLISHING` y si se nota en pantalla | Criterios de paso 2 de §3.A |

Copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups\` antes de cada prueba que escriba memoria (CLAUDE.md).
Ya hay una de las 21:53.

### Kit 36 (B) — preparado, **sin empaquetar a propósito**

Espera a los resultados del kit 35 en Shadow, y la recomendación es encargar antes la segunda revisión de B.

**Inis** (en `C:\Emulador CD-s36\docs\research\ini\`):

| Ini | Base | Añade |
|---|---|---|
| `cdmp.p8-s36-local-paso1.ini` / `-paso2.ini` | = `cdmp.p8-s35-local-paso2.ini` (fase A activa) | `npc_family_control=probe` / `1`, `npc_family_types=3,6` |
| `cdmp.p8-s36-host-paso1.ini` / `-paso2.ini` | = `cdmp.p8-s35-host-paso2.ini` (fase A activa, `host_save=probe`) | ídem |
| `cdmp.p8-s36-shadow.ini` | = `cdmp.p8-s35-shadow.ini` byte a byte (`8e8240f5…`) | — |

**Para empaquetarlo:**
1. Desplegar `E9F0CC58…` desde `C:\Emulador CD-s36` (`.\scripts\deploy.ps1`, con el juego cerrado).
2. Copiar los inis s36 al lado en bin64.
3. Hacer el zip con `cdmp.asi`, el ini s36 de Shadow, `winmm.dll` sin cambios, un `LEEME-S36.txt` nuevo (por escribir) y
   `SHA256.txt`.

**Pasos** (`s36-fase-b.md` §6):

| Paso | Qué mirar / éxito | Parar si |
|---|---|---|
| Un PC + peer, paso 1 (`-local-paso1`) | `NpcFamily S36: npc_family_control=probe`; `[censo]` de guardias con perro, pastores y porteadores; `preexamen_fallido` y `vínculo_ilegible` ≈ 0; `hijo_antes_que_raíz` = 0; `pendientes` ≈ 0; `reservas vivas` 0; sin cierre | §3.B criterios |
| Un PC + peer, paso 2 (`-local-paso2`) | `npc_family_control=1` sin WRN; `unidad=admitida`; ningún `[partida]` ni `[CONTROL ABORT]`; al volver, `ctl_cleanup` retira los hijos antes que los padres; `reservas vivas` 0 | ídem |
| Shadow, paso 1 (`-host-paso1`) | `unidad=se admitiría` en `B01004A4`/`B01004A5` y equivalentes; `estado=completa` ≥ 95 %; ningún partido `vínculo +0x2F0 distinto`/`ilegible`; `pendientes` y `tardíos` ≈ 0; `claves_llenas`/`activación_llena`/`marco_lleno` = 0; coste dentro de §5; coincidencia con los 2005 de la captura de Shadow | Si `pendientes`/`tardíos` son frecuentes, **no pasar** (el modo 1 retiraría esas unidades) |
| Shadow, paso 2 (`-host-paso2`) | guardia + perro y niña + beagle: `admitida estado=completa`, y el 2005 del perro con el controlador del usuario 2 en la misma llamada que la raíz; pastor + cabras/ovejas; los Game Mechanic tipo 7 `host-kept` visibles; primera Req de la raíz en 1-3 s; el perro sigue y combate sin duplicados; la familia entera en los dos clientes; transiciones (cerca/lejos, viaje rápido, salida/vuelta del invitado) sin `[control leftover]`; la misión acredita sólo al anfitrión | §3.B criterios |

---

## 6. Lo que falta del plan: fases C y D

**Orden de los kits:** 35 (A+E) → 36 (B) → 37 (C) → 38 (D).
- Cada kit pasa por un arranque en un PC, la prueba con el peer, el paso 1 de Shadow (probe) y el paso 2 (activo).
- **No se activan varias políticas nuevas a la vez** (AUDITORÍA §8, prueba 6).
- Condiciones para pasar al siguiente kit:
  - al 36, que el paso 2 del 35 cumpla sus criterios de éxito;
  - al 37, que el paso 2 del 36 cumpla los suyos;
  - y así sucesivamente.
- `host_save=real` (§8 orden 6) sigue bloqueado aparte.

### Fase C — viajeros 2550 del invitado (kit 37)

**Qué hay diseñado** (`3-viajeros.md` §4, `user2_road_control=1`):
- La autoridad se decide al nacer, en el **mismo** mid `0x2792A7B`, por la procedencia del descriptor:
  - vtable `0x5B04BE8`, `+0x3CD == 1`, `+0x3C8 == g_u2_key` y `+0x3B8 == g_u2_char`;
  - la escribe `0x27EA160`, y el constructor `0x278EA70` la pone a 0.
- Veredicto puro `road_verdict` (tipos 3/5/6; 4/7 sólo contados) y tabla propia `g_road_keys`, independiente de
  U-1/U-e.
- Limpieza sólo en la carga y el Logout del invitado. **No se devuelven** al anfitrión cuando éste se acerca.
- Parada local con `g_road_stopped`.
- Hoy existe `user2_road_control=probe` (S33/S34). Registra el 2005 tipo 3 encolado (`NpcActivation [viajero]`), **no
  lee el descriptor al nacer**.

**Cambios obligatorios de la auditoría** (§3, INFORME-A3 §3-§4):
1. **Procedencia.** `+0x3CD=1` **no** es exclusiva: `0x28981B0` construye el mismo descriptor con `0x278EA70`,
   recupera `AutoSpawnOwnerData` y escribe la palabra `+0x3CC=0x101` en `0x28995E9` (`66 C7 80 CC 03 00 00 01 01`).
   - Hace falta una sonda positiva y negativa que distinga `27EA160`, `28981B0` y otros.
   - Hay que cerrar la relación descriptor→unidad→2005, por tipo y familia.
   - La marca sola, o vtable + marca, no prueban un spawn nuevo.
2. **Registro antes de autoridad.** Un único mid y reserva por época/nacimiento **antes** de EAX. `KeyTable` no guarda
   época ni token. `session_key2=0` no puede contar como coincidencia.
3. **Limpieza estricta y aislada.** `cleanup_one` tal cual no sirve:
   - su `cleanup_fault` toca `g_activation_fault`/`g_ctl_aborted` (`user2_stages.cpp:1627-1637` en `d835806`);
   - con `strict=false` convierte errores desconocidos de `Logout` en `Done` (`npc_activation_logic.h:121-124`).

   Hace falta un resultado estricto, la desaparición confirmada y no ocultar excepciones del motor.
4. **Continuar en el servidor tras la desconexión.** «64 por llamada + siguiente Troc» no es garantía: se vieron 91/81
   pendientes en B/C, y puede no llegar otro Troc.
5. **Cancelar la publicación pendiente:** `child_hold`, `observer_hold_acks`, altas 2005 y acciones encoladas.
   Tombstones por época + nacimiento; ningún 2005 viejo resucita una baja.
6. **Unidad entera.** Los 34 hijos tipo 7 (21/13) no tienen metadatos. No activar una raíz dejando sus tipo 7 con el
   anfitrión sin contrato. Distinguir bandadas, carros, caballos, monturas, mascotas, compañeros y NPC de misión.
7. **Una Req del anfitrión es un indicio, no prueba doble control.** Hay que medir autor, resultado y efecto.
8. **Misiones sólo del anfitrión**, y seguridad ≠ visibilidad (`WithheldUser2`).

Ciclo de vida:
- Mantener el controlador invitado cuando llega el anfitrión es razonable [P].
- Viaje, cambio de campo y logout exigen cancelación y retirada terminal.
- Alejarse cabalgando sin cargar es [NV]: medir, sin inventar radio.

**Por dónde empezar y qué medir primero:**
1. Ampliar la sonda en el mid `0x2792A7B`, sólo lectura, en `probe`. Por nacimiento debe registrar:
   - vtable, `+0x3CD`/`+0x3CC`, `+0x3C8`, `+0x3B8` y `+0x3D0..+0x3E8`;
   - el `dock_parent` de los tipo 7;
   - el veredicto `road_verdict`;
   - una forma de reconocer el productor (`27EA160` frente a `28981B0`). **Cómo reconocerlo desde el mid no está
     diseñado**: es la primera decisión.
2. Prueba en un PC con el peer: con los **viajeros del propio anfitrión** (`HostRequester`), 1:1 por clave con los 2005
   de `fin+88 = A0100001`, acompañantes incluidos. Los descriptores con marca y vtable o productor inesperados deben
   ser 0.
3. Shadow probe, como el paso 2b de S34 (anfitrión quieto a ≥ 60 celdas durante ≥ 60 s): «would» = cohorte 2005 del
   invitado, 0 discrepancias; tipo 7 medido.
4. Separar el resumen del invitado (`npc_probe_client.cpp:301` mezcla viajeros propios y ajenos).
5. Diseñar la limpieza estricta y continuable (tests puros con 65/91/128 pendientes sin Troc) **antes** del modo 1.

Reutilizable de la fase B: marcos por Troc, reserva entera, `parent_birth`, retirada de hojas a raíz y la puerta de
publicación. Éxito en el modo 1: paridad del 100 % en registro, control y familia, y movimiento X/Z > 1 m donde toque.
«≥ 90 % de Req en ≤ 3 s» **no basta**: S34 ya tenía Req rápidas sin locomoción.

### Fase D — rearmado y 2043 (kit 38)

**Qué hay diseñado** (`4-contencion-y-relevo.md` §4, arreglos 3, 5 y 6):
- (3a) Armar sin histéresis ante un salto de ≤ 7 a ≥ 10 celdas.
- (3b) Un 2043 de un solo disparo al armar (`p[11]=0`, `delay 1` = 100 ms) en `arm_locked`.
- (5) Conservar las stages del invitado mientras el anfitrión está de visita. Es estructural y la auditoría lo deja
  **fuera**.
- (6) Ignorar el WRN `stages left … after a drain` durante el drenaje.

Base ya en la historia: `53d70df` (reinicia la cadena 2043 del invitado tras un armado) y `883864e` (periodo en ms,
un reinicio por armado).

**Cambios obligatorios de la auditoría** (§4, parte de rearmado; INFORME-A4 «Rearmado rápido»):
- **Puntual:** uno pendiente por sujeto/época/armado, con resultado de encolado comprobado y consumo o cancelación
  trazables, sólo en el hilo del servidor con el cerrojo existente.
  - Separado de `queue_chain_2043`, que siempre pone `+0xB=1` y actualiza `g_chain_last_us`.
  - **Un puntual no sustituye la cadena periódica**: hay que mantener su reparación.
- **Estadísticas:** `timer_hit` actualiza `g_last_mgr2_tick_us` para los dos tipos de tick; no se debe enmascarar una
  cadena muerta con puntuales. Medir el retraso de cadenas viejas: «sin ticks en 1,5 periodos» puede confundir atraso
  con muerte.
- **Invalidar el trabajo viejo** al drenar, salir o rearmar. Ningún temporizador pendiente sobrevive a drain, logout o
  rearmado.
- **El salto ≤ 7 → ≥ 10 es [F] como prueba de viaje rápido.** Hace falta una señal real de carga o viaje **del
  anfitrión**, el mismo campo y época, muestras válidas consecutivas, el invitado disponible y ningún prepare/drain.
  Reevaluar antes de actuar; el resto, con histéresis.
- **El WRN de drenaje** se quita con el estado real `g_p_draining` (por operación/generación), no con una espera fija
  de 1 s.
- **El hueco < 2 s es [NV]** hasta medirlo.
- `0x287DBB0`: con estado 2, campo válido y sin preparación, `param3`/`+0xB = 0` evalúa sin reencolar [V], pero
  «encolar siempre» no está probado inocuo.

**Por dónde empezar y qué medir primero:**
1. Instrumentar, en `probe`, por armado: el estado de la cadena, puntual frente a periódico, el resultado de
   `EnqueueTimer`, los eventos de carga/viaje del anfitrión y su época.
2. Medir el desglose de la salida con un lector homogéneo: **21,402565 s** recepción→recepción = 10,807702 s
   (baja→armado) + 10,052223 s (armado→2043) + 0,542640 s (2043→primera alta) (AUDITORÍA §4).
3. Prueba en un PC con el peer: el anfitrión viaja lejos del peer y vuelve, varias veces. Objetivo de
   `4-contencion` §4 #3: `[ARMED]` − baja de stages del anfitrión < 2 s y `[create] (user 2)` − `[ARMED]` < 0,5 s, sin
   cadenas nuevas.
4. Después, Shadow: el hueco en el invitado (`swap.py`), con los relevos de llegada y de salida emparejados por stage,
   nodo, familia y época.

---

## 7. Preguntas abiertas y riesgos

### Puntos de la auditoría sin cerrar ([NV] o pendientes)

| Punto | Estado |
|---|---|
| IA tipo 6 con el invitado como controlador (§2) | [NV]: lo decide el paso 2 del kit 36 |
| Contrato del tipo 7 (`SetOwnershipState 0x27A1430`, vf7) | no demostrado; B lo deja host-kept |
| Que los hijos nazcan en el mismo Troc que su raíz (A2 [P]) | hipótesis de la que depende el modo 1 de B; el paso 1 lo mide |
| Nombre dinámico del contexto de vf+0x170 | [NV]; no hace falta |
| Predicado nativo de locomoción lejos (navmesh/streaming) | [NV] |
| Descriptor→2005 1:1 de viajeros; si `28981B0` pasa las guardas | [NV] |
| «Dejar los tipo 7 con el anfitrión es seguro» (A3) | [NV], no aprobado |
| Hueco < 2 s del rearmado | [NV] |
| Configuración efectiva de Steam Cloud | [NV] |
| Destinos indirectos en código virtualizado (§7) | no se certifican |
| Crédito de misiones del anfitrión para cada cohorte nueva | sin medir; `quest_policy` sigue en `probe` en todos los inis de anfitrión (`c7ae132` no se ha probado en vivo) |

### Decisiones de diseño abiertas

- **Tipo 7 host-kept bajo una raíz del invitado (B).** ¿Basta la puerta de publicación o hay que dejar la unidad entera
  con el anfitrión? Que lo valore la segunda revisión.
- **Las salidas sin puerta** (`character_hold`, `child_settle`, D-53, `CtrlNotUser`, `partial`, `unlinked`, sesión
  ilegible) necesitan política con medida.
- **Retirada acotada por unidad** con la contención degradada (A, revisión 3).
- **Plazos** 8 s / 2 s / 8 s / 60 s / 60 s: hay que reelegirlos con las distribuciones del kit 35.
- **Fase C:** cómo reconocer el productor desde el mid, y qué hacer con las bandadas tipo 6 en restore 0.
- **Fase D:** qué señal de viaje del anfitrión se acepta; conservar stages al llegar el anfitrión (fuera por ahora).
- **Fase E:** si se mantiene la fecha antigua al restaurar (L12) o se pone la actual; la cola en memoria para el
  diario (L7).

### Riesgos de lo implementado

| Riesgo | Detalle (fuente) |
|---|---|
| **Cierres** | Las rutas sin puerta (A §6) pueden volver a llevar restore 0 al invitado (`0x88F832`/`0x1831598`). Si `prepare()` falla, el `NotifyAppear` sale sin comprobar (A rev. 11). Una SEH a través del detour se maneja con `__try/__finally` (A rev. 14), sin probar en vivo. Ningún gancho se ha instalado todavía en el juego con esta build |
| **NPC invisibles** | La puerta retiene hasta `publication_retain_max_ms` (60 s) y luego **suelta sin publicar** (A §1.4). Con la tabla llena, «retenido sin entrada» no tiene reanuncio. `Unknown` puede ser frecuente en gimmicks con algo en `+0x40` (A §6). Puede haber animales sin acción restaurable, piezas de familia y (C) bandadas en restore 0 retenidas por `WithheldUser2` |
| **NPC bloqueados o sin dueño** | Con la contención degradada se pausan las asignaciones y las unidades ya asignadas sin progreso sólo se cuentan (A §6). Posposición de hasta 60 s. Ventana residual antes de `Logout` (A §1.2). En B: el padre bloqueado por un hijo no retirado sigue asignado y puede disparar `LeftAfterDrain` (B §8); una unidad partida que no se puede retirar se abandona protegida sólo por la puerta; los marcos perdidos por SEH llevan a `marcos_agotados` (fail-closed, B §2.3) |
| **Monturas y mascotas** | `_Rider` está excluido: la unidad entera se queda con el anfitrión (B §2.1). Los hijos de usuario (D-53) quedan fuera de la puerta (A §6). La auditoría pide pruebas de monturas montadas/desmontadas, mascotas y compañeros (INFORME-A4 Shadow) y no hay ninguna |
| **Gimmicks** | Tipo 7 host-kept con raíz del invitado (B §2.7). El cliente sólo ejecuta un evento de gimmick si `[[jugador+0xA0]+0x60] == [gimmick+0x90]` (`docked_gimmicks_logic.h:7-11`, citado en `3-viajeros.md` §4.5): riesgo de familia o caravana «partida» funcionalmente. Los host-kept tras un drenaje los quita sólo el drenaje nativo (B §8) |
| **NPC de misión** | B y A sólo cambian `actor+0x90`, nunca `actor+0xA0`, `desc+0x40` ni el gestor de misiones (B §2.8). Pero el crédito al anfitrión de una acción del invitado sobre una cohorte nueva no se ha medido (AUDITORÍA §6 «Fronteras») |
| **Salida o viaje del invitado** | Desconexión sin siguiente Troc: sin cambios (B §8, AUDITORÍA §6.4). La limpieza de 64 por llamada no garantiza terminar. Viaje rápido y salida/vuelta: sólo en los planes de Shadow, sin probar |
| **Guardado (E)** | El diario es un `WriteFile` síncrono en el hilo del servidor: un disco colgado bloquea ese Save/Flush (L7). «Verified» no prueba el medio físico (L4). Se aparta como mucho 3 veces la caja y luego vuelve el bloqueo. Cloud ante una restauración (L12). Desinstalar el juego borraría las exportaciones si se usara la ruta por defecto en bin64 |
| **Trabajo paralelo** | `C:\Emulador CD-2inst` cambia `save_hooks.cpp` sin commit; posible conflicto con la fase E al unirlo |

---

## 8. Archivos que hay que leer para continuar (en orden)

1. `C:\Emulador CD-s36\CLAUDE.md`: reglas del proyecto. La versión de `opcion-d` (`3d1e304`) añade lo de las dos copias
   del juego en un PC.
2. Este documento (`docs/research/s25/s35-relevo.md`): estado y siguiente paso.
3. `docs/research/s25/s34-investigaciones/AUDITORIA.md` (§8 primero, luego §1-§7): qué exige la auditoría y en qué orden.
4. `docs/research/s25/s35-fase-a.md`: fase A completa, con criterios de aborto (§4) y plan de pruebas (§5).
5. `docs/research/s25/s35-fase-e-guardado.md`: fase E, restauración (§7) y plan en un PC (§9).
6. `docs/research/s25/s36-fase-b.md`: fase B, criterios (§5), plan (§6) y lo no hecho (§8).
7. `docs/research/s25/s34-investigaciones/datos/auditoria-codex-20261014/a2-familias/AUDITORIA-2-FAMILIAS.md`: los A2-n
   que cita la fase B, para la segunda revisión.
8. `docs/PRUEBA-DOS-PC.md`: cómo se monta la prueba anfitrión/Shadow por Steam.
9. `C:\Emulador CD-s35\dist\kit35\LEEME-S35.txt`: lo que hace Shadow en cada paso del kit 35.
10. `docs/research/s25/u0-resultados-s28.md` §8: guion del peer (`make_u1_idle_script.py`, `u1_idle.troc`).
11. `docs/research/s25/s34-investigaciones/3-viajeros.md` y `…/a3-viajeros/informe-a3.md`: diseño y cambios
    obligatorios de la fase C.
12. `docs/research/s25/s34-investigaciones/4-contencion-y-relevo.md` y `…/a4-contencion/INFORME-A4.md`: datos del relevo y
    cambios obligatorios de la fase D.
13. `src/mod/session2/npc_activation_logic.h`, `publication_gate_logic.h`, `npc_family_logic.h` y `host_save_logic.h`:
    la lógica pura de A, B y E con sus tests `tests/optd_*`.
14. `src/mod/session2/user2_stages.cpp` (`npc_control_impl`, `npc_activation_cleanup`, `ctl_cleanup`, `arm_locked`,
    `queue_chain_2043`): el pegamento donde irán C y D.
15. `docs/research/opcion-d-diario.md`: diario del proyecto (entrada «Kit 35»).
