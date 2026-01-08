-- /rd/battle/battle.lua

local turn      = require("battle.turn_system")
local targeting = require("battle.targeting")
local pres      = require("battle.presentation")
local fx        = require("battle.fx")
local status    = require("battle.status")
local ui        = require("battle.ui")
local hpbar     = require("battle.hpbar")
local skills    = require("data.skills")
local encounter = require("data.encounters.test_slimes")

local M = {}
local INSTANCE_ID = math.random(1000, 9999)

local assets = require("assets")
local stamp = assets.sprite("/rd/plasma.png")
sprite.draw(stamp, 30, 30, 12, 12, 0)

local ctx = {
  state   = "command",
  actors  = {},
  order   = {},
  turn_i  = 1,
  active  = nil,
  pending = nil,
  anim_t  = 0,
}

local function alive(a)
  return a and a.hp and a.hp > 0
end

local function get_attack_skill()
  if skills and skills.by_id and skills.by_id["attack"] then
    return skills.by_id["attack"]
  end
  if skills and skills.attack then
    return skills.attack
  end
  if skills and skills.list and #skills.list > 0 then
    return skills.list[1]
  end
  return nil
end

local function rebuild_order()
  ctx.order = turn.build_order(ctx.actors)
  ctx.turn_i = 1
  ctx.active = ctx.order[1]
  if ctx.active then
    turn.start_turn(ctx, ctx.active)
  end
end

local function ensure_active_alive_or_advance()
  if alive(ctx.active) then return true end
  ctx.state = "turn_advance"
  return false
end

function M.start()
  ctx.actors = encounter.spawn()

  for _, a in ipairs(ctx.actors) do
    if a.hp == nil then
      if a.hp_cur ~= nil then a.hp = a.hp_cur
      elseif a.hp_max ~= nil then a.hp = a.hp_max
      elseif a.stats and a.stats.hp ~= nil then a.hp = a.stats.hp
      else a.hp = 10 end
    end
    a.max_hp = a.max_hp or a.hp_max or (a.stats and (a.stats.max_hp or a.stats.hp_max)) or a.hp
  end

  rebuild_order()
  ui.reset()
  ctx.pending = nil
  ctx.state = "command"
  ctx.anim_t = 0
end

M.start()

function M.update(dt)
  pres.update(dt)
  fx.update(dt)

  for _, a in ipairs(ctx.actors) do
    if a._hurt_t and a._hurt_t > 0 then
      a._hurt_t = a._hurt_t - dt
    end
  end

  if targeting.count_alive_team(ctx, "enemy") == 0 then
    ctx.state = "win"
  elseif targeting.count_alive_team(ctx, "player") == 0 then
    ctx.state = "lose"
  end

  if ctx.state == "command" then
    if not ensure_active_alive_or_advance() then return end

    if ctx.active.team == "player" then
      local action = ui.update(dt, ctx, ctx.active)
      if action then
        ctx.pending = action
        local sk = ctx.pending.skill
        local mode = (sk and sk.target_mode) or "none"

        -- ✅ IMPORTANT:
        -- Do NOT skip when targets is empty (AOE uses {} on purpose).
        -- Only skip for "none".
        if (not sk) or (mode == "none") then
          ctx.state = "turn_advance"
        else
          ctx.state = "resolve"
        end
      end

    else
      local sk = get_attack_skill()
      if not sk then
        ctx.state = "turn_advance"
        return
      end

      local mode = sk.target_mode or "enemy_single"
      local targets = targeting.select(ctx, ctx.active, mode, 1)
      ctx.pending = { skill = sk, targets = targets, cursor_index = 1 }

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

    -- If UI didn't provide targets (AOE), select them now using target_mode
    if (#targets == 0) and sk.target_mode and (sk.target_mode ~= "self") then
      local idx = ctx.pending.cursor_index or 1
      targets = targeting.select(ctx, ctx.active, sk.target_mode, idx)
      ctx.pending.targets = targets
    end

    if (sk.target_mode ~= "self") and (#targets == 0) then
      ctx.pending = nil
      ui.reset()
      ctx.state = "command"
      return
    end

    local result = (sk.resolve and sk.resolve(ctx, ctx.active, targets)) or { ok=false }

    -- ✅ Correct signature:
    -- present(ctx, user, result, pres, targets)
    if sk.present then
      sk.present(ctx, ctx.active, result, pres, targets)
    end

    ctx.anim_t = 0
    ctx.state = "animate"

  elseif ctx.state == "animate" then
    ctx.anim_t = (ctx.anim_t or 0) + dt
    if ctx.anim_t >= 0.20 and pres.done() then
      ctx.state = "turn_advance"
    end

  elseif ctx.state == "turn_advance" then
    -- End-of-turn status bookkeeping for the actor that just acted
    if ctx.active then
      status.on_turn_end(ctx, ctx.active)
      status.tick_turns(ctx, ctx.active)
    end
    ctx.active = turn.advance(ctx)
    ui.reset()
    ctx.pending = nil
    ctx.state = "command"
  end
end

function M.draw()
  for _, a in ipairs(ctx.actors) do
    if a and a.hp and a.hp > 0 then
      local ox, oy = pres.offset(a)

      local rot = 0
      if a.team == "enemy" then
        rot = math.sin((ctx.anim_t or 0) * 6.0) * 0.10
      end

      sprite.draw(a.spr, a.x + ox, a.y + oy, a.w, a.h, rot,
        (a.team == "enemy") and 2 or 0)
    end

    if a._hurt_t and a._hurt_t > 0 then
      sprite.draw(assets.sprite("/rd/plasma.png"),
        a.x, a.y - (a.h * 0.9),
        12, 12, 0, 0)
    end
  end

  -- Bars under UI
  hpbar.draw_all(ctx.actors, pres)

  -- Anything in pres.draw() (selection orb / markers / etc) should happen BEFORE fx
  pres.draw()

  -- UI menus/cursor before fx (unless you want numbers over menus too)
  ui.draw(ctx, pres)

  -- Floating numbers ALWAYS last
  fx.draw(pres)
end


return M
