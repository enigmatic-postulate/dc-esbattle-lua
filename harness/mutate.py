#!/usr/bin/env python3
"""
Mutation test of the differential harness: injects realistic porting mistakes
into the C++ port one at a time and checks that the suite detects ("kills") each.
Uses the cached Lua traces from run_suite.sh (WORK=/tmp/suite).
"""
import subprocess, sys, os, glob, shutil, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.environ.get("WORK", "/tmp/suite")

M = [
 ("recoil direction flipped",            "presentation.cpp", "push_move(ctx, target, dur, -1, 0, dist)", "push_move(ctx, target, dur, 1, 0, dist)"),
 ("swap-remove skips swapped-in element","fx.cpp",           "if (f.t >= f.dur) v.swap_remove(i);\n        else ++i;", "if (f.t >= f.dur) v.swap_remove(i);\n        ++i;"),
 ("erase() instead of swap-remove order","presentation.cpp", "if (f[j].t >= f[j].dur) f.swap_remove(j); else ++j;", "if (f[j].t >= f[j].dur) { for (int k = j; k + 1 < f.size(); ++k) f[k] = f[k + 1]; f.n--; } else ++j;"),
 ("status expires at <0 instead of <=0", "status.cpp",       "if (st.turns <= 0) {", "if (st.turns < 0) {"),
 ("home-ring left wrap wrong",           "ui.cpp",           "ui.home_i = (ui.home_i + 2) % 3;", "ui.home_i = (ui.home_i + 1) % 3;"),
 ("target entry lock dropped",           "ui.cpp",           "ui.target_entry_lock = TARGET_ENTRY_LOCK_S;", "ui.target_entry_lock = 0;"),
 ("Lua `and` operand order swapped (B)", "ui.cpp",           "if (click(ui, UB_B) && ui.state == UiState::Target)", "if (ui.state == UiState::Target && click(ui, UB_B))"),
 ("Lua `and` operand order swapped (A)", "ui.cpp",           "if (ui.target_entry_lock == 0 && click(ui, UB_FIRE))", "if (click(ui, UB_FIRE) && ui.target_entry_lock == 0)"),
 ("elseif ported as independent if",     "ui.cpp",           "else if (click(ui, UB_RIGHT)) { ui.home_i", "if (click(ui, UB_RIGHT)) { ui.home_i"),
 ("min-1 damage clamp forgotten",        "combat.cpp",       "int dmg = maxv(1, (user->stats.str + bonus) - target->stats.def);", "int dmg = (user->stats.str + bonus) - target->stats.def;"),
 ("AOE 0.65 scaling truncated wrong",    "skills.cpp",       "int dmg = maxv(1, (base * 65) / 100);", "int dmg = maxv(1, (base * 2) / 3);"),
 ("burn applied in resolve (ordering)",  "skills.cpp",       "        r.hits.push(Hit{ t, dmg });", "        r.hits.push(Hit{ t, dmg });\n        status_add(ctx_, *t, StatusId::Burn, 2, 2);"),
 ("alpha rounding (+0.5) dropped",       "fx.cpp",           "(int)floor(255 * a01 + (real)0.5)", "(int)floor(255 * a01)"),
 ("hurt marker only for living actors",  "battle.cpp",       "        if (a.hurt_t > 0)   // drawn even", "        if (a.hp > 0 && a.hurt_t > 0)   // drawn even"),
 ("UI idle-baseline replaced by edge",   "ui.cpp",           "static bool active(const Ui& ui, int b) { return probe_down(UB_KEY[b]) != ui.idle[b]; }", "static bool active(const Ui& ui, int b) { (void)ui; return probe_down(UB_KEY[b]); }"),
 ("state change applied same frame",     "battle.cpp",       "        ctx.anim_t = 0;\n        ctx.state = BattleState::Animate;\n        break;", "        ctx.anim_t = 0;\n        ctx.state = BattleState::Animate;\n        ctx.anim_t = ctx.anim_t + dt;\n        break;"),
 ("pip fill drawn before border",        "hpbar.cpp",        "        eng::gfx_rect_tr((float)(px - 1), (float)(py - 1), (float)(pip_sz + 2), (float)(pip_sz + 2), 0xFF000000, z);\n        eng::gfx_rect_tr((float)px, (float)py, (float)pip_sz, (float)pip_sz, col, z);", "        eng::gfx_rect_tr((float)px, (float)py, (float)pip_sz, (float)pip_sz, col, z);\n        eng::gfx_rect_tr((float)(px - 1), (float)(py - 1), (float)(pip_sz + 2), (float)(pip_sz + 2), 0xFF000000, z);"),
 ("enemy wobble uses sin*0.1 w/o anim", "battle.cpp",        "rot = sin(ctx.anim_t * (real)6.0) * (real)0.10;", "rot = sin(ctx.anim_t * (real)6.0) * (real)0.11;"),
 ("turn index not wrapped via order",   "turn_system.cpp",   "ctx.turn_i = (ctx.turn_i + 1) % n;", "ctx.turn_i = (ctx.turn_i + 1) % (n + 1); if (ctx.turn_i == n) continue;"),
 ("guard rounding (d+1)/2 -> d/2",       "combat.cpp",       "dmg = maxv(1, (dmg + 1) / 2);", "dmg = maxv(1, dmg / 2);"),
]

cases = sorted(glob.glob(f"{WORK}/*.in"))
assert cases, "run run_suite.sh first"
# Scripted scenarios first (fast kills), then fuzz.
cases.sort(key=lambda p: (os.path.basename(p).startswith("fuzz"), p))

killed = 0
for name, fname, old, new in M:
    tmp = tempfile.mkdtemp()
    shutil.copytree(os.path.join(ROOT, "src"), os.path.join(tmp, "src"))
    p = os.path.join(tmp, "src", fname)
    s = open(p).read()
    if old not in s:
        print(f"  ?? {name}: pattern not found"); continue
    s = s.replace(old, new, 1)
    if "ctx_" in new:
        s = s.replace("static SkillResult fireball_resolve(BattleCtx&,", "static SkillResult fireball_resolve(BattleCtx& ctx_,")
    open(p, "w").write(s)
    exe = os.path.join(tmp, "bt")
    b = subprocess.run(["g++", "-std=c++14", "-O1", "-DBATTLE_REAL_DOUBLE", "-w", *glob.glob(f"{tmp}/src/*.cpp"),
                        f"{ROOT}/platform/host/host_engine.cpp", f"{ROOT}/platform/host/trace_main.cpp", "-I", f"{tmp}/src", "-o", exe], capture_output=True, text=True)
    if b.returncode:
        print(f"  !! {name}: build failed\n{b.stderr[:500]}"); continue
    hit = None
    for c in cases:
        subprocess.run([exe, c, c + ".mut"], check=True)
        r = subprocess.run([sys.executable, os.path.join(ROOT, "harness/compare.py"), c + ".lua", c + ".mut", "--quiet"],
                           capture_output=True, text=True)
        if r.returncode:
            hit = (os.path.basename(c)[:-3], r.stdout.splitlines()[0]); break
    shutil.rmtree(tmp)
    if hit:
        killed += 1; print(f"  KILLED   {name:40s} by {hit[0]:22s} {hit[1][:70]}")
    else:
        print(f"  SURVIVED {name:40s}")
print(f"mutation score: {killed}/{len(M)}")
