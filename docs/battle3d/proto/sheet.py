import sys; sys.path.insert(0,'/workspace/larp2_3d_vs1/larp3-2/tools/dmf')
import numpy as np, dmf_format as D, dmf_preview as P, static_mesh as S
from PIL import Image, ImageDraw
KK='/workspace/KayKit'
def dmf_mesh(m, keys, matname):
    skin=P.skin_matrices(m,keys); Pd,Nd=P.deform(m,skin)
    tri=[]
    strips = [s for sm in m.submeshes for s in sm[0]] if m.submeshes else m.strips
    mats=[]
    if m.submeshes:
        for strips_, t, fl, argb in m.submeshes:
            for s in strips_:
                for k in range(len(s)-2): tri.append((s[k],s[k+1],s[k+2])); mats.append(0 if t>=0 else 1)
    else:
        for s in m.strips:
            for k in range(len(s)-2): tri.append((s[k],s[k+1],s[k+2])); mats.append(0)
    tri=np.array(tri); mats=np.array(mats)
    ok=(tri[:,0]!=tri[:,1])&(tri[:,1]!=tri[:,2])&(tri[:,0]!=tri[:,2])
    return dict(pos=Pd,nrm=Nd,uv=m.vertices['uv'].astype(np.float64),tri=tri[ok],mat=mats[ok],mats=[matname,'glow']), skin
def bone_world(m, skin, name):
    i=[bname(b) for b in m.bones].index(name)
    ib=np.eye(4); ib[:3,:]=np.asarray(m.bones[i]['inv_bind'],np.float64).reshape(3,4)
    return skin[i]@np.linalg.inv(ib)
def attach(mesh, W):
    r=dict(mesh); r['pos']=(np.c_[mesh['pos'],np.ones(len(mesh['pos']))]@W.T)[:,:3]; r['nrm']=mesh['nrm']@W[:3,:3].T; return r
def bname(b):
    n=b['name']; return n.decode() if isinstance(n,(bytes,)) or hasattr(n,'decode') else n
CH={'hero':('knight',KK+'/Characters/knight_texture.png',[('handslot.r','sword_1handed'),('handslot.l','shield_badge_color')]),
    'skeleton':('skeleton',KK+'/Characters/skeleton_texture.png',[('handslot.r','Skeleton_Blade'),('handslot.l','Skeleton_Shield_Large_A')]),
    'mage':('mage',KK+'/Characters/mage_texture.png',[('handslot.r','Skeleton_Staff')])}
which=sys.argv[1]; matname,texp,weap=CH[which]
m=D.read('/tmp/bproto/%s.dmf'%which)
tex=S.load_texture(texp,(512,512))
sk=S.load_texture(KK+'/Characters/skeleton_texture.png',(512,512))
textures={matname:tex,'glow':np.tile(np.array([120,255,230,255],np.uint8),(4,4,1)),'knight':S.load_texture(KK+'/Characters/knight_texture.png',(512,512)),'skeleton':sk}
W={w:S.load_fbx(KK+'/Weapons/%s.fbx'%w,1.0) for _,w in weap}
rows=[]
TS=150
for c in m.clips:
    n=c['keys'].shape[0]; dur=n/c['fps']
    tiles=[]
    for f in (0.0,0.25,0.5,0.75):
        keys=P.sample_clip(m,c,dur*f)
        mesh,skin=dmf_mesh(m,keys,matname)
        for slot,w in weap:
            mesh=S.merge(mesh, attach(W[w], bone_world(m,skin,slot)))
        c0=np.array([0,1.3,0.3]); yaw=np.radians(55); pit=np.radians(14)
        d=np.array([np.sin(yaw)*np.cos(pit),np.sin(pit),np.cos(yaw)*np.cos(pit)])
        img,_,_=S.render(mesh,textures,c0+d*12,c0,size=(TS,TS+20),ortho=1.9)
        tiles.append(img)
    rows.append((c['name'] if isinstance(c['name'],str) else c['name'].decode(), '%.2fs %d keys'%(dur,n), tiles))
sheet=Image.new('RGB',(110+4*TS, len(rows)*(TS+22)),(22,22,26)); d=ImageDraw.Draw(sheet)
for i,(nm,info,tiles) in enumerate(rows):
    y=i*(TS+22)
    d.text((4,y+60),nm,fill=(255,255,255)); d.text((4,y+76),info,fill=(180,180,180))
    for j,t in enumerate(tiles):
        sheet.paste(Image.fromarray((np.clip(t,0,1)*255).astype(np.uint8)),(110+j*TS,y))
sheet.save('/tmp/bproto/clips_%s.png'%which); print('ok',len(rows))
