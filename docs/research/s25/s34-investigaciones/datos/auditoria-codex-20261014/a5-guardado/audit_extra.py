from pathlib import Path
import json,re,struct
exec(compile(open(r'%TEMP%\codex-s34-auditoria-20261014\a5-guardado\audit_pe.py',encoding='utf-8-sig').read(),'audit_pe.py','exec'))
rows=[]
for r,n in [(0x1791b94,17),(0xb742f0,25),(0x5983c04,8)]:
 data=b[off(r):off(r)+n]
 rows.append({'rva':hex(r),'offset':hex(off(r)),'bytes':data.hex(' '),'instructions':[f'{i.address:#x} {i.mnemonic} {i.op_str}' for i in cs.disasm(data,r)]})
(OUT/'extra-bytes.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
print(rows)
