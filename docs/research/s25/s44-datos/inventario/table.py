import json, csv, sys, re
from collections import Counter
rows = json.load(open('out/by_resource.json'))
p287 = {}
for line in open('out/path287.txt', encoding='utf-8'):
    m = re.match(r'(\S+)\s+(\{.*\})', line)
    if m:
        p287[m.group(1)] = m.group(2)
hs = {r['resource']: r for r in csv.DictReader(open('out/host_side.csv', encoding='utf-8'))}
fam_order = ['boss_rematch', 'memory', 'use', 'faction', 'reward', 'minigame', 'funcnpc (tienda/función)', 'quest', 'onetimequest',
             'node', 'contents', 'contents_weather', 'organization', 'entrance', 'spawn', 'cd', '?']
out = []
for r in rows:
    if r['owner_cls'] != 'H':
        continue
    name = r['name'].replace(' (log)', '')
    ctrl = sorted({x.split(' x')[0] for x in r['xveto'].split(' | ') if x and not any(k in x for k in ('UIStageGuide', 'SetQuestDialogAICategory', 'SetInteraction'))})
    h = hs.get(r['resource'], {})
    out.append({
        'familia': r['family'], 'recurso': r['resource'], 'nombre': name, 'kind': r['kinds'], 'b17/b18': r['b17_b18'],
        'stage@19': r['stage_form'], 'k@55': r['k55'], 'ultimo@90': r['last_byte'], 'n_enlaces': r['nb'],
        'rol_anfitrion': r['host_roles'], 'rol_invitado': r['guest_roles'], 'npc_enlazados': r['npc_binds'],
        'firma_enlaces(rol:actor/tipo)': r['signatures'], 'estados_2981': r['s2981'], 'n_2524': r['n2524'],
        'camino_287C200(anfitrion)': p287.get(name, ''),
        'ocurrencias_invitado': r['n_occ'], 'sesiones_invitado': r['n_runs'], 'veredictos': r['verdicts'], 'creacion': r['results'],
        'rechazos_actor_ausente': r['missing'], 'vetos_X_control': ' '.join(ctrl), 'vetos_X_todos': r['xveto'],
        'viva_en_marca': r['live_at_marks'], 'host_preparadas': h.get('host_prepared', ''), 'host_enviadas_u2': h.get('sent_to_user2', ''),
        'sesiones': r['runs'], 'fuente_primera': r['src_first'],
    })
out.sort(key=lambda o: (fam_order.index(o['familia']) if o['familia'] in fam_order else 99, o['nombre']))
with open('tabla_escenas_anfitrion.csv', 'w', newline='', encoding='utf-8-sig') as f:
    w = csv.DictWriter(f, fieldnames=list(out[0].keys()))
    w.writeheader(); w.writerows(out)
json.dump(out, open('tabla_escenas_anfitrion.json', 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
for o in out:
    sig = o['firma_enlaces(rol:actor/tipo)'].split(' || ')[0]
    sig = re.sub(r' \(x\d+\)', '', sig)
    if len(sig) > 40: sig = sig[:37] + '...'
    print('| %s | `%s` | %s | %s | %s | %s | %s | %s | %s | %s | %d/%d | %s | %s | %s |' % (
        o['familia'].split(' ')[0], (o['recurso'] or '0x?')[2:], o['nombre'], o['b17/b18'] or '-', o['n_enlaces'],
        o['rol_anfitrion'].replace('0x2000000', '…') if o['rol_anfitrion'] != '-' else '-', sig or '(ninguno)', o['estados_2981'],
        o['camino_287C200(anfitrion)'].replace("'", '').replace('{', '').replace('}', '') or '-', o['vetos_X_control'] or '-',
        o['ocurrencias_invitado'], o['sesiones_invitado'], o['veredictos'].replace(':', ' ') or '-', o['creacion'].replace(':', ' ') or '-',
        ('sí (' + str(len(o['viva_en_marca'].split())) + ')') if o['viva_en_marca'] else '-'))
