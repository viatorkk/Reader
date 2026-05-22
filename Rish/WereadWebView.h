#ifndef __WEREAD_WEBVIEW_H__
#define __WEREAD_WEBVIEW_H__

#include <windows.h>

void OpenWereadWebView(HWND hParent);
void HideWereadWebView(HWND hParent);
void CloseWereadWebView(HWND hParent);
void ResizeWereadWebView(HWND hParent, const RECT* bounds);
void RefreshWereadWebView(HWND hParent);
BOOL IsWereadWebViewVisible(HWND hParent);
void AppendWereadMenuItems(HMENU hMenu, HWND hParent);

#endif
