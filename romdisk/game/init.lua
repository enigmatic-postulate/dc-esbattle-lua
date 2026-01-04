-- /rd/game/init.lua
local battle = require("battle.battle")

local M = {}

function M.update(dt)
  battle.update(dt)
end

function M.draw()
  battle.draw()
end

return M
