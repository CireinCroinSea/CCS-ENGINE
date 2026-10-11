#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <iterator>
#include <cstdint>
#include <memory>

#include "MemoryScanner.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace {
constexpr int ID_PROCESS = 1001, ID_REFRESH = 1002, ID_TYPE = 1003, ID_VALUE = 1004,
    ID_FIRST = 1005, ID_STATUS = 1006, ID_RESULTS = 1007, ID_NEXT = 1008,
    ID_NEW = 1009, ID_MODE = 1010, ID_WRITE = 1011, ID_SETTINGS = 1012, ID_LANGUAGE = 1013,
    ID_AUTHOR = 1014, ID_INFO_PANEL = 1015, ID_THEME_FIRST = 1020, ID_THEME_LAST = 1025,
    ID_JOURNAL = 1019, ID_TOOLS = 1026;
constexpr UINT WM_SCAN_COMPLETE = WM_APP + 1;
constexpr UINT WM_TOOL_COMPLETE = WM_APP + 2, WM_DEBUG_UPDATE = WM_APP + 3;
constexpr UINT_PTR TIMER_FREEZE = 7;
constexpr size_t MAX_RESULTS = 10000;
constexpr int ID_TOOL_POINTER_TARGET = 1101, ID_TOOL_POINTER_FIND = 1102,
    ID_TOOL_PATTERN = 1103, ID_TOOL_PATTERN_SCAN = 1104, ID_TOOL_MEM_ADDRESS = 1105,
    ID_TOOL_MEM_READ = 1106, ID_TOOL_MEM_BYTES = 1107, ID_TOOL_MEM_WRITE = 1108,
    ID_TOOL_FREEZE = 1109, ID_TOOL_DEBUG = 1110, ID_TOOL_OUTPUT = 1111;

struct ThemePalette {
    const wchar_t* name;
    COLORREF background, panel, control, text, muted, accent, accentHover, border, rowAlt;
};
const ThemePalette THEMES[] = {
    { L"Midnight", RGB(15, 18, 24), RGB(22, 27, 36), RGB(30, 36, 47), RGB(230, 234, 242), RGB(143, 153, 169), RGB(91, 141, 239), RGB(111, 157, 248), RGB(52, 61, 76), RGB(25, 30, 39) },
    { L"Graphite", RGB(20, 20, 21), RGB(29, 30, 32), RGB(40, 41, 44), RGB(235, 235, 235), RGB(160, 162, 166), RGB(180, 180, 185), RGB(205, 205, 210), RGB(72, 73, 76), RGB(34, 35, 37) },
    { L"Ocean", RGB(10, 21, 31), RGB(16, 34, 48), RGB(23, 48, 65), RGB(224, 242, 248), RGB(137, 170, 184), RGB(36, 178, 196), RGB(67, 203, 216), RGB(42, 76, 91), RGB(19, 40, 55) },
    { L"Forest", RGB(15, 24, 19), RGB(23, 36, 28), RGB(32, 49, 38), RGB(228, 241, 230), RGB(148, 174, 151), RGB(91, 169, 112), RGB(119, 194, 137), RGB(53, 79, 59), RGB(26, 42, 32) },
    { L"Violet", RGB(23, 18, 31), RGB(34, 27, 46), RGB(47, 37, 62), RGB(239, 232, 249), RGB(171, 153, 191), RGB(163, 112, 225), RGB(187, 143, 242), RGB(72, 58, 91), RGB(39, 31, 52) },
    { L"Light", RGB(245, 247, 250), RGB(255, 255, 255), RGB(237, 240, 245), RGB(31, 38, 48), RGB(97, 108, 123), RGB(49, 105, 195), RGB(69, 124, 215), RGB(205, 211, 220), RGB(240, 243, 247) }
};
COLORREF COLOR_WINDOW_BG = THEMES[0].background;
COLORREF COLOR_PANEL = THEMES[0].panel;
COLORREF COLOR_CONTROL = THEMES[0].control;
COLORREF COLOR_TEXT = THEMES[0].text;
COLORREF COLOR_MUTED = THEMES[0].muted;
COLORREF COLOR_ACCENT = THEMES[0].accent;
COLORREF COLOR_ACCENT_HOVER = THEMES[0].accentHover;
COLORREF COLOR_BORDER = THEMES[0].border;
COLORREF COLOR_ROW_ALT = THEMES[0].rowAlt;
int g_themeIndex = 0;
bool g_themePickerVisible = false;
bool g_russian = false;

HWND g_main{}, g_process{}, g_type{}, g_mode{}, g_value{}, g_status{}, g_results{};
HWND g_refresh{}, g_first{}, g_next{}, g_new{}, g_write{}, g_settings{}, g_author{}, g_journal{};
HWND g_themePanel{}, g_themeButtons[6]{}, g_title{}, g_subtitle{}, g_languageButton{};
HWND g_processLabel{}, g_typeLabel{}, g_modeLabel{}, g_valueLabel{};
HWND g_infoPanel{}, g_infoTitle{}, g_infoDescription{}, g_infoTypesLabel{}, g_infoTypes{}, g_infoAuthorLabel{};
HWND g_journalWindow{}, g_journalEdit{};
std::vector<std::wstring> g_journalEntries;
HWND g_toolsButton{}, g_toolsWindow{}, g_toolPointerAddress{}, g_toolPattern{}, g_toolMemoryAddress{},
    g_toolMemoryData{}, g_toolOutput{}, g_toolFreezeButton{}, g_toolDebugButton{};
std::thread g_toolWorker, g_debugWorker;
std::atomic_bool g_toolBusy{ false }, g_toolCancel{ false }, g_debugStop{ false }, g_debugActive{ false };
DWORD g_debugPid{};
struct FrozenValue { DWORD pid{}; ValueType type{}; uintptr_t address{}; uint64_t value{}; };
std::vector<FrozenValue> g_frozenValues;
std::vector<DWORD> g_pids;
std::vector<ScanResult> g_scanResults;
std::thread g_worker;
std::atomic_bool g_cancel{false};
bool g_scanning = false;
DWORD g_sessionPid = 0;
ValueType g_sessionType = ValueType::Int32;
HBRUSH g_windowBrush{}, g_panelBrush{}, g_controlBrush{};
HFONT g_uiFont{}, g_titleFont{}, g_headingFont{};
UINT g_dpi = 96;

int Scale(int value) { return MulDiv(value, static_cast<int>(g_dpi), 96); }

struct ScanPayload {
    std::vector<ScanResult> results;
    std::wstring error;
    bool firstScan{};
    ValueType type{};
};

int ControlId(HWND control) {
    return static_cast<int>(GetDlgCtrlID(control));
}

std::wstring GetText(HWND h) {
    const int length = GetWindowTextLengthW(h);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(h, text.data(), length + 1);
    text.resize(length);
    return text;
}

void AppendJournal(const std::wstring& message) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::wostringstream line;
    line << L"[" << std::setfill(L'0') << std::setw(4) << now.wYear << L'-'
         << std::setw(2) << now.wMonth << L'-' << std::setw(2) << now.wDay << L' '
         << std::setw(2) << now.wHour << L':' << std::setw(2) << now.wMinute << L':'
         << std::setw(2) << now.wSecond << L"] " << message << L"\r\n";
    const std::wstring entry = line.str();
    g_journalEntries.push_back(entry);

    if (g_journalEdit) {
        const int end = GetWindowTextLengthW(g_journalEdit);
        SendMessageW(g_journalEdit, EM_SETSEL, end, end);
        SendMessageW(g_journalEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(entry.c_str()));
    }

    wchar_t modulePath[MAX_PATH]{};
    const DWORD pathLength = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(_countof(modulePath)));
    if (!pathLength || pathLength >= _countof(modulePath)) return;
    wchar_t* lastSlash = wcsrchr(modulePath, L'\\');
    if (!lastSlash) return;
    *(lastSlash + 1) = L'\0';
    std::wstring logPath(modulePath);
    logPath += L"CCS engine-journal.log";
    HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER fileSize{};
    if (GetFileSizeEx(file, &fileSize) && fileSize.QuadPart == 0) {
        const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        DWORD written = 0;
        WriteFile(file, bom, sizeof(bom), &written, nullptr);
    }
    const int byteCount = WideCharToMultiByte(CP_UTF8, 0, entry.data(), static_cast<int>(entry.size()),
        nullptr, 0, nullptr, nullptr);
    if (byteCount > 0) {
        std::string utf8(static_cast<size_t>(byteCount), '\0');
        WideCharToMultiByte(CP_UTF8, 0, entry.data(), static_cast<int>(entry.size()),
            utf8.data(), byteCount, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(file);
}

void SetStatus(const std::wstring& text) {
    if (g_status) SetWindowTextW(g_status, text.c_str());
}

DWORD SelectedPid() {
    const LRESULT index = SendMessageW(g_process, CB_GETCURSEL, 0, 0);
    return index >= 0 && static_cast<size_t>(index) < g_pids.size()
        ? g_pids[static_cast<size_t>(index)] : 0;
}

ValueType SelectedType() {
    switch (SendMessageW(g_type, CB_GETCURSEL, 0, 0)) {
    case 1: return ValueType::Int16;
    case 2: return ValueType::Int32;
    case 3: return ValueType::Int64;
    case 4: return ValueType::Float;
    case 5: return ValueType::Double;
    default: return ValueType::Byte;
    }
}

ScanMode SelectedMode() {
    switch (SendMessageW(g_mode, CB_GETCURSEL, 0, 0)) {
    case 1: return ScanMode::Changed;
    case 2: return ScanMode::Unchanged;
    case 3: return ScanMode::Increased;
    case 4: return ScanMode::Decreased;
    default: return ScanMode::Exact;
    }
}

std::wstring Hex(uintptr_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::uppercase << std::hex << value;
    return stream.str();
}

std::wstring FormatValue(ValueType type, uint64_t raw) {
    switch (type) {
    case ValueType::Byte: return std::to_wstring(static_cast<uint8_t>(raw));
    case ValueType::Int16: return std::to_wstring(static_cast<int16_t>(raw));
    case ValueType::Int32: return std::to_wstring(static_cast<int32_t>(raw));
    case ValueType::Int64: return std::to_wstring(static_cast<int64_t>(raw));
    case ValueType::Float: {
        float value{}; const uint32_t bits = static_cast<uint32_t>(raw);
        std::memcpy(&value, &bits, sizeof(value));
        std::wstringstream stream; stream << std::setprecision(7) << value; return stream.str();
    }
    case ValueType::Double: {
        double value{}; std::memcpy(&value, &raw, sizeof(value));
        std::wstringstream stream; stream << std::setprecision(15) << value; return stream.str();
    }
    }
    return {};
}

void RebuildFonts() {
    HFONT oldUi = g_uiFont;
    HFONT oldHeading = g_headingFont;
    HFONT oldTitle = g_titleFont;
    g_uiFont = CreateFontW(-Scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_headingFont = CreateFontW(-Scale(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_titleFont = CreateFontW(-Scale(30), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    const HWND uiControls[] = { g_subtitle, g_author, g_infoDescription, g_infoTypes, g_process, g_refresh, g_type, g_mode, g_value,
        g_new, g_first, g_next, g_write, g_settings, g_results, g_status, g_themePanel, g_languageButton,
        g_journal, g_toolsButton, g_themeButtons[0], g_themeButtons[1], g_themeButtons[2],
        g_themeButtons[3], g_themeButtons[4], g_themeButtons[5] };
    for (HWND control : uiControls)
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
    const HWND headingControls[] = { g_processLabel, g_typeLabel, g_modeLabel, g_valueLabel,
        g_infoTitle, g_infoTypesLabel, g_infoAuthorLabel };
    for (HWND control : headingControls)
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_headingFont), TRUE);
    if (g_title) SendMessageW(g_title, WM_SETFONT, reinterpret_cast<WPARAM>(g_titleFont), TRUE);
    if (g_results) {
        HWND header = ListView_GetHeader(g_results);
        if (header) SendMessageW(header, WM_SETFONT, reinterpret_cast<WPARAM>(g_headingFont), TRUE);
    }
    if (oldUi) DeleteObject(oldUi);
    if (oldHeading) DeleteObject(oldHeading);
    if (oldTitle) DeleteObject(oldTitle);
}

void ApplyTheme(int index) {
    if (index < 0 || index >= static_cast<int>(_countof(THEMES))) return;
    g_themeIndex = index;
    const ThemePalette& theme = THEMES[index];
    COLOR_WINDOW_BG = theme.background;
    COLOR_PANEL = theme.panel;
    COLOR_CONTROL = theme.control;
    COLOR_TEXT = theme.text;
    COLOR_MUTED = theme.muted;
    COLOR_ACCENT = theme.accent;
    COLOR_ACCENT_HOVER = theme.accentHover;
    COLOR_BORDER = theme.border;
    COLOR_ROW_ALT = theme.rowAlt;

    if (g_windowBrush) DeleteObject(g_windowBrush);
    if (g_panelBrush) DeleteObject(g_panelBrush);
    if (g_controlBrush) DeleteObject(g_controlBrush);
    g_windowBrush = CreateSolidBrush(COLOR_WINDOW_BG);
    g_panelBrush = CreateSolidBrush(COLOR_PANEL);
    g_controlBrush = CreateSolidBrush(COLOR_CONTROL);

    if (g_results) {
        ListView_SetBkColor(g_results, COLOR_PANEL);
        ListView_SetTextBkColor(g_results, COLOR_PANEL);
        ListView_SetTextColor(g_results, COLOR_TEXT);
    }
    const wchar_t* visualTheme = index == 5 ? L"Explorer" : L"DarkMode_Explorer";
    const HWND themedControls[] = { g_process, g_type, g_mode, g_value, g_results };
    for (HWND control : themedControls) if (control) SetWindowTheme(control, visualTheme, nullptr);
    if (g_results) {
        HWND header = ListView_GetHeader(g_results);
        if (header) SetWindowTheme(header, visualTheme, nullptr);
    }
    if (g_main) {
        const BOOL useDarkCaption = index != 5;
        DwmSetWindowAttribute(g_main, 20, &useDarkCaption, sizeof(useDarkCaption));
        RedrawWindow(g_main, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    if (g_journalWindow)
        RedrawWindow(g_journalWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    if (g_toolsWindow)
        RedrawWindow(g_toolsWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

LRESULT CALLBACK ThemePickerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK JournalWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
void ToggleThemePicker();
void OpenJournalWindow();
LRESULT CALLBACK ToolsWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
void OpenToolsWindow();
void ApplyLanguage();

void SetSessionControls() {
    const bool hasResults = !g_scanResults.empty();
    EnableWindow(g_next, hasResults && !g_scanning);
    EnableWindow(g_write, hasResults && !g_scanning);
    EnableWindow(g_first, !g_scanning);
    EnableWindow(g_new, !g_scanning);
    EnableWindow(g_process, !g_scanning);
    EnableWindow(g_type, !g_scanning);
    EnableWindow(g_mode, !g_scanning);
    EnableWindow(g_value, !g_scanning);
    EnableWindow(g_refresh, !g_scanning);
}

void ResetSession(bool updateStatus = true) {
    g_scanResults.clear();
    g_sessionPid = 0;
    ListView_DeleteAllItems(g_results);
    SetSessionControls();
    if (updateStatus) SetStatus(L"Ready  /  Choose a process, type and value to start a scan.");
}

void RefreshProcesses() {
    SendMessageW(g_process, CB_RESETCONTENT, 0, 0);
    g_pids.clear();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        SetStatus(L"Could not enumerate running processes.");
        return;
    }

    PROCESSENTRY32W entry{ sizeof(entry) };
    if (Process32FirstW(snapshot, &entry)) {
        do {
            std::wstring label = entry.szExeFile + std::wstring(L"   ·   PID ") +
                std::to_wstring(entry.th32ProcessID);
            const LRESULT index = SendMessageW(g_process, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(label.c_str()));
            if (index >= 0) {
                const size_t slot = static_cast<size_t>(index);
                if (slot >= g_pids.size()) g_pids.resize(slot + 1);
                g_pids[slot] = entry.th32ProcessID;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (SendMessageW(g_process, CB_GETCOUNT, 0, 0) > 0)
        SendMessageW(g_process, CB_SETCURSEL, 0, 0);
    ResetSession(false);
    SetStatus(L"Process list updated. Select a process to begin.");
}

void AddResult(const ScanResult& result, int row, ValueType type) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    std::wstring address = Hex(result.address);
    item.pszText = address.data();
    const int index = ListView_InsertItem(g_results, &item);
    std::wstring value = FormatValue(type, result.value);
    ListView_SetItemText(g_results, index, 1, value.data());
    std::wstring bytes = std::to_wstring(type == ValueType::Byte ? 1 :
        (type == ValueType::Int16 ? 2 : ((type == ValueType::Int32 || type == ValueType::Float) ? 4 : 8)));
    ListView_SetItemText(g_results, index, 2, bytes.data());
}

void WriteSelectedValues() {
    if (g_scanning) return;
    const DWORD pid = SelectedPid();
    const ValueType type = SelectedType();
    if (!pid || pid != g_sessionPid || type != g_sessionType) {
        SetStatus(L"Run a scan for the selected process and value type first.");
        return;
    }

    std::vector<int> rows;
    std::vector<uintptr_t> addresses;
    for (int row = ListView_GetNextItem(g_results, -1, LVNI_SELECTED); row >= 0;
         row = ListView_GetNextItem(g_results, row, LVNI_SELECTED)) {
        if (static_cast<size_t>(row) >= g_scanResults.size()) continue;
        rows.push_back(row);
        addresses.push_back(g_scanResults[static_cast<size_t>(row)].address);
    }
    if (addresses.empty()) {
        SetStatus(L"Select one or more result rows, then choose Set selected.");
        return;
    }

    uint64_t newValue = 0;
    std::vector<uint64_t> previousValues;
    std::vector<bool> previousValuesRead;
    std::wstring error;
    const std::vector<bool> written = MemoryScanner::WriteValues(
        pid, type, addresses, GetText(g_value), newValue, previousValues, previousValuesRead, error);
    size_t successCount = 0;
    for (size_t i = 0; i < written.size(); ++i) {
        if (!written[i]) {
            AppendJournal((g_russian ? L"Не удалось изменить PID " : L"Write failed for PID ") +
                std::to_wstring(pid) + L" | " + Hex(addresses[i]));
            continue;
        }
        ++successCount;
        ScanResult& result = g_scanResults[static_cast<size_t>(rows[i])];
        AppendJournal((g_russian ? L"Изменено PID " : L"Changed PID ") + std::to_wstring(pid) +
            L" | " + Hex(addresses[i]) + L" | " + GetText(g_type) + L" | " +
            (previousValuesRead[i] ? FormatValue(type, previousValues[i]) : (g_russian ? L"не прочитано" : L"unreadable")) +
            L" -> " + FormatValue(type, newValue));
        result.value = newValue;
        std::wstring valueText = FormatValue(type, newValue);
        ListView_SetItemText(g_results, rows[i], 1, valueText.data());
    }
    std::wstring status = L"Updated " + std::to_wstring(successCount) + L" of " +
        std::to_wstring(addresses.size()) + L" selected value(s).";
    if (!error.empty()) status += L"  " + error;
    else if (successCount < addresses.size())
        status += L"  Some addresses are read-only or no longer available.";
    SetStatus(status);
    AppendJournal((g_russian ? L"Результат изменения: " : L"Write summary: ") +
        std::to_wstring(successCount) + L"/" + std::to_wstring(addresses.size()));
}

void BeginScan(bool nextScan) {
    if (g_scanning) {
        g_cancel.store(true, std::memory_order_relaxed);
        SetStatus(L"Stopping scan…");
        return;
    }
    const DWORD pid = SelectedPid();
    const ValueType type = SelectedType();
    const ScanMode mode = SelectedMode();
    const std::wstring value = GetText(g_value);
    if (!pid) { SetStatus(L"Select a process first."); return; }
    if (nextScan && (g_scanResults.empty() || pid != g_sessionPid || type != g_sessionType)) {
        SetStatus(L"Run a first scan with this process and value type before filtering.");
        return;
    }
    if ((!nextScan || mode == ScanMode::Exact) && value.empty()) {
        SetStatus(L"Enter a value for the exact-value scan.");
        return;
    }
    if (g_worker.joinable()) g_worker.join();

    std::vector<ScanResult> previous;
    if (nextScan) previous = g_scanResults;
    g_cancel.store(false, std::memory_order_relaxed);
    g_scanning = true;
    SetSessionControls();
    SetStatus(nextScan ? L"Filtering previous results against current memory…" :
        L"Scanning readable committed memory…");
    AppendJournal((g_russian ? (nextScan ? L"Уточнение поиска PID " : L"Первый поиск PID ") :
        (nextScan ? L"Refine scan PID " : L"First scan PID ")) + std::to_wstring(pid) +
        L" | " + GetText(g_type) + L" | " + GetText(g_mode) + L" | " + value);

    g_worker = std::thread([pid, type, mode, value, previous = std::move(previous), nextScan]() mutable {
        auto payload = std::make_unique<ScanPayload>();
        payload->firstScan = !nextScan;
        payload->type = type;
        if (nextScan) {
            payload->results = MemoryScanner::ScanNext(pid, type, previous, mode, value,
                MAX_RESULTS, &g_cancel, payload->error);
        } else {
            payload->results = MemoryScanner::ScanFirst(pid, type, value,
                MAX_RESULTS, &g_cancel, payload->error);
        }
        ScanPayload* rawPayload = payload.release();
        if (!PostMessageW(g_main, WM_SCAN_COMPLETE, 0, reinterpret_cast<LPARAM>(rawPayload)))
            delete rawPayload;
    });
}

HWND CreateLabel(HWND parent, const wchar_t* text, int id) {
    HWND label = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 100, 22, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(g_headingFont), TRUE);
    return label;
}

HWND CreateButton(HWND parent, const wchar_t* text, int id, bool primary = false) {
    HWND button = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 100, 38, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(primary ? g_headingFont : g_uiFont), TRUE);
    return button;
}

void Layout(int width, int height) {
    const int margin = Scale(26);
    const int available = (std::max)(Scale(300), width - margin * 2);
    const int infoWidth = Scale(280);
    const int infoGap = Scale(18);
    if (g_title) MoveWindow(g_title, margin, Scale(20), Scale(420), Scale(36), TRUE);
    if (g_subtitle) MoveWindow(g_subtitle, margin + Scale(1), Scale(58), available, Scale(24), TRUE);
    if (g_author) MoveWindow(g_author, margin + Scale(236), Scale(27), Scale(250), Scale(30), TRUE);
    if (g_settings) MoveWindow(g_settings, width - margin - Scale(96), Scale(26), Scale(96), Scale(36), TRUE);
    if (g_journal) MoveWindow(g_journal, width - margin - Scale(208), Scale(26), Scale(102), Scale(36), TRUE);
    if (g_toolsButton) MoveWindow(g_toolsButton, width - margin - Scale(310), Scale(26), Scale(90), Scale(36), TRUE);

    const int rowY = Scale(126);
    const int labelY = Scale(101);
    const int processWidth = (std::max)(Scale(220), available * 30 / 100);
    const int refreshWidth = Scale(88);
    const int typeWidth = Scale(150);
    const int modeWidth = Scale(200);
    const int valueWidth = (std::max)(Scale(130), available - processWidth - refreshWidth - typeWidth - modeWidth - Scale(66));
    int x = margin;
    MoveWindow(g_processLabel, x, labelY, processWidth, Scale(20), TRUE);
    MoveWindow(g_process, x, rowY, processWidth, Scale(260), TRUE);
    x += processWidth + Scale(10);
    MoveWindow(g_refresh, x, rowY, refreshWidth, Scale(36), TRUE);
    x += refreshWidth + Scale(20);
    MoveWindow(g_typeLabel, x, labelY, typeWidth, Scale(20), TRUE);
    MoveWindow(g_type, x, rowY, typeWidth, Scale(180), TRUE);
    x += typeWidth + Scale(18);
    MoveWindow(g_modeLabel, x, labelY, modeWidth, Scale(20), TRUE);
    MoveWindow(g_mode, x, rowY, modeWidth, Scale(200), TRUE);
    x += modeWidth + Scale(18);
    MoveWindow(g_valueLabel, x, labelY, valueWidth, Scale(20), TRUE);
    MoveWindow(g_value, x, rowY, valueWidth, Scale(36), TRUE);

    MoveWindow(g_new, margin, Scale(178), Scale(126), Scale(40), TRUE);
    MoveWindow(g_first, margin + Scale(138), Scale(178), Scale(142), Scale(40), TRUE);
    MoveWindow(g_next, margin + Scale(292), Scale(178), Scale(142), Scale(40), TRUE);
    MoveWindow(g_write, margin + Scale(446), Scale(178), Scale(158), Scale(40), TRUE);
    const int listTop = Scale(238);
    const int resultsWidth = (std::max)(Scale(560), available - infoWidth - infoGap);
    const int infoX = margin + resultsWidth + infoGap;
    const int contentHeight = (std::max)(Scale(140), height - listTop - Scale(72));
    MoveWindow(g_results, margin, listTop, resultsWidth, contentHeight, TRUE);
    if (g_infoPanel) MoveWindow(g_infoPanel, infoX, listTop, infoWidth, contentHeight, TRUE);
    if (g_infoTitle) MoveWindow(g_infoTitle, infoX + Scale(16), listTop + Scale(16), infoWidth - Scale(32), Scale(24), TRUE);
    if (g_infoDescription) MoveWindow(g_infoDescription, infoX + Scale(16), listTop + Scale(48), infoWidth - Scale(32), Scale(112), TRUE);
    if (g_infoTypesLabel) MoveWindow(g_infoTypesLabel, infoX + Scale(16), listTop + Scale(172), infoWidth - Scale(32), Scale(22), TRUE);
    if (g_infoTypes) MoveWindow(g_infoTypes, infoX + Scale(16), listTop + Scale(198), infoWidth - Scale(32), Scale(84), TRUE);
    if (g_infoAuthorLabel) ShowWindow(g_infoAuthorLabel, SW_HIDE);
    MoveWindow(g_status, margin, height - Scale(47), available, Scale(25), TRUE);

}

LRESULT DrawButton(const DRAWITEMSTRUCT* draw) {
    const int id = static_cast<int>(draw->CtlID);
    if (id == ID_INFO_PANEL) {
        FillRect(draw->hDC, &draw->rcItem, g_panelBrush);
        FrameRect(draw->hDC, &draw->rcItem, g_controlBrush);
        return TRUE;
    }
    if (id >= ID_THEME_FIRST && id <= ID_THEME_LAST) {
        const int themeIndex = id - ID_THEME_FIRST;
        const ThemePalette& palette = THEMES[themeIndex];
        HBRUSH brush = CreateSolidBrush(palette.panel);
        HPEN pen = CreatePen(PS_SOLID, Scale(themeIndex == g_themeIndex ? 2 : 1),
            themeIndex == g_themeIndex ? palette.accent : palette.border);
        HGDIOBJ oldBrush = SelectObject(draw->hDC, brush);
        HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
        RoundRect(draw->hDC, draw->rcItem.left, draw->rcItem.top,
            draw->rcItem.right, draw->rcItem.bottom, Scale(8), Scale(8));
        SelectObject(draw->hDC, oldBrush); SelectObject(draw->hDC, oldPen);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, palette.text);
        RECT textRect = draw->rcItem;
        InflateRect(&textRect, -Scale(8), -Scale(5));
        DrawTextW(draw->hDC, palette.name, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(brush); DeleteObject(pen);
        return TRUE;
    }
    const bool primary = draw->CtlID == ID_FIRST || draw->CtlID == ID_NEXT;
    const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
    COLORREF fill = primary ? COLOR_ACCENT : COLOR_CONTROL;
    if (disabled) fill = COLOR_PANEL;
    else if ((draw->itemState & ODS_HOTLIGHT) && primary) fill = COLOR_ACCENT_HOVER;
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, Scale(1), primary ? fill : COLOR_BORDER);
    HGDIOBJ oldBrush = SelectObject(draw->hDC, brush);
    HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
    RoundRect(draw->hDC, draw->rcItem.left, draw->rcItem.top,
        draw->rcItem.right, draw->rcItem.bottom, 8, 8);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, disabled ? COLOR_MUTED : COLOR_TEXT);
    wchar_t text[128]{};
    GetWindowTextW(draw->hwndItem, text, static_cast<int>(_countof(text)));
    DrawTextW(draw->hDC, text, -1, const_cast<RECT*>(&draw->rcItem),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(draw->hDC, oldBrush);
    SelectObject(draw->hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    return TRUE;
}

LRESULT CALLBACK ThemePickerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        for (int i = 0; i < 6; ++i) {
            const int x = Scale(12) + (i % 2) * Scale(120);
            const int y = Scale(12) + (i / 2) * Scale(62);
            g_themeButtons[i] = CreateWindowW(L"BUTTON", THEMES[i].name,
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x, y, Scale(108), Scale(48), window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_THEME_FIRST + i)),
                reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);
            if (g_themeButtons[i])
                SendMessageW(g_themeButtons[i], WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
        }
        g_languageButton = CreateWindowW(L"BUTTON", L"Language: English",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, Scale(12), Scale(190), Scale(234), Scale(36), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LANGUAGE)),
            reinterpret_cast<LPCREATESTRUCTW>(lParam)->hInstance, nullptr);
        if (g_languageButton)
            SendMessageW(g_languageButton, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
        return 0;
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, g_panelBrush);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        FillRect(dc, &client, g_panelBrush);
        FrameRect(dc, &client, g_controlBrush);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DRAWITEM:
        return DrawButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_LANGUAGE) {
            g_russian = !g_russian;
            ApplyLanguage();
            SetWindowTextW(g_languageButton, g_russian ? L"Язык: Русский" : L"Language: English");
            InvalidateRect(g_languageButton, nullptr, TRUE);
            InvalidateRect(g_main, nullptr, TRUE);
            return 0;
        }
        if (LOWORD(wParam) >= ID_THEME_FIRST && LOWORD(wParam) <= ID_THEME_LAST) {
            ApplyTheme(LOWORD(wParam) - ID_THEME_FIRST);
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(window); return 0; }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (g_themePanel == window) {
            g_themePanel = nullptr;
            g_languageButton = nullptr;
            g_themePickerVisible = false;
            std::fill(std::begin(g_themeButtons), std::end(g_themeButtons), nullptr);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void ToggleThemePicker() {
    if (g_themePanel) {
        DestroyWindow(g_themePanel);
        return;
    }
    RECT anchor{};
    GetWindowRect(g_settings, &anchor);
    const int width = Scale(258), height = Scale(238);
    int x = anchor.right - width;
    int y = anchor.bottom;
    MONITORINFO monitorInfo{ sizeof(monitorInfo) };
    const HMONITOR monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        const RECT& work = monitorInfo.rcWork;
        if (x + width > work.right) x = work.right - width;
        if (x < work.left) x = work.left;
        if (y + height > work.bottom) y = anchor.top - height;
    }
    g_themePanel = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"CCSThemePickerWindow", L"", WS_POPUP | WS_CLIPCHILDREN,
        x, y, width, height, g_main, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (g_themePanel) {
        g_themePickerVisible = true;
        ShowWindow(g_themePanel, SW_SHOWNORMAL);
        UpdateWindow(g_themePanel);
    }
}

void CreateControls(HWND window) {
    RebuildFonts();

    g_title = CreateWindowW(L"STATIC", L"CCS engine", WS_CHILD | WS_VISIBLE,
        0, 0, 300, 38, window, nullptr, nullptr, nullptr);
    g_subtitle = CreateWindowW(L"STATIC", L"Process memory scanner  ·  First scan, narrow results, edit selected values",
        WS_CHILD | WS_VISIBLE, 0, 0, 600, 24, window, nullptr, nullptr, nullptr);
    g_infoPanel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, 100, 100, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_INFO_PANEL)), nullptr, nullptr);
    g_infoTitle = CreateLabel(window, L"PROCESS MEMORY SCANNER", ID_INFO_PANEL + 1);
    g_infoDescription = CreateWindowW(L"STATIC",
        L"Search readable process memory, refine results with comparisons, and update selected values.",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 112, window, nullptr, nullptr, nullptr);
    g_infoTypesLabel = CreateLabel(window, L"VALUE TYPES", ID_INFO_PANEL + 2);
    g_infoTypes = CreateWindowW(L"STATIC", L"Byte  ·  2 Bytes\n4 Bytes  ·  8 Bytes\nFloat  ·  Double",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 84, window, nullptr, nullptr, nullptr);
    g_infoAuthorLabel = CreateLabel(window, L"", ID_INFO_PANEL + 3);
    ShowWindow(g_infoAuthorLabel, SW_HIDE);
    g_author = CreateWindowW(L"STATIC", L"Author — cireincroinsea",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY, 0, 0, 250, 30, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_AUTHOR)), nullptr, nullptr);
    SendMessageW(g_title, WM_SETFONT, reinterpret_cast<WPARAM>(g_titleFont), TRUE);
    SendMessageW(g_subtitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
    SendMessageW(g_author, WM_SETFONT, reinterpret_cast<WPARAM>(g_headingFont), TRUE);
    g_settings = CreateButton(window, L"Settings", ID_SETTINGS);
    g_journal = CreateButton(window, L"Journal", ID_JOURNAL);
    g_toolsButton = CreateButton(window, L"Tools", ID_TOOLS);

    g_processLabel = CreateLabel(window, L"PROCESS", ID_PROCESS);
    g_process = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 300, 260, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PROCESS + 100)), nullptr, nullptr);
    g_refresh = CreateButton(window, L"Refresh", ID_REFRESH);
    g_typeLabel = CreateLabel(window, L"VALUE TYPE", ID_TYPE);
    g_type = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        0, 0, 150, 180, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TYPE)), nullptr, nullptr);
    const wchar_t* types[] = { L"Byte", L"2 Bytes", L"4 Bytes", L"8 Bytes", L"Float", L"Double" };
    for (const wchar_t* type : types) SendMessageW(g_type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(type));
    SendMessageW(g_type, CB_SETCURSEL, 2, 0);

    g_modeLabel = CreateLabel(window, L"COMPARE", ID_MODE);
    g_mode = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        0, 0, 200, 200, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_MODE)), nullptr, nullptr);
    const wchar_t* modes[] = { L"Exact value", L"Changed", L"Unchanged", L"Increased", L"Decreased" };
    for (const wchar_t* mode : modes) SendMessageW(g_mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode));
    SendMessageW(g_mode, CB_SETCURSEL, 0, 0);

    g_valueLabel = CreateLabel(window, L"VALUE", ID_VALUE);
    g_value = CreateWindowW(L"EDIT", L"100", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 180, 36, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_VALUE)), nullptr, nullptr);

    g_new = CreateButton(window, L"New scan", ID_NEW);
    g_first = CreateButton(window, L"First scan", ID_FIRST, true);
    g_next = CreateButton(window, L"Next scan", ID_NEXT, true);
    g_write = CreateButton(window, L"Set selected", ID_WRITE);
    g_results = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE |
        LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 700, 400, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RESULTS)), nullptr, nullptr);
    ListView_SetExtendedListViewStyle(g_results,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
    ListView_SetBkColor(g_results, COLOR_PANEL);
    ListView_SetTextBkColor(g_results, COLOR_PANEL);
    ListView_SetTextColor(g_results, COLOR_TEXT);
    SetWindowTheme(g_results, L"DarkMode_Explorer", nullptr);
    HWND header = ListView_GetHeader(g_results);
    if (header) SetWindowTheme(header, L"DarkMode_Explorer", nullptr);
    SetWindowTheme(g_process, L"DarkMode_Explorer", nullptr);
    SetWindowTheme(g_type, L"DarkMode_Explorer", nullptr);
    SetWindowTheme(g_mode, L"DarkMode_Explorer", nullptr);
    SetWindowTheme(g_value, L"DarkMode_Explorer", nullptr);
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = const_cast<wchar_t*>(L"ADDRESS"); column.cx = Scale(240); ListView_InsertColumn(g_results, 0, &column);
    column.pszText = const_cast<wchar_t*>(L"CURRENT VALUE"); column.cx = Scale(220); ListView_InsertColumn(g_results, 1, &column);
    column.pszText = const_cast<wchar_t*>(L"BYTES"); column.cx = Scale(90); ListView_InsertColumn(g_results, 2, &column);

    g_status = CreateWindowW(L"STATIC", L"Starting…", WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 700, 25, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), nullptr, nullptr);

    RebuildFonts();

    RefreshProcesses();
    SetSessionControls();
}

void ApplyLanguage() {
    const wchar_t* processLabel = g_russian ? L"ПРОЦЕСС" : L"PROCESS";
    const wchar_t* typeLabel = g_russian ? L"ТИП ЗНАЧЕНИЯ" : L"VALUE TYPE";
    const wchar_t* compareLabel = g_russian ? L"СРАВНЕНИЕ" : L"COMPARE";
    SetWindowTextW(g_subtitle, g_russian
        ? L"Сканирование памяти процесса · поиск, уточнение результатов и изменение значений"
        : L"Process memory scanner  ·  First scan, narrow results, edit selected values");
    SetWindowTextW(g_settings, g_russian ? L"Настройки" : L"Settings");
    SetWindowTextW(g_journal, g_russian ? L"Журнал" : L"Journal");
    SetWindowTextW(g_toolsButton, g_russian ? L"Средства" : L"Tools");
    if (g_journalWindow)
        SetWindowTextW(g_journalWindow, g_russian ? L"Журнал CCS engine" : L"CCS engine journal");
    if (g_toolsWindow)
        SetWindowTextW(g_toolsWindow, g_russian ? L"Инструменты памяти" : L"Memory tools");
    SetWindowTextW(g_processLabel, processLabel);
    SetWindowTextW(g_typeLabel, typeLabel);
    SetWindowTextW(g_modeLabel, compareLabel);
    SetWindowTextW(g_valueLabel, g_russian ? L"ЗНАЧЕНИЕ" : L"VALUE");
    SetWindowTextW(g_refresh, g_russian ? L"Обновить" : L"Refresh");
    SetWindowTextW(g_new, g_russian ? L"Новый поиск" : L"New scan");
    SetWindowTextW(g_first, g_russian ? L"Первый поиск" : L"First scan");
    SetWindowTextW(g_next, g_russian ? L"Следующий" : L"Next scan");
    SetWindowTextW(g_write, g_russian ? L"Изменить выбранное" : L"Set selected");
    SetWindowTextW(g_infoTitle, g_russian ? L"СКАНЕР ПАМЯТИ ПРОЦЕССОВ" : L"PROCESS MEMORY SCANNER");
    SetWindowTextW(g_infoDescription, g_russian
        ? L"Поиск в доступной памяти процесса, уточнение результатов сравнением и изменение выбранных значений."
        : L"Search readable process memory, refine results with comparisons, and update selected values.");
    SetWindowTextW(g_infoTypesLabel, g_russian ? L"ТИПЫ ЗНАЧЕНИЙ" : L"VALUE TYPES");
    SetWindowTextW(g_infoTypes, g_russian
        ? L"1 байт  ·  2 байта\n4 байта  ·  8 байт\nFloat  ·  Double"
        : L"Byte  ·  2 Bytes\n4 Bytes  ·  8 Bytes\nFloat  ·  Double");
    SetWindowTextW(g_infoAuthorLabel, L"");
    SetWindowTextW(g_author, g_russian ? L"Автор — cireincroinsea" : L"Author — cireincroinsea");

    const int selectedType = static_cast<int>(SendMessageW(g_type, CB_GETCURSEL, 0, 0));
    const wchar_t* typesEn[] = { L"Byte", L"2 Bytes", L"4 Bytes", L"8 Bytes", L"Float", L"Double" };
    const wchar_t* typesRu[] = { L"1 байт", L"2 байта", L"4 байта", L"8 байт", L"Float", L"Double" };
    SendMessageW(g_type, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 6; ++i)
        SendMessageW(g_type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g_russian ? typesRu[i] : typesEn[i]));
    SendMessageW(g_type, CB_SETCURSEL, selectedType >= 0 ? selectedType : 2, 0);

    const int selectedMode = static_cast<int>(SendMessageW(g_mode, CB_GETCURSEL, 0, 0));
    const wchar_t* modesEn[] = { L"Exact value", L"Changed", L"Unchanged", L"Increased", L"Decreased" };
    const wchar_t* modesRu[] = { L"Точное значение", L"Изменилось", L"Не изменилось", L"Увеличилось", L"Уменьшилось" };
    SendMessageW(g_mode, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 5; ++i)
        SendMessageW(g_mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g_russian ? modesRu[i] : modesEn[i]));
    SendMessageW(g_mode, CB_SETCURSEL, selectedMode >= 0 ? selectedMode : 0, 0);

    const wchar_t* headers[] = { g_russian ? L"АДРЕС" : L"ADDRESS",
        g_russian ? L"ТЕКУЩЕЕ ЗНАЧЕНИЕ" : L"CURRENT VALUE", g_russian ? L"БАЙТЫ" : L"BYTES" };
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = const_cast<wchar_t*>(headers[i]);
        ListView_SetColumn(g_results, i, &column);
    }
    SetStatus(g_russian ? L"Список процессов обновлён. Выберите процесс для начала." :
        L"Process list updated. Select a process to begin.");
}

LRESULT CALLBACK JournalWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_journalWindow = window;
        g_journalEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 100, 100, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (g_journalEdit) {
            SendMessageW(g_journalEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
            SetWindowTheme(g_journalEdit, g_themeIndex == 5 ? L"Explorer" : L"DarkMode_Explorer", nullptr);
            std::wstring allEntries;
            for (const auto& entry : g_journalEntries) allEntries += entry;
            SetWindowTextW(g_journalEdit, allEntries.c_str());
            const int end = GetWindowTextLengthW(g_journalEdit);
            SendMessageW(g_journalEdit, EM_SETSEL, end, end);
        }
        return 0;
    }
    case WM_SIZE:
        if (g_journalEdit) MoveWindow(g_journalEdit, Scale(12), Scale(12),
            (std::max)(0, LOWORD(lParam) - Scale(24)), (std::max)(0, HIWORD(lParam) - Scale(24)), TRUE);
        return 0;
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, g_windowBrush);
        return 1;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_CONTROL);
        return reinterpret_cast<LRESULT>(g_controlBrush);
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (g_journalWindow == window) {
            g_journalWindow = nullptr;
            g_journalEdit = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void OpenJournalWindow() {
    if (g_journalWindow) {
        ShowWindow(g_journalWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_journalWindow);
        return;
    }
    RECT owner{};
    GetWindowRect(g_main, &owner);
    const int width = Scale(760), height = Scale(480);
    const int x = owner.left + ((owner.right - owner.left) - width) / 2;
    const int y = owner.top + ((owner.bottom - owner.top) - height) / 2;
    const wchar_t* title = g_russian ? L"Журнал CCS engine" : L"CCS engine journal";
    g_journalWindow = CreateWindowExW(WS_EX_TOOLWINDOW, L"CCSJournalWindow", title,
        WS_OVERLAPPEDWINDOW, x, y, width, height, g_main, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (g_journalWindow) {
        ShowWindow(g_journalWindow, SW_SHOWNORMAL);
        UpdateWindow(g_journalWindow);
    }
}

struct ToolSearchPayload {
    bool pointers{};
    DWORD pid{};
    uintptr_t target{};
    size_t count{};
    std::vector<uintptr_t> addresses;
    std::wstring error;
};

struct DebugUpdate { bool attached{}; bool failed{}; std::wstring message; bool stateChange{}; };

void AppendToolsOutput(const std::wstring& text) {
    if (!g_toolOutput) return;
    const int end = GetWindowTextLengthW(g_toolOutput);
    SendMessageW(g_toolOutput, EM_SETSEL, end, end);
    SendMessageW(g_toolOutput, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

bool ParseAddress(const std::wstring& text, uintptr_t& address) {
    if (text.empty()) return false;
    wchar_t* end = nullptr;
    const unsigned long long parsed = std::wcstoull(text.c_str(), &end, 0);
    if (end == text.c_str() || *end || parsed > std::numeric_limits<uintptr_t>::max()) return false;
    address = static_cast<uintptr_t>(parsed);
    return true;
}

bool ParseHexBytes(const std::wstring& text, std::vector<int>& bytes, bool wildcards) {
    bytes.clear();
    std::wistringstream input(text);
    std::wstring token;
    while (input >> token) {
        if (wildcards && (token == L"?" || token == L"??")) bytes.push_back(-1);
        else {
            if (token.size() != 2) return false;
            wchar_t* end = nullptr;
            const unsigned long value = std::wcstoul(token.c_str(), &end, 16);
            if (end == token.c_str() || *end || value > 0xff) return false;
            bytes.push_back(static_cast<int>(value));
        }
        if (bytes.size() > 256) return false;
    }
    return !bytes.empty();
}

std::wstring HexBytes(const std::vector<unsigned char>& bytes) {
    std::wostringstream out;
    out << std::uppercase << std::hex << std::setfill(L'0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i) out << L' ';
        out << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return out.str();
}

void StartToolSearch(bool pointers, uintptr_t target, std::vector<int> pattern) {
    if (g_toolBusy.load()) {
        AppendToolsOutput(g_russian ? L"Поиск уже выполняется.\r\n" : L"A search is already running.\r\n");
        return;
    }
    if (g_toolWorker.joinable()) g_toolWorker.join();
    const DWORD pid = SelectedPid();
    if (!pid) {
        AppendToolsOutput(g_russian ? L"Сначала выберите процесс.\r\n" : L"Select a process first.\r\n");
        return;
    }
    g_toolCancel.store(false);
    g_toolBusy.store(true);
    g_toolWorker = std::thread([pointers, target, pattern = std::move(pattern), pid]() mutable {
        auto payload = std::make_unique<ToolSearchPayload>();
        payload->pointers = pointers;
        payload->pid = pid;
        payload->target = target;
        payload->addresses = pointers
            ? MemoryScanner::FindDirectPointers(pid, target, 5000, &g_toolCancel, payload->error)
            : MemoryScanner::ScanBytePattern(pid, pattern, 5000, &g_toolCancel, payload->error);
        payload->count = payload->addresses.size();
        auto* raw = payload.release();
        if (!PostMessageW(g_main, WM_TOOL_COMPLETE, 0, reinterpret_cast<LPARAM>(raw))) delete raw;
    });
}

void ToggleDebugger();

void ToggleFreezeSelected() {
    if (g_scanning) return;
    const DWORD pid = SelectedPid();
    if (!pid || pid != g_sessionPid || g_sessionType != SelectedType()) {
        SetStatus(g_russian ? L"Сначала выполните поиск для выбранного процесса и типа." :
            L"Run a scan for the selected process and type first.");
        return;
    }
    size_t changed = 0;
    for (int row = ListView_GetNextItem(g_results, -1, LVNI_SELECTED); row >= 0;
         row = ListView_GetNextItem(g_results, row, LVNI_SELECTED)) {
        if (static_cast<size_t>(row) >= g_scanResults.size()) continue;
        const uintptr_t address = g_scanResults[static_cast<size_t>(row)].address;
        auto found = std::find_if(g_frozenValues.begin(), g_frozenValues.end(),
            [pid, address](const FrozenValue& item) { return item.pid == pid && item.address == address; });
        if (found != g_frozenValues.end()) {
            g_frozenValues.erase(found);
            AppendJournal((g_russian ? L"Снята фиксация " : L"Unfroze ") + Hex(address));
        } else {
            if (g_frozenValues.size() >= 256) {
                SetStatus(g_russian ? L"Одновременно можно фиксировать не более 256 значений." :
                    L"Freeze is limited to 256 values at a time.");
                continue;
            }
            const uint64_t value = g_scanResults[static_cast<size_t>(row)].value;
            g_frozenValues.push_back({ pid, g_sessionType, address, value });
            AppendJournal((g_russian ? L"Зафиксировано PID " : L"Frozen PID ") + std::to_wstring(pid) +
                L" | " + Hex(address) + L" = " + FormatValue(g_sessionType, value));
        }
        ++changed;
    }
    if (g_frozenValues.empty()) KillTimer(g_main, TIMER_FREEZE);
    else SetTimer(g_main, TIMER_FREEZE, 500, nullptr);
    if (g_toolFreezeButton) {
        const std::wstring freezeCaption = (g_russian ? L"Снять/поставить фиксацию (" : L"Freeze/unfreeze selected (") +
            std::to_wstring(g_frozenValues.size()) + L")";
        SetWindowTextW(g_toolFreezeButton, freezeCaption.c_str());
    }
    SetStatus((g_russian ? L"Зафиксировано значений: " : L"Frozen values: ") +
        std::to_wstring(g_frozenValues.size()));
    if (!changed) SetStatus(g_russian ? L"Выберите результаты в списке." : L"Select result rows first.");
}

void MaintainFrozenValues() {
    std::vector<bool> processed(g_frozenValues.size(), false);
    for (size_t i = 0; i < g_frozenValues.size(); ++i) {
        if (processed[i]) continue;
        const DWORD pid = g_frozenValues[i].pid;
        const ValueType type = g_frozenValues[i].type;
        const uint64_t value = g_frozenValues[i].value;
        std::vector<uintptr_t> addresses;
        for (size_t j = i; j < g_frozenValues.size(); ++j) {
            if (processed[j]) continue;
            const auto& item = g_frozenValues[j];
            if (item.pid == pid && item.type == type && item.value == value) {
                addresses.push_back(item.address);
                processed[j] = true;
            }
        }
        uint64_t writtenValue{};
        std::vector<uint64_t> previous;
        std::vector<bool> previousRead;
        std::wstring error;
        MemoryScanner::WriteValues(pid, type, addresses, FormatValue(type, value),
            writtenValue, previous, previousRead, error);
        ++i;
    }
}

LRESULT CALLBACK ToolsWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_toolsWindow = window;
        auto createText = [window](const wchar_t* text, int id) {
            return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 22, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        };
        auto createEdit = [window](int id, DWORD extra = 0) {
            HWND control = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | extra,
                0, 0, 100, 30, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
            return control;
        };
        createText(g_russian ? L"Выбранные результаты" : L"Selected results", 1200);
        g_toolFreezeButton = CreateButton(window, L"Freeze selected (0)", ID_TOOL_FREEZE);
        const std::wstring freezeText = (g_russian ? L"Фиксация (" : L"Frozen (") +
            std::to_wstring(g_frozenValues.size()) + L")";
        SetWindowTextW(g_toolFreezeButton, freezeText.c_str());
        g_toolDebugButton = CreateButton(window, g_debugActive.load()
            ? (g_russian ? L"Отключить отладчик" : L"Detach debugger")
            : (g_russian ? L"Подключить отладчик" : L"Attach debugger"), ID_TOOL_DEBUG);
        createText(g_russian ? L"Адрес для поиска указателей (найдёт прямые ссылки)" : L"Pointer target address (direct references)", ID_TOOL_POINTER_TARGET + 100);
        g_toolPointerAddress = createEdit(ID_TOOL_POINTER_TARGET);
        CreateButton(window, g_russian ? L"Найти указатели" : L"Find pointers", ID_TOOL_POINTER_FIND);
        createText(g_russian ? L"Массив байтов (hex; ?? — любой байт)" : L"Byte array (hex; ?? is wildcard)", ID_TOOL_PATTERN + 100);
        g_toolPattern = createEdit(ID_TOOL_PATTERN);
        CreateButton(window, g_russian ? L"Сканировать" : L"Scan bytes", ID_TOOL_PATTERN_SCAN);
        createText(g_russian ? L"Адрес памяти" : L"Memory address", ID_TOOL_MEM_ADDRESS + 100);
        g_toolMemoryAddress = createEdit(ID_TOOL_MEM_ADDRESS);
        CreateButton(window, g_russian ? L"Читать 128 байт" : L"Read 128 bytes", ID_TOOL_MEM_READ);
        createText(g_russian ? L"Байты для записи (hex)" : L"Bytes to write (hex)", ID_TOOL_MEM_BYTES + 100);
        g_toolMemoryData = createEdit(ID_TOOL_MEM_BYTES);
        CreateButton(window, g_russian ? L"Записать" : L"Write bytes", ID_TOOL_MEM_WRITE);
        createText(g_russian ? L"Операции памяти и отладочные события" : L"Memory operations and debugger events", ID_TOOL_OUTPUT + 100);
        g_toolOutput = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
            ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0, 0, 100, 200, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOL_OUTPUT)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_toolOutput, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
        const int selected = ListView_GetNextItem(g_results, -1, LVNI_SELECTED);
        if (selected >= 0 && static_cast<size_t>(selected) < g_scanResults.size()) {
            const auto address = Hex(g_scanResults[static_cast<size_t>(selected)].address);
            SetWindowTextW(g_toolPointerAddress, address.c_str());
            SetWindowTextW(g_toolMemoryAddress, address.c_str());
        }
        return 0;
    }
    case WM_SIZE: {
        const int width = LOWORD(lParam), height = HIWORD(lParam), margin = Scale(12), row = Scale(34);
        MoveWindow(g_toolFreezeButton, margin, margin, Scale(330), row, TRUE);
        MoveWindow(g_toolDebugButton, margin + Scale(342), margin, Scale(180), row, TRUE);
        MoveWindow(GetDlgItem(window, 1200), margin, Scale(56), width - Scale(24), Scale(20), TRUE);
        MoveWindow(g_toolPointerAddress, margin, Scale(78), width - Scale(224), row, TRUE);
        MoveWindow(GetDlgItem(window, ID_TOOL_POINTER_FIND), width - margin - Scale(196), Scale(78), Scale(196), row, TRUE);
        MoveWindow(GetDlgItem(window, ID_TOOL_PATTERN + 100), margin, Scale(118), width - Scale(24), Scale(20), TRUE);
        MoveWindow(g_toolPattern, margin, Scale(140), width - Scale(224), row, TRUE);
        MoveWindow(GetDlgItem(window, ID_TOOL_PATTERN_SCAN), width - margin - Scale(196), Scale(140), Scale(196), row, TRUE);
        MoveWindow(GetDlgItem(window, ID_TOOL_MEM_ADDRESS + 100), margin, Scale(180), width - Scale(24), Scale(20), TRUE);
        MoveWindow(g_toolMemoryAddress, margin, Scale(202), width - Scale(224), row, TRUE);
        MoveWindow(GetDlgItem(window, ID_TOOL_MEM_READ), width - margin - Scale(196), Scale(202), Scale(196), row, TRUE);
        MoveWindow(GetDlgItem(window, ID_TOOL_MEM_BYTES + 100), margin, Scale(242), width - Scale(24), Scale(20), TRUE);
        MoveWindow(g_toolMemoryData, margin, Scale(264), width - Scale(224), row, TRUE);
        MoveWindow(GetDlgItem(window, ID_TOOL_MEM_WRITE), width - margin - Scale(196), Scale(264), Scale(196), row, TRUE);
        MoveWindow(GetDlgItem(window, ID_TOOL_OUTPUT + 100), margin, Scale(304), width - Scale(24), Scale(20), TRUE);
        MoveWindow(g_toolOutput, margin, Scale(326), width - Scale(24), (std::max)(Scale(120), height - Scale(350)), TRUE);
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, g_windowBrush);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, COLOR_TEXT);
        return reinterpret_cast<LRESULT>(g_windowBrush);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_CONTROL);
        return reinterpret_cast<LRESULT>(g_controlBrush);
    }
    case WM_DRAWITEM:
        return DrawButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == ID_TOOL_FREEZE) { ToggleFreezeSelected(); return 0; }
        if (id == ID_TOOL_DEBUG) { ToggleDebugger(); return 0; }
        if (id == ID_TOOL_POINTER_FIND) {
            uintptr_t target{};
            if (!ParseAddress(GetText(g_toolPointerAddress), target)) {
                AppendToolsOutput(g_russian ? L"Некорректный адрес указателя.\r\n" : L"Invalid pointer target address.\r\n");
                return 0;
            }
            AppendToolsOutput(g_russian ? L"Идёт поиск прямых указателей…\r\n" : L"Searching direct pointers…\r\n");
            StartToolSearch(true, target, {});
            return 0;
        }
        if (id == ID_TOOL_PATTERN_SCAN) {
            std::vector<int> pattern;
            if (!ParseHexBytes(GetText(g_toolPattern), pattern, true)) {
                AppendToolsOutput(g_russian ? L"Введите байты как 48 8B ?? 01.\r\n" : L"Enter bytes like 48 8B ?? 01.\r\n");
                return 0;
            }
            AppendToolsOutput(g_russian ? L"Идёт сканирование массива байтов…\r\n" : L"Scanning byte array…\r\n");
            StartToolSearch(false, 0, std::move(pattern));
            return 0;
        }
        if (id == ID_TOOL_MEM_READ || id == ID_TOOL_MEM_WRITE) {
            uintptr_t address{};
            std::vector<int> parsed;
            if (!SelectedPid() || !ParseAddress(GetText(g_toolMemoryAddress), address)) {
                AppendToolsOutput(g_russian ? L"Выберите процесс и укажите корректный адрес.\r\n" : L"Select a process and enter a valid address.\r\n");
                return 0;
            }
            std::wstring error;
            if (id == ID_TOOL_MEM_READ) {
                std::vector<unsigned char> bytes;
                if (MemoryScanner::ReadMemory(SelectedPid(), address, 128, bytes, error)) {
                    SetWindowTextW(g_toolMemoryData, HexBytes(bytes).c_str());
                    std::wostringstream dump;
                    for (size_t offset = 0; offset < bytes.size(); offset += 16) {
                        dump << Hex(address + offset) << L"  ";
                        for (size_t i = offset; i < (std::min)(bytes.size(), offset + 16); ++i)
                            dump << std::uppercase << std::hex << std::setw(2) << std::setfill(L'0')
                                 << static_cast<unsigned>(bytes[i]) << L' ';
                        dump << L"\r\n";
                    }
                    AppendToolsOutput(dump.str());
                } else AppendToolsOutput(error + L"\r\n");
            } else {
                if (!ParseHexBytes(GetText(g_toolMemoryData), parsed, false)) {
                    AppendToolsOutput(g_russian ? L"Байты должны быть hex-парами, например 10 FF 03.\r\n" : L"Bytes must be hex pairs, such as 10 FF 03.\r\n");
                    return 0;
                }
                std::vector<unsigned char> bytes;
                for (int byte : parsed) bytes.push_back(static_cast<unsigned char>(byte));
                std::vector<unsigned char> oldBytes;
                if (MemoryScanner::WriteMemory(SelectedPid(), address, bytes, oldBytes, error)) {
                    AppendJournal((g_russian ? L"Записаны байты PID " : L"Wrote bytes PID ") +
                        std::to_wstring(SelectedPid()) + L" | " + Hex(address) + L" | " +
                        (oldBytes.empty() ? (g_russian ? L"старое значение не прочитано" : L"old bytes unreadable") : HexBytes(oldBytes)) +
                        L" -> " + HexBytes(bytes));
                    AppendToolsOutput((g_russian ? L"Записано: " : L"Wrote: ") + HexBytes(bytes) + L"\r\n");
                } else AppendToolsOutput(error + L"\r\n");
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (g_toolsWindow == window) {
            g_toolsWindow = nullptr;
            g_toolPointerAddress = g_toolPattern = g_toolMemoryAddress = g_toolMemoryData = nullptr;
            g_toolOutput = g_toolFreezeButton = g_toolDebugButton = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void OpenToolsWindow() {
    if (g_toolsWindow) {
        ShowWindow(g_toolsWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_toolsWindow);
        return;
    }
    RECT owner{}; GetWindowRect(g_main, &owner);
    const int width = Scale(760), height = Scale(690);
    const int x = owner.left + ((owner.right - owner.left) - width) / 2;
    const int y = owner.top + ((owner.bottom - owner.top) - height) / 2;
    g_toolsWindow = CreateWindowExW(WS_EX_TOOLWINDOW, L"CCSToolsWindow",
        g_russian ? L"Инструменты памяти" : L"Memory tools", WS_OVERLAPPEDWINDOW,
        x, y, width, height, g_main, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (g_toolsWindow) {
        ShowWindow(g_toolsWindow, SW_SHOWNORMAL);
        UpdateWindow(g_toolsWindow);
    }
}

void SendDebugUpdate(bool attached, bool failed, const std::wstring& message, bool stateChange) {
    auto* update = new DebugUpdate{ attached, failed, message, stateChange };
    if (!PostMessageW(g_main, WM_DEBUG_UPDATE, 0, reinterpret_cast<LPARAM>(update))) delete update;
}

void ToggleDebugger() {
    if (g_debugActive.load()) {
        g_debugStop.store(true);
        AppendToolsOutput(g_russian ? L"Отключение отладчика…\r\n" : L"Detaching debugger…\r\n");
        return;
    }
    if (g_debugWorker.joinable()) g_debugWorker.join();
    const DWORD pid = SelectedPid();
    if (!pid) {
        AppendToolsOutput(g_russian ? L"Сначала выберите процесс.\r\n" : L"Select a process first.\r\n");
        return;
    }
    g_debugPid = pid;
    g_debugStop.store(false);
    g_debugActive.store(true); // marks attach pending so a second click can request stop
    if (g_toolDebugButton)
        SetWindowTextW(g_toolDebugButton, g_russian ? L"Подключение…" : L"Attaching…");
    const bool russian = g_russian;
    g_debugWorker = std::thread([pid, russian]() {
        if (!DebugActiveProcess(pid)) {
            g_debugActive.store(false);
            SendDebugUpdate(false, true, (russian ? L"Не удалось подключить отладчик, код ошибки " :
                L"Debugger attach failed, error ") + std::to_wstring(GetLastError()), true);
            return;
        }
        DebugSetProcessKillOnExit(FALSE);
        g_debugActive.store(true);
        SendDebugUpdate(true, false, (russian ? L"Отладчик подключён к PID " : L"Debugger attached to PID ") +
            std::to_wstring(pid), true);
        bool processExited = false;
        bool initialBreakpointPending = true;
        while (!g_debugStop.load()) {
            DEBUG_EVENT event{};
            if (!WaitForDebugEvent(&event, 250)) {
                if (GetLastError() == ERROR_SEM_TIMEOUT) continue;
                break;
            }
            DWORD continueStatus = DBG_CONTINUE;
            std::wstring description;
            switch (event.dwDebugEventCode) {
            case CREATE_PROCESS_DEBUG_EVENT:
                description = L"CREATE_PROCESS_DEBUG_EVENT";
                break;
            case CREATE_THREAD_DEBUG_EVENT:
                description = L"CREATE_THREAD_DEBUG_EVENT";
                break;
            case EXIT_THREAD_DEBUG_EVENT:
                description = L"EXIT_THREAD_DEBUG_EVENT";
                break;
            case LOAD_DLL_DEBUG_EVENT:
                description = L"LOAD_DLL_DEBUG_EVENT";
                break;
            case UNLOAD_DLL_DEBUG_EVENT:
                description = L"UNLOAD_DLL_DEBUG_EVENT";
                break;
            case OUTPUT_DEBUG_STRING_EVENT:
                description = L"OUTPUT_DEBUG_STRING_EVENT";
                break;
            case EXCEPTION_DEBUG_EVENT: {
                const DWORD code = event.u.Exception.ExceptionRecord.ExceptionCode;
                const uintptr_t address = reinterpret_cast<uintptr_t>(event.u.Exception.ExceptionRecord.ExceptionAddress);
                description = L"Exception 0x";
                std::wostringstream codeText;
                codeText << std::uppercase << std::hex << code;
                description += codeText.str() + L" at " + Hex(address);
                if (code == EXCEPTION_BREAKPOINT && initialBreakpointPending) {
                    continueStatus = DBG_CONTINUE;
                    initialBreakpointPending = false;
                } else continueStatus = DBG_EXCEPTION_NOT_HANDLED;
                break;
            }
            case EXIT_PROCESS_DEBUG_EVENT:
                description = L"EXIT_PROCESS_DEBUG_EVENT";
                processExited = true;
                break;
            default:
                description = L"Debug event " + std::to_wstring(event.dwDebugEventCode);
                break;
            }
            if (!description.empty()) SendDebugUpdate(true, false, description, false);
            ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continueStatus);
            if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
                if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
                if (event.u.CreateProcessInfo.hThread) CloseHandle(event.u.CreateProcessInfo.hThread);
                if (event.u.CreateProcessInfo.hProcess) CloseHandle(event.u.CreateProcessInfo.hProcess);
            } else if (event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT) {
                if (event.u.CreateThread.hThread) CloseHandle(event.u.CreateThread.hThread);
            } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
                if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
            }
            if (processExited) break;
        }
        if (!processExited) DebugActiveProcessStop(pid);
        g_debugActive.store(false);
        SendDebugUpdate(false, false, russian ? L"Отладчик отключён." : L"Debugger detached.", true);
    });
}

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_main = window;
        g_windowBrush = CreateSolidBrush(COLOR_WINDOW_BG);
        g_panelBrush = CreateSolidBrush(COLOR_PANEL);
        g_controlBrush = CreateSolidBrush(COLOR_CONTROL);
        CreateControls(window);
        ApplyTheme(g_themeIndex);
        RECT client{}; GetClientRect(window, &client);
        Layout(client.right, client.bottom);
        AppendJournal(L"CCS engine started");
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wParam);
        RebuildFonts();
        ListView_SetColumnWidth(g_results, 0, Scale(240));
        ListView_SetColumnWidth(g_results, 1, Scale(220));
        ListView_SetColumnWidth(g_results, 2, Scale(90));
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, g_windowBrush);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        SetBkMode(dc, TRANSPARENT);
        const bool inInfoCard = control == g_infoTitle || control == g_infoDescription ||
            control == g_infoTypesLabel || control == g_infoTypes || control == g_infoAuthorLabel;
        SetBkMode(dc, inInfoCard ? OPAQUE : TRANSPARENT);
        if (inInfoCard) SetBkColor(dc, COLOR_PANEL);
        SetTextColor(dc, control == g_title || control == g_infoTitle || control == g_infoTypesLabel ||
            control == g_infoAuthorLabel ? COLOR_TEXT : (control == g_author ? COLOR_ACCENT : COLOR_MUTED));
        return reinterpret_cast<LRESULT>(inInfoCard ? g_panelBrush : g_windowBrush);
    }
    case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_CONTROL);
        return reinterpret_cast<LRESULT>(g_controlBrush);
    }
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_WINDOW_BG);
        return reinterpret_cast<LRESULT>(g_windowBrush);
    }
    case WM_DRAWITEM:
        return DrawButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->hwndFrom == ListView_GetHeader(g_results) && header->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(draw->hdc, COLOR_MUTED);
                SetBkColor(draw->hdc, COLOR_PANEL);
                return CDRF_DODEFAULT;
            }
        }
        if (header->hwndFrom == g_results && header->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                draw->clrText = COLOR_TEXT;
                draw->clrTextBk = (draw->nmcd.dwItemSpec % 2) ? COLOR_PANEL : COLOR_ROW_ALT;
                return CDRF_DODEFAULT;
            }
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_REFRESH: RefreshProcesses(); return 0;
        case ID_NEW:
            AppendJournal(g_russian ? L"Создан новый сеанс поиска." : L"New scan session started.");
            ResetSession();
            return 0;
        case ID_FIRST: BeginScan(false); return 0;
        case ID_NEXT: BeginScan(true); return 0;
        case ID_WRITE: WriteSelectedValues(); return 0;
        case ID_SETTINGS: ToggleThemePicker(); return 0;
        case ID_JOURNAL: OpenJournalWindow(); return 0;
        case ID_TOOLS: OpenToolsWindow(); return 0;
        case ID_AUTHOR:
            if (HIWORD(wParam) == STN_CLICKED) {
                ShellExecuteW(window, L"open", L"https://t.me/cireincroinsea", nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
            break;
        case ID_TYPE:
            if (HIWORD(wParam) == CBN_SELCHANGE) ResetSession();
            return 0;
        case ID_PROCESS + 100:
            if (HIWORD(wParam) == CBN_SELCHANGE) ResetSession();
            return 0;
        default:
            if (LOWORD(wParam) >= ID_THEME_FIRST && LOWORD(wParam) <= ID_THEME_LAST) {
                ApplyTheme(LOWORD(wParam) - ID_THEME_FIRST);
                if (g_themePickerVisible) ToggleThemePicker();
                return 0;
            }
        }
        break;
    case WM_SCAN_COMPLETE: {
        std::unique_ptr<ScanPayload> payload(reinterpret_cast<ScanPayload*>(lParam));
        if (g_worker.joinable()) g_worker.join();
        g_scanning = false;
        g_scanResults = std::move(payload->results);
        if (payload->firstScan) {
            g_sessionPid = SelectedPid();
            g_sessionType = payload->type;
        }
        ListView_DeleteAllItems(g_results);
        for (size_t i = 0; i < g_scanResults.size(); ++i)
            AddResult(g_scanResults[i], static_cast<int>(i), payload->type);
        SetSessionControls();
        std::wstring status = L"Found " + std::to_wstring(g_scanResults.size()) + L" result(s).";
        if (g_scanResults.size() == MAX_RESULTS) status += L" Result limit reached.";
        if (!payload->error.empty()) status += L"  " + payload->error;
        SetStatus(status);
        AppendJournal((g_russian ? L"Поиск завершён: найдено " : L"Scan complete: found ") +
            std::to_wstring(g_scanResults.size()) + (g_russian ? L" результатов." : L" result(s)."));
        if (!payload->error.empty())
            AppendJournal((g_russian ? L"Ошибка сканирования: " : L"Scan error: ") + payload->error);
        return 0;
    }
    case WM_TOOL_COMPLETE: {
        std::unique_ptr<ToolSearchPayload> payload(reinterpret_cast<ToolSearchPayload*>(lParam));
        g_toolBusy.store(false);
        if (g_toolWorker.joinable()) g_toolWorker.join();
        const std::wstring label = payload->pointers
            ? (g_russian ? L"Прямые указатели на " : L"Direct pointers to ") + Hex(payload->target)
            : (g_russian ? L"Совпадения массива байтов" : L"Byte array matches");
        AppendJournal(label + L": " + std::to_wstring(payload->count));
        if (g_toolsWindow) {
            AppendToolsOutput(label + L": " + std::to_wstring(payload->count) + L"\r\n");
            for (size_t i = 0; i < payload->addresses.size() && i < 200; ++i)
                AppendToolsOutput(Hex(payload->addresses[i]) + L"\r\n");
            if (payload->addresses.size() > 200)
                AppendToolsOutput(g_russian ? L"Показаны первые 200 адресов.\r\n" : L"Showing first 200 addresses.\r\n");
            if (!payload->error.empty()) AppendToolsOutput(payload->error + L"\r\n");
        }
        if (!payload->error.empty()) AppendJournal(payload->error);
        SetStatus(label + L": " + std::to_wstring(payload->count));
        return 0;
    }
    case WM_DEBUG_UPDATE: {
        std::unique_ptr<DebugUpdate> update(reinterpret_cast<DebugUpdate*>(lParam));
        AppendJournal(update->message);
        if (g_toolsWindow) {
            AppendToolsOutput(update->message + L"\r\n");
            if (update->stateChange && g_toolDebugButton)
                SetWindowTextW(g_toolDebugButton, update->attached
                    ? (g_russian ? L"Отключить отладчик" : L"Detach debugger")
                    : (g_russian ? L"Подключить отладчик" : L"Attach debugger"));
        }
        if (update->failed) SetStatus(update->message);
        return 0;
    }
    case WM_TIMER:
        if (wParam == TIMER_FREEZE) {
            MaintainFrozenValues();
            return 0;
        }
        break;
    case WM_SIZE:
        Layout(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = Scale(960);
        limits->ptMinTrackSize.y = Scale(600);
        return 0;
    }
    case WM_DESTROY: {
        g_cancel.store(true, std::memory_order_relaxed);
        g_toolCancel.store(true);
        g_debugStop.store(true);
        if (g_worker.joinable()) g_worker.join();
        if (g_toolWorker.joinable()) g_toolWorker.join();
        if (g_debugWorker.joinable()) g_debugWorker.join();
        if (g_journalWindow) DestroyWindow(g_journalWindow);
        if (g_toolsWindow) DestroyWindow(g_toolsWindow);
        KillTimer(window, TIMER_FREEZE);
        MSG queued{};
        while (PeekMessageW(&queued, window, WM_SCAN_COMPLETE, WM_SCAN_COMPLETE, PM_REMOVE))
            delete reinterpret_cast<ScanPayload*>(queued.lParam);
        while (PeekMessageW(&queued, window, WM_TOOL_COMPLETE, WM_TOOL_COMPLETE, PM_REMOVE))
            delete reinterpret_cast<ToolSearchPayload*>(queued.lParam);
        while (PeekMessageW(&queued, window, WM_DEBUG_UPDATE, WM_DEBUG_UPDATE, PM_REMOVE))
            delete reinterpret_cast<DebugUpdate*>(queued.lParam);
        if (g_windowBrush) DeleteObject(g_windowBrush);
        if (g_panelBrush) DeleteObject(g_panelBrush);
        if (g_controlBrush) DeleteObject(g_controlBrush);
        if (g_uiFont) DeleteObject(g_uiFont);
        if (g_headingFont) DeleteObject(g_headingFont);
        if (g_titleFont) DeleteObject(g_titleFont);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    g_dpi = GetDpiForSystem();
    if (!g_dpi) g_dpi = 96;
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_LINK_CLASS };
    InitCommonControlsEx(&controls);
    WNDCLASSEXW windowClass{ sizeof(windowClass) };
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"MemoryScoutWindow";
    if (!RegisterClassExW(&windowClass)) return 1;

    WNDCLASSEXW themeClass{ sizeof(themeClass) };
    themeClass.lpfnWndProc = ThemePickerProc;
    themeClass.hInstance = instance;
    themeClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    themeClass.lpszClassName = L"CCSThemePickerWindow";
    RegisterClassExW(&themeClass);

    WNDCLASSEXW journalClass{ sizeof(journalClass) };
    journalClass.lpfnWndProc = JournalWindowProc;
    journalClass.hInstance = instance;
    journalClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    journalClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    journalClass.lpszClassName = L"CCSJournalWindow";
    RegisterClassExW(&journalClass);

    WNDCLASSEXW toolsClass{ sizeof(toolsClass) };
    toolsClass.lpfnWndProc = ToolsWindowProc;
    toolsClass.hInstance = instance;
    toolsClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    toolsClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    toolsClass.lpszClassName = L"CCSToolsWindow";
    RegisterClassExW(&toolsClass);

    HWND window = CreateWindowExW(0, windowClass.lpszClassName,
        L"CCS engine  |  Windows Memory Analyzer", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, Scale(1240), Scale(760), nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

