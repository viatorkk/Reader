#ifndef __WEREAD_WEBVIEW_H__
#define __WEREAD_WEBVIEW_H__

#include <windows.h>

void OpenWereadWebView(HWND hParent);
void HideWereadWebView(HWND hParent);
void CloseWereadWebView(HWND hParent);
void ResizeWereadWebView(HWND hParent, const RECT* bounds);
void RefreshWereadWebView(HWND hParent);
void UpdateWereadWebViewBackground(HWND hParent);
BOOL IsWereadWebViewVisible(HWND hParent);
void AppendOnlineBookStoreMenuItems(HMENU hMenu, HWND hParent);
void OpenOnlineStoreWebView(HWND hParent, const wchar_t* url);
BOOL OpenCustomOnlineStoreWebView(HWND hParent, int index);
BOOL AddOnlineStoreUrl(HWND hParent);
BOOL DeleteCustomOnlineStoreUrl(HWND hParent, int index);
BOOL IsOnlineStoreDeleteCommandClick(HWND hParent, int index);
BOOL MeasureOnlineStoreMenuItem(HWND hParent, MEASUREITEMSTRUCT* measure);
BOOL DrawOnlineStoreMenuItem(HWND hParent, DRAWITEMSTRUCT* draw);
int HandleOnlineStoreMenuCommand(HWND hParent, HMENU hMenu, UINT itemPosition);

#define ONLINE_STORE_MENU_NONE          0
#define ONLINE_STORE_MENU_HANDLED       1
#define ONLINE_STORE_MENU_CHANGED       2
#define ONLINE_STORE_MENU_WEBVIEW       3
#define ONLINE_STORE_MENU_ADD_REQUEST   4

#endif
