exec(compile(open(r'%TEMP%\codex-s34-auditoria-20261014\a5-guardado\audit_logs.py',encoding='utf-8-sig').read(),'audit_logs.py','exec'))
for tag,path in logs.items():
 stamps=[]; summaries=[]; n=0
 with path.open(encoding='utf-8-sig',errors='replace') as f:
  for n,s in enumerate(f,1):
   m=re.match(r'\[([0-9:.]+)\]',s)
   if m:stamps.append((n,m[1]))
   m=re.search(r'guest save 1: .*? \| guest save link:',s)
   if m:summaries.append((n,m[0][:-19]))
 print(tag,'lines',n,'timefirstlast',stamps[0],stamps[-1],'lastsummary',summaries[-1] if summaries else None)
 results[tag]['coverage']={'lines':n,'first':stamps[0],'last':stamps[-1],'last_summary':summaries[-1] if summaries else None}
 for row in (OUT/f'log-{tag}-extract.txt').read_text(encoding='utf-8').splitlines():
  if 'FlushPendingSaveData 0x' in row or 'EXPORTED gen' in row:print(tag,row)
(OUT/'logs.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
