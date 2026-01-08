-- /rd/battle/status.lua
-- Status effect framework (turn-based).
--
-- Goals:
--  * simple data model: actor.status = { {id=..., turns=..., ...}, ... }
--  * hooks: on_apply, on_turn_start, on_turn_end, on_expire
--  * utility: has/add/remove/tick

local combat = require("battle.combat")
local fx     = require("battle.fx")

local M = {}

-- Status definitions ----------------------------------------------------------

M.defs = {
  burn = {
    name = "Burn",
    on_apply = function(ctx, actor, st)
      fx.float_status(actor, "BURN")
    end,
    -- Deal DOT at start of victim's turn
    on_turn_start = function(ctx, actor, st)
      if not actor or not actor.hp or actor.hp <= 0 then return end
      local dmg = st.dot or 2
      combat.apply_damage(actor, dmg)
      fx.float_damage(actor, dmg, { color = 0xFFFFA040 })
      actor._hurt_t = math.max(actor._hurt_t or 0, 0.18)
    end,
  },

  guard = {
    name = "Guard",
    on_apply = function(ctx, actor, st)
      fx.float_status(actor, "GUARD")
    end,
  },

  stun = {
    name = "Stun",
    on_apply = function(ctx, actor, st)
      fx.float_status(actor, "STUN")
    end,
    -- If stunned at start of your turn: skip the action (consume 1 turn)
    skip_turn = true,
  },
}

-- Helpers -------------------------------------------------------------------

local function alive(a) return a and a.hp and a.hp > 0 end

function M.has(actor, id)
  if not actor or not actor.status then return false end
  for i = 1, #actor.status do
    if actor.status[i] and actor.status[i].id == id then return true end
  end
  return false
end

function M.get(actor, id)
  if not actor or not actor.status then return nil end
  for i = 1, #actor.status do
    local s = actor.status[i]
    if s and s.id == id then return s end
  end
  return nil
end

function M.add(ctx, actor, id, turns, extra)
  if not actor or not id then return end
  actor.status = actor.status or {}

  local st = { id = id, turns = turns or 1 }
  if extra then
    for k, v in pairs(extra) do st[k] = v end
  end
  actor.status[#actor.status + 1] = st

  local def = M.defs[id]
  if def and def.on_apply then def.on_apply(ctx, actor, st) end
end

function M.remove(actor, id)
  if not actor or not actor.status then return end
  local i = 1
  while i <= #actor.status do
    if actor.status[i] and actor.status[i].id == id then
      actor.status[i] = actor.status[#actor.status]
      actor.status[#actor.status] = nil
    else
      i = i + 1
    end
  end
end

function M.tick_turns(ctx, actor)
  if not actor or not actor.status then return end
  local i = 1
  while i <= #actor.status do
    local st = actor.status[i]
    st.turns = (st.turns or 0) - 1
    if st.turns <= 0 then
      local def = M.defs[st.id]
      if def and def.on_expire then def.on_expire(ctx, actor, st) end
      actor.status[i] = actor.status[#actor.status]
      actor.status[#actor.status] = nil
    else
      i = i + 1
    end
  end
end

function M.on_turn_start(ctx, actor)
  if not alive(actor) then return { can_act = false } end

  -- Run start-of-turn hooks
  if actor.status then
    for i = 1, #actor.status do
      local st = actor.status[i]
      local def = st and M.defs[st.id]
      if def and def.on_turn_start then
        def.on_turn_start(ctx, actor, st)
      end
    end
  end

  -- If DOT killed the actor, they can't act
  if not alive(actor) then return { can_act = false } end

  -- Stun check (skip turn). We skip if any status definition sets skip_turn=true.
  if actor.status then
    for i = 1, #actor.status do
      local st = actor.status[i]
      local def = st and M.defs[st.id]
      if def and def.skip_turn then
        fx.float_text_xy(actor.x, actor.y - ((actor.h or 32) * 1.15), "SKIP", { color = 0xFFFFFFFF })
        return { can_act = false, skipped = true }
      end
    end
  end

  return { can_act = true }
end

function M.on_turn_end(ctx, actor)
  if not actor or not actor.status then return end
  for i = 1, #actor.status do
    local st = actor.status[i]
    local def = st and M.defs[st.id]
    if def and def.on_turn_end then
      def.on_turn_end(ctx, actor, st)
    end
  end
end

return M
