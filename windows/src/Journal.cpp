#include "Journal.h"
#include "Log.h"
#include <shlobj.h>
#include <shellapi.h>
#include <vector>
#include <cstdio>

namespace {

const wchar_t* kHeader = L"# MindfulCompute Journal\n";

// ---- path helpers -----------------------------------------------------------
std::wstring appDataRoot() {
    PWSTR raw = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw)) && raw) {
        out = raw;
    }
    if (raw) CoTaskMemFree(raw);
    return out;
}

std::wstring journalPath() { return Journal::baseDir() + L"\\journal.md"; }
std::wstring dataPath()    { return Journal::baseDir() + L"\\sessions.json"; }

bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// ---- UTF-8 file IO ----------------------------------------------------------
bool writeUtf8(const std::wstring& path, const std::wstring& text) {
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                    nullptr, 0, nullptr, nullptr);
    std::string buf((size_t)bytes, '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                            buf.data(), bytes, nullptr, nullptr);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    size_t wrote = buf.empty() ? 0 : fwrite(buf.data(), 1, buf.size(), f);
    fclose(f);
    return wrote == buf.size();
}

bool readUtf8(const std::wstring& path, std::wstring& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    std::string buf((size_t)n, '\0');
    size_t got = n > 0 ? fread(buf.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    if (got != (size_t)n) return false;
    // Strip a UTF-8 BOM if one is present.
    size_t off = 0;
    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) off = 3;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.data() + off, (int)(buf.size() - off),
                                   nullptr, 0);
    out.assign((size_t)wlen, L'\0');
    if (wlen > 0)
        MultiByteToWideChar(CP_UTF8, 0, buf.data() + off, (int)(buf.size() - off),
                            out.data(), wlen);
    return true;
}

// ---- date formatting --------------------------------------------------------
// journal.md: "yyyy-MM-dd HH:mm" in LOCAL time (matches Swift's local-tz entry).
std::wstring localEntryDate(const FILETIME& utc) {
    FILETIME local{};
    FileTimeToLocalFileTime(&utc, &local);
    SYSTEMTIME st{};
    FileTimeToSystemTime(&local, &st);
    wchar_t buf[32];
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute);
    return buf;
}

// sessions.json: ISO-8601 in UTC with 'Z' (matches Swift JSONEncoder .iso8601).
std::wstring isoUtc(const FILETIME& utc) {
    SYSTEMTIME st{};
    FileTimeToSystemTime(&utc, &st);
    wchar_t buf[32];
    swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02dZ", st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ---- JSON string escaping (quotes, backslashes, control chars) --------------
// Non-ASCII is emitted verbatim as UTF-8 at write time, matching Swift's
// JSONEncoder (which does not \u-escape non-ASCII).
std::wstring jsonEscape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (wchar_t c : s) {
        switch (c) {
        case L'"':  out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\b': out += L"\\b";  break;
        case L'\f': out += L"\\f";  break;
        case L'\n': out += L"\\n";  break;
        case L'\r': out += L"\\r";  break;
        case L'\t': out += L"\\t";  break;
        default:
            if (c < 0x20) {
                wchar_t u[8];
                swprintf_s(u, L"\\u%04x", (unsigned)c);
                out += u;
            } else {
                out += c;
            }
        }
    }
    return out;
}

// ---- one stored record ------------------------------------------------------
struct Record {
    std::wstring start;   // ISO-8601 UTC string (kept verbatim on round-trip)
    int          planned = 0;
    int          actual  = 0;
    std::wstring intention;
    std::wstring reflection;
};

// Encode records as pretty JSON with sorted keys, matching Swift's
// [.prettyPrinted, .sortedKeys] output (2-space indent, " : " separator).
std::wstring encode(const std::vector<Record>& recs) {
    std::wstring out = L"[";
    for (size_t k = 0; k < recs.size(); ++k) {
        const Record& r = recs[k];
        out += L"\n  {\n";
        wchar_t nums[64];
        swprintf_s(nums, L"    \"actualMinutes\" : %d,\n", r.actual);
        out += nums;
        out += L"    \"intention\" : \"" + jsonEscape(r.intention) + L"\",\n";
        swprintf_s(nums, L"    \"plannedMinutes\" : %d,\n", r.planned);
        out += nums;
        out += L"    \"reflection\" : \"" + jsonEscape(r.reflection) + L"\",\n";
        out += L"    \"start\" : \"" + jsonEscape(r.start) + L"\"\n";
        out += (k + 1 < recs.size()) ? L"  }," : L"  }";
    }
    out += L"\n]";
    return out;
}

// ---- minimal, strict JSON parser (validates + extracts records) -------------
struct Json {
    int t = 0; // 0 null 1 bool 2 num 3 str 4 arr 5 obj
    bool b = false;
    double num = 0;
    std::wstring str;
    std::vector<Json> arr;
    std::vector<std::pair<std::wstring, Json>> obj;
};

struct Parser {
    const wchar_t* s;
    size_t i, n;
    bool ok = true;

    void ws() {
        while (i < n && (s[i] == L' ' || s[i] == L'\t' || s[i] == L'\n' || s[i] == L'\r')) i++;
    }
    bool string(std::wstring& out) {
        if (i >= n || s[i] != L'"') { ok = false; return false; }
        i++;
        while (i < n) {
            wchar_t c = s[i++];
            if (c == L'"') return true;
            if (c == L'\\') {
                if (i >= n) { ok = false; return false; }
                wchar_t e = s[i++];
                switch (e) {
                case L'"':  out += L'"';  break;
                case L'\\': out += L'\\'; break;
                case L'/':  out += L'/';  break;
                case L'b':  out += L'\b'; break;
                case L'f':  out += L'\f'; break;
                case L'n':  out += L'\n'; break;
                case L'r':  out += L'\r'; break;
                case L't':  out += L'\t'; break;
                case L'u': {
                    if (i + 4 > n) { ok = false; return false; }
                    int v = 0;
                    for (int k = 0; k < 4; k++) {
                        wchar_t h = s[i++];
                        v <<= 4;
                        if (h >= L'0' && h <= L'9') v |= h - L'0';
                        else if (h >= L'a' && h <= L'f') v |= h - L'a' + 10;
                        else if (h >= L'A' && h <= L'F') v |= h - L'A' + 10;
                        else { ok = false; return false; }
                    }
                    out += (wchar_t)v;
                    break;
                }
                default: ok = false; return false;
                }
            } else {
                out += c;
            }
        }
        ok = false;
        return false;
    }
    bool value(Json& v) {
        ws();
        if (i >= n) { ok = false; return false; }
        wchar_t c = s[i];
        if (c == L'"') { v.t = 3; return string(v.str); }
        if (c == L'{') return object(v);
        if (c == L'[') return array(v);
        if (c == L't') { if (i + 4 <= n && !wcsncmp(s + i, L"true", 4))  { i += 4; v.t = 1; v.b = true;  return true; } ok = false; return false; }
        if (c == L'f') { if (i + 5 <= n && !wcsncmp(s + i, L"false", 5)) { i += 5; v.t = 1; v.b = false; return true; } ok = false; return false; }
        if (c == L'n') { if (i + 4 <= n && !wcsncmp(s + i, L"null", 4))  { i += 4; v.t = 0;             return true; } ok = false; return false; }
        size_t st = i;
        if (i < n && s[i] == L'-') i++;
        while (i < n && ((s[i] >= L'0' && s[i] <= L'9') || s[i] == L'.' ||
                         s[i] == L'e' || s[i] == L'E' || s[i] == L'+' || s[i] == L'-')) i++;
        if (i == st) { ok = false; return false; }
        v.t = 2;
        v.num = _wtof(std::wstring(s + st, i - st).c_str());
        return true;
    }
    bool object(Json& v) {
        v.t = 5; i++; ws();
        if (i < n && s[i] == L'}') { i++; return true; }
        while (true) {
            ws();
            std::wstring key;
            if (!string(key)) { ok = false; return false; }
            ws();
            if (i >= n || s[i] != L':') { ok = false; return false; }
            i++;
            Json cv;
            if (!value(cv)) return false;
            v.obj.push_back({ key, cv });
            ws();
            if (i < n && s[i] == L',') { i++; continue; }
            if (i < n && s[i] == L'}') { i++; return true; }
            ok = false; return false;
        }
    }
    bool array(Json& v) {
        v.t = 4; i++; ws();
        if (i < n && s[i] == L']') { i++; return true; }
        while (true) {
            Json cv;
            if (!value(cv)) return false;
            v.arr.push_back(cv);
            ws();
            if (i < n && s[i] == L',') { i++; continue; }
            if (i < n && s[i] == L']') { i++; return true; }
            ok = false; return false;
        }
    }
};

const Json* field(const Json& obj, const wchar_t* key) {
    for (auto& kv : obj.obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

// Parse the whole file into records. Returns false if the content is not a
// valid array of session objects (each with all five typed fields present).
bool parseRecords(const std::wstring& content, std::vector<Record>& out) {
    Parser p{ content.c_str(), 0, content.size() };
    Json root;
    if (!p.value(root) || !p.ok) return false;
    p.ws();
    if (p.i != p.n) return false;         // trailing garbage
    if (root.t != 4) return false;        // must be an array
    for (const Json& e : root.arr) {
        if (e.t != 5) return false;       // each element must be an object
        const Json* start   = field(e, L"start");
        const Json* planned = field(e, L"plannedMinutes");
        const Json* actual  = field(e, L"actualMinutes");
        const Json* inten   = field(e, L"intention");
        const Json* refl    = field(e, L"reflection");
        if (!start || start->t != 3) return false;
        if (!planned || planned->t != 2) return false;
        if (!actual || actual->t != 2) return false;
        if (!inten || inten->t != 3) return false;
        if (!refl || refl->t != 3) return false;
        Record r;
        r.start      = start->str;
        r.planned    = (int)planned->num;
        r.actual     = (int)actual->num;
        r.intention  = inten->str;
        r.reflection = refl->str;
        out.push_back(r);
    }
    return true;
}

// Write the JSON side: read + parse existing, append, re-encode. On a parse
// failure move the old file aside to sessions.json.corrupt and start fresh.
void appendRecord(const Record& rec) {
    std::vector<Record> records;
    std::wstring path = dataPath();
    if (fileExists(path)) {
        std::wstring content;
        bool parsed = false;
        if (readUtf8(path, content))
            parsed = parseRecords(content, records);
        if (!parsed) {
            records.clear();
            std::wstring corrupt = path + L".corrupt";
            DeleteFileW(corrupt.c_str());
            if (MoveFileW(path.c_str(), corrupt.c_str())) {
                Log::write(L"[journal] sessions.json could not be decoded; moved aside to sessions.json.corrupt");
            } else {
                Log::write(L"[journal] sessions.json could not be decoded and could not be moved aside (err=%lu); leaving it untouched",
                           GetLastError());
                return;
            }
        }
    }
    records.push_back(rec);
    if (!writeUtf8(path, encode(records)))
        Log::write(L"[journal] could not write sessions.json");
    else
        Log::write(L"[journal] sessions.json now has %zu record(s)", records.size());
}

} // namespace

// ---------------------------------------------------------------- public -----
std::wstring Journal::baseDir() {
    std::wstring root = appDataRoot();
    if (root.empty()) return L"";
    std::wstring dir = root + L"\\MindfulCompute";
    CreateDirectoryW(dir.c_str(), nullptr);   // ok if it already exists
    return dir;
}

void Journal::append(const FILETIME& startUtc, int plannedMinutes, int actualMinutes,
                     const std::wstring& intention, const std::wstring& reflection) {
    if (baseDir().empty()) {
        Log::write(L"[journal] no %%APPDATA%% base dir; skipping append");
        return;
    }
    Log::write(L"[journal] append -> %ls (%d min, planned %d)",
               journalPath().c_str(), actualMinutes, plannedMinutes);

    // JSON side first (mirrors Swift order: appendRecord before the md write).
    Record rec;
    rec.start      = isoUtc(startUtc);
    rec.planned    = plannedMinutes;
    rec.actual     = actualMinutes;
    rec.intention  = intention;
    rec.reflection = reflection;
    appendRecord(rec);

    // journal.md side.
    std::wstring entry = L"\n## " + localEntryDate(startUtc) + L" — " +
                         std::to_wstring(actualMinutes) + L" min";
    if (actualMinutes != plannedMinutes)
        entry += L" (planned " + std::to_wstring(plannedMinutes) + L")";
    entry += L"\n\n**Intention:** " + intention + L"\n";
    if (!reflection.empty())
        entry += L"\n**Reflection:** " + reflection + L"\n";

    std::wstring path = journalPath();
    std::wstring existing;
    if (fileExists(path)) {
        if (!readUtf8(path, existing)) {
            Log::write(L"[journal] journal.md exists but could not be read; skipping append to avoid clobbering it");
            return;
        }
    } else {
        existing = kHeader;
    }
    if (!writeUtf8(path, existing + entry))
        Log::write(L"[journal] could not write journal.md");
    else
        Log::write(L"[journal] journal.md appended (%d min%ls)", actualMinutes,
                   reflection.empty() ? L"" : L", reflection");
    Log::flush();   // a completed journal write is an important, disk-worthy event
}

void Journal::open() {
    std::wstring dir = baseDir();
    std::wstring path = journalPath();
    if (fileExists(path)) {
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        Log::write(L"[journal] open journal.md");
    } else if (!dir.empty()) {
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        Log::write(L"[journal] journal.md missing; opened folder");
    }
}
