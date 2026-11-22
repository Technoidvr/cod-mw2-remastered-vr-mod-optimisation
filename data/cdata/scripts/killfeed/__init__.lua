local STYLE_DISABLED = 0
local STYLE_BOCW = 1
local STYLE_MW_CLASSIC = 2
local STYLE_MW2019 = 3
local STYLE_MW2022 = 4
local STYLE_MW2023 = 5

local pending_damage = {}
local bocw_hit_index = 1
local bocw_headshot_index = 1
local duck_error_reported = false
local duck_submission_reported = false

local DUCK_SCRIPT = "scripts/killfeed_ducking"
local DUCK_GROUP_COUNT = 50

local function report_duck_error(action, err)
    if duck_error_reported then
        return
    end

    duck_error_reported = true
    print("[H2 Killfeed] Failed to " .. action .. " ducking submix: " .. tostring(err))
end

-- The shared native/launcher setting owns registration, persistence and default.
local styles = {
    off = STYLE_DISABLED,
    bocw = STYLE_BOCW,
    mw_classic = STYLE_MW_CLASSIC,
    mw2019 = STYLE_MW2019,
    mw2022 = STYLE_MW2022,
    mw2023 = STYLE_MW2023
}

-- Modern Warfare presets replace hits with kills on lethal damage. MWII shares
-- MW2019's hit recording; only MWIII has a separate nonlethal headshot sound.
local modern_sounds = {
    [STYLE_MW2019] = {
        hit = "kf_mw2019_hit",
        kill = "kf_mw2019_kill",
        kill_headshot = "kf_mw2019_kill_headshot"
    },
    [STYLE_MW2022] = {
        hit = "kf_mw2019_hit",
        kill = "kf_mw2022_kill",
        kill_headshot = "kf_mw2022_kill_headshot"
    },
    [STYLE_MW2023] = {
        hit = "kf_mw2023_hit",
        hit_headshot = "kf_mw2023_hit_headshot",
        kill = "kf_mw2023_kill",
        kill_headshot = "kf_mw2023_kill_headshot"
    }
}

local function call_getentref(entity)
    return entity:getentref()
end

local function call_isalive(victim)
    return game:isalive(victim)
end

local function call_trigger_duck()
    return game:scriptcall(DUCK_SCRIPT, "trigger")
end

local function get_entref(entity)
    if entity == nil then
        return nil, nil
    end

    local ok, entnum, classnum = pcall(call_getentref, entity)
    if not ok then
        return nil, nil
    end

    return entnum, classnum
end

local function is_local_player(entity)
    local entnum, classnum = get_entref(entity)
    return entnum == 0 and classnum == 0
end

local function feedback_style()
    return styles[game:getdvar("vr_killfeedStyle")] or STYLE_DISABLED
end

local function is_head_hit(hitloc)
    return hitloc == "head" or hitloc == "helmet"
end

local function next_bocw_hit()
    local alias = "kf_bocw_hit_" .. bocw_hit_index
    bocw_hit_index = (bocw_hit_index % 3) + 1
    return alias
end

local function next_bocw_headshot()
    local alias = "kf_bocw_headshot_" .. bocw_headshot_index
    bocw_headshot_index = (bocw_headshot_index % 3) + 1
    return alias
end

local function trigger_duck()
    -- GSC owns the deadline and recovery thread, including cleanup if the Lua
    -- context is reloaded. Stop attempting ducking after an adapter failure;
    -- feedback audio remains independent of optional mix adjustment.
    if duck_error_reported then return end
    local ok, submitted_groups = pcall(call_trigger_duck)
    if not ok or submitted_groups ~= DUCK_GROUP_COUNT then
        report_duck_error("submit", ok
            and ("expected " .. DUCK_GROUP_COUNT .. " volume groups, got " .. tostring(submitted_groups))
            or submitted_groups)
        return
    end
    if not duck_submission_reported then
        duck_submission_reported = true
        print("[H2 Killfeed] Feedback ducking submitted " .. submitted_groups .. " volume groups")
    end
end

local function play_feedback(style, killed, headshot)
    trigger_duck()

    if style == STYLE_BOCW then
        player:playlocalsound(headshot and next_bocw_headshot() or next_bocw_hit())
        if killed then
            player:playlocalsound("kf_bocw_kill")
        end
    elseif style == STYLE_MW_CLASSIC then
        player:playlocalsound("kf_mw_classic_hit")
    elseif modern_sounds[style] ~= nil then
        local sounds = modern_sounds[style]
        if killed then
            player:playlocalsound(headshot and sounds.kill_headshot or sounds.kill)
        else
            player:playlocalsound(headshot and sounds.hit_headshot or sounds.hit)
        end
    end
end

local function resolve_damage(key, killed)
    local pending = pending_damage[key]
    if pending == nil then
        return
    end

    pending_damage[key] = nil
    if pending.death_listener ~= nil then
        pending.death_listener:clear()
    end
    if pending.timeout ~= nil then
        pending.timeout:clear()
    end

    if killed ~= nil then
        play_feedback(pending.style, killed, pending.headshot)
    end
end

local function queue_feedback(victim, style, headshot, entnum, classnum)
    if entnum == nil then
        entnum, classnum = get_entref(victim)
        if entnum == nil then
            return
        end
    end

    local key = entnum .. ":" .. classnum
    local existing = pending_damage[key]

    if existing ~= nil then
        existing.style = style
        existing.headshot = headshot
        return
    end

    local pending = {
        style = style,
        headshot = headshot
    }
    pending_damage[key] = pending

    pending.death_listener = victim:onnotifyonce("death", function()
        resolve_damage(key, true)
    end)
    pending.timeout = game:ontimeout(function()
        -- A target can be removed before the next frame; its absence alone
        -- cannot establish a kill. Always retire its listener and timeout.
        local ok, alive = pcall(call_isalive, victim)
        if ok then
            resolve_damage(key, alive == 0)
        else
            resolve_damage(key, nil)
        end
    end, 0)
end

game:onentitydamage(function(victim, inflictor, attacker, damage, mod, weapon, direction, hitloc)
    if damage <= 0 then
        return nil
    end

    local style = feedback_style()
    if style == STYLE_DISABLED then
        return nil
    end

    if not is_local_player(attacker) then
        return nil
    end

    local victim_entnum, victim_classnum = get_entref(victim)
    if victim_entnum == nil or (victim_entnum == 0 and victim_classnum == 0) then
        return nil
    end

    if game:isalive(victim) == 0 then
        return nil
    end

    queue_feedback(victim, style, is_head_hit(hitloc), victim_entnum, victim_classnum)
    return nil
end)

print("[H2 Killfeed] Hit and kill feedback initialized")
