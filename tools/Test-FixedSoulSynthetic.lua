-- Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
-- GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
-- content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
-- Fixed figure soul test, against the real kdcmp.lua under MoonSharp (engine stubbed, fake clock). Driven by Test-FixedSoulSynthetic.ps1.
NOW = 0
os.clock = function() return NOW end
LOG = {}; TIMERS = {}; ENTS = {}; ERRS = {}; TOASTS = {}; CMDS = {}; CCMDS = {}; LISTENERS = {}

local function mkstub()
    return setmetatable({}, { __index = function(_, k) return function(...) return nil end end })
end
System = mkstub()
System.LogAlways = function(s) LOG[#LOG + 1] = tostring(s) end
System.GetCVarValue = function() return "0" end
CVARS = {}
System.GetCVar = function(n) return CVARS[n] ~= nil and tostring(CVARS[n]) or "0" end
System.SetCVar = function(n, v) CVARS[n] = v end
System.GetEntityByName = function(n) return ENTS[n] end
System.GetEntitiesInSphere = function(p, r) return {} end
System.ExecuteCommand = function(s) CMDS[#CMDS + 1] = tostring(s) end
System.RemoveEntity = function(eid) end
System.AddCCommand = function(name, body, help) CCMDS[name] = { body = tostring(body), help = tostring(help or "") } end
System.GetTerrainElevation = function(p) return 0 end
System.GetFrameTime = function() return 1 / 60 end
Script = mkstub()
Script.SetTimer = function(ms, f) TIMERS[#TIMERS + 1] = { ms = ms, f = f, at = NOW } end
Game = mkstub(); AI = mkstub(); Sound = mkstub(); Physics = mkstub(); Terrain = mkstub()
UIAction = mkstub()
UIAction.CallFunction = function(panel, inst, fn, text) TOASTS[#TOASTS + 1] = tostring(text) end
UIAction.RegisterElementListener = function(tbl, el, inst, ev, fn) LISTENERS[#LISTENERS + 1] = { tbl = tbl, el = el, ev = ev, fn = fn } end
Calendar = mkstub()
Calendar.GetWorldTime = function() return 1000000 end
XGenAIModule = mkstub()
MSGS = {}; ENDCOMBAT = {}
XGenAIModule.MakeTableFromType = function(t) return { kind = t } end
XGenAIModule.SendMessageToEntityData = function(to, kind, t) MSGS[#MSGS + 1] = { to = to, kind = kind, t = t } end
XGenAIModule.TryEndCombat = function(w) ENDCOMBAT[#ENDCOMBAT + 1] = w; return true end
EnvironmentModule = { GetRainIntensity = function() return 0 end }
ItemManager = mkstub()

NEXTID = 5000
PX, PY, PZ = 100, 100, 10
IN_DIALOG = false
ORIG_TALKS = 0
P_DANGER = false
BasicAIActions = { OnTalk = function(self, user, slot) ORIG_TALKS = ORIG_TALKS + 1 end,
                   OnChatWithFocus = function(self, user, slot) ORIG_TALKS = ORIG_TALKS + 1 end }
player = { id = 1, class = "Player" }
player.GetName = function() return "Dude" end
player.GetWorldPos = function() return { x = PX, y = PY, z = PZ } end
player.GetWorldAngles = function() return { x = 0, y = 0, z = 0 } end
player.GetDirectionVector = function() return { x = 1, y = 0, z = 0 } end
player.actor = { GetHealth = function() return 100 end, IsDead = function() return false end, StandUp = function() end }
player.human = { IsInDialog = function() return IN_DIALOG end }
player.soul = { RestrictDialog = function() end, GetState = function() return 0 end,
                IsInCombatDanger = function() return P_DANGER end, IsInTenseCircumstance = function() return false end }
ENTS["Dude"] = player

local rawpcall = pcall
pcall = function(f, ...)
    local r = { rawpcall(f, ...) }
    if not r[1] then ERRS[#ERRS + 1] = tostring(r[2]) end
    return unpack(r)
end

-- @@KDCMP@@

local RESULTS = {}
local function check(name, ok, detail)
    RESULTS[#RESULTS + 1] = (ok and "PASS  " or "FAIL  ") .. name .. (detail and ("  [" .. tostring(detail) .. "]") or "")
end
local function lastLog(needle, fromLog)
    for i = #LOG, (fromLog or 0) + 1, -1 do if LOG[i]:find(needle, 1, true) then return LOG[i] end end
    return nil
end
local function countEvt(name, argPrefix, fromLog)
    local n = 0
    for i = (fromLog or 0) + 1, #LOG do
        local l = LOG[i]
        if l:find("[KCD2-MP-EVT] v1 ", 1, true) and l:find(" " .. name .. " " .. (argPrefix or ""), 1, true) then n = n + 1 end
    end
    return n
end
local function runTimers()
    local t = TIMERS
    TIMERS = {}
    for _, x in ipairs(t) do x.f() end
end
local function noErrs(label) check(label .. ": no swallowed Lua errors", #ERRS == 0, ERRS[1]) end

-- Modified build: a mapped player's figure is a fixed female soul and keeps her own outfit.
local GUID = "4763a986-8361-a712-61d9-bf6dd706ddb6"
local SPAWNS, PRESETS, EQUIPS, CREATED = {}, 0, 0, 0
local function mkent(name, cls)
    local e = { id = NEXTID, class = cls, Properties = {} }
    NEXTID = NEXTID + 1
    e.actor = setmetatable({
        EquipClothingPreset = function() PRESETS = PRESETS + 1 end,
        EquipWeaponPreset = function() PRESETS = PRESETS + 1 end,
        EquipInventoryItem = function() EQUIPS = EQUIPS + 1 end,
    }, { __index = function() return function() return nil end end })
    e.inventory = setmetatable({
        FindItem = function() return "item" end,
        CreateItem = function() CREATED = CREATED + 1 end,
    }, { __index = function() return function() return nil end end })
    e.human = setmetatable({}, { __index = function() return function() return nil end end })
    e.GetName = function() return name end
    e.GetWorldPos = function() return { x = 1, y = 2, z = 3 } end
    e.GetCharacterFileName = function() return "humans/female/skeleton/female.chr" end
    return e
end
XGenAIModule.SpawnEntity = function(t)
    SPAWNS[#SPAWNS + 1] = t
    ENTS[t.Name] = mkent(t.Name, t.ClassName)
end

do
    local before = KCD2MP_PickFigureFace("Mapped")
    check("F1: unmapped = one of the mod's own souls", before.mod == true and before.className == "NPC")
    check("F2: a mapping is accepted", KCD2MP_SetFixedSoul("Mapped", "ttkc_woman_6", GUID) == true)
    check("F3: the same mapping again changes nothing", KCD2MP_SetFixedSoul("Mapped", "ttkc_woman_6", GUID) == false)
    local p = KCD2MP_PickFigureFace("Mapped")
    check("F4: the mapped name picks the female soul", p.className == "NPC_Female" and p.guid == GUID and p.fixedLook == true)
    check("F5: another name is untouched", KCD2MP_PickFigureFace("Someone").mod == true)
    KCD2MP_W177SetFigureSoul("off")
    check("F6: the mapping also holds with mp_figure_soul off", KCD2MP_PickFigureFace("Mapped").guid == GUID)
    KCD2MP_W177SetFigureSoul("on")

    KCD2MP.ghostNames["9"] = "Mapped"
    KCD2MP_SpawnGhost("9", 10, 20, 30, 0)
    local g = KCD2MP.ghosts["9"]
    check("F7: spawned as NPC_Female with the mapped soul", SPAWNS[#SPAWNS].ClassName == "NPC_Female" and SPAWNS[#SPAWNS].SharedSoulGuid == GUID)
    check("F8: the figure is marked", g ~= nil and g.fixedLook == true)
    check("F9: no clothing or weapon preset touched her", PRESETS == 0, PRESETS)
    check("F10: the outfit sync cannot dress her", KCD2MP_W144Equip("kcd2mp_9", "some-male-item") == false and EQUIPS == 0 and CREATED == 0)
    check("F11: the empty preset cannot strip her", KCD2MP_W136ClearPreset("9", "test") == false and PRESETS == 0)

    KCD2MP.ghostNames["8"] = "Someone"
    KCD2MP_SpawnGhost("8", 11, 21, 31, 0)
    local presetsAfterMale = PRESETS
    check("F12: a normal figure still gets the bare preset", presetsAfterMale > 0 and KCD2MP.ghosts["8"].fixedLook == false)
    check("F13: a normal figure is still dressed", KCD2MP_W144Equip("kcd2mp_8", "some-item") == true and EQUIPS == 1)
    check("F14: a normal figure can still be cleared", KCD2MP_W136ClearPreset("8", "test") == true)

    check("F15: mapping a player whose figure stands removes that body", KCD2MP_SetFixedSoul("Someone", "tsem_woman_9", "4187a4bf-c27a-dd4b-c348-7bec934968ad") == true
        and KCD2MP.ghosts["8"] == nil)
    check("F16: the figure already on its soul stays", KCD2MP.ghosts["9"] ~= nil)
    noErrs("F")
end

OUT = table.concat(RESULTS, "\n")
