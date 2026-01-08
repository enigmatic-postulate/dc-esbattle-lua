local M = {}

local status = require("battle.status")

local function alive(a) return a and a.hp and a.hp > 0 end

function M.build_order(actors)
  local list = {}
  for _, a in ipairs(actors) do
    list[#list+1] = a
  end

  table.sort(list, function(a, b)
    return (a.stats.agi or 0) > (b.stats.agi or 0)
  end)

  return list
end

function M.start_turn(ctx, actor)
  -- Status hooks (DOT, skip-turn, etc.)
  local r = status.on_turn_start(ctx, actor)
  return (r and r.can_act) or false
end

function M.advance(ctx)
  if not ctx.order or #ctx.order == 0 then return nil end

  local tries = 0
  while tries < #ctx.order do
    ctx.turn_i = ctx.turn_i + 1
    if ctx.turn_i > #ctx.order then ctx.turn_i = 1 end

    local a = ctx.order[ctx.turn_i]
    if alive(a) then
      local can_act = M.start_turn(ctx, a)
      if can_act then
        return a
      else
        -- Skipped/invalid turn still consumes duration
        status.on_turn_end(ctx, a)
        status.tick_turns(ctx, a)
      end
    end
    tries = tries + 1
  end

  return nil
end

return M
