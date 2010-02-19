function mkunit(f, r, num)
  u = unit.create(f, r)
  u.number = num
  u:add_item("money", num*10)
  u:clear_orders()
  return u
end

function test_movement()
  west = direction("west")
  east = direction("east")

  -- im westen ohne strassen
  ocean = region.create(-3, 0, "ocean")
  w2 = region.create(-2, 0, "plain")
  w1 = region.create(-1, 0, "plain")

  -- im osten mit strassen
  r0 = region.create(0, 0, "plain")
  r1 = region.create(1, 0, "desert")
  r2 = region.create(2, 0, "glacier")
  r3 = region.create(3, 0, "plain")
  r4 = region.create(4, 0, "glacier")

  r0:add_direction(r4, "Wirbel", "Nimm die Abk�rzung, Luke")

  r0:set_road(east, 1.0)
  r1:set_road(west, 1.0)
  r1:set_road(east, 1.0)
  r2:set_road(west, 1.0)
  r2:set_road(east, 1.0)
  r3:set_road(west, 1.0)
  r3:set_road(east, 1.0)
  r4:set_road(west, 1.0)

  orcs = faction.create("orcs@eressea.de", "orc", "de")
  orcs.age = 20

  aqua = faction.create("aqua@eressea.de", "aquarian", "de")
  aqua.age = 20

  bugs = faction.create("bugz@eressea.de", "insect", "de")
  bugs.age = 20

  orc = mkunit(orcs, r0, 10)
  orc:add_item("horse", orc.number*3)
  orc:set_skill("riding", 10)

  -- schiffe zum abtreiben:
  ships = {}
  for i = 1, 100 do
    ships[i] = add_ship(ocean, "boat")
  end

  astra = mkunit(orcs, r0, 1)
  astra:add_order("NACH Wirbel")
  astra:add_order("NUMMER EINHEIT astr")

  foot = mkunit(orcs, r0, 1)
  foot:add_order("ROUTE W W")
  foot:add_order("NUMMER EINHEIT foot")

  watch = mkunit(orcs, w2, 1)

  ship = add_ship(ocean, "boat")
  cptn = mkunit(aqua, ocean, 1)
  cptn.ship = ship
  cptn:add_order("NACH O")
  cptn:add_order("NUMMER EINHEIT cptn")
  cptn:add_order("BENENNE EINHEIT Landungsleiter")
  cptn:add_order("BENENNE PARTEI Meermenschen")

  swim = mkunit(aqua, ocean, 1)
  swim.ship = ship
  swim:add_order("NACH O")
  swim:add_order("NUMMER EINHEIT swim")
  swim:add_order("BENENNE EINHEIT Landungstruppe")

  -- ein schiff im landesinneren
  ship = add_ship(r0, "boat")
  sail = mkunit(aqua, r0, 1)
  sail.ship = ship

  crew = mkunit(aqua, r0, 1)
  crew.ship = ship

  bug = mkunit(bugs, r0, 1)

  crew:add_order("NACH O")
  crew:add_order("NUMMER EINHEIT crew")
  crew:add_order("BENENNE EINHEIT Aussteiger")
  crew:add_order("NUMMER PARTEI aqua")

  sail:add_order("NACH O")
  sail:add_order("NUMMER EINHEIT saiL")
  sail:add_order("BENENNE EINHEIT Aussteiger")

  orc:add_order("NUMMER PARTEI orcs")
  orc:add_order("NUMMER EINHEIT orc")
  orc:add_order("BENENNE EINHEIT Orks")
  orc:add_order("ROUTE O O O P P O W W W W")
  orc:add_order("GIB 0 ALLES Steine")
  orc:add_order("GIB 0 ALLES Holz")
  orc:add_order("TRANSPORTIEREN " .. itoa36(bug.id))

  bug:add_order("NUMMER PARTEI bugs")
  bug:add_order("NUMMER EINHEIT bug")
  bug:add_order("BENENNE EINHEIT K�fer")
  bug:add_order("GIB 0 ALLES Steine")
  bug:add_order("GIB 0 ALLES Holz")
  bug:add_order("FAHREN " .. itoa36(orc.id))

  u = unit.create(orcs, r0)
  u.number = 1
  u:add_item("horse", u.number*3)
  u:add_item("money", u.number*10)
  u:set_skill("riding", 10)
  u:set_skill("stealth", 2)
  u:clear_orders()
  u:add_order("FOLGEN EINHEIT " .. itoa36(bug.id))
  u:add_order("NACH W")
  u:add_order("NUMMER EINHEIT foLg")
  u:add_order("BENENNE EINHEIT Verfolger")

  u2 = unit.create(orcs, r0)
  u2.number = 1
  u2:add_item("horse", u2.number*3)
  u2:add_item("money", u.number*10)
  u2:set_skill("riding", 10)
  u2:set_skill("stealth", 2)
  u2:clear_orders()
  u2:add_order("FOLGEN EINHEIT nix")
  u2:add_order("NUMMER EINHEIT Last")
  u2:add_order("BENENNE EINHEIT Verfolger-Verfolger")

end


function test_sail()
  r0 = region.create(0, 0, "plain")

  orcs = faction.create("enno@eressea.de", "orc", "de")
  orcs.age = 20

  orc = unit.create(orcs, r0)
  orc.number = 1
  orc:add_item("speedsail", orc.number)

  orc:clear_orders()
  orc:add_order("NUMMER PARTEI orcs")
  orc:add_order("NUMMER EINHEIT orc")
  orc:add_order("BENENNE EINHEIT Orks")
  orc:add_order("ZEIGEN \"Sonnensegel\"")
end

function test_handler()

  local function msg_handler(u, evt)
    str = evt:get_string(0)
    u2 = evt:get_unit(1)
    print(u)
    print(u2)
    print(str)
    message_unit(u, u2, "thanks unit, i got your message: " .. str)
    message_faction(u, u2.faction, "thanks faction, i got your message: " .. str)
    message_region(u, "thanks region, i got your message: " .. str)
  end

  plain = region.create(0, 0, "plain")
  skill = 8

  f = faction.create("enno@eressea.de", "orc", "de")
  f.age = 20

  u = unit.create(f, plain)
  u.number = 1
  u:add_item("money", u.number*100)
  u:clear_orders()
  u:add_order("NUMMER PARTEI test")
  u:add_handler("message", msg_handler)
  msg = "BOTSCHAFT EINHEIT " .. itoa36(u.id) .. " Du~Elf~stinken"

  f = faction.create("enno@eressea.de", "elf", "de")
  f.age = 20

  u = unit.create(f, plain)
  u.number = 1
  u:add_item("money", u.number*100)
  u:clear_orders()
  u:add_order("NUMMER PARTEI eviL")
  u:add_order(msg)

end

function test_combat()

  plain = region.create(0, 0, "plain")
  skill = 8

  f = faction.create("enno@eressea.de", "orc", "de")
  f.age = 20

  u = unit.create(f, plain)
  u.number = 100
  u:add_item("money", u.number*100)
  u:add_item("sword", u.number)
  u:set_skill("melee", skill)
  u:clear_orders()
  u:add_order("NUMMER PARTEI test")
  u:add_order("K�MPFE")
  u:add_order("BEF�RDERUNG")
  attack = "ATTACKIERE " .. itoa36(u.id)

  f = faction.create("enno@eressea.de", "elf", "de")
  f.age = 20

  u = unit.create(f, plain)
  u.number = 100
  u:add_item("money", u.number*100)
  u:add_item("sword", u.number)
  u:set_skill("melee", skill+2)
  u:clear_orders()
  u:add_order("NUMMER PARTEI eviL")
  u:add_order("KAEMPFE")
  u:add_order(attack)

end

function test_rewards()
  -- this script tests manufacturing and fighting.

  plain = region.create(0, 0, "plain")
  skill = 5

  f = faction.create("enno@eressea.de", "human", "de")
  f.age = 20
  u = unit.create(f, plain)
  u.number = 10
  u:add_item("money", u.number*100)
  u:add_item("greatbow", u.number)
  u:set_skill("bow", skill)
  u:clear_orders()
  u:add_order("KAEMPFE")
  attack = "ATTACKIERE " .. itoa36(u.id)

  u = unit.create(f, plain)
  u.number = 7
  u:add_item("money", u.number*100)
  u:add_item("mallorn", u.number*10)
  u:set_skill("weaponsmithing", 7)
  u:clear_orders()
  u:add_order("KAEMPFE NICHT")
  u:add_order("MACHEN Elfenbogen")
  u:add_order("NUMMER PARTEI test")

  f = faction.create("enno@eressea.de", "elf", "de")
  f.age = 20
  u = unit.create(f, plain)
  u.number = 7
  u:add_item("money", u.number*100)
  u:add_item("greatbow", u.number)
  u:set_skill("bow", skill)
  u:clear_orders()
  u:add_order("KAEMPFE HINTEN")
  u:add_order(attack)

  u = unit.create(f, plain)
  u.number = 7
  u:add_item("money", u.number*100)
  u:add_item("mallorn", u.number*10)
  u:set_skill("weaponsmithing", 7)
  u:clear_orders()
  u:add_order("KAEMPFE NICHT")
  u:add_order("MACHEN Elfenbogen")
  u:add_order("NUMMER PARTEI eviL")

  u = unit.create(f, plain)
  u.number = 7
  u:add_item("money", u.number*100)
  u:add_item("mallorn", u.number*10)
  u:set_skill("weaponsmithing", 7)
  u:clear_orders()
  u:add_order("KAEMPFE NICHT")

  items = { "hornofdancing", "trappedairelemental", 
    "aurapotion50", "bagpipeoffear",
    "instantartacademy", "instantartsculpture" }
  local index
  local item
  for index, item in pairs(items) do
    u:add_item(item, 1)
    u:add_order('@BENUTZEN "' .. get_string("de", item) .. '"')
  end
  u:add_order("NUMMER PARTEI eviL")

end

function test_give()
  plain = region.create(0, 0, "plain")
  f = faction.create("enno@eressea.de", "human", "de")
  f.age = 20
  u = unit.create(f, plain)
  u.number = 10
  u:add_item("money", u.number*100)
  u:clear_orders()
  u:add_order("MACHE TEMP eins")
  u:add_order("REKRUTIERE 1")
  u:add_order("ENDE")
  u:add_order("GIB TEMP eins ALLES silber")
  u:add_order("NUMMER PARTEI test")
  
end

function test_write()
  read_game("24")
  read_orders("befehle")
end

function move_north(u)
  for order in u.orders do
    print(order)
  end
  u:clear_orders()
  u:add_order("NACH NORDEN")
end

function test_monsters()
  -- magrathea = get_region(-67, -5)
  local magrathea = get_region(0, 0)
  if magrathea ~= nil then
    if pcall(dofile, scriptpath .. "/ponnuki.lua") then
      init_ponnuki(magrathea)
    else
      print("could not open ponnuki")
    end
  end

  set_race_brain("braineater", move_north)
  plan_monsters()
end

function test_parser()
  -- this script tests the changes to quotes

  plain = region.create(0, 0, "plain")
  skill = 5

  f = faction.create("enno@eressea.de", "human", "de")
  f.age = 20
  u = unit.create(f, plain)
  u.number = 10
  u:clear_orders()
  u:add_order("Nummer Partei test")
  u:add_order("BENENNE PARTEI \"Diese Partei heisst \\\"Enno's Schergen\\\".\"")
  u:add_order("BENENNE EINHEIT \"Mein Name ist \\\"Enno\\\".\"")
end

function test_fail()
  plain = region.create(0, 0, "plain")
  skill = 5

  f = faction.create("enno@eressea.de", "human", "de")
  print(f)
end

function run_scripts()
  scripts = { 
    "xmas2004.lua"
  }
  local index
  local name
  for index, name in pairs(scripts) do
    local script = scriptpath .. "/" .. name
    print("- loading " .. script)
    if pcall(dofile, script)==0 then
      print("Could not load " .. script)
    end
  end
end

function test_moving()
  test_movement()
  run_scripts()
  process_orders()
  write_reports() 

  if swim.region==ocean then
    print "ERROR: Meermenschen k�nnen nicht anschwimmen"
  end
  if sail.region~=r0 then
    print "ERROR: Kapit�n kann Schiff mit NACH ohne VERLASSE verlassen"
  end
  if crew.region==r0 then
    print "ERROR: Einheiten kann Schiff nicht mit NACH ohne VERLASSE verlassen"
  end
  drift = false
  for i = 1, 100 do
    if ships[i].region ~= ocean then
      drift = true
      break
    end
  end
  if not drift then
    print "ERROR: Unbemannte Schiffe treiben nicht ab"
  end
  if foot.region ~= w1 then
    print "ERROR: Fusseinheit hat ihr NACH nicht korrekt ausgef�hrt"
  end
  if astra.region ~= r4 then
    print "ERROR: Astraleinheit konnte Wirbel nicht benutzen"
  end
end

-- test_movement()
-- test_fail()
-- test_handler()
-- test_parser()
-- test_monsters()
-- test_combat()
-- test_rewards()
-- test_give()
-- test_write()

-- test_sail()
-- write_game("../testg.txt")
-- read_game("../testg.txt")

if 0==1 then
  run_scripts()
  process_orders()
  write_reports() 
end

-- test_moving()
if 0==1 then
 read_game("530")
 -- read_orders("../game/orders.530")
 plan_monsters()
 process_orders()
 write_game("531")
else
 read_game("531")
 plan_monsters()
 process_orders()
 write_game("532")
end
