/*
 * ntfw.h
 *
 */
#pragma once
#include <ntfe\ntfeusr.h>
#include <windows.h>






INT_PTR CALLBACK OnRulesDlgMsg(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK OnBasicDlgMsg(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK OnUsersDlgMsg(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam);

bool RulesDownload(ntfe_config* pCfg);
void RulesUpload(ntfe_config* pCfg);

bool FindRuleMagic(ntfe_rule* pRules, u32 RuleNum, u32* pSeed, u32* pHashSz, u32 MaximumTimeMsec);

void Log(const char* pFormat, ...);

int NotifyUser(UINT MsgBoxType, const char* pTitle, const char* pFormat, ...);