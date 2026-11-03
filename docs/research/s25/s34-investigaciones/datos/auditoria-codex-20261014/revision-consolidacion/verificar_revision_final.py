from pathlib import Path
import struct,json,hashlib
root=Path(r'%TEMP%\codex-s34-auditoria-20261014\revision-consolidacion')
p=Path(r'C:\Emulador CD\backups\p8-sesion34\shadow-p1b-p2b\cdmp_packets_20261014-175057.bin')
with p.open('rb') as f:
 f.seek(146811672);b=f.read(64)
out={'capture':str(p),'packet_cdpk_offset':146811672,'timestamp_us':struct.unpack_from('<Q',b,0)[0],'actor_offset':146811703,'actor_type':b[31],'actor_key':hex(struct.unpack_from('<I',b,32)[0]),'raw_64_bytes':b.hex(' ')}
base=Path(r'%TEMP%\codex-s34-auditoria-20261014')
paths=['consolidacion/AUDITORIA.md','consolidacion/seccion-9.md','a4-contencion/INFORME-A4.md','a5-guardado/AUDITORIA-5.md']
out['reviewed_hashes']={s:hashlib.sha256((base/s).read_bytes()).hexdigest()for s in paths}
(root/'verificacion-final.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in out.items()if k!='raw_64_bytes'},indent=2))
