KK='/workspace/KayKit'
def bname(b):
    n=b['name']; return n.decode() if hasattr(n,'decode') else n
def dmf_mesh(m, keys, matname):
    skin=P.skin_matrices(m,keys); Pd,Nd=P.deform(m,skin)
    tri=[]; mats=[]
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
    r=dict(mesh); r['pos']=(np.c_[mesh['pos'],np.ones(len(mesh['pos']))]@W.T)[:,:3]; r['nrm']=mesh['nrm']@W[:3,:3].T
    r['nrm']=r['nrm']/np.maximum(np.linalg.norm(r['nrm'],axis=1,keepdims=True),1e-12); return r
CHDEF={'hero':('knight','knight_texture',[('handslot.r','sword_1handed'),('handslot.l','shield_badge_color')]),
    'skeleton':('skeleton','skeleton_texture',[('handslot.r','Skeleton_Blade'),('handslot.l','Skeleton_Shield_Large_A')]),
    'mage':('mage','mage_texture',[])}
_TEX={}
def load_char(which):
    matname,tex,weap=CHDEF[which]
    m=D.read('/tmp/bproto/%s.dmf'%which)
    W={w:S.load_fbx(KK+'/Weapons/%s.fbx'%w,1.0) for _,w in weap}
    textures[matname]=S.load_texture(KK+'/Characters/%s.png'%tex,(512,512))
    textures['glow']=np.tile(np.array([120,255,230,255],np.uint8),(4,4,1))
    for mm in ('knight','skeleton'): textures.setdefault(mm,S.load_texture(KK+'/Characters/%s_texture.png'%mm,(512,512)))
    return dict(m=m,mat=matname,weap=weap,W=W)
def posed(ch, clip, t):
    m=ch['m']; c=[c for c in m.clips if (c['name'].decode() if hasattr(c['name'],'decode') else c['name'])==clip][0]
    keys=P.sample_clip(m,c,t*c['keys'].shape[0]/c['fps'])
    mesh,skin=dmf_mesh(m,keys,ch['mat'])
    for slot_,w in ch['weap']:
        mesh=S.merge(mesh, attach(ch['W'][w], bone_world(m,skin,slot_)))
    return mesh
