-- /rd/battle/input_probe.lua
-- Active-high input normalization:
-- numeric 1 = pressed, numeric 0 = released
-- (matches: game auto-acts when NOT holding X, but stops when holding X under active-low logic)

local M = {}

local spr = sprite.load("/rd/plasma.png")

local CAND = {
  { name="fire"  },
  { name="left"  },
  { name="right" },
  { name="up"    },
  { name="down"  },
  { name="start" },
}

local last_down = {}

local function raw(name)
  return input.down(name)
end

-- Active-high: numeric 1 means DOWN.
function M.down(name)
  local v = raw(name)

  if type(v) == "boolean" then
    return v
  end

  if type(v) == "number" then
    return v == 1
  end

  return false
end

function M.pressed(name)
  local now = M.down(name)
  local prev = last_down[name] or false
  last_down[name] = now
  return now and (not prev)
end

function M.reset_edges()
  last_down = {}
end

function M.update() end

function M.draw()
  if not spr then return end
  local x0, y0 = 40, 450
  local dx = 26

  for i, c in ipairs(CAND) do
    local is_down = M.down(c.name)
    local s = is_down and 1.8 or 1.0
    local x = x0 + (i - 1) * dx
    sprite.draw(spr, x, y0, 10 * s, 10 * s, 0)
  end

  -- marker above "fire" (first column)
  sprite.draw(spr, x0, y0 - 18, 18, 18, 0)
end

return M
