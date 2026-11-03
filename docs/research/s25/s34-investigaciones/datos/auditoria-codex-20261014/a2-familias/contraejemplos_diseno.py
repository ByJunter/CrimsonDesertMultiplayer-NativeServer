# Modelos mínimos de contraejemplo; no ejecutan el mod ni el juego.
# Fuente del criterio: npc_activation_logic.h:393-414, commit d835806.
import pathlib,json
actors=[dict(key=1,birth=20,parent=0,epoch=1,removed=False),dict(key=2,birth=11,parent=1,epoch=1,removed=False)]
selected=[actors[0]];grew=True
while grew:
 grew=False
 for a in actors:
  keys={x['key'] for x in selected}
  if a['removed'] or not a['parent'] or a['epoch']!=selected[0]['epoch'] or a['key'] in keys or a['parent'] not in keys:continue
  selected.append(a);grew=True
assert [x['birth'] for x in selected]==[20,11]
# El hijo 11 pertenecía a la encarnación 10 de la clave 1; el criterio sólo guarda parent=1.
# Fuente del segundo criterio: 2-familias-npc.md:194-213,234-238.
n=63;capacity=64;subtree=2;root_allowed=n<capacity
root_added=n+1;child_has_space=root_added<capacity
assert root_allowed and not child_has_space
# Un único marco mutable pierde A tras B anidado; cambiar de nuevo a A lo vacía.
frame={'troc':100,'records':['raiz-A']};frame={'troc':101,'records':['raiz-B']}
if frame['troc']!=100:frame={'troc':100,'records':[]}
assert 'raiz-A' not in frame['records']
result={'familia_clave_reutilizada':{'actores':actors,'seleccion':selected,'falta':'parent_birth o token de raíz'},'capacidad_no_reservada':{'ocupados':n,'subarbol_nodos':subtree,'raiz_permitida':root_allowed,'hijo_cabe':child_has_space},'troc_anidado':{'marco_al_volver':frame,'falta':'pila de marcos o ámbito por lote con cierre seguro'}}
pathlib.Path(__file__).with_suffix('.json').write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps(result,indent=2))
