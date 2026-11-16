#!/usr/bin/env python3
"""Measures the Lua battle scripts' per-frame cost with no-op engine mocks:
   - garbage allocated per frame (KB, GC stopped) -> platform independent
   - host CPU time per frame (indicative only; not Dreamcast timing)
   usage: bench_lua.py <romdisk> <inputs.txt> [reps]"""
import sys, time, lupa.lua55 as lua55
romdisk, inputs = sys.argv[1], sys.argv[2]
reps = int(sys.argv[3]) if len(sys.argv) > 3 else 3
frames = []
for line in open(inputs):
    if line.startswith("#") or not line.strip(): continue
    d, b = line.split(); frames.append((float(d), int(b, 16)))

L = lua55.LuaRuntime()
g = L.globals(); g.ROMDISK = romdisk
L.execute(r'''
package.path = ROMDISK .. "/?.lua;" .. ROMDISK .. "/?/init.lua"
local n = 0
CALLS = 0
sprite = { load = function() n = n + 1; return n end, draw = function() CALLS = CALLS + 1 end }
gfx = { rect = function() CALLS = CALLS + 1 end, rect_tr = function() CALLS = CALLS + 1 end }
dbg = { print = function() end }
BTN = 0
local BIT = { up=1, down=2, left=4, right=8, fire=16, b=32, y=64, start=128 }
input = { down = function(name) local b = BIT[name]; return b ~= nil and (BTN & b) ~= 0 end,
          axis = function() return 0 end }
dofile(ROMDISK .. "/boot.lua")
local battle = require("battle.battle")
local ctx
for i = 1, 64 do local nm, v = debug.getupvalue(battle.update, i); if nm == "ctx" then ctx = v end end
RESTARTS = 0
function run(dts, btns, nframes)
  for i = 1, nframes do
    BTN = btns[i]; update(dts[i]); draw()
    if ctx.state == "win" or ctx.state == "lose" then battle.start(); RESTARTS = RESTARTS + 1 end
  end
end
''')
dts = L.table_from([f[0] for f in frames]); btns = L.table_from([f[1] for f in frames])
run = g.run
L.execute("collectgarbage('collect'); collectgarbage('stop')")
kb0 = L.eval("collectgarbage('count')"); c0 = g.CALLS
run(dts, btns, len(frames))
kb1 = L.eval("collectgarbage('count')"); c1 = g.CALLS
L.execute("collectgarbage('restart'); collectgarbage('collect')")
print(f"lua: battles completed: {g.RESTARTS}")
print(f"lua: garbage {(kb1-kb0)*1024/len(frames):.0f} bytes/frame, engine calls {(c1-c0)/len(frames):.1f}/frame (avg over {len(frames)} frames)")
best = 1e9
for _ in range(reps):
    t0 = time.perf_counter(); run(dts, btns, len(frames)); t1 = time.perf_counter()
    best = min(best, (t1 - t0) / len(frames) * 1e6)
print(f"lua: {best:.2f} us/frame host CPU (best of {reps}, GC on)")
