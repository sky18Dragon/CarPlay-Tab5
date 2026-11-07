import re,sys
raw=open(sys.argv[1],errors='replace').read().splitlines()
start=end=None; rows=[]
pat=re.compile(r'I \((\d+)\) VIDEO: STATS sub=(\d+) idr=(\d+) busy=\d+ rej=(\d+) dec=(\d+) pend=\d+ err=(\d+) ui_fail=\d+ starts=\d+ decoders=(\d+)(?: skip=\d+)?(?: pack_total=(?:\d+)ms)? decode_max=(\d+)ms decode_total=(\d+)ms ui_max=(\d+)ms ui_total=(\d+)ms size=(\S+)')
for l in raw:
    m=re.search(r'\((\d+)\) VIDEO: MOTION (start|end)',l)
    if m:
        if m[2]=='start': start=int(m[1])
        else: end=int(m[1])
    m=pat.search(l)
    if m: rows.append(list(map(int,m.groups()[:11]))+[m[12]])
if not start: print('no motion'); sys.exit()
end=end or rows[-1][0]
w=[r for r in rows if start+3000<=r[0]<=end]
a,b=w[0],w[-1]; t=(b[0]-a[0])/1000
d=lambda i:b[i]-a[i]
print(f"{b[11]} window={t:.0f}s in={d(1)/t:.1f}fps shown={d(4)/t:.1f}fps idr={d(2)} rej={d(3)}/{d(1)} decoders+={d(6)} decode_avg={d(8)/max(d(4),1):.0f}ms decode_max={b[7]} ui_avg={d(10)/max(d(4),1):.0f}ms err={d(5)}")
