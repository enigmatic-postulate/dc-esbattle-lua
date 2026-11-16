#!/usr/bin/env python3
"""
Reference runner: executes the ORIGINAL, UNMODIFIED romdisk Lua scripts under
Lua 5.5 (same VM version as embedded in sprite_demo.elf) with a mocked engine
API, and writes a per-frame trace of every engine call + logical battle state.

The C++ port emits the identical trace format (platform/host/trace_main.cpp),
so the two can be diffed with compare.py.

Usage: lua_ref.py <romdisk_dir> <inputs.txt> <out.trace> [--native-sort] [--shipped-input-binding] [--hw-polarity]
"""
import sys, os, lupa.lua55 as lua55

args = [a for a in sys.argv[1:] if not a.startswith("--")]
flags = {a for a in sys.argv[1:] if a.startswith("--")}
romdisk, inputs_path, out_path = args

L = lua55.LuaRuntime(unpack_returned_tuples=True)
g = L.globals()
g.ROMDISK = os.path.abspath(romdisk)
g.NATIVE_SORT = "--native-sort" in flags
g.SHIPPED_BINDING = "--shipped-input-binding" in flags
g.HW_POLARITY = "--hw-polarity" in flags
g.FIX_INPUT_EDGES = "--fix-input-edges" in flags   # apply the A1 fix to the reference: clicks = press edges   # model main.cpp's inverted button test (idle reads pressed)

L.execute(r'''
-- ---------------------------------------------------------------- trace sink
TRACE = {}
local function emit(s) TRACE[#TRACE+1] = s end
local function f9(v) return string.format("%.9g", v) end

-- ---------------------------------------------------------------- package path
package.path = ROMDISK .. "/?.lua;" .. ROMDISK .. "/?/init.lua"
package.cpath = ""

-- ---------------------------------------------------------------- RNG seam
-- math.random() (float form) is replaced by a shared 24-bit LCG that the C++
-- port implements bit-identically, so randomized float-text drift is comparable.
-- math.random(m,n) (only used for battle.lua's unused INSTANCE_ID) returns m
-- without consuming the shared stream.
local rng_state = 12345
math.random = function(m, n)
  if m ~= nil then return m end
  rng_state = (rng_state * 1664525 + 1013904223) % 4294967296
  return (rng_state >> 8) / 16777216.0
end

-- ---------------------------------------------------------------- sort seam
-- Lua's table.sort is NOT stable. Default: replace with a stable insertion sort
-- (the C++ port's documented, intended semantics: ties keep encounter order).
-- --native-sort keeps the shipped behaviour to demonstrate the divergence.
if not NATIVE_SORT then
  table.sort = function(t, lt)
    lt = lt or function(a, b) return a < b end
    for i = 2, #t do
      local v = t[i]; local j = i - 1
      while j >= 1 and lt(v, t[j]) do t[j+1] = t[j]; j = j - 1 end
      t[j+1] = v
    end
  end
end

-- ---------------------------------------------------------------- engine mocks
local handle_path = {}
local n_loads = 0
sprite = {
  load = function(path)
    n_loads = n_loads + 1
    handle_path[n_loads] = path
    emit("L " .. path)
    return n_loads
  end,
  draw = function(h, x, y, w, hh, a, filter)
    local path = handle_path[h]
    if not path then return end           -- C side: invalid handle => no-op
    local f = 0
    if filter ~= nil and math.type(filter) then f = math.tointeger(filter) or 0 end
    f = (f == 2) and 2 or 0               -- C side: 2 => bilinear, else nearest
    emit(string.format("S %s %s %s %s %s %s %d", path, f9(x), f9(y), f9(w), f9(hh), f9(a), f))
  end,
}
local function rect(list, defz)
  return function(x, y, w, h, argb, z)
    emit(string.format("R %s %s %s %s %s %08X %s", list, f9(x), f9(y), f9(w), f9(h),
      math.tointeger(argb) & 0xFFFFFFFF, f9(z or defz)))
  end
end
gfx = { rect = rect("OP", 1.0), rect_tr = rect("TR", 0.1) }
dbg = { print = function(s) emit("D " .. tostring(s)) end, set_pose = function() end }

-- Button bitmask (active-high here; the C layer already converts active-low).
BTN = 0
local BIT = { up=1, down=2, left=4, right=8, fire=16, b=32, y=64, start=128 }
local ALIAS = { w="up", s="down", a="left", d="right" }
input = {
  down = function(name)
    name = ALIAS[name] or name
    -- main.cpp's l_input_down only maps dpad + "fire"; "b","y","start" => false.
    if SHIPPED_BINDING and (name == "b" or name == "y" or name == "start") then return false end
    local bit = BIT[name]
    if not bit then return false end
    local v = (BTN & bit) ~= 0
    if HW_POLARITY then v = not v end
    return v
  end,
  axis = function() return 0.0 end,
}

-- ---------------------------------------------------------------- boot
emit("F boot")
dofile(ROMDISK .. "/boot.lua")

-- Optional: the same input fix as the C++ port (slice task A1). Every click_* wrapper in
-- ui.lua gets an edge-detecting click(); all five buttons are sampled once per update().
if FIX_INPUT_EDGES then
  local ui = require("battle.ui")
  local cur, prev = {}, {}
  local names = { "fire", "left", "right", "b", "y" }
  local function edge_click(name) return (cur[name] and not prev[name]) and true or false end
  for i = 1, 64 do
    local n, v = debug.getupvalue(ui.update, i)
    if not n then break end
    if n:match("_click$") then
      for j = 1, 8 do
        local cn = debug.getupvalue(v, j)
        if cn == "click" then debug.setupvalue(v, j, edge_click) end
      end
    end
  end
  local orig_update = update
  update = function(dt)
    for _, k in ipairs(names) do prev[k] = cur[k] or false; cur[k] = input.down(k) end
    orig_update(dt)
  end
end

-- Grab module-local state for logical snapshots (read-only).
local battle = require("battle.battle")
local ctx
for i = 1, 64 do
  local name, val = debug.getupvalue(battle.update, i)
  if not name then break end
  if name == "ctx" then ctx = val end
end
assert(ctx, "could not locate ctx upvalue")

function snapshot()
  local parts = {}
  for _, a in ipairs(ctx.actors) do
    local st = {}
    for _, s in ipairs(a.status or {}) do st[#st+1] = s.id .. ":" .. tostring(s.turns) end
    parts[#parts+1] = string.format("%s:%d[%s]", a.id, a.hp, table.concat(st, ","))
  end
  local order = {}
  for _, a in ipairs(ctx.order) do order[#order+1] = a.id end
  emit(string.format("X %s active=%s turn=%d order=%s %s", ctx.state,
    ctx.active and ctx.active.id or "nil", ctx.turn_i, table.concat(order, ","),
    table.concat(parts, " ")))
end

function step(frame, dt, btn)
  BTN = btn
  emit("F " .. frame .. " " .. f9(dt))
  update(dt)
  draw()
  snapshot()
end

function drain()
  local s = table.concat(TRACE, "\n")
  TRACE = {}
  return s
end
''')

step, drain = g.step, g.drain
with open(out_path, "w") as out, open(inputs_path) as inp:
    frame = 0
    for line in inp:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        dt_s, btn_s = line.split()
        frame += 1
        step(frame, float(dt_s), int(btn_s, 16))
        if frame % 500 == 0:
            out.write(drain() + "\n")
    out.write(drain() + "\n")
