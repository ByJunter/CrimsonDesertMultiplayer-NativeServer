import json, sys
d = json.load(open(sys.argv[1]))
out = {}
for h, v in d.items():
    for base in v:
        if base.startswith('cd_seq_funcnpc_') or base.startswith('cd_seq_node_funcnpc'):
            out[h] = base
json.dump(out, open(sys.argv[2], 'w'), indent=1)
print(len(out))
