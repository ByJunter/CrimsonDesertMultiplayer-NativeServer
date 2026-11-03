import os, sys, re, collections
d = sys.argv[1]
rows = []
for fn in sorted(os.listdir(d)):
    if not fn.endswith('.paseq'): continue
    b = open(os.path.join(d, fn), 'rb').read()
    def has(s): return s.encode() in b
    name = fn.replace('sequencer__', '').replace('.paseq', '')
    m = {
      'camSel': b.count(b'CameraSelectorEventData') > 0,
      'camComp': b.count(b'CameraComponent') > 0,
      'cutScene': bool(re.search(rb'set_cutScene\(True\)', b)),
      'cutSceneOther': bool(re.search(rb'set_cutScene\((?!True)', b)),
      'excl': has('set_exclusive(True)'),
      'fade': has('_screenFadeInOut'),
      'letter': has('LetterBoxControl'),
      'ctlIn': has('SequencerControl_Input'),
      'camPreset': has('SequencerPlayerCameraPreset_ITimeLineEventData'),
    }
    rows.append((name, m))
cols = ['camSel','camComp','cutScene','cutSceneOther','excl','fade','letter','ctlIn','camPreset']
print('name\t' + '\t'.join(cols))
for n, m in rows:
    print(n + '\t' + '\t'.join('x' if m[c] else '.' for c in cols))
c = collections.Counter()
for n, m in rows:
    c[(m['camSel'] or m['camComp'], m['cutScene'])] += 1
print(c)
