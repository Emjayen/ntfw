/*
 * ntfw.cpp
 *
 */
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "resource.h"
#include "ntfw.h"





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



HWND hwndMain;


INT_PTR CALLBACK DlgProcX(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message)
    {
        case WM_INITDIALOG:
        {


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
                //{ "Status", IDD_STATUS, DlgProcX },
                //{ "General", IDD_BASIC, DlgProcX},
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

                SetWindowPos((HWND) tci.lParam, NULL, rcDisplay.left, rcDisplay.top, szDisplay.cx, szDisplay.cy, NULL);
            }

            ShowWindow(GetDlgItem(hwnd, Tabs[0].DlgResId), SW_SHOW);


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
        } break;
    }

    return FALSE;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{



    hwndMain = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, &DlgProc);


    HANDLE hTimer = CreateWaitableTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);


    LARGE_INTEGER Due = {};

    SetWaitableTimer(hTimer, &Due, 13, NULL, NULL, FALSE);


    for(MSG msg = {}; msg.message != WM_QUIT;)
    {
        if(MsgWaitForMultipleObjects(1, &hTimer, FALSE, INFINITE, QS_ALLINPUT ^ QS_PAINT) == WAIT_OBJECT_0)
        {
            u64 qpcT;

            // ((qpcT = QPC()) - qpcNextCompose) > 1000 && 
            // 
            // Paint accumulated dirty window regions, ensuring not to exceed the deadline
            // for composition which may cause tearing. The precision here isn't great, being
            // that of a window's WM_PAINT and a pretty arbitrary estimation of maximum time.
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




void NotifyUser(UINT MsgBoxType, const char* pTitle, const char* pFormat, ...)
{
    va_list va;
    char tmp[256];

    va_start(va, pFormat);
    wvsprintf(tmp, pFormat, va);
    va_end(va);

    MessageBox(hwndMain, tmp, pTitle, MsgBoxType);
}