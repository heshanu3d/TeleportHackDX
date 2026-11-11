-- Server-owned destination IDs; clients never send map IDs or coordinates.
local data = require('Teleport_xyz')
local M = {}
local PREFIX, ITEM = 'TELEPORTUI', 6948
local nextSession = 0
local waiting, lastRead = {}, {}
local readCount, READ_LIMIT = 0, 512
local lastApi, apiCount, API_LIMIT = {}, 0, 512
local categories, byName, points = {}, {}, {}
local groups = {}
local function fingerprint(text)
    local value = 1
    for i = 1, #text do value = (value * 131 + text:byte(i)) % 2147483647 end
    return value
end

local function assign(group, names)
    for _, name in ipairs(names) do groups[name] = group end
end
assign(2, {'艾尔文森林','西部荒野','赤脊山','湿地','暮色森林','希尔斯布莱德','阿拉希','奥特兰克',
    '丹莫罗','洛克莫丹','荒芜之地','荆棘谷','灼热峡谷','悲伤沼泽','辛特兰','诅咒之地',
    '燃烧平原','西瘟疫','东瘟疫'})
assign(3, {'泰达希尔','灰谷','闪光平原','杜隆塔尔','贫瘠之地','尘泥沼泽','凄凉之地','菲拉斯',
    '塔纳利斯','月光林地','棘齿城','安戈洛','达纳苏斯','冬泉谷','希利苏斯'})
assign(4, {'秘血岛','秘蓝岛','地狱火半岛','赞加沼泽','纳格兰','泰罗卡森林','刀锋山','虚空风暴','影月谷'})
assign(5, {'死亡骑士','嚎风峡湾','北风苔原','龙骨荒野','晶歌森林','祖达克',
    '索拉查盆地','灰熊丘陵','风暴峭壁','冰冠冰川'})

local function category(name, group)
    local id = byName[name]
    if id then return categories[id] end
    id = #categories + 1
    local row = {id=id, name=name, group=group, points={}, signature=fingerprint(name)}
    categories[id], byName[name] = row, id
    return row
end
local function add(cat, row, home)
    local id = #points + 1
    local identity = home and ('home|' .. row[1]) or
        ('%s|%s|%.9g|%.9g|%.9g|%.9g'):format(row[1], tostring(row[2]), row[3], row[4], row[5], row[6] or 0)
    points[id] = {id=id, name=row[1], map=row[2], x=row[3], y=row[4], z=row[5], facing=row[6], home=home,
        signature=fingerprint(identity)}
    cat.points[#cat.points + 1] = points[id]
end

-- Keep the pre-existing shortcuts available in addition to the favorite catalog.
local shortcuts = category('快捷传送', 1)
local shortcutGroups = {
    {'主城与中立城镇', data.destinations.cities}, {'东部王国', data.destinations.eastern},
    {'卡利姆多', data.destinations.kalimdor}, {'经典副本入口', data.destinations.dungeons},
    {'经典团队副本', data.destinations.raids}, {'古拉巴什竞技场', data.destinations.arena},
}
for _, group in ipairs(shortcutGroups) do
    for _, row in ipairs(group[2]) do
        add(shortcuts, {shortcuts.name .. '-' .. group[1] .. '-' .. row[1], row[2], row[3], row[4], row[5], row[6]})
    end
end
add(shortcuts, {shortcuts.name .. '-返回设定旅店', 0, 0, 0, 0, 0}, true)
for _, row in ipairs(data.favorites) do
    local name = row[1]:match('^([^-]+)') or row[1]
    add(category(name, groups[name] or 1), row)
end

local function send(player, message)
    if #message > 240 or message:find('[\r\n\t]') then
        error('invalid teleport addon payload')
    end
    player:SendAddonMessage(PREFIX, message, 6, player)
end
local function allowed(player)
    return player and player:GetItemByEntry(ITEM) ~= nil
end
local function result(player, session, token, status)
    send(player, ('R:%s:%s:%s'):format(session, token, status))
end

function M.open(player)
    nextSession = nextSession % 999999999 + 1
    local id, guid = nextSession, player:GetGUIDLow()
    waiting[guid] = id
    player:GossipComplete()
    send(player, 'OPEN:' .. id)
    player:RegisterEvent(function(_, _, _, owner)
        if owner and waiting[guid] == id then
            waiting[guid] = nil
            owner:SendBroadcastMessage('请启用 TeleportManager 客户端插件后再使用超级传送。')
        end
    end, 2500, 1)
end

function M.logout(_, player)
    if player then
        local guid = player:GetGUIDLow()
        waiting[guid] = nil
        if lastRead[guid] then lastRead[guid] = nil; readCount = readCount - 1 end
        if lastApi[guid] then lastApi[guid] = nil; apiCount = apiCount - 1 end
    end
end

-- The existing .synthopen SAY bridge invokes this on the player's map thread.
-- Sessions are response-correlation tokens, not authority: after cross-map
-- travel the Eluna state changes, so every request rechecks the hearthstone.
function M.command(_, player, command)
    if not player or type(command) ~= 'string' or not command:match('^synthopen tp ') then return end
    if command:match('^synthopen tp xyz ') then
        if #command > 180 then return false end
        local token, map, x, y, z, facing = command:match('^synthopen tp xyz (%d+) (%d+) ([%d%.%-]+) ([%d%.%-]+) ([%d%.%-]+) ([%d%.%-]+)$')
        if not token or #token > 9 or tonumber(token) == 0 then return false end
        local function apiResult(status) send(player, ('D:%s:%s'):format(token, status)) end
        local guid, now = player:GetGUIDLow(), GetCurrTime()
        if lastApi[guid] and (now - lastApi[guid]) % 4294967296 < 1500 then
            apiResult('rate'); return false
        end
        if not lastApi[guid] then
            if apiCount >= API_LIMIT then
                local oldest, age
                for id, last in pairs(lastApi) do
                    local elapsed = (now - last) % 4294967296
                    if not age or elapsed > age then oldest, age = id, elapsed end
                end
                if oldest then lastApi[oldest] = nil; apiCount = apiCount - 1 end
            end
            apiCount = apiCount + 1
        end
        lastApi[guid] = now
        map, x, y, z = tonumber(map), tonumber(x), tonumber(y), tonumber(z)
        facing = facing == '-' and player:GetO() or tonumber(facing)
        local function finite(n) return n and n == n and n ~= math.huge and n ~= -math.huge end
        if not map or map > 4294967295 or not finite(x) or not finite(y) or
            not finite(z) or not finite(facing) or math.abs(x) > 17066 or
            math.abs(y) > 17066 or math.abs(z) > 400000 or math.abs(facing) > 12.566370614 then
            apiResult('invalid'); return false
        end
        if player:IsInCombat() then apiResult('combat'); return false end
        local ok = player:Teleport(map, x, y, z, facing)
        apiResult(ok and 'ok' or 'rejected')
        return false
    end
    if command == 'synthopen tp open' then
        if not allowed(player) then player:SendBroadcastMessage('未找到超级炉石。')
        elseif player:IsInCombat() then player:SendBroadcastMessage('战斗中不能使用超级传送。')
        else M.open(player) end
        return false
    end
    local action, session, token, arg, signature = command:match('^synthopen tp (%a+) (%d+) (%d+) ?(%d*) ?(%d*)$')
    if not action or #session > 9 or #token > 9 or #arg > 10 or #signature > 10 then return false end
    if action == 'ack' and arg == '' and signature == '' then
        if waiting[player:GetGUIDLow()] == tonumber(session) then waiting[player:GetGUIDLow()] = nil end
        return false
    end
    if action == 'nav' and waiting[player:GetGUIDLow()] == tonumber(session) then waiting[player:GetGUIDLow()] = nil end
    if not allowed(player) then result(player, session, token, 'item'); return false end
    if action == 'nav' or action == 'list' then
        local guid, now = player:GetGUIDLow(), GetCurrTime()
        local reads = lastRead[guid]
        if not reads then
            if readCount >= READ_LIMIT then
                local oldest, age
                for id, entry in pairs(lastRead) do
                    local elapsed = (now - entry.last) % 4294967296
                    if not age or elapsed > age then oldest, age = id, elapsed end
                end
                if oldest then lastRead[oldest] = nil; readCount = readCount - 1 end
            end
            reads = {}; lastRead[guid] = reads; readCount = readCount + 1
        end
        reads.last = now
        if reads[action] and (now - reads[action]) % 4294967296 < 500 then
            result(player, session, token, 'rate')
            return false
        end
        reads[action] = now
    end
    if action == 'nav' and arg == '' and signature == '' then
        send(player, ('H:%s:%s:%d'):format(session, token, #categories))
        for _, cat in ipairs(categories) do
            send(player, ('N:%s:%s:%d:%d:%d:%d:%s'):format(session, token, cat.id, cat.group, #cat.points, cat.signature, cat.name))
        end
        send(player, ('Z:%s:%s'):format(session, token))
    elseif action == 'list' then
        local cat = categories[tonumber(arg)]
        if not cat or cat.signature ~= tonumber(signature) then result(player, session, token, 'changed'); return false end
        send(player, ('H:%s:%s:%d'):format(session, token, #cat.points))
        for _, row in ipairs(cat.points) do
            local available = row.home or (type(row.map) == 'number' and row.map >= 0)
            send(player, ('I:%s:%s:%d:%d:%d:%s'):format(session, token, row.id, available and 1 or 0, row.signature, row.name))
        end
        send(player, ('Z:%s:%s'):format(session, token))
    elseif action == 'go' then
        local row = points[tonumber(arg)]
        if not row or row.signature ~= tonumber(signature) then result(player, session, token, 'changed')
        elseif player:IsInCombat() then result(player, session, token, 'combat')
        elseif row.home then
            player:CastSpell(player, 8690, true)
            result(player, session, token, 'home')
        elseif type(row.map) ~= 'number' or row.map < 0 then result(player, session, token, 'unknown')
        else
            local ok = player:Teleport(row.map, row.x, row.y, row.z, row.facing or 0)
            result(player, session, token, ok and 'ok' or 'rejected')
        end
    end
    return false
end

return M
