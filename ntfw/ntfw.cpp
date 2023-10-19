/*
 * ntfw.cpp
 *
 */
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "resource.h"
#include "ntfw.h"
#include <ntfwkm\shared.h>


#define TEST_FILE  "Z:\\test.ntfc"


// Prototypes
bool LoadNtfeConfiguration(const char* pFile);
bool SaveNtfeConfiguration(const char* pFile);



void Log(const char* pFormat, ...)
{
    char tmp[512];

    va_list va;
    va_start(va, pFormat);
    wvsprintf(tmp, pFormat, va);
    va_end(va);

    OutputDebugString(tmp);
}

#define LOG(format, ...) Log(format "\n", __VA_ARGS__)


// Globals
static HWND hwndMain;
static HKEY hkDriver;



HWND CreateToolTip(int toolID, HWND hDlg, const char* pszText)
{
    if(!toolID || !hDlg || !pszText)
    {
        return FALSE;
    }
    // Get the window of the tool.
    HWND hwndTool = GetDlgItem(hDlg, toolID);

    // Create the tooltip. g_hInst is the global instance handle.
    HWND hwndTip = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL,
        WS_POPUP |TTS_ALWAYSTIP | TTS_NOFADE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        hDlg, NULL,
        GetModuleHandle(NULL), NULL);

    if(!hwndTool || !hwndTip)
    {
        return (HWND) NULL;
    }

    // Associate the tooltip with the tool.
    TOOLINFO toolInfo = { 0 };
    toolInfo.cbSize = sizeof(toolInfo);
    toolInfo.hwnd = hDlg;
    toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    toolInfo.uId = (UINT_PTR) hwndTool;
    toolInfo.lpszText = (LPSTR) pszText;
    SendMessage(hwndTip, TTM_ADDTOOL, 0, (LPARAM) &toolInfo);

    return hwndTip;
}



INT_PTR CALLBACK DlgProcX(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message)
    {
        case WM_INITDIALOG:
        {
            HWND h = CreateToolTip(IDC_AUTHOUT, hwnd, "Authorizes remote hosts for originating private traffic.\n\nIf this option is disabled, applications on this computer will be unable to "\
                                             "communicate with unauthorized hosts (e.g, web-browsers, Windows Update, ect)");

            SendMessage(h, TTM_SETMAXTIPWIDTH, 0, 400);

            return true;
        }
    }



    return false;
}





INT_PTR CALLBACK DlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message)
    {
        case WM_INITDIALOG:
        {
            HWND hwndTab = GetDlgItem(hwnd, IDC_TAB);


            // Create all the child tabs; they're persistent and global lifetime.
            struct
            {
                const char* Name;
                int DlgResId;
                DLGPROC DlgProc;
            } const Tabs[] =
            {
                { "Basic", IDD_BASIC, DlgProcX },
                { "Users", IDD_USERS, OnUsersDlgMsg },
                { "Rules", IDD_RULES, OnRulesDlgMsg },
            };


            for(int i = 0; i < ARRAYSIZE(Tabs); i++)
            {
                RECT rcWindow;
                RECT rcDisplay;
                SIZE szDisplay;
                TC_ITEM tci;
                tci.mask = TCIF_TEXT | TCIF_PARAM;
                tci.lParam = (LPARAM) CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(Tabs[i].DlgResId), hwndTab, Tabs[i].DlgProc);
                tci.pszText = (LPSTR) Tabs[i].Name;
                TabCtrl_InsertItem(hwndTab, i, &tci);

                if(i == 0)
                {
                    // Calculate the logical client area size of the tab control and size children.
                    // We couldn't do this before because the display area would be computed incorrectly
                    // due to the lack of any tabs.
                    GetWindowRect(hwndTab, &rcWindow);
                    CopyRect(&rcDisplay, &rcWindow);
                    TabCtrl_AdjustRect(hwndTab, FALSE, &rcDisplay);
                    rcDisplay.left -= 2;
                    rcDisplay.bottom += 2;

                    szDisplay.cx = rcDisplay.right - rcDisplay.left;
                    szDisplay.cy = rcDisplay.bottom - rcDisplay.top;
                    rcDisplay.left -= rcWindow.left;
                    rcDisplay.top -= rcWindow.top;
                }

                SetWindowPos((HWND) tci.lParam, NULL, rcDisplay.left, rcDisplay.top, szDisplay.cx, szDisplay.cy, i ? NULL : SWP_SHOWWINDOW);
            }

            return true;
        }


        case WM_NOTIFY:
        {
            NMHDR* Msg = (NMHDR*) lParam;

            switch(Msg->code)
            {
                case TCN_SELCHANGING:
                {
                    TCITEM tci = { TCIF_PARAM };

                    TabCtrl_GetItem(Msg->hwndFrom, TabCtrl_GetCurSel(Msg->hwndFrom), &tci);
                    ShowWindow((HWND) tci.lParam, SW_HIDE);

                } break;

                case TCN_SELCHANGE:
                {
                    TCITEM tci = { TCIF_PARAM };

                    TabCtrl_GetItem(Msg->hwndFrom, TabCtrl_GetCurSel(Msg->hwndFrom), &tci);
                    ShowWindow((HWND) tci.lParam, SW_SHOW);

                } break;
            }
        } break;

        case WM_COMMAND:
        {
            if(wParam == IDCANCEL)
                PostQuitMessage(0);

            if(wParam == IDC_SAVE)
                SaveNtfeConfiguration(TEST_FILE);

            return TRUE;
        } break;
    }

    return FALSE;
}



bool Startup()
{
    INITCOMMONCONTROLSEX ic = { sizeof(ic), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES };

    if(!InitCommonControlsEx(&ic))
    {
        return false;
    }

    return true;
}




int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
    if(!Startup())
        return -1;


 

    hwndMain = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, &DlgProc);



    LoadNtfeConfiguration(TEST_FILE);


    HANDLE hTimer = CreateWaitableTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);


    LARGE_INTEGER Due = {};

    SetWaitableTimer(hTimer, &Due, 13, NULL, NULL, FALSE);


for(MSG msg = {}; msg.message != WM_QUIT;)
{
    if(MsgWaitForMultipleObjects(1, &hTimer, FALSE, INFINITE, QS_ALLINPUT ^ QS_PAINT) == WAIT_OBJECT_0)
    {
        while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            DispatchMessage(&msg);
        }
    }

    else
    {
        while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_QS_INPUT | PM_QS_POSTMESSAGE | PM_QS_SENDMESSAGE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}



}




bool LoadNtfeConfiguration(const char* pFile)
{
    HANDLE hFile;
    DWORD Result;
    char Path[MAX_PATH];

    union
    {
        byte data[MAX_NTFE_CFG_SZ];
        ntfe_config cfg;
    };

    GetFullPathName(pFile, sizeof(Path), Path, NULL);
    memzero(data, sizeof(data));

    if((hFile = CreateFile(Path, GENERIC_ALL, FILE_SHARE_READ, NULL, OPEN_ALWAYS, NULL, NULL)) == INVALID_HANDLE_VALUE || !ReadFile(hFile, data, sizeof(data), &Result, NULL))
    {
        NotifyUser(MB_OK | MB_ICONERROR, "File open failed.", "Unable to open configuration file: '%s'", Path);

        if(hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);

        return false;
    }

    RulesUpload(&cfg);

    CloseHandle(hFile);
}


bool SaveNtfeConfiguration(const char* pFile)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    DWORD Result;
    char Path[MAX_PATH];

    union
    {
        byte data[MAX_NTFE_CFG_SZ];
        ntfe_config cfg;
    };


    memzero(&cfg, sizeof(data));
    cfg.version = NTFE_CFG_VERSION;
    cfg.length = sizeof(cfg);

    RulesDownload(&cfg);


    // Do some basic sanity checks so the user doesn't shoot
    // themself in the foot
    if(cfg.rule_count == 0)
    {
        if(NotifyUser(MB_YESNO | MB_ICONWARNING, "Warning",
            "No rules have been defined; only authorized hosts will be able to communicate with this machine.\n\n"
            "Are you sure you wish to continue?") != IDYES) return false;
    }

    else if(!FindRuleMagic((ntfe_rule*) cfg.ect, cfg.rule_count, &cfg.rule_seed, &cfg.rule_hashsz, 5000))
    {
        NotifyUser(MB_OK | MB_ICONERROR, "Failure", "Unable to find seed for rule table (duplicate rules?)");
        return false;
    }

    // Check if ARP isn't enabled.
    ntfe_rule* rl = (ntfe_rule*) cfg.ect;

    for(uint i = 0; i < cfg.rule_count; i++)
    {
        if(rl[i].ether == ETH_P_ARP)
        {
            rl = NULL;
            break;
        }
    }

    if(rl)
    {
        if(NotifyUser(MB_YESNO | MB_ICONWARNING, "Warning",
            "A rule including the ARP protocol has not been defined. Unless you know what you're doing, it is highly recommended to allow ARP.\n\n"
            "Are you sure you wish to continue?") != IDYES) return false;
    }

    // Write to disk.
    GetFullPathName(pFile, sizeof(Path), Path, NULL);

    if((hFile = CreateFile(Path, GENERIC_WRITE, NULL, NULL, OPEN_ALWAYS, NULL, NULL)) == INVALID_HANDLE_VALUE || !WriteFile(hFile, &cfg, cfg.length, &Result, NULL) || Result != cfg.length)
    {
        NotifyUser(MB_OK | MB_ICONERROR, "File open failed.", "Unable to write configuration file: '%s'", Path);

        if(hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);

        return false;
    }

    CloseHandle(hFile);

    return true;
}




int NotifyUser(UINT MsgBoxType, const char* pTitle, const char* pFormat, ...)
{
    va_list va;
    char tmp[256];

    va_start(va, pFormat);
    wvsprintf(tmp, pFormat, va);
    va_end(va);

    return MessageBox(hwndMain, tmp, pTitle, MsgBoxType);
}