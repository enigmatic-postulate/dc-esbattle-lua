-- /rd/data/skills.lua
-- Skills used by battle/ui.lua and battle/battle.lua via: require("data.skills")

local combat = require("battle.combat")

local M = {}

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function find_actor_by_id(ctx, id)
  if not ctx or not ctx.actors or not id then return nil end
  for i = 1, #ctx.actors do
    local a = ctx.actors[i]
    if a and a.id == id then return a end
  end
  return nil
end

local function canon_target(ctx, t)
  if not t then return nil end
  if t.id then
    return find_actor_by_id(ctx, t.id) or t
  end
  return t
end

-- ------------------------------------------------------------
-- ATTACK (single enemy)
-- ------------------------------------------------------------
M.attack = {
  id = "attack",
  name = "Attack",
  target_mode = "enemy_single",

  resolve = function(ctx, user, targets)
    if not user or not targets or #targets == 0 then
      return { ok=false, reason="no_targets" }
    end

    local t = canon_target(ctx, targets[1])
    if not t or not t.hp or t.hp <= 0 then
      return { ok=false, reason="target_dead" }
    end

    t.max_hp = t.max_hp or t.hp_max or t.hp

    local dmg = combat.damage_physical(user, t, 0)
    combat.apply_damage(t, dmg)
    t.hp = clamp(t.hp, 0, t.max_hp)

    return { ok=true, kind="single", target=t, dmg=dmg }
  end,

  -- present(ctx, user, result, pres, targets)
  present = function(ctx, user, res, pres, targets)
    if not res or not res.ok or not res.target then return end
    local t = res.target

    if pres and pres.lunge then pres.lunge(user, t, 0.10, 22) end
    if pres and pres.hit_flash then pres.hit_flash(t, 0.12) end
    if pres and pres.recoil then pres.recoil(t, 0.10, 10) end

    t._hurt_t = 0.18
  end
}

-- ------------------------------------------------------------
-- FIREBALL (AOE all enemies)
-- ------------------------------------------------------------
M.fireball = {
  id = "fireball",
  name = "Fireball",
  target_mode = "enemy_all",

  resolve = function(ctx, user, targets)
    if not user then
      return { ok=false, reason="no_user" }
    end

    local hits = {}

    for i = 1, #targets do
      local t = canon_target(ctx, targets[i])
      if t and t.hp and t.hp > 0 then
        t.max_hp = t.max_hp or t.hp_max or t.hp

        -- AOE is weaker than single-target; magic model uses user.int vs target.mdef/def
        local base = combat.damage_magic(user, t, 0)
        local dmg  = math.max(1, math.floor(base * 0.65))

        combat.apply_damage(t, dmg)
        t.hp = clamp(t.hp, 0, t.max_hp)

        hits[#hits + 1] = { target=t, dmg=dmg }
      end
    end

    return { ok=true, kind="aoe", hits=hits }
  end,

  present = function(ctx, user, res, pres, targets)
    if not res or not res.ok or not res.hits then return end

    -- Make it feel "simultaneous": trigger all effects in one frame
    for i = 1, #res.hits do
      local t = res.hits[i].target
      if t then
        if pres and pres.hit_flash then pres.hit_flash(t, 0.16) end
        if pres and pres.recoil then pres.recoil(t, 0.10, 10) end
        t._hurt_t = 0.22
      end
    end
  end
}

-- ------------------------------------------------------------
-- ITEMS (placeholder)
-- ------------------------------------------------------------
M.items = {
  id = "items",
  name = "Items",
  target_mode = "none",

  resolve = function(ctx, user, targets)
    return { ok=true, noop=true }
  end
}

M.list  = { M.attack, M.fireball, M.items }
M.by_id = { attack = M.attack, fireball = M.fireball, items = M.items }

return M
