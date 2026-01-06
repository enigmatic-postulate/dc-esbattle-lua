-- /rd/battle/ui.lua
-- Mario RPG-like flow (no text):
-- HOME: Attack / Fireball / Items
-- TARGET: pick a single enemy (Attack only)
-- Fireball is AOE: confirms immediately and battle.lua selects enemy_all targets.

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

local ui_state = "home"   -- "home" | "target"
local home_i = 1          -- 1=Attack, 2=Fireball, 3=Items
local target_i = 1
local target_entry_lock = 0

local idle = { fire=false, left=false, right=false, b=nil, y=nil }
local lat  = { fire=false, left=false, right=false, b=false, y=false }

-- Defensive registry
if not skills.list then
  skills.list = {}
  if skills.attack   then skills.list[#skills.list+1] = skills.attack end
  if skills.fireball then skills.list[#skills.list+1] = skills.fireball end
  if skills.items    then skills.list[#skills.list+1] = skills.items end
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
  return (skills.by_id and skills.by_id["attack"]) or (skills.list and skills.list[1])
end

local function fireball_skill()
  return skills.by_id and skills.by_id["fireball"]
end

function M.update(dt, ctx, user)
  cursor.bob = cursor.bob + dt
  probe.update()
  target_entry_lock = math.max(0, target_entry_lock - dt)

  -- Only accept input on player turn
  if not ctx.active or ctx.active.team ~= "player" then
    A_click(); L_click(); R_click(); B_click(); Y_click()
    return nil
  end

  -- Cancel
  if B_click() and ui_state == "target" then
    ui_state = "home"
    target_entry_lock = 0
    return nil
  end

  if ui_state == "home" then
    if L_click() then
      home_i = home_i - 1
      if home_i < 1 then home_i = 3 end
    elseif R_click() then
      home_i = home_i + 1
      if home_i > 3 then home_i = 1 end
    end

    if Y_click() then
      home_i = 3
      return nil
    end

    if A_click() then
      if home_i == 1 then
        ui_state = "target"
        target_i = 1
        target_entry_lock = TARGET_ENTRY_LOCK_S
        return nil

      elseif home_i == 2 then
        local sk = fireball_skill()
        if not sk then return nil end
        ui_state = "home"
        -- AOE: targets intentionally empty; battle.lua will select enemy_all
        return { skill = sk, targets = {}, cursor_index = 1 }

      else
        -- Items placeholder
        return nil
      end
    end

  elseif ui_state == "target" then
    local sk = attack_skill()
    if not sk then ui_state = "home"; return nil end

    local candidates = targeting.alive_enemies(ctx, user)
    if #candidates == 0 then ui_state = "home"; return nil end

    if target_i < 1 then target_i = 1 end
    if target_i > #candidates then target_i = #candidates end

    if L_click() then
      target_i = target_i - 1
      if target_i < 1 then target_i = #candidates end
    elseif R_click() then
      target_i = target_i + 1
      if target_i > #candidates then target_i = 1 end
    end

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

  -- keep overlay
  probe.draw()

  local function off(a)
    if pres then return pres.offset(a) end
    return 0, 0
  end

  local bob = math.sin(cursor.bob * 7.0) * 4.0

  if ctx.active.team ~= "player" then
    local ox, oy = off(ctx.active)
    sprite.draw(cursor.spr,
      ctx.active.x + ox,
      ctx.active.y + oy - (ctx.active.h * 0.75) + bob,
      cursor.base_w * 1.4, cursor.base_h * 1.4, 0)
    return
  end

  if ui_state == "home" then
    -- Three buttons above player
    local ox, oy = off(ctx.active)
    local base_x = ctx.active.x + ox
    local base_y = ctx.active.y + oy - (ctx.active.h * 0.75) + bob

    local spacing = 28
    for i = 1, 3 do
      local sel = (i == home_i)
      local s = sel and 1.55 or 1.0
      local px = base_x + (i - 2.0) * spacing
      sprite.draw(cursor.spr, px, base_y, cursor.base_w * s, cursor.base_h * s, 0)
    end

    -- Fireball preview: cursor over ALL enemies
    if home_i == 2 and fireball_skill() then
      local enemies = targeting.alive_enemies(ctx, ctx.active)
      for _, e in ipairs(enemies) do
        local ex, ey = off(e)
        sprite.draw(cursor.spr,
          e.x + ex,
          e.y + ey - (e.h * 0.75) + bob,
          cursor.base_w * 1.25, cursor.base_h * 1.25, 0)
      end
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
