import numpy as np,sys,pickle
from PIL import Image,ImageDraw,ImageFont
def load(n):
    d=np.load(n+'.npz'); return d['Y'],d['U'],d['V']
PIL_R={'box':Image.BOX,'bil':Image.BILINEAR,'lan':Image.LANCZOS,'bic':Image.BICUBIC}
def rs(p,W,H,m):
    return np.asarray(Image.fromarray(p.astype(np.float32)).resize((W,H),PIL_R[m]))
def to_rgb(y,u,v):
    l=y-16;u=u-128;v=v-128
    c=np.clip(np.stack([1.164*l+1.596*v,1.164*l-.392*u-.813*v,1.164*l+2.017*u],-1),0,255)
    q=np.array([8,4,8],dtype=np.float32); c=np.floor(c/q)*q*np.array([255/248,255/252,255/248],dtype=np.float32)
    return np.clip(c,0,255).astype(np.uint8)
def sharp_v(p,k,lim=64):
    o=p.copy(); d=np.clip(2*p[1:-1]-p[:-2]-p[2:],-lim,lim); o[1:-1]=np.clip(p[1:-1]+k*d/2,0,255); return o
def sharp_h(p,k,lim=64):
    o=p.copy(); d=np.clip(2*p[:,1:-1]-p[:,:-2]-p[:,2:],-lim,lim); o[:,1:-1]=np.clip(p[:,1:-1]+k*d/2,0,255); return o
def cubic(x,a=-.5):
    x=np.abs(x); return np.where(x<1,(a+2)*x**3-(a+3)*x**2+1,np.where(x<2,a*x**3-5*a*x**2+8*a*x-4*a,0))
def cr_v(p,H):
    h0=p.shape[0]; s=h0/H; o=np.zeros((H,p.shape[1]),np.float32)
    for y in range(H):
        c=(y+.5)*s-.5; i0=int(np.floor(c))-1
        idx=np.arange(i0,i0+4); w=cubic(idx-c); w/=w.sum()
        o[y]=(p[np.clip(idx,0,h0-1)]*w[:,None]).sum(0)
    return o
def enc(Y,U,V,w,h,m='box'):
    return rs(Y,w,h,m),rs(U,w//2,h//2,m),rs(V,w//2,h//2,m)
def disp(y,u,v):
    return rs(y,1280,720,'bil'),rs(u,1280,720,'bil'),rs(v,1280,720,'bil')
def run(name):
    Y,U,V=load(name)
    # reference: what a true 1280x720 path would give from the 576 frame
    ref=[rs(Y,1280,720,'lan'),rs(U,1280,720,'bil'),rs(V,1280,720,'bil')]
    C={}
    def add(label,y,u,v,mb):
        y=np.clip(y,0,255)
        C[label]=(to_rgb(y,u,v),float(10*np.log10(255**2/np.mean((y-ref[0])**2))),mb)
    def std(w,h,label,proc=None):
        y,u,v=enc(Y,U,V,w,h); mb=(w+15)//16*((h+15)//16)
        if proc: y=proc(y)
        a,b,c=disp(y,u,v); add(label,a,b,c,mb)
    std(1024,192,'1024x192 now')
    std(1024,192,'1024x192 presharp.5',lambda y:sharp_v(y,.5))
    std(1024,192,'1024x192 presharp.8',lambda y:sharp_v(y,.8))
    y,u,v=enc(Y,U,V,1024,192); a,b,c=disp(y,u,v)
    y2=rs(cr_v(sharp_v(y,.5),720),1280,720,'bil'); add('1024x192 presharp.5+catmull',y2,b,c,768)
    std(1024,240,'1024x240')
    std(1024,240,'1024x240 presharp.5',lambda y:sharp_v(y,.5))
    std(1024,288,'1024x288')
    std(1024,360,'1024x360')
    std(320,576,'320x576')
    std(320,576,'320x576 hsharp.5',lambda y:sharp_h(y,.5))
    std(344,576,'344x576')
    std(512,576,'512x576')
    std(1024,576,'1024x576 full')
    return C,ref
for n in ('t576a','t576b'):
    C,ref=run(n)
    print(n)
    for k,(im,p,mb) in C.items(): print(f'  {k:32s} MB={mb:5d} PSNR(Y)={p:.2f}')
    pickle.dump(C,open(n+'.pkl','wb'))
