#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>

#include <algorithm>
#include <thread>
#include <vector>

#include "scanner.hpp"

MemoryScanner::MemoryScanner()
    : m_handle(nullptr), m_pid(0), m_running(false), m_stop(false)
{
}

MemoryScanner::~MemoryScanner() {
    StopScan();
    Close();
}

std::vector<DWORD> MemoryScanner::FindByName(const wchar_t* name) const {
    std::vector<DWORD> result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0)
                result.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return result;
}

bool MemoryScanner::Open(DWORD pid) {
    Close();
    m_handle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!m_handle) return false;
    m_pid = pid;
    wchar_t buf[MAX_PATH]{};
    DWORD sz = MAX_PATH;
    QueryFullProcessImageNameW(m_handle, 0, buf, &sz);
    m_imagePath = buf;
    return true;
}

void MemoryScanner::Close() {
    if (m_handle) { CloseHandle(m_handle); m_handle = nullptr; }
    m_pid = 0;
    m_imagePath.clear();
}

std::wstring MemoryScanner::QueryMappedFile(uintptr_t base) const {
    wchar_t buf[MAX_PATH]{};
    if (GetMappedFileNameW(m_handle, reinterpret_cast<LPVOID>(base), buf, MAX_PATH) == 0)
        return {};
    std::wstring full(buf);
    auto pos = full.rfind(L'\\');
    return (pos != std::wstring::npos) ? full.substr(pos + 1) : full;
}

std::vector<MemoryScanner::Region> MemoryScanner::EnumerateRegions(const ScanOptions& opts) const {
    std::vector<Region> out;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0;

    while (VirtualQueryEx(m_handle, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.State == MEM_COMMIT &&
            !(mbi.Protect & PAGE_NOACCESS) &&
            !(mbi.Protect & PAGE_GUARD))
        {
            RegionKind kind = RegionKind::Unknown;
            if (mbi.Type == MEM_IMAGE && opts.scanImage)   kind = RegionKind::Image;
            else if (mbi.Type == MEM_MAPPED && opts.scanMapped)  kind = RegionKind::Mapped;
            else if (mbi.Type == MEM_PRIVATE && opts.scanPrivate) kind = RegionKind::Private;

            if (kind != RegionKind::Unknown)
                out.push_back({ reinterpret_cast<uintptr_t>(mbi.BaseAddress), mbi.RegionSize, kind });
        }
        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }
    return out;
}

void MemoryScanner::ExtractAscii(
    const uint8_t* buf, size_t bufLen,
    uintptr_t base, RegionKind kind,
    const std::wstring& file,
    size_t minLen,
    const StringCallback& cb)
{
    size_t start = SIZE_MAX;
    for (size_t i = 0; i <= bufLen; ++i) {
        uint8_t b = (i < bufLen) ? buf[i] : 0;
        bool printable = (b >= 0x20 && b <= 0x7E);
        if (printable) {
            if (start == SIZE_MAX) start = i;
        }
        else {
            if (start != SIZE_MAX) {
                size_t len = i - start;
                if (len >= minLen) {
                    ExtractedString es;
                    es.text.assign(buf + start, buf + i);
                    es.address = base + start;
                    es.kind = kind;
                    es.mappedFile = file;
                    es.isUnicode = false;
                    cb(es);
                }
                start = SIZE_MAX;
            }
        }
    }
}

void MemoryScanner::ExtractUnicode(
    const uint8_t* buf, size_t bufLen,
    uintptr_t base, RegionKind kind,
    const std::wstring& file,
    size_t minLen,
    const StringCallback& cb)
{
    if (bufLen < 2) return;
    size_t start = SIZE_MAX;
    size_t limit = bufLen - (bufLen % 2);

    for (size_t i = 0; i <= limit; i += 2) {
        wchar_t wc = 0;
        if (i < limit) {
            wc = static_cast<wchar_t>(buf[i] | (static_cast<uint16_t>(buf[i + 1]) << 8));
        }
        bool printable = (wc >= 0x0020 && wc <= 0x007E) || (wc >= 0x00A0 && wc < 0xD800);
        if (printable) {
            if (start == SIZE_MAX) start = i;
        }
        else {
            if (start != SIZE_MAX) {
                size_t charCount = (i - start) / 2;
                if (charCount >= minLen) {
                    ExtractedString es;
                    es.text.resize(charCount);
                    for (size_t c = 0; c < charCount; ++c)
                        es.text[c] = static_cast<wchar_t>(buf[start + c * 2] | (static_cast<uint16_t>(buf[start + c * 2 + 1]) << 8));
                    es.address = base + start;
                    es.kind = kind;
                    es.mappedFile = file;
                    es.isUnicode = true;
                    cb(es);
                }
                start = SIZE_MAX;
            }
        }
    }
}

void MemoryScanner::Worker(ScanOptions opts, StringCallback onStr, ProgressCallback onProg) {
    m_running = true;
    m_stop = false;

    auto regions = EnumerateRegions(opts);

    size_t totalMem = 0;
    for (auto& reg : regions)
        totalMem += reg.size;
    size_t scannedMem = 0;

    for (auto& reg : regions) {
        if (m_stop.load(std::memory_order_relaxed)) break;

        std::wstring file = QueryMappedFile(reg.base);

        uintptr_t cursor = reg.base;
        uintptr_t end = reg.base + reg.size;

        while (cursor < end) {
            if (m_stop.load(std::memory_order_relaxed)) break;

            size_t wanted = std::min(opts.chunkBytes, end - cursor);
            std::vector<uint8_t> chunk(wanted);
            SIZE_T read = 0;

            if (ReadProcessMemory(m_handle, reinterpret_cast<LPCVOID>(cursor),
                chunk.data(), wanted, &read) && read > 0)
            {
                if (opts.extractAscii)
                    ExtractAscii(chunk.data(), read, cursor, reg.kind, file, opts.minLength, onStr);
                if (opts.extractUnicode)
                    ExtractUnicode(chunk.data(), read, cursor, reg.kind, file, opts.minLength, onStr);
            }
            scannedMem += wanted;
            cursor += wanted;

            if (onProg && totalMem > 0)
                onProg(static_cast<float>(scannedMem) / static_cast<float>(totalMem));
        }
    }

    m_running = false;
    if (onProg) onProg(1.0f);
}

void MemoryScanner::BeginScan(ScanOptions opts, StringCallback onStr, ProgressCallback onProg) {
    if (m_running.load()) return;
    std::thread([this, opts, onStr, onProg]() {
        Worker(opts, onStr, onProg);
        }).detach();
}

void MemoryScanner::StopScan() {
    m_stop = true;
}