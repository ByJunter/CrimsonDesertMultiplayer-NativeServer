import sys, re
d = open(sys.argv[1], 'rb').read()
n = int(sys.argv[2]) if len(sys.argv) > 2 else 4
for m in re.finditer(rb'[\x20-\x7e]{%d,}' % n, d):
    print(f"{m.start():6x} {m.group().decode()}")
