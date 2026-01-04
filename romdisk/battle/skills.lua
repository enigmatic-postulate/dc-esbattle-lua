-- /rd/data/skills.lua
local M = {}

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function find_actor_by_id(ctx, id)
  if not ctx or not ctx.actors or not id then return nil end
  for _, a in ipairs(ctx.actors) do
    if a and a.id == id then return a end
  end
  return nil
end

M.attack = {
  id = "attack",
  name = "Attack",
  target_mode = "enemy_single",

  resolve = function(ctx, user, targets)
    if not targets or #targets == 0 then
      return { ok=false, reason="no_targets" }
    end

    local t = targets[1]
    -- ✅ canonicalize to real ctx actor table
    if t and t.id then t = find_actor_by_id(ctx, t.id) or t end

    if not t or not t.hp or t.hp <= 0 then
      return { ok=false, reason="target_dead" }
    end

    -- use encounter’s hp_max
    t.hp_max = t.hp_max or t.max_hp or t.hp
    local before = t.hp

    local dmg = 5
    t.hp = clamp(before - dmg, 0, t.hp_max)

    return { ok=true, dmg=dmg, target=t, hp_before=before, hp_after=t.hp }
  end,

  present = function(pres, ctx, user, targets, result)
    if not result or not result.ok then return end

    local t = result.target or (targets and targets[1])
    if t and t.id then t = find_actor_by_id(ctx, t.id) or t end
    if not t then return end

    if pres and pres.lunge then pres.lunge(user, t, 0.10, 22) end
    if pres and pres.hit_flash then pres.hit_flash(t, 0.12) end
    if pres and pres.recoil then pres.recoil(t, 0.10, 10) end
  end
}

M.items = { id="items", name="Items", target_mode="none" }

M.list = { M.attack, M.items }
M.by_id = { attack = M.attack, items = M.items }

return M
