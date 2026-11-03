import struct,csv,sys,collections
names={}
for r in csv.DictReader(open(r'C:\Emulador CD-opcion-d\docs\research\data\troc_messages_2976.csv',encoding='utf8')):
    names[int(r['id'])]=r['name']
def read(path):
    with open(path,'rb') as f:
        b=f.read(12)
        assert b[:4]==b'CDPK',b
        n=0;first=None
        while True:
            h=f.read(24)
            if len(h)<24: break
            t,d,pad,size,copied,pad2,sess,tid=struct.unpack('<QBBHHHII',h)
            data=f.read(copied)
            if len(data)<copied: break
            n+=1
            if first is None: first=t
            op=struct.unpack_from('<H',data)[0] if len(data)>=2 else -1
            yield n,(t-first)/1e6,d,op,size,data,sess,tid
