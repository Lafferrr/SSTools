#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#define _CRT_SECURE_NO_WARNINGS
#define PSAPI_VERSION 2
#define GDIPVER 0x0110

#include <windows.h>
#include <winioctl.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wbemidl.h>
#include <comdef.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <winreg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <gdiplus.h>
#include <winternl.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <memory>
#include <queue>
#include <cwchar>
#include <map>
#include <array>
#include <condition_variable>
#include <chrono>
#include <sstream>
#include <functional>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "version.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef IDR_APPICON
#define IDR_APPICON 101
#endif
#include "resource.h"

#define IDC_PROGRESS   1001
#define IDC_PHASE      1002
#define IDC_CURRENT    1003
#define IDC_LISTVIEW   1004
#define IDC_BTN_START  1005
#define IDC_BTN_REPORT 1006
#define IDC_STAT_FILES 1008
#define IDC_STAT_FOUND 1009
#define IDC_STAT_CRIT  1010
#define IDC_STAT_HIGH  1011
#define IDC_STAT_MED   1012
#define IDC_STAT_LOW   1013
#define IDC_PCT_LABEL  1014
#define TIMER_DRAIN    1

#define WM_SCAN_DONE       (WM_APP+3)
#define WM_PHASE_PROGRESS  (WM_APP+4)

#define COL_RISK  0
#define COL_NAME  1
#define COL_PATH  2
#define COL_KW    3
#define COL_MOD   4
#define COL_SCORE 5

static const COLORREF CB = RGB(13, 17, 23);
static const COLORREF CP = RGB(22, 27, 34);
static const COLORREF CT = RGB(10, 14, 20);
static const COLORREF CTX = RGB(230, 237, 243);
static const COLORREF CD = RGB(110, 118, 129);
static const COLORREF CA = RGB(88, 166, 255);
static const COLORREF CBR = RGB(33, 38, 45);
static const COLORREF CBO = RGB(33, 38, 45);
static const COLORREF CBH = RGB(48, 54, 61);
static const COLORREF CCR = RGB(248, 81, 73);
static const COLORREF CHI = RGB(255, 148, 0);
static const COLORREF CME = RGB(255, 212, 0);
static const COLORREF CLW = RGB(63, 185, 80);
static const COLORREF CAR = RGB(18, 22, 29);
static const COLORREF CPR = RGB(88, 166, 255);
static const int TH = 44;
static const int BS = 5;

static const wchar_t* const HARD_WL[] = {
    L"\\AppData\\Roaming\\Code\\User\\History\\",
    L"\\AppData\\Roaming\\Windsurf\\User\\History\\",
    L"\\AppData\\Roaming\\Python\\Python314\\site-packages\\",
    L"\\.vscode\\extensions\\ms-python.debugpy-",
    L"\\.cursor\\extensions\\ms-python.python-",
    L"\\.cursor\\extensions\\ms-python.debugpy-",
    L"\\python_files\\lib\\jedilsp\\",
    L"\\bundled\\libs\\debugpy\\_vendored\\pydevd\\",
    L"\\AppData\\Roaming\\Code\\User\\History",
    L"\\AppData\\Roaming\\Windsurf\\User\\History",
    L"\\AppData\\Roaming\\Python\\Python314\\site-packages",
    L"\\.vscode\\extensions\\ms-python.debugpy",
    L"\\.cursor\\extensions\\ms-python.python",
    L"\\.cursor\\extensions\\ms-python.debugpy",
    L"\\Lib\\test\\",
    L"\\Lib\\idlelib\\",
    L"\\Lib\\_pyrepl\\",
    L"\\Lib\\test\\support\\",
};

struct ACNode {
    std::unordered_map<char, std::unique_ptr<ACNode>> ch;
    ACNode* fail = nullptr;
    std::vector<std::string> outputs;
};

class AhoCorasick {
    std::unique_ptr<ACNode> root = std::make_unique<ACNode>();
    static char norm(unsigned char c) { return (char)tolower(c); }
    void buildFail() {
        std::queue<ACNode*> q;
        for (auto& pair : root->ch) {
            pair.second->fail = root.get();
            q.push(pair.second.get());
        }
        while (!q.empty()) {
            ACNode* cur = q.front(); q.pop();
            for (auto& pair : cur->ch) {
                char c = pair.first;
                ACNode* child = pair.second.get();
                q.push(child);
                ACNode* f = cur->fail;
                while (f != root.get() && f->ch.find(c) == f->ch.end())
                    f = f->fail;
                if (f->ch.find(c) != f->ch.end())
                    f = f->ch[c].get();
                child->fail = f;
                child->outputs.insert(child->outputs.end(),
                    f->outputs.begin(), f->outputs.end());
            }
        }
    }
public:
    void addPattern(const std::string& s) {
        ACNode* cur = root.get();
        for (unsigned char c : s) {
            char nc = norm(c);
            auto& nx = cur->ch[nc];
            if (!nx) nx = std::make_unique<ACNode>();
            cur = nx.get();
        }
        cur->outputs.push_back(s);
    }
    void finalize() { buildFail(); }
    std::vector<std::string> match(const std::string& content) const {
        std::unordered_set<std::string> found;
        const ACNode* cur = root.get();
        for (unsigned char c : content) {
            char nc = norm(c);
            while (cur != root.get() && cur->ch.find(nc) == cur->ch.end())
                cur = cur->fail;
            if (cur->ch.find(nc) != cur->ch.end())
                cur = cur->ch.at(nc).get();
            if (!cur->outputs.empty())
                for (auto& o : cur->outputs) found.insert(o);
        }
        return std::vector<std::string>(found.begin(), found.end());
    }
};

struct KS { std::vector<std::string> kws; int spm = 3, base = 0; bool rm = false; std::shared_ptr<AhoCorasick> matcher; std::unordered_set<std::string> weak; };
struct SW { std::string name; int score = 5; };
struct PB { std::string pat; int score = 2; };
struct DCfg {
    int thr = 3, maxKb = 2048, firstKb = 64;
    std::vector<std::string> exts, portable, wl, excl, sproc;
    std::vector<SW> sw; std::unordered_map<std::string, KS> ks; std::vector<PB> pb;
    std::unordered_map<std::string, std::vector<std::string>> vf, vi;
};
enum class RT { File, Inst, Proc, Peri, Port, ActiveScript, Prefetch, DeletedScript, UsnJournal };
enum class RL { Lo, Me, Hi, Cr };
struct SR {
    RT type = RT::File; std::wstring name, path, cat, mod, pub, pid, cmd, info;
    std::vector<std::string> kws; int score = 0;
    RL risk() const { if (score >= 15) return RL::Cr; if (score >= 10) return RL::Hi; if (score >= 5) return RL::Me; return RL::Lo; }
    std::wstring rl() const { switch (risk()) { case RL::Cr: return L"CRITICAL"; case RL::Hi: return L"HIGH"; case RL::Me: return L"MEDIUM"; default: return L"LOW"; } }
    COLORREF rc() const { switch (risk()) { case RL::Cr: return CCR; case RL::Hi: return CHI; case RL::Me: return CME; default: return CLW; } }
};

static DCfg G;
static std::mutex GMX;
static std::vector<std::unique_ptr<SR>> GRES;
static std::mutex QMTX;
static std::queue<SR*> QUE;
static std::atomic_bool GCAN{ false };
static std::atomic_int GFCNT{ 0 }, GRCNT{ 0 };
static std::wstring GVENDOR;
static std::thread GTHR;
static bool GSCANNING = false;
static std::wstring GREP = L"C:\\SSTools\\MacroScanner\\Results.txt";
static std::wstring GOUR_EXE;

static HWND GW = nullptr, GLV = nullptr, GPR = nullptr, GPH = nullptr, GCU = nullptr;
static HWND GBS = nullptr, GBR = nullptr;
static HWND GSF = nullptr, GSFo = nullptr, GSC = nullptr, GSH = nullptr, GSM = nullptr, GSL = nullptr, GPCT = nullptr;
static HBRUSH GBG = nullptr, GPAN = nullptr, GTIT = nullptr, GSEP = nullptr, GBGCARD = nullptr;
static HFONT GFU = nullptr, GFB = nullptr, GFS = nullptr, GFM = nullptr, GFT = nullptr, GFSM = nullptr;
static bool GMINH = false, GCLH = false;
static int gBaseProgress = 0;
static int gStatY = 0;
static std::wstring gPhaseText;

struct JVal;
using JArr = std::vector<JVal>;
using JObj = std::unordered_map<std::string, JVal>;
struct JVal {
    enum T { N, B, Num, S, A, O } t = N;
    bool b = false; double n = 0; std::string s;
    std::shared_ptr<JArr> a; std::shared_ptr<JObj> o;
    bool isNull() const { return t == N; }
    int asInt() const { return (int)n; }
    bool asBool() const { return b; }
    const std::string& asStr() const { return s; }
    const JArr& asArr() const { static JArr e; return a ? *a : e; }
    bool has(const std::string& k) const { return o && o->count(k); }
    const JVal& operator[](const std::string& k) const {
        static JVal nv; if (!o) return nv; auto it = o->find(k); return it != o->end() ? it->second : nv;
    }
};
struct JP {
    const char* p, * e;
    JP(const std::string& s) : p(s.c_str()), e(s.c_str() + s.size()) {}
    void sk() { while (p < e && (unsigned char)*p <= 32) ++p; }
    JVal pa() {
        sk(); if (p >= e) return{}; if (*p == '"') return ps(); if (*p == '{') return po();
        if (*p == '[') return pv(); if (*p == 't') { p += 4; JVal v; v.t = JVal::B; v.b = true; return v; }
        if (*p == 'f') { p += 5; JVal v; v.t = JVal::B; return v; } if (*p == 'n') { p += 4; return{}; } return pn();
    }
    JVal ps() {
        ++p; std::string s; while (p < e && *p != '"') {
            if (*p == '\\') { ++p; if (*p == 'n') s += '\n'; else if (*p == 't') s += '\t'; else if (*p == 'r') s += '\r'; else s += *p; }
            else s += *p; ++p;
        }
        if (p < e) ++p; JVal v; v.t = JVal::S; v.s = std::move(s); return v;
    }
    JVal pn() { char* x; double d = strtod(p, &x); p = x; JVal v; v.t = JVal::Num; v.n = d; return v; }
    JVal pv() {
        ++p; auto a = std::make_shared<JArr>(); sk(); while (p < e && *p != ']') { a->push_back(pa()); sk(); if (p < e && *p == ',') ++p; sk(); }
        if (p < e) ++p; JVal v; v.t = JVal::A; v.a = std::move(a); return v;
    }
    JVal po() {
        ++p; auto o = std::make_shared<JObj>(); sk(); while (p < e && *p != '}') {
            JVal k = ps(); sk(); if (p < e && *p == ':') ++p; JVal val = pa(); (*o)[k.s] = std::move(val); sk();
            if (p < e && *p == ',') ++p; sk();
        } if (p < e) ++p; JVal v; v.t = JVal::O; v.o = std::move(o); return v;
    }
};
static JVal PJ(const std::string& s) { JP p(s); return p.pa(); }

static std::string TLA(const std::string& s) { std::string r = s; for (char& c : r) c = (char)tolower((unsigned char)c); return r; }
static std::wstring TLW(const std::wstring& s) { std::wstring r = s; for (wchar_t& c : r) c = (wchar_t)towlower(c); return r; }
static bool CCI(const std::string& h, const std::string& n) {
    if (n.empty()) return true;
    return std::search(h.begin(), h.end(), n.begin(), n.end(), [](char a, char b) { return tolower((unsigned char)a) == tolower((unsigned char)b); }) != h.end();
}
static bool CCIW(const std::wstring& h, const std::wstring& n) {
    if (n.empty()) return true;
    return std::search(h.begin(), h.end(), n.begin(), n.end(), [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); }) != h.end();
}
static std::wstring A2W(const std::string& s) { if (s.empty()) return{}; int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0); std::wstring r(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), r.data(), n); return r; }
static std::string W2A(const std::wstring& s) { if (s.empty()) return{}; int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr); std::string r(n, 0); WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), r.data(), n, nullptr, nullptr); return r; }
static std::wstring EE(const std::wstring& p) { wchar_t b[4096]; DWORD n = ExpandEnvironmentStringsW(p.c_str(), b, 4096); return (n && n <= 4096) ? b : p; }
static std::string GEX(const std::wstring& p) { size_t d = p.rfind(L'.'); return (d == std::wstring::npos) ? "" : TLA(W2A(p.substr(d))); }
static std::wstring GFN(const std::wstring& p) { size_t x = p.find_last_of(L"\\/"); return (x == std::wstring::npos) ? p : p.substr(x + 1); }
static std::wstring GetFileStem(const std::wstring& p) { std::wstring f = GFN(p); size_t d = f.rfind(L'.'); return (d == std::wstring::npos) ? f : f.substr(0, d); }

static bool QuickExcl(const std::wstring& p) {
    std::wstring lo = TLW(p);
    for (auto& e : HARD_WL) if (lo.find(TLW(e)) != std::wstring::npos) return true;
    return false;
}

static DCfg LoadCfg(const std::string& j) {
    DCfg c; JVal r = PJ(j); if (r.isNull()) return c;
    auto& d = r["detection"];
    if (!d.isNull()) {
        if (d.has("scoreThreshold")) c.thr = d["scoreThreshold"].asInt();
        if (d.has("maxFileSizeKb")) c.maxKb = d["maxFileSizeKb"].asInt();
        if (d.has("firstBytesKb")) c.firstKb = d["firstBytesKb"].asInt();
    }
    for (auto& e : r["extensions"].asArr()) c.exts.push_back(e.asStr());
    for (auto& e : r["portableNamePatterns"].asArr()) c.portable.push_back(e.asStr());
    for (auto& e : r["fileWhitelist"].asArr()) c.wl.push_back(e.asStr());
    for (auto& e : r["pathExclusions"].asArr()) c.excl.push_back(e.asStr());
    for (auto& e : r["suspiciousProcessNames"].asArr()) c.sproc.push_back(e.asStr());
    for (auto& e : r["installedSoftwareTargets"].asArr()) { SW s; s.name = e["name"].asStr(); s.score = e["score"].asInt(); c.sw.push_back(std::move(s)); }
    for (auto& e : r["pathBonuses"].asArr()) { PB p; p.pat = e["pattern"].asStr(); p.score = e["score"].asInt(); c.pb.push_back(std::move(p)); }
    if (r["keywordSets"].t == JVal::O && r["keywordSets"].o)
        for (auto& kv : *r["keywordSets"].o) {
            std::string key = kv.first;
            KS ks; for (auto& w : kv.second["keywords"].asArr()) ks.kws.push_back(w.asStr());
            if (kv.second.has("scorePerMatch")) ks.spm = kv.second["scorePerMatch"].asInt();
            if (kv.second.has("baseScore")) ks.base = kv.second["baseScore"].asInt();
            if (kv.second.has("requireMultiple")) ks.rm = kv.second["requireMultiple"].asBool();
            c.ks[key] = std::move(ks);
        }
    if (r["vendorFolders"].t == JVal::O && r["vendorFolders"].o)
        for (auto& kv : *r["vendorFolders"].o) { std::vector<std::string> ps; for (auto& p : kv.second.asArr()) ps.push_back(p.asStr()); c.vf[kv.first] = std::move(ps); }
    if (r["vendorIndicators"].t == JVal::O && r["vendorIndicators"].o)
        for (auto& kv : *r["vendorIndicators"].o) { std::vector<std::string> is; for (auto& i : kv.second.asArr()) is.push_back(i.asStr()); c.vi[kv.first] = std::move(is); }
    return c;
}

static void ApplyFallbackExts(DCfg& c) {
    if (!c.exts.empty()) return;
    for (auto* e : { ".ahk",".au3",".py",".pyw",".js",".amc2",".cuecfg",".mcr",".kbs" }) c.exts.push_back(e);
    if (c.ks.find(".py") == c.ks.end()) {
        KS k; k.spm = 3; k.base = 0; k.rm = false;
        for (auto* w : { "pyautogui","pydirectinput","pynput","from pynput","mouseCtrl","kbCtrl",".press(",".release(","keyboard.press","keyboard.release","keyboard.send","keyboard.write","mouse.click","mouse.move","mouse.press","mouse.release","mouse.wheel","SendInput","keybd_event","mouse_event","SetCursorPos","GetAsyncKeyState","ctypes.windll","ctypes.windll.user32","win32api","win32con","windll","WM_KEYDOWN","WM_KEYUP","WM_LBUTTONDOWN","preciseSleep" }) k.kws.push_back(w);
        c.ks[".py"] = k; c.ks[".pyw"] = k;
    }
    if (c.ks.find(".ahk") == c.ks.end()) {
        KS k; k.spm = 3; k.base = 3; k.rm = false;
        for (auto* w : { "Send","SendInput","Click","MouseClick","Loop","Sleep","GetKeyState","Hotkey","SetMouseDelay","SetKeyDelay","DllCall","ControlClick","WinActivate","BlockInput","SetTimer","#InstallKeybdHook" }) k.kws.push_back(w);
        c.ks[".ahk"] = k;
    }
    if (c.ks.find(".au3") == c.ks.end()) {
        KS k; k.spm = 3; k.base = 3; k.rm = false;
        for (auto* w : { "MouseClick","MouseMove","Send","ControlSend","WinActivate","Sleep","HotKeySet","BlockInput","DllCall" }) k.kws.push_back(w);
        c.ks[".au3"] = k;
    }
    if (c.ks.find(".js") == c.ks.end()) {
        KS k; k.spm = 2; k.base = 0; k.rm = true;
        for (auto* w : { "robotjs","robot.moveMouse","robot.keyTap","nutjs","keyboard.type","keyboard.pressKey","iohook","uiohook-napi","SendInput" }) k.kws.push_back(w);
        c.ks[".js"] = k;
    }
    if (c.portable.empty())
        for (auto* p : { "macro","autoclick","auto_click","autoclicker","bhop","bunny","triggerbot","clicker","keypress","hotkey","rapidfire","spammer","inputbot","mousebot","autofire","jitter" })
            c.portable.push_back(p);
    if (c.pb.empty()) {
        std::pair<const char*, int> arr[] = { {"Desktop", 2}, {"Downloads", 2}, {"Startup", 4}, {"Start Menu", 4}, {"Documents", 1} };
        for (int i = 0; i < 5; i++) c.pb.push_back({ arr[i].first, arr[i].second });
    }
}

static void PrepareKeywordSets(DCfg& c) {
    for (auto& kv : c.ks) {
        auto& ks = kv.second;
        auto ac = std::make_shared<AhoCorasick>();
        for (auto& w : ks.kws) ac->addPattern(w);
        ac->finalize();
        ks.matcher = ac;
        if (kv.first == ".py" || kv.first == ".pyw")
            for (auto* w : { ".release(", "windll", "ctypes.windll", "pynput", "win32con", "win32api", "from pynput" }) ks.weak.insert(w);
    }
}

static std::string LdEmbed() {
    HRSRC h = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_CONFIG), L"RCDATA"); if (!h) return{};
    HGLOBAL g = LoadResource(nullptr, h); if (!g) return{};
    void* p = LockResource(g); DWORD sz = SizeofResource(nullptr, h);
    return (p && sz) ? std::string((char*)p, sz) : std::string{};
}

static bool IsWL(const std::wstring& p, const DCfg& c) {
    if (QuickExcl(p)) return true;
    std::wstring lo = TLW(p);
    for (auto& w : c.wl) { std::wstring ww = TLW(A2W(w)); if (lo.find(ww) != std::wstring::npos) return true; }
    return false;
}
static bool IsEx(const std::wstring& p, const DCfg& c) { std::wstring lo = TLW(p); for (auto& e : c.excl) if (CCIW(lo, A2W(e))) return true; return false; }
static int PB2(const std::wstring& p, const DCfg& c) { int b = 0; for (auto& pb : c.pb) if (CCIW(p, A2W(pb.pat))) b += pb.score; return b; }

static std::vector<std::string> MKW(const std::string& cnt, const KS& ks) {
    if (!ks.matcher) return {};
    std::string lc = TLA(cnt);
    return ks.matcher->match(lc);
}

static std::string RFB(const std::wstring& p, int mb) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return{};
    std::string buf(mb, '\0'); DWORD rd = 0; ReadFile(h, buf.data(), mb, &rd, nullptr); CloseHandle(h); buf.resize(rd); return buf;
}
static std::wstring FFT(FILETIME ft) {
    FILETIME lft; FileTimeToLocalFileTime(&ft, &lft); SYSTEMTIME st; FileTimeToSystemTime(&lft, &st);
    wchar_t b[32]; swprintf_s(b, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute); return b;
}
static std::wstring ECat(const std::string& ext) {
    if (ext == ".ahk") return L"AutoHotkey Script"; if (ext == ".au3") return L"AutoIt Script";
    if (ext == ".pyw") return L"Python Window Script"; if (ext == ".py") return L"Python Script";
    if (ext == ".js") return L"JavaScript Automation";
    return L"Automation Script";
}

static void QR(SR* r) { std::lock_guard<std::mutex> lk(QMTX); QUE.push(r); }

static std::unique_ptr<SR> AF(const std::wstring& fp, LARGE_INTEGER fsz, FILETIME lw, const DCfg& c) {
    if (fp == GOUR_EXE) return nullptr;
    std::string ext = GEX(fp); if (ext.empty()) return nullptr;
    bool custom = (ext == ".amc2" || ext == ".cuecfg" || ext == ".mcr" || ext == ".kbs");
    if (IsWL(fp, c)) return nullptr;
    if (c.ks.find(ext) == c.ks.end() && !custom) return nullptr;
    if (fsz.QuadPart > (LONGLONG)c.maxKb * 1024) return nullptr;
    std::string cnt = RFB(fp, c.firstKb * 1024);
    auto it = c.ks.find(ext); const KS* ks = (it != c.ks.end()) ? &it->second : nullptr;
    if (cnt.empty()) {
        int bs = (ks ? ks->base : 0) + PB2(fp, c);
        if (custom) bs = std::max(bs, c.thr);
        if (bs < c.thr) return nullptr;
        auto r = std::make_unique<SR>(); r->type = RT::File; r->name = GFN(fp); r->path = fp; r->cat = ECat(ext); r->mod = FFT(lw);
        r->kws = { "File present (unreadable)" }; r->score = bs; return r;
    }
    std::vector<std::string> matched; if (ks) matched = MKW(cnt, *ks);
    if (ks && !ks->weak.empty() && !matched.empty()) {
        bool hasNonWeak = false;
        for (auto& m : matched) if (ks->weak.find(m) == ks->weak.end()) { hasNonWeak = true; break; }
        if (!hasNonWeak) return nullptr;
    }
    if (matched.size() == 1 && TLA(matched[0]) == "input") return nullptr;
    if (ks && ks->rm && matched.size() < 2) return nullptr;
    int sc = (ks ? ks->base : 0) + (int)matched.size() * (ks ? ks->spm : 0) + PB2(fp, c);
    if (custom) sc = std::max(sc, c.thr); if (sc < c.thr) return nullptr;
    auto r = std::make_unique<SR>(); r->type = RT::File; r->name = GFN(fp); r->path = fp; r->cat = ECat(ext); r->mod = FFT(lw);
    r->kws = matched; r->score = sc; return r;
}

static std::unique_ptr<SR> AE(const std::wstring& fp, const DCfg& c) {
    if (fp == GOUR_EXE || IsWL(fp, c)) return nullptr;
    std::string stem = W2A(TLW(GetFileStem(fp))); std::string matched;
    for (auto& p : c.portable) if (CCI(stem, p)) { matched = p; break; }
    if (matched.empty()) return nullptr;
    int sc = 6 + PB2(fp, c); WIN32_FILE_ATTRIBUTE_DATA fa; GetFileAttributesExW(fp.c_str(), GetFileExInfoStandard, &fa);
    auto r = std::make_unique<SR>(); r->type = RT::Port; r->name = GFN(fp); r->path = fp;
    r->cat = L"Portable Macro Executable"; r->mod = FFT(fa.ftLastWriteTime); r->kws = { matched }; r->score = sc; return r;
}

static void ScanVendorFolder(const std::wstring& folder, const std::wstring& vendorName, const std::string& vendorKey, const DCfg& c) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    std::wstring mod;
    if (GetFileAttributesExW(folder.c_str(), GetFileExInfoStandard, &fa)) mod = FFT(fa.ftLastWriteTime);
    auto* folderRes = new SR;
    folderRes->type = RT::Peri;
    folderRes->name = vendorName + L" Folder";
    folderRes->path = folder;
    folderRes->cat = vendorName + L" Gaming Software";
    folderRes->mod = mod;
    folderRes->kws = { vendorKey + " software folder found" };
    folderRes->score = 6;
    QR(folderRes);
    if (GVENDOR.empty()) GVENDOR = vendorName;
    auto it = c.vi.find(vendorKey);
    if (it == c.vi.end()) return;
    const auto& indicators = it->second;
    std::function<void(const std::wstring&, int)> scanDir = [&](const std::wstring& dir, int depth) {
        if (depth > 2) return;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring full = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (depth < 2) scanDir(full, depth + 1);
            }
            else {
                std::wstring ext = TLW(GFN(full));
                size_t dot = ext.rfind(L'.');
                if (dot != std::wstring::npos) {
                    std::wstring e = ext.substr(dot);
                    if (e == L".json" || e == L".xml" || e == L".ini" || e == L".db" || e == L".cfg" || e == L".lua") {
                        std::string content = RFB(full, 65536);
                        if (!content.empty()) {
                            int hitCount = 0;
                            for (auto& ind : indicators) if (CCI(content, ind)) ++hitCount;
                            if (hitCount > 0) {
                                auto* fileRes = new SR;
                                fileRes->type = RT::Peri;
                                fileRes->name = fd.cFileName;
                                fileRes->path = full;
                                fileRes->cat = vendorName + L" Core Profile";
                                fileRes->mod = FFT(fd.ftLastWriteTime);
                                fileRes->kws = { vendorKey + " folder detected" };
                                std::string kwList;
                                for (auto& ind : indicators) if (CCI(content, ind)) {
                                    if (!kwList.empty()) kwList += ", ";
                                    kwList += ind;
                                }
                                fileRes->kws.push_back(kwList);
                                fileRes->score = 8 + std::min(hitCount, 3);
                                QR(fileRes);
                            }
                        }
                    }
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        };
    scanDir(folder, 0);
}

static std::vector<std::wstring> GetBasePaths() {
    std::vector<std::wstring> bases;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) if (mask & (1 << i)) {
        wchar_t root[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
        if (GetDriveTypeW(root) == DRIVE_FIXED) bases.push_back(root);
    }
    for (auto* env : { L"%ProgramFiles%", L"%ProgramFiles(x86)%", L"%CommonProgramFiles%", L"%CommonProgramFiles(x86)%",
                       L"%LocalAppData%", L"%AppData%", L"%ProgramData%" }) {
        std::wstring p = EE(env);
        if (!p.empty() && p != env) bases.push_back(p);
    }
    wchar_t buf[MAX_PATH];
    if (SHGetSpecialFolderPathW(nullptr, buf, CSIDL_DESKTOPDIRECTORY, FALSE)) bases.push_back(buf);
    if (SHGetSpecialFolderPathW(nullptr, buf, CSIDL_PROFILE, FALSE)) {
        std::wstring dl = std::wstring(buf) + L"\\Downloads";
        if (GetFileAttributesW(dl.c_str()) != INVALID_FILE_ATTRIBUTES) bases.push_back(dl);
    }
    return bases;
}

static void FindVendorFolderSmart(const std::wstring& target, std::vector<std::wstring>& out) {
    auto bases = GetBasePaths();
    for (auto& base : bases) {
        std::wstring full = base + L"\\" + target;
        DWORD attr = GetFileAttributesW(full.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            out.push_back(full);
        }
    }
    if (!out.empty()) return;
    std::function<void(const std::wstring&, int)> recSearch = [&](const std::wstring& rootPath, int maxDepth) {
        if (maxDepth <= 0) return;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW((rootPath + L"\\*").c_str(), FindExInfoBasic, &fd,
            FindExSearchLimitToDirectories, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (_wcsicmp(fd.cFileName, target.c_str()) == 0) {
                out.push_back(rootPath + L"\\" + fd.cFileName);
            }
            if (maxDepth > 1) recSearch(rootPath + L"\\" + fd.cFileName, maxDepth - 1);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        };
    for (auto* root : { L"C:\\", L"D:\\", L"E:\\" }) {
        if (GetDriveTypeW(root) == DRIVE_FIXED) recSearch(root, 6);
    }
    for (auto* env : { L"%ProgramFiles%", L"%ProgramFiles(x86)%" }) {
        std::wstring pf = EE(env);
        if (!pf.empty() && pf != env) recSearch(pf, 6);
    }
}

static void RunPeri(const DCfg& c, std::atomic_bool& cancel) {
    std::vector<std::wstring> mice, kbds;
    IWbemLocator* pL = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void**)&pL))) {
        IWbemServices* pS = nullptr;
        if (SUCCEEDED(pL->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pS))) {
            CoSetProxyBlanket(pS, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
            IEnumWbemClassObject* pE = nullptr;
            if (SUCCEEDED(pS->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT Name FROM Win32_PointingDevice"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pE))) {
                IWbemClassObject* pO; ULONG ret;
                while (pE->Next(WBEM_INFINITE, 1, &pO, &ret) == WBEM_S_NO_ERROR) {
                    VARIANT v; if (SUCCEEDED(pO->Get(L"Name", 0, &v, nullptr, nullptr))) { if (v.vt == VT_BSTR && v.bstrVal) mice.push_back(v.bstrVal); VariantClear(&v); }
                    pO->Release();
                }
                pE->Release();
            }
            if (SUCCEEDED(pS->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT Name FROM Win32_Keyboard"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pE))) {
                IWbemClassObject* pO; ULONG ret;
                while (pE->Next(WBEM_INFINITE, 1, &pO, &ret) == WBEM_S_NO_ERROR) {
                    VARIANT v; if (SUCCEEDED(pO->Get(L"Name", 0, &v, nullptr, nullptr))) { if (v.vt == VT_BSTR && v.bstrVal) kbds.push_back(v.bstrVal); VariantClear(&v); }
                    pO->Release();
                }
                pE->Release();
            }
            pS->Release();
        }
        pL->Release();
    }
    auto processVendor = [&](const std::wstring& vendorName, const std::string& vendorKey, const std::vector<std::string>& patterns) {
        for (auto& pe : patterns) {
            if (cancel) return;
            std::vector<std::wstring> dirs;
            if (pe.size() > 18 && pe.substr(0, 18) == "SEARCH_FOR_FOLDER:") {
                std::wstring fn = A2W(pe.substr(18));
                FindVendorFolderSmart(fn, dirs);
            }
            else {
                std::wstring exp = EE(A2W(pe));
                if (GetFileAttributesW(exp.c_str()) != INVALID_FILE_ATTRIBUTES)
                    dirs.push_back(exp);
            }
            for (auto& dir : dirs) {
                if (cancel) break;
                std::wstring dev;
                for (auto& n : mice) if (CCIW(n, vendorName)) { dev = n; break; }
                if (dev.empty()) for (auto& n : kbds) if (CCIW(n, vendorName)) { dev = n; break; }
                if (!dev.empty() && GVENDOR.empty()) GVENDOR = dev;
                ScanVendorFolder(dir, vendorName, vendorKey, c);
            }
        }
        };
    for (auto& kv : c.vf) {
        if (cancel) break;
        processVendor(A2W(kv.first), kv.first, kv.second);
    }
    static const std::pair<std::wstring, std::wstring> hardcoded[] = {
        { L"Glorious", L"BY-COMBO2" }, { L"Razer", L"Razer" }, { L"Logitech", L"LGHUB" },
        { L"SteelSeries", L"SteelSeries Engine 3" }, { L"Corsair", L"Corsair" }, { L"ASUS", L"ASUS" }
    };
    for (int i = 0; i < sizeof(hardcoded) / sizeof(hardcoded[0]); ++i) {
        if (cancel) break;
        std::vector<std::wstring> dirs;
        FindVendorFolderSmart(hardcoded[i].second, dirs);
        for (auto& dir : dirs) ScanVendorFolder(dir, hardcoded[i].first, W2A(TLW(hardcoded[i].first)), c);
    }
}

static void RunReg(const DCfg& c, std::atomic_bool& cancel) {
    static const wchar_t* keys[] = { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall" };
    HKEY hives[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    std::unordered_set<std::wstring> seen;
    for (auto hive : hives) for (auto* kp : keys) {
        if (cancel) return;
        HKEY hK; if (RegOpenKeyExW(hive, kp, 0, KEY_READ, &hK) != ERROR_SUCCESS) continue;
        wchar_t name[512]; DWORD idx = 0;
        while (!cancel) {
            DWORD nL = ARRAYSIZE(name);
            if (RegEnumKeyExW(hK, idx++, name, &nL, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            HKEY hA; if (RegOpenKeyExW(hK, name, 0, KEY_READ, &hA) != ERROR_SUCCESS) continue;
            wchar_t dn[512]; DWORD dL = sizeof(dn);
            if (RegQueryValueExW(hA, L"DisplayName", nullptr, nullptr, (LPBYTE)dn, &dL) == ERROR_SUCCESS) {
                std::wstring dnW = dn; std::string dnA = W2A(dnW);
                for (auto& t : c.sw) if (CCI(dnA, t.name) && !seen.count(dnW)) {
                    seen.insert(dnW);
                    wchar_t loc[512]{}; DWORD lL = sizeof(loc); RegQueryValueExW(hA, L"InstallLocation", nullptr, nullptr, (LPBYTE)loc, &lL);
                    wchar_t pub[512]{}; DWORD pL = sizeof(pub); RegQueryValueExW(hA, L"Publisher", nullptr, nullptr, (LPBYTE)pub, &pL);
                    wchar_t ver[128]{}; DWORD vL = sizeof(ver); RegQueryValueExW(hA, L"DisplayVersion", nullptr, nullptr, (LPBYTE)ver, &vL);
                    auto* r = new SR; r->type = RT::Inst; r->name = dnW; r->path = loc; r->cat = L"Installed Macro Software";
                    r->pub = pub; r->info = ver[0] ? std::wstring(L"v") + ver : L"";
                    r->kws = { "Matched: " + t.name }; r->score = t.score; QR(r);
                }
            }
            RegCloseKey(hA);
        }
        RegCloseKey(hK);
    }
}

static void RunPort(const DCfg& c, std::atomic_bool& cancel) {
    static const int flds[] = { CSIDL_DESKTOPDIRECTORY, CSIDL_PERSONAL, CSIDL_PROFILE, CSIDL_APPDATA, CSIDL_LOCAL_APPDATA };
    std::unordered_set<std::wstring> sc;
    for (int f : flds) {
        if (cancel) break;
        wchar_t p[MAX_PATH]; if (!SHGetSpecialFolderPathW(nullptr, p, f, FALSE)) continue;
        std::wstring root = p; if (!sc.insert(root).second) continue;
        auto sd = [&](const std::wstring& dir) {
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((dir + L"\\*.exe").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                if (cancel) break; std::wstring fp = dir + L"\\" + fd.cFileName;
                if (!IsWL(fp, c) && !IsEx(fp, c)) { auto res = AE(fp, c); if (res) QR(res.release()); }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
            };
        if (f == CSIDL_PROFILE) { std::wstring dl = root + L"\\Downloads"; if (GetFileAttributesW(dl.c_str()) != INVALID_FILE_ATTRIBUTES) sd(dl); }
        sd(root);
    }
}

static bool MatchProcessVersionInfo(const std::wstring& exePath, const std::vector<std::string>& knownNames) {
    DWORD dummy;
    DWORD verSize = GetFileVersionInfoSizeW(exePath.c_str(), &dummy);
    if (verSize == 0) return false;
    std::vector<BYTE> verData(verSize);
    if (!GetFileVersionInfoW(exePath.c_str(), 0, verSize, verData.data())) return false;
    struct LANGANDCODEPAGE { WORD wLanguage; WORD wCodePage; } *lpTranslate;
    UINT cbTranslate;
    if (!VerQueryValueW(verData.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate))
        return false;
    for (UINT i = 0; i < cbTranslate / sizeof(LANGANDCODEPAGE); i++) {
        wchar_t subBlock[256];
        swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\FileDescription", lpTranslate[i].wLanguage, lpTranslate[i].wCodePage);
        wchar_t* value = nullptr; UINT len = 0;
        if (VerQueryValueW(verData.data(), subBlock, (LPVOID*)&value, &len) && value && len > 0) {
            std::string desc = W2A(value);
            for (auto& k : knownNames) if (!k.empty() && CCI(desc, k)) return true;
        }
        swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\ProductName", lpTranslate[i].wLanguage, lpTranslate[i].wCodePage);
        if (VerQueryValueW(verData.data(), subBlock, (LPVOID*)&value, &len) && value && len > 0) {
            std::string prod = W2A(value);
            for (auto& k : knownNames) if (!k.empty() && CCI(prod, k)) return true;
        }
        swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\OriginalFilename", lpTranslate[i].wLanguage, lpTranslate[i].wCodePage);
        if (VerQueryValueW(verData.data(), subBlock, (LPVOID*)&value, &len) && value && len > 0) {
            std::string orig = W2A(value);
            for (auto& k : knownNames) if (!k.empty() && CCI(orig, k)) return true;
        }
    }
    return false;
}

static void RunPrefetchScan(const DCfg& c, std::atomic_bool& cancel) {
    std::wstring prefetchDir = EE(L"%SystemRoot%\\Prefetch");
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileExW((prefetchDir + L"\\*.pf").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (cancel) break;
        std::wstring fname = fd.cFileName;
        size_t dash = fname.find(L'-');
        if (dash == std::wstring::npos) continue;
        std::wstring exePart = fname.substr(0, dash);
        std::string exePartA = W2A(TLW(exePart));
        static const std::vector<std::string> knownExes = {
            "autohotkey.exe", "autoit3.exe", "autoitv3.exe", "198macros.exe", "zenithmacros.exe",
            "tinytask.exe", "pulover.exe", "macrocreator.exe", "xdotool.exe", "sikuli.exe",
            "keymousego.exe", "xmousebuttoncontrol.exe", "rewasd.exe", "logitechgamingframework.exe",
            "razer synapse.exe", "steelseries engine 3.exe"
        };
        for (auto& exe : knownExes) {
            if (exePartA == exe) {
                auto* r = new SR;
                r->type = RT::Prefetch;
                r->name = exePart;
                r->path = prefetchDir + L"\\" + fname;
                r->cat = L"Prefetch Evidence";
                r->mod = FFT(fd.ftLastWriteTime);
                r->kws = { "Prefetch file for " + exe };
                r->score = 4;
                QR(r);
                break;
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

static void RunRecentFolderScan(const DCfg& c, std::atomic_bool& cancel) {
    wchar_t recentPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_RECENT, nullptr, 0, recentPath)))
        return;
    std::wstring recentDir = recentPath;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileExW((recentDir + L"\\*.lnk").c_str(), FindExInfoBasic, &fd,
        FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (cancel) break;
        std::wstring lnkPath = recentDir + L"\\" + fd.cFileName;
        IShellLinkW* psl;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
            IID_IShellLinkW, (void**)&psl))) {
            IPersistFile* ppf;
            if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf))) {
                if (SUCCEEDED(ppf->Load(lnkPath.c_str(), STGM_READ))) {
                    wchar_t target[MAX_PATH] = { 0 };
                    if (SUCCEEDED(psl->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH))) {
                        std::wstring targetPath(target);
                        std::wstring targetLower = TLW(targetPath);
                        if (targetLower.size() >= 4 &&
                            (targetLower.substr(targetLower.size() - 4) == L".ahk" ||
                                targetLower.substr(targetLower.size() - 4) == L".au3")) {
                            if (GetFileAttributesW(targetPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                                auto* r = new SR;
                                r->type = RT::DeletedScript;
                                r->name = GFN(targetPath);
                                r->path = targetPath;
                                r->cat = L"Deleted Script Evidence";
                                r->kws = { "Deleted script (shortcut exists, target missing)" };
                                r->score = 5;
                                QR(r);
                            }
                        }
                    }
                }
                ppf->Release();
            }
            psl->Release();
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

static void RunProcScanWMI(const DCfg& c, std::atomic_bool& cancel) {
    std::vector<std::string> knownNames;
    for (auto& sw : c.sw) knownNames.push_back(sw.name);
    for (auto& sp : c.sproc) knownNames.push_back(sp);
    knownNames.push_back("AutoHotkey"); knownNames.push_back("AutoIt"); knownNames.push_back("Pulover's Macro Creator");
    knownNames.push_back("198Macros"); knownNames.push_back("ZenithMacros");

    IWbemLocator* pLoc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void**)&pLoc)))
        return;
    IWbemServices* pSvc = nullptr;
    if (FAILED(pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pSvc))) { pLoc->Release(); return; }
    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    IEnumWbemClassObject* pEnum = nullptr;
    if (FAILED(pSvc->ExecQuery(_bstr_t(L"WQL"),
        _bstr_t(L"SELECT ExecutablePath, Name, CommandLine, ProcessId FROM Win32_Process"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnum))) {
        pSvc->Release(); pLoc->Release(); return;
    }
    IWbemClassObject* pObj = nullptr; ULONG uReturn = 0;
    while (pEnum) {
        HRESULT hr = pEnum->Next(WBEM_INFINITE, 1, &pObj, &uReturn);
        if (FAILED(hr) || uReturn == 0) break;
        VARIANT vtPath, vtName, vtCmd, vtPid;
        VariantInit(&vtPath); VariantInit(&vtName); VariantInit(&vtCmd); VariantInit(&vtPid);
        std::wstring exePath, procName, cmdLine, pid;
        if (SUCCEEDED(pObj->Get(L"ExecutablePath", 0, &vtPath, nullptr, nullptr)) && vtPath.vt == VT_BSTR)
            exePath = vtPath.bstrVal;
        if (SUCCEEDED(pObj->Get(L"Name", 0, &vtName, nullptr, nullptr)) && vtName.vt == VT_BSTR)
            procName = vtName.bstrVal;
        if (SUCCEEDED(pObj->Get(L"CommandLine", 0, &vtCmd, nullptr, nullptr)) && vtCmd.vt == VT_BSTR)
            cmdLine = vtCmd.bstrVal;
        if (SUCCEEDED(pObj->Get(L"ProcessId", 0, &vtPid, nullptr, nullptr)) && vtPid.vt == VT_I4)
            pid = std::to_wstring(vtPid.lVal);
        VariantClear(&vtPath); VariantClear(&vtName); VariantClear(&vtCmd); VariantClear(&vtPid);
        pObj->Release();
        if (cancel) break;

        std::wstring nameLower = TLW(procName);
        bool matched = false;
        std::string reason;
        std::string nameA = W2A(nameLower);
        for (auto& sp : c.sproc) { if (CCI(nameA, sp)) { matched = true; reason = "Process name: " + sp; break; } }
        if (!matched && !exePath.empty()) {
            std::wstring pathLower = TLW(exePath);
            for (auto& kw : knownNames) {
                if (!kw.empty() && pathLower.find(TLW(A2W(kw))) != std::wstring::npos)
                {
                    matched = true; reason = "Path contains: " + kw; break;
                }
            }
        }
        if (!matched && !cmdLine.empty()) {
            std::wstring cmdLower = TLW(cmdLine);
            for (auto& kw : knownNames) {
                if (!kw.empty() && cmdLower.find(TLW(A2W(kw))) != std::wstring::npos)
                {
                    matched = true; reason = "Command line: " + kw; break;
                }
            }
        }
        if (!matched && !exePath.empty()) { if (MatchProcessVersionInfo(exePath, knownNames)) { matched = true; reason = "Version info matches known macro tool"; } }
        if (!matched && !exePath.empty()) {
            static const std::vector<std::wstring> vendors = { L"Glorious",L"Razer",L"Logitech",L"SteelSeries",L"Corsair",L"ASUS",L"198Macros",L"ZenithMacros" };
            std::wstring pathLower = TLW(exePath);
            for (auto& v : vendors) { if (pathLower.find(TLW(v)) != std::wstring::npos) { matched = true; reason = "Vendor folder: " + W2A(v); if (GVENDOR.empty()) GVENDOR = v; break; } }
        }

        if (matched) {
            auto* r = new SR; r->type = RT::Proc; r->name = procName; r->path = exePath; r->cmd = cmdLine; r->pid = pid;
            r->cat = L"Suspicious Running Process"; r->kws.push_back(reason); r->score = 7; QR(r);
        }

        if (!cmdLine.empty()) {
            std::wstring cmd = cmdLine;
            for (auto& ext : c.exts) {
                std::wstring wext = A2W(ext);
                size_t pos = cmd.rfind(wext);
                if (pos != std::wstring::npos && (pos + wext.size() == cmd.size() || iswspace(cmd[pos + wext.size()]) || cmd[pos + wext.size()] == L'"')) {
                    size_t start = 0, end = pos + wext.size();
                    if (cmd[0] == L'"') { start = 1; size_t q = cmd.find(L'"', 1); if (q != std::wstring::npos && q <= end) end = q; }
                    else if (cmd[0] != L'"') { size_t s = cmd.find(L' '); if (s != std::wstring::npos && s < end) end = s; }
                    std::wstring scriptPath = cmd.substr(start, end - start);
                    auto* r = new SR; r->type = RT::ActiveScript; r->name = GFN(scriptPath); r->path = scriptPath;
                    r->cat = L"Active Script"; r->pid = pid; r->score = 6;
                    QR(r);
                    break;
                }
            }
        }
    }
    pEnum->Release(); pSvc->Release(); pLoc->Release();
}

static void RunUsnJournalScan(const DCfg& c, std::atomic_bool& cancel) {
    HANDLE hVol = CreateFileW(L"\\\\.\\C:", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hVol == INVALID_HANDLE_VALUE) return;
    DWORD bytesRet;
    USN_JOURNAL_DATA ujd = {};
    if (!DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &ujd, sizeof(ujd), &bytesRet, nullptr)) {
        CloseHandle(hVol);
        return;
    }
    MFT_ENUM_DATA med = { 0, 0, ujd.NextUsn, 0, 0 };
    BYTE buf[65536];
    DWORDLONG resume = 0;
    std::unordered_set<std::wstring> seenNames;
    std::vector<std::wstring> knownExeNames;
    for (auto& sw : c.sw) knownExeNames.push_back(TLW(A2W(sw.name)));
    for (auto& sp : c.sproc) knownExeNames.push_back(TLW(A2W(sp)));
    static const wchar_t* hardExe[] = { L"autohotkey.exe", L"autoit3.exe", L"autoitv3.exe",
        L"tinytask.exe", L"pulover.exe", L"macrocreator.exe", L"198macros.exe", L"zenithmacros.exe" };
    for (auto* exe : hardExe) knownExeNames.push_back(exe);
    std::vector<std::wstring> portPatterns;
    for (auto& p : c.portable) portPatterns.push_back(TLW(A2W(p)));
    while (!cancel) {
        med.StartFileReferenceNumber = resume;
        if (!DeviceIoControl(hVol, FSCTL_ENUM_USN_DATA, &med, sizeof(med), buf, sizeof(buf), &bytesRet, nullptr))
            break;
        PUSN_RECORD pur = (PUSN_RECORD)(buf + sizeof(DWORDLONG));
        while (!cancel && (PBYTE)pur < buf + bytesRet) {
            std::wstring fname(pur->FileName, pur->FileNameLength / sizeof(WCHAR));
            std::wstring lower = TLW(fname);
            bool found = false;
            for (auto& ext : c.exts) {
                std::wstring wext = A2W(ext);
                if (lower.size() >= wext.size() && lower.compare(lower.size() - wext.size(), wext.size(), wext) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                for (auto& name : knownExeNames) {
                    if (lower == name) { found = true; break; }
                }
            }
            if (!found && lower.size() > 4 && lower.substr(lower.size() - 4) == L".exe") {
                std::wstring stem = lower.substr(0, lower.size() - 4);
                for (auto& pat : portPatterns) {
                    if (stem.find(pat) != std::wstring::npos) { found = true; break; }
                }
            }
            if (found) {
                std::wstring key = lower + L"|" + std::to_wstring(pur->Usn);
                if (seenNames.insert(key).second) {
                    std::wstring reason;
                    DWORD flags = pur->Reason;
                    if (flags & USN_REASON_FILE_CREATE) reason = L"Create";
                    else if (flags & USN_REASON_FILE_DELETE) reason = L"Delete";
                    else if (flags & USN_REASON_RENAME_OLD_NAME || flags & USN_REASON_RENAME_NEW_NAME) reason = L"Rename";
                    else if (flags & USN_REASON_BASIC_INFO_CHANGE) reason = L"Info Change";
                    else reason = L"Other Change";
                    auto* r = new SR;
                    r->type = RT::UsnJournal;
                    r->name = fname;
                    r->path = L"USN Journal (C:)";
                    r->cat = L"USN Journal Evidence";
                    ULARGE_INTEGER uli; uli.QuadPart = pur->TimeStamp.QuadPart;
                    FILETIME ft; ft.dwLowDateTime = uli.LowPart; ft.dwHighDateTime = uli.HighPart;
                    r->mod = FFT(ft);
                    r->kws = { "USN record: " + W2A(reason) };
                    r->score = 4;
                    QR(r);
                }
            }
            resume = pur->Usn;
            pur = (PUSN_RECORD)((PBYTE)pur + pur->RecordLength);
        }
        if (bytesRet < sizeof(buf)) break;
    }
    CloseHandle(hVol);
}

struct WorkItem {
    std::wstring fullpath;
    bool isDir;
    FILETIME ftLastWrite;
    LARGE_INTEGER fileSize;
};
static std::mutex gWorkMtx;
static std::vector<WorkItem> gWorkQueue;
static std::condition_variable gWorkCV;
static bool gWorkDone = false;

static void ProcessFile(const std::wstring& full, const FILETIME& ft, LARGE_INTEGER sz, const DCfg& c,
    const std::unordered_set<std::string>& extSet, std::atomic_bool& cancel, std::atomic_int& fc) {
    std::string ext = GEX(full);
    if (extSet.count(ext)) {
        if (IsWL(full, c) || IsEx(full, c)) return;
        ++fc;
        auto res = AF(full, sz, ft, c);
        if (res) QR(res.release());
    }
    if (ext == ".exe" && !IsWL(full, c) && !IsEx(full, c)) {
        std::string stem = W2A(TLW(GetFileStem(full)));
        for (auto& p : c.portable) if (CCI(stem, p)) {
            auto r = AE(full, c); if (r) QR(r.release());
            break;
        }
    }
}

static void WorkerProc(const DCfg& c, const std::unordered_set<std::string>& extSet,
    std::atomic_bool& cancel, std::atomic_int& fc) {
    while (!cancel) {
        WorkItem item;
        {
            std::unique_lock<std::mutex> lk(gWorkMtx);
            gWorkCV.wait(lk, [&] { return !gWorkQueue.empty() || gWorkDone; });
            if (gWorkQueue.empty() && gWorkDone) break;
            item = gWorkQueue.back();
            gWorkQueue.pop_back();
        }
        if (item.isDir) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileExW((item.fullpath + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (h == INVALID_HANDLE_VALUE) continue;
            do {
                if (cancel) { FindClose(h); return; }
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                std::wstring full = item.fullpath + L"\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (!IsEx(full, c) && !QuickExcl(full)) {
                        std::lock_guard<std::mutex> lk(gWorkMtx);
                        gWorkQueue.push_back({ full, true, {}, {} });
                        gWorkCV.notify_one();
                    }
                }
                else {
                    LARGE_INTEGER sz; sz.HighPart = fd.nFileSizeHigh; sz.LowPart = fd.nFileSizeLow;
                    {
                        std::lock_guard<std::mutex> lk(gWorkMtx);
                        gWorkQueue.push_back({ full, false, fd.ftLastWriteTime, sz });
                        gWorkCV.notify_one();
                    }
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        else {
            ProcessFile(item.fullpath, item.ftLastWrite, item.fileSize, c, extSet, cancel, fc);
        }
    }
}

static void RunUnifiedScan(const DCfg& c, std::atomic_bool& cancel, std::atomic_int& fc) {
    std::unordered_set<std::string> extSet;
    for (auto& e : c.exts) extSet.insert(TLA(e));
    extSet.insert(".exe");
    DWORD mask = GetLogicalDrives();
    std::vector<std::wstring> drives;
    for (int i = 0; i < 26; i++) {
        if (!(mask & (1 << i))) continue;
        wchar_t root[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
        UINT type = GetDriveTypeW(root);
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) drives.push_back(std::wstring(root, 3));
    }
    {
        std::lock_guard<std::mutex> lk(gWorkMtx);
        gWorkQueue.clear();
        for (auto& d : drives) gWorkQueue.push_back({ d, true, {}, {} });
        gWorkDone = false;
    }
    unsigned int nt = std::thread::hardware_concurrency();
    if (nt < 4) nt = 4; if (nt > 32) nt = 32;
    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < nt; ++i)
        workers.emplace_back(WorkerProc, std::cref(c), std::cref(extSet), std::ref(cancel), std::ref(fc));
    while (!cancel) {
        std::unique_lock<std::mutex> lk(gWorkMtx);
        if (gWorkQueue.empty()) break;
        lk.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    {
        std::lock_guard<std::mutex> lk(gWorkMtx);
        gWorkDone = true;
    }
    gWorkCV.notify_all();
    for (auto& t : workers) if (t.joinable()) t.join();
}

static std::wstring TS() {
    SYSTEMTIME st; GetLocalTime(&st); wchar_t b[32];
    swprintf_s(b, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond); return b;
}

static void WriteRpt(const std::vector<SR*>& res, const std::wstring& path, const std::wstring& vendor) {
    std::wstring dir = path.substr(0, path.rfind(L'\\'));
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    std::wofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.is_open()) {
        wchar_t docs[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs) == S_OK) {
            std::wstring fallback = std::wstring(docs) + L"\\MacroScannerResults.txt";
            f.open(fallback, std::ios::out | std::ios::trunc);
            if (!f.is_open()) return;
        }
        else return;
    }
    f << L"==========================================\nLaffer's MacroScanner Report\n";
    f << L"Scanned    : All Fixed Drives\nDate       : " << TS() << L"\nTotal Found: " << res.size() << L"\n";
    f << L"Mouse/Keyboard: " << (vendor.empty() ? L"Unknown Mouse and Keyboard" : vendor) << L"\n";
    f << L"==========================================\n\n";
    auto writeCatSection = [&](const std::wstring& title, const std::vector<SR*>& items) {
        if (items.empty()) return;
        f << L"====================\n" << title << L" (" << items.size() << L")\n====================\n\n";
        int idx = 1;
        for (auto* r : items) {
            f << L"[" << idx++ << L"]\n";
            f << L"  Name          : " << r->name << L"\n";
            if (!r->pub.empty())  f << L"  Publisher     : " << r->pub << L"\n";
            if (!r->info.empty()) f << L"  Version/Info  : " << r->info << L"\n";
            if (!r->path.empty()) f << L"  Location      : " << r->path << L"\n";
            if (!r->cat.empty())  f << L"  Category      : " << r->cat << L"\n";
            if (!r->pid.empty())  f << L"  PID           : " << r->pid << L"\n";
            if (!r->cmd.empty())  f << L"  CmdLine       : " << r->cmd << L"\n";
            if (!r->mod.empty())  f << L"  Last Modified : " << r->mod << L"\n";
            if (!r->kws.empty()) { f << L"  Keywords Found: "; for (size_t i = 0; i < r->kws.size(); i++) { if (i) f << L", "; f << A2W(r->kws[i]); } f << L"\n"; }
            f << L"  Risk          : " << r->rl() << L"\n\n";
        }
        };
    std::map<std::wstring, std::vector<SR*>> fileGroups, vendorGroups;
    std::vector<SR*> installed, proc, port, activeScripts, prefetch, deletedScript, usn;
    for (auto* r : res) {
        switch (r->type) {
        case RT::Inst: installed.push_back(r); break;
        case RT::Peri: vendorGroups[r->cat].push_back(r); break;
        case RT::Proc: proc.push_back(r); break;
        case RT::Port: port.push_back(r); break;
        case RT::ActiveScript: activeScripts.push_back(r); break;
        case RT::Prefetch: prefetch.push_back(r); break;
        case RT::DeletedScript: deletedScript.push_back(r); break;
        case RT::UsnJournal: usn.push_back(r); break;
        default: fileGroups[r->cat].push_back(r); break;
        }
    }
    writeCatSection(L"Installed Macro Software", installed);
    for (auto& kv : vendorGroups) writeCatSection(kv.first, kv.second);
    writeCatSection(L"Active Scripts", activeScripts);
    writeCatSection(L"Suspicious Running Processes", proc);
    writeCatSection(L"Prefetch Evidence", prefetch);
    writeCatSection(L"Deleted Script Evidence", deletedScript);
    writeCatSection(L"USN Journal Evidence", usn);
    writeCatSection(L"Portable Macro Executables", port);
    std::vector<std::wstring> catOrder = { L"Python Window Script", L"Python Script", L"AutoHotkey Script",
        L"AutoIt Script", L"JavaScript Automation", L"Automation Script" };
    for (auto& cat : catOrder) {
        auto it = fileGroups.find(cat);
        if (it != fileGroups.end()) {
            auto& items = it->second;
            std::sort(items.begin(), items.end(), [](SR* a, SR* b) { return a->score > b->score; });
            writeCatSection(cat + L" Files", items);
        }
    }
    int cr = 0, hi = 0, me = 0, lo = 0;
    for (auto* r : res) switch (r->risk()) { case RL::Cr: cr++; break; case RL::Hi: hi++; break; case RL::Me: me++; break; default: lo++; }
    f << L"==========================================\nRisk Summary\n==========================================\n";
    f << L"  CRITICAL : " << cr << L"\n  HIGH     : " << hi << L"\n  MEDIUM   : " << me << L"\n  LOW      : " << lo << L"\n";
    f.flush(); f.close();
}

static void DrainQueue();

static void ScanMain(HWND hwnd) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    GVENDOR.clear();
    PostMessageW(hwnd, WM_PHASE_PROGRESS, 0, (LPARAM)L"Scanning Gaming Software");
    RunPeri(G, GCAN);
    if (!GCAN) {
        PostMessageW(hwnd, WM_PHASE_PROGRESS, 100, (LPARAM)L"Scanning Registry");
        RunReg(G, GCAN);
    }
    if (!GCAN) {
        PostMessageW(hwnd, WM_PHASE_PROGRESS, 200, (LPARAM)L"Scanning Portables & Prefetch");
        RunPort(G, GCAN);
        RunPrefetchScan(G, GCAN);
        RunRecentFolderScan(G, GCAN);
    }
    if (!GCAN) {
        PostMessageW(hwnd, WM_PHASE_PROGRESS, 250, (LPARAM)L"Scanning USN Journal");
        RunUsnJournalScan(G, GCAN);
    }
    if (!GCAN) {
        PostMessageW(hwnd, WM_PHASE_PROGRESS, 300, (LPARAM)L"Scanning Running Processes");
        RunProcScanWMI(G, GCAN);
    }
    if (!GCAN) {
        gPhaseText = L"Scanning Files";
        gBaseProgress = 450;
        PostMessageW(hwnd, WM_PHASE_PROGRESS, 450, (LPARAM)L"Scanning Files");
        RunUnifiedScan(G, GCAN, GFCNT);
    }
    GSCANNING = false;
    DrainQueue();
    std::vector<SR*> flat;
    {
        std::lock_guard<std::mutex> lk(GMX);
        for (auto& r : GRES) flat.push_back(r.get());
    }
    std::sort(flat.begin(), flat.end(), [](SR* a, SR* b) {
        std::string extA = GEX(a->path.empty() ? L"" : a->path);
        std::string extB = GEX(b->path.empty() ? L"" : b->path);
        if (extA != extB) return extA < extB;
        return a->score > b->score;
        });
    if (GVENDOR.empty()) {
        for (auto* r : flat) {
            std::wstring lower = TLW(r->path + r->cmd);
            if (lower.find(L"glorious") != std::wstring::npos) { GVENDOR = L"Glorious"; break; }
            if (lower.find(L"razer") != std::wstring::npos) { GVENDOR = L"Razer"; break; }
            if (lower.find(L"logitech") != std::wstring::npos) { GVENDOR = L"Logitech"; break; }
            if (lower.find(L"steelseries") != std::wstring::npos) { GVENDOR = L"SteelSeries"; break; }
            if (lower.find(L"corsair") != std::wstring::npos) { GVENDOR = L"Corsair"; break; }
        }
    }
    WriteRpt(flat, GREP, GVENDOR);
    CoUninitialize();
    PostMessageW(hwnd, WM_SCAN_DONE, GCAN.load() ? 1 : 0, 0);
}

static HICON LoadJpegIcon(HINSTANCE hInst) {
    HRSRC hR = FindResourceW(hInst, MAKEINTRESOURCEW(IDR_APPICON), L"RCDATA");
    if (!hR) return nullptr; HGLOBAL hG = LoadResource(hInst, hR); if (!hG) return nullptr;
    void* pData = LockResource(hG); DWORD sz = SizeofResource(hInst, hR);
    if (!pData || !sz) return nullptr;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz); if (!hMem) return nullptr;
    void* pMem = GlobalLock(hMem); if (!pMem) { GlobalFree(hMem); return nullptr; }
    memcpy(pMem, pData, sz); GlobalUnlock(hMem);
    IStream* pStream = nullptr; CreateStreamOnHGlobal(hMem, TRUE, &pStream);
    if (!pStream) { GlobalFree(hMem); return nullptr; }
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(pStream); pStream->Release();
    if (!bmp || (bmp->GetLastStatus() != Gdiplus::Ok)) { delete bmp; return nullptr; }
    Gdiplus::Bitmap scaled(32, 32, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&scaled); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(bmp, 0, 0, 32, 32);
    delete bmp; HICON icon = nullptr; scaled.GetHICON(&icon); return icon;
}

static void MkFonts(HWND hwnd) {
    UINT dpi = 96; typedef UINT(WINAPI* PF)(HWND);
    if (auto* fn = (PF)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")) dpi = fn(hwnd);
    auto mk = [&](int pt, int w, const wchar_t* f) -> HFONT {
        int px = MulDiv(pt, dpi, 72);
        return CreateFontW(-px, 0, 0, 0, w, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, f);
        };
    if (GFU) DeleteObject(GFU); if (GFB) DeleteObject(GFB); if (GFS) DeleteObject(GFS);
    if (GFM) DeleteObject(GFM); if (GFT) DeleteObject(GFT); if (GFSM) DeleteObject(GFSM);
    GFU = mk(10, FW_NORMAL, L"Segoe UI");
    GFB = mk(10, FW_SEMIBOLD, L"Segoe UI");
    GFS = mk(9, FW_NORMAL, L"Segoe UI");
    GFM = mk(9, FW_NORMAL, L"Consolas");
    GFT = mk(11, FW_SEMIBOLD, L"Segoe UI");
    GFSM = mk(8, FW_NORMAL, L"Segoe UI");
}

static void SetupLV() {
    ListView_SetExtendedListViewStyle(GLV, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_BORDERSELECT);
    SendMessageW(GLV, WM_SETFONT, (WPARAM)GFM, TRUE);
    static const struct { const wchar_t* n; int w; } cols[] = {
        {L"RISK",72},{L"NAME",210},{L"PATH",310},{L"KEYWORDS",250},{L"MODIFIED",140},{L"SCORE",60}
    };
    LVCOLUMNW lvc = {}; lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    for (int i = 0; i < 6; i++) { lvc.iSubItem = i; lvc.pszText = (LPWSTR)cols[i].n; lvc.cx = cols[i].w; ListView_InsertColumn(GLV, i, &lvc); }
    ListView_SetBkColor(GLV, CB); ListView_SetTextBkColor(GLV, CB); ListView_SetTextColor(GLV, CTX);
    SetWindowTheme(GLV, L"Explorer", nullptr);
}

static void AddRow(SR* r, int idx) {
    LVITEMW lvi = {}; lvi.mask = LVIF_TEXT | LVIF_PARAM; lvi.iItem = idx; lvi.lParam = (LPARAM)r;
    std::wstring rl = r->rl(); lvi.pszText = rl.data();
    int row = ListView_InsertItem(GLV, &lvi);
    ListView_SetItemText(GLV, row, COL_NAME, r->name.data());
    ListView_SetItemText(GLV, row, COL_PATH, r->path.data());
    std::wstring kw; for (auto& k : r->kws) { if (!kw.empty()) kw += L", "; kw += A2W(k); }
    ListView_SetItemText(GLV, row, COL_KW, kw.data());
    ListView_SetItemText(GLV, row, COL_MOD, r->mod.data());
    std::wstring sc = std::to_wstring(r->score); ListView_SetItemText(GLV, row, COL_SCORE, sc.data());
}

static void Layout(int W, int H) {
    int pad = 16;
    int tbOff = TH + pad;
    int rowY = tbOff + 14;
    int rowH = 48;
    int btnW = 140, btnH = 36;
    int gap1 = 30;
    int pbY = rowY + rowH + gap1;
    int pbH = 8;
    int gap2 = 20;
    int pctY = pbY + pbH + gap2;
    int phaseX = pad;
    int phaseW = W / 2 - 40;
    int pctX = phaseX + phaseW + 20;
    int pctW = 100;
    int btn1X = W - pad - btnW * 2 - 10;
    SetWindowPos(GPH, nullptr, phaseX, rowY + 8, phaseW, 22, SWP_NOZORDER);
    SetWindowPos(GCU, nullptr, pctX, rowY + 8, pctW, 22, SWP_NOZORDER);
    SetWindowPos(GBS, nullptr, btn1X, rowY, btnW, btnH, SWP_NOZORDER);
    SetWindowPos(GBR, nullptr, btn1X + btnW, rowY, btnW, btnH, SWP_NOZORDER);
    SetWindowPos(GPR, nullptr, pad, pbY, W - pad * 2, pbH, SWP_NOZORDER);
    int statY = pctY + 16 + pad;
    gStatY = statY;
    int statH = 64;
    int cw = (W - pad * 2) / 6;
    int listY = statY + statH + pad;
    int listH = H - listY - pad;
    HWND stats[] = { GSF, GSFo, GSC, GSH, GSM, GSL };
    for (int i = 0; i < 6; i++)
        SetWindowPos(stats[i], nullptr, pad + cw * i, statY, cw - 2, statH, SWP_NOZORDER);
    SetWindowPos(GLV, nullptr, pad, listY, W - pad * 2, listH, SWP_NOZORDER);
}

static void PBtn(HDC hdc, RECT rc, const wchar_t* txt, bool en, bool hov, bool prs, bool acc) {
    COLORREF bg;
    if (!en)       bg = RGB(26, 31, 38);
    else if (prs)  bg = acc ? RGB(52, 120, 200) : CBH;
    else if (hov)  bg = acc ? RGB(100, 178, 255) : RGB(60, 66, 73);
    else           bg = acc ? CA : CBO;
    HBRUSH br = CreateSolidBrush(bg); FillRect(hdc, &rc, br); DeleteObject(br);
    HPEN pen = CreatePen(PS_SOLID, 1, en ? (acc ? RGB(60, 140, 220) : CBR) : RGB(28, 33, 40));
    HPEN op = (HPEN)SelectObject(hdc, pen); SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom); SelectObject(hdc, op); DeleteObject(pen);
    SetBkMode(hdc, TRANSPARENT);
    COLORREF tx = !en ? CD : (hov ? (acc ? RGB(10, 14, 20) : RGB(255, 255, 255)) : (acc ? RGB(10, 14, 20) : CTX));
    SetTextColor(hdc, tx);
    SelectObject(hdc, GFB); DrawTextW(hdc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrainQueue() {
    std::vector<SR*> batch;
    { std::lock_guard<std::mutex> lk(QMTX); while (!QUE.empty() && batch.size() < 200) { batch.push_back(QUE.front()); QUE.pop(); } }
    if (batch.empty()) return;
    int idx; { std::lock_guard<std::mutex> lk(GMX); idx = (int)GRES.size(); for (auto* r : batch) GRES.emplace_back(r); }
    for (auto* r : batch) { AddRow(r, idx++); }
    int cr = 0, hi = 0, me = 0, lo = 0, total = 0;
    {
        std::lock_guard<std::mutex> lk(GMX); total = (int)GRES.size();
        for (auto& r : GRES) switch (r->risk()) { case RL::Cr: cr++; break; case RL::Hi: hi++; break; case RL::Me: me++; break; default: lo++; }
    }
    wchar_t b[64];
    swprintf_s(b, L"Files Scanned\n%d", GFCNT.load()); SetWindowTextW(GSF, b);
    swprintf_s(b, L"Total Found\n%d", total); SetWindowTextW(GSFo, b);
    swprintf_s(b, L"Critical\n%d", cr); SetWindowTextW(GSC, b);
    swprintf_s(b, L"High\n%d", hi); SetWindowTextW(GSH, b);
    swprintf_s(b, L"Medium\n%d", me); SetWindowTextW(GSM, b);
    swprintf_s(b, L"Low\n%d", lo); SetWindowTextW(GSL, b);
    int pos;
    if (gBaseProgress >= 450 && GFCNT.load() > 0) {
        double frac = std::min((double)GFCNT.load() / 80000.0, 1.0);
        pos = gBaseProgress + (int)(frac * (1000 - gBaseProgress));
        if (pos < gBaseProgress) pos = gBaseProgress;
        if (pos > 999) pos = 999;
    }
    else { pos = gBaseProgress; }
    SendMessageW(GPR, PBM_SETPOS, pos, 0);
    swprintf_s(b, L"%.1f%%", pos / 10.0);
    SetWindowTextW(GCU, b);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wP, LPARAM lP) {
    switch (msg) {
    case WM_CREATE: {
        MkFonts(hwnd);
        GBG = CreateSolidBrush(CB); GPAN = CreateSolidBrush(CP); GTIT = CreateSolidBrush(CT);
        GSEP = CreateSolidBrush(RGB(48, 54, 61)); GBGCARD = CreateSolidBrush(RGB(22, 27, 34));
        GPR = CreateWindowExW(0, PROGRESS_CLASS, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 10, 8, hwnd, (HMENU)IDC_PROGRESS, nullptr, nullptr);
        SendMessageW(GPR, PBM_SETBARCOLOR, 0, (LPARAM)CPR);
        SendMessageW(GPR, PBM_SETBKCOLOR, 0, (LPARAM)CP);
        SendMessageW(GPR, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
        auto mkS = [&](int id, const wchar_t* t, HFONT f, DWORD st = SS_LEFT) -> HWND {
            HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | st, 0, 0, 10, 10, hwnd, (HMENU)(intptr_t)id, nullptr, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); return h;
            };
        GPH = mkS(IDC_PHASE, L"Ready", GFB);
        GCU = mkS(IDC_CURRENT, L"0.0%", GFS);
        auto mkSt = [&](int id, const wchar_t* t) -> HWND {
            HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 10, 10, hwnd, (HMENU)(intptr_t)id, nullptr, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)GFU, TRUE); return h;
            };
        GSF = mkSt(IDC_STAT_FILES, L"Files Scanned\n0");
        GSFo = mkSt(IDC_STAT_FOUND, L"Total Found\n0");
        GSC = mkSt(IDC_STAT_CRIT, L"Critical\n0");
        GSH = mkSt(IDC_STAT_HIGH, L"High\n0");
        GSM = mkSt(IDC_STAT_MED, L"Medium\n0");
        GSL = mkSt(IDC_STAT_LOW, L"Low\n0");
        GLV = CreateWindowExW(0, WC_LISTVIEWW, nullptr, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, 0, 0, 10, 10, hwnd, (HMENU)IDC_LISTVIEW, nullptr, nullptr);
        SetupLV();
        auto mkB = [&](int id, const wchar_t* t) -> HWND {
            HWND h = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 10, 10, hwnd, (HMENU)(intptr_t)id, nullptr, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)GFB, TRUE); return h;
            };
        GBS = mkB(IDC_BTN_START, L"Start Scan");
        GBR = mkB(IDC_BTN_REPORT, L"Open Report");
        EnableWindow(GBR, FALSE);
        MARGINS m = { 0,0,0,0 }; DwmExtendFrameIntoClientArea(hwnd, &m);
        wchar_t selfPath[MAX_PATH]; GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
        GOUR_EXE = selfPath;
        std::string json = LdEmbed();
        if (json.empty()) {
            wchar_t exeDir[MAX_PATH]; GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
            wchar_t* sl = wcsrchr(exeDir, L'\\'); if (sl) *sl = 0;
            std::wstring cp = std::wstring(exeDir) + L"\\detection_config.json";
            std::ifstream fi(cp); if (fi.is_open()) json = std::string(std::istreambuf_iterator<char>(fi), {});
        }
        G = LoadCfg(json);
        ApplyFallbackExts(G);
        PrepareKeywordSets(G);
        SetTimer(hwnd, TIMER_DRAIN, 80, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (wP == TIMER_DRAIN && GSCANNING) DrainQueue();
        return 0;
    case WM_PHASE_PROGRESS: {
        int progress = (int)wP;
        const wchar_t* txt = (const wchar_t*)lP;
        gPhaseText = txt ? txt : L"";
        gBaseProgress = progress;
        SendMessageW(GPR, PBM_SETPOS, progress, 0);
        SetWindowTextW(GPH, gPhaseText.c_str());
        wchar_t b[32]; swprintf_s(b, L"%.1f%%", progress / 10.0); SetWindowTextW(GCU, b);
        return 0;
    }
    case WM_DPICHANGED: {
        MkFonts(hwnd); RECT* r = (RECT*)lP;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        RECT rc; GetClientRect(hwnd, &rc); Layout(rc.right, rc.bottom); InvalidateRect(hwnd, nullptr, TRUE); return 0;
    }
    case WM_NCCALCSIZE:
        if (wP == TRUE) { if (IsMaximized(hwnd)) { MONITORINFO mi = { sizeof(mi) }; GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi); ((NCCALCSIZE_PARAMS*)lP)->rgrc[0] = mi.rcWork; } return 0; }
        return DefWindowProcW(hwnd, msg, wP, lP);
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lP), GET_Y_LPARAM(lP) }; ScreenToClient(hwnd, &pt);
        RECT rc; GetClientRect(hwnd, &rc);
        if (!IsMaximized(hwnd)) {
            if (pt.x < BS && pt.y < BS) return HTTOPLEFT; if (pt.x > rc.right - BS && pt.y < BS) return HTTOPRIGHT;
            if (pt.x < BS && pt.y > rc.bottom - BS) return HTBOTTOMLEFT; if (pt.x > rc.right - BS && pt.y > rc.bottom - BS) return HTBOTTOMRIGHT;
            if (pt.x < BS) return HTLEFT; if (pt.x > rc.right - BS) return HTRIGHT; if (pt.y < BS) return HTTOP; if (pt.y > rc.bottom - BS) return HTBOTTOM;
        }
        if (pt.y >= 0 && pt.y < TH) { int bx = rc.right - 88; if (pt.x >= bx) return HTCLIENT; return HTCAPTION; }
        return HTCLIENT;
    }
    case WM_NCACTIVATE: return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(wP);
        if (id == IDC_BTN_START) {
            if (!GSCANNING) {
                GSCANNING = true; GCAN = false; GFCNT = 0; GRCNT = 0;
                { std::lock_guard<std::mutex> lk(GMX); GRES.clear(); }
                { std::lock_guard<std::mutex> lk(QMTX); while (!QUE.empty()) QUE.pop(); }
                ListView_DeleteAllItems(GLV);
                SetWindowTextW(GSF, L"Files Scanned\n0"); SetWindowTextW(GSFo, L"Total Found\n0");
                SetWindowTextW(GSC, L"Critical\n0"); SetWindowTextW(GSH, L"High\n0");
                SetWindowTextW(GSM, L"Medium\n0"); SetWindowTextW(GSL, L"Low\n0");
                SetWindowTextW(GPH, L"Starting scan…");
                SetWindowTextW(GCU, L"0.0%");
                SendMessageW(GPR, PBM_SETPOS, 0, 0);
                EnableWindow(GBR, FALSE);
                SetWindowTextW(GBS, L"Stop Scan");
                InvalidateRect(hwnd, nullptr, TRUE);
                if (GTHR.joinable()) GTHR.join();
                GTHR = std::thread(ScanMain, hwnd);
            }
            else { GCAN = true; SetWindowTextW(GPH, L"Stopping scan…"); }
        }
        else if (id == IDC_BTN_REPORT) {
            ShellExecuteW(nullptr, L"open", GREP.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
    }
    case WM_SCAN_DONE: {
        GSCANNING = false; DrainQueue();
        bool cancelled = (wP != 0);
        SendMessageW(GPR, PBM_SETPOS, cancelled ? 0 : 1000, 0);
        SetWindowTextW(GPH, cancelled ? L"Scan cancelled" : L"Scan complete");
        SetWindowTextW(GCU, cancelled ? L"" : L"100.0%");
        SetWindowTextW(GBS, L"Start Scan");
        EnableWindow(GBR, !cancelled && GetFileAttributesW(GREP.c_str()) != INVALID_FILE_ATTRIBUTES ? TRUE : FALSE);
        if (ListView_GetItemCount(GLV) == 0 && !cancelled) {
            LVITEMW lvi = {}; lvi.mask = LVIF_TEXT; lvi.iItem = 0; lvi.pszText = (LPWSTR)L"No items found";
            ListView_InsertItem(GLV, &lvi);
        }
        InvalidateRect(hwnd, nullptr, TRUE); return 0;
    }
    case WM_NOTIFY: {
        auto* nm = (LPNMHDR)lP;
        if (nm->hwndFrom == GLV && nm->code == NM_CUSTOMDRAW) {
            auto* cd = (LPNMLVCUSTOMDRAW)lP;
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:
                cd->clrTextBk = (cd->nmcd.dwItemSpec % 2) ? CAR : CB; cd->clrText = CTX;
                return CDRF_NOTIFYSUBITEMDRAW | CDRF_NEWFONT;
            case CDDS_SUBITEM | CDDS_ITEMPREPAINT: {
                LVITEMW lvi = {}; lvi.mask = LVIF_PARAM; lvi.iItem = (int)cd->nmcd.dwItemSpec; ListView_GetItem(GLV, &lvi);
                SR* r = (SR*)lvi.lParam;
                if (cd->iSubItem == COL_RISK || cd->iSubItem == COL_SCORE) { if (r) { cd->clrText = r->rc(); return CDRF_NEWFONT; } }
                if (cd->iSubItem == COL_PATH || cd->iSubItem == COL_MOD) { cd->clrText = CD; return CDRF_NEWFONT; }
                return CDRF_DODEFAULT;
            }
            }
        }
        return CDRF_DODEFAULT;
    }
    case WM_DRAWITEM: {
        auto* di = (DRAWITEMSTRUCT*)lP; int id = (int)wP;
        if (id == IDC_BTN_START || id == IDC_BTN_REPORT) {
            bool en = IsWindowEnabled(di->hwndItem) != 0;
            bool hov = (di->itemState & ODS_HOTLIGHT) != 0;
            bool prs = (di->itemState & ODS_SELECTED) != 0;
            wchar_t txt[128]; GetWindowTextW(di->hwndItem, txt, 128);
            PBtn(di->hDC, di->rcItem, txt, en, hov, prs, id == IDC_BTN_START);
            return TRUE;
        }
        return FALSE;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wP; int id = GetDlgCtrlID((HWND)lP); SetBkMode(hdc, TRANSPARENT);
        if (id == IDC_PHASE) { SetTextColor(hdc, CTX); return (LRESULT)GBG; }
        if (id == IDC_CURRENT) { SetTextColor(hdc, CD); return (LRESULT)GBG; }
        if (id == IDC_STAT_CRIT) { SetTextColor(hdc, CCR); return (LRESULT)GPAN; }
        if (id == IDC_STAT_HIGH) { SetTextColor(hdc, CHI); return (LRESULT)GPAN; }
        if (id == IDC_STAT_MED) { SetTextColor(hdc, CME); return (LRESULT)GPAN; }
        if (id == IDC_STAT_LOW) { SetTextColor(hdc, CLW); return (LRESULT)GPAN; }
        SetTextColor(hdc, CTX); return (LRESULT)GPAN;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wP; RECT rc; GetClientRect(hwnd, &rc); FillRect(hdc, &rc, GBG);
        RECT tb = { 0,0,rc.right,TH }; FillRect(hdc, &tb, GTIT);
        HPEN pen = CreatePen(PS_SOLID, 1, CBR); HPEN op = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, 0, TH, nullptr); LineTo(hdc, rc.right, TH); SelectObject(hdc, op); DeleteObject(pen);
        int bw = 44, bx = rc.right - bw * 2;
        POINT cur; GetCursorPos(&cur); ScreenToClient(hwnd, &cur);
        bool mH = cur.x >= bx && cur.x < bx + bw && cur.y >= 0 && cur.y < TH;
        bool cH = cur.x >= bx + bw && cur.y >= 0 && cur.y < TH;
        if (mH) { RECT mr = { bx,0,bx + bw,TH }; HBRUSH bh = CreateSolidBrush(CBH); FillRect(hdc, &mr, bh); DeleteObject(bh); }
        if (cH) { RECT cr = { bx + bw,0,bx + bw * 2,TH }; HBRUSH bh = CreateSolidBrush(RGB(196, 43, 28)); FillRect(hdc, &cr, bh); DeleteObject(bh); }
        SetBkMode(hdc, TRANSPARENT); SelectObject(hdc, GFT);
        SetTextColor(hdc, CTX); RECT tr = { 12,0,bx - 4,TH }; DrawTextW(hdc, L"Laffer's Macro Scanner", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hdc, CTX); RECT mr = { bx,0,bx + bw,TH }; DrawTextW(hdc, L"−", -1, &mr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hdc, cH ? RGB(255, 255, 255) : CTX); RECT cr2 = { bx + bw,0,bx + bw * 2,TH }; DrawTextW(hdc, L"×", -1, &cr2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT sep = { 0,TH,rc.right,TH + 1 }; FillRect(hdc, &sep, GSEP);
        int pad = 16, cw = (rc.right - pad * 2) / 6;
        int statY = gStatY;
        if (statY == 0) statY = 230;
        for (int i = 0; i < 6; i++) {
            RECT card = { pad + cw * i, statY, pad + cw * (i + 1) - 2, statY + 58 };
            FillRect(hdc, &card, GBGCARD);
            HPEN p2 = CreatePen(PS_SOLID, 1, CBR); HPEN op2 = (HPEN)SelectObject(hdc, p2);
            SelectObject(hdc, GetStockObject(NULL_BRUSH)); Rectangle(hdc, card.left, card.top, card.right, card.bottom);
            SelectObject(hdc, op2); DeleteObject(p2);
        }
        if (ListView_GetItemCount(GLV) == 0 && !GSCANNING) {
            RECT lrc; GetWindowRect(GLV, &lrc); MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&lrc, 2);
            SetTextColor(hdc, CD); SelectObject(hdc, GFS);
            DrawTextW(hdc, L"No results yet — press Start Scan to begin.", -1, &lrc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return 1;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 }; TrackMouseEvent(&tme);
        POINT pt = { GET_X_LPARAM(lP), GET_Y_LPARAM(lP) }; RECT rc2; GetClientRect(hwnd, &rc2);
        int bw = 44, bx = rc2.right - bw * 2;
        bool nm = pt.x >= bx && pt.x < bx + bw && pt.y >= 0 && pt.y < TH;
        bool nc = pt.x >= bx + bw && pt.y >= 0 && pt.y < TH;
        if (nm != GMINH || nc != GCLH) { GMINH = nm; GCLH = nc; RECT tb = { 0,0,rc2.right,TH }; InvalidateRect(hwnd, &tb, TRUE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        GMINH = GCLH = false; { RECT rc3; GetClientRect(hwnd, &rc3); RECT tb = { 0,0,rc3.right,TH }; InvalidateRect(hwnd, &tb, TRUE); } return 0;
    case WM_LBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lP), GET_Y_LPARAM(lP) }; RECT rc4; GetClientRect(hwnd, &rc4);
        int bw = 44, bx = rc4.right - bw * 2;
        if (pt.y >= 0 && pt.y < TH) {
            if (pt.x >= bx && pt.x < bx + bw) ShowWindow(hwnd, SW_MINIMIZE);
            else if (pt.x >= bx + bw) { GCAN = true; DestroyWindow(hwnd); }
        }
        return 0;
    }
    case WM_SIZE: {
        int W = LOWORD(lP), H = HIWORD(lP); if (W > 1) Layout(W, H); InvalidateRect(hwnd, nullptr, TRUE); return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lP; mm->ptMinTrackSize = { 920, 680 };
        if (IsMaximized(hwnd)) {
            MONITORINFO mi = { sizeof(mi) }; GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            mm->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left; mm->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            mm->ptMaxPosition = { mi.rcWork.left, mi.rcWork.top };
        }
        return 0;
    }
    case WM_DESTROY:
        GCAN = true; KillTimer(hwnd, TIMER_DRAIN); if (GTHR.joinable()) GTHR.join();
        if (GBG) DeleteObject(GBG); if (GPAN) DeleteObject(GPAN); if (GTIT) DeleteObject(GTIT);
        if (GSEP) DeleteObject(GSEP); if (GBGCARD) DeleteObject(GBGCARD);
        if (GFU) DeleteObject(GFU); if (GFB) DeleteObject(GFB); if (GFS) DeleteObject(GFS);
        if (GFM) DeleteObject(GFM); if (GFT) DeleteObject(GFT); if (GFSM) DeleteObject(GFSM);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wP, lP);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    typedef BOOL(WINAPI* PFN)(DPI_AWARENESS_CONTEXT);
    if (auto* fn = (PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    else SetProcessDPIAware();
    ULONG_PTR gdipToken; Gdiplus::GdiplusStartupInput gsi; Gdiplus::GdiplusStartup(&gdipToken, &gsi, nullptr);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES }; InitCommonControlsEx(&icc);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(CB);
    wc.lpszClassName = L"LafferMS";
    HICON appIcon = LoadJpegIcon(hInst);
    wc.hIcon = appIcon ? appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN), ww = 1130, wh = 760;
    GW = CreateWindowExW(WS_EX_APPWINDOW, L"LafferMS", L"Laffer's Macro Scanner",
        WS_POPUP | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
        (sw - ww) / 2, (sh - wh) / 2, ww, wh, nullptr, nullptr, hInst, nullptr);
    if (appIcon) { SendMessageW(GW, WM_SETICON, ICON_BIG, (LPARAM)appIcon); SendMessageW(GW, WM_SETICON, ICON_SMALL, (LPARAM)appIcon); }
    ShowWindow(GW, SW_SHOW); UpdateWindow(GW);
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    Gdiplus::GdiplusShutdown(gdipToken); if (appIcon) DestroyIcon(appIcon);
    return (int)msg.wParam;
}