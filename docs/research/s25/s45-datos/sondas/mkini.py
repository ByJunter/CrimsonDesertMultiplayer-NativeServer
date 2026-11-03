import sys
src, dst = sys.argv[1], sys.argv[2]
b = open(src, 'rb').read()
nl = b'\r\n' if b'\r\n' in b else b'\n'
lines = b.split(nl)
head = [
 "; S45 PROBE INVITADO (docs/research/s25/s45-escenas-ajenas.md). Base exacta: cdmp.2inst-s44-activo-guest.ini.",
 "; Unico cambio respecto a la base: guest_seq_foreign_present_probe=1 (junto a guest_seq_foreign_probe). SOLO LECTURA:",
 ";   S-A en P1 (procesador y vtable de cada etapa ajena, id de linea de tiempo stage+0x298), S-A2 (funciones de etapa",
 ";   de X antes de su P1), S-B (inicio de CameraSelector / PlayerCameraPreset / LetterBox / PostProcess / AIDialog:",
 ";   a que etapa pertenece su linea de tiempo), S-C (StartLoadingSequencer), S-D (camara y bloqueo de entrada del",
 ";   cliente cada fotograma, solo cambios). Lineas 'GuestSeqPresent [...]' y 'GuestSeqPresent summary' cada 60 s.",
 "; El anfitrion usa cdmp.2inst-s43-activo-host.ini sin cambios. APAGAR (criterio de aborto: cierre, tirones):",
 ";   guest_seq_foreign_present_probe=0 (exactamente S44). Copia de las partidas antes de cada prueba. No desplegado.",
 "; ---- cabecera de la base, conservada:",
]
out = [h.encode('utf-8') for h in head]
done = False
for ln in lines:
    out.append(ln)
    if ln.strip() == b'guest_seq_foreign_probe=1' and not done:
        out.append(b"; S45 (docs/research/s25/s45-escenas-ajenas.md). SOLO INVITADO, SOLO LECTURA (nada escribe en el juego).")
        out.append(b";   0 = sin ganchos nuevos ni coste. Necesita guest_seq_route=1 (sin escenas en X todo queda en silencio);")
        out.append(b";   S-D necesita guest_seq_play (su gancho 0x8B6144); S-A2 necesita guest_x_ui_veto o guest_x_camera_veto.")
        out.append(b"guest_seq_foreign_present_probe=1")
        done = True
assert done
open(dst, 'wb').write(nl.join(out))
print('ok', len(out), nl)
