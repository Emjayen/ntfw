/*
 * ntfw.cpp
 *
 */
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "resource.h"










INT_PTR CALLBACK DlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message)
    {
        case WM_INITDIALOG:
        {
            TCITEM tci;
            tci.mask = TCIF_TEXT;
            tci.pszText = (LPSTR) "TestTab";

            TabCtrl_InsertItem(GetDlgItem(hwnd, IDC_TAB), 0, &tci);


            return true;
        }

    }

    return FALSE;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{

    CreateDialog(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, &DlgProc);



    MSG msg;

    while(GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }




}