local lunit = require("lunit")
if _VERSION >= 'Lua 5.2' then
  _ENV = module("tests.locale", 'seeall')
else
  module("tests.locale", 'seeall')
end

function setup()
    eressea.free_game()
end

function test_get_set()
    local loc = "en"
    assert_not_nil(eressea.locale)
    eressea.locale.create(loc)
    assert_equal(nil, eressea.locale.get(loc, "move"))
    eressea.locale.set(loc, "move", "MOVE")
    assert_equal("MOVE", eressea.locale.get(loc, "move"))
end

