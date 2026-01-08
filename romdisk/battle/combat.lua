-- /rd/battle/combat.lua
local M = {}

-- NOTE: combat.lua is a low-level math/mutation module.
-- It MUST NOT require battle.status (status.lua already requires combat.lua).
-- Keep status helpers here as lightweight table scans to avoid circular requires.

local function alive(a) return a and a.hp and a.hp > 0 end

function M.has_status(actor, id)
  if not actor or not actor.status then return false end
  for i = 1, #actor.status do
    local s = actor.status[i]
    if s and s.id == id then return true end
  end
  return false
end

function M.add_status(actor, id, turns)
  if not actor then return end
  actor.status = actor.status or {}
  actor.status[#actor.status + 1] = { id = id, turns = turns or 1 }
end

function M.apply_damage(target, dmg)
  if not target or target.hp <= 0 then return end
  target.hp = math.max(0, target.hp - math.max(0, dmg))
end

-- Basic damage models (placeholders, but consistent)
function M.damage_physical(user, target, bonus)
  if not user or not target then return 0 end
  local ustr = (user.stats and user.stats.str) or 0
  local tdef = (target.stats and target.stats.def) or 0
  local dmg = math.max(1, (ustr + (bonus or 0)) - tdef)

  -- Guard halves incoming physical damage (rounded)
  if M.has_status(target, "guard") then
    dmg = math.max(1, math.floor((dmg + 1) / 2))
  end
  return dmg
end

function M.damage_magic(user, target, bonus)
  if not user or not target then return 0 end
  local uint = (user.stats and user.stats.int) or 0
  local tmdef = (target.stats and target.stats.mdef) or ((target.stats and target.stats.def) or 0)
  local dmg = math.max(1, (uint + (bonus or 0)) - tmdef)

  -- Guard does NOT reduce magic (for now)
  return dmg
end

return M
