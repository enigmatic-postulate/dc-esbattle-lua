-- /rd/battle/input_util.lua
-- Robust input with alias sets + change logging.
-- We don't assume polarity. We detect "down" as a stable change from an idle baseline,
-- and we allow multiple candidate names per action (confirm/left/right).

local M = {}

-- Candidate bindings. Add/remove as needed.
local ALIASES = {
  confirm = { "fire", "a", "btn_a", "button_a", "A", "confirm" },
  left    = { "left", "dpad_left", "pad_left" },
  right   = { "right", "dpad_right", "pad_right" },
}

local idle_raw = {}     -- name -> first seen raw value
local last_down = {}    -- action -> bool
local chosen = {}       -- action -> name that actually changes

local function raw(name)
  return input.down(name)
end

local function down_from_idle(name)
  local v = raw(name)
  if type(v) == "boolean" then return v end

  if idle_raw[name] == nil then
    idle_raw[name] = v
  end

  -- Treat "changed from baseline" as down.
  return v ~= idle_raw[name]
end

-- Pick the first alias that ever shows a meaningful change from idle
local function pick_binding(action)
  if chosen[action] then return chosen[action] end
  local list = ALIASES[action] or {}
  for _, name in ipairs(list) do
    local _ = down_from_idle(name) -- initializes idle baseline
  end
  return nil
end

function M.down(action)
  pick_binding(action)

  -- If we already chose a binding, use it
  if chosen[action] then
    return down_from_idle(chosen[action])
  end

  -- Otherwise, scan: whichever alias currently differs from its idle becomes the chosen one
  local list = ALIASES[action] or {}
  for _, name in ipairs(list) do
    if down_from_idle(name) then
      chosen[action] = name
      dbg.print(("Input binding chosen: %s -> %s"):format(action, name))
      return true
    end
  end

  return false
end

function M.pressed(action)
  local now = M.down(action)
  local prev = last_down[action] or false
  last_down[action] = now
  return now and (not prev)
end

function M.reset_edges()
  last_down = {}
end

-- Print raw values periodically so we can see what changes
local t_acc = 0
function M.debug_tick(dt)
  t_acc = t_acc + dt
  if t_acc < 0.5 then return end
  t_acc = 0

  local function dump(action)
    local list = ALIASES[action] or {}
    for _, name in ipairs(list) do
      local v = raw(name)
      dbg.print(("raw %-7s %-12s = %s  idle=%s  down=%s"):format(
        action, name, tostring(v), tostring(idle_raw[name]), tostring(down_from_idle(name))
      ))
    end
  end

  dump("confirm")
  dump("left")
  dump("right")
end

return M
