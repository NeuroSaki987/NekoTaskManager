#pragma once
#include "Common.h"
#include "Theme.h"
#include "FontManager.h"
#include "ProcessManager.h"
#include "SystemMetrics.h"

class App {
public:
    App();
    ~App();
    bool Initialize(HINSTANCE instance, int cmdShow);
    int Run();

private:
    enum class Tab { Processes, Performance, About };
    enum class ProcessCategoryFilter { All, Apps, Background };
    enum class RefreshSpeed { High, Normal, Low, Paused };
    enum class DefaultStartPage { Processes, Performance };

    struct Button {
        RECT rc{};
        std::wstring text;
        bool active = false;
    };

    struct DialogState {
        bool visible = false;
        std::wstring title;
        std::wstring message;
        std::wstring primaryText;
        std::wstring secondaryText;
        std::wstring actionPath;
        RECT panel{};
        RECT primaryButton{};
        RECT secondaryButton{};
        bool hasSecondary = false;
    };

    bool CreateMainWindow(HINSTANCE instance, int cmdShow);
    bool CreateGraphicsResources();
    void DiscardGraphicsResources();
    void ResizeSwapChain();
    void EnableBackdropEffect();
    void UpdateLayout(int width, int height);
    void RefreshData(bool forceProcessRefresh = true);
    void RebuildTree();
    void HandleClick(POINT pt);
    void ShowProcessMenu(POINT screenPt, int index);
    int HitTestProcessRow(POINT pt) const;
    int HitTestTreeToggle(POINT pt) const;
    void Draw();
    void DrawBackground();
    void DrawSidebar();
    void DrawHeader();
    void DrawProcessesView();
    void DrawPerformanceView();
    void DrawAboutView();
    void DrawRoundedPanel(const D2D1_RECT_F& rc, D2D1_COLOR_F fill, D2D1_COLOR_F border, float radius = 18.0f, float stroke = 1.0f);
    void DrawTextLine(const std::wstring& text, const D2D1_RECT_F& rc, IDWriteTextFormat* format, D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT paragraph = DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    void DrawMetricCard(float x, float y, float w, float h, const std::wstring& title, const std::wstring& value, const std::wstring& subtitle, D2D1_COLOR_F accent);
    void DrawProgressBar(float x, float y, float w, float h, float ratio, D2D1_COLOR_F color);
    void DrawLineChart(float x, float y, float w, float h, const std::deque<double>& values, D2D1_COLOR_F stroke, const std::wstring& label, const std::wstring& valueText);
    void DrawScrollbar();
    void DrawDialogOverlay();
    void DrawProcessGlyph(float left, float top, float size, const ProcessInfo& proc, bool selected);
    bool LoadProcessIconBitmap(const ProcessInfo& proc, ID2D1Bitmap** outBitmap);
    ID2D1Bitmap* GetProcessIconBitmap(const ProcessInfo& proc);
    void ClearProcessIconCache();
    bool EnsureWicFactory();
    bool LoadBitmapFromResource(int resourceId, ID2D1Bitmap** outBitmap);
    bool LoadBackgroundBitmap();
    bool LoadAboutBitmap();
    bool CreateTextFormats();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void RestoreFromTray();
    void ShowTrayMenu(POINT screenPt);
    bool IsAutoStartEnabled() const;
    bool SetAutoStartEnabled(bool enabled) const;
    void LoadPreferences();
    void SavePreferences() const;
    void ApplyWindowPreferences();
    void UpdateRefreshTimer();
    std::wstring BuildDumpFilePath(const ProcessInfo& proc) const;
    std::wstring AppDirectory() const;
    std::wstring DumpDirectory() const;
    int VisibleProcessRows() const;
    void RunNewTask();
    void OpenSelectedFileLocation();
    bool ApplyAffinityPreset(DWORD pid, int preset);
    void ShowWin32Error(const std::wstring& action);
    void ShowThemedDialog(const std::wstring& title, const std::wstring& message, const std::wstring& primaryText = L"确定", const std::wstring& secondaryText = L"", const std::wstring& actionPath = L"");
    void CloseThemedDialog();
    void MarkInteraction();
    bool IsPointInDragRegion(POINT pt) const;
    LRESULT HitTestNonClient(POINT screenPt) const;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    HINSTANCE m_instance = nullptr;
    UINT m_dpi = 96;
    float m_scale = 1.0f;
    RECT m_client{};
    Tab m_tab = Tab::Processes;
    std::wstring m_filter;
    std::vector<FlatProcessNode> m_flatProcesses;
    std::set<DWORD> m_expanded;
    int m_selectedIndex = -1;
    int m_scrollOffset = 0;
    int m_rowHeight = 36;
    int m_headerHeight = 88;
    int m_sidebarWidth = 220;
    int m_contentTop = 0;
    int m_resizeBorder = 8;
    std::vector<Button> m_sidebarButtons;
    Button m_newTaskButton{};
    Button m_refreshButton{};
    RECT m_dragRegion{};
    RECT m_searchBox{};
    RECT m_contentRect{};
    RECT m_tableRect{};
    RECT m_tableHeaderRect{};
    RECT m_tableRowsRect{};
    RECT m_footerHintRect{};
    RECT m_filterButtons[3]{};
    RECT m_startupToggleRect{};
    RECT m_refreshSpeedRects[4]{};
    RECT m_defaultPageRects[2]{};
    RECT m_topMostRect{};
    RECT m_minimizeHideRect{};

    FontManager m_fonts;
    ProcessManager m_processes;
    SystemMetrics m_metrics;

    ULONGLONG m_lastProcessRefreshTick = 0;
    ULONGLONG m_lastMetricsTick = 0;
    ULONGLONG m_lastInteractionTick = 0;
    bool m_searchFocused = false;
    bool m_autoStartEnabled = false;
    bool m_alwaysOnTop = false;
    bool m_hideOnMinimize = true;
    ProcessCategoryFilter m_categoryFilter = ProcessCategoryFilter::All;
    RefreshSpeed m_refreshSpeed = RefreshSpeed::Normal;
    DefaultStartPage m_defaultStartPage = DefaultStartPage::Processes;

    ID2D1Factory1* m_d2dFactory = nullptr;
    ID2D1Device* m_d2dDevice = nullptr;
    ID2D1DeviceContext* m_target = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    ID2D1SolidColorBrush* m_brush = nullptr;
    IDWriteTextFormat* m_titleFormat = nullptr;
    IDWriteTextFormat* m_normalFormat = nullptr;
    IDWriteTextFormat* m_smallFormat = nullptr;
    IDWriteTextFormat* m_monoFormat = nullptr;
    IDWriteTextFormat* m_pixelValueFormat = nullptr;
    IDWriteTextFormat* m_pixelChartFormat = nullptr;
    ID2D1Bitmap* m_aboutBitmap = nullptr;
    DialogState m_dialog{};
    std::map<std::wstring, ID2D1Bitmap*> m_processIconCache;
    bool m_trayVisible = false;
    bool m_draggingScrollThumb = false;
    int m_scrollDragStartY = 0;
    int m_scrollStartOffset = 0;
    NOTIFYICONDATAW m_trayIcon{};
    IWICImagingFactory* m_wicFactory = nullptr;
    ID2D1Bitmap* m_backgroundBitmap = nullptr;
    ID2D1Bitmap1* m_targetBitmap = nullptr;
    IDXGISwapChain1* m_swapChain = nullptr;
    ID3D11Device* m_d3dDevice = nullptr;
    ID3D11DeviceContext* m_d3dContext = nullptr;
};
