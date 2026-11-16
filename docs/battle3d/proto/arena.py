import sys, os; sys.path.insert(0,'/workspace/larp2_3d_vs1/larp3-2/tools/dmf')
import numpy as np, dmf_format as D, dmf_preview as P, static_mesh as S, props as PR
from PIL import Image, ImageDraw
exec(open('/tmp/bproto/sheet_lib.py').read())
ASSETS='/workspace'
cache=PR.Cache('/workspace/larp2_3d_vs1/larp3-2/build/props_cache')
HERO_H=1.55
# ---------------- arena layout (metres; x east, z south = toward the default camera) -----------
LAYOUT=[('house_plaster',0,(-4.6,-10.0)),('arch',0,(1.2,-7.8)),('house_stone',0,(7.4,-9.8)),
        ('wagon',90,(-8.2,-3.2)),('boulder02',0,(-8.6,1.8)),('boulder03',90,(9.0,-1.4)),('boulder03',0,(7.6,-5.4)),
        ('crate',0,(4.4,-6.4)),('crate',0,(5.4,-6.1)),('crate',0,(-3.9,-6.3)),
        ('fence',0,(-2.1,-6.8)),('fence',0,(2.1,-6.8)),('fence',90,(-6.2,-5.2)),('fence',90,(6.6,-3.0))]
scene=S.empty(); prop_tris=0; per=[]
for prefab,yaw,(x,z) in LAYOUT:
    m=PR.assemble(ASSETS,cache,prefab,yaw)
    m=S.transform(m,(x,0,z),0); prop_tris+=len(m['tri']); per.append((prefab,len(m['tri'])))
    scene=S.merge(scene,m)
textures=PR.source_textures(ASSETS,256)
# ground: 40x40 m quad, procedural grass + a dirt ring of radius 5
N=512; yy,xx=np.mgrid[0:N,0:N]; wx=(xx/N-0.5)*40; wz=(yy/N-0.5)*40
rng=np.random.default_rng(0); noise=rng.random((N,N))
r=np.hypot(wx,wz-0.0)
dirt=np.clip((5.4-r)/0.8,0,1)
g=np.dstack([0.30+0.06*noise,0.50+0.08*noise,0.22+0.04*noise]); d=np.dstack([0.55+0.08*noise,0.43+0.06*noise,0.30+0.05*noise])
tex=(g*(1-dirt[...,None])+d*dirt[...,None]); tex=(np.dstack([tex,np.ones((N,N))])*255).astype(np.uint8)
textures['ground']=tex
G=40; gx,gz=np.meshgrid(np.linspace(-20,20,G+1),np.linspace(-20,20,G+1)); gp=np.c_[gx.ravel(),np.zeros(gx.size),gz.ravel()]
gT=[]
for i in range(G):
    for j in range(G):
        a=i*(G+1)+j; gT += [[a,a+G+1,a+1],[a+1,a+G+1,a+G+2]]
gq=dict(pos=gp,nrm=np.tile([0,1,0],(len(gp),1)).astype(float),uv=np.c_[(gp[:,0]+20)/40,(gp[:,2]+20)/40],tri=np.array(gT),mat=np.zeros(len(gT),np.int64),mats=['ground'])
scene=S.merge(scene,gq)
# ---------------- combatants (2D encounter coordinates mapped to the arena) --------------------
def slot(x2d,y2d): return ((x2d-320)/56.0, (y2d-260)/48.0)
hero=load_char('hero'); skel=load_char('skeleton'); mage=load_char('mage')
def place(ch, clip, t, x, z, yaw_deg):
    mesh=posed(ch,clip,t)
    s=HERO_H/(ch['m'].bbox[4]-ch['m'].bbox[1])
    W=np.eye(4); a=np.radians(yaw_deg); W[:3,:3]=np.array([[np.cos(a),0,np.sin(a)],[0,1,0],[-np.sin(a),0,np.cos(a)]])*s; W[0,3]=x; W[2,3]=z
    return attach(mesh,W)
def ring(x,z,r0,r1,mat,n=40,y=0.02):
    a=np.linspace(0,2*np.pi,n,endpoint=False); P_=[];T=[]
    for i,ang in enumerate(a):
        P_ += [[x+r0*np.cos(ang),y,z+r0*np.sin(ang)],[x+r1*np.cos(ang),y,z+r1*np.sin(ang)]]
        j=(i+1)%n; T += [[2*i,2*j,2*i+1],[2*i+1,2*j,2*j+1]]
    return dict(pos=np.array(P_),nrm=np.tile([0,1,0],(2*n,1)).astype(float),uv=np.zeros((2*n,2)),tri=np.array(T),mat=np.zeros(len(T),np.int64),mats=[mat])
def arrow(x,y,z,mat):
    P_=np.array([[x,y,z],[x-0.15,y+0.3,z],[x+0.15,y+0.3,z],[x,y+0.3,z-0.15],[x,y+0.3,z+0.15]],float)
    T=np.array([[0,1,3],[0,3,2],[0,2,4],[0,4,1],[1,4,2],[1,2,3]])
    return dict(pos=P_,nrm=np.tile([0,1,0],(5,1)).astype(float),uv=np.zeros((5,2)),tri=T,mat=np.zeros(6,np.int64),mats=[mat])
textures['sel']=np.tile(np.array([255,205,117,255],np.uint8),(4,4,1))
textures['enemyturn']=np.tile(np.array([239,125,87,255],np.uint8),(4,4,1))
ENC={'test_slimes':[(1.1,-1.5),(2.3,-0.2),(3.5,1.1)],'slimes_2':[(1.6,-1.0),(3.0,0.6)],'guard_duel':[(2.3,-0.2)]}
HERO_SLOT=(-2.6,0.9)
def build(enc, hero_clip=('idle',0.3), target=None, all_targets=False):
    sc=scene; ch_tris=0
    hx,hz=HERO_SLOT
    hm=place(hero,*hero_clip,hx,hz,90); sc=S.merge(sc,hm); ch_tris+=len(hm['tri'])
    pos=[]
    for i,(x,z) in enumerate(ENC[enc]):
        pos.append((x,z))
        ch = mage if enc=='guard_duel' else skel
        em=place(ch,'idle',0.37*i,x,z,-90); sc=S.merge(sc,em); ch_tris+=len(em['tri'])
    if target is not None:
        x,z=pos[target]; sc=S.merge(sc,ring(x,z,0.55,0.7,'sel')); sc=S.merge(sc,arrow(x,1.95,z,'sel'))
    if all_targets:
        for x,z in pos: sc=S.merge(sc,ring(x,z,0.55,0.7,'enemyturn'))
    return sc, ch_tris
SHOTS={'overview':((-2.4,3.2,8.2),(0.9,0.7,-0.6),40),
       'over_shoulder':((-5.2,2.0,2.6),(1.5,1.0,-0.2),34),
       'enemy_side':((5.5,2.4,4.6),(-1.0,0.9,-0.3),38)}
def shot(sc,name,size=(640,480)):
    eye,tgt,fov=SHOTS[name]
    img,_,_=S.render(sc,textures,np.array(eye,float),np.array(tgt,float),size=size,fov=fov,alpha_mats=('MI_Vine',),bg=(0.30,0.38,0.55))
    return Image.fromarray((np.clip(img,0,1)*255).astype(np.uint8))
if __name__=='__main__':
    out=[]
    sc,ct=build('test_slimes',target=1); out.append(('overview - test_slimes, target cursor on slime_b',shot(sc,'overview')))
    sc,ct2=build('test_slimes',all_targets=True); out.append(('overview - Fire: all enemies marked',shot(sc,'overview')))
    sc,_=build('test_slimes',hero_clip=('chop',0.45)); out.append(('action cam - hero attack',shot(sc,'over_shoulder')))
    sc,_=build('guard_duel'); out.append(('overview - guard_duel (Mage)',shot(sc,'overview')))
    sheet=Image.new('RGB',(1280,2*500),(20,20,24)); d=ImageDraw.Draw(sheet)
    for i,(lab,im) in enumerate(out):
        x,y=(i%2)*640,(i//2)*500
        sheet.paste(im,(x,y+20)); d.text((x+6,y+4),lab,fill=(255,255,255))
    sheet.save('/tmp/bproto/arena_mock.png')
    print('props',prop_tris,per,'characters',ct)
