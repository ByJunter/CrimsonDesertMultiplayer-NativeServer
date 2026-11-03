import pathlib,json
O=pathlib.Path(__file__).parent
for l in 'ABC':
 x=json.loads((O/f'{l}-censo.json').read_text()); print(l,'birthpairs',len(x['observed_birth_pairs']),'delta us', min(z['delta_us'] for z in x['observed_birth_pairs']),max(z['delta_us'] for z in x['observed_birth_pairs']))
 for f in x['families']:
  if any(c['type']==6 for c in f['members']):
   t=max(c['t'] for c in f['members']);q=f['first_2223'];print(f['root']['key'], 'first2223',q,'delta ms',None if not q else (q['t']-t)/1000)
