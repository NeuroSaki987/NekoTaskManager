#include "App.h"
#include "../resources/resource.h"

namespace {
constexpr UINT WM_APP_REFRESH_READY = WM_APP + 1;
constexpr UINT WM_APP_TRAYICON = WM_APP + 2;
constexpr UINT ID_TRAY_OPEN = 2001;
constexpr UINT ID_TRAY_EXIT = 2002;
constexpr UINT ID_FILTER_ALL = 3001;
constexpr UINT ID_FILTER_APPS = 3002;
constexpr UINT ID_FILTER_BACKGROUND = 3003;
constexpr UINT ID_PROCESS_DUMP = 1301;
constexpr UINT ID_PROCESS_KILLTREE = 1302;
constexpr UINT ID_PROCESS_SUSPEND = 1303;
constexpr UINT ID_PROCESS_RESUME = 1304;
constexpr UINT IDC_NEWTASK_EDIT = 4101;
constexpr UINT IDC_NEWTASK_RUN = 4102;
constexpr UINT IDC_NEWTASK_CMD = 4103;
constexpr UINT IDC_NEWTASK_BROWSE = 4104;
constexpr UINT IDC_NEWTASK_CANCEL = 4105;
constexpr UINT IDC_NEWTASK_ADMIN = 4106;
constexpr UINT WM_APP_NEWTASK_ACCEPT = WM_APP + 61;

std::wstring MenuTextForId(UINT id)
{
    switch (id) {
        case 1001: return L"结束任务";
        case 1002: return L"打开文件位置";
        case 1101: return L"优先级：实时";
        case 1102: return L"优先级：高";
        case 1103: return L"优先级：高于正常";
        case 1104: return L"优先级：正常";
        case 1105: return L"优先级：低于正常";
        case 1106: return L"优先级：空闲";
        case 1201: return L"亲和性：全部核心";
        case 1202: return L"亲和性：前半核心";
        case 1203: return L"亲和性：后半核心";
        case ID_TRAY_OPEN: return L"显示窗口";
        case ID_TRAY_EXIT: return L"退出";
        case ID_FILTER_ALL: return L"全部进程";
        case ID_FILTER_APPS: return L"应用";
        case ID_FILTER_BACKGROUND: return L"后台进程";
        case ID_PROCESS_DUMP: return L"创建内存转储文件";
        case ID_PROCESS_KILLTREE: return L"结束进程树";
        case ID_PROCESS_SUSPEND: return L"挂起进程";
        case ID_PROCESS_RESUME: return L"恢复进程";
        default: return L"";
    }
}

float ClampF(float v, float lo, float hi)
{
    return (std::max)(lo, (std::min)(hi, v));
}

D2D1_RECT_F RectFrom(const RECT& rc)
{
    return D2D1::RectF(static_cast<float>(rc.left), static_cast<float>(rc.top), static_cast<float>(rc.right), static_cast<float>(rc.bottom));
}

RECT MakeRect(int l, int t, int r, int b)
{
    RECT rc{ l, t, r, b };
    return rc;
}

std::wstring QuoteArg(const std::wstring& s)
{
    if (s.find(L' ') == std::wstring::npos && s.find(L'\t') == std::wstring::npos) return s;
    return L'"' + s + L'"';
}

bool PtInRectInclusive(const RECT& rc, POINT pt)
{
    return pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom;
}

std::wstring SanitizeFileName(std::wstring s)
{
    for (wchar_t& ch : s) {
        switch (ch) {
            case L'\\': case L'/': case L':': case L'*': case L'?': case L'"': case L'<': case L'>': case L'|':
                ch = L'_'; break;
            default: break;
        }
    }
    while (!s.empty() && (s.back() == L'.' || s.back() == L' ')) s.pop_back();
    if (s.empty()) s = L"process";
    return s;
}

D2D1_COLOR_F BadgeColorForText(const std::wstring& text, bool selected)
{
    unsigned hash = 2166136261u;
    for (wchar_t ch : text) {
        hash ^= static_cast<unsigned>(std::towlower(ch));
        hash *= 16777619u;
    }
    const float hue = static_cast<float>(hash % 360) / 360.0f;
    const float s = selected ? 0.28f : 0.22f;
    const float v = selected ? 0.92f : 0.86f;
    const float h = hue * 6.0f;
    const int i = static_cast<int>(std::floor(h));
    const float f = h - static_cast<float>(i);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    float r = v, g = t, b = p;
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return D2D1::ColorF(r, g, b, 1.0f);
}

struct NewTaskDialogResult {
    std::wstring text;
    bool accepted = false;
    bool runAsAdmin = false;
};

struct NewTaskDialogState {
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    RECT panel{};
    RECT titleRc{};
    RECT subtitleRc{};
    RECT adminRc{};
    RECT runRc{};
    RECT browseRc{};
    RECT cancelRc{};
    bool hoverRun = false;
    bool hoverBrowse = false;
    bool hoverCancel = false;
    bool runAsAdmin = false;
    NewTaskDialogResult result{};
};

D2D1_COLOR_F ToColor(COLORREF c, float a = 1.0f)
{
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, a);
}

void FillRoundRectGdi(HDC hdc, const RECT& rc, COLORREF color, int radius)
{
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void FrameRoundRectGdi(HDC hdc, const RECT& rc, COLORREF color, int radius)
{
    HBRUSH brush = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

bool PtInRectLocal(const RECT& rc, LPARAM lp)
{
    const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    return pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom;
}

bool CommitNewTaskDialog(NewTaskDialogState* state)
{
    if (!state || !state->edit) return false;
    wchar_t buf[32768]{};
    GetWindowTextW(state->edit, buf, static_cast<int>(std::size(buf)));
    std::wstring value = buf;
    if (value.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    state->result.text = value;
    state->result.runAsAdmin = state->runAsAdmin;
    state->result.accepted = true;
    DestroyWindow(state->hwnd);
    return true;
}

LRESULT CALLBACK NewTaskEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    switch (msg) {
        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                PostMessageW(GetParent(hwnd), WM_APP_NEWTASK_ACCEPT, 0, 0);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                PostMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'A')) {
                SendMessageW(hwnd, EM_SETSEL, 0, -1);
                return 0;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, NewTaskEditSubclassProc, 1);
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void LayoutNewTaskDialog(NewTaskDialogState* state)
{
    if (!state || !state->hwnd) return;
    RECT rc{};
    GetClientRect(state->hwnd, &rc);
    state->panel = MakeRect(0, 0, rc.right, rc.bottom);
    state->titleRc = MakeRect(24, 18, rc.right - 24, 50);
    state->subtitleRc = MakeRect(24, 50, rc.right - 24, 74);
    if (state->edit) MoveWindow(state->edit, 24, 86, rc.right - 48, 40, TRUE);
    state->adminRc = MakeRect(24, 142, rc.right - 24, 168);

    const int btnTop = rc.bottom - 58;
    const int runW = 96;
    const int cancelW = 96;
    const int browseW = 108;
    state->runRc = MakeRect(rc.right - 24 - runW, btnTop, rc.right - 24, btnTop + 36);
    state->cancelRc = MakeRect(state->runRc.left - 12 - cancelW, btnTop, state->runRc.left - 12, btnTop + 36);
    state->browseRc = MakeRect(state->cancelRc.left - 12 - browseW, btnTop, state->cancelRc.left - 12, btnTop + 36);

    HRGN rgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, 28, 28);
    SetWindowRgn(state->hwnd, rgn, TRUE);
}

void DrawNewTaskDialog(NewTaskDialogState* state, HDC hdc)
{
    if (!state) return;
    RECT client{};
    GetClientRect(state->hwnd, &client);
    FillRoundRectGdi(hdc, client, RGB(244, 249, 255), 28);
    FrameRoundRectGdi(hdc, client, RGB(207, 225, 245), 28);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(10, 17, 28));
    HFONT titleFont = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
    HFONT normalFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(hdc, titleFont);
    DrawTextW(hdc, L"新建任务", -1, &state->titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, normalFont);
    SetTextColor(hdc, RGB(79, 97, 120));
    DrawTextW(hdc, L"输入程序、文件夹、文档或 Internet 资源，Windows 将为你打开它。", -1, &state->subtitleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT checkBox = MakeRect(state->adminRc.left, state->adminRc.top + 3, state->adminRc.left + 18, state->adminRc.top + 21);
    FillRoundRectGdi(hdc, checkBox, RGB(248, 251, 255), 6);
    FrameRoundRectGdi(hdc, checkBox, state->runAsAdmin ? RGB(47, 128, 237) : RGB(186, 201, 220), 6);
    if (state->runAsAdmin) {
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(47, 128, 237));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, checkBox.left + 4, checkBox.top + 10, nullptr);
        LineTo(hdc, checkBox.left + 8, checkBox.top + 14);
        LineTo(hdc, checkBox.right - 4, checkBox.top + 5);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
    RECT adminTextRc = MakeRect(checkBox.right + 10, state->adminRc.top, state->adminRc.right, state->adminRc.bottom);
    SetTextColor(hdc, RGB(20, 30, 45));
    DrawTextW(hdc, L"以管理员身份创建此任务", -1, &adminTextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    auto drawButton = [&](const RECT& rc, const wchar_t* text, COLORREF fill, COLORREF border, COLORREF fg) {
        FillRoundRectGdi(hdc, rc, fill, 16);
        FrameRoundRectGdi(hdc, rc, border, 16);
        SetTextColor(hdc, fg);
        DrawTextW(hdc, text, -1, const_cast<RECT*>(&rc), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };
    drawButton(state->browseRc, L"浏览...", RGB(235, 242, 250), RGB(206, 220, 236), RGB(20, 30, 45));
    drawButton(state->cancelRc, L"取消", RGB(235, 242, 250), RGB(206, 220, 236), RGB(20, 30, 45));
    drawButton(state->runRc, L"确认", RGB(222, 236, 255), RGB(47, 128, 237), RGB(47, 128, 237));

    SelectObject(hdc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(normalFont);
}

LRESULT CALLBACK NewTaskDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<NewTaskDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE: {
            state = reinterpret_cast<NewTaskDialogState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            if (!state) return -1;
            state->hwnd = hwnd;
            state->edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_NEWTASK_EDIT)), nullptr, nullptr);
            HFONT font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
            SendMessageW(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SetWindowLongPtrW(state->edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(font));
            SetWindowSubclass(state->edit, NewTaskEditSubclassProc, 1, 0);
            LayoutNewTaskDialog(state);
            SetFocus(state->edit);
            return 0;
        }
        case WM_SIZE:
            if (state) { LayoutNewTaskDialog(state); InvalidateRect(hwnd, nullptr, TRUE); }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(248, 251, 255));
            SetTextColor(hdc, RGB(10, 17, 28));
            static HBRUSH editBrush = CreateSolidBrush(RGB(248, 251, 255));
            return reinterpret_cast<INT_PTR>(editBrush);
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawNewTaskDialog(state, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (hit == HTCLIENT) {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd, &pt);
                if (pt.y < 56) return HTCAPTION;
            }
            return hit;
        }
        case WM_APP_NEWTASK_ACCEPT:
            CommitNewTaskDialog(state);
            return 0;
        case WM_LBUTTONDOWN:
            if (!state) return 0;
            if (PtInRectLocal(state->adminRc, lParam)) { state->runAsAdmin = !state->runAsAdmin; InvalidateRect(hwnd, nullptr, TRUE); return 0; }
            if (PtInRectLocal(state->browseRc, lParam)) {
                wchar_t fileBuf[32768]{};
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFile = fileBuf;
                ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    SetWindowTextW(state->edit, fileBuf);
                    SetFocus(state->edit);
                }
                return 0;
            }
            if (PtInRectLocal(state->cancelRc, lParam)) { DestroyWindow(hwnd); return 0; }
            if (PtInRectLocal(state->runRc, lParam)) { CommitNewTaskDialog(state); return 0; }
            return 0;
        case WM_COMMAND:
            if (!state) return 0;
            if (LOWORD(wParam) == IDC_NEWTASK_EDIT && HIWORD(wParam) == EN_UPDATE) return 0;
            if (LOWORD(wParam) == IDC_NEWTASK_ADMIN) return 0;
            return 0;
        case WM_KEYDOWN:
            if (!state) return 0;
            if (wParam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
            if (wParam == VK_RETURN) { CommitNewTaskDialog(state); return 0; }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state && state->edit) {
                HFONT font = reinterpret_cast<HFONT>(GetWindowLongPtrW(state->edit, GWLP_USERDATA));
                if (font) DeleteObject(font);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShowNewTaskDialog(HWND owner, NewTaskDialogResult& result)
{
    const wchar_t* cls = L"NekoTaskNewTaskDialog";
    WNDCLASSW wc{};
    wc.lpfnWndProc = NewTaskDialogProc;
    wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);

    NewTaskDialogState state{};
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, cls, L"新建任务", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 568, 252, owner, nullptr, wc.hInstance, &state);
    if (!hwnd) return false;

    RECT ownerRc{};
    GetWindowRect(owner, &ownerRc);
    SetWindowPos(hwnd, HWND_TOPMOST, ownerRc.left + ((ownerRc.right - ownerRc.left) - 568) / 2, ownerRc.top + ((ownerRc.bottom - ownerRc.top) - 252) / 2, 568, 252, SWP_SHOWWINDOW);
    EnableWindow(owner, FALSE);

    MSG msg{};
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    result = state.result;
    return result.accepted;
}

}

App::App() = default;

App::~App()
{
    RemoveTrayIcon();
    DiscardGraphicsResources();
    SafeRelease(m_wicFactory);
    SafeRelease(m_titleFormat);
    SafeRelease(m_normalFormat);
    SafeRelease(m_smallFormat);
    SafeRelease(m_monoFormat);
    SafeRelease(m_pixelValueFormat);
    SafeRelease(m_pixelChartFormat);
    SafeRelease(m_dwriteFactory);
    SafeRelease(m_d2dFactory);
    CoUninitialize();
}

bool App::Initialize(HINSTANCE instance, int cmdShow)
{
    m_instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InitCommonControls();
    m_fonts.LoadEmbeddedFonts(instance);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return false;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&m_d2dFactory)))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_dwriteFactory)))) return false;
    if (!CreateTextFormats()) return false;

    m_metrics.Initialize();
    if (!CreateMainWindow(instance, cmdShow)) return false;

    LoadPreferences();
    UpdateRefreshTimer();
    m_autoStartEnabled = IsAutoStartEnabled();
    m_tab = (m_defaultStartPage == DefaultStartPage::Performance) ? Tab::Performance : Tab::Processes;
    ApplyWindowPreferences();
    GetClientRect(m_hwnd, &m_client);
    UpdateLayout(m_client.right - m_client.left, m_client.bottom - m_client.top);
    RefreshData(true);
    return true;
}

int App::Run()
{
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

bool App::CreateMainWindow(HINSTANCE instance, int cmdShow)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = App::WndProc;
    wc.lpszClassName = L"NekoTaskManagerWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    RegisterClassExW(&wc);

    const DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
    m_hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"NekoTaskManager", style,
        CW_USEDEFAULT, CW_USEDEFAULT, 1580, 980, nullptr, nullptr, instance, this);
    if (!m_hwnd) return false;

    EnableBackdropEffect();
    ShowWindow(m_hwnd, cmdShow);
    UpdateWindow(m_hwnd);
    UpdateRefreshTimer();
    return true;
}


bool App::CreateTextFormats()
{
    SafeRelease(m_titleFormat);
    SafeRelease(m_normalFormat);
    SafeRelease(m_smallFormat);
    SafeRelease(m_monoFormat);
    SafeRelease(m_pixelValueFormat);
    SafeRelease(m_pixelChartFormat);

    const std::wstring uiFont = m_fonts.UiFontFamily();
    const std::wstring pixelFont = m_fonts.PixelFontFamily();
    const float titleSize = Theme::Dp(31.0f, static_cast<float>(m_dpi));
    const float normalSize = Theme::Dp(17.5f, static_cast<float>(m_dpi));
    const float smallSize = Theme::Dp(14.5f, static_cast<float>(m_dpi));
    const float monoSize = Theme::Dp(14.5f, static_cast<float>(m_dpi));
    const float pixelValueSize = Theme::Dp(30.0f, static_cast<float>(m_dpi));
    const float pixelChartSize = Theme::Dp(18.5f, static_cast<float>(m_dpi));

    if (FAILED(m_dwriteFactory->CreateTextFormat(pixelFont.c_str(), nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, titleSize, L"", &m_titleFormat))) return false;
    if (FAILED(m_dwriteFactory->CreateTextFormat(uiFont.c_str(), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, normalSize, L"", &m_normalFormat))) return false;
    if (FAILED(m_dwriteFactory->CreateTextFormat(uiFont.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, smallSize, L"", &m_smallFormat))) return false;
    if (FAILED(m_dwriteFactory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, monoSize, L"", &m_monoFormat))) return false;
    if (FAILED(m_dwriteFactory->CreateTextFormat(pixelFont.c_str(), nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, pixelValueSize, L"", &m_pixelValueFormat))) return false;
    if (FAILED(m_dwriteFactory->CreateTextFormat(pixelFont.c_str(), nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, pixelChartSize, L"", &m_pixelChartFormat))) return false;

    DWRITE_TRIMMING trimming{};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    IDWriteInlineObject* ellipsis = nullptr;
    m_dwriteFactory->CreateEllipsisTrimmingSign(m_smallFormat, &ellipsis);
    for (auto* fmt : { m_titleFormat, m_normalFormat, m_smallFormat, m_monoFormat, m_pixelValueFormat, m_pixelChartFormat }) {
        if (!fmt) continue;
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        fmt->SetTrimming(&trimming, ellipsis);
    }
    SafeRelease(ellipsis);
    return true;
}


void App::EnableBackdropEffect()
{
    const DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

bool App::CreateGraphicsResources()
{
    if (m_target) return true;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL createdLevel{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
        D3D11_SDK_VERSION, &m_d3dDevice, &createdLevel, &m_d3dContext);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, ARRAYSIZE(levels),
            D3D11_SDK_VERSION, &m_d3dDevice, &createdLevel, &m_d3dContext);
        if (FAILED(hr)) return false;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    hr = m_d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) return false;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) { SafeRelease(dxgiDevice); return false; }
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { SafeRelease(adapter); SafeRelease(dxgiDevice); return false; }

    D2D1_CREATION_PROPERTIES props{};
    props.threadingMode = D2D1_THREADING_MODE_SINGLE_THREADED;
    props.debugLevel = D2D1_DEBUG_LEVEL_NONE;
    hr = m_d2dFactory->CreateDevice(dxgiDevice, &m_d2dDevice);
    if (FAILED(hr)) {
        SafeRelease(factory); SafeRelease(adapter); SafeRelease(dxgiDevice);
        return false;
    }
    hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_target);
    if (FAILED(hr)) {
        SafeRelease(factory); SafeRelease(adapter); SafeRelease(dxgiDevice);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 0;
    desc.Height = 0;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    hr = factory->CreateSwapChainForHwnd(m_d3dDevice, m_hwnd, &desc, nullptr, nullptr, &m_swapChain);
    SafeRelease(factory);
    SafeRelease(adapter);
    SafeRelease(dxgiDevice);
    if (FAILED(hr)) return false;

    factory = nullptr;
    adapter = nullptr;
    dxgiDevice = nullptr;

    if (FAILED(m_target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &m_brush))) return false;
    ResizeSwapChain();
    LoadBackgroundBitmap();
    LoadAboutBitmap();
    return m_targetBitmap != nullptr;
}

void App::ResizeSwapChain()
{
    if (!m_swapChain || !m_target) return;
    m_target->SetTarget(nullptr);
    SafeRelease(m_targetBitmap);

    GetClientRect(m_hwnd, &m_client);
    const UINT w = static_cast<UINT>((std::max)(1L, m_client.right - m_client.left));
    const UINT h = static_cast<UINT>((std::max)(1L, m_client.bottom - m_client.top));
    m_swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);

    IDXGISurface* backBuffer = nullptr;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return;

    const float dpi = static_cast<float>(m_dpi);
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);

    if (SUCCEEDED(m_target->CreateBitmapFromDxgiSurface(backBuffer, &props, &m_targetBitmap))) {
        m_target->SetTarget(m_targetBitmap);
        m_target->SetDpi(dpi, dpi);
    }
    SafeRelease(backBuffer);
}

void App::DiscardGraphicsResources()
{
    ClearProcessIconCache();
    SafeRelease(m_backgroundBitmap);
    SafeRelease(m_aboutBitmap);
    SafeRelease(m_targetBitmap);
    SafeRelease(m_brush);
    SafeRelease(m_target);
    SafeRelease(m_d2dDevice);
    SafeRelease(m_swapChain);
    SafeRelease(m_d3dContext);
    SafeRelease(m_d3dDevice);
}

void App::ClearProcessIconCache()
{
    for (auto& item : m_processIconCache) SafeRelease(item.second);
    m_processIconCache.clear();
}

bool App::LoadProcessIconBitmap(const ProcessInfo& proc, ID2D1Bitmap** outBitmap)
{
    if (!m_target || !outBitmap) return false;
    if (!EnsureWicFactory()) return false;

    SHFILEINFOW sfi{};
    DWORD flags = SHGFI_ICON | SHGFI_SMALLICON;
    std::wstring source = proc.path.empty() ? L".exe" : proc.path;
    if (proc.path.empty()) flags |= SHGFI_USEFILEATTRIBUTES;
    const DWORD_PTR result = SHGetFileInfoW(source.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), flags);
    if (result == 0 || !sfi.hIcon) return false;

    ICONINFO iconInfo{};
    IWICBitmap* wicBitmap = nullptr;
    bool ok = false;
    if (GetIconInfo(sfi.hIcon, &iconInfo)) {
        HBITMAP colorBitmap = iconInfo.hbmColor ? iconInfo.hbmColor : iconInfo.hbmMask;
        if (colorBitmap &&
            SUCCEEDED(m_wicFactory->CreateBitmapFromHBITMAP(colorBitmap, nullptr, WICBitmapUsePremultipliedAlpha, &wicBitmap)) &&
            SUCCEEDED(m_target->CreateBitmapFromWicBitmap(wicBitmap, nullptr, outBitmap))) {
            ok = true;
        }
    }
    SafeRelease(wicBitmap);
    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
    DestroyIcon(sfi.hIcon);
    return ok;
}

ID2D1Bitmap* App::GetProcessIconBitmap(const ProcessInfo& proc)
{
    const std::wstring key = proc.path.empty() ? (L"name:" + ToLowerCopy(proc.name)) : (L"path:" + ToLowerCopy(proc.path));
    const auto it = m_processIconCache.find(key);
    if (it != m_processIconCache.end()) return it->second;

    ID2D1Bitmap* bitmap = nullptr;
    if (LoadProcessIconBitmap(proc, &bitmap) && bitmap) {
        m_processIconCache.emplace(key, bitmap);
        return bitmap;
    }
    return nullptr;
}

void App::DrawProcessGlyph(float left, float top, float size, const ProcessInfo& proc, bool selected)
{
    const auto& p = Theme::Current();
    if (ID2D1Bitmap* icon = GetProcessIconBitmap(proc)) {
        m_target->DrawBitmap(icon, D2D1::RectF(left, top, left + size, top + size), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        const D2D1_COLOR_F fill = selected ? p.accent : BadgeColorForText(proc.name, selected);
        DrawRoundedPanel(D2D1::RectF(left, top, left + size, top + size), fill, selected ? p.accent : p.border, size * 0.28f, 0.0f);
        std::wstring initial = L"?";
        if (!proc.name.empty()) initial = std::wstring(1, static_cast<wchar_t>(std::towupper(proc.name.front())));
        DrawTextLine(initial, D2D1::RectF(left, top - 1.0f, left + size, top + size + 1.0f), m_smallFormat, D2D1::ColorF(1, 1, 1, 0.98f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (proc.isSuspended) {
        const float badgeR = size * 0.28f;
        const float cx = left + size - badgeR * 0.65f;
        const float cy = top + size - badgeR * 0.65f;
        m_brush->SetColor(D2D1::ColorF(0.96f, 0.66f, 0.18f, 0.98f));
        m_target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), badgeR, badgeR), m_brush);
        m_brush->SetColor(D2D1::ColorF(1.f, 1.f, 1.f, 0.98f));
        const float barH = badgeR * 0.95f;
        const float barW = badgeR * 0.34f;
        m_target->FillRectangle(D2D1::RectF(cx - barW - 1.5f, cy - barH * 0.5f, cx - 1.5f, cy + barH * 0.5f), m_brush);
        m_target->FillRectangle(D2D1::RectF(cx + 1.5f, cy - barH * 0.5f, cx + barW + 1.5f, cy + barH * 0.5f), m_brush);
    }
}

bool App::EnsureWicFactory()
{
    if (m_wicFactory) return true;
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wicFactory)));
}

bool App::LoadBitmapFromResource(int resourceId, ID2D1Bitmap** outBitmap)
{
    if (!m_target || !outBitmap) return false;
    if (!EnsureWicFactory()) return false;

    HRSRC res = FindResourceW(m_instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!res) return false;
    HGLOBAL loaded = LoadResource(m_instance, res);
    if (!loaded) return false;
    const DWORD size = SizeofResource(m_instance, res);
    const void* data = LockResource(loaded);
    if (!data || size == 0) return false;

    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool ok = false;

    if (SUCCEEDED(m_wicFactory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<void*>(data)), size)) &&
        SUCCEEDED(m_wicFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(m_wicFactory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut)) &&
        SUCCEEDED(m_target->CreateBitmapFromWicBitmap(converter, nullptr, outBitmap))) {
        ok = true;
    }

    SafeRelease(converter);
    SafeRelease(frame);
    SafeRelease(decoder);
    SafeRelease(stream);
    return ok;
}

bool App::LoadBackgroundBitmap()
{
    if (m_backgroundBitmap) return true;
    return LoadBitmapFromResource(IDR_BACKGROUND_JPG, &m_backgroundBitmap);
}

bool App::LoadAboutBitmap()
{
    if (m_aboutBitmap) return true;
    return LoadBitmapFromResource(IDR_ABOUT_PNG, &m_aboutBitmap);
}


void App::RefreshData(bool forceProcessRefresh)
{
    if (!m_hwnd || IsIconic(m_hwnd) || (m_refreshSpeed == RefreshSpeed::Paused && !forceProcessRefresh)) return;

    const ULONGLONG now = GetTickCount64();
    bool needsRedraw = false;
    ULONGLONG metricsInterval = 900;
    ULONGLONG processInterval = 3500;
    switch (m_refreshSpeed) {
        case RefreshSpeed::High: metricsInterval = 450; processInterval = 1200; break;
        case RefreshSpeed::Normal: metricsInterval = 900; processInterval = 2500; break;
        case RefreshSpeed::Low: metricsInterval = 1800; processInterval = 5000; break;
        case RefreshSpeed::Paused: metricsInterval = processInterval = ~0ull; break;
    }

    if (m_lastMetricsTick == 0 || now - m_lastMetricsTick >= metricsInterval) {
        m_metrics.Sample();
        m_lastMetricsTick = now;
        needsRedraw = true;
    }

    const bool userInteracting = (now - m_lastInteractionTick) < 1200;
    const bool processTabVisible = (m_tab == Tab::Processes);
    if (forceProcessRefresh || (!userInteracting && processTabVisible && (m_lastProcessRefreshTick == 0 || now - m_lastProcessRefreshTick >= processInterval))) {
        m_processes.Refresh();
        m_lastProcessRefreshTick = now;
        RebuildTree();
        needsRedraw = true;
    }

    if (needsRedraw || forceProcessRefresh) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::MarkInteraction()
{
    m_lastInteractionTick = GetTickCount64();
}

void App::RebuildTree()
{
    m_flatProcesses = m_processes.BuildTree(m_filter, m_expanded);
    if (m_categoryFilter != ProcessCategoryFilter::All) {
        m_flatProcesses.erase(std::remove_if(m_flatProcesses.begin(), m_flatProcesses.end(), [this](const FlatProcessNode& node) {
            if (!node.process) return true;
            const bool isApp = node.process->isAppLike;
            if (m_categoryFilter == ProcessCategoryFilter::Apps) return !isApp;
            return isApp;
        }), m_flatProcesses.end());
    }
    if (m_selectedIndex >= static_cast<int>(m_flatProcesses.size())) {
        m_selectedIndex = m_flatProcesses.empty() ? -1 : static_cast<int>(m_flatProcesses.size()) - 1;
    }
    const int maxScroll = (std::max)(0, static_cast<int>(m_flatProcesses.size()) - VisibleProcessRows());
    m_scrollOffset = (std::clamp)(m_scrollOffset, 0, maxScroll);
}

int App::VisibleProcessRows() const
{
    const int h = m_tableRowsRect.bottom - m_tableRowsRect.top;
    const int available = (std::max)(0, h);
    return (std::max)(1, available / (std::max)(1, m_rowHeight));
}

void App::UpdateLayout(int width, int height)
{
    width = (std::max)(1140, width);
    height = (std::max)(760, height);

    const int margin = static_cast<int>(Theme::Dp(20.0f, static_cast<float>(m_dpi)));
    const int gap = static_cast<int>(Theme::Dp(16.0f, static_cast<float>(m_dpi)));
    const int topBarH = static_cast<int>(Theme::Dp(44.0f, static_cast<float>(m_dpi)));
    m_resizeBorder = static_cast<int>(Theme::Dp(10.0f, static_cast<float>(m_dpi)));

    m_sidebarWidth = (std::clamp)(static_cast<int>(width / 4.8f), static_cast<int>(Theme::Dp(248.0f, static_cast<float>(m_dpi))), static_cast<int>(Theme::Dp(340.0f, static_cast<float>(m_dpi))));
    m_rowHeight = static_cast<int>(Theme::Dp(width < 1280 ? 60.0f : 64.0f, static_cast<float>(m_dpi)));
    m_headerHeight = static_cast<int>(Theme::Dp(136.0f, static_cast<float>(m_dpi)));

    const RECT sidebar = MakeRect(margin, margin + topBarH + 10, margin + m_sidebarWidth, height - margin);
    m_contentRect = MakeRect(sidebar.right + gap, margin + topBarH + 10, width - margin, height - margin);
    m_dragRegion = MakeRect(margin, margin, width - margin, margin + topBarH);

    const int navTop = sidebar.top + static_cast<int>(Theme::Dp(122.0f, static_cast<float>(m_dpi)));
    const int navH = static_cast<int>(Theme::Dp(58.0f, static_cast<float>(m_dpi)));
    m_sidebarButtons = {
        { MakeRect(sidebar.left + 16, navTop, sidebar.right - 16, navTop + navH), L"进程", m_tab == Tab::Processes },
        { MakeRect(sidebar.left + 16, navTop + navH + 12, sidebar.right - 16, navTop + navH * 2 + 12), L"性能", m_tab == Tab::Performance },
        { MakeRect(sidebar.left + 16, navTop + (navH + 12) * 2, sidebar.right - 16, navTop + navH * 3 + 24), L"关于", m_tab == Tab::About },
    };

    const int buttonH = static_cast<int>(Theme::Dp(54.0f, static_cast<float>(m_dpi)));
    const int buttonW = static_cast<int>(Theme::Dp(132.0f, static_cast<float>(m_dpi)));
    const int contentW = m_contentRect.right - m_contentRect.left;
    const int actionsTop = m_contentRect.top + static_cast<int>(Theme::Dp(62.0f, static_cast<float>(m_dpi)));
    const int minSearchW = static_cast<int>(Theme::Dp(360.0f, static_cast<float>(m_dpi)));
    int searchW = contentW - buttonW * 2 - gap * 2;
    searchW = (std::max)(minSearchW, searchW);
    m_searchBox = MakeRect(m_contentRect.left, actionsTop, m_contentRect.left + searchW, actionsTop + buttonH);
    m_newTaskButton = { MakeRect(m_searchBox.right + gap, actionsTop, m_searchBox.right + gap + buttonW, actionsTop + buttonH), L"新建任务", false };
    m_refreshButton = { MakeRect(m_newTaskButton.rc.right + gap, actionsTop, m_newTaskButton.rc.right + gap + buttonW, actionsTop + buttonH), L"刷新", false };
    if (m_refreshButton.rc.right > m_contentRect.right) {
        const int overflow = m_refreshButton.rc.right - m_contentRect.right;
        m_searchBox.right -= overflow;
        m_newTaskButton.rc = MakeRect(m_searchBox.right + gap, actionsTop, m_searchBox.right + gap + buttonW, actionsTop + buttonH);
        m_refreshButton.rc = MakeRect(m_newTaskButton.rc.right + gap, actionsTop, m_newTaskButton.rc.right + gap + buttonW, actionsTop + buttonH);
    }

    const int chipTop = actionsTop + buttonH + 10;
    const int chipW = static_cast<int>(Theme::Dp(120.0f, static_cast<float>(m_dpi)));
    const int chipH = static_cast<int>(Theme::Dp(40.0f, static_cast<float>(m_dpi)));
    m_filterButtons[0] = MakeRect(m_contentRect.left, chipTop, m_contentRect.left + chipW, chipTop + chipH);
    m_filterButtons[1] = MakeRect(m_filterButtons[0].right + 10, chipTop, m_filterButtons[0].right + 10 + chipW, chipTop + chipH);
    m_filterButtons[2] = MakeRect(m_filterButtons[1].right + 10, chipTop, m_filterButtons[1].right + 10 + chipW, chipTop + chipH);

    m_contentTop = (m_tab == Tab::Processes) ? (chipTop + chipH + gap) : (actionsTop + buttonH + gap);
    m_tableRect = MakeRect(m_contentRect.left, m_contentTop, m_contentRect.right, m_contentRect.bottom);
    m_tableHeaderRect = MakeRect(m_tableRect.left + 22, m_tableRect.top + 18, m_tableRect.right - 36, m_tableRect.top + 60);
    m_tableRowsRect = MakeRect(m_tableRect.left + 16, m_tableRect.top + 68, m_tableRect.right - static_cast<int>(Theme::Dp(24.0f, static_cast<float>(m_dpi))), m_tableRect.bottom - 50);
    m_footerHintRect = MakeRect(m_tableRect.left + 16, m_tableRect.bottom - 40, m_tableRect.right - 18, m_tableRect.bottom - 12);

    const int settingsLeft = m_tableRect.left + 28;
    const int settingsTop = m_tableRect.top + 122;
    const int cardW = static_cast<int>(Theme::Dp(320.0f, static_cast<float>(m_dpi)));
    const int cardH = static_cast<int>(Theme::Dp(52.0f, static_cast<float>(m_dpi)));
    const int settingsGap = static_cast<int>(Theme::Dp(14.0f, static_cast<float>(m_dpi)));
    m_startupToggleRect = MakeRect(settingsLeft, settingsTop, settingsLeft + cardW, settingsTop + cardH);
    const int refreshTop = m_startupToggleRect.bottom + settingsGap + 32;
    for (int i = 0; i < 4; ++i) {
        m_refreshSpeedRects[i] = MakeRect(settingsLeft + i * (cardW / 4 + 8), refreshTop, settingsLeft + i * (cardW / 4 + 8) + cardW / 4, refreshTop + static_cast<int>(Theme::Dp(40.0f, static_cast<float>(m_dpi))));
    }
    const int pageTop = refreshTop + static_cast<int>(Theme::Dp(72.0f, static_cast<float>(m_dpi)));
    for (int i = 0; i < 2; ++i) {
        m_defaultPageRects[i] = MakeRect(settingsLeft + i * (cardW / 2 + 8), pageTop, settingsLeft + i * (cardW / 2 + 8) + cardW / 2, pageTop + static_cast<int>(Theme::Dp(40.0f, static_cast<float>(m_dpi))));
    }
    const int windowTop = pageTop + static_cast<int>(Theme::Dp(74.0f, static_cast<float>(m_dpi)));
    m_topMostRect = MakeRect(settingsLeft, windowTop, settingsLeft + cardW, windowTop + cardH);
    m_minimizeHideRect = MakeRect(settingsLeft, windowTop + cardH + settingsGap, settingsLeft + cardW, windowTop + cardH * 2 + settingsGap);
    RebuildTree();
}

void App::DrawRoundedPanel(const D2D1_RECT_F& rc, D2D1_COLOR_F fill, D2D1_COLOR_F border, float radius, float stroke)
{
    m_brush->SetColor(fill);
    m_target->FillRoundedRectangle(D2D1::RoundedRect(rc, radius, radius), m_brush);
    if (stroke > 0.0f) {
        m_brush->SetColor(border);
        m_target->DrawRoundedRectangle(D2D1::RoundedRect(rc, radius, radius), m_brush, stroke);
    }
}

void App::DrawTextLine(const std::wstring& text, const D2D1_RECT_F& rc, IDWriteTextFormat* format, D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT align, DWRITE_PARAGRAPH_ALIGNMENT paragraph)
{
    if (!format) return;
    format->SetTextAlignment(align);
    format->SetParagraphAlignment(paragraph);
    m_brush->SetColor(color);
    m_target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, rc, m_brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void App::DrawMetricCard(float x, float y, float w, float h, const std::wstring& title, const std::wstring& value, const std::wstring& subtitle, D2D1_COLOR_F accent)
{
    const auto& p = Theme::Current();
    DrawRoundedPanel(D2D1::RectF(x, y, x + w, y + h), p.panel, p.border, 20.0f, 1.0f);
    m_brush->SetColor(accent);
    m_target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 20.0f, y + 20.0f), 4.0f, 4.0f), m_brush);
    DrawTextLine(title, D2D1::RectF(x + 30.0f, y + 8.0f, x + w - 12.0f, y + 28.0f), m_smallFormat, p.subText);
    DrawTextLine(value, D2D1::RectF(x + 14.0f, y + 24.0f, x + w - 14.0f, y + 72.0f), m_pixelValueFormat, p.text);
    DrawTextLine(subtitle, D2D1::RectF(x + 14.0f, y + h - 24.0f, x + w - 14.0f, y + h - 8.0f), m_smallFormat, p.subText);
}

void App::DrawProgressBar(float x, float y, float w, float h, float ratio, D2D1_COLOR_F color)
{
    const auto& p = Theme::Current();
    DrawRoundedPanel(D2D1::RectF(x, y, x + w, y + h), p.panelAlt, p.panelAlt, h / 2.0f, 0.0f);
    const float fill = ClampF(w * ratio, 0.0f, w);
    DrawRoundedPanel(D2D1::RectF(x, y, x + fill, y + h), color, color, h / 2.0f, 0.0f);
}

void App::DrawLineChart(float x, float y, float w, float h, const std::deque<double>& values, D2D1_COLOR_F stroke, const std::wstring& label, const std::wstring& valueText)
{
    const auto& p = Theme::Current();
    DrawRoundedPanel(D2D1::RectF(x, y, x + w, y + h), p.panel, p.border, 18.0f, 1.0f);

    const float gx = x + 14.0f;
    const float gy = y + 40.0f;
    const float gw = w - 28.0f;
    const float gh = h - 56.0f;
    m_brush->SetColor(D2D1::ColorF(0.70f, 0.80f, 0.90f, 0.15f));
    for (int i = 1; i <= 4; ++i) {
        const float yy = gy + gh * (static_cast<float>(i) / 5.0f);
        m_target->DrawLine(D2D1::Point2F(gx, yy), D2D1::Point2F(gx + gw, yy), m_brush, 1.0f);
    }

    if (values.size() < 2) {
        DrawTextLine(L"等待采样…", D2D1::RectF(gx, gy + gh * 0.45f, gx + gw, gy + gh * 0.55f + 18.0f), m_smallFormat, p.subText, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        m_brush->SetColor(stroke);
        for (size_t i = 1; i < values.size(); ++i) {
            const float x1 = gx + gw * static_cast<float>(i - 1) / static_cast<float>(values.size() - 1);
            const float x2 = gx + gw * static_cast<float>(i) / static_cast<float>(values.size() - 1);
            const float y1 = gy + gh * (1.0f - ClampF(static_cast<float>(values[i - 1]) / 100.0f, 0.0f, 1.0f));
            const float y2 = gy + gh * (1.0f - ClampF(static_cast<float>(values[i]) / 100.0f, 0.0f, 1.0f));
            m_target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), m_brush, 2.0f);
        }
    }

    DrawTextLine(label, D2D1::RectF(x + 14.0f, y + 10.0f, x + w - 96.0f, y + 30.0f), m_normalFormat, p.text);
    DrawTextLine(valueText, D2D1::RectF(x + w - 98.0f, y + 10.0f, x + w - 14.0f, y + 30.0f), m_pixelChartFormat ? m_pixelChartFormat : m_pixelValueFormat, stroke, DWRITE_TEXT_ALIGNMENT_TRAILING);
}

void App::DrawBackground()
{
    const D2D1_RECT_F client = D2D1::RectF(0.0f, 0.0f, static_cast<float>(m_client.right), static_cast<float>(m_client.bottom));
    if (m_backgroundBitmap) {
        const D2D1_SIZE_F sz = m_backgroundBitmap->GetSize();
        const float scale = (std::max)(client.right / sz.width, client.bottom / sz.height);
        const float drawW = sz.width * scale;
        const float drawH = sz.height * scale;
        const float offsetX = (client.right - drawW) * 0.5f;
        const float offsetY = (client.bottom - drawH) * 0.5f;
        m_target->DrawBitmap(m_backgroundBitmap, D2D1::RectF(offsetX, offsetY, offsetX + drawW, offsetY + drawH), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    m_brush->SetColor(D2D1::ColorF(0x07101B, 0.20f));
    m_target->FillRectangle(client, m_brush);
}

void App::DrawSidebar()
{
    const auto& p = Theme::Current();
    const RECT rc = MakeRect(14, m_dragRegion.bottom + 8, 14 + m_sidebarWidth, m_client.bottom - 14);
    DrawRoundedPanel(RectFrom(rc), p.panel, p.border, 22.0f, 1.0f);
    DrawTextLine(L"NekoTaskManager", D2D1::RectF(static_cast<float>(rc.left + 20), static_cast<float>(rc.top + 20), static_cast<float>(rc.right - 20), static_cast<float>(rc.top + 58)), m_titleFormat, p.text);

    auto drawNavIcon = [&](const RECT& brc, size_t index, bool active) {
        const float size = 18.0f;
        const float left = static_cast<float>(brc.left + 16);
        const float top = static_cast<float>(brc.top + (brc.bottom - brc.top) * 0.5f - size * 0.5f);
        const D2D1_COLOR_F c = active ? p.accent : p.subText;
        m_brush->SetColor(c);
        const D2D1_RECT_F box = D2D1::RectF(left, top, left + size, top + size);
        if (index == 0) {
            m_target->DrawRoundedRectangle(D2D1::RoundedRect(box, 3.0f, 3.0f), m_brush, 1.35f);
            const float pad = 3.3f;
            const float gap = 2.0f;
            const float cell = (size - pad * 2.0f - gap) * 0.5f;
            for (int r = 0; r < 2; ++r) for (int col = 0; col < 2; ++col) {
                const float x = left + pad + col * (cell + gap);
                const float y = top + pad + r * (cell + gap);
                m_target->DrawRectangle(D2D1::RectF(x, y, x + cell, y + cell), m_brush, 1.1f);
            }
        } else if (index == 1) {
            m_target->DrawRoundedRectangle(D2D1::RoundedRect(box, 3.0f, 3.0f), m_brush, 1.35f);
            const D2D1_POINT_2F pts[] = {
                D2D1::Point2F(left + 3.0f, top + 10.5f),
                D2D1::Point2F(left + 5.8f, top + 10.5f),
                D2D1::Point2F(left + 8.0f, top + 7.0f),
                D2D1::Point2F(left + 10.6f, top + 12.6f),
                D2D1::Point2F(left + 14.5f, top + 5.2f)
            };
            for (int i = 0; i < 4; ++i) m_target->DrawLine(pts[i], pts[i + 1], m_brush, 1.35f);
        } else {
            const D2D1_POINT_2F ctr = D2D1::Point2F(left + size * 0.5f, top + size * 0.5f);
            m_target->DrawEllipse(D2D1::Ellipse(ctr, 5.2f, 5.2f), m_brush, 1.25f);
            for (int i = 0; i < 8; ++i) {
                const float a = static_cast<float>(i) * 3.1415926f / 4.0f;
                const float x1 = ctr.x + cosf(a) * 6.8f;
                const float y1 = ctr.y + sinf(a) * 6.8f;
                const float x2 = ctr.x + cosf(a) * 8.9f;
                const float y2 = ctr.y + sinf(a) * 8.9f;
                m_target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), m_brush, 1.2f);
            }
        }
    };

    for (size_t i = 0; i < m_sidebarButtons.size(); ++i) {
        const auto& btn = m_sidebarButtons[i];
        const auto fill = btn.active ? p.accentSoft : p.panelAlt;
        const auto border = btn.active ? p.accent : p.border;
        DrawRoundedPanel(RectFrom(btn.rc), fill, border, 15.0f, 1.0f);
        drawNavIcon(btn.rc, i, btn.active);
        DrawTextLine(btn.text, D2D1::RectF(static_cast<float>(btn.rc.left + 44), static_cast<float>(btn.rc.top), static_cast<float>(btn.rc.right - 12), static_cast<float>(btn.rc.bottom)), m_normalFormat, btn.active ? p.accent : p.text, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    const auto& s = m_metrics.Snapshot();
    const float cardX = static_cast<float>(rc.left + 12);
    const float cardW = static_cast<float>(rc.right - rc.left - 24);
    const float cardH = 120.0f;
    const float cardY = static_cast<float>(rc.bottom) - cardH - 12.0f;
    DrawRoundedPanel(D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH), p.panelAlt, p.border, 16.0f, 1.0f);
    DrawTextLine(L"系统状态", D2D1::RectF(cardX + 12.0f, cardY + 10.0f, cardX + cardW - 12.0f, cardY + 34.0f), m_normalFormat, p.text);
    DrawTextLine(L"CPU  " + FormatPercent1(s.cpuPercent), D2D1::RectF(cardX + 12.0f, cardY + 38.0f, cardX + cardW - 12.0f, cardY + 60.0f), m_smallFormat, p.cpu);
    DrawProgressBar(cardX + 12.0f, cardY + 62.0f, cardW - 24.0f, 9.0f, static_cast<float>(s.cpuPercent / 100.0), p.cpu);
    DrawTextLine(L"内存  " + FormatPercent1(s.memoryPercent), D2D1::RectF(cardX + 12.0f, cardY + 80.0f, cardX + cardW - 12.0f, cardY + 102.0f), m_smallFormat, p.mem);
    DrawProgressBar(cardX + 12.0f, cardY + 104.0f, cardW - 24.0f, 9.0f, static_cast<float>(s.memoryPercent / 100.0), p.mem);
}

void App::DrawHeader()
{
    const auto& p = Theme::Current();
    DrawTextLine(L"NekoTaskManager", D2D1::RectF(static_cast<float>(m_contentRect.left), static_cast<float>(m_contentRect.top + 4), static_cast<float>(m_contentRect.right), static_cast<float>(m_contentRect.top + 44)), m_titleFormat, p.text);

    DrawRoundedPanel(RectFrom(m_searchBox), p.panel, m_searchFocused ? p.accent : p.border, 16.0f, 1.0f);
    const std::wstring searchText = m_filter.empty() ? L"搜索进程名 / PID / 路径" : m_filter;
    DrawTextLine(searchText, D2D1::RectF(static_cast<float>(m_searchBox.left + 18), static_cast<float>(m_searchBox.top + 2), static_cast<float>(m_searchBox.right - 18), static_cast<float>(m_searchBox.bottom - 2)), m_normalFormat, m_filter.empty() ? p.subText : p.text, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    DrawRoundedPanel(RectFrom(m_newTaskButton.rc), p.panelAlt, p.border, 16.0f, 1.0f);
    DrawTextLine(m_newTaskButton.text, RectFrom(m_newTaskButton.rc), m_normalFormat, p.text, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawRoundedPanel(RectFrom(m_refreshButton.rc), p.accentSoft, p.accent, 16.0f, 1.0f);
    DrawTextLine(m_refreshButton.text, RectFrom(m_refreshButton.rc), m_normalFormat, p.accent, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (m_tab == Tab::Processes) {
        const wchar_t* labels[3] = { L"全部", L"应用", L"后台" };
        for (int i = 0; i < 3; ++i) {
            const bool active = static_cast<int>(m_categoryFilter) == i;
            DrawRoundedPanel(RectFrom(m_filterButtons[i]), active ? p.accentSoft : p.panelAlt, active ? p.accent : p.border, 14.0f, 1.0f);
            DrawTextLine(labels[i], RectFrom(m_filterButtons[i]), m_smallFormat, active ? p.accent : p.text, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
}

void App::DrawProcessesView()
{
    const auto& p = Theme::Current();
    DrawRoundedPanel(RectFrom(m_tableRect), p.panel, p.border, 22.0f, 1.0f);

    const float contentWidth = static_cast<float>(m_tableHeaderRect.right - m_tableHeaderRect.left);
    const float x = static_cast<float>(m_tableHeaderRect.left);
    const float y = static_cast<float>(m_tableHeaderRect.top);
    const float iconW = static_cast<float>(Theme::Dp(34.0f, static_cast<float>(m_dpi)));

    const float pidW = (std::clamp)(contentWidth * 0.08f, 72.0f, 96.0f);
    const float cpuW = (std::clamp)(contentWidth * 0.08f, 66.0f, 88.0f);
    const float memW = (std::clamp)(contentWidth * 0.14f, 108.0f, 152.0f);
    const float threadW = (std::clamp)(contentWidth * 0.08f, 70.0f, 92.0f);
    const float pathW = (std::clamp)(contentWidth * 0.30f, 190.0f, 360.0f);
    const float nameW = contentWidth - iconW - pidW - cpuW - memW - threadW - pathW - 36.0f;

    DrawTextLine(L"名称", D2D1::RectF(x + 10.0f, y, x + 10.0f + iconW + nameW, y + 28.0f), m_normalFormat, p.subText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawTextLine(L"PID", D2D1::RectF(x + 10.0f + iconW + nameW, y, x + 10.0f + iconW + nameW + pidW, y + 28.0f), m_normalFormat, p.subText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawTextLine(L"CPU", D2D1::RectF(x + 10.0f + iconW + nameW + pidW, y, x + 10.0f + iconW + nameW + pidW + cpuW, y + 28.0f), m_normalFormat, p.subText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawTextLine(L"内存", D2D1::RectF(x + 10.0f + iconW + nameW + pidW + cpuW, y, x + 10.0f + iconW + nameW + pidW + cpuW + memW, y + 28.0f), m_normalFormat, p.subText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawTextLine(L"线程", D2D1::RectF(x + 10.0f + iconW + nameW + pidW + cpuW + memW, y, x + 10.0f + iconW + nameW + pidW + cpuW + memW + threadW, y + 28.0f), m_normalFormat, p.subText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawTextLine(L"路径", D2D1::RectF(x + 10.0f + iconW + nameW + pidW + cpuW + memW + threadW, y, x + contentWidth - 8.0f, y + 28.0f), m_normalFormat, p.subText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const int visible = VisibleProcessRows();
    const int start = (std::clamp)(m_scrollOffset, 0, (std::max)(0, static_cast<int>(m_flatProcesses.size()) - visible));
    const int end = (std::min)(static_cast<int>(m_flatProcesses.size()), start + visible);
    for (int i = start; i < end; ++i) {
        const float rowTop = static_cast<float>(m_tableRowsRect.top + (i - start) * m_rowHeight);
        const D2D1_RECT_F rowRc = D2D1::RectF(static_cast<float>(m_tableRowsRect.left), rowTop, static_cast<float>(m_tableRowsRect.right), rowTop + static_cast<float>(m_rowHeight - 6));
        const bool selected = i == m_selectedIndex;
        DrawRoundedPanel(rowRc, selected ? p.accentSoft : p.panelAlt, selected ? p.accent : p.border, 14.0f, 1.0f);

        const ProcessInfo& proc = *m_flatProcesses[i].process;
        const float iconSize = static_cast<float>(Theme::Dp(22.0f, static_cast<float>(m_dpi)));
        const float iconX = rowRc.left + 12.0f;
        const float iconY = rowRc.top + (rowRc.bottom - rowRc.top - iconSize) * 0.5f;
        DrawProcessGlyph(iconX, iconY, iconSize, proc, selected);

        const float baseX = rowRc.left + 16.0f + iconW;
        const float textTop = rowRc.top + 7.0f;
        const float lineH = 22.0f;
        const std::wstring displayName = proc.isSuspended ? (proc.name + L" (已挂起)") : proc.name;
        DrawTextLine(displayName, D2D1::RectF(baseX, textTop, baseX + nameW, textTop + lineH), m_normalFormat, proc.isSuspended ? p.accent : p.text);
        DrawTextLine(std::to_wstring(proc.pid), D2D1::RectF(baseX + nameW, textTop, baseX + nameW + pidW, textTop + lineH), m_monoFormat, p.subText);
        DrawTextLine(FormatPercent1(proc.cpuPercent), D2D1::RectF(baseX + nameW + pidW, textTop, baseX + nameW + pidW + cpuW, textTop + lineH), m_monoFormat, p.cpu);
        DrawTextLine(FormatBytes(proc.workingSet), D2D1::RectF(baseX + nameW + pidW + cpuW, textTop, baseX + nameW + pidW + cpuW + memW, textTop + lineH), m_monoFormat, p.mem);
        DrawTextLine(std::to_wstring(proc.threadCount), D2D1::RectF(baseX + nameW + pidW + cpuW + memW, textTop, baseX + nameW + pidW + cpuW + memW + threadW, textTop + lineH), m_monoFormat, p.subText);
        DrawTextLine(proc.path.empty() ? L"—" : proc.path, D2D1::RectF(baseX + nameW + pidW + cpuW + memW + threadW, textTop, rowRc.right - 10.0f, textTop + lineH), m_smallFormat, p.subText);

        const std::wstring sub = L"PID " + std::to_wstring(proc.pid) + L"  ·  线程 " + std::to_wstring(proc.threadCount);
        DrawTextLine(sub, D2D1::RectF(baseX, textTop + 22.0f, baseX + nameW, textTop + 40.0f), m_smallFormat, selected ? p.accent : p.subText);
    }

    std::wstring footer = L"共 " + std::to_wstring(m_flatProcesses.size()) + L" 个进程";
    DrawTextLine(footer, RectFrom(m_footerHintRect), m_smallFormat, p.subText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void App::DrawPerformanceView()
{
    const auto& p = Theme::Current();
    const auto& s = m_metrics.Snapshot();
    DrawRoundedPanel(RectFrom(m_tableRect), p.panel, p.border, 22.0f, 1.0f);

    const float left = static_cast<float>(m_tableRect.left + 14);
    const float top = static_cast<float>(m_tableRect.top + 14);
    const float width = static_cast<float>(m_tableRect.right - m_tableRect.left - 28);
    const float gap = 12.0f;
    const float cardH = 110.0f;
    const bool narrow = width < 980.0f;
    const int cols = narrow ? 2 : 4;
    const float cardW = (width - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);

    DrawMetricCard(left, top, cardW, cardH, L"CPU", FormatPercent1(s.cpuPercent), L"总处理器利用率", p.cpu);
    DrawMetricCard(left + (cardW + gap) * 1, top, cardW, cardH, L"内存", FormatPercent1(s.memoryPercent), FormatBytes(s.memoryUsed) + L" / " + FormatBytes(s.memoryTotal), p.mem);
    const std::wstring gpuSubtitle = (s.gpuName.empty() ? L"GPU" : s.gpuName) + (s.gpuIsDiscrete ? L" · 独显综合利用率" : L" · 核显利用率");
    if (cols == 4) {
        DrawMetricCard(left + (cardW + gap) * 2, top, cardW, cardH, L"磁盘", FormatPercent1(s.diskPercent), L"活动时间", p.disk);
        DrawMetricCard(left + (cardW + gap) * 3, top, cardW, cardH, L"GPU", FormatPercent1(s.gpuPercent), gpuSubtitle, p.accent);
    } else {
        DrawMetricCard(left, top + cardH + gap, cardW, cardH, L"磁盘", FormatPercent1(s.diskPercent), L"活动时间", p.disk);
        DrawMetricCard(left + (cardW + gap), top + cardH + gap, cardW, cardH, L"GPU", FormatPercent1(s.gpuPercent), gpuSubtitle, p.accent);
    }

    const float chartsTop = narrow ? (top + cardH * 2 + gap * 2 + 6.0f) : (top + cardH + gap + 6.0f);
    const float chartsH = static_cast<float>(m_tableRect.bottom) - chartsTop - 16.0f;
    const float chartW = (width - gap) * 0.5f;
    const float chartH = (chartsH - gap) * 0.5f;
    DrawLineChart(left, chartsTop, chartW, chartH, m_metrics.CpuHistory(), p.cpu, L"CPU", FormatPercent1(s.cpuPercent));
    DrawLineChart(left + chartW + gap, chartsTop, chartW, chartH, m_metrics.MemHistory(), p.mem, L"内存", FormatPercent1(s.memoryPercent));
    DrawLineChart(left, chartsTop + chartH + gap, chartW, chartH, m_metrics.DiskHistory(), p.disk, L"磁盘", FormatPercent1(s.diskPercent));
    DrawLineChart(left + chartW + gap, chartsTop + chartH + gap, chartW, chartH, m_metrics.GpuHistory(), p.accent, s.gpuName.empty() ? L"GPU" : (L"GPU · " + s.gpuName), FormatPercent1(s.gpuPercent));
}

void App::DrawAboutView()
{
    const auto& p = Theme::Current();
    DrawRoundedPanel(RectFrom(m_tableRect), p.panel, p.border, 22.0f, 1.0f);

    const float left = static_cast<float>(m_tableRect.left + 28);
    const float top = static_cast<float>(m_tableRect.top + 24);
    const float right = static_cast<float>(m_tableRect.right - 28);
    const float textRight = right - static_cast<float>(Theme::Dp(352.0f, static_cast<float>(m_dpi)));

    DrawTextLine(L"NekoTaskManager v1.1", D2D1::RectF(left, top, right, top + 44.0f), m_titleFormat, p.accent);
    DrawTextLine(L"本软件由 @NeuroSaki987 开发", D2D1::RectF(left, top + 58.0f, textRight, top + 90.0f), m_normalFormat, p.text);

    auto drawToggleCard = [&](const RECT& rc, const std::wstring& title, bool enabled) {
        DrawRoundedPanel(RectFrom(rc), p.panelAlt, enabled ? p.accent : p.border, 18.0f, 1.0f);
        DrawTextLine(title, D2D1::RectF(static_cast<float>(rc.left + 18), static_cast<float>(rc.top), static_cast<float>(rc.right - 84), static_cast<float>(rc.bottom)), m_normalFormat, p.text, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        const D2D1_RECT_F switchRc = D2D1::RectF(static_cast<float>(rc.right - 72), static_cast<float>(rc.top + 11), static_cast<float>(rc.right - 18), static_cast<float>(rc.bottom - 11));
        DrawRoundedPanel(switchRc, enabled ? p.accentSoft : p.panel, enabled ? p.accent : p.border, (switchRc.bottom - switchRc.top) * 0.5f, 1.0f);
        const float knobR = (switchRc.bottom - switchRc.top) * 0.5f - 4.0f;
        const float knobX = enabled ? (switchRc.right - knobR - 4.0f) : (switchRc.left + knobR + 4.0f);
        m_brush->SetColor(enabled ? p.accent : p.subText);
        m_target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, (switchRc.top + switchRc.bottom) * 0.5f), knobR, knobR), m_brush);
    };
    auto drawChoice = [&](const RECT& rc, const std::wstring& label, bool active) {
        DrawRoundedPanel(RectFrom(rc), active ? p.accentSoft : p.panelAlt, active ? p.accent : p.border, 14.0f, 1.0f);
        DrawTextLine(label, RectFrom(rc), m_smallFormat, active ? p.accent : p.text, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };

    DrawTextLine(L" ", D2D1::RectF(left, top + 118.0f, textRight, top + 148.0f), m_normalFormat, p.text);
    drawToggleCard(m_startupToggleRect, L"开机启动", m_autoStartEnabled);

    DrawTextLine(L"更新速度", D2D1::RectF(left, static_cast<float>(m_startupToggleRect.bottom + 18), textRight, static_cast<float>(m_startupToggleRect.bottom + 46)), m_normalFormat, p.text);
    const wchar_t* speedLabels[4] = { L"高", L"常规", L"低", L"已暂停" };
    for (int i = 0; i < 4; ++i) drawChoice(m_refreshSpeedRects[i], speedLabels[i], static_cast<int>(m_refreshSpeed) == i);

    DrawTextLine(L"默认起始页", D2D1::RectF(left, static_cast<float>(m_defaultPageRects[0].top - 30), textRight, static_cast<float>(m_defaultPageRects[0].top - 2)), m_normalFormat, p.text);
    drawChoice(m_defaultPageRects[0], L"进程", m_defaultStartPage == DefaultStartPage::Processes);
    drawChoice(m_defaultPageRects[1], L"性能", m_defaultStartPage == DefaultStartPage::Performance);

    DrawTextLine(L"窗口管理", D2D1::RectF(left, static_cast<float>(m_topMostRect.top - 30), textRight, static_cast<float>(m_topMostRect.top - 2)), m_normalFormat, p.text);
    drawToggleCard(m_topMostRect, L"置于顶层", m_alwaysOnTop);
    drawToggleCard(m_minimizeHideRect, L"最小化时隐藏", m_hideOnMinimize);

    if (m_aboutBitmap) {
        const float imgW = static_cast<float>(Theme::Dp(308.0f, static_cast<float>(m_dpi)));
        const float imgH = static_cast<float>(Theme::Dp(432.0f, static_cast<float>(m_dpi)));
        const float imgX = right - imgW;
        const float imgY = top + 14.0f;
        DrawRoundedPanel(D2D1::RectF(imgX - 8.0f, imgY - 8.0f, imgX + imgW + 8.0f, imgY + imgH + 8.0f), p.panelAlt, p.border, 24.0f, 1.0f);
        m_target->DrawBitmap(m_aboutBitmap, D2D1::RectF(imgX, imgY, imgX + imgW, imgY + imgH), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
}

void App::DrawScrollbar()
{
    if (m_tab != Tab::Processes) return;
    const int total = static_cast<int>(m_flatProcesses.size());
    const int visible = VisibleProcessRows();
    if (total <= visible || visible <= 0) return;

    const auto& p = Theme::Current();
    const float trackW = static_cast<float>(Theme::Dp(10.0f, static_cast<float>(m_dpi)));
    const float margin = static_cast<float>(Theme::Dp(6.0f, static_cast<float>(m_dpi)));
    const float left = static_cast<float>(m_tableRect.right) - trackW - margin;
    const float top = static_cast<float>(m_tableRowsRect.top);
    const float bottom = static_cast<float>(m_tableRowsRect.bottom);
    const float trackH = bottom - top;
    DrawRoundedPanel(D2D1::RectF(left, top, left + trackW, bottom), p.panelAlt, p.border, trackW * 0.5f, 1.0f);

    const float thumbH = (std::max)(trackH * (static_cast<float>(visible) / static_cast<float>(total)), static_cast<float>(Theme::Dp(46.0f, static_cast<float>(m_dpi))));
    const float maxScroll = static_cast<float>((std::max)(1, total - visible));
    const float thumbY = top + (trackH - thumbH) * (static_cast<float>(m_scrollOffset) / maxScroll);
    DrawRoundedPanel(D2D1::RectF(left + 1.0f, thumbY, left + trackW - 1.0f, thumbY + thumbH), p.accentSoft, p.accent, trackW * 0.5f, 1.0f);
}

void App::DrawDialogOverlay()
{
    if (!m_dialog.visible) return;
    const auto& p = Theme::Current();
    const D2D1_RECT_F overlay = D2D1::RectF(0.0f, 0.0f, static_cast<float>(m_client.right), static_cast<float>(m_client.bottom));
    m_brush->SetColor(D2D1::ColorF(0.02f, 0.05f, 0.10f, 0.30f));
    m_target->FillRectangle(overlay, m_brush);

    DrawRoundedPanel(RectFrom(m_dialog.panel), p.panel, p.border, 24.0f, 1.0f);
    const float left = static_cast<float>(m_dialog.panel.left + 24);
    const float right = static_cast<float>(m_dialog.panel.right - 24);
    const float top = static_cast<float>(m_dialog.panel.top + 20);
    DrawTextLine(m_dialog.title, D2D1::RectF(left, top, right, top + 34.0f), m_titleFormat, p.text);
    DrawTextLine(m_dialog.message, D2D1::RectF(left, top + 42.0f, right, static_cast<float>(m_dialog.panel.bottom - 84)), m_normalFormat, p.subText);

    if (m_dialog.hasSecondary) {
        DrawRoundedPanel(RectFrom(m_dialog.secondaryButton), p.panelAlt, p.border, 16.0f, 1.0f);
        DrawTextLine(m_dialog.secondaryText, RectFrom(m_dialog.secondaryButton), m_normalFormat, p.text, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    DrawRoundedPanel(RectFrom(m_dialog.primaryButton), p.accentSoft, p.accent, 16.0f, 1.0f);
    DrawTextLine(m_dialog.primaryText, RectFrom(m_dialog.primaryButton), m_normalFormat, p.accent, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void App::Draw()
{
    if (!CreateGraphicsResources()) return;
    m_target->BeginDraw();
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    m_target->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawBackground();
    DrawSidebar();
    DrawHeader();
    switch (m_tab) {
        case Tab::Processes: DrawProcessesView(); break;
        case Tab::Performance: DrawPerformanceView(); break;
        case Tab::About: DrawAboutView(); break;
    }
    DrawScrollbar();
    DrawDialogOverlay();
    const HRESULT hr = m_target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardGraphicsResources();
    } else if (m_swapChain) {
        m_swapChain->Present(1, 0);
    }
}

int App::HitTestProcessRow(POINT pt) const
{
    if (m_tab != Tab::Processes) return -1;
    if (!PtInRectInclusive(m_tableRowsRect, pt)) return -1;
    const int localIndex = (pt.y - m_tableRowsRect.top) / (std::max)(1, m_rowHeight);
    const int index = m_scrollOffset + localIndex;
    return (index >= 0 && index < static_cast<int>(m_flatProcesses.size())) ? index : -1;
}

int App::HitTestTreeToggle(POINT pt) const
{
    (void)pt;
    return -1;
}

void App::ShowThemedDialog(const std::wstring& title, const std::wstring& message, const std::wstring& primaryText, const std::wstring& secondaryText, const std::wstring& actionPath)
{
    m_dialog.visible = true;
    m_dialog.title = title;
    m_dialog.message = message;
    m_dialog.primaryText = primaryText.empty() ? L"确定" : primaryText;
    m_dialog.secondaryText = secondaryText;
    m_dialog.actionPath = actionPath;
    m_dialog.hasSecondary = !secondaryText.empty();

    const int panelW = static_cast<int>(Theme::Dp(520.0f, static_cast<float>(m_dpi)));
    const int panelH = static_cast<int>(Theme::Dp(m_dialog.hasSecondary ? 250.0f : 220.0f, static_cast<float>(m_dpi)));
    const int cx = (m_client.right - m_client.left) / 2;
    const int cy = (m_client.bottom - m_client.top) / 2;
    m_dialog.panel = MakeRect(cx - panelW / 2, cy - panelH / 2, cx + panelW / 2, cy + panelH / 2);
    const int btnH = static_cast<int>(Theme::Dp(46.0f, static_cast<float>(m_dpi)));
    const int btnW = static_cast<int>(Theme::Dp(m_dialog.hasSecondary ? 146.0f : 160.0f, static_cast<float>(m_dpi)));
    const int gap = static_cast<int>(Theme::Dp(12.0f, static_cast<float>(m_dpi)));
    const int btnTop = m_dialog.panel.bottom - static_cast<int>(Theme::Dp(64.0f, static_cast<float>(m_dpi)));
    if (m_dialog.hasSecondary) {
        m_dialog.secondaryButton = MakeRect(m_dialog.panel.right - btnW * 2 - gap - 24, btnTop, m_dialog.panel.right - btnW - gap - 24, btnTop + btnH);
        m_dialog.primaryButton = MakeRect(m_dialog.panel.right - btnW - 24, btnTop, m_dialog.panel.right - 24, btnTop + btnH);
    } else {
        m_dialog.secondaryButton = RECT{};
        m_dialog.primaryButton = MakeRect(m_dialog.panel.right - btnW - 24, btnTop, m_dialog.panel.right - 24, btnTop + btnH);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::CloseThemedDialog()
{
    m_dialog.visible = false;
    m_dialog = DialogState{};
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::ShowWin32Error(const std::wstring& action)
{
    const DWORD err = GetLastError();
    wchar_t* msgBuf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&msgBuf), 0, nullptr);
    std::wstring text = action + L"失败";
    if (msgBuf && *msgBuf) {
        text += L"\n\n";
        text += msgBuf;
    }
    if (msgBuf) LocalFree(msgBuf);
    ShowThemedDialog(L"操作提示", text, L"确定");
}

bool App::ApplyAffinityPreset(DWORD pid, int preset)
{
    DWORD_PTR processMask = 0;
    DWORD_PTR systemMask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask) || systemMask == 0) return false;

    DWORD_PTR mask = 0;
    if (preset == 0) {
        mask = systemMask;
    } else {
        std::vector<int> cpuBits;
        for (int i = 0; i < static_cast<int>(sizeof(DWORD_PTR) * 8); ++i) {
            if (systemMask & (static_cast<DWORD_PTR>(1) << i)) cpuBits.push_back(i);
        }
        if (cpuBits.empty()) return false;
        const size_t half = (cpuBits.size() + 1) / 2;
        const size_t start = (preset == 1) ? 0 : half;
        const size_t end = (preset == 1) ? half : cpuBits.size();
        for (size_t i = start; i < end; ++i) mask |= (static_cast<DWORD_PTR>(1) << cpuBits[i]);
        if (mask == 0) mask = systemMask;
    }
    return m_processes.SetAffinityMaskByPid(pid, mask);
}

void App::OpenSelectedFileLocation()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_flatProcesses.size())) return;
    const std::wstring& path = m_flatProcesses[m_selectedIndex].process->path;
    if (path.empty()) {
        ShowThemedDialog(L"提示", L"当前进程没有可用路径。", L"确定");
        return;
    }
    std::wstring args = L"/select," + QuoteArg(path);
    ShellExecuteW(m_hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void App::RunNewTask()
{
    NewTaskDialogResult result{};
    if (!ShowNewTaskDialog(m_hwnd, result)) return;

    const bool ok = m_processes.LaunchProcess(result.text, result.runAsAdmin);
    if (!ok) ShowWin32Error(L"新建任务");
    else RefreshData(true);
}

void App::ShowProcessMenu(POINT screenPt, int index)
{
    MarkInteraction();
    if (index < 0 || index >= static_cast<int>(m_flatProcesses.size())) return;
    m_selectedIndex = index;
    const ProcessInfo& proc = *m_flatProcesses[index].process;
    InvalidateRect(m_hwnd, nullptr, FALSE);
    UpdateWindow(m_hwnd);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_OWNERDRAW, 1001, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1001)));
    AppendMenuW(menu, MF_OWNERDRAW, ID_PROCESS_KILLTREE, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(ID_PROCESS_KILLTREE)));
    AppendMenuW(menu, MF_OWNERDRAW, proc.isSuspended ? ID_PROCESS_RESUME : ID_PROCESS_SUSPEND, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(proc.isSuspended ? ID_PROCESS_RESUME : ID_PROCESS_SUSPEND)));
    AppendMenuW(menu, MF_OWNERDRAW, ID_PROCESS_DUMP, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(ID_PROCESS_DUMP)));
    AppendMenuW(menu, MF_OWNERDRAW, 1002, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1002)));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_OWNERDRAW, 1101, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1101)));
    AppendMenuW(menu, MF_OWNERDRAW, 1102, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1102)));
    AppendMenuW(menu, MF_OWNERDRAW, 1103, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1103)));
    AppendMenuW(menu, MF_OWNERDRAW, 1104, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1104)));
    AppendMenuW(menu, MF_OWNERDRAW, 1105, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1105)));
    AppendMenuW(menu, MF_OWNERDRAW, 1106, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1106)));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_OWNERDRAW, 1201, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1201)));
    AppendMenuW(menu, MF_OWNERDRAW, 1202, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1202)));
    AppendMenuW(menu, MF_OWNERDRAW, 1203, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(1203)));

    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == 0) return;

    const DWORD pid = proc.pid;
    bool ok = true;
    switch (cmd) {
        case 1001: ok = m_processes.KillProcess(pid); break;
        case ID_PROCESS_KILLTREE: ok = m_processes.KillProcessTree(pid); break;
        case ID_PROCESS_SUSPEND: ok = m_processes.SuspendProcess(pid); break;
        case ID_PROCESS_RESUME: ok = m_processes.ResumeProcess(pid); break;
        case ID_PROCESS_DUMP: {
            const std::wstring path = BuildDumpFilePath(proc);
            ok = m_processes.CreateDumpFile(pid, path);
            if (ok) {
                ShowThemedDialog(L"转储已保存", L"内存转储文件已保存到：\n\n" + path, L"完成", L"打开保存位置", path);
            }
            break;
        }
        case 1002: OpenSelectedFileLocation(); return;
        case 1101: ok = m_processes.SetPriorityClassByPid(pid, REALTIME_PRIORITY_CLASS); break;
        case 1102: ok = m_processes.SetPriorityClassByPid(pid, HIGH_PRIORITY_CLASS); break;
        case 1103: ok = m_processes.SetPriorityClassByPid(pid, ABOVE_NORMAL_PRIORITY_CLASS); break;
        case 1104: ok = m_processes.SetPriorityClassByPid(pid, NORMAL_PRIORITY_CLASS); break;
        case 1105: ok = m_processes.SetPriorityClassByPid(pid, BELOW_NORMAL_PRIORITY_CLASS); break;
        case 1106: ok = m_processes.SetPriorityClassByPid(pid, IDLE_PRIORITY_CLASS); break;
        case 1201: ok = ApplyAffinityPreset(pid, 0); break;
        case 1202: ok = ApplyAffinityPreset(pid, 1); break;
        case 1203: ok = ApplyAffinityPreset(pid, 2); break;
    }
    if (!ok) ShowWin32Error(L"操作");
    RefreshData(true);
}

void App::HandleClick(POINT pt)
{
    MarkInteraction();
    if (m_dialog.visible) {
        if (PtInRectInclusive(m_dialog.primaryButton, pt)) {
            CloseThemedDialog();
            return;
        }
        if (m_dialog.hasSecondary && PtInRectInclusive(m_dialog.secondaryButton, pt)) {
            if (!m_dialog.actionPath.empty()) {
                const std::filesystem::path p = m_dialog.actionPath;
                if (std::filesystem::exists(p) && !std::filesystem::is_directory(p)) {
                    const std::wstring args = L"/select," + QuoteArg(p.wstring());
                    ShellExecuteW(m_hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
                } else {
                    const std::filesystem::path target = std::filesystem::is_directory(p) ? p : p.parent_path();
                    ShellExecuteW(m_hwnd, L"open", target.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
            CloseThemedDialog();
            return;
        }
        if (!PtInRectInclusive(m_dialog.panel, pt)) CloseThemedDialog();
        return;
    }
    m_searchFocused = PtInRectInclusive(m_searchBox, pt);

    if (PtInRectInclusive(m_newTaskButton.rc, pt)) { RunNewTask(); return; }
    if (PtInRectInclusive(m_refreshButton.rc, pt)) { RefreshData(true); return; }

    if (m_tab == Tab::Processes) {
        for (int i = 0; i < 3; ++i) {
            if (PtInRectInclusive(m_filterButtons[i], pt)) {
                m_categoryFilter = static_cast<ProcessCategoryFilter>(i);
                m_scrollOffset = 0;
                RebuildTree();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }
    }
    if (m_tab == Tab::About) {
        if (PtInRectInclusive(m_startupToggleRect, pt)) {
            const bool next = !m_autoStartEnabled;
            if (SetAutoStartEnabled(next)) m_autoStartEnabled = next;
            else ShowWin32Error(next ? L"启用开机启动" : L"关闭开机启动");
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        for (int i = 0; i < 4; ++i) {
            if (PtInRectInclusive(m_refreshSpeedRects[i], pt)) {
                m_refreshSpeed = static_cast<RefreshSpeed>(i);
                UpdateRefreshTimer();
                SavePreferences();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }
        for (int i = 0; i < 2; ++i) {
            if (PtInRectInclusive(m_defaultPageRects[i], pt)) {
                m_defaultStartPage = static_cast<DefaultStartPage>(i);
                SavePreferences();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }
        if (PtInRectInclusive(m_topMostRect, pt)) {
            m_alwaysOnTop = !m_alwaysOnTop;
            ApplyWindowPreferences();
            SavePreferences();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (PtInRectInclusive(m_minimizeHideRect, pt)) {
            m_hideOnMinimize = !m_hideOnMinimize;
            SavePreferences();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    for (size_t i = 0; i < m_sidebarButtons.size(); ++i) {
        if (PtInRectInclusive(m_sidebarButtons[i].rc, pt)) {
            m_tab = static_cast<Tab>(i);
            UpdateLayout(m_client.right - m_client.left, m_client.bottom - m_client.top);
            if (m_tab == Tab::Processes) RefreshData(true);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    if (m_tab == Tab::Processes) {
        const int total = static_cast<int>(m_flatProcesses.size());
        const int visible = VisibleProcessRows();
        if (total > visible) {
            RECT track = MakeRect(m_tableRect.right - static_cast<int>(Theme::Dp(16.0f, static_cast<float>(m_dpi))), m_tableRowsRect.top, m_tableRect.right - static_cast<int>(Theme::Dp(6.0f, static_cast<float>(m_dpi))), m_tableRowsRect.bottom);
            if (PtInRectInclusive(track, pt)) {
                const int trackH = track.bottom - track.top;
                const int thumbH = (std::max)(static_cast<int>(trackH * (static_cast<float>(visible) / static_cast<float>(total))), static_cast<int>(Theme::Dp(46.0f, static_cast<float>(m_dpi))));
                const int maxScroll = (std::max)(1, total - visible);
                const int thumbY = track.top + (trackH - thumbH) * m_scrollOffset / maxScroll;
                if (pt.y >= thumbY && pt.y <= thumbY + thumbH) {
                    m_draggingScrollThumb = true;
                    m_scrollDragStartY = pt.y;
                    m_scrollStartOffset = m_scrollOffset;
                } else {
                    const int clickedOffset = (static_cast<int>(pt.y) - track.top) * total / (std::max)(1, trackH) - visible / 2;
                    m_scrollOffset = (std::clamp)(clickedOffset, 0, total - visible);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
                return;
            }
        }
    }

    const int row = HitTestProcessRow(pt);
    if (row >= 0) {
        m_selectedIndex = row;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

bool App::IsAutoStartEnabled() const
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    wchar_t buf[4096]{};
    DWORD type = 0;
    DWORD cb = sizeof(buf);
    const LONG rc = RegQueryValueExW(key, L"NekoTaskManager", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && buf[0] != L'\0';
}

bool App::SetAutoStartEnabled(bool enabled) const
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    bool ok = true;
    if (enabled) {
        const std::wstring exe = QuoteArg(AppDirectory() + L"\\NekoTaskManager.exe");
        ok = RegSetValueExW(key, L"NekoTaskManager", 0, REG_SZ, reinterpret_cast<const BYTE*>(exe.c_str()), static_cast<DWORD>((exe.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        const LONG rc = RegDeleteValueW(key, L"NekoTaskManager");
        ok = (rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(key);
    return ok;
}

void App::LoadPreferences()
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\NeuroSaki987\\NekoTaskManager", 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key, L"RefreshSpeed", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS && value <= 3) m_refreshSpeed = static_cast<RefreshSpeed>(value);
    size = sizeof(value);
    if (RegQueryValueExW(key, L"DefaultStartPage", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS && value <= 1) m_defaultStartPage = static_cast<DefaultStartPage>(value);
    size = sizeof(value);
    if (RegQueryValueExW(key, L"AlwaysOnTop", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS) m_alwaysOnTop = (value != 0);
    size = sizeof(value);
    if (RegQueryValueExW(key, L"HideOnMinimize", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS) m_hideOnMinimize = (value != 0);
    RegCloseKey(key);
}

void App::SavePreferences() const
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\NeuroSaki987\\NekoTaskManager", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    DWORD value = static_cast<DWORD>(m_refreshSpeed);
    RegSetValueExW(key, L"RefreshSpeed", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    value = static_cast<DWORD>(m_defaultStartPage);
    RegSetValueExW(key, L"DefaultStartPage", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    value = m_alwaysOnTop ? 1u : 0u;
    RegSetValueExW(key, L"AlwaysOnTop", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    value = m_hideOnMinimize ? 1u : 0u;
    RegSetValueExW(key, L"HideOnMinimize", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
}

void App::ApplyWindowPreferences()
{
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void App::UpdateRefreshTimer()
{
    if (!m_hwnd) return;
    KillTimer(m_hwnd, 1);
    UINT interval = 0;
    switch (m_refreshSpeed) {
        case RefreshSpeed::High: interval = 500; break;
        case RefreshSpeed::Normal: interval = 1000; break;
        case RefreshSpeed::Low: interval = 2500; break;
        case RefreshSpeed::Paused: interval = 0; break;
    }
    if (interval > 0) SetTimer(m_hwnd, 1, interval, nullptr);
}

std::wstring App::BuildDumpFilePath(const ProcessInfo& proc) const
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::wstringstream ss;
    ss << AppDirectory() << L"\\dump_" << proc.name << L"_" << proc.pid << L"_"
       << st.wYear
       << std::setw(2) << std::setfill(L'0') << st.wMonth
       << std::setw(2) << std::setfill(L'0') << st.wDay << L"_"
       << std::setw(2) << std::setfill(L'0') << st.wHour
       << std::setw(2) << std::setfill(L'0') << st.wMinute
       << std::setw(2) << std::setfill(L'0') << st.wSecond << L".dmp";
    return ss.str();
}

std::wstring App::AppDirectory() const
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().wstring();
}

bool App::IsPointInDragRegion(POINT pt) const
{
    if (!PtInRectInclusive(m_dragRegion, pt)) return false;
    if (PtInRectInclusive(m_searchBox, pt) || PtInRectInclusive(m_newTaskButton.rc, pt) || PtInRectInclusive(m_refreshButton.rc, pt)) return false;
    for (const auto& btn : m_sidebarButtons) if (PtInRectInclusive(btn.rc, pt)) return false;
    return true;
}

LRESULT App::HitTestNonClient(POINT screenPt) const
{
    RECT wnd{};
    GetWindowRect(m_hwnd, &wnd);
    const int x = screenPt.x - wnd.left;
    const int y = screenPt.y - wnd.top;
    const int w = wnd.right - wnd.left;
    const int h = wnd.bottom - wnd.top;
    const int b = m_resizeBorder;

    const bool left = x < b;
    const bool right = x >= w - b;
    const bool top = y < b;
    const bool bottom = y >= h - b;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;

    POINT clientPt = screenPt;
    ScreenToClient(m_hwnd, &clientPt);
    return IsPointInDragRegion(clientPt) ? HTCAPTION : HTCLIENT;
}


void App::AddTrayIcon()
{
    if (m_trayVisible) return;
    ZeroMemory(&m_trayIcon, sizeof(m_trayIcon));
    m_trayIcon.cbSize = sizeof(m_trayIcon);
    m_trayIcon.hWnd = m_hwnd;
    m_trayIcon.uID = 1;
    m_trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_trayIcon.uCallbackMessage = WM_APP_TRAYICON;
    m_trayIcon.hIcon = static_cast<HICON>(LoadImageW(m_instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    wcscpy_s(m_trayIcon.szTip, L"NekoTaskManager");
    Shell_NotifyIconW(NIM_ADD, &m_trayIcon);
    m_trayVisible = true;
}

void App::RemoveTrayIcon()
{
    if (!m_trayVisible) return;
    Shell_NotifyIconW(NIM_DELETE, &m_trayIcon);
    if (m_trayIcon.hIcon) DestroyIcon(m_trayIcon.hIcon);
    m_trayIcon.hIcon = nullptr;
    m_trayVisible = false;
}

void App::RestoreFromTray()
{
    ShowWindow(m_hwnd, SW_RESTORE);
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    RemoveTrayIcon();
}

void App::ShowTrayMenu(POINT screenPt)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_OWNERDRAW, ID_TRAY_OPEN, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(ID_TRAY_OPEN)));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_OWNERDRAW, ID_TRAY_EXIT, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(ID_TRAY_EXIT)));
    SetForegroundWindow(m_hwnd);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == ID_TRAY_OPEN) RestoreFromTray();
    else if (cmd == ID_TRAY_EXIT) DestroyWindow(m_hwnd);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_NCCALCSIZE:
            return 0;
        case WM_NCHITTEST: {
            const POINT screenPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            return HitTestNonClient(screenPt);
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 1040;
            mmi->ptMinTrackSize.y = 700;
            return 0;
        }
        case WM_DPICHANGED: {
            m_dpi = HIWORD(wParam);
            m_scale = static_cast<float>(m_dpi) / 96.0f;
            CreateTextFormats();
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            GetClientRect(m_hwnd, &m_client);
            UpdateLayout(m_client.right - m_client.left, m_client.bottom - m_client.top);
            ResizeSwapChain();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) {
                if (m_hideOnMinimize) {
                    AddTrayIcon();
                    ShowWindow(m_hwnd, SW_HIDE);
                }
                return 0;
            }
            GetClientRect(m_hwnd, &m_client);
            UpdateLayout(m_client.right - m_client.left, m_client.bottom - m_client.top);
            ResizeSwapChain();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_TIMER:
            RefreshData(false);
            return 0;
        case WM_MOUSEWHEEL:
            if (m_dialog.visible) return 0;
            if (m_tab == Tab::Processes) {
                MarkInteraction();
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                const int step = (std::max)(1, VisibleProcessRows() / 4);
                const int maxScroll = (std::max)(0, static_cast<int>(m_flatProcesses.size()) - VisibleProcessRows());
                m_scrollOffset = (std::clamp)(m_scrollOffset - (delta / WHEEL_DELTA) * step, 0, maxScroll);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            SetFocus(m_hwnd);
            HandleClick(pt);
            if (m_draggingScrollThumb) SetCapture(m_hwnd);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (m_draggingScrollThumb && m_tab == Tab::Processes) {
                const int total = static_cast<int>(m_flatProcesses.size());
                const int visible = VisibleProcessRows();
                const int maxScroll = (std::max)(1, total - visible);
                const int trackH = m_tableRowsRect.bottom - m_tableRowsRect.top;
                const int thumbH = (std::max)(static_cast<int>(trackH * (static_cast<float>(visible) / static_cast<float>(total))), static_cast<int>(Theme::Dp(46.0f, static_cast<float>(m_dpi))));
                const int travel = (std::max)(1, trackH - thumbH);
                const int deltaY = GET_Y_LPARAM(lParam) - m_scrollDragStartY;
                m_scrollOffset = (std::clamp)(m_scrollStartOffset + deltaY * maxScroll / travel, 0, total - visible);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (m_draggingScrollThumb) {
                m_draggingScrollThumb = false;
                ReleaseCapture();
            }
            return 0;
        case WM_RBUTTONUP: {
            if (m_dialog.visible) return 0;
            const POINT client{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            POINT screen = client;
            ClientToScreen(m_hwnd, &screen);
            const int row = HitTestProcessRow(client);
            if (row >= 0) ShowProcessMenu(screen, row);
            return 0;
        }
        case WM_APP_TRAYICON:
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                RestoreFromTray();
            } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                POINT pt{};
                GetCursorPos(&pt);
                ShowTrayMenu(pt);
            }
            return 0;
        case WM_CHAR:
        case WM_IME_CHAR:
            if (m_tab == Tab::Processes && m_searchFocused) {
                MarkInteraction();
                if (wParam == 8) {
                    if (!m_filter.empty()) m_filter.pop_back();
                } else if (wParam >= 32 && wParam != 127) {
                    m_filter.push_back(static_cast<wchar_t>(wParam));
                }
                RebuildTree();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_PASTE:
            if (m_tab == Tab::Processes && m_searchFocused && OpenClipboard(m_hwnd)) {
                HANDLE handle = GetClipboardData(CF_UNICODETEXT);
                if (handle) {
                    if (const wchar_t* textData = static_cast<const wchar_t*>(GlobalLock(handle))) {
                        m_filter += textData;
                        GlobalUnlock(handle);
                        RebuildTree();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                    }
                }
                CloseClipboard();
                return 0;
            }
            break;
        case WM_KEYDOWN:
            MarkInteraction();
            if (m_dialog.visible) {
                if (wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_SPACE) { CloseThemedDialog(); return 0; }
                return 0;
            }
            if (m_tab == Tab::Processes && m_searchFocused && (GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'A') {
                m_filter.clear();
                RebuildTree();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'N') { RunNewTask(); return 0; }
            if (wParam == VK_F5) { RefreshData(true); return 0; }
            if (wParam == VK_ESCAPE) { m_searchFocused = false; InvalidateRect(m_hwnd, nullptr, FALSE); return 0; }
            if (wParam == VK_DELETE && m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_flatProcesses.size())) {
                if (!m_processes.KillProcess(m_flatProcesses[m_selectedIndex].process->pid)) ShowWin32Error(L"结束任务");
                RefreshData(true);
                return 0;
            }
            if (m_tab == Tab::Processes) {
                if (wParam == VK_DOWN && m_selectedIndex + 1 < static_cast<int>(m_flatProcesses.size())) {
                    ++m_selectedIndex;
                    if (m_selectedIndex >= m_scrollOffset + VisibleProcessRows()) ++m_scrollOffset;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_UP && m_selectedIndex > 0) {
                    --m_selectedIndex;
                    if (m_selectedIndex < m_scrollOffset) --m_scrollOffset;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(m_hwnd, &ps);
            Draw();
            EndPaint(m_hwnd, &ps);
            return 0;
        }

        case WM_MEASUREITEM: {
            auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mi && mi->CtlType == ODT_MENU) {
                const std::wstring txt = MenuTextForId(static_cast<UINT>(mi->itemID));
                mi->itemHeight = static_cast<UINT>(Theme::Dp(40.0f, static_cast<float>(m_dpi)));
                mi->itemWidth = static_cast<UINT>(Theme::Dp((txt.size() * 20.0f) + 78.0f, static_cast<float>(m_dpi)));
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (di && di->CtlType == ODT_MENU) {
                const auto& p = Theme::Current();
                const std::wstring txt = MenuTextForId(static_cast<UINT>(di->itemID));
                RECT rcBg = di->rcItem;
                HBRUSH bg = CreateSolidBrush(RGB(248, 251, 255));
                FillRect(di->hDC, &rcBg, bg);
                DeleteObject(bg);
                RECT rcSel = di->rcItem;
                InflateRect(&rcSel, -4, -2);
                if (di->itemState & ODS_SELECTED) {
                    HBRUSH sel = CreateSolidBrush(RGB(217, 235, 255));
                    FillRect(di->hDC, &rcSel, sel);
                    DeleteObject(sel);
                    HPEN pen = CreatePen(PS_SOLID, 1, RGB(47, 128, 237));
                    HGDIOBJ oldPen = SelectObject(di->hDC, pen);
                    HGDIOBJ oldBrush = SelectObject(di->hDC, GetStockObject(HOLLOW_BRUSH));
                    RoundRect(di->hDC, rcSel.left, rcSel.top, rcSel.right, rcSel.bottom, 14, 14);
                    SelectObject(di->hDC, oldBrush);
                    SelectObject(di->hDC, oldPen);
                    DeleteObject(pen);
                }
                SetBkMode(di->hDC, TRANSPARENT);
                SetTextColor(di->hDC, (di->itemState & ODS_SELECTED) ? RGB(47, 128, 237) : RGB(8, 17, 28));
                HFONT font = CreateFontW(-static_cast<int>(Theme::Dp(16.0f, static_cast<float>(m_dpi))), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
                HGDIOBJ old = SelectObject(di->hDC, font);
                RECT rc = di->rcItem;
                rc.left += static_cast<LONG>(Theme::Dp(18.0f, static_cast<float>(m_dpi)));
                DrawTextW(di->hDC, txt.c_str(), -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
                SelectObject(di->hDC, old);
                DeleteObject(font);
                return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            KillTimer(m_hwnd, 1);
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
