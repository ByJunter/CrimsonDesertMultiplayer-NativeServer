import pathlib,json,re,statistics
p=pathlib.Path(__file__).parent
for l in 'ABC':
 x=json.loads((p/f'{l}-censo.json').read_text());print(l)
 for fam in x['families']:
  if any(c['type']==6 for c in fam['members']) or fam['missing']:
   print(fam['root']['key'],fam['root']['offset'],fam['root']['record_offset'],fam['root']['t'],fam['first_req'],fam['removals'])
   for c in fam['members']:print(c['key'],c['offset'],c['record_offset'],c['t'],c['parent'],c['controller'])
 print('timeout lines', len(x['timeout_release_lines']))
 for y in x['timeout_release_lines']: print(y)
 print('observed',len(x['observed_birth_pairs']),min(v['delta_us'] for v in x['observed_birth_pairs']),max(v['delta_us'] for v in x['observed_birth_pairs']))
