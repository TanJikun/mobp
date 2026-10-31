#include <windows.h>
#include <shellapi.h>
#include <string.h>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1002
#define ID_TRAY_ABOUT 1003

#define TIMER_SCAN 1
#define TIMER_START_MOBP 2

#define IDI_ICON1 101
#define IDI_ICON2 1

static const char *TITLE_KEYWORD = "_mobp";
static const char *WORD_CLASS = "OpusApp";
static const char *EXCEL_CLASS = "XLMAIN";
static const char *PPT_CLASS = "PPTFrameClass";
static const wchar_t *WND_CLASSNAME = L"MobpTrayWindow";

static HWND g_hwndMain = NULL;
static NOTIFYICONDATAW g_nid = {0};
static HWINEVENTHOOK g_hook = NULL;
static BOOL g_mobpRunning = FALSE;
static HINSTANCE g_hInstance = NULL;
static HICON g_hIcon = NULL;

static BOOL IsOfficeClass(const char *cls)
{
    return strcmp(cls, WORD_CLASS) == 0 ||
           strcmp(cls, EXCEL_CLASS) == 0 ||
           strcmp(cls, PPT_CLASS) == 0;
}

static HWND QueryWindow(const char *cls, const char *keyword)
{
    char title[256];
    HWND hwnd = NULL, after = NULL;
    while ((hwnd = FindWindowExA(NULL, after, cls, NULL)))
    {
        GetWindowTextA(hwnd, title, sizeof(title));
        if (strstr(title, keyword))
            break;
        after = hwnd;
    }
    return hwnd;
}

static void CloseWindowByQuery(const char *cls, const char *keyword)
{
    HWND hwnd = QueryWindow(cls, keyword);
    if (hwnd)
    {
        PostMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
    }
}

static void HideMobpWindow(HWND hwnd)
{
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);

    SetWindowPos(hwnd, HWND_BOTTOM, -32000, -32000, 0, 0,
                 SWP_NOSENDCHANGING | SWP_NOACTIVATE | SWP_HIDEWINDOW);

    ShowWindow(hwnd, SW_HIDE);
}

static void ScanExistingWindows(void)
{
    const char *classes[] = {WORD_CLASS, EXCEL_CLASS, PPT_CLASS};
    BOOL found_any = FALSE;

    for (int i = 0; i < 3; i++)
    {
        HWND hwnd = NULL, after = NULL;
        while ((hwnd = FindWindowExA(NULL, after, classes[i], NULL)))
        {
            after = hwnd;
            char title[256];
            GetWindowTextA(hwnd, title, sizeof(title));
            if (strstr(title, TITLE_KEYWORD))
            {
                HideMobpWindow(hwnd);
                found_any = TRUE;
            }
        }
    }

    if (found_any)
    {
        g_mobpRunning = TRUE;
    }
}

static void CALLBACK WinEventProc(HWINEVENTHOOK hHook, DWORD event, HWND hwnd,
                                  LONG idObject, LONG idChild,
                                  DWORD dwThread, DWORD dwTime)
{
    if (event != EVENT_OBJECT_SHOW)
        return;
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
        return;
    if (GetParent(hwnd) != NULL)
        return;

    char cls[256];
    if (!GetClassNameA(hwnd, cls, sizeof(cls)))
        return;
    if (!IsOfficeClass(cls))
        return;

    char title[256];
    GetWindowTextA(hwnd, title, sizeof(title));

    if (strstr(title, TITLE_KEYWORD))
    {
        HideMobpWindow(hwnd);
        g_mobpRunning = TRUE;
    }
    else
    {
        if (!IsZoomed(hwnd))
        {
            ShowWindow(hwnd, SW_MAXIMIZE);
        }
    }
}

static void StartMobp(void)
{
    ShellExecuteA(
        NULL, NULL, "cmd",
        "/c "
        "cd /d %temp%"
        "&"
        "cd.>_mobp.docx"
        "&"
        "cd.>_mobp.xlk"
        "&"
        "cd.>_mobp.pptx"
        "&"
        "start \"\" _mobp.docx"
        "&"
        "start \"\" _mobp.xlk"
        "&"
        "start \"\" _mobp.pptx",
        NULL, SW_HIDE);
    g_mobpRunning = TRUE;
}

static void AddTrayIcon(void)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hwndMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;

    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);

    // 1. 尝试提取指定尺寸
    g_hIcon = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);

    // 2. 尝试提取默认尺寸
    if (!g_hIcon)
    {
        g_hIcon = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    }

    // 3. 尝试 LoadIconW
    if (!g_hIcon)
    {
        g_hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    }
    if (!g_hIcon)
    {
        g_hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_ICON2));
    }

    // 4. 系统兜底
    if (!g_hIcon)
    {
        g_hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    }

    g_nid.hIcon = g_hIcon;
    lstrcpyW(g_nid.szTip, L"mobp - Office 后台快速启动");

    // 强制清理可能残留的旧图标，然后再添加
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (!Shell_NotifyIconW(NIM_ADD, &g_nid))
    {
        // 如果失败，稍等 100ms 再试一次
        Sleep(100);
        Shell_NotifyIconW(NIM_ADD, &g_nid);
    }
}

static void ShowTrayMenu(void)
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();

    AppendMenuW(hMenu, MF_STRING, ID_TRAY_ABOUT, L"关于");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出（并关闭Office）");

    SetForegroundWindow(g_hwndMain);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwndMain, NULL);
    DestroyMenu(hMenu);

    PostMessage(g_hwndMain, WM_NULL, 0, 0);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP)
        {
            ShowTrayMenu();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_ABOUT:
        {
            int ret = MessageBoxW(hwnd,
                                  L"mobp - Office 后台快速启动\n\n"
                                  L"项目地址:\n"
                                  L"https://github.com/TanJikun/mobp\n\n"
                                  L"如果这个工具帮到了你，\n"
                                  L"记得去 GitHub 点个 Star 支持一下！\n\n"
                                  L"是否现在打开项目页面？",
                                  L"关于 mobp",
                                  MB_YESNO | MB_ICONINFORMATION);
            if (ret == IDYES)
            {
                ShellExecuteW(NULL, L"open", L"https://github.com/TanJikun/mobp",
                              NULL, NULL, SW_SHOWNORMAL);
            }
            return 0;
        }

        case ID_TRAY_EXIT:
            // 1. 先发送优雅关闭
            CloseWindowByQuery(WORD_CLASS, TITLE_KEYWORD);
            CloseWindowByQuery(EXCEL_CLASS, TITLE_KEYWORD);
            CloseWindowByQuery(PPT_CLASS, TITLE_KEYWORD);

            // 2. 强制杀掉残留的 Office 后台进程（防止插件或 COM 服务让进程假死）
            WinExec("taskkill /F /IM WINWORD.EXE /IM EXCEL.EXE /IM POWERPNT.EXE /T >nul 2>&1", SW_HIDE);

            DestroyWindow(hwnd);
            return 0;
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_SCAN)
        {
            ScanExistingWindows();
        }
        else if (wParam == TIMER_START_MOBP)
        {
            KillTimer(hwnd, TIMER_START_MOBP);
            StartMobp();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_SCAN);
        KillTimer(hwnd, TIMER_START_MOBP);

        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow)
{
    g_hInstance = hInstance;

    // 【重要】修改了 Mutex 名字，防止被旧版本的幽灵进程拦住无法启动
    HANDLE hMutex = CreateMutex(NULL, TRUE, "mobp_single_instance_mutex_v4");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (hMutex)
            CloseHandle(hMutex);
        return 0;
    }

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WND_CLASSNAME;
    RegisterClassW(&wc);

    g_hwndMain = CreateWindowW(WND_CLASSNAME, L"mobp", 0,
                               0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    AddTrayIcon();

    g_hook = SetWinEventHook(
        EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
        NULL, WinEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    SetTimer(g_hwndMain, TIMER_SCAN, 200, NULL);

    if (QueryWindow(WORD_CLASS, TITLE_KEYWORD))
    {
        g_mobpRunning = TRUE;
    }
    else
    {
        ULONGLONG uptime = GetTickCount64();
        UINT wait_ms = 1000;
        if (uptime < 120000)
            wait_ms = 30000;
        else if (uptime < 300000)
            wait_ms = 5000;

        SetTimer(g_hwndMain, TIMER_START_MOBP, wait_ms, NULL);
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_hook)
        UnhookWinEvent(g_hook);
    if (g_hIcon)
        DestroyIcon(g_hIcon);
    if (hMutex)
        CloseHandle(hMutex);
    return 0;
}