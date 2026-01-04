-- /rd/battle/presentation.lua
local M = {}

local moves = {}   -- { actor, t, dur, nx, ny, dist }
local flashes = {} -- { actor, t, dur }
local flash_spr = sprite.load("/rd/plasma.png")

local function norm(dx, dy)
  local len = math.sqrt(dx*dx + dy*dy)
  if len < 0.0001 then return 0, 0 end
  return dx/len, dy/len
end

local function push_move(actor, dur, nx, ny, dist)
  moves[#moves+1] = { actor=actor, t=0, dur=dur, nx=nx, ny=ny, dist=dist }
end

function M.lunge(attacker, target, dur, dist)
  if not attacker or not target then return end
  dur  = dur  or 0.10
  dist = dist or 22
  local dx = (target.x or 0) - (attacker.x or 0)
  local dy = (target.y or 0) - (attacker.y or 0)
  local nx, ny = norm(dx, dy)
  push_move(attacker, dur, nx, ny, dist)
end

function M.recoil(target, dur, dist)
  if not target then return end
  dur  = dur  or 0.10
  dist = dist or 10
  -- quick “bump” left
  push_move(target, dur, -1, 0, dist)
end

function M.hit_flash(target, dur)
  if not target then return end
  flashes[#flashes+1] = { actor=target, t=0, dur=(dur or 0.12) }
end

function M.update(dt)
  -- moves
  local i = 1
  while i <= #moves do
    local e = moves[i]
    e.t = e.t + dt
    if e.t >= e.dur then
      moves[i] = moves[#moves]
      moves[#moves] = nil
    else
      i = i + 1
    end
  end

  -- flashes
  local j = 1
  while j <= #flashes do
    local f = flashes[j]
    f.t = f.t + dt
    if f.t >= f.dur then
      flashes[j] = flashes[#flashes]
      flashes[#flashes] = nil
    else
      j = j + 1
    end
  end
end

function M.offset(actor)
  local ox, oy = 0, 0
  for i = 1, #moves do
    local e = moves[i]
    if e.actor == actor then
      local u = e.t / e.dur
      -- triangle wave 0->1->0
      local a = (u < 0.5) and (u / 0.5) or ((1 - u) / 0.5)
      ox = ox + (e.nx * e.dist * a)
      oy = oy + (e.ny * e.dist * a)
    end
  end
  return ox, oy
end

function M.done()
  return (#moves == 0) and (#flashes == 0)
end

function M.draw()
  -- visible flash marker (easy validation)
  for i = 1, #flashes do
    local a = flashes[i].actor
    if a and a.x and a.y and a.h then
      sprite.draw(flash_spr, a.x, a.y - (a.h * 0.75), 14, 14, 0)
    end
  end
end

return M
