#pragma once
#include <string>
#include <vector>
#include <optional>

struct DetectionHit {
    std::wstring category;
    std::wstring matchedKeyword;
    std::wstring rawInput;
    bool         fuzzyMatch;
};

class StringDetector {
public:
    StringDetector();
    std::optional<DetectionHit> Analyze(const std::wstring& input) const;

private:
    struct Entry {
        std::wstring raw;
        std::wstring normalized;
        std::wstring category;
    };

    std::vector<Entry> m_entries;

    std::wstring Normalize(const std::wstring& s) const;
    void         AddKeywords(const wchar_t* const* arr, size_t n, const wchar_t* cat);
};