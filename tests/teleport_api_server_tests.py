"""Run with Python + lupa.lua51; exercises the real integration Lua script."""
from pathlib import Path
import subprocess
import sys
from lupa import lua51

root = Path(__file__).resolve().parents[1]
lua = lua51.LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
package.preload.Teleport_xyz = function()
    local empty = {}
    return {destinations={cities=empty,eastern=empty,kalimdor=empty,dungeons=empty,
                          raids=empty,arena=empty},
            favorites={{"Example-Test", 0, 10, 20, 30, 0}}}
end
now = 10000
function GetCurrTime() return now end
player = {sent={}, calls={}, combat=false, item=false, orientation=1.25, result=true}
function player:GetGUIDLow() return 123 end
function player:GetItemByEntry() if self.item then return {} end end
function player:IsInCombat() return self.combat end
function player:GetO() return self.orientation end
function player:SendAddonMessage(prefix, text) assert(prefix == 'TELEPORTUI'); table.insert(self.sent, text) end
function player:Teleport(...) table.insert(self.calls, {...}); return self.result end
function player:GossipComplete() end
function player:RegisterEvent() end
function player:SendBroadcastMessage() end
''')
module = lua.execute((root / "integration/vmangos/TeleportManager.lua").read_text(encoding="utf-8"))
lua.globals().module = module
lua.execute(r'''
local function command(text) assert(module.command(nil, player, text) == false) end
local function status(expected) assert(player.sent[#player.sent] == expected, player.sent[#player.sent]) end
local function advance() now = now + 2000 end
command('synthopen tp xyz 1 0 1.250000 -2.500000 3.000000 -')
status('D:1:ok')
assert(#player.calls == 1 and player.calls[1][1] == 0 and player.calls[1][5] == 1.25)
command('synthopen tp xyz 2 230 1 2 3 0.5'); status('D:2:rate')
advance(); player.combat = true
command('synthopen tp xyz 3 230 1 2 3 0.5'); status('D:3:combat')
assert(#player.calls == 1)
advance(); player.combat = false
command('synthopen tp xyz 4 230 1 2 3 0.5'); status('D:4:ok')
assert(#player.calls == 2 and player.calls[2][1] == 230 and player.calls[2][5] == 0.5)
advance(); command('synthopen tp xyz 5 230 17067 2 3 0'); status('D:5:invalid')
advance(); command('synthopen tp xyz 6 4294967296 1 2 3 0'); status('D:6:invalid')
advance(); command('synthopen tp xyz 7 230 1 2 3 99'); status('D:7:invalid')
advance(); player.result = false
command('synthopen tp xyz 8 230 1 2 3 0'); status('D:8:rejected')
local count = #player.calls
advance(); command('synthopen tp xyz 9 0 1 2 3 0 extra'); assert(#player.calls == count)
command('synthopen tp xyz 0000000000 0 1 2 3 0'); assert(#player.calls == count)
-- Original catalog path still enforces the hearthstone.
command('synthopen tp nav 1 10'); status('R:1:10:item')
player.item = true; advance(); command('synthopen tp nav 1 11')
assert(player.sent[#player.sent] == 'Z:1:11')
module.logout(nil, player)
advance(); player.result = true; command('synthopen tp xyz 10 1 1 2 3 0'); status('D:10:ok')
''')
print("teleport API server tests passed")

if len(sys.argv) > 1:
    script = subprocess.check_output([sys.argv[1], "--emit-script"], text=True)
    client = lua51.LuaRuntime(unpack_returned_tuples=True)
    client.execute(r'''
messages, sent = {}, {}
DEFAULT_CHAT_FRAME = {}
function DEFAULT_CHAT_FRAME:AddMessage(s) table.insert(messages, s) end
function UnitName() return 'hero' end
function SendChatMessage(text, channel) table.insert(sent, {text, channel}) end
function CreateFrame()
    local frame = {}
    function frame:RegisterEvent(e) assert(e == 'CHAT_MSG_ADDON') end
    function frame:SetScript(e, callback) assert(e == 'OnEvent'); self.callback = callback end
    return frame
end
''')
    client.execute(script)
    client.execute(r'''
assert(#sent == 1 and sent[1][1] == '.synthopen tp xyz 42 0 1.250000 -2.500000 3.000000 -')
assert(sent[1][2] == 'SAY')
arg1, arg2, arg4 = 'TELEPORTUI', 'D:42:ok', 'hero'
TeleportHackDXApiFrame.callback()
assert(messages[1] == '[Teleport API] 42: ok')
arg4 = 'someone else'; TeleportHackDXApiFrame.callback(); assert(#messages == 1)
''')
    print("teleport API client Lua tests passed")
