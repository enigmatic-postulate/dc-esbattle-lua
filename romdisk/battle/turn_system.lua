local M = {}

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

local function tick_status(actor)
  if not actor.status then return end
  local i = 1
  while i <= #actor.status do
    local s = actor.status[i]
    s.turns = (s.turns or 0) - 1
    if s.turns <= 0 then
      actor.status[i] = actor.status[#actor.status]
      actor.status[#actor.status] = nil
    else
      i = i + 1
    end
  end
end

function M.start_turn(ctx, actor)
  -- decrement timed statuses at start of actor's turn
  tick_status(actor)
end

function M.advance(ctx)
  if not ctx.order or #ctx.order == 0 then return nil end

  local tries = 0
  while tries < #ctx.order do
    ctx.turn_i = ctx.turn_i + 1
    if ctx.turn_i > #ctx.order then ctx.turn_i = 1 end

    local a = ctx.order[ctx.turn_i]
    if alive(a) then
      M.start_turn(ctx, a)
      return a
    end
    tries = tries + 1
  end

  return nil
end

return M
