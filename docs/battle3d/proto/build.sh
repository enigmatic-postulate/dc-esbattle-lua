cd /workspace/KayKit
A=Animations; T=/workspace/larp2_3d_vs1/larp3-2/tools/dmf
G=$A/Rig_Medium_General.fbx; M=$A/Rig_Medium_CombatMelee.fbx; MB=$A/Rig_Medium_MovementBasic.fbx; MA=$A/Rig_Medium_MovementAdvanced.fbx; SP=$A/Rig_Medium_Special.fbx
/tmp/venv/bin/python $T/fbx2dmf.py --model Characters/Knight.fbx --texture Characters/knight_texture.png --no-hidden-pass --max-error 0.025 \
 --native-anim $G:Idle_B:idle:15 --native-anim $MB:Running_A:run:15 --native-anim $M:Melee_1H_Attack_Chop:chop:15 --native-anim $M:Melee_1H_Attack_Slice_Diagonal:slice:15 \
 --native-anim $M:Melee_1H_Attack_Jump_Chop:jumpchop:15 --native-anim $MA:Dodge_Backward:dodgeback:15 --native-anim $G:Throw:throw:15 --native-anim $G:Use_Item:useitem:15 \
 --native-anim $M:Melee_Block:block:15 --native-anim $M:Melee_Blocking:blocking:15 --native-anim $M:Melee_Block_Hit:blockhit:15 --native-anim $G:Hit_A:hit:15 --native-anim $G:Death_A:death:15 \
 --native-anim $SP:Skeletons_Taunt:taunt:15 --native-anim $MB:Jump_Full_Short:jump:15 \
 -o /tmp/bproto/hero.dmf > /tmp/bproto/hero.log 2>&1
/tmp/venv/bin/python $T/fbx2dmf.py --model Characters/Skeleton_Warrior.fbx --texture Characters/skeleton_texture.png --unlit Glow=78FFE6 --no-hidden-pass --max-error 0.025 \
 --native-anim $SP:Skeletons_Idle:idle:15 --native-anim $SP:Skeletons_Spawn_Ground:spawn:15 --native-anim $SP:Skeletons_Awaken_Standing:awaken:15 --native-anim $MB:Running_A:run:15 \
 --native-anim $M:Melee_1H_Attack_Chop:chop:15 --native-anim $M:Melee_1H_Attack_Slice_Horizontal:slicehz:15 --native-anim $G:Hit_B:hit:15 --native-anim $SP:Skeletons_Death:death:15 --native-anim $SP:Skeletons_Taunt:taunt:15 \
 -o /tmp/bproto/skeleton.dmf > /tmp/bproto/skeleton.log 2>&1
/tmp/venv/bin/python $T/fbx2dmf.py --model Characters/Mage.fbx --texture Characters/mage_texture.png --no-hidden-pass --max-error 0.02 \
 --native-anim $G:Idle_A:idle:15 --native-anim $G:Spawn_Air:spawn:15 --native-anim $G:Throw:throw:15 --native-anim $M:Melee_2H_Attack_Chop:staffchop:15 --native-anim $M:Melee_Unarmed_Attack_Punch_A:punch:15 \
 --native-anim $G:Hit_A:hit:15 --native-anim $G:Death_B:death:15 \
 -o /tmp/bproto/mage.dmf > /tmp/bproto/mage.log 2>&1
echo done > /tmp/bproto/done
