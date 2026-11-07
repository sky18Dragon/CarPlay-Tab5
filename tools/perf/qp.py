import struct,re,sys,statistics
def unescape(d):
    o=bytearray();z=0
    for x in d:
        if z>=2 and x==3: z=0; continue
        o.append(x); z=z+1 if x==0 else 0
    return bytes(o)
class B:
    def __init__(s,d): s.d=d;s.p=0
    def u(s,n):
        v=0
        for _ in range(n):
            v=v*2+((s.d[s.p//8]>>(7-s.p%8))&1); s.p+=1
        return v
    def ue(s):
        z=0
        while s.u(1)==0: z+=1
        return (1<<z)-1+(s.u(z) if z else 0)
    def se(s):
        k=s.ue(); return (k+1)//2 if k&1 else -(k//2)
def sps(n):
    b=B(unescape(n[1:])); prof=b.u(8); b.u(8); lvl=b.u(8); b.ue()
    if prof in(100,110,122,244,44,83,86,118,128):
        cf=b.ue()
        if cf==3: b.u(1)
        b.ue(); b.ue(); b.u(1)
        if b.u(1):
            for i in range(8 if cf!=3 else 12):
                if b.u(1):
                    last=8;nxt=8
                    for j in range(16 if i<6 else 64):
                        if nxt: nxt=(last+b.se()+256)%256
                        last=nxt or last
    lf=b.ue()+4; poc=b.ue(); lp=0
    if poc==0: lp=b.ue()+4
    elif poc==1:
        b.u(1); b.se(); b.se(); c=b.ue()
        for _ in range(c): b.se()
    refs=b.ue(); b.u(1); w=b.ue()+1; h=b.ue()+1; fmo=b.u(1)
    return dict(prof=prof,lvl=lvl,lf=lf,poc=poc,lp=lp,refs=refs,mbw=w,mbh=h)
def pps(n):
    b=B(unescape(n[1:])); b.ue(); b.ue(); cabac=b.u(1); bot=b.u(1); b.ue(); r0=b.ue(); r1=b.ue(); b.u(1); b.u(2); qp=26+b.se(); b.se(); cq=b.se(); dbk=b.u(1); ci=b.u(1); rr=b.u(1)
    t8=0
    try: t8=b.u(1)
    except Exception: pass
    return dict(cabac=cabac,initqp=qp,chroma_qp_off=cq,deblock_ctrl=dbk,constrained_intra=ci,t8x8=t8,r0=r0+1)
def slice_h(n,S,P):
    t=n[0]&31; ref=(n[0]>>5)&3
    b=B(unescape(n[1:60])); b.ue(); st=b.ue()%5; b.ue(); b.u(S['lf'])
    if t==5: b.ue()
    if S['poc']==0: b.u(S['lp'])
    if st==0:
        if b.u(1): b.ue()
        if b.u(1):
            while True:
                op=b.ue()
                if op==3: break
                b.ue()
    if ref:
        if t==5: b.u(2)
        elif b.u(1):
            while True:
                op=b.ue()
                if op==0: break
                if op in(1,3): b.ue()
                if op==2: b.ue()
                if op==3: b.ue()
                if op in(4,6): b.ue()
    if P['cabac'] and st!=2: b.ue()
    return st,P['initqp']+b.se(),t,ref
for path in sys.argv[1:]:
    d=open(path,'rb').read(); p=0; S=P=None; rows=[]
    while p<len(d):
        s=struct.unpack('<I',d[p:p+4])[0]; u=d[p+4:p+4+s]; p+=4+s
        idx=[m.end() for m in re.finditer(b'\x00\x00\x01',u)]
        for i,a in enumerate(idx):
            e=idx[i+1]-3 if i+1<len(idx) else len(u)
            n=u[a:e]
            if not n: continue
            t=n[0]&31
            try:
                if t==7: S=sps(n)
                elif t==8: P=pps(n)
                elif t in(1,5) and S and P:
                    st,qp,tt,ref=slice_h(n,S,P); rows.append((tt,st,qp,len(n),ref))
            except Exception as ex: pass
    print(path.split('/')[-1],S,P)
    qs=[r[2] for r in rows if r[0]==1]; iq=[r[2] for r in rows if r[0]==5]
    big=[r for r in rows if r[0]==1 and r[3]>6000]; small=[r for r in rows if r[0]==1 and r[3]<=2500]
    f=lambda v:(min(v),statistics.median(v),max(v)) if v else None
    print(' P frames',len(qs),'qp min/med/max',f(qs),' IDR',len(iq),f(iq))
    print(' motion P(>6KB) n=%d qp %s avg bytes %d'%(len(big),f([r[2] for r in big]),sum(r[3] for r in big)//max(len(big),1)))
    print(' static P(<=2.5KB) n=%d qp %s'%(len(small),f([r[2] for r in small])))
    print(' nonref frames',sum(1 for r in rows if r[4]==0),'of',len(rows))
