-- /rd/battle/hpbar.lua
local assets = require("assets")
local M = {}

local bar_spr = assets.sprite("/rd/plasma.png")

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
end

function M.draw_all(actors, pres)
  for _, a in ipairs(actors) do
    M.draw_for_actor(a, pres)
  end
end

return M
