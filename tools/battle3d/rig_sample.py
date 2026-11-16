"""rig_sample.py - samples KayKit Rig_Medium clips from FBX into the .dmf bone convention.
Runs in Blender's Python (bpy). See build_anims.py.

Convention (checked against the converted models, see build_anims.py --check): model space is the
armature space of the imported FBX (Y up, the character faces +Z); a bone's local transform is its
pose matrix relative to its parent's pose matrix. The 'root' bone is kept at identity and its
motion is folded into 'hips' (root's only child), so root motion (dodges, the skeletons' death
slide) lives in the hips translation.
"""
import bpy
import numpy as np


def import_fbx(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
    return arm


def mat_np(m):
    return np.array([[m[i][j] for j in range(4)] for i in range(4)], dtype=float)


def rest_locals(arm):
    """Bind-pose (parent-relative) transforms, one 4x4 per bone, in bone order."""
    out = []
    for b in arm.data.bones:
        M = mat_np(b.matrix_local)
        if b.parent is not None:
            M = np.linalg.inv(mat_np(b.parent.matrix_local)) @ M
        out.append(M)
    return out


def mat_to_quat(R):
    """3x3 rotation -> quaternion (x, y, z, w), w >= 0."""
    m = R
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = np.sqrt(tr + 1.0) * 2
        w = 0.25 * s; x = (m[2, 1] - m[1, 2]) / s; y = (m[0, 2] - m[2, 0]) / s; z = (m[1, 0] - m[0, 1]) / s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        w = (m[2, 1] - m[1, 2]) / s; x = 0.25 * s; y = (m[0, 1] + m[1, 0]) / s; z = (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        w = (m[0, 2] - m[2, 0]) / s; x = (m[0, 1] + m[1, 0]) / s; y = 0.25 * s; z = (m[1, 2] + m[2, 1]) / s
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        w = (m[1, 0] - m[0, 1]) / s; x = (m[0, 2] + m[2, 0]) / s; y = (m[1, 2] + m[2, 1]) / s; z = 0.25 * s
    q = np.array([x, y, z, w])
    q /= np.linalg.norm(q)
    return -q if q[3] < 0 else q


def sample_action(arm, action):
    """Returns (frames, nb, 4, 4) parent-relative local matrices (root folded into hips) and the
    armature-space pose matrices (frames, nb, 4, 4)."""
    arm.animation_data_create()
    arm.animation_data.action = action
    f0, f1 = int(round(action.frame_range[0])), int(round(action.frame_range[1]))
    bones = arm.pose.bones
    names = [b.name for b in arm.data.bones]
    idx = {n: i for i, n in enumerate(names)}
    parent = [idx[b.parent.name] if b.parent else -1 for b in arm.data.bones]
    locs, worlds = [], []
    for f in range(f0, f1 + 1):
        bpy.context.scene.frame_set(f)
        W = [mat_np(bones[n].matrix) for n in names]
        L = []
        for i, n in enumerate(names):
            if n == 'root':
                L.append(np.eye(4))
            elif parent[i] >= 0 and names[parent[i]] != 'root':
                L.append(np.linalg.inv(W[parent[i]]) @ W[i])
            else:
                L.append(W[i])          # hips: armature space == relative to an identity root
        locs.append(L)
        # model-space world matrices with the root folded (== the pose chain the runtime builds)
        worlds.append(W)
    return np.array(locs), np.array(worlds), names, parent
