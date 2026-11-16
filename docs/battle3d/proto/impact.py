import sys; sys.path.insert(0,'/workspace/larp2_3d_vs1/larp3-2/tools/dmf')
import numpy as np, dmf_format as D, dmf_preview as P, static_mesh as S
textures={}
exec(open('/tmp/bproto/sheet_lib.py').read())
for which,clips in [('hero',['chop','slice','jumpchop','throw','useitem','block','blockhit','hit','death','dodgeback','taunt','jump']),('skeleton',['chop','slicehz','hit','death','spawn','awaken','taunt']),('mage',['throw','staffchop','punch','hit','death','spawn'])]:
    m=D.read('/tmp/bproto/%s.dmf'%which)
    names=[bname(b) for b in m.bones]
    for cn in clips:
        c=[c for c in m.clips if (c['name'].decode() if hasattr(c['name'],'decode') else c['name'])==cn][0]
        n=c['keys'].shape[0]; fps=c['fps']
        tip=[];hip=[];hand=[]
        for k in range(n):
            skin=P.skin_matrices(m,c['keys'][k])
            W=bone_world(m,skin,'handslot.r'); tip.append((W@np.array([0,1.2,0,1]))[:3]); hand.append(W[:3,3])
            hip.append(bone_world(m,skin,'hips')[:3,3])
        tip=np.array(tip); hip=np.array(hip); hand=np.array(hand)
        sp=np.linalg.norm(np.diff(tip,axis=0),axis=1)*fps
        kpk=int(np.argmax(sp))+1
        low=int(np.argmin(tip[:,1]))
        fwd=int(np.argmax(hand[:,2]))
        print(f"{which:8s} {cn:9s} {n/fps:4.2f}s  tip speed peak at frame {kpk} ({kpk/fps:.2f}s, {sp.max():.1f} u/s); tip lowest {low/fps:.2f}s; hand most forward {fwd/fps:.2f}s; hips travel x {hip[:,0].min():+.2f}..{hip[:,0].max():+.2f} z {hip[:,2].min():+.2f}..{hip[:,2].max():+.2f} y {hip[:,1].min():.2f}..{hip[:,1].max():.2f}")
