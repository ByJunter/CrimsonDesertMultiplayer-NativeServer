from pathlib import Path
import json,struct,hashlib
OUT=Path(r'%TEMP%\codex-s34-auditoria-20261014\a5-guardado')
def header_ok(blob):
 if len(blob)<128:return False
 magic,version,headersize=struct.unpack_from('<IHH',blob);flags=struct.unpack_from('<I',blob,12)[0];raw,data=struct.unpack_from('<II',blob,18)
 return magic==0x45564153 and 0<version<=2 and headersize==128 and flags&~2==0 and raw>0 and 0<data<=raw and raw<=64*1024*1024 and raw<=255*data and len(blob)<=16*1024*1024 and len(blob)==128+data
def blob(payload):
 h=bytearray(128);struct.pack_into('<IHH',h,0,0x45564153,2,128);struct.pack_into('<II',h,18,len(payload),len(payload));return bytes(h)+payload
# A model of the PROPOSAL, not game execution or a claim these byte strings decrypt.
a=blob(b'HOST-LOBBY-OLD');b=blob(b'GUEST-STATE-XX')
result={'header_does_not_certify_payload':{'a_ok':header_ok(a),'b_ok':header_ok(b),'different_sha256':hashlib.sha256(a).digest()!=hashlib.sha256(b).digest(),'meaning':'Both arbitrary payloads pass precisely the structural header predicate; no decoding, actor, quest, account or cross-file check exists in it.'}}
# Existence/open/rename observed by the proposal with a mixed pair after a concurrent writer.
events=['host scope begins generation 10','open lobby for writing: success','open save for writing: success','rename new to slot: success','host original returns code 0','snapshot copies lobby generation 10','another writer replaces slot generation 11','snapshot copies save generation 11']
result['mixed_snapshot']={'events':events,'proposed_open_and_rename_conditions':True,'coherent_pair':False,'needed':'Exclusive slot generation ownership from writing through immutable snapshot, or equivalent handles plus generation validation.'}
# Deterministic native startup policy observed in 0x2358EF0.
states=[{'stage':'new written','slot':'old','new':'new','backup':None},{'stage':'old renamed','slot':None,'new':'new','backup':'old'},{'stage':'new installed','slot':'new','new':None,'backup':'old'},{'stage':'backup archived','slot':'new','new':None,'backup':None}]
for s in states:
 s['native_recovery_slot']=s['backup'] if s['backup'] else s['slot'];s['native_recovery_new']=None
result['native_recovery_model']=states
(OUT/'counterexamples.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))
