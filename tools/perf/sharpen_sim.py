import sys,re,base64
from PIL import Image
import array
log,out=sys.argv[1:3]
raw=open(log,'rb').read().decode(errors='replace')
part=raw.split('FRAMEDUMP ')[1]
w,h=map(int,part.split('\n',1)[0].split()); size=w*h*3//2; buf=bytearray(size)
for off,data in re.findall(r'F:(\d+):([A-Za-z0-9+/=]+)',part.split('FRAMEDONE')[0]):
    try: b=base64.b64decode(data)
    except Exception: continue
    buf[int(off):int(off)+len(b)]=b
stride=w*3//2
Y=bytearray(w*h); U=bytearray(w*h//4+w); V=bytearray(w*h//4+w)
for y in range(h):
    row=buf[y*stride:(y+1)*stride]
    for x in range(0,w,2):
        i=x*3//2
        Y[y*w+x]=row[i+1]; Y[y*w+x+1]=row[i+2]
        (V if y&1 else U)[(y//2)*(w//2)+x//2]=row[i]
def clip(v): return 0 if v<0 else 255 if v>255 else int(v)
def sharpen(k):
    if k==0: return bytes(Y)
    o=bytearray(Y)
    for y in range(1,h-1):
        for x in range(w):
            c=Y[y*w+x]; d=2*c-Y[(y-1)*w+x]-Y[(y+1)*w+x]
            d=max(-48,min(48,d))
            o[y*w+x]=clip(c+k*d/2)
    return bytes(o)
def compose(yb):
    yi=Image.frombytes('L',(w,h),yb).resize((1280,720),Image.BILINEAR)
    ui=Image.frombytes('L',(w//2,h//2),bytes(U[:w*h//4])).resize((1280,720),Image.BILINEAR)
    vi=Image.frombytes('L',(w//2,h//2),bytes(V[:w*h//4])).resize((1280,720),Image.BILINEAR)
    ya,ua,va=yi.tobytes(),ui.tobytes(),vi.tobytes()
    px=bytearray(1280*720*3)
    for i in range(1280*720):
        l=ya[i]-16;u=ua[i]-128;v=va[i]-128
        px[i*3]=clip((298*l+409*v+128)/256);px[i*3+1]=clip((298*l-100*u-208*v+128)/256);px[i*3+2]=clip((298*l+516*u+128)/256)
    return Image.frombytes('RGB',(1280,720),bytes(px))
crops=[(700,60,1280,300)]
canvas=Image.new('RGB',(580*2+10,250*3),(255,255,255))
for n,k in enumerate((0,0.7,1.3)):
    im=compose(sharpen(k))
    if k==0: im.save(out.replace('.png','-full.png'))
    canvas.paste(im.crop((700,60,1280,300)),(0,n*250))
    canvas.paste(im.crop((90,430,670,670)),(590,n*250))
canvas.save(out); print(w,h)
