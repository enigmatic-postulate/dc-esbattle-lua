-- /rd/assets.lua
local M = {}
local spr_cache = {}

function M.sprite(path)
  local s = spr_cache[path]
  if s then return s end

  s = sprite.load(path)
  if not s then
    dbg.print("sprite.load FAILED: " .. tostring(path))
    return nil
  end

  spr_cache[path] = s
  return s
end

return M
