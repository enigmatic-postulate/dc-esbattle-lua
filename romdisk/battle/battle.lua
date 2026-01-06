-- /rd/battle/battle.lua
-- Battle orchestrator (state machine) with:
-- - UI-driven player actions (Mario-RPG flow handled in ui.lua)
-- - Enemy AI "Attack"
-- - Skill hooks: skill.resolve + skill.present
-- - Presentation queue (pres.*)
-- - Skips dead actors on turn advance
-- - Win/Lose end states
-- - Animate dwell so results are visible

local turn      = require("battle.turn_system")
local targeting = require("battle.targeting")
local pres      = require("battle.presentation")
local ui        = require("battle.ui")
local hpbar     = require("battle.hpbar")
local skills    = require("data.skills")
local encounter = require("data.encounters.test_slimes")
local assets    = require("assets")

local sprite = sprite

local M = {}
local ctx = {
  actors = {},
  order = {},
  turn_i = 1,
  active = nil,
  state = "init",
  pending = nil,
  anim_t = 0,
}

local function alive(a)
  return a and a.hp and a.hp > 0
end

local function rebuild_order()
  ctx.order = turn.build_order(ctx.actors)
  ctx.turn_i = 1
  ctx.active = ctx.order[1]
  if ctx.active then
    ctx.active._turn_started = true
  end
end

local function next_alive_in_order()
  local tries = 0
  while tries < (#ctx.order + 1) do
    ctx.turn_i = ctx.turn_i + 1
    if ctx.turn_i > #ctx.order then
      -- rebuild each round to respect deaths/status
      rebuild_order()
    else
      ctx.active = ctx.order[ctx.turn_i]
    end
    if alive(ctx.active) then
      ctx.active._turn_started = true
      return true
    end
    tries = tries + 1
  end
  return false
end

local function ensure_active_alive_or_advance()
  if alive(ctx.active) then return true end
  ctx.state = "turn_advance"
  return false
end

function M.start()
  ctx.actors = encounter.spawn()

  for _, a in ipairs(ctx.actors) do
      -- Canonicalize hp
      if a.hp == nil then
        if a.hp_cur ~= nil then a.hp = a.hp_cur
        elseif a.hp_max ~= nil then a.hp = a.hp_max
        elseif a.stats and a.stats.hp ~= nil then a.hp = a.stats.hp
        end
      end
    end


  -- Ensure max_hp exists so hp bars shrink correctly
  for _, a in ipairs(ctx.actors) do
    if a and a.hp and not a.max_hp then a.max_hp = a.hp end
  end

  rebuild_order()
  ui.reset()
  ctx.pending = nil
  ctx.state = "command"
  ctx.anim_t = 0
end

-- Auto-start
M.start()

function M.update(dt)
  pres.update(dt)
  for _, a in ipairs(ctx.actors) do
      if a._hurt_t and a._hurt_t > 0 then
        a._hurt_t = a._hurt_t - dt
      end
    end

  -- Win/Lose checks
  if targeting.count_alive_team(ctx, "enemy") == 0 then
    ctx.state = "win"
  elseif targeting.count_alive_team(ctx, "player") == 0 then
    ctx.state = "lose"
  end

  if ctx.state == "command" then
    if not ensure_active_alive_or_advance() then return end

    -- If enemy turn, auto-attack
    if ctx.active.team ~= "player" then
      local sk = skills.by_id and skills.by_id["attack"] or (skills.list and skills.list[1])
      if sk then
        local targets = targeting.select(ctx, ctx.active, sk.target_mode or "enemy_single", 1)
        ctx.pending = { skill = sk, targets = targets, cursor_index = 1 }
        ctx.state = "resolve"
      else
        ctx.state = "turn_advance"
      end
      return
    end

    -- Player UI: returns {skill, targets, cursor_index} or nil
    local pending = ui.update(dt, ctx, ctx.active)
    if pending then
      ctx.pending = pending
      local mode = (pending.skill and pending.skill.target_mode) or "none"
      local targets = pending.targets or {}

      if (mode ~= "self" and #targets == 0) then
        ctx.state = "turn_advance"
      else
        ctx.state = "resolve"
      end
    end

   elseif ctx.state == "resolve" then
    if not ctx.pending or not ctx.pending.skill then
      ctx.state = "turn_advance"
      return
    end

    local sk = ctx.pending.skill
    local targets = ctx.pending.targets or {}

    -- ✅ CRITICAL FIX:
    -- If UI didn’t provide targets (or provided an empty list),
    -- re-select them *now* using cursor_index.
    if (#targets == 0) and sk.target_mode and (sk.target_mode ~= "self") then
      local idx = ctx.pending.cursor_index or 1
      targets = targeting.select(ctx, ctx.active, sk.target_mode, idx)
      ctx.pending.targets = targets
    end

    -- Execute skill gameplay
    local result = (sk.resolve and sk.resolve(ctx, ctx.active, targets)) or { ok=false }

    -- Execute skill presentation
    -- Standard signature: present(ctx, user, result, pres, targets)
    if sk.present then
      sk.present(ctx, ctx.active, result, pres, targets)
    end

    ctx.anim_t = 0
    ctx.state = "animate"
    --if targets[1] then targets[1].x = targets[1].x + 3 end

  elseif ctx.state == "animate" then
    ctx.anim_t = ctx.anim_t + dt
    -- wait until presentation queue is done, plus minimum dwell
    if pres.done() and ctx.anim_t > 0.20 then
      ctx.state = "turn_advance"
    end

  elseif ctx.state == "turn_advance" then
    ctx.pending = nil
    ui.reset()
    next_alive_in_order()
    ctx.state = "command"

  elseif ctx.state == "win" or ctx.state == "lose" then
    -- Placeholder end states
    pres.update(dt)
  end
end

function M.draw()
  -- Draw actors with presentation offsets applied
  for _, a in ipairs(ctx.actors) do
    if a and a.hp and a.hp > 0 then
      local ox, oy = pres.offset(a)
      sprite.draw(a.spr, a.x + ox, a.y + oy, a.w, a.h, 0)
    end
      -- draw a small red-ish flash marker using plasma sprite whenever hurt timer is active
    if a._hurt_t and a._hurt_t > 0 then
      sprite.draw(assets.sprite("/rd/plasma.png"), a.x, a.y - (a.h * 0.9), 12, 12, 0)
    end


  end




  hpbar.draw_all(ctx.actors, pres)
  ui.draw(ctx, pres)
  pres.draw()
end

return M
