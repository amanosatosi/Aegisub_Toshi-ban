-- Mangetsu normal-furigana adapter for Aegisub's existing Karaskel.
-- This creates a VIRTUAL karaoke stream. It never edits line.text or converts
-- the actual ASS events, and it does not attempt gyaku-furi (third fields).
local mangetsu_karaskel = {}

local function stripped(text)
    return (text:gsub("%b{}", ""))
end

-- Only top-level karaoke tags may move the shared event karaoke clock.
-- Braces containing other override tags are preserved in the virtual text.
local function scan_override(block, advance)
    local remainder = {}
    local last = 2
    local i, depth = 2, 0
    while i < #block do
        local c = block:sub(i, i)
        if c == "(" then
            depth = depth + 1
        elseif c == ")" then
            depth = math.max(0, depth - 1)
        elseif c == "\\" and depth == 0 then
            local a, b, name, duration = block:find("\\([kK][fFoO]?)(%d+)", i)
            if a == i then
                if i > last then
                    remainder[#remainder + 1] = block:sub(last, i - 1)
                end
                local tag = name == "K" and "\\kf" or "\\" .. name
                advance(tag, tonumber(duration) * 10)
                i = b + 1
                last = i
            end
        end
        i = i + 1
    end
    if last < #block then
        remainder[#remainder + 1] = block:sub(last, #block - 1)
    end
    local kept = table.concat(remainder)
    return kept ~= "" and "{" .. kept .. "}" or ""
end

-- Find only a complete native group with exactly one unescaped separator.
-- Distinguish the opening < from Karaskel's *internal* ruby spillback marker.
local function group_at(text, at)
    local pipe, i = nil, at + 1
    while i <= #text do
        local c = text:sub(i, i)
        if c == "{" then
            local close = text:find("}", i + 1, true)
            if not close then return nil end
            i = close + 1
        elseif c == "\\" then
            local next_char = text:sub(i + 1, i + 1)
            if next_char == "N" or next_char == "n" then return nil end
            -- Escaped structural delimiters are literals, not separators.
            i = i + 2
        elseif c == "<" then
            return nil
        elseif c == "|" then
            if pipe then return nil end -- gyaku-furi is not in scope
            pipe = i
            i = i + 1
        elseif c == ">" then
            if not pipe then return nil end
            local base, reading = text:sub(at + 1, pipe - 1),
                                  text:sub(pipe + 1, i - 1)
            if stripped(base) == "" or stripped(reading) == "" then return nil end
            return {first = at, last = i, base = base, reading = reading}
        else
            i = i + 1
        end
    end
    return nil
end

local function has_group(text)
    local i, in_override = 1, false
    while i <= #text do
        local c = text:sub(i, i)
        if c == "{" then
            in_override = true
        elseif c == "}" then
            in_override = false
        elseif not in_override and c == "\\" then
            i = i + 1
        elseif not in_override and c == "<" and group_at(text, i) then
            return true
        end
        i = i + 1
    end
    return false
end

-- Return a [0]-indexed parse_karaoke_data-compatible table, or nil to use
-- the stock Aegisub parser for ordinary/legacy subtitles.
function mangetsu_karaskel.parse_karaoke_data(line)
    if not line or type(line.text) ~= "string" or not has_group(line.text) then
        return nil
    end

    local source = line.text
    local kara = {
        [0] = {start_time = 0, end_time = 0, duration = 0,
               tag = "", text = "", text_stripped = ""}
    }
    local current_start, current_duration, current_tag = 0, 0, ""
    local regular_raw, regular_plain = "", ""

    local function append(raw, plain)
        kara[#kara + 1] = {
            start_time = current_start,
            end_time = current_start + current_duration,
            duration = current_duration,
            tag = current_tag,
            text = raw,
            text_stripped = plain
        }
    end
    local function flush_regular()
        if regular_plain ~= "" then
            append(regular_raw, regular_plain)
        end
        regular_raw, regular_plain = "", ""
    end
    local function advance(tag, duration)
        flush_regular()
        current_start = current_start + current_duration
        current_duration, current_tag = duration, tag
    end
    local function plain_char(c)
        regular_raw = regular_raw .. c
        regular_plain = regular_plain .. c
    end

    local i = 1
    while i <= #source do
        local c = source:sub(i, i)
        if c == "{" then
            local close = source:find("}", i + 1, true)
            if not close then
                plain_char(c)
                i = i + 1
            else
                local kept = scan_override(source:sub(i, close), advance)
                regular_raw = regular_raw .. kept
                i = close + 1
            end
        elseif c == "\\" then
            -- Literal escapes must not begin a native <...> group.
            local escape = source:sub(i, i + 1)
            plain_char(escape)
            i = i + #escape
        elseif c == "<" then
            local group = group_at(source, i)
            if not group then
                plain_char(c)
                i = i + 1
            else
                flush_regular()
                local base_plain = stripped(group.base)
                local base_raw = group.base
                local first = true
                local part_raw, part_plain = "", ""
                local function flush_ruby(force)
                    if part_plain == "" and not force then return end
                    local leader = first and (base_plain .. "|<") or "#|"
                    local raw_leader = first and (base_raw .. "|<") or "#|"
                    append(raw_leader .. part_raw, leader .. part_plain)
                    part_raw, part_plain = "", ""
                    first = false
                end
                local function ruby_advance(tag, duration)
                    -- Empty segments (waits) must still advance the clock,
                    -- without manufacturing visible base/ruby characters.
                    flush_ruby(false)
                    current_start = current_start + current_duration
                    current_duration, current_tag = duration, tag
                end
                local j = 1
                while j <= #group.reading do
                    local rc = group.reading:sub(j, j)
                    if rc == "{" then
                        local close = group.reading:find("}", j + 1, true)
                        if close then
                            local kept = scan_override(group.reading:sub(j, close), ruby_advance)
                            part_raw = part_raw .. kept
                            j = close + 1
                        else
                            part_raw, part_plain = part_raw .. rc, part_plain .. rc
                            j = j + 1
                        end
                    else
                        part_raw, part_plain = part_raw .. rc, part_plain .. rc
                        j = j + 1
                    end
                end
                flush_ruby(true)
                i = group.last + 1
            end
        else
            plain_char(c)
            i = i + 1
        end
    end
    flush_regular()
    return kara
end

return mangetsu_karaskel
