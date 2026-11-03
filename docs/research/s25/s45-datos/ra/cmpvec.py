import pe, re
src=open(r'C:\Emulador CD-escenas\tests\optd_seq_present_tests.cpp',encoding='utf-8').read()
for name,va in (('kExeClient',0x558C080),('kExeCommon',0x590DF78),('kExeServer',0x5B1FE10)):
    m=re.search(name+r' = \{(.*?)\};',src,re.S)
    vals=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]+',m.group(1))]
    exe=[pe.u64(va+8*i)-pe.BASE for i in range(105)]
    print(name,len(vals),vals==exe)
