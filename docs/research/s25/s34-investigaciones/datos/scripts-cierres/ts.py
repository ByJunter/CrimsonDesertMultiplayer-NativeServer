import struct,sys,glob
for p in sys.argv[1:]:
    try:
        d=open(p,'rb').read(4096); pe=struct.unpack_from('<I',d,0x3C)[0]; print(hex(struct.unpack_from('<I',d,pe+8)[0]),p)
    except Exception as e: print('ERR',p,e)
