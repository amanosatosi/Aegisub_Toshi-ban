-- Standalone unit regression for mangetsu-karaskel.lua (Lua 5.1+).
-- Run from repository root: lua tests/lua/mangetsu-karaskel-test.lua
local adapter = dofile("automation/include/mangetsu-karaskel.lua")
local function check(line, expected)
    local out = assert(adapter.parse_karaoke_data({text = line}), "expected native furigana")
    assert(#out == #expected, "unexpected syllable count: " .. #out)
    for i, e in ipairs(expected) do
        local s = out[i]
        assert(s.text_stripped == e[1], i .. ": text mismatch " .. s.text_stripped)
        assert(s.start_time == e[2], i .. ": start mismatch")
        assert(s.duration == e[3], i .. ": duration mismatch")
    end
    return out
end

-- An unclosed furigana timing remains active after '>'.
check("<噛|{\\k60}か>ん{\\k70}だ", {
    {"噛|<か", 0, 600}, {"ん", 0, 600}, {"だ", 600, 700},
})
-- Each bracketed native group forces Karaskel's < (break+spillback) mode.
check("<水|{\\k30}みず><鏡|{\\k40}かがみ>", {
    {"水|<みず", 0, 300}, {"鏡|<かがみ", 300, 400},
})
-- Ruby can span several timed pieces: only the first owns the base.
check("<病|{\\k30}や{\\k26}ま{\\k10}い>", {
    {"病|<や", 0, 300}, {"#|ま", 300, 260}, {"#|い", 560, 100},
})
-- Untimed base group inherits outer karaoke.
check("{\\k50}<love|ai>", {{"love|<ai", 0, 500}})
-- No native furi: leave stock/legacy parsing fully untouched.
assert(adapter.parse_karaoke_data({text="漢字|<かな"}) == nil)
assert(adapter.parse_karaoke_data({text="{\\k20}ordinary"}) == nil)
-- Gyaku-furi not supported by this phase; leave it to the stock parser.
assert(adapter.parse_karaoke_data({text="<漢字|かな|KANJI>"}) == nil)
print("Mangetsu Karaskel adapter regressions passed")
