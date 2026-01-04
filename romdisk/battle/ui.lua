-- /rd/battle/ui.lua
-- Mario RPG-like flow (no text):
-- 1) HOME: select Attack/Items, press A to enter target mode
-- 2) TARGET: use Left/Right to pick enemy
-- 3) Press A to confirm and perform
-- B cancels (optional), Y selects Items (optional placeholder)
--
-- IMPORTANT: Uses baseline-delta input:
-- active(name) = probe.down(name) ~= idle[name]
-- This fixes inverted/odd backends and prevents latches getting stuck.

local targeting = require("battle.targeting")
local skills    = require("data.skills")
local probe     = require("battle.input_probe")

local M = {}

local TARGET_ENTRY_LOCK_S = 0.25

local cursor = {
  spr = sprite.load("/rd/plasma.png"),
  base_w = 18,
  base_h = 18,
  bob = 0
}

-- UI phases
local ui_state = "home"   -- "home" | "target"
local home_i = 1          -- 1=Attack, 2=Items (placeholder)
local target_i = 1
local target_entry_lock = 0

-- Idle baselines (sampled on reset)
local idle = {
  fire  = false,
  left  = false,
  right = false,
  b     = nil,
  y     = nil,
}

-- Latches (one click per press)
local lat = { fire=false, left=false, right=false, b=false, y=false }

-- Defensive skills module
if not skills.list then
  skills.list = {}
  if skills.attack then skills.list[#skills.list+1] = skills.attack end
  if skills.guard  then skills.list[#skills.list+1] = skills.guard  end
end
if not skills.by_id then
  skills.by_id = {}
  for _, sk in ipairs(skills.list) do
    if sk and sk.id then skills.by_id[sk.id] = sk end
  end
end

local function sample_idle()
  probe.update()

  idle.fire  = probe.down("fire")
  idle.left  = probe.down("left")
  idle.right = probe.down("right")

  -- Optional: only sample if the probe actually supports these names
  local vb = probe.down("b")
  if vb ~= nil then idle.b = vb end

  local vy = probe.down("y")
  if vy ~= nil then idle.y = vy end
end

local function active(name)
  local v = probe.down(name)
  local b = idle[name]
  if b == nil then return false end
  return v ~= b
end

local function click(name)
  local d = active(name)
  if not d then
    lat[name] = false
    return false
  end
  if lat[name] then return false end
  lat[name] = true
  return true
end

local function A_click() return click("fire") end
local function L_click() return click("left") end
local function R_click() return click("right") end
local function B_click() return click("b") end
local function Y_click() return click("y") end

function M.reset()
  ui_state = "home"
  home_i = 1
  target_i = 1
  target_entry_lock = 0

  lat.fire, lat.left, lat.right, lat.b, lat.y = false, false, false, false, false

  sample_idle()

  if probe.reset_edges then probe.reset_edges() end
end

local function attack_skill()
  return (skills.by_id and skills.by_id["attack"]) or skills.list[1]
end

function M.update(dt, ctx, user)
  cursor.bob = cursor.bob + dt
  probe.update()
  target_entry_lock = math.max(0, target_entry_lock - dt)

  -- Only accept input on player turn
  if not ctx.active or ctx.active.team ~= "player" then
    -- allow latches to release
    A_click(); L_click(); R_click(); B_click(); Y_click()
    return nil
  end

  -- Cancel
  if B_click() then
    if ui_state == "target" then
      ui_state = "home"
      target_entry_lock = 0
      return nil
    end
  end

  if ui_state == "home" then
    -- Home selection: Attack/Items
    if L_click() then
      home_i = home_i - 1
      if home_i < 1 then home_i = 2 end
    elseif R_click() then
      home_i = home_i + 1
      if home_i > 2 then home_i = 1 end
    end

    -- Optional: Y selects Items (placeholder)
    if Y_click() then
      home_i = 2
      return nil
    end

    -- A confirms choice
    if A_click() then
      if home_i == 1 then
        ui_state = "target"
        target_i = 1
        target_entry_lock = TARGET_ENTRY_LOCK_S
        return nil
      else
        -- Items placeholder
        return nil
      end
    end

  elseif ui_state == "target" then
    local sk = attack_skill()
    if not sk then
      ui_state = "home"
      return nil
    end

    local candidates = targeting.alive_enemies(ctx, user)
    if #candidates == 0 then
      ui_state = "home"
      return nil
    end

    if target_i < 1 then target_i = 1 end
    if target_i > #candidates then target_i = #candidates end

    -- Move target selection
    if L_click() then
      target_i = target_i - 1
      if target_i < 1 then target_i = #candidates end
    elseif R_click() then
      target_i = target_i + 1
      if target_i > #candidates then target_i = 1 end
    end

    -- Confirm target (A), after lockout
    if target_entry_lock == 0 and A_click() then
      local targets = targeting.select(ctx, user, sk.target_mode or "enemy_single", target_i)
      ui_state = "home"
      return { skill = sk, targets = targets, cursor_index = target_i }
    end
  end

  return nil
end

function M.draw(ctx, pres)
  if not ctx.active or ctx.active.hp <= 0 then return end
  if not cursor.spr then return end

  -- bottom indicator overlay (keep while debugging)
  probe.draw()

  local function off(a)
    if pres then return pres.offset(a) end
    return 0, 0
  end

  local bob = math.sin(cursor.bob * 7.0) * 4.0

  -- Enemy turn marker
  if ctx.active.team ~= "player" then
    local ox, oy = off(ctx.active)
    sprite.draw(cursor.spr,
      ctx.active.x + ox,
      ctx.active.y + oy - (ctx.active.h * 0.75) + bob,
      cursor.base_w * 1.4, cursor.base_h * 1.4, 0)
    return
  end

  if ui_state == "home" then
    -- Two "buttons" above player: Attack/Items
    local ox, oy = off(ctx.active)
    local base_x = ctx.active.x + ox
    local base_y = ctx.active.y + oy - (ctx.active.h * 0.75) + bob

    local spacing = 28
    for i = 1, 2 do
      local sel = (i == home_i)
      local s = sel and 1.55 or 1.0
      local px = base_x + (i - 1.5) * spacing
      sprite.draw(cursor.spr, px, base_y, cursor.base_w * s, cursor.base_h * s, 0)
    end

  elseif ui_state == "target" then
    local candidates = targeting.alive_enemies(ctx, ctx.active)
    if #candidates == 0 then return end

    if target_i < 1 then target_i = 1 end
    if target_i > #candidates then target_i = #candidates end

    local t = candidates[target_i]
    local tox, toy = off(t)
    sprite.draw(cursor.spr,
      t.x + tox,
      t.y + toy - (t.h * 0.75) + bob,
      cursor.base_w * 1.6, cursor.base_h * 1.6, 0)
  end
end

return M
