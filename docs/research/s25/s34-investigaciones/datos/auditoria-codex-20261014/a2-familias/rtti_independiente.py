import pathlib,runpy,contextlib,io,json
O=pathlib.Path(__file__).parent
with contextlib.redirect_stdout(io.StringIO()):n=runpy.run_path(str(O/'estatico_independiente.py'))
b=n['b'];off=n['off'];u32=n['u32'];u64=n['u64'];base=n['base'];res={}
for rva in [0x5B04C68,0x5B04CB0,0x5B04E80,0x5B04E20,0x5B04DA0,0x5B04D60,0x5B04DC0,0x5B04D80,0x5B04DE0,0x5B04E60,0x5B04E00]:
 col=u64(off(rva-8))-base;td=u32(off(col)+12);q=off(td)+16;e=b.find(b'\0',q);res[hex(rva)]=dict(col=hex(col),td=hex(td),name_rva=hex(td+16),name=b[q:e].decode('ascii','replace'),slots=[hex(u64(off(rva)+i*8)-base) for i in range(8 if rva==0x5b04c68 else 3)])
(O/'rtti.json').write_text(json.dumps(res,indent=2),encoding='utf8');print(json.dumps(res,indent=2))
