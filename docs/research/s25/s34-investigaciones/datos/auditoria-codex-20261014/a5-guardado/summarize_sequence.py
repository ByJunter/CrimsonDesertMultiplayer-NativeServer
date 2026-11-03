from pathlib import Path
import json,re
p=Path(r'%TEMP%\codex-s34-auditoria-20261014\a5-guardado')
d=json.loads((p/'logs.json').read_text(encoding='utf-8'))
for tag,r in d.items():
 print(tag)
 for x in r['redirects']:
  s=x['text'];m=re.search(r'(#[0-9]+) (\w+)',s)
  if m and m[2] in ['CreateFileW','MoveFileExW']:
   paths=re.findall(r'\\(slot\d+[^\x27]*)',s)
   print(x['line'],m[1],m[2],paths[::2],s.rsplit(' -> ',1)[1])
