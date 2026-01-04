local assets = require("assets")
local M = {}

function M.spawn()
  return {
    -- Player
    {
      id="hero",
      team="player",
      hp=20, hp_max=20,
      stats={str=6, def=2, agi=99},
      status={},
      x=180, y=240, w=64, h=64,
      spr=assets.sprite("/rd/es_sprite_64.png")
    },

    -- Enemies (3)
    {
      id="slime_a",
      team="enemy",
      hp=12, hp_max=12,
      stats={str=4, def=1, agi=1},
      status={},
      x=460, y=160, w=64, h=64,
      spr=assets.sprite("/rd/es_sprite_64.png")
    },
    {
      id="slime_b",
      team="enemy",
      hp=12, hp_max=12,
      stats={str=4, def=1, agi=1},
      status={},
      x=460, y=240, w=64, h=64,
      spr=assets.sprite("/rd/es_sprite_64.png")
    },
    {
      id="slime_c",
      team="enemy",
      hp=12, hp_max=12,
      stats={str=4, def=1, agi=1},
      status={},
      x=460, y=320, w=64, h=64,
      spr=assets.sprite("/rd/es_sprite_64.png")
    },
  }
end

return M
