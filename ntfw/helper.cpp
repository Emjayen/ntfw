/*
 * helper.cpp
 *
 */
#include "helper.h"
#include <windowsx.h>







int Edit_GetTextInt(HWND hwnd)
{
	char tmp[64];

	Edit_GetText(hwnd, tmp, sizeof(tmp));

	return atoi(tmp);
}


void Edit_SetTextInt(HWND hwnd, int v)
{
	char tmp[64];

	itoa(v, tmp, 10);
	Edit_SetText(hwnd, tmp);
}