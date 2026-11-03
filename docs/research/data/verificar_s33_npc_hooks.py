import sys,struct,re,json,hashlib
from pathlib import Path
sys.path.insert(0,r"C:\Emulador CD\build\research")
from cdpe import PE
p=PE(); output=[]
pe=struct.unpack_from('<I',p.data,0x3c)[0]; opt=pe+24; va,size=struct.unpack_from('<II',p.data,opt+112+3*8); pd=p.read(va,size); funcs=[struct.unpack_from('<III',pd,i) for i in range(0,len(pd)-11,12)]
rs=p.sec('.rsrc')[0]; raw=p.read(rs[1],rs[4])
for a,n,patch in [(0x2A78CA0,40,5),(0x281CEE0,32,5),(0x2A79060,16,7)]:
 b=p.read(a,n); bounds=[(x,y) for x,y,z in funcs if x<=a<y]; instr=[]; total=0
 for i in p.md.disasm(p.read(a,32),a):
  instr.append({'rva':hex(i.address),'bytes':i.bytes.hex(' '),'asm':i.mnemonic+' '+i.op_str});total+=i.size
  if total>=patch:break
 hits=[]
 for m in re.finditer(rb'[\xe8\xe9]|\x0f[\x80-\x8f]',raw):
  o=m.start(); l=len(m.group())
  if o+l+4>len(raw):continue
  dst=rs[1]+o+l+4+struct.unpack_from('<i',raw,o+l)[0]
  if a<dst<a+patch:hits.append(hex(rs[1]+o))
 # Saltos cortos próximos al parche: se registran conservadoramente, incluso bytes no ejecutables.
 for o in range(max(0,a-rs[1]-128),a-rs[1]+128):
  if raw[o] in range(0x70,0x80) or raw[o]==0xeb:
   dst=rs[1]+o+2+struct.unpack_from('<b',raw,o+1)[0]
   if a<dst<a+patch:hits.append(hex(rs[1]+o))
 output.append({'rva':hex(a),'seccion':p.section_of(a)[0],'firma':b.hex(' ').upper(),'ocurrencias_PE':p.data.count(b),'pdata':[[hex(x),hex(y)] for x,y in bounds],'parche_bytes':patch,'instrucciones':instr,'limite_completo':total==patch,'posibles_saltos_al_interior':hits})
output[0]['analisis_candidatos']={'0x2a78c35':'Byte 74 dentro del desplazamiento del E9 que empieza en 0x2A78C34; coincidencia binaria, no salto corto decodificado.'}
output[2]['analisis_candidatos']={'0x38f256f':'Patrón 0F8C fuera de funciones del directorio de excepciones, en bytes ofuscados; destino aparente 0x2A79064 no es frontera de instrucción. H1b ya existía y no se añade un parche superpuesto. No se atribuye a una ruta ejecutable verificada.'}
assert struct.unpack_from('<I',p.data,pe+8)[0] == 0x6AB28F00
result={'build':'1.0.0.2976','PE_timestamp':'0x6AB28F00','exe_sha256':hashlib.sha256(p.data).hexdigest(),'puntos':output,'contrato_constructor':'0x2818AE0,0x2819E00 (4 sitios),0x281D970,0x281DBD0,0x2820D80: false omite descriptor; salida inicial null','nota':'Lectura del archivo PE; no ejecución ni carga del juego.'}
Path(sys.argv[1]).write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf-8')
print(json.dumps(result,indent=2,ensure_ascii=False))
