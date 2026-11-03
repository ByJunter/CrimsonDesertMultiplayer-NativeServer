import sys, os
IDS = r'C:\Emulador CD\build\research\ids'
sys.path.insert(0, IDS)
cwd = os.getcwd(); os.chdir(IDS)
from ext import rows, get
os.chdir(cwd)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data2')
for p in sys.argv[1:]:
    for i in range(len(rows.get(p, []))):
        d = get(p, i)
        fn = os.path.join(OUT, p.replace('/', '__') + (f'.{i}' if i else ''))
        open(fn, 'wb').write(d); print(p, i, len(d))
