-- /rd/battle/fx.lua
-- Lightweight effects: floating numbers + simple pop text using gfx.rect (no font asset needed).
--
-- Usage:
--   local fx = require("battle.fx")
--   fx.float_damage(actor, 12)
--   fx.float_text_xy(x, y, "MISS")
--   fx.update(dt)
--   fx.draw(pres)

local M = {}

-- Active float entries
-- { x, y, vy, t, dur, text, color, scale }
local floats = {}

-- 7-seg digit layout (Lua side) ------------------------------------------------
-- Each digit is composed of 7 rectangles. We draw them in screen space.
-- Segment positions are relative to (x,y) top-left.

local SEG = {
  -- a, b, c, d, e, f, g (top, top-right, bottom-right, bottom, bottom-left, top-left, middle)
  [0] = {1,1,1,1,1,1,0},
  [1] = {0,1,1,0,0,0,0},
  [2] = {1,1,0,1,1,0,1},
  [3] = {1,1,1,1,0,0,1},
  [4] = {0,1,1,0,0,1,1},
  [5] = {1,0,1,1,0,1,1},
  [6] = {1,0,1,1,1,1,1},
  [7] = {1,1,1,0,0,0,0},
  [8] = {1,1,1,1,1,1,1},
  [9] = {1,1,1,1,0,1,1},
}

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function with_alpha(argb, a01)
  local a = clamp(math.floor(255 * a01 + 0.5), 0, 255)
  -- Lua 5.1 safe (no bitops): keep RGB, replace AA
  local rgb = argb % 0x1000000
  return a * 0x1000000 + rgb
end

local function draw_seg_digit(x, y, digit, s, argb)
  local on = SEG[digit]
  if not on then return end

  -- Size tuned for DC readability
  local w = 6 * s
  local h = 10 * s
  local t = 2 * s  -- thickness

  -- Segment rectangles
  -- a: top
  if on[1] == 1 then gfx.rect_tr(x + t, y, w, t, argb, 2.0) end
  -- b: top-right
  if on[2] == 1 then gfx.rect_tr(x + w + t, y + t, t, h, argb,2.0) end
  -- c: bottom-right
  if on[3] == 1 then gfx.rect_tr(x + w + t, y + h + 2*t, t, h, argb,2.0) end
  -- d: bottom
  if on[4] == 1 then gfx.rect_tr(x + t, y + 2*h + 2*t, w, t, argb,2.0) end
  -- e: bottom-left
  if on[5] == 1 then gfx.rect_tr(x, y + h + 2*t, t, h, argb,2.0) end
  -- f: top-left
  if on[6] == 1 then gfx.rect_tr(x, y + t, t, h, argb,2.0) end
  -- g: middle
  if on[7] == 1 then gfx.rect_tr(x + t, y + h + t, w, t, argb,2.0) end
end

local function draw_minus(x, y, s, argb)
  local w = 6 * s
  local t = 2 * s
  local h = 10 * s
  gfx.rect_tr(x + t, y + h + t, w, t, argb, 2.0)
end

local function draw_colon(x, y, s, argb)
  local t = 2 * s
  gfx.rect_tr(x, y + 6*s,  t, t, argb, 2.0)
  gfx.rect_tr(x, y + 14*s, t, t, argb, 2.0)
end


local function draw_sevenseg_string(x, y, str, s, argb)
  local cx = x
  for i = 1, #str do
    local ch = string.sub(str, i, i)
    if ch == "-" then
      draw_minus(cx, y, s, argb)
      cx = cx + 10*s
    elseif ch == ":" then
      draw_colon(cx, y, s, argb)
      cx = cx + 6*s
    elseif ch >= "0" and ch <= "9" then
      draw_seg_digit(cx, y, tonumber(ch), s, argb)
      cx = cx + 12*s
    else
      -- Unknown: tiny block
      gfx.rect_tr(cx, y + 10*s, 4*s, 4*s, argb, 2.0)
      cx = cx + 8*s
    end
  end
end

-- API -------------------------------------------------------------------------

function M.float_text_xy(x, y, text, opt)
  opt = opt or {}
  floats[#floats+1] = {
    x = x, y = y,
    vx = opt.vx or ((math.random() * 2 - 1) * (opt.vx_mag or 18)), -- -18..+18 px/sec
    vy = opt.vy or -42,
    t = 0,
    dur = opt.dur or 0.70,
    text = tostring(text or ""),
    color = opt.color or 0xFFFFFFFF,
    scale = opt.scale or 1,
  }
end


function M.float_damage(actor, dmg, opt)
  if not actor or not actor.x or not actor.y then return end
  local x = actor.x
  local y = actor.y - ((actor.h or 32) * 0.75)
  opt = opt or {}
  opt.color = opt.color or 0xFFFFE060 -- warm yellow
  opt.scale = opt.scale or 1
  M.float_text_xy(x, y, tostring(dmg), opt)
end

function M.float_status(actor, label, opt)
  if not actor or not actor.x or not actor.y then return end
  local x = actor.x
  local y = actor.y - ((actor.h or 32) * 0.95)
  opt = opt or {}
  opt.color = opt.color or 0xFF80D0FF -- light blue
  opt.scale = opt.scale or 1
  M.float_text_xy(x, y, tostring(label), opt)
end

function M.update(dt)
  local i = 1
  while i <= #floats do
    local f = floats[i]
    f.t = f.t + dt
    f.x = f.x + (f.vx * dt)
    f.y = f.y + (f.vy * dt)
    if f.t >= f.dur then
      floats[i] = floats[#floats]
      floats[#floats] = nil
    else
      i = i + 1
    end
  end
end

function M.draw(pres)
  -- pres is optional; if you want, you can feed offsets here later.
  for i = 1, #floats do
    local f = floats[i]
    local a01 = 1.0 - (f.t / f.dur)
    local argb = with_alpha(f.color, a01)
    draw_sevenseg_string(f.x, f.y, f.text, f.scale or 1, argb)
  end
end

return M
