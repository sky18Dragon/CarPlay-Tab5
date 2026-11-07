import sys,re,base64
import numpy as np
from PIL import Image
log=sys.argv[1]; idx=int(sys.argv[2]); out=sys.argv[3]
raw=open(log,'rb').read().decode(errors='replace')
parts=raw.split('FRAMEDUMP ')[1:]
print('dumps',len(parts))
part=parts[idx]
w,h=map(int,part.split('\n',1)[0].split()[:2]); size=w*h*3//2; buf=bytearray(size); got=0
for off,data in re.findall(r'F:(\d+):([A-Za-z0-9+/=]+)',part.split('FRAMEDONE')[0]):
    try: b=base64.b64decode(data)
    except Exception: continue
    buf[int(off):int(off)+len(b)]=b; got+=len(b)
print(w,h,'got',got,'of',size)
a=np.frombuffer(bytes(buf),dtype=np.uint8).reshape(h,w*3//2)
Y=np.zeros((h,w),np.float32); Y[:,0::2]=a[:,1::3]; Y[:,1::2]=a[:,2::3]
U=a[0::2,0::3].astype(np.float32); V=a[1::2,0::3].astype(np.float32)
np.savez('frame%d.npz'%idx,Y=Y,U=U,V=V)
def bil(p,H,W):
    return np.asarray(Image.fromarray(p).resize((W,H),Image.BILINEAR))
def kern_resize_v(p,H,kern,support):
    h0=p.shape[0]; s=h0/H
    out=np.zeros((H,p.shape[1]),np.float32)
    for y in range(H):
        c=(y+.5)*s-.5; i0=int(np.floor(c))-support+1
        idx=np.arange(i0,i0+2*support); w_=kern(idx-c); w_/=w_.sum()
        out[y]=(p[np.clip(idx,0,h0-1)]*w_[:,None]).sum(0)
    return out
def cubic(x,a=-.5):
    x=np.abs(x); return np.where(x<1,(a+2)*x**3-(a+3)*x**2+1,np.where(x<2,a*x**3-5*a*x**2+8*a*x-4*a,0))
def lanc(x,n=3):
    return np.where(np.abs(x)<n,np.sinc(x)*np.sinc(x/n),0)
def presharp(p,k,lim=48):
    o=p.copy(); d=np.clip(2*p[1:-1]-p[:-2]-p[2:],-lim,lim); o[1:-1]=np.clip(p[1:-1]+k*d/2,0,255); return o
def to_rgb(yp,up,vp):
    l=yp-16;u=up-128;v=vp-128
    r=1.164*l+1.596*v;g=1.164*l-.392*u-.813*v;b=1.164*l+2.017*u
    # RGB565 quantisation like the panel
    rgb=np.clip(np.stack([r,g,b],-1),0,255)
    rgb[...,0]=np.floor(rgb[...,0]/8)*8.2258; rgb[...,1]=np.floor(rgb[...,1]/4)*4.0476; rgb[...,2]=np.floor(rgb[...,2]/8)*8.2258
    return np.clip(rgb,0,255).astype(np.uint8)
Uc=bil(U,720,1280); Vc=bil(V,720,1280)
def show(yv, uc=Uc, vc=Vc):
    y=bil(np.clip(yv,0,255).astype(np.float32),720,1280)
    return to_rgb(y,uc,vc)
def hscale(yv): # vertical already done, PPA stretches width
    return bil(np.clip(yv,0,255).astype(np.float32),720,1280)
cands={}
cands['A now bilinear']=show(Y)
cands['B presharp .5']=show(presharp(Y,.5))
cands['C presharp .7']=show(presharp(Y,.7))
cr=kern_resize_v(Y,720,cubic,2)
cands['D catmull-rom']=to_rgb(hscale(cr),Uc,Vc)
la=kern_resize_v(Y,720,lanc,3)
cands['E lanczos3']=to_rgb(hscale(la),Uc,Vc)
cands['F presharp.5+catmull']=to_rgb(hscale(kern_resize_v(presharp(Y,.5,80),720,cubic,2)),Uc,Vc)
ucr=kern_resize_v(U,720,cubic,2); vcr=kern_resize_v(V,720,cubic,2)
uc2=bil(np.clip(ucr,0,255).astype(np.float32),720,1280); vc2=bil(np.clip(vcr,0,255).astype(np.float32),720,1280)
cands['G F+chroma catmull']=to_rgb(hscale(kern_resize_v(presharp(Y,.5,80),720,cubic,2)),uc2,vc2)
import pickle
pickle.dump(cands,open('cands%d.pkl'%idx,'wb'))
full=Image.fromarray(cands['A now bilinear']); full.save(out.replace('.png','-full.png'))
for k,v in cands.items(): print(k, 'edge energy', float(np.abs(np.diff(v[...,1].astype(np.float32),axis=0)).mean()))
