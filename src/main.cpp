#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kClassName[] = L"StarfruitNotes.NoteWindow";
constexpr wchar_t kAppName[] = L"Starfruit Notes";
constexpr int kIconResource = 101;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kHotkeyId = 1;
constexpr UINT_PTR kTimerId = 1;

enum CommandId : UINT {
    CmdShow = 100,
    CmdDesktop,
    CmdTopmost,
    CmdStartup,
    CmdThemePaper,
    CmdThemePlain,
    CmdThemeInk,
    CmdHistory,
    CmdReset,
    CmdExit,
};

struct Task {
    std::wstring text;
    bool done = false;

    bool operator==(const Task&) const = default;
};

struct Theme {
    COLORREF bg;
    COLORREF line;
    COLORREF text;
    COLORREF sub;
    COLORREF placeholder;
    COLORREF accent;
    COLORREF soft;
    COLORREF track;
    COLORREF field;
    COLORREF hover;
    COLORREF checkbox;
    COLORREF done;
};

enum class WindowMode { Desktop = 0, Topmost = 1 };
enum class ThemeId { Paper = 0, Plain = 1, Ink = 2 };

HWND g_window = nullptr;
HWND g_edit = nullptr;
HFONT g_font = nullptr;
HFONT g_smallFont = nullptr;
HFONT g_dateFont = nullptr;
HFONT g_doneFont = nullptr;
HBRUSH g_editBrush = nullptr;
HICON g_appIcon = nullptr;
NOTIFYICONDATAW g_tray{};
UINT g_taskbarCreated = 0;
UINT g_dpi = 96;

fs::path g_dataDir;
fs::path g_daysDir;
fs::path g_settingsPath;
std::vector<Task> g_tasks;
std::vector<Task> g_pending;
std::wstring g_currentDate;
std::wstring g_pendingDate;
std::time_t g_snoozeUntil = 0;

WindowMode g_mode = WindowMode::Desktop;
ThemeId g_themeId = ThemeId::Paper;
bool g_visible = true;
bool g_bannerVisible = false;
bool g_quitting = false;
int g_hoverIndex = -1;
int g_dragIndex = -1;
int g_dropIndex = -1;
int g_editingIndex = -1;
int g_scroll = 0;
bool g_dragging = false;
POINT g_dragStart{};

RECT g_inputRect{};
RECT g_listRect{};
RECT g_historyRect{};
RECT g_pinRect{};
RECT g_menuRect{};
RECT g_minimizeRect{};
RECT g_carryRect{};
RECT g_discardRect{};
RECT g_snoozeRect{};

int Scale(int px) {
    return MulDiv(px, static_cast<int>(g_dpi), 96);
}

Theme CurrentTheme() {
    switch (g_themeId) {
    case ThemeId::Plain:
        return {RGB(255,255,255), RGB(233,234,238), RGB(27,28,31), RGB(107,111,120),
                RGB(107,111,120), RGB(59,110,245), RGB(235,240,255), RGB(236,238,242),
                RGB(246,247,250), RGB(243,246,252), RGB(201,203,210), RGB(107,111,120)};
    case ThemeId::Ink:
        return {RGB(30,31,35), RGB(45,47,53), RGB(233,231,227), RGB(138,141,149),
                RGB(138,141,149), RGB(227,176,114), RGB(50,43,34), RGB(51,53,60),
                RGB(38,40,45), RGB(40,42,49), RGB(74,77,85), RGB(138,141,149)};
    case ThemeId::Paper:
    default:
        return {RGB(251,247,238), RGB(237,228,210), RGB(51,48,42), RGB(117,109,96),
                RGB(120,111,98), RGB(190,123,74), RGB(244,229,214), RGB(232,221,201),
                RGB(245,238,224), RGB(244,236,220), RGB(207,195,173), RGB(117,109,96)};
    }
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring FromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring Trim(std::wstring value) {
    const auto isSpace = [](wchar_t ch) { return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n'; };
    while (!value.empty() && isSpace(value.front())) value.erase(value.begin());
    while (!value.empty() && isSpace(value.back())) value.pop_back();
    for (auto& ch : value) {
        if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
    }
    return value;
}

std::string SerializeTasks(const std::vector<Task>& tasks) {
    std::string output;
    for (const auto& task : tasks) {
        output += task.done ? "1\t" : "0\t";
        output += ToUtf8(Trim(task.text));
        output += '\n';
    }
    return output;
}

std::vector<Task> ParseTasks(const std::string& text) {
    std::vector<Task> tasks;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 3 || line[1] != '\t') continue;
        auto value = Trim(FromUtf8(line.substr(2)));
        if (!value.empty()) tasks.push_back({std::move(value), line[0] == '1'});
    }
    return tasks;
}

bool WriteUtf8Atomic(const fs::path& path, const std::string& content) {
    try {
        fs::create_directories(path.parent_path());
        const fs::path temp = path.wstring() + L".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file) return false;
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!file) return false;
        }
        if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        std::error_code error;
        fs::remove(temp, error);
        return false;
    } catch (...) {
        return false;
    }
}

std::string ReadUtf8(const fs::path& path) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) return {};
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    } catch (...) {
        return {};
    }
}

std::wstring DateKeyNow() {
    SYSTEMTIME value{};
    GetLocalTime(&value);
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"%04u-%02u-%02u", value.wYear, value.wMonth, value.wDay);
    return buffer;
}

std::wstring DateKeyDaysAgo(int days) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    FILETIME ft{};
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    value.QuadPart -= static_cast<ULONGLONG>(days) * 24ULL * 60ULL * 60ULL * 10000000ULL;
    ft.dwLowDateTime = value.LowPart;
    ft.dwHighDateTime = value.HighPart;
    FileTimeToSystemTime(&ft, &st);
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return buffer;
}

std::wstring FriendlyDate() {
    SYSTEMTIME value{};
    GetLocalTime(&value);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%u月%u日", value.wMonth, value.wDay);
    return buffer;
}

std::wstring FriendlyWeekday() {
    static constexpr const wchar_t* names[] = {L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
    SYSTEMTIME value{};
    GetLocalTime(&value);
    return names[value.wDayOfWeek % 7];
}

fs::path DayPath(const std::wstring& date) {
    return g_daysDir / (date + L".txt");
}

std::wstring ReadSetting(const wchar_t* key, const wchar_t* fallback = L"") {
    wchar_t buffer[512]{};
    constexpr wchar_t sentinel[] = L"{missing}";
    GetPrivateProfileStringW(L"Starfruit Notes", key, sentinel, buffer,
                             static_cast<DWORD>(std::size(buffer)), g_settingsPath.c_str());
    if (wcscmp(buffer, sentinel) == 0) {
        GetPrivateProfileStringW(L"DayNote", key, fallback, buffer,
                                 static_cast<DWORD>(std::size(buffer)), g_settingsPath.c_str());
    }
    return buffer;
}

int ReadSettingInt(const wchar_t* key, int fallback) {
    const auto value = ReadSetting(key, std::to_wstring(fallback).c_str());
    return _wtoi(value.c_str());
}

void WriteSetting(const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(L"Starfruit Notes", key, value.c_str(), g_settingsPath.c_str());
}

void WriteSettingInt(const wchar_t* key, long long value) {
    WriteSetting(key, std::to_wstring(value));
}

void SaveDay() {
    if (!g_currentDate.empty()) WriteUtf8Atomic(DayPath(g_currentDate), SerializeTasks(g_tasks));
}

void SavePending() {
    WriteUtf8Atomic(g_dataDir / L"pending.txt", SerializeTasks(g_pending));
    WriteSetting(L"PendingDate", g_pending.empty() ? L"" : g_pendingDate);
    WriteSettingInt(L"SnoozeUntil", static_cast<long long>(g_snoozeUntil));
}

void SaveSettings() {
    WriteSetting(L"CurrentDate", g_currentDate);
    WriteSettingInt(L"Mode", static_cast<int>(g_mode));
    WriteSettingInt(L"Theme", static_cast<int>(g_themeId));
    WriteSettingInt(L"Visible", g_visible ? 1 : 0);
    SavePending();
}

void CleanupHistory() {
    const std::wstring cutoff = DateKeyDaysAgo(30);
    try {
        for (const auto& entry : fs::directory_iterator(g_daysDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != L".txt") continue;
            if (entry.path().stem().wstring() < cutoff) {
                std::error_code error;
                fs::remove(entry.path(), error);
            }
        }
    } catch (...) {
    }
}

void InitializeStorage() {
    wchar_t appData[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData);
    const fs::path legacyDataDir = fs::path(appData) / L"DayNote";
    g_dataDir = fs::path(appData) / L"Starfruit Notes";
    if (!fs::exists(g_dataDir) && fs::exists(legacyDataDir)) {
        std::error_code migrationError;
        fs::copy(legacyDataDir, g_dataDir,
                 fs::copy_options::recursive | fs::copy_options::skip_existing, migrationError);
    }
    g_daysDir = g_dataDir / L"days";
    g_settingsPath = g_dataDir / L"settings.ini";
    fs::create_directories(g_daysDir);

    g_mode = ReadSettingInt(L"Mode", 0) == 1 ? WindowMode::Topmost : WindowMode::Desktop;
    const int theme = std::clamp(ReadSettingInt(L"Theme", 0), 0, 2);
    g_themeId = static_cast<ThemeId>(theme);
    g_visible = ReadSettingInt(L"Visible", 1) != 0;
    g_snoozeUntil = static_cast<std::time_t>(_wtoi64(ReadSetting(L"SnoozeUntil", L"0").c_str()));
    g_pendingDate = ReadSetting(L"PendingDate", L"");
    g_pending = ParseTasks(ReadUtf8(g_dataDir / L"pending.txt"));

    const std::wstring today = DateKeyNow();
    const std::wstring lastDate = ReadSetting(L"CurrentDate", L"");
    if (lastDate.empty() || lastDate == today) {
        g_currentDate = today;
        g_tasks = ParseTasks(ReadUtf8(DayPath(today)));
    } else {
        auto oldTasks = ParseTasks(ReadUtf8(DayPath(lastDate)));
        for (const auto& task : oldTasks) {
            if (!task.done) g_pending.push_back({task.text, false});
        }
        if (!oldTasks.empty() && g_pendingDate.empty()) g_pendingDate = lastDate;
        g_currentDate = today;
        g_tasks = ParseTasks(ReadUtf8(DayPath(today)));
    }

    g_bannerVisible = !g_pending.empty() && std::time(nullptr) >= g_snoozeUntil;
    SaveDay();
    SaveSettings();
    CleanupHistory();
}

void RollToTodayIfNeeded() {
    const std::wstring today = DateKeyNow();
    if (today == g_currentDate) return;

    SaveDay();
    for (const auto& task : g_tasks) {
        if (!task.done) g_pending.push_back({task.text, false});
    }
    if (!g_tasks.empty() && g_pendingDate.empty()) g_pendingDate = g_currentDate;
    g_currentDate = today;
    g_tasks = ParseTasks(ReadUtf8(DayPath(today)));
    g_scroll = 0;
    g_snoozeUntil = 0;
    g_bannerVisible = !g_pending.empty();
    SaveDay();
    SaveSettings();
    CleanupHistory();
}

int RunSelfTest() {
    const std::vector<Task> expected{{L"完成论文引言", true}, {L"查看 FAST-LIVO2", false}, {L"整理 数据", false}};
    const auto encoded = SerializeTasks(expected);
    if (ParseTasks(encoded) != expected) return 1;
    if (FromUtf8(ToUtf8(L"中文输入 ✓")) != L"中文输入 ✓") return 2;
    if (Trim(L" \t整理\r\n数据 ") != L"整理  数据") return 3;
    return 0;
}

} // namespace

namespace {

void CreateUiResources();
void LayoutControls();
bool PointIn(const RECT& rect, int x, int y);
int TaskIndexAt(int x, int y);
void ClampScroll();
void PaintWindow(HDC targetDc);
void Refresh();

void CommitInput() {
    const int length = GetWindowTextLengthW(g_edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) GetWindowTextW(g_edit, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    text = Trim(std::move(text));
    if (text.empty()) return;

    if (g_editingIndex >= 0 && g_editingIndex < static_cast<int>(g_tasks.size())) {
        g_tasks[g_editingIndex].text = std::move(text);
    } else {
        g_tasks.push_back({std::move(text), false});
    }
    g_editingIndex = -1;
    SetWindowTextW(g_edit, L"");
    SaveDay();
    Refresh();
}

void CancelInput() {
    g_editingIndex = -1;
    SetWindowTextW(g_edit, L"");
    SetFocus(g_window);
    Refresh();
}

void BeginTaskEdit(int index) {
    if (index < 0 || index >= static_cast<int>(g_tasks.size())) return;
    g_editingIndex = index;
    SetWindowTextW(g_edit, g_tasks[index].text.c_str());
    SetFocus(g_edit);
    SendMessageW(g_edit, EM_SETSEL, 0, -1);
    Refresh();
}

void DeleteTask(int index) {
    if (index < 0 || index >= static_cast<int>(g_tasks.size())) return;
    g_tasks.erase(g_tasks.begin() + index);
    if (g_editingIndex == index) CancelInput();
    else if (g_editingIndex > index) --g_editingIndex;
    g_hoverIndex = -1;
    SaveDay();
    Refresh();
}

void ToggleTask(int index) {
    if (index < 0 || index >= static_cast<int>(g_tasks.size())) return;
    g_tasks[index].done = !g_tasks[index].done;
    SaveDay();
    Refresh();
}

void CarryPending() {
    for (const auto& task : g_pending) g_tasks.push_back({task.text, false});
    g_pending.clear();
    g_pendingDate.clear();
    g_snoozeUntil = 0;
    g_bannerVisible = false;
    SaveDay();
    SaveSettings();
    Refresh();
}

void DiscardPending() {
    g_pending.clear();
    g_pendingDate.clear();
    g_snoozeUntil = 0;
    g_bannerVisible = false;
    SaveSettings();
    Refresh();
}

void SnoozePending() {
    g_snoozeUntil = std::time(nullptr) + 30 * 60;
    g_bannerVisible = false;
    SaveSettings();
    Refresh();
}

bool IsStartupEnabled() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    const LONG result = RegQueryValueExW(key, kAppName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

void ToggleStartup() {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    if (IsStartupEnabled()) {
        RegDeleteValueW(key, kAppName);
    } else {
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        const std::wstring command = L"\"" + std::wstring(executable) + L"\"";
        RegSetValueExW(key, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
}

void MigrateLegacyStartup() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    const bool newEntryExists = RegQueryValueExW(key, kAppName, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    const bool oldEntryExists = RegQueryValueExW(key, L"DayNote", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    if (!newEntryExists && oldEntryExists) {
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        const std::wstring command = L"\"" + std::wstring(executable) + L"\"";
        if (RegSetValueExW(key, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                          static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS) {
            RegDeleteValueW(key, L"DayNote");
        }
    }
    RegCloseKey(key);
}

void ApplyWindowMode(bool activate = false) {
    if (!g_window) return;
    if (g_mode == WindowMode::Topmost) {
        SetWindowPos(g_window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | (activate ? 0 : SWP_NOACTIVATE));
    } else {
        SetWindowPos(g_window, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | (activate ? 0 : SWP_NOACTIVATE));
    }
    Refresh();
}

void ShowNote(bool show, bool activate = true) {
    g_visible = show;
    if (show) {
        ShowWindow(g_window, SW_SHOWNORMAL);
        ApplyWindowMode(activate);
        if (activate) {
            SetForegroundWindow(g_window);
            SetFocus(g_edit);
        }
    } else {
        ShowWindow(g_window, SW_HIDE);
    }
    SaveSettings();
}

void SaveWindowPlacement() {
    if (!g_window || IsIconic(g_window)) return;
    RECT rect{};
    GetWindowRect(g_window, &rect);
    WriteSettingInt(L"WindowX", rect.left);
    WriteSettingInt(L"WindowY", rect.top);
    WriteSettingInt(L"WindowW", rect.right - rect.left);
    WriteSettingInt(L"WindowH", rect.bottom - rect.top);
}

void ResetWindowPlacement() {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = Scale(300);
    const int height = Scale(400);
    SetWindowPos(g_window, nullptr, work.right - width - Scale(24), work.bottom - height - Scale(24),
                 width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    SaveWindowPlacement();
}

void ShowHistory() {
    std::vector<fs::path> files;
    try {
        for (const auto& entry : fs::directory_iterator(g_daysDir)) {
            if (entry.is_regular_file() && entry.path().extension() == L".txt") files.push_back(entry.path());
        }
    } catch (...) {
    }
    std::sort(files.begin(), files.end(), std::greater<>());

    std::wstring summary;
    int shown = 0;
    for (const auto& file : files) {
        if (shown++ >= 30) break;
        const auto tasks = ParseTasks(ReadUtf8(file));
        const int completed = static_cast<int>(std::count_if(tasks.begin(), tasks.end(), [](const Task& task) {
            return task.done;
        }));
        summary += file.stem().wstring() + L"    " + std::to_wstring(completed) + L" / " +
                   std::to_wstring(tasks.size()) + L" 已完成\r\n";
    }
    if (summary.empty()) summary = L"还没有历史记录。";
    MessageBoxW(g_window, summary.c_str(), L"最近 30 天", MB_OK | MB_ICONINFORMATION);
}

void AddTrayIcon() {
    g_tray = {};
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_window;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = kTrayMessage;
    g_tray.hIcon = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_tray.szTip, L"Starfruit Notes · 桌面待办便签");
    Shell_NotifyIconW(NIM_ADD, &g_tray);
    g_tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
}

void ShowMainMenu(POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    HMENU themes = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (IsWindowVisible(g_window) ? MF_CHECKED : 0), CmdShow, L"显示便签\tCtrl+Alt+N");
    AppendMenuW(menu, MF_STRING | (g_mode == WindowMode::Desktop ? MF_CHECKED : 0), CmdDesktop, L"贴在桌面");
    AppendMenuW(menu, MF_STRING | (g_mode == WindowMode::Topmost ? MF_CHECKED : 0), CmdTopmost, L"始终置顶");
    AppendMenuW(menu, MF_STRING | (IsStartupEnabled() ? MF_CHECKED : 0), CmdStartup, L"开机自动启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(themes, MF_STRING | (g_themeId == ThemeId::Paper ? MF_CHECKED : 0), CmdThemePaper, L"米纸");
    AppendMenuW(themes, MF_STRING | (g_themeId == ThemeId::Plain ? MF_CHECKED : 0), CmdThemePlain, L"素白");
    AppendMenuW(themes, MF_STRING | (g_themeId == ThemeId::Ink ? MF_CHECKED : 0), CmdThemeInk, L"夜墨");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(themes), L"主题");
    AppendMenuW(menu, MF_STRING, CmdHistory, L"历史记录 · 30 天");
    AppendMenuW(menu, MF_STRING, CmdReset, L"位置与尺寸复位");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CmdExit, L"退出");

    SetForegroundWindow(g_window);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        screenPoint.x, screenPoint.y, 0, g_window, nullptr);
    DestroyMenu(menu);
    if (command != 0) SendMessageW(g_window, WM_COMMAND, command, 0);
}

void HandleCommand(UINT command) {
    switch (command) {
    case CmdShow:
        ShowNote(!IsWindowVisible(g_window));
        break;
    case CmdDesktop:
        g_mode = WindowMode::Desktop;
        ApplyWindowMode();
        SaveSettings();
        break;
    case CmdTopmost:
        g_mode = WindowMode::Topmost;
        ApplyWindowMode();
        SaveSettings();
        break;
    case CmdStartup:
        ToggleStartup();
        break;
    case CmdThemePaper:
    case CmdThemePlain:
    case CmdThemeInk:
        g_themeId = command == CmdThemePaper ? ThemeId::Paper :
                    command == CmdThemePlain ? ThemeId::Plain : ThemeId::Ink;
        CreateUiResources();
        SaveSettings();
        Refresh();
        break;
    case CmdHistory:
        ShowHistory();
        break;
    case CmdReset:
        ResetWindowPlacement();
        break;
    case CmdExit:
        g_quitting = true;
        DestroyWindow(g_window);
        break;
    default:
        break;
    }
}

void ShowTaskMenu(int index, POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"编辑");
    AppendMenuW(menu, MF_STRING, 2, L"删除");
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        screenPoint.x, screenPoint.y, 0, g_window, nullptr);
    DestroyMenu(menu);
    if (command == 1) BeginTaskEdit(index);
    if (command == 2) DeleteTask(index);
}

LRESULT CALLBACK EditProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                          UINT_PTR subclassId, DWORD_PTR referenceData) {
    static_cast<void>(subclassId);
    static_cast<void>(referenceData);
    if (message == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            CommitInput();
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            CancelInput();
            return 0;
        }
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        InvalidateRect(g_window, &g_inputRect, FALSE);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace

namespace {

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_taskbarCreated && g_taskbarCreated != 0) {
        AddTrayIcon();
        return 0;
    }

    switch (message) {
    case WM_NCCREATE:
        g_window = window;
        return TRUE;

    case WM_CREATE: {
        g_dpi = GetDpiForWindow(window);
        CreateUiResources();
        g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 0, 0, 0, 0, window, reinterpret_cast<HMENU>(10),
                                 GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageW(g_edit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"添加任务，回车确认"));
        SetWindowSubclass(g_edit, EditProc, 1, 0);
        LayoutControls();

        const DWORD cornerAttribute = 33;
        const int rounded = 2;
        DwmSetWindowAttribute(window, cornerAttribute, &rounded, sizeof(rounded));
        AddTrayIcon();
        RegisterHotKey(window, kHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'N');
        SetTimer(window, kTimerId, 30'000, nullptr);
        return 0;
    }

    case WM_NCCALCSIZE:
        return 0;

    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &point);
        RECT client{};
        GetClientRect(window, &client);
        const int edge = Scale(6);
        const bool left = point.x < edge;
        const bool right = point.x >= client.right - edge;
        const bool top = point.y < edge;
        const bool bottom = point.y >= client.bottom - edge;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;

        const int banner = g_bannerVisible ? Scale(90) : 0;
        if (point.y >= banner && point.y < banner + Scale(40) &&
            !PointIn(g_pinRect, point.x, point.y) && !PointIn(g_menuRect, point.x, point.y) &&
            !PointIn(g_minimizeRect, point.x, point.y)) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {Scale(240), Scale(260)};
        info->ptMaxTrackSize = {Scale(480), Scale(700)};
        return 0;
    }

    case WM_DPICHANGED: {
        g_dpi = HIWORD(wParam);
        CreateUiResources();
        const auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Refresh();
        return 0;
    }

    case WM_SIZE:
        LayoutControls();
        InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_EXITSIZEMOVE:
        SaveWindowPlacement();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        PaintWindow(dc);
        EndPaint(window, &paint);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLOREDIT: {
        const Theme theme = CurrentTheme();
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme.text);
        SetBkColor(dc, theme.field);
        return reinterpret_cast<LRESULT>(g_editBrush);
    }

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        SetFocus(window);

        if (g_bannerVisible) {
            if (PointIn(g_carryRect, x, y)) CarryPending();
            else if (PointIn(g_discardRect, x, y)) DiscardPending();
            else if (PointIn(g_snoozeRect, x, y)) SnoozePending();
            if (y < Scale(90)) return 0;
        }
        if (PointIn(g_pinRect, x, y)) {
            g_mode = g_mode == WindowMode::Topmost ? WindowMode::Desktop : WindowMode::Topmost;
            ApplyWindowMode();
            SaveSettings();
            return 0;
        }
        if (PointIn(g_menuRect, x, y)) {
            POINT point{x, g_menuRect.bottom};
            ClientToScreen(window, &point);
            ShowMainMenu(point);
            return 0;
        }
        if (PointIn(g_minimizeRect, x, y)) {
            ShowNote(false);
            return 0;
        }
        if (PointIn(g_historyRect, x, y)) {
            ShowHistory();
            return 0;
        }

        const int index = TaskIndexAt(x, y);
        if (index >= 0) {
            if (x >= g_listRect.right - Scale(30)) {
                DeleteTask(index);
            } else if (x <= g_listRect.left + Scale(31)) {
                ToggleTask(index);
            } else {
                g_dragIndex = index;
                g_dropIndex = index;
                g_dragStart = {x, y};
                g_dragging = false;
                SetCapture(window);
            }
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        const int index = TaskIndexAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (index >= 0) BeginTaskEdit(index);
        return 0;
    }

    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);

        const int hover = TaskIndexAt(x, y);
        if (hover != g_hoverIndex) {
            g_hoverIndex = hover;
            InvalidateRect(window, &g_listRect, FALSE);
        }
        if (g_dragIndex >= 0 && (wParam & MK_LBUTTON) != 0) {
            if (!g_dragging &&
                (std::abs(x - g_dragStart.x) >= Scale(4) || std::abs(y - g_dragStart.y) >= Scale(4))) {
                g_dragging = true;
            }
            if (g_dragging) {
                int target = TaskIndexAt(x, y);
                if (target < 0 && y < g_listRect.top) target = g_scroll;
                if (target < 0 && y >= g_listRect.bottom && !g_tasks.empty()) {
                    target = static_cast<int>(g_tasks.size()) - 1;
                }
                if (target >= 0 && target != g_dropIndex) {
                    g_dropIndex = target;
                    InvalidateRect(window, &g_listRect, FALSE);
                }
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_hoverIndex = -1;
        InvalidateRect(window, &g_listRect, FALSE);
        return 0;

    case WM_LBUTTONUP:
        if (g_dragIndex >= 0) {
            if (g_dragging && g_dropIndex >= 0 && g_dropIndex != g_dragIndex) {
                Task moved = std::move(g_tasks[g_dragIndex]);
                const int original = g_dragIndex;
                g_tasks.erase(g_tasks.begin() + original);
                int destination = g_dropIndex;
                if (destination > original) --destination;
                destination = std::clamp(destination, 0, static_cast<int>(g_tasks.size()));
                g_tasks.insert(g_tasks.begin() + destination, std::move(moved));
                SaveDay();
            }
            g_dragIndex = -1;
            g_dropIndex = -1;
            g_dragging = false;
            ReleaseCapture();
            Refresh();
        }
        return 0;

    case WM_CAPTURECHANGED:
        g_dragIndex = -1;
        g_dropIndex = -1;
        g_dragging = false;
        InvalidateRect(window, &g_listRect, FALSE);
        return 0;

    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_scroll += delta > 0 ? -1 : 1;
        ClampScroll();
        InvalidateRect(window, &g_listRect, FALSE);
        return 0;
    }

    case WM_RBUTTONUP: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        POINT point{x, y};
        ClientToScreen(window, &point);
        const int index = TaskIndexAt(x, y);
        if (index >= 0) ShowTaskMenu(index, point);
        else ShowMainMenu(point);
        return 0;
    }

    case WM_COMMAND:
        HandleCommand(LOWORD(wParam));
        return 0;

    case WM_TIMER:
        if (wParam == kTimerId) {
            const bool wasBannerVisible = g_bannerVisible;
            RollToTodayIfNeeded();
            if (!g_pending.empty() && !g_bannerVisible && std::time(nullptr) >= g_snoozeUntil) {
                g_bannerVisible = true;
            }
            if (g_bannerVisible != wasBannerVisible) Refresh();
        }
        return 0;

    case WM_HOTKEY:
        if (wParam == kHotkeyId) ShowNote(!IsWindowVisible(window));
        return 0;

    case kTrayMessage: {
        const UINT event = LOWORD(lParam);
        if (event == WM_LBUTTONUP || event == NIN_SELECT) {
            ShowNote(!IsWindowVisible(window));
        } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
            POINT point{};
            GetCursorPos(&point);
            ShowMainMenu(point);
        }
        return 0;
    }

    case WM_APP + 2:
        ShowNote(true);
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && g_mode == WindowMode::Desktop && IsWindowVisible(window)) {
            SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;

    case WM_QUERYENDSESSION:
        SaveDay();
        SaveWindowPlacement();
        SaveSettings();
        return TRUE;

    case WM_CLOSE:
        if (!g_quitting) {
            ShowNote(false);
            return 0;
        }
        break;

    case WM_DESTROY:
        SaveDay();
        SaveWindowPlacement();
        SaveSettings();
        KillTimer(window, kTimerId);
        UnregisterHotKey(window, kHotkeyId);
        Shell_NotifyIconW(NIM_DELETE, &g_tray);
        if (g_font) DeleteObject(g_font);
        if (g_smallFont) DeleteObject(g_smallFont);
        if (g_dateFont) DeleteObject(g_dateFont);
        if (g_doneFont) DeleteObject(g_doneFont);
        if (g_editBrush) DeleteObject(g_editBrush);
        g_font = g_smallFont = g_dateFont = g_doneFont = nullptr;
        g_editBrush = nullptr;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int showCommand) {
    static_cast<void>(previous);
    static_cast<void>(showCommand);
    if (commandLine && wcsstr(commandLine, L"--self-test")) return RunSelfTest();

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\StarfruitNotes.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kClassName, nullptr)) {
            SendMessageW(existing, WM_APP + 2, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    InitializeStorage();
    MigrateLegacyStartup();
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_dpi = GetDpiForSystem();
    g_appIcon = LoadIconW(instance, MAKEINTRESOURCEW(kIconResource));

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass)) {
        if (mutex) CloseHandle(mutex);
        return 1;
    }

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int width = std::clamp(ReadSettingInt(L"WindowW", Scale(300)), Scale(240), Scale(480));
    int height = std::clamp(ReadSettingInt(L"WindowH", Scale(400)), Scale(260), Scale(700));
    int x = ReadSettingInt(L"WindowX", work.right - width - Scale(24));
    int y = ReadSettingInt(L"WindowY", work.bottom - height - Scale(24));
    RECT proposed{x, y, x + width, y + height};
    if (!MonitorFromRect(&proposed, MONITOR_DEFAULTTONULL)) {
        x = work.right - width - Scale(24);
        y = work.bottom - height - Scale(24);
    }

    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"Starfruit Notes",
                                  WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN,
                                  x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!window) {
        if (mutex) CloseHandle(mutex);
        return 2;
    }

    if (g_visible) {
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        ApplyWindowMode(false);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (mutex) CloseHandle(mutex);
    return static_cast<int>(message.wParam);
}

namespace {

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK EditProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                          UINT_PTR subclassId, DWORD_PTR referenceData);

HFONT MakeFont(int px, int weight, bool strikeout = false) {
    return CreateFontW(-Scale(px), 0, 0, 0, weight, FALSE, FALSE, strikeout ? TRUE : FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void CreateUiResources() {
    if (g_font) DeleteObject(g_font);
    if (g_smallFont) DeleteObject(g_smallFont);
    if (g_dateFont) DeleteObject(g_dateFont);
    if (g_doneFont) DeleteObject(g_doneFont);
    if (g_editBrush) DeleteObject(g_editBrush);

    g_font = MakeFont(12, FW_NORMAL);
    g_smallFont = MakeFont(10, FW_NORMAL);
    g_dateFont = MakeFont(15, FW_SEMIBOLD);
    g_doneFont = MakeFont(12, FW_NORMAL, true);
    g_editBrush = CreateSolidBrush(CurrentTheme().field);
    if (g_edit) SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
}

void FillSolid(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border, int borderWidth = 1) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, borderWidth), border);
    const auto oldBrush = SelectObject(dc, brush);
    const auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawTextIn(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, UINT flags) {
    const auto oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, flags);
    SelectObject(dc, oldFont);
}

void DrawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, std::max(1, width), color);
    const auto oldPen = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

bool PointIn(const RECT& rect, int x, int y) {
    POINT point{x, y};
    return PtInRect(&rect, point) != FALSE;
}

int PendingCount() {
    return static_cast<int>(g_pending.size());
}

int CompletedCount() {
    return static_cast<int>(std::count_if(g_tasks.begin(), g_tasks.end(), [](const Task& task) {
        return task.done;
    }));
}

void ClampScroll() {
    const int rowHeight = Scale(32);
    const int visible = std::max(1, static_cast<int>((g_listRect.bottom - g_listRect.top) / rowHeight));
    const int maximum = std::max(0, static_cast<int>(g_tasks.size()) - visible);
    g_scroll = std::clamp(g_scroll, 0, maximum);
}

void LayoutControls() {
    if (!g_window) return;
    RECT client{};
    GetClientRect(g_window, &client);
    const int width = client.right;
    const int banner = g_bannerVisible ? Scale(90) : 0;
    const int footer = Scale(29);

    g_pinRect = {width - Scale(82), banner + Scale(8), width - Scale(58), banner + Scale(32)};
    g_menuRect = {width - Scale(56), banner + Scale(8), width - Scale(32), banner + Scale(32)};
    g_minimizeRect = {width - Scale(30), banner + Scale(8), width - Scale(6), banner + Scale(32)};
    g_inputRect = {Scale(12), banner + Scale(82), width - Scale(12), banner + Scale(112)};
    const bool allDone = !g_tasks.empty() && CompletedCount() == static_cast<int>(g_tasks.size());
    const int completedNotice = allDone ? Scale(28) : 0;
    g_listRect = {Scale(6), banner + Scale(118), width - Scale(6),
                  client.bottom - footer - Scale(2) - completedNotice};
    g_historyRect = {width - Scale(62), client.bottom - footer, width - Scale(8), client.bottom};

    const int buttonLeft = Scale(12);
    const int buttonGap = Scale(6);
    const int buttonWidth = std::max(50, (width - Scale(24) - buttonGap * 2) / 3);
    g_carryRect = {buttonLeft, Scale(52), buttonLeft + buttonWidth, Scale(76)};
    g_discardRect = {g_carryRect.right + buttonGap, Scale(52),
                     g_carryRect.right + buttonGap + buttonWidth, Scale(76)};
    g_snoozeRect = {g_discardRect.right + buttonGap, Scale(52), width - Scale(12), Scale(76)};

    ClampScroll();
    if (g_edit) {
        const int insetLeft = Scale(26);
        const int insetRight = Scale(9);
        const int insetY = Scale(3);
        MoveWindow(g_edit, g_inputRect.left + insetLeft, g_inputRect.top + insetY,
                   std::max(20, static_cast<int>(g_inputRect.right - g_inputRect.left - insetLeft - insetRight)),
                   std::max(18, static_cast<int>(g_inputRect.bottom - g_inputRect.top - insetY * 2)), TRUE);
    }
}

RECT TaskRectForVisibleRow(int visibleRow) {
    const int top = g_listRect.top + visibleRow * Scale(32);
    return {g_listRect.left, top, g_listRect.right, top + Scale(32)};
}

int TaskIndexAt(int x, int y) {
    if (!PointIn(g_listRect, x, y)) return -1;
    const int visibleRow = (y - g_listRect.top) / Scale(32);
    const int index = g_scroll + visibleRow;
    return index >= 0 && index < static_cast<int>(g_tasks.size()) ? index : -1;
}

void DrawPinIcon(HDC dc, const RECT& rect, COLORREF color) {
    const int cx = (rect.left + rect.right) / 2;
    const int cy = (rect.top + rect.bottom) / 2;
    DrawLine(dc, cx - Scale(4), cy - Scale(5), cx + Scale(4), cy - Scale(5), color, Scale(1));
    DrawLine(dc, cx - Scale(2), cy - Scale(5), cx - Scale(2), cy, color, Scale(1));
    DrawLine(dc, cx + Scale(2), cy - Scale(5), cx + Scale(2), cy, color, Scale(1));
    DrawLine(dc, cx - Scale(5), cy, cx + Scale(5), cy, color, Scale(1));
    DrawLine(dc, cx, cy, cx, cy + Scale(6), color, Scale(1));
}

void DrawActionIcons(HDC dc, const Theme& theme) {
    if (g_mode == WindowMode::Topmost) {
        FillRounded(dc, g_pinRect, Scale(6), theme.soft, theme.soft);
    }
    DrawPinIcon(dc, g_pinRect, g_mode == WindowMode::Topmost ? theme.accent : theme.sub);

    HBRUSH dot = CreateSolidBrush(theme.sub);
    for (int i = -1; i <= 1; ++i) {
        const int cx = (g_menuRect.left + g_menuRect.right) / 2 + i * Scale(5);
        const int cy = (g_menuRect.top + g_menuRect.bottom) / 2;
        RECT circle{cx - Scale(1), cy - Scale(1), cx + Scale(2), cy + Scale(2)};
        FillRect(dc, &circle, dot);
    }
    DeleteObject(dot);
    DrawLine(dc, g_minimizeRect.left + Scale(7), (g_minimizeRect.top + g_minimizeRect.bottom) / 2,
             g_minimizeRect.right - Scale(7), (g_minimizeRect.top + g_minimizeRect.bottom) / 2,
             theme.sub, Scale(1));
}

void DrawButton(HDC dc, RECT rect, const std::wstring& text, const Theme& theme, bool primary) {
    FillRounded(dc, rect, Scale(5), primary ? theme.accent : theme.bg,
                primary ? theme.accent : theme.line);
    DrawTextIn(dc, text, rect, g_smallFont, primary ? RGB(255,255,255) : theme.text,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void DrawBanner(HDC dc, const Theme& theme, int width) {
    RECT banner{0, 0, width, Scale(90)};
    FillSolid(dc, banner, theme.soft);
    RECT accent{0, 0, Scale(2), Scale(90)};
    FillSolid(dc, accent, theme.accent);

    RECT title{Scale(12), Scale(9), width - Scale(12), Scale(28)};
    DrawTextIn(dc, L"昨天还有 " + std::to_wstring(PendingCount()) + L" 项没完成",
               title, g_font, theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT subtitle{Scale(12), Scale(29), width - Scale(12), Scale(47)};
    DrawTextIn(dc, L"要不要带到今天？", subtitle, g_smallFont, theme.sub,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawButton(dc, g_carryRect, L"延续到今天", theme, true);
    DrawButton(dc, g_discardRect, L"不再延续", theme, false);
    RECT snooze = g_snoozeRect;
    DrawTextIn(dc, L"30 分钟后", snooze, g_smallFont, theme.sub,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void DrawCheckbox(HDC dc, int cx, int cy, bool checked, const Theme& theme) {
    HPEN pen = CreatePen(PS_SOLID, Scale(1), checked ? theme.accent : theme.checkbox);
    HBRUSH brush = CreateSolidBrush(checked ? theme.accent : theme.bg);
    const auto oldPen = SelectObject(dc, pen);
    const auto oldBrush = SelectObject(dc, brush);
    Ellipse(dc, cx - Scale(7), cy - Scale(7), cx + Scale(7), cy + Scale(7));
    if (checked) {
        HPEN check = CreatePen(PS_SOLID, Scale(2), RGB(255,255,255));
        SelectObject(dc, check);
        MoveToEx(dc, cx - Scale(3), cy, nullptr);
        LineTo(dc, cx - Scale(1), cy + Scale(3));
        LineTo(dc, cx + Scale(4), cy - Scale(4));
        SelectObject(dc, pen);
        DeleteObject(check);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawTasks(HDC dc, const Theme& theme, int width) {
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, g_listRect.left, g_listRect.top, g_listRect.right, g_listRect.bottom);
    const int rowHeight = Scale(32);
    const int visibleRows = std::max(0, static_cast<int>((g_listRect.bottom - g_listRect.top + rowHeight - 1) / rowHeight));

    for (int row = 0; row < visibleRows; ++row) {
        const int index = g_scroll + row;
        if (index >= static_cast<int>(g_tasks.size())) break;
        RECT taskRect = TaskRectForVisibleRow(row);
        if (index == g_hoverIndex || index == g_dragIndex) {
            FillRounded(dc, taskRect, Scale(7), index == g_dragIndex ? theme.bg : theme.hover,
                        index == g_dragIndex ? theme.line : theme.hover);
        }

        const int centerY = (taskRect.top + taskRect.bottom) / 2;
        DrawCheckbox(dc, taskRect.left + Scale(15), centerY, g_tasks[index].done, theme);
        RECT textRect{taskRect.left + Scale(31), taskRect.top, taskRect.right - Scale(36), taskRect.bottom};
        DrawTextIn(dc, g_tasks[index].text, textRect, g_tasks[index].done ? g_doneFont : g_font,
                   g_tasks[index].done ? theme.done : theme.text,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (index == g_hoverIndex) {
            RECT removeRect{taskRect.right - Scale(28), taskRect.top, taskRect.right - Scale(4), taskRect.bottom};
            DrawTextIn(dc, L"×", removeRect, g_font, theme.sub,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        if (g_dragging && index == g_dropIndex) {
            DrawLine(dc, taskRect.left + Scale(7), taskRect.top, taskRect.right - Scale(7), taskRect.top,
                     theme.accent, Scale(2));
        }
    }
    RestoreDC(dc, saved);

    if (g_tasks.empty()) {
        RECT empty{Scale(18), g_listRect.top + Scale(18), width - Scale(18), g_listRect.top + Scale(50)};
        DrawTextIn(dc, L"新的一天，还没有任务", empty, g_font, theme.sub,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT hint{Scale(18), g_listRect.top + Scale(47), width - Scale(18), g_listRect.top + Scale(74)};
        DrawTextIn(dc, L"在上方输入一件要完成的事", hint, g_smallFont, theme.placeholder,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

void PaintWindow(HDC targetDc) {
    RECT client{};
    GetClientRect(g_window, &client);
    const int width = client.right;
    const int height = client.bottom;
    const Theme theme = CurrentTheme();

    HDC dc = CreateCompatibleDC(targetDc);
    HBITMAP bitmap = CreateCompatibleBitmap(targetDc, width, height);
    const auto oldBitmap = SelectObject(dc, bitmap);
    FillSolid(dc, client, theme.bg);

    const int banner = g_bannerVisible ? Scale(90) : 0;
    if (g_bannerVisible) DrawBanner(dc, theme, width);

    RECT dateRect{Scale(14), banner + Scale(8), width - Scale(94), banner + Scale(29)};
    DrawTextIn(dc, FriendlyDate(), dateRect, g_dateFont, theme.text,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT weekdayRect{Scale(14), banner + Scale(28), width - Scale(94), banner + Scale(43)};
    std::wstring status = FriendlyWeekday() + (g_mode == WindowMode::Topmost ? L" · 置顶" : L" · 今天");
    DrawTextIn(dc, status, weekdayRect, g_smallFont, theme.sub,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawActionIcons(dc, theme);

    const int completed = CompletedCount();
    RECT progressLabel{Scale(14), banner + Scale(48), width - Scale(14), banner + Scale(65)};
    DrawTextIn(dc, L"今日进度", progressLabel, g_smallFont, theme.sub,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextIn(dc, std::to_wstring(completed) + L" / " + std::to_wstring(g_tasks.size()),
               progressLabel, g_smallFont, theme.sub,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT track{Scale(14), banner + Scale(70), width - Scale(14), banner + Scale(73)};
    FillRounded(dc, track, Scale(2), theme.track, theme.track);
    if (!g_tasks.empty()) {
        RECT progress = track;
        progress.right = progress.left + (track.right - track.left) * completed / static_cast<int>(g_tasks.size());
        if (progress.right > progress.left) FillRounded(dc, progress, Scale(2), theme.accent, theme.accent);
    }

    const bool inputFocused = GetFocus() == g_edit;
    FillRounded(dc, g_inputRect, Scale(8), inputFocused ? theme.bg : theme.field,
                inputFocused ? theme.accent : theme.field);
    const int plusX = g_inputRect.left + Scale(13);
    const int plusY = (g_inputRect.top + g_inputRect.bottom) / 2;
    DrawLine(dc, plusX - Scale(4), plusY, plusX + Scale(4), plusY, theme.sub, Scale(1));
    DrawLine(dc, plusX, plusY - Scale(4), plusX, plusY + Scale(4), theme.sub, Scale(1));
    DrawTasks(dc, theme, width);

    const int footerTop = height - Scale(29);
    DrawLine(dc, 0, footerTop, width, footerTop, theme.line, Scale(1));
    RECT footer{Scale(14), footerTop, width - Scale(14), height};
    DrawTextIn(dc, L"共 " + std::to_wstring(g_tasks.size()) + L" 项 · 剩 " +
                    std::to_wstring(static_cast<int>(g_tasks.size()) - completed) + L" 项",
               footer, g_smallFont, theme.sub, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextIn(dc, L"历史", g_historyRect, g_smallFont, theme.sub,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (!g_tasks.empty() && completed == static_cast<int>(g_tasks.size())) {
        RECT done{Scale(14), std::max(static_cast<int>(g_listRect.top), footerTop - Scale(28)),
                  width - Scale(14), footerTop};
        DrawTextIn(dc, L"✓  今天全部完成 · 明早自动开新页", done, g_smallFont, theme.sub,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    BitBlt(targetDc, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void Refresh() {
    LayoutControls();
    InvalidateRect(g_window, nullptr, FALSE);
}

} // namespace
