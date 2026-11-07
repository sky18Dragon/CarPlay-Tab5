import sys,base64,re
from PIL import Image
raw=open(sys.argv[1],'rb').read().decode(errors='replace')
out=sys.argv[2]; parts=raw.split('FRAMEDUMP ')[1:]
def clip(v): return 0 if v<0 else 255 if v>255 else int(v)
tiles=[]
for k,part in enumerate(parts):
    w,h=map(int,part.split('\n',1)[0].split()); size=w*h*3//2; buf=bytearray(size)
    for off,data in re.findall(r'F:(\d+):([A-Za-z0-9+/=]+)',part.split('FRAMEDONE')[0]):
        try: b=base64.b64decode(data)
        except Exception: continue
        buf[int(off):int(off)+len(b)]=b
    stride=w*3//2; px=bytearray(w*h*3)
    for y in range(0,h,2):
        a=buf[y*stride:(y+1)*stride]; c=buf[(y+1)*stride:(y+2)*stride]
        for x in range(0,w,2):
            i=x*3//2; u=a[i]-128; v=c[i]-128
            for dy,row in ((0,a),(1,c)):
                for dx in (0,1):
                    l=row[i+1+dx]-16; o=((y+dy)*w+x+dx)*3
                    px[o]=clip((298*l+409*v+128)/256); px[o+1]=clip((298*l-100*u-208*v+128)/256); px[o+2]=clip((298*l+516*u+128)/256)
    tiles.append(Image.frombytes('RGB',(w,h),bytes(px)).resize((1280,720),Image.NEAREST))
if not tiles: print('NO DUMP'); sys.exit(1)
o=Image.new('RGB',(1280,730*len(tiles)),(255,255,255))
for i,t in enumerate(tiles): o.paste(t,(0,i*730))
o.save(out); print('dumps',len(tiles))
