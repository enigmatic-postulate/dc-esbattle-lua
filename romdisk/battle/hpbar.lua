-- /rd/battle/hpbar.lua
local assets = require("assets")
local M = {}

local bar_spr = assets.sprite("/rd/plasma.png")

-- Status pips (small persistent indicators) ---------------------------------
-- We intentionally keep this "dumb": no font, no icons; just colored pips.
-- Expected actor.status model: { { id="burn", turns=3, ... }, ... }

-- Status pips ---------------------------------------------------------------
local PIP = {
  burn  = 0xFFFFA040, -- orange
  guard = 0xFF40A0FF, -- blue
  stun  = 0xFFFFFF80, -- pale yellow
}

local function status_pip_color(id)
  return PIP[id] or 0xFFC0C0C0
end

local function draw_status_pips(a, x, y, bar_w)
  if not a or not a.status or #a.status == 0 then return end

  local pip_sz   = 8
  local gap      = 3
  local max_pips = 3
  local z        = 2.0  -- same visible layer as fx

  local n = math.min(#a.status, max_pips)

  -- Place ABOVE the HP bar, centered over its left side
  local px = x + 2
  local py = y - (pip_sz + 3)

  -- Backing plate so pips don't disappear over bright sprites
  local plate_w = (n * pip_sz) + ((n - 1) * gap) + 4 + ((#a.status > max_pips) and 6 or 0)
  local plate_h = pip_sz + 4
  gfx.rect_tr(px - 2, py - 2, plate_w, plate_h, 0xC0000000, z) -- translucent black

  for i = 1, n do
    local st = a.status[i]
    local col = status_pip_color(st and st.id)

    -- border + fill
    gfx.rect_tr(px - 1, py - 1, pip_sz + 2, pip_sz + 2, 0xFF000000, z)
    gfx.rect_tr(px,     py,     pip_sz,     pip_sz,     col,        z)

    px = px + pip_sz + gap
  end

  if #a.status > max_pips then
    -- overflow marker: 3 tiny dots
    gfx.rect_tr(px + 1, py + 3, 2, 2, 0xFFFFFFFF, z)
    gfx.rect_tr(px + 4, py + 3, 2, 2, 0xFFFFFFFF, z)
    gfx.rect_tr(px + 7, py + 3, 2, 2, 0xFFFFFFFF, z)
  end
end


local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

function M.draw_for_actor(a, pres)
  if not a or not a.hp or a.hp <= 0 then return end

  a.hp_max = a.hp_max or a.max_hp or a.hp
  local hpmax = a.hp_max
  if not hpmax or hpmax <= 0 then return end

  local ratio = clamp(a.hp / hpmax, 0, 1)

  local bar_w = (a.w or 64) * 0.90
  local bar_h = 10
  local w = math.max(2, bar_w * ratio)

  local ox, oy = 0, 0
  if pres and pres.offset then ox, oy = pres.offset(a) end

  local x = (a.x or 0) + ox
  local y = (a.y or 0) + oy - ((a.h or 64) * 0.65)

  sprite.draw(bar_spr, x, y, w, bar_h, 0)

  draw_status_pips(a, x, y, bar_w)
end

function M.draw_all(actors, pres)
  for _, a in ipairs(actors) do
    M.draw_for_actor(a, pres)
  end
end

return M
