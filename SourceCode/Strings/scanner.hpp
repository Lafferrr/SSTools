#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <cstdint>

enum class RegionKind { Image, Mapped, Private, Unknown };

struct ExtractedString {
    std::wstring    text;
    uintptr_t       address;
    RegionKind      kind;
    std::wstring    mappedFile;
    bool            isUnicode;
};

struct ScanOptions {
    bool   scanImage = true;
    bool   scanMapped = true;
    bool   scanPrivate = true;
    bool   extractAscii = true;
    bool   extractUnicode = true;
    size_t minLength = 4;
    size_t chunkBytes = 4 * 1024 * 1024;
};

using StringCallback = std::function<void(const ExtractedString&)>;
using ProgressCallback = std::function<void(float)>;

class MemoryScanner {
public:
    MemoryScanner();
    ~MemoryScanner();

    std::vector<DWORD> FindByName(const wchar_t* name) const;
    bool               Open(DWORD pid);
    void               Close();

    void               BeginScan(ScanOptions opts, StringCallback onStr, ProgressCallback onProg);
    void               StopScan();

    bool               IsRunning()  const { return m_running.load(std::memory_order_relaxed); }
    DWORD              GetPid()     const { return m_pid; }
    std::wstring       GetImage()   const { return m_imagePath; }

private:
    HANDLE              m_handle;
    DWORD               m_pid;
    std::wstring        m_imagePath;
    std::atomic<bool>   m_running;
    std::atomic<bool>   m_stop;

    struct Region {
        uintptr_t   base;
        size_t      size;
        RegionKind  kind;
    };

    std::vector<Region> EnumerateRegions(const ScanOptions& opts) const;
    std::wstring        QueryMappedFile(uintptr_t base)           const;

    static void ExtractAscii(
        const uint8_t* buf, size_t bufLen,
        uintptr_t base, RegionKind kind,
        const std::wstring& file,
        size_t minLen,
        const StringCallback& cb);

    static void ExtractUnicode(
        const uint8_t* buf, size_t bufLen,
        uintptr_t base, RegionKind kind,
        const std::wstring& file,
        size_t minLen,
        const StringCallback& cb);

    void Worker(ScanOptions opts, StringCallback onStr, ProgressCallback onProg);
};