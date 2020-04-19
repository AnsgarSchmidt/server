-- Tests that work in E3. With game config of E3. 
-- Tests are under scripts/test/e3 and all files must be in scripts/test/e3/init.lua

lunit = require('lunit')
module = lunit.module

path = 'scripts'
if config.install then
    path = config.install .. '/' .. path
    --needed to find lunit if not run form eressea root. Needs right [lua] install setting in eressea.ini (point to eressea root from the start folder)
end
package.path = package.path .. ';' .. path .. '/?.lua;' .. path .. '/?/init.lua'

config.rules = 'e3'

require 'eressea'
require 'eressea.path'
require 'eressea.xmlconf'
require 'tests.e3'

rng.inject(0)
eressea.settings.set("rules.alliances", "0")
eressea.settings.set("rules.food.flags", "4")
eressea.settings.set("rules.ship.damage.nocrew", "0")
eressea.settings.set("rules.ship.drifting", "0")
eressea.settings.set("rules.ship.storms", "0")
eressea.settings.set("nmr.timeout", "0")
eressea.settings.set("NewbieImmunity", "0")
rules = require('eressea.' .. config.rules)
result = lunit.main()
return result.errors + result.failed
