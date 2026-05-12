#include "detector.hpp"
#include <algorithm>
#include <cwctype>

static const wchar_t* kPrimary[] = {
    L"attackRegisteredThisClick", L"findKnockbackSword", L"swapBackToOriginalSlot",
    L"anchor_macro",   L"anchormacro",  L"autocrystal",    L"auto_crystal",
    L"autodhand",      L"auto_dhand",   L"PLACE_DELAY",    L"onAnchorLoad",
    L"crystal_aura",   L"crystalaura",  L"axe_macro",      L"macro_axe",
    L"totem_hover",    L"auto_totem",   L"autototem",      L"autoxp",
    L"auto_xp",        L"prestige",     L"argon",
    L"autohitcrystal", L"inventorytotem", L"autopot",      L"autodoublehand",
    L"aimassist",      L"triggerbot",   L"pingspoof",      L"selfdestruct",
    L"lvstrng",        L"airplace",     L"place_chance",   L"stop_on_kill",
    L"auto_hit_crystal", L"auto_inventory_totem"
};
static const wchar_t* kClients[] = {
    L"wurst",   L"argon",         L"meteor",        L"doomsday",
    L"salhack", L"koneclient",    L"aristois",      L"liquidbounce",
    L"novoline"
};

static const wchar_t* kSuspicious[] = {
    L"autoclicker", L"auto clicker"
};

static const wchar_t* kModules[] = {
    L"AutoClicker", L"KillAura",  L"HighJump",   L"XRay",
    L"FastBreak",   L"FastAttack", L"AutoTotem", L"AutoCrystal",
    L"AutoArmor",   L"Hitboxes",   L"TriggerBot", L"FastPlace",
    L"ChestESP",    L"AutoMine",   L"WallHack",  L"AntiAim",
    L"boatfly",     L"SelfDestruct", L"panicmode", L"disablemodules"
};

StringDetector::StringDetector() {
    AddKeywords(kPrimary, std::size(kPrimary), L"Primary");
    AddKeywords(kClients, std::size(kClients), L"Cheat Client");
    AddKeywords(kSuspicious, std::size(kSuspicious), L"Suspicious Mod");
    AddKeywords(kModules, std::size(kModules), L"Cheat Module");
}

void StringDetector::AddKeywords(const wchar_t* const* arr, size_t n, const wchar_t* cat) {
    for (size_t i = 0; i < n; ++i) {
        Entry e;
        e.raw = arr[i];
        e.category = cat;
        e.normalized = Normalize(arr[i]);
        m_entries.push_back(std::move(e));
    }
}

std::wstring StringDetector::Normalize(const std::wstring& s) const {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        switch (c) {
        case L' ': case L'_': case L'-': case L'.':
        case L'/': case L'\\': case L'\'':
            continue;
        default: break;
        }
        wchar_t lc = static_cast<wchar_t>(std::towlower(c));
        switch (lc) {
        case L'0': lc = L'o'; break;
        case L'1': lc = L'l'; break;
        case L'3': lc = L'e'; break;
        case L'4': lc = L'a'; break;
        case L'5': lc = L's'; break;
        case L'7': lc = L't'; break;
        default:   break;
        }
        out += lc;
    }
    return out;
}

std::optional<DetectionHit> StringDetector::Analyze(const std::wstring& input) const {
    if (input.size() < 3) return std::nullopt;

    std::wstring normInput = Normalize(input);
    if (normInput.empty()) return std::nullopt;

    for (const auto& e : m_entries) {
        if (e.normalized.empty()) continue;
        if (normInput.find(e.normalized) != std::wstring::npos) {
            bool exact = false;
            std::wstring lRaw = e.raw;
            std::wstring lInput = input;
            std::transform(lRaw.begin(), lRaw.end(), lRaw.begin(), ::towlower);
            std::transform(lInput.begin(), lInput.end(), lInput.begin(), ::towlower);
            exact = (lInput.find(lRaw) != std::wstring::npos);

            return DetectionHit{
                e.category,
                e.raw,
                input,
                !exact
            };
        }
    }
    return std::nullopt;
}