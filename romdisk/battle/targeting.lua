-- /rd/battle/targeting.lua
-- IMPORTANT: Always returns references to actor tables from ctx.actors (never copies).
-- This ensures hp changes, deaths, and presentation offsets actually apply to what gets drawn.

local M = {}

local function alive(a)
  return a and a.hp and a.hp > 0
end

local function is_team(a, team)
  return a and a.team == team
end

local function other_team(team)
  if team == "player" then return "enemy" end
  return "player"
end

function M.alive_team(ctx, team)
  local out = {}
  for _, a in ipairs(ctx.actors) do
    if alive(a) and is_team(a, team) then
      out[#out+1] = a -- reference
    end
  end
  return out
end

function M.alive_enemies(ctx, user)
  local team = other_team(user.team)
  return M.alive_team(ctx, team)
end

function M.alive_allies(ctx, user)
  return M.alive_team(ctx, user.team)
end

function M.count_alive_team(ctx, team)
  local n = 0
  for _, a in ipairs(ctx.actors) do
    if alive(a) and is_team(a, team) then
      n = n + 1
    end
  end
  return n
end

-- Select targets based on mode.
-- cursor_index is used only for *_single selections.
function M.select(ctx, user, mode, cursor_index)
  if mode == "self" then
    return { user } -- reference
  end

  if mode == "enemy_all" then
    return M.alive_enemies(ctx, user) -- reference list
  end

  if mode == "enemy_single" then
    local list = M.alive_enemies(ctx, user)
    if #list == 0 then return {} end
    local index = cursor_index or 1
    if index < 1 then index = 1 end
    if index > #list then index = #list end
    return { list[index] } -- reference
  end

  if mode == "ally_all" then
    return M.alive_allies(ctx, user) -- reference list
  end

  -- Unknown mode => nothing
  return {}
end

return M
