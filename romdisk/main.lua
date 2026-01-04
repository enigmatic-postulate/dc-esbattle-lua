local W, H = 640, 480

-- Ship state
local ship = {
  x = 320, y = 240,
  angle = 0,
  w = 128, h = 128,
  move_speed = 160,
  rot_speed  = 2.5,
  spr = sprite.load("/rd/es_sprite_64.png"),
}

-- Plasma orb sprite (draw scaled up if you want)
local orb = {
  spr = sprite.load("/rd/plasma.png"),
  w = 16, h = 16,
  speed = 280
}

-- Bullet list
local bullets = {}

-- simple fire edge detect + cooldown
local fire_prev = false
local fire_cd = 0

local function forward_vec(angle)
  -- y-down coordinate system, ship faces up at angle=0
  return math.sin(angle), -math.cos(angle)
end

local function spawn_bullet()
  local dx, dy = forward_vec(ship.angle)

  -- tip of the ship: half the ship height forward from center
  local tip_dist = ship.h * 0.5
  local bx = ship.x + dx * tip_dist
  local by = ship.y + dy * tip_dist

  bullets[#bullets + 1] = {
    x = bx, y = by,
    vx = dx * orb.speed,
    vy = dy * orb.speed,
    w = orb.w, h = orb.h,
    angle = ship.angle
  }
end

local function bullet_offscreen(b)
  -- treat b.x/b.y as center like your sprite draw does
  local left   = b.x - b.w * 0.5
  local right  = b.x + b.w * 0.5
  local top    = b.y - b.h * 0.5
  local bottom = b.y + b.h * 0.5

  return (right < 0) or (left > W) or (bottom < 0) or (top > H)
end

function update(dt)
  -- rotate ship
  if input.down("left")  then ship.angle = ship.angle - ship.rot_speed * dt end
  if input.down("right") then ship.angle = ship.angle + ship.rot_speed * dt end

  -- move ship forward/back
  local forward = 0
  if input.down("up")   then forward = forward - 1 end
  if input.down("down") then forward = forward + 1 end

  local dx, dy = forward_vec(ship.angle)
  ship.x = ship.x + dx * ship.move_speed * forward * dt
  ship.y = ship.y + dy * ship.move_speed * forward * dt

  -- fire
  fire_cd = math.max(0, fire_cd - dt)
  local fire_now = input.down("fire")
  if fire_now and (not fire_prev) and fire_cd == 0 then
    spawn_bullet()
    fire_cd = 0.12
  end
  fire_prev = fire_now

  -- update bullets + remove offscreen
  local i = 1
  while i <= #bullets do
    local b = bullets[i]
    b.x = b.x + b.vx * dt
    b.y = b.y + b.vy * dt

    if bullet_offscreen(b) then
      bullets[i] = bullets[#bullets]
      bullets[#bullets] = nil
    else
      i = i + 1
    end
  end
end

function draw()
  -- ship
  sprite.draw(ship.spr, ship.x, ship.y, ship.w, ship.h, ship.angle)

  -- bullets
  for i = 1, #bullets do
    local b = bullets[i]
    sprite.draw(orb.spr, b.x, b.y, orb.w, orb.h, b.angle)
  end
end
