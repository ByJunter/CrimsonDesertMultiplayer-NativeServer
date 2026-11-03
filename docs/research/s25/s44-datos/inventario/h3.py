def rot(x,k): return ((x<<k)|(x>>(32-k)))&0xffffffff
def hashlittle(k, initval=0xC5EDE):
    if isinstance(k,str): k=k.encode()
    M=0xffffffff
    length=len(k); a=b=c=(0xDEADBEEF+length+initval)&M; i=0
    while length>12:
        a=(a+int.from_bytes(k[i:i+4],'little'))&M; b=(b+int.from_bytes(k[i+4:i+8],'little'))&M; c=(c+int.from_bytes(k[i+8:i+12],'little'))&M
        a=(a-c)&M; a^=rot(c,4); c=(c+b)&M
        b=(b-a)&M; b^=rot(a,6); a=(a+c)&M
        c=(c-b)&M; c^=rot(b,8); b=(b+a)&M
        a=(a-c)&M; a^=rot(c,16); c=(c+b)&M
        b=(b-a)&M; b^=rot(a,19); a=(a+c)&M
        c=(c-b)&M; c^=rot(b,4); b=(b+a)&M
        length-=12; i+=12
    if length==0: return c
    t=k[i:i+length]+b'\0'*(12-length)
    a=(a+int.from_bytes(t[0:4],'little'))&M; b=(b+int.from_bytes(t[4:8],'little'))&M; c=(c+int.from_bytes(t[8:12],'little'))&M
    c^=b; c=(c-rot(b,14))&M
    a^=c; a=(a-rot(c,11))&M
    b^=a; b=(b-rot(a,25))&M
    c^=b; c=(c-rot(b,16))&M
    a^=c; a=(a-rot(c,4))&M
    b^=a; b=(b-rot(a,14))&M
    c^=b; c=(c-rot(b,24))&M
    return c
if __name__=='__main__':
    import sys
    for n in sys.argv[1:]: print(n, hex(hashlittle(n)))
