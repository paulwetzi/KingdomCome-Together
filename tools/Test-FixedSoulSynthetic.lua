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
    e.soul = setmetatable({}, { __index = function() return function() return nil end end })
    e.SetWorldAngles = function(_, a) e.angles = a end
    setmetatable(e, { __index = function() return function() return nil end end })
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

-- The character window, and the look a partner chose
do
    local M, L = KCD2MP.w154menu, KCD2MP.look
    local G1 = "7ac037fe-60ca-4212-a39f-0093cff270ba"
    local G2 = "00ec8c08-21d3-4f65-8c84-cf28958f0cde"
    local function evt(name, arg, from) return countEvt(name, arg, from) end
    local function page(node, title, rows)
        KCD2MP_FigurePageBegin(node, title, "", #rows)
        for _, r in ipairs(rows) do KCD2MP_FigureRow(node, r[1], r[2], r[3], r[4] or "", r[5] or "") end
    end
    local function key(verb) return KCD2MP_W154Menu(verb, "key") end
    local function frame() M.menu.frameAt = NOW; M.draw() end
    player.actor.IsUnconscious = function() return false end
    player.human.IsMounted = function() return false end

    KCD2MP_W154MenuSession("host", true, nil, nil, nil, "connected", "test", false, false)
    check("W1: the menu opens", key("open") == true)
    check("W2: the first item is the character window", M.ITEMS[1].id == "character" and M.menu.sel == 1)
    local mark = #LOG
    check("W3: End opens the window and asks for the list", key("change") == true and L.open == true and evt("fig_list", "root", mark) == 1)
    check("W4: keys wait while the list loads", key("down") == true and L.sel == 1)
    page("stale", "x", { { "n", "g0", "old" } })
    check("W5: an answer for another page is ignored", L.waiting == true and #L.rows == 0)
    page("root", "Charakter waehlen", { { "r", "none", "Standardfigur" }, { "n", "g0", "Frauen (2)" } })
    check("W6: the page arrives", L.waiting == false and #L.rows == 2 and L.sel == 1)
    frame()
    check("W7: PgDn moves, and wraps at the end", key("down") and L.sel == 2 and key("down") and L.sel == 1 and key("up") and L.sel == 2)
    mark = #LOG
    key("change")
    check("W8: End on a group asks for it", evt("fig_list", "g0", mark) == 1 and L.waiting == true)
    page("g0", "Frauen", { { "b", "root", "< Zurueck" }, { "s", "ttkc_woman_9", "Magd 9", G1, "NPC_Female" }, { "s", "tvez_woman_3", "Frau 3 (!)", G2, "NPC_Female" } })
    check("W9: a page starts on its first entry, not on Back", L.sel == 2)

    local before = #SPAWNS
    frame()
    check("W10: no preview before the short delay", #SPAWNS == before)
    NOW = NOW + 1
    frame()
    local s = SPAWNS[#SPAWNS]
    check("W11: the preview stands in front of the player", #SPAWNS == before + 1 and s.Name:find("^kcd2mp_preview_") ~= nil and s.SharedSoulGuid == G1 and s.Pos[1] > PX)
    check("W12: the preview counts as the mod's own body", L.previewEnt ~= nil and KCD2MP_W139IsAvatar(L.previewEnt) == false and L.previewGuid == G1)
    frame()
    check("W13: the same entry is not spawned twice", #SPAWNS == before + 1)
    key("down")
    NOW = NOW + 1
    frame()
    check("W14: the next entry replaces the preview, under a new name", #SPAWNS == before + 2 and SPAWNS[#SPAWNS].SharedSoulGuid == G2 and SPAWNS[#SPAWNS].Name ~= s.Name)

    P_DANGER = true
    mark = #LOG
    key("change")
    check("W15: not during a fight", evt("fig_pick", "", mark) == 0 and tostring(L.note):find("Kampf", 1, true) ~= nil)
    P_DANGER = false
    mark = #LOG
    key("change")
    check("W16: End chooses the character", evt("fig_pick", "tvez_woman_3", mark) == 1)
    KCD2MP_FigureCurrent("tvez_woman_3", "Frau 3", true)
    check("W17: the player is told", L.cur == "tvez_woman_3" and tostring(L.note):find("Frau 3", 1, true) ~= nil)
    frame()

    key("toggle")
    check("W18: the menu key closes the window and the menu", L.open == false and M.menu.open == false)
    check("W19: the preview goes with it", L.previewEnt == nil and L.previewGuid == nil)

    -- the partner's choice on this machine
    KCD2MP.ghostNames["7"] = "Partner"
    KCD2MP_SpawnGhost("7", 12, 22, 32, 0)
    local male = KCD2MP.ghosts["7"]
    check("P1: without a choice the figure is the mod's own", male.facePick.mod == true and male.fixedLook == false)
    check("P2: the partner's choice is taken", KCD2MP_SetFigureLook("7", "ttkc_woman_9", G1, "NPC_Female", false) == true)
    check("P3: the old body is removed for the respawn", KCD2MP.ghosts["7"] == nil)
    check("P4: the same choice again changes nothing", KCD2MP_SetFigureLook("7", "ttkc_woman_9", G1, "NPC_Female", false) == false)
    PRESETS = 0
    KCD2MP_SpawnGhost("7", 12, 22, 32, 0)
    check("P5: the respawn wears the chosen soul", SPAWNS[#SPAWNS].SharedSoulGuid == G1 and SPAWNS[#SPAWNS].ClassName == "NPC_Female")
    check("P6: her clothes stay", PRESETS == 0 and KCD2MP.ghosts["7"].fixedLook == true and KCD2MP_W144Equip("kcd2mp_7", "x") == false)
    check("P7: a class that is no human is refused", KCD2MP_SetFigureLook("6", "x", G2, "Horse", false) == false and KCD2MP.figureLooks["6"] == nil)

    KCD2MP_SetFigureLook("7", "ttac_man_8", "fd1af8c5-c500-4add-b0b6-6c0505fe80c2", "NPC", true)
    local weapons, clothes = 0, 0
    local cmdMark = #CMDS
    KCD2MP_SpawnGhost("7", 12, 22, 32, 0)
    ENTS["kcd2mp_7"].actor.EquipClothingPreset = function() clothes = clothes + 1 end
    runTimers()
    local paused = false
    for i = cmdMark + 1, #CMDS do if CMDS[i] == "wh_ai_PauseNPC kcd2mp_7" then paused = true end end
    check("P8: a calm figure does not act on its own", paused)
    check("P9: a male character keeps his clothes", clothes == 0 and KCD2MP.ghosts["7"].fixedLook == true)

    check("P10: back to standard", KCD2MP_SetFigureLook("7", "", "", "", false) == true and KCD2MP.figureLooks["7"] == nil and KCD2MP.ghosts["7"] == nil)
    KCD2MP_SpawnGhost("7", 12, 22, 32, 0)
    check("P11: the standard figure is back and is dressed by the sync again", KCD2MP.ghosts["7"].facePick.mod == true and KCD2MP.ghosts["7"].fixedLook == false)
    check("P12: clearing twice changes nothing", KCD2MP_SetFigureLook("7", "", "", "", false) == false)
    noErrs("W/P")
end

OUT = table.concat(RESULTS, "\n")
