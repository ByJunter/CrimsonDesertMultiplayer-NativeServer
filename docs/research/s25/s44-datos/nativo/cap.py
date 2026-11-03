import struct
REC = struct.Struct("<QBBHHHII")
def read(path, ops=(2661,)):
    data = open(path,'rb').read()
    if data[:4] != b"CDPK": return
    pos = 12
    while pos + REC.size <= len(data):
        t, d, _, size, copied, _, sess, thread = REC.unpack_from(data, pos)
        pos += REC.size
        pkt = data[pos:pos+copied]; pos += copied
        if len(pkt) < 5: continue
        op = struct.unpack_from("<H", pkt, 0)[0]
        if op in ops: yield t, d, op, pkt[5:]
