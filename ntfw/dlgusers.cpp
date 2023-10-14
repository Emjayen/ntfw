/*
 * dlgusers.cpp
 *
 */
#include "ntfw.h"
#include "resource.h"
#include <commctrl.h>









INT_PTR CALLBACK OnUsersDlgMsg(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
		case WM_INITDIALOG:
		{
			// Initialize user LV.
			struct
			{
				const char* name;
				int width;
				int fmt;
			} cols[] =
			{
				{ "Username", 70, LVCFMT_LEFT },
				{ "Password", 100, LVCFMT_LEFT },
				{ "Public Key", 100, LVCFMT_LEFT },
				{ "Last IP", 100, LVCFMT_LEFT },
				{ "Last Login", 100, LVCFMT_LEFT },
			};

			for(int i = 0; i < ARRAYSIZE(cols); i++)
			{
				LVCOLUMNA lvc;
				lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
				lvc.fmt = cols[i].fmt;
				lvc.cx = cols[i].width;
				lvc.pszText = (LPSTR) cols[i].name;

				ListView_InsertColumn(GetDlgItem(hwnd, IDC_USERLV), i, &lvc);
			}

			return true;
		}
	}

	return false;
}
