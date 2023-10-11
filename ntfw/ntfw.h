/*
 * ntfw.h
 *
 */
#pragma once
#include <ntfe\ntfeusr.h>
#include <windows.h>






INT_PTR CALLBACK OnRulesDlgMsg(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);



void Log(const char* pFormat, ...);

void NotifyUser(UINT MsgBoxType, const char* pTitle, const char* pFormat, ...);