-- /rd/data/skills.lua
-- Skill definitions used by battle/ui.
-- IMPORTANT:
--   * Always mutate the canonical actor tables in ctx.actors.
--   * Do not rely on copies of targets coming from UI/targeting.

local combat = require("battle.combat")

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

local function canon_target(ctx, t)
  if not t then return nil end
  if t.id then
    return find_actor_by_id(ctx, t.id) or t
  end
  return t
end

-- ------------------------------------------------------------
-- ATTACK
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

    -- Ensure max hp exists for consistent bar scaling
    t.hp_max = t.hp_max or t.max_hp or t.hp

    local before = t.hp
    local dmg = combat.damage_physical(user, t, 0)
    combat.apply_damage(t, dmg)

    -- Clamp to known max if we have it (defensive)
    if t.hp_max then
      t.hp = clamp(t.hp, 0, t.hp_max)
    end

    return {
      ok=true,
      dmg=dmg,
      target=t,
      hp_before=before,
      hp_after=t.hp,
    }
  end,

  -- Standard signature: present(ctx, user, result, pres, targets)
  present = function(ctx, user, res, pres, targets)
    if not res or not res.ok then return end
    local t = res.target
    if not t then return end

    -- simple presentation effects
    if pres and pres.lunge then pres.lunge(user, t, 0.10, 22) end
    if pres and pres.hit_flash then pres.hit_flash(t, 0.12) end
    if pres and pres.recoil then pres.recoil(t, 0.10, 10) end

    -- debug marker already drawn by battle.lua (plasma icon)
    t._hurt_t = 0.18
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
  end,

  -- Standard signature: present(ctx, user, result, pres, targets)
  present = function(ctx, user, res, pres, targets)
    -- no-op for now
  end
}

M.list  = { M.attack, M.items }
M.by_id = { attack = M.attack, items = M.items }

return M
