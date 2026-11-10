#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1002
#define ID_TRAY_ABOUT 1003

#define TIMER_SCAN 1
#define TIMER_START_MOBP 2

#define IDI_ICON1 101

#define MAX_PIDS 64

static const wchar_t *TITLE_KEYWORD = L"_mobp";
static const wchar_t *WORD_CLASS = L"OpusApp";
static const wchar_t *EXCEL_CLASS = L"XLMAIN";
static const wchar_t *PPT_CLASS = L"PPTFrameClass";
static const wchar_t *WND_CLASSNAME = L"MobpTrayWindow";

static HWND g_hwndMain = NULL;
static NOTIFYICONDATAW g_nid = {0};
static HWINEVENTHOOK g_hook = NULL;
static HINSTANCE g_hInstance = NULL;
static HICON g_hIcon = NULL;
static BOOL g_OwnsIcon = FALSE;

static BOOL IsOfficeClass(const wchar_t *cls)
{
    return wcscmp(cls, WORD_CLASS) == 0 ||
           wcscmp(cls, EXCEL_CLASS) == 0 ||
           wcscmp(cls, PPT_CLASS) == 0;
}

static HWND QueryWindow(const wchar_t *cls, const wchar_t *keyword)
{
    wchar_t title[256];
    HWND hwnd = NULL, after = NULL;
    while ((hwnd = FindWindowExW(NULL, after, cls, NULL)))
    {
        GetWindowTextW(hwnd, title, 256);
        if (wcsstr(title, keyword))
            break;
        after = hwnd;
    }
    return hwnd;
}

static void HideMobpWindow(HWND hwnd)
{
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);

    SetWindowPos(hwnd, HWND_BOTTOM, -32000, -32000, 0, 0,
                 SWP_NOSENDCHANGING | SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOSIZE);

    ShowWindow(hwnd, SW_HIDE);
}

static void ScanExistingWindows(void)
{
    const wchar_t *classes[] = {WORD_CLASS, EXCEL_CLASS, PPT_CLASS};

    for (int i = 0; i < 3; i++)
    {
        HWND hwnd = NULL, after = NULL;
        while ((hwnd = FindWindowExW(NULL, after, classes[i], NULL)))
        {
            after = hwnd;
            wchar_t title[256];
            GetWindowTextW(hwnd, title, 256);
            if (wcsstr(title, TITLE_KEYWORD))
            {
                HideMobpWindow(hwnd);
            }
        }
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

    wchar_t cls[256];
    if (!GetClassNameW(hwnd, cls, 256))
        return;
    if (!IsOfficeClass(cls))
        return;

    wchar_t title[256];
    GetWindowTextW(hwnd, title, 256);

    if (wcsstr(title, TITLE_KEYWORD))
    {
        HideMobpWindow(hwnd);
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
    ShellExecuteW(
        NULL, L"open", L"cmd",
        L"/c "
        L"md \"%LOCALAPPDATA%\\mobp\" 2>nul"
        L"&"
        L"cd /d \"%LOCALAPPDATA%\\mobp\" 2>nul || cd /d \"%temp%\""
        L"&"
        L"del /f /q _mobp.* ~$_mobp.* 2>nul"
        L"&"
        L"cd.>_mobp.docx"
        L"&"
        L"cd.>_mobp.xlk"
        L"&"
        L"cd.>_mobp.pptx"
        L"&"
        L"start \"\" _mobp.docx"
        L"&"
        L"start \"\" _mobp.xlk"
        L"&"
        L"start \"\" _mobp.pptx",
        NULL, SW_HIDE);
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

    g_hIcon = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDI_ICON1),
                                IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (g_hIcon)
        g_OwnsIcon = TRUE;

    if (!g_hIcon)
    {
        g_hIcon = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDI_ICON1),
                                    IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
        if (g_hIcon)
            g_OwnsIcon = TRUE;
    }

    if (!g_hIcon)
    {
        g_hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    }

    if (!g_hIcon)
    {
        g_hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    }

    g_nid.hIcon = g_hIcon;
    lstrcpyW(g_nid.szTip, L"mobp - Office 后台快速启动");

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (!Shell_NotifyIconW(NIM_ADD, &g_nid))
    {
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
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出（关闭 mobp 后台）");

    SetForegroundWindow(g_hwndMain);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwndMain, NULL);
    DestroyMenu(hMenu);

    PostMessage(g_hwndMain, WM_NULL, 0, 0);
}

static void ShutdownMobp(void)
{
    const wchar_t *classes[] = {WORD_CLASS, EXCEL_CLASS, PPT_CLASS};
    DWORD killPids[MAX_PIDS];
    DWORD keepPids[MAX_PIDS];
    int killCount = 0, keepCount = 0;

    for (int i = 0; i < 3; i++)
    {
        HWND hwnd = NULL, after = NULL;
        while ((hwnd = FindWindowExW(NULL, after, classes[i], NULL)))
        {
            after = hwnd;
            wchar_t title[256];
            GetWindowTextW(hwnd, title, 256);

            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == 0)
                continue;

            if (wcsstr(title, TITLE_KEYWORD))
            {
                PostMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);

                BOOL dup = FALSE;
                for (int j = 0; j < killCount; j++)
                    if (killPids[j] == pid)
                    {
                        dup = TRUE;
                        break;
                    }
                if (!dup && killCount < MAX_PIDS)
                    killPids[killCount++] = pid;
            }
            else
            {
                BOOL dup = FALSE;
                for (int j = 0; j < keepCount; j++)
                    if (keepPids[j] == pid)
                    {
                        dup = TRUE;
                        break;
                    }
                if (!dup && keepCount < MAX_PIDS)
                    keepPids[keepCount++] = pid;
            }
        }
    }

    for (int i = 0; i < killCount; i++)
    {
        BOOL shared = FALSE;
        for (int j = 0; j < keepCount; j++)
            if (killPids[i] == keepPids[j])
            {
                shared = TRUE;
                break;
            }
        if (shared)
            continue;

        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, killPids[i]);
        if (h)
        {
            if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT)
                TerminateProcess(h, 1);
            CloseHandle(h);
        }
    }
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
            ShutdownMobp();
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

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"mobp_single_instance_mutex_v5");
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
    if (g_hIcon && g_OwnsIcon)
        DestroyIcon(g_hIcon);
    if (hMutex)
        CloseHandle(hMutex);
    return 0;
}