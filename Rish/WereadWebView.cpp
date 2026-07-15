#include "WereadWebView.h"
#include "DPIAwareness.h"
#include "resource.h"
#include "types.h"

#include <WebView2.h>
#include <commctrl.h>
#include <cwctype>
#include <shlobj.h>
#include <strsafe.h>
#include <wrl.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "WebView2LoaderStatic.lib")

extern BOOL DispatchWereadHostShortcut(HWND hParent, WPARAM key);
extern BOOL IsWereadDragStripEnabled(HWND hParent);
extern HINSTANCE hInst;
extern header_t* _header;
extern void Save(HWND);

namespace
{
const wchar_t kWereadUrl[] = L"https://weread.qq.com/";
const wchar_t kDragStripClass[] = L"RishWereadDragStrip";
const int kBorderlessResizeMargin = 12;
const int kBorderlessTopReserve = 32;
const int kDragStripHeight = kBorderlessTopReserve - kBorderlessResizeMargin;
const BYTE kDragHandleDefaultAlpha = 20;
const BYTE kDragHandleHoverAlpha = 80;
const BYTE kDragHandlePressedAlpha = 145;
const BYTE kDragHandleDarkDefaultAlpha = 64;
const BYTE kDragHandleDarkHoverAlpha = 96;
const BYTE kDragHandleDarkPressedAlpha = 160;
const BYTE kNavControlsDefaultAlpha = 64;
const BYTE kNavControlsHoverAlpha = 112;
const BYTE kNavControlsPressedAlpha = 150;
const BYTE kNavControlsDarkDefaultAlpha = 92;
const BYTE kNavControlsDarkHoverAlpha = 132;
const BYTE kNavControlsDarkPressedAlpha = 170;
const COLORREF kDragStripColorKey = RGB(1, 0, 1);
const int kNavButtonSize = 24;
const int kNavButtonGap = 4;
const int kNavButtonRightPadding = 12;
const int kNavButtonCornerRadius = 6;
const int kNavArrowIconWidth = 14;
const int kNavArrowIconHeight = 12;
const int kNavArrowHeadWidth = 7;
const int kNavArrowShaftHeight = 5;
const int kNavRefreshIconFontSize = 15;
const wchar_t kNavBackTooltip[] = L"\u540e\u9000\u7f51\u9875";
const wchar_t kNavForwardTooltip[] = L"\u524d\u8fdb\u7f51\u9875";
const wchar_t kNavRefreshTooltip[] = L"\u5237\u65b0\u7f51\u9875";
const wchar_t kDragStripTooltipProp[] = L"RishDragStripTooltip";
const wchar_t kDragStripThemeDarkMessage[] = L"rish-drag-strip-theme:dark";
const wchar_t kDragStripThemeLightMessage[] = L"rish-drag-strip-theme:light";
const wchar_t kOnlineBookStoreTitle[] = L"\u5728\u7ebf\u4e66\u57ce";
const wchar_t kWebViewStateFileName[] = L".webview.dat";
const wchar_t kWereadMenuTitle[] = L"\u5fae\u4fe1\u9605\u8bfb(&W)";
const wchar_t kAddUrlMenuTitle[] = L"\u6dfb\u52a0\u7f51\u5740...";
const wchar_t kDeleteIcon[] = L"\u00d7";
const wchar_t kRightClickDeleteHint[] = L"\u53f3\u952e\u5220\u9664";
const wchar_t kRemoveTransparentBackgroundScript[] = LR"JS(
(() => {
    const style = document.getElementById('rish-webview-transparent-bg');
    if (style) {
        style.remove();
    }
})();
)JS";
const wchar_t kHideScrollbarsScript[] = LR"JS(
(() => {
    const id = 'rish-webview-hide-scrollbars';
    let style = document.getElementById(id);
    if (!style) {
        style = document.createElement('style');
        style.id = id;
        const root = document.head || document.documentElement;
        if (!root) {
            return;
        }
        root.appendChild(style);
    }
    style.textContent = 'html, body { scrollbar-width: none !important; -ms-overflow-style: none !important; } ::-webkit-scrollbar { width: 0 !important; height: 0 !important; display: none !important; }';
})();
)JS";
const wchar_t kRemoveHideScrollbarsScript[] = LR"JS(
(() => {
    const style = document.getElementById('rish-webview-hide-scrollbars');
    if (style) {
        style.remove();
    }
})();
)JS";
const wchar_t kDetectDragStripThemeScript[] = LR"JS(
(() => {
    const observerKey = '__rishDragStripThemeObserver';
    const timerKey = '__rishDragStripThemeTimer';
    const messageKey = '__rishDragStripThemeMessage';

    const parseColor = value => {
        const match = value && value.match(/rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)(?:\s*,\s*([\d.]+))?\s*\)/i);
        if (!match || (match[4] !== undefined && Number(match[4]) <= 0.05)) {
            return null;
        }
        return [Number(match[1]), Number(match[2]), Number(match[3])];
    };

    const findBackground = () => {
        const x = Math.max(0, Math.min(window.innerWidth - 1, Math.floor(window.innerWidth / 2)));
        const y = Math.max(0, Math.min(window.innerHeight - 1, 8));
        let element = document.elementFromPoint(x, y) || document.body || document.documentElement;
        while (element) {
            const color = parseColor(getComputedStyle(element).backgroundColor);
            if (color) {
                return color;
            }
            element = element.parentElement;
        }
        const textColor = parseColor(getComputedStyle(document.body || document.documentElement).color);
        if (textColor) {
            const lightText = textColor[0] * 299 + textColor[1] * 587 + textColor[2] * 114 >= 128000;
            return lightText ? [0, 0, 0] : [255, 255, 255];
        }
        return window.matchMedia('(prefers-color-scheme: dark)').matches ? [0, 0, 0] : [255, 255, 255];
    };

    const report = () => {
        const color = findBackground();
        if (!color || !window.chrome || !window.chrome.webview) {
            return;
        }
        const dark = color[0] * 299 + color[1] * 587 + color[2] * 114 < 128000;
        const message = `rish-drag-strip-theme:${dark ? 'dark' : 'light'}`;
        if (window[messageKey] !== message) {
            window[messageKey] = message;
            window.chrome.webview.postMessage(message);
        }
    };

    const scheduleReport = () => {
        clearTimeout(window[timerKey]);
        window[timerKey] = setTimeout(report, 40);
    };

    if (window[observerKey]) {
        window[observerKey].observer.disconnect();
        window.removeEventListener('resize', window[observerKey].scheduleReport);
        if (window[observerKey].media.removeEventListener) {
            window[observerKey].media.removeEventListener('change', window[observerKey].scheduleReport);
        } else {
            window[observerKey].media.removeListener(window[observerKey].scheduleReport);
        }
    }
    const observer = new MutationObserver(scheduleReport);
    observer.observe(document.documentElement, {
        attributes: true,
        attributeFilter: ['class', 'style'],
        childList: true,
        subtree: true
    });
    const media = window.matchMedia('(prefers-color-scheme: dark)');
    window[observerKey] = { observer, scheduleReport, media };
    window.addEventListener('resize', scheduleReport);
    if (media.addEventListener) {
        media.addEventListener('change', scheduleReport);
    } else {
        media.addListener(scheduleReport);
    }
    report();
    setTimeout(report, 250);
    setTimeout(report, 1000);
})();
)JS";
const int kOnlineStoreDeleteZoneWidth = 42;
const int kOnlineStoreMenuPaddingX = 12;

struct OnlineStoreDialogData
{
    wchar_t name[MAX_ONLINE_STORE_NAME];
    wchar_t url[MAX_ONLINE_STORE_URL];
};

struct WebViewSessionState
{
    DWORD magic;
    DWORD version;
    BOOL reopen;
    wchar_t url[MAX_ONLINE_STORE_URL];
    double zoomFactor;
};

class EmbeddedWereadView;
EmbeddedWereadView* GetView(HWND hParent, bool create);
static UINT64 g_nextViewGeneration = 0;

enum DragStripCommand
{
    DragStripCommandNone = 0,
    DragStripCommandBack = 1,
    DragStripCommandForward = 2,
    DragStripCommandRefresh = 3
};

BOOL IsDragStripNavigationCommandEnabled(HWND hWnd, DragStripCommand command);
void ExecuteDragStripNavigationCommand(HWND hWnd, DragStripCommand command);

RECT g_deleteRects[MAX_ONLINE_STORE_COUNT] = {0};
BOOL g_deleteRectValid[MAX_ONLINE_STORE_COUNT] = {0};
HMENU g_onlineStoreMenu = NULL;
int g_onlineStoreMenuPositions[MAX_ONLINE_STORE_COUNT] = {0};
const DWORD kWebViewStateMagic = 0x57565253;
const DWORD kWebViewStateVersion = 3;
const double kDefaultZoomFactor = 1.0;
const double kMinZoomFactor = 0.25;
const double kMaxZoomFactor = 5.0;

double NormalizeZoomFactor(double zoomFactor)
{
    if (zoomFactor < kMinZoomFactor || zoomFactor > kMaxZoomFactor)
        return kDefaultZoomFactor;
    return zoomFactor;
}

bool BuildPersistedUrl(const wchar_t* url, wchar_t* buffer, size_t cchBuffer);

bool IsWereadUrl(const wchar_t* url)
{
    const wchar_t* hosts[] = {
        L"http://weread.qq.com",
        L"https://weread.qq.com"
    };

    if (!url)
        return false;

    for (int i = 0; i < ARRAYSIZE(hosts); i++)
    {
        size_t length = wcslen(hosts[i]);
        wchar_t next;

        if (_wcsnicmp(url, hosts[i], length) != 0)
            continue;

        next = url[length];
        if (next == L'\0' || next == L'/' || next == L'?' || next == L'#')
            return true;
    }

    return false;
}


bool GetWebViewStateFilePath(wchar_t* buffer, size_t cchBuffer)
{
    size_t len;

    if (!buffer || cchBuffer == 0)
        return false;

    if (!GetModuleFileNameW(NULL, buffer, (DWORD)cchBuffer))
        return false;

    len = wcslen(buffer);
    while (len > 0 && buffer[len - 1] != L'\\' && buffer[len - 1] != L'/')
        buffer[--len] = L'\0';

    return SUCCEEDED(StringCchCatW(buffer, cchBuffer, kWebViewStateFileName));
}

void SaveWebViewSessionState(BOOL reopen, const wchar_t* url, double zoomFactor)
{
    wchar_t fileName[MAX_PATH] = {0};
    wchar_t persistedUrl[MAX_ONLINE_STORE_URL] = {0};
    WebViewSessionState state = {0};
    HANDLE hFile;
    DWORD bytesWritten = 0;

    if (!url || !url[0] || !GetWebViewStateFilePath(fileName, ARRAYSIZE(fileName)))
        return;

    if (!BuildPersistedUrl(url, persistedUrl, ARRAYSIZE(persistedUrl)))
    {
        // Do not leave a previously stored URL behind when the current page is not safe to persist.
        DeleteFileW(fileName);
        return;
    }

    state.magic = kWebViewStateMagic;
    state.version = kWebViewStateVersion;
    state.reopen = reopen;
    StringCchCopyW(state.url, ARRAYSIZE(state.url), persistedUrl);
    state.zoomFactor = NormalizeZoomFactor(zoomFactor);

    hFile = CreateFileW(fileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    WriteFile(hFile, &state, sizeof(state), &bytesWritten, NULL);
    CloseHandle(hFile);
}

bool LoadWebViewSessionState(WebViewSessionState* state)
{
    wchar_t fileName[MAX_PATH] = {0};
    wchar_t persistedUrl[MAX_ONLINE_STORE_URL] = {0};
    HANDLE hFile;
    DWORD bytesRead = 0;

    if (!state || !GetWebViewStateFilePath(fileName, ARRAYSIZE(fileName)))
        return false;

    hFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_HIDDEN, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    ZeroMemory(state, sizeof(*state));
    if (!ReadFile(hFile, state, sizeof(*state), &bytesRead, NULL))
    {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    DWORD minBytesRead = sizeof(DWORD) * 2 + sizeof(BOOL) + sizeof(state->url);
    if (bytesRead < minBytesRead
        || state->magic != kWebViewStateMagic
        || state->version < 1
        || state->version > kWebViewStateVersion
        || !state->url[0])
    {
        return false;
    }

    if (state->version < 2 || bytesRead < sizeof(*state))
        state->zoomFactor = kDefaultZoomFactor;
    else
        state->zoomFactor = NormalizeZoomFactor(state->zoomFactor);

    // Migrate legacy states in memory: retain only an HTTPS origin and never restore query/fragment data.
    if (!BuildPersistedUrl(state->url, persistedUrl, ARRAYSIZE(persistedUrl)))
        return false;
    StringCchCopyW(state->url, ARRAYSIZE(state->url), persistedUrl);

    return true;
}

void TrimText(wchar_t* text)
{
    wchar_t* start = text;
    size_t len;

    if (!text)
        return;

    while (*start && iswspace(*start))
        start++;
    if (start != text)
        memmove(text, start, (wcslen(start) + 1) * sizeof(wchar_t));

    len = wcslen(text);
    while (len > 0 && iswspace(text[len - 1]))
        text[--len] = L'\0';
}

bool NormalizeUrl(wchar_t* url, size_t cchUrl)
{
    UNREFERENCED_PARAMETER(cchUrl);

    TrimText(url);
    if (!url[0])
        return false;

    if (_wcsnicmp(url, L"http://", 7) == 0 || _wcsnicmp(url, L"https://", 8) == 0)
        return true;

    if (wcsstr(url, L"://"))
        return false;

    return true;
}

bool HasHttpScheme(const wchar_t* url)
{
    return url && (_wcsnicmp(url, L"http://", 7) == 0 || _wcsnicmp(url, L"https://", 8) == 0);
}

bool IsHttpsUrl(const wchar_t* url)
{
    return url && _wcsnicmp(url, L"https://", 8) == 0;
}

void CopyTrimmedUrl(const wchar_t* url, wchar_t* buffer, size_t cchBuffer)
{
    StringCchCopyW(buffer, cchBuffer, url ? url : L"");
    TrimText(buffer);
}

void BuildUrlCompareKey(const wchar_t* url, wchar_t* key, size_t cchKey)
{
    wchar_t temp[MAX_ONLINE_STORE_URL] = {0};
    size_t len;
    const wchar_t* start;

    CopyTrimmedUrl(url, temp, ARRAYSIZE(temp));
    start = temp;
    if (_wcsnicmp(start, L"http://", 7) == 0)
        start += 7;
    else if (_wcsnicmp(start, L"https://", 8) == 0)
        start += 8;

    StringCchCopyW(key, cchKey, start);
    len = wcslen(key);
    while (len > 0 && key[len - 1] == L'/')
        key[--len] = L'\0';
}

bool IsSameOnlineStoreUrl(const wchar_t* left, const wchar_t* right)
{
    wchar_t leftTrimmed[MAX_ONLINE_STORE_URL] = {0};
    wchar_t rightTrimmed[MAX_ONLINE_STORE_URL] = {0};
    wchar_t leftKey[MAX_ONLINE_STORE_URL] = {0};
    wchar_t rightKey[MAX_ONLINE_STORE_URL] = {0};

    CopyTrimmedUrl(left, leftTrimmed, ARRAYSIZE(leftTrimmed));
    CopyTrimmedUrl(right, rightTrimmed, ARRAYSIZE(rightTrimmed));
    if (_wcsicmp(leftTrimmed, rightTrimmed) == 0)
        return true;

    if (!HasHttpScheme(leftTrimmed) || !HasHttpScheme(rightTrimmed))
    {
        BuildUrlCompareKey(leftTrimmed, leftKey, ARRAYSIZE(leftKey));
        BuildUrlCompareKey(rightTrimmed, rightKey, ARRAYSIZE(rightKey));
        return _wcsicmp(leftKey, rightKey) == 0;
    }

    return false;
}

bool BuildNavigableUrl(const wchar_t* url, wchar_t* buffer, size_t cchBuffer)
{
    wchar_t trimmed[MAX_ONLINE_STORE_URL] = {0};

    CopyTrimmedUrl(url, trimmed, ARRAYSIZE(trimmed));
    if (!trimmed[0])
        return false;

    if (HasHttpScheme(trimmed))
        return SUCCEEDED(StringCchCopyW(buffer, cchBuffer, trimmed));

    return SUCCEEDED(StringCchPrintfW(buffer, cchBuffer, L"https://%s", trimmed));
}

bool BuildPersistedUrl(const wchar_t* url, wchar_t* buffer, size_t cchBuffer)
{
    wchar_t trimmed[MAX_ONLINE_STORE_URL] = {0};
    const wchar_t* host;
    const wchar_t* end;
    const wchar_t* character;
    size_t hostLength;

    if (!buffer || cchBuffer == 0)
        return false;

    buffer[0] = L'\0';
    CopyTrimmedUrl(url, trimmed, ARRAYSIZE(trimmed));
    if (!HasHttpScheme(trimmed))
        return false;

    // Legacy state may contain http URLs. Keep the saved state compatible while
    // always restoring it as HTTPS; explicit HTTP navigation is never inferred.
    host = trimmed + (IsHttpsUrl(trimmed) ? 8 : 7);
    end = host;
    while (*end && *end != L'/' && *end != L'\\' && *end != L'?' && *end != L'#')
        end++;
    hostLength = (size_t)(end - host);

    // Persist only an HTTPS origin. Query, fragment, path and user-info may contain sensitive data.
    if (hostLength == 0)
        return false;

    for (character = host; character < end; character++)
    {
        if (*character == L'@' || *character <= 0x1F || *character == 0x7F)
            return false;
    }

    return SUCCEEDED(StringCchPrintfW(buffer, cchBuffer, L"https://%.*s/", (int)hostLength, host));
}

bool IsDuplicateOnlineStoreUrl(const wchar_t* url)
{
    if (!url || !url[0])
        return false;
    if (IsSameOnlineStoreUrl(url, kWereadUrl))
        return true;

    if (!_header)
        return false;

    for (int i = 0; i < _header->online_store_count; i++)
    {
        if (IsSameOnlineStoreUrl(url, _header->online_stores[i].url))
            return true;
    }
    return false;
}

bool IsOnlineStoreCommand(UINT id)
{
    return id >= IDM_ONLINE_STORE_FIRST && id < IDM_ONLINE_STORE_FIRST + MAX_ONLINE_STORE_COUNT;
}

int FindMenuItemPosition(HMENU hMenu, UINT id)
{
    int count;

    if (!hMenu)
        return -1;

    count = GetMenuItemCount(hMenu);
    for (int i = 0; i < count; i++)
    {
        if (GetMenuItemID(hMenu, i) == id)
            return i;
    }
    return -1;
}

void ResetOnlineStoreMenuClickState(HMENU hMenu)
{
    ZeroMemory(g_deleteRects, sizeof(g_deleteRects));
    ZeroMemory(g_deleteRectValid, sizeof(g_deleteRectValid));
    g_onlineStoreMenu = hMenu;
    for (int i = 0; i < MAX_ONLINE_STORE_COUNT; i++)
        g_onlineStoreMenuPositions[i] = -1;
}

void RememberOnlineStoreMenuPosition(int index, HMENU hMenu)
{
    if (index < 0 || index >= MAX_ONLINE_STORE_COUNT)
        return;

    g_onlineStoreMenu = hMenu;
    g_onlineStoreMenuPositions[index] = GetMenuItemCount(hMenu) - 1;
}

BOOL GetOnlineStoreMenuItemRect(HWND hParent, int index, RECT* rc)
{
    int position;

    if (!rc || index < 0 || index >= MAX_ONLINE_STORE_COUNT || !g_onlineStoreMenu)
        return FALSE;

    position = g_onlineStoreMenuPositions[index];
    if (position < 0)
        return FALSE;

    return GetMenuItemRect(hParent, g_onlineStoreMenu, position, rc)
        || GetMenuItemRect(NULL, g_onlineStoreMenu, position, rc);
}

const wchar_t* GetOnlineStoreDisplayName(int index)
{
    if (!_header || index < 0 || index >= _header->online_store_count)
        return L"";
    return _header->online_stores[index].name[0] ? _header->online_stores[index].name : _header->online_stores[index].url;
}

void CacheDeleteZoneRect(HWND hParent, HMENU hMenu, UINT id, const RECT* fallbackRect)
{
    RECT rc;
    int itemPosition;
    int index;

    if (!IsOnlineStoreCommand(id))
        return;

    index = id - IDM_ONLINE_STORE_FIRST;
    if (index < 0 || index >= MAX_ONLINE_STORE_COUNT)
        return;

    itemPosition = FindMenuItemPosition(hMenu, id);
    if (itemPosition >= 0
        && (GetMenuItemRect(hParent, hMenu, itemPosition, &rc) || GetMenuItemRect(NULL, hMenu, itemPosition, &rc)))
    {
        rc.left = rc.right - kOnlineStoreDeleteZoneWidth;
        g_deleteRects[index] = rc;
        g_deleteRectValid[index] = TRUE;
        return;
    }

    if (fallbackRect)
    {
        g_deleteRects[index] = *fallbackRect;
        g_deleteRectValid[index] = TRUE;
    }
}

BOOL IsPointInCachedDeleteZone(int index, POINT pt)
{
    if (index < 0 || index >= MAX_ONLINE_STORE_COUNT || !g_deleteRectValid[index])
        return FALSE;
    return PtInRect(&g_deleteRects[index], pt);
}

void GetMenuFont(LOGFONTW* lf)
{
    NONCLIENTMETRICSW ncm = {0};

    if (!lf)
        return;

    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    {
        *lf = ncm.lfMenuFont;
        return;
    }

    ZeroMemory(lf, sizeof(LOGFONTW));
    lf->lfHeight = -12;
    StringCchCopyW(lf->lfFaceName, ARRAYSIZE(lf->lfFaceName), L"Segoe UI");
}

BOOL IsPointInDeleteZone(HWND hParent, HMENU hMenu, UINT itemPosition, UINT id, POINT pt)
{
    RECT rc;
    int index;

    if (GetMenuItemRect(hParent, hMenu, itemPosition, &rc) || GetMenuItemRect(NULL, hMenu, itemPosition, &rc))
        return PtInRect(&rc, pt) && pt.x >= rc.right - kOnlineStoreDeleteZoneWidth;

    if (IsOnlineStoreCommand(id))
    {
        index = id - IDM_ONLINE_STORE_FIRST;
        if (IsPointInCachedDeleteZone(index, pt))
            return TRUE;
    }

    return FALSE;
}

BOOL IsDeleteZoneClick(HWND hParent, HMENU hMenu, UINT itemPosition, UINT id)
{
    DWORD messagePos;
    POINT pt;

    messagePos = GetMessagePos();
    pt.x = (int)(short)LOWORD(messagePos);
    pt.y = (int)(short)HIWORD(messagePos);
    if (IsPointInDeleteZone(hParent, hMenu, itemPosition, id, pt))
        return TRUE;

    GetCursorPos(&pt);
    return IsPointInDeleteZone(hParent, hMenu, itemPosition, id, pt);
}

BOOL IsOnlineStoreDeleteCommandClickInternal(HWND hParent, int index)
{
    DWORD messagePos;
    POINT pt;
    RECT rc;

    messagePos = GetMessagePos();
    pt.x = (int)(short)LOWORD(messagePos);
    pt.y = (int)(short)HIWORD(messagePos);
    if (GetOnlineStoreMenuItemRect(hParent, index, &rc))
    {
        rc.left = rc.right - kOnlineStoreDeleteZoneWidth;
        if (PtInRect(&rc, pt))
            return TRUE;
    }
    if (IsPointInCachedDeleteZone(index, pt))
        return TRUE;

    GetCursorPos(&pt);
    if (GetOnlineStoreMenuItemRect(hParent, index, &rc))
    {
        rc.left = rc.right - kOnlineStoreDeleteZoneWidth;
        if (PtInRect(&rc, pt))
            return TRUE;
    }
    return IsPointInCachedDeleteZone(index, pt);
}

INT_PTR CALLBACK AddOnlineStoreUrlDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    OnlineStoreDialogData* data;

    switch (message)
    {
    case WM_INITDIALOG:
        SetWindowLongPtr(hDlg, DWLP_USER, lParam);
        SetDlgItemTextW(hDlg, IDC_EDIT_ONLINE_STORE_NAME, L"");
        SetDlgItemTextW(hDlg, IDC_EDIT_ONLINE_STORE_URL, L"");
        SetFocus(GetDlgItem(hDlg, IDC_EDIT_ONLINE_STORE_NAME));
        return (INT_PTR)FALSE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            data = (OnlineStoreDialogData*)GetWindowLongPtr(hDlg, DWLP_USER);
            if (!data)
                return (INT_PTR)FALSE;
            GetDlgItemTextW(hDlg, IDC_EDIT_ONLINE_STORE_NAME, data->name, MAX_ONLINE_STORE_NAME);
            GetDlgItemTextW(hDlg, IDC_EDIT_ONLINE_STORE_URL, data->url, MAX_ONLINE_STORE_URL);
            TrimText(data->name);
            if (!NormalizeUrl(data->url, MAX_ONLINE_STORE_URL))
            {
                MessageBoxW(hDlg, L"\u8bf7\u8f93\u5165\u6709\u6548\u7684\u7f51\u5740\u3002", L"Rish", MB_OK | MB_ICONERROR);
                return (INT_PTR)TRUE;
            }
            if (!data->name[0])
                StringCchCopyW(data->name, ARRAYSIZE(data->name), data->url);
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        default:
            break;
        }
        break;
    default:
        break;
    }

    return (INT_PTR)FALSE;
}

void BeginParentDrag(HWND hWnd)
{
    POINT pt;
    HWND hParent = GetParent(hWnd);
    if (!hParent)
        return;

    GetCursorPos(&pt);
    ReleaseCapture();
    SendMessage(hParent, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
}

BOOL IsDragStripParentBorderless(HWND hWnd)
{
    HWND hParent = GetParent(hWnd);
    LONG_PTR style;

    if (!hParent || !IsWereadDragStripEnabled(hParent))
        return FALSE;

    style = GetWindowLongPtr(hParent, GWL_STYLE);
    return (style & WS_CAPTION) == 0;
}

enum DragStripState
{
    DragStripHovered = 0x01,
    DragStripPressed = 0x02,
    DragStripTracking = 0x04,
    DragStripDarkBackground = 0x08,
    DragStripCanGoBack = 0x10,
    DragStripCanGoForward = 0x20,
    DragStripCanRefresh = 0x40
};

const UINT kDragStripHoverCommandMask = 0x00000F00;
const UINT kDragStripPressedCommandMask = 0x0000F000;
const int kDragStripHoverCommandShift = 8;
const int kDragStripPressedCommandShift = 12;
const UINT kDragStripNavigationMask = DragStripCanGoBack | DragStripCanGoForward | DragStripCanRefresh;
const UINT kDragStripTransientStateMask = DragStripHovered | DragStripPressed | DragStripTracking | kDragStripHoverCommandMask | kDragStripPressedCommandMask;

struct DragStripLayout
{
    RECT client;
    RECT buttons[4];
    BOOL showButtons;
};

UINT GetDragStripState(HWND hWnd)
{
    return (UINT)GetWindowLongPtr(hWnd, GWLP_USERDATA);
}

void SetDragStripState(HWND hWnd, UINT state);

DragStripCommand GetDragStripCommandFromState(UINT state, UINT mask, int shift)
{
    return (DragStripCommand)((state & mask) >> shift);
}

UINT SetDragStripCommandInState(UINT state, UINT mask, int shift, DragStripCommand command)
{
    state &= ~mask;
    state |= ((UINT)command << shift) & mask;
    return state;
}

BOOL IsDragStripCommandEnabledInState(UINT state, DragStripCommand command)
{
    switch (command)
    {
    case DragStripCommandBack:
        return (state & DragStripCanGoBack) != 0;
    case DragStripCommandForward:
        return (state & DragStripCanGoForward) != 0;
    case DragStripCommandRefresh:
        return (state & DragStripCanRefresh) != 0;
    default:
        return FALSE;
    }
}

BYTE GetDragStripAlpha(UINT state)
{
    bool darkBackground = (state & DragStripDarkBackground) != 0;
    bool hasNavigation = (state & kDragStripNavigationMask) != 0;

    if ((state & DragStripPressed)
        || GetDragStripCommandFromState(state, kDragStripPressedCommandMask, kDragStripPressedCommandShift) != DragStripCommandNone)
    {
        if (hasNavigation)
            return darkBackground ? kNavControlsDarkPressedAlpha : kNavControlsPressedAlpha;
        return darkBackground ? kDragHandleDarkPressedAlpha : kDragHandlePressedAlpha;
    }
    if ((state & DragStripHovered)
        || GetDragStripCommandFromState(state, kDragStripHoverCommandMask, kDragStripHoverCommandShift) != DragStripCommandNone)
    {
        if (hasNavigation)
            return darkBackground ? kNavControlsDarkHoverAlpha : kNavControlsHoverAlpha;
        return darkBackground ? kDragHandleDarkHoverAlpha : kDragHandleHoverAlpha;
    }
    if (hasNavigation)
        return darkBackground ? kNavControlsDarkDefaultAlpha : kNavControlsDefaultAlpha;
    return darkBackground ? kDragHandleDarkDefaultAlpha : kDragHandleDefaultAlpha;
}

void SetDragStripState(HWND hWnd, UINT state)
{
    SetWindowLongPtr(hWnd, GWLP_USERDATA, state);
    SetLayeredWindowAttributes(
        hWnd,
        kDragStripColorKey,
        GetDragStripAlpha(state),
        LWA_COLORKEY | LWA_ALPHA);
    InvalidateRect(hWnd, NULL, FALSE);
}

const wchar_t* GetDragStripCommandTooltip(DragStripCommand command)
{
    switch (command)
    {
    case DragStripCommandBack:
        return kNavBackTooltip;
    case DragStripCommandForward:
        return kNavForwardTooltip;
    case DragStripCommandRefresh:
        return kNavRefreshTooltip;
    default:
        return L"";
    }
}

void DrawFilledArrowIcon(HDC hdc, const RECT& button, COLORREF color, bool forward)
{
    int buttonWidth = button.right - button.left;
    int buttonHeight = button.bottom - button.top;
    int iconWidth = GetWidthForDpi(kNavArrowIconWidth);
    int iconHeight = GetHeightForDpi(kNavArrowIconHeight);
    int headWidth = GetWidthForDpi(kNavArrowHeadWidth);
    int shaftHeight = GetHeightForDpi(kNavArrowShaftHeight);
    int maxWidth = buttonWidth - GetWidthForDpi(6);
    int maxHeight = buttonHeight - GetHeightForDpi(6);
    int xLeft;
    int xRight;
    int yTop;
    int yBottom;
    int shaftTop;
    int shaftBottom;
    int centerX;
    int centerY;
    POINT points[7];
    HBRUSH brush;
    HGDIOBJ oldBrush;
    HGDIOBJ oldPen;

    if (maxWidth < 4 || maxHeight < 4)
        return;

    if (iconWidth > maxWidth)
        iconWidth = maxWidth;
    if (iconHeight > maxHeight)
        iconHeight = maxHeight;
    if (headWidth > iconWidth - 2)
        headWidth = iconWidth - 2;
    if (shaftHeight < 2)
        shaftHeight = 2;
    if (shaftHeight > iconHeight - 2)
        shaftHeight = iconHeight - 2;

    centerX = (button.left + button.right) / 2;
    centerY = (button.top + button.bottom) / 2;
    xLeft = centerX - iconWidth / 2;
    xRight = xLeft + iconWidth;
    yTop = centerY - iconHeight / 2;
    yBottom = yTop + iconHeight;
    shaftTop = centerY - shaftHeight / 2;
    shaftBottom = shaftTop + shaftHeight;

    if (forward)
    {
        points[0] = {xRight, centerY};
        points[1] = {xRight - headWidth, yTop};
        points[2] = {xRight - headWidth, shaftTop};
        points[3] = {xLeft, shaftTop};
        points[4] = {xLeft, shaftBottom};
        points[5] = {xRight - headWidth, shaftBottom};
        points[6] = {xRight - headWidth, yBottom};
    }
    else
    {
        points[0] = {xLeft, centerY};
        points[1] = {xLeft + headWidth, yTop};
        points[2] = {xLeft + headWidth, shaftTop};
        points[3] = {xRight, shaftTop};
        points[4] = {xRight, shaftBottom};
        points[5] = {xLeft + headWidth, shaftBottom};
        points[6] = {xLeft + headWidth, yBottom};
    }

    brush = CreateSolidBrush(color);
    oldBrush = SelectObject(hdc, brush);
    oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Polygon(hdc, points, ARRAYSIZE(points));
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(brush);
}

void DrawRefreshIcon(HDC hdc, const RECT& button, COLORREF color)
{
    HFONT iconFont;
    HFONT oldFont;
    LOGFONTW logFont = {0};
    RECT iconRect = button;

    logFont.lfHeight = -GetHeightForDpi(kNavRefreshIconFontSize);
    logFont.lfWeight = FW_NORMAL;
    StringCchCopyW(logFont.lfFaceName, ARRAYSIZE(logFont.lfFaceName), L"Segoe MDL2 Assets");
    iconFont = CreateFontIndirectW(&logFont);
    oldFont = iconFont ? (HFONT)SelectObject(hdc, iconFont) : NULL;

    SetTextColor(hdc, color);
    DrawTextW(hdc, L"\xE72C", -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (oldFont)
        SelectObject(hdc, oldFont);
    if (iconFont)
        DeleteObject(iconFont);
}

void DrawDragStripNavigationIcon(HDC hdc, const RECT& button, DragStripCommand command, COLORREF color)
{
    switch (command)
    {
    case DragStripCommandBack:
        DrawFilledArrowIcon(hdc, button, color, false);
        break;
    case DragStripCommandForward:
        DrawFilledArrowIcon(hdc, button, color, true);
        break;
    case DragStripCommandRefresh:
        DrawRefreshIcon(hdc, button, color);
        break;
    default:
        break;
    }
}

void CalculateDragStripLayout(HWND hWnd, UINT state, DragStripLayout* layout)
{
    int width;
    int height;
    int buttonSize;
    int buttonGap;
    int rightPadding;
    int buttonGroupWidth;
    int x;
    int y;

    UNREFERENCED_PARAMETER(state);

    if (!layout)
        return;

    ZeroMemory(layout, sizeof(*layout));
    if (!GetClientRect(hWnd, &layout->client))
        return;

    width = layout->client.right - layout->client.left;
    height = layout->client.bottom - layout->client.top;
    if (width <= 0 || height <= 0)
        return;

    buttonSize = GetWidthForDpi(kNavButtonSize);
    if (buttonSize > height)
        buttonSize = height;
    if (buttonSize < GetWidthForDpi(16))
        buttonSize = min(GetWidthForDpi(16), height);

    buttonGap = GetWidthForDpi(kNavButtonGap);
    rightPadding = GetWidthForDpi(kNavButtonRightPadding);
    buttonGroupWidth = buttonSize * 3 + buttonGap * 2;

    layout->showButtons = buttonGroupWidth + rightPadding <= width;
    if (!layout->showButtons)
        return;

    x = width - rightPadding - buttonGroupWidth;
    y = (height - buttonSize) / 2;
    if (y < 0)
        y = 0;

    for (int i = DragStripCommandBack; i <= DragStripCommandRefresh; i++)
    {
        layout->buttons[i].left = x;
        layout->buttons[i].top = y;
        layout->buttons[i].right = x + buttonSize;
        layout->buttons[i].bottom = y + buttonSize;
        x += buttonSize + buttonGap;
    }
}

DragStripCommand HitTestDragStripCommand(HWND hWnd, POINT pt)
{
    DragStripLayout layout;
    CalculateDragStripLayout(hWnd, GetDragStripState(hWnd), &layout);
    if (!layout.showButtons)
        return DragStripCommandNone;

    for (int i = DragStripCommandBack; i <= DragStripCommandRefresh; i++)
    {
        if (PtInRect(&layout.buttons[i], pt))
            return (DragStripCommand)i;
    }
    return DragStripCommandNone;
}

HWND GetDragStripTooltip(HWND hWnd)
{
    return (HWND)GetPropW(hWnd, kDragStripTooltipProp);
}

void AddDragStripTooltipTool(HWND hWnd, HWND hTooltip, DragStripCommand command)
{
    TOOLINFOW tool = {0};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_SUBCLASS;
    tool.hwnd = hWnd;
    tool.uId = (UINT_PTR)command;
    tool.lpszText = (LPWSTR)GetDragStripCommandTooltip(command);
    SetRectEmpty(&tool.rect);
    SendMessageW(hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&tool);
}

void UpdateDragStripTooltipRects(HWND hWnd)
{
    HWND hTooltip = GetDragStripTooltip(hWnd);
    DragStripLayout layout;

    if (!hTooltip)
        return;

    CalculateDragStripLayout(hWnd, GetDragStripState(hWnd), &layout);
    for (int i = DragStripCommandBack; i <= DragStripCommandRefresh; i++)
    {
        TOOLINFOW tool = {0};
        tool.cbSize = sizeof(tool);
        tool.hwnd = hWnd;
        tool.uId = (UINT_PTR)i;
        if (layout.showButtons)
            tool.rect = layout.buttons[i];
        else
            SetRectEmpty(&tool.rect);
        SendMessageW(hTooltip, TTM_NEWTOOLRECTW, 0, (LPARAM)&tool);
    }
}

void EnsureDragStripTooltip(HWND hWnd)
{
    HWND hTooltip = GetDragStripTooltip(hWnd);
    if (hTooltip && IsWindow(hTooltip))
        return;

    hTooltip = CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        hWnd,
        NULL,
        GetModuleHandle(NULL),
        NULL);
    if (!hTooltip)
        return;

    SetPropW(hWnd, kDragStripTooltipProp, hTooltip);
    SendMessageW(hTooltip, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELPARAM(500, 0));
    AddDragStripTooltipTool(hWnd, hTooltip, DragStripCommandBack);
    AddDragStripTooltipTool(hWnd, hTooltip, DragStripCommandForward);
    AddDragStripTooltipTool(hWnd, hTooltip, DragStripCommandRefresh);
    UpdateDragStripTooltipRects(hWnd);
}

void DestroyDragStripTooltip(HWND hWnd)
{
    HWND hTooltip = GetDragStripTooltip(hWnd);
    if (hTooltip && IsWindow(hTooltip))
        DestroyWindow(hTooltip);
    RemovePropW(hWnd, kDragStripTooltipProp);
}

void RestoreDragStripStateAfterDrag(HWND hWnd)
{
    POINT pt;
    RECT rc;
    UINT state = GetDragStripState(hWnd)
        & ~(DragStripHovered | DragStripPressed | DragStripTracking | kDragStripHoverCommandMask | kDragStripPressedCommandMask);

    if (GetCursorPos(&pt) && ScreenToClient(hWnd, &pt) && GetClientRect(hWnd, &rc) && PtInRect(&rc, pt))
    {
        TRACKMOUSEEVENT tracking = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0};
        TrackMouseEvent(&tracking);
        state |= DragStripHovered | DragStripTracking;
    }

    SetDragStripState(hWnd, state);
}

void ToggleParentMaximized(HWND hWnd)
{
    HWND hParent = GetParent(hWnd);
    if (!hParent)
        return;

    SendMessage(hParent, WM_SYSCOMMAND, IsZoomed(hParent) ? SC_RESTORE : SC_MAXIMIZE, 0);
}

void PaintDragStrip(HWND hWnd, HDC hdc)
{
    DragStripLayout layout;
    HBRUSH backgroundBrush;
    HBRUSH hoverBrush = NULL;
    HGDIOBJ oldBrush;
    HGDIOBJ oldPen;
    UINT state = GetDragStripState(hWnd);
    DragStripCommand hoveredCommand = GetDragStripCommandFromState(state, kDragStripHoverCommandMask, kDragStripHoverCommandShift);
    COLORREF buttonColor = (state & DragStripDarkBackground) ? RGB(224, 226, 230) : RGB(24, 28, 36);
    COLORREF disabledColor = (state & DragStripDarkBackground) ? RGB(150, 152, 156) : RGB(118, 122, 128);
    COLORREF hoverFillColor = (state & DragStripDarkBackground) ? RGB(255, 255, 255) : RGB(0, 0, 0);
    int radius;

    backgroundBrush = CreateSolidBrush(kDragStripColorKey);
    GetClientRect(hWnd, &layout.client);
    FillRect(hdc, &layout.client, backgroundBrush);
    DeleteObject(backgroundBrush);

    if (buttonColor == kDragStripColorKey)
        buttonColor ^= 1;
    if (disabledColor == kDragStripColorKey)
        disabledColor ^= 1;
    if (hoverFillColor == kDragStripColorKey)
        hoverFillColor ^= 1;

    CalculateDragStripLayout(hWnd, state, &layout);
    oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    SetBkMode(hdc, TRANSPARENT);

    if (layout.showButtons)
    {
        hoverBrush = CreateSolidBrush(hoverFillColor);
        for (int i = DragStripCommandBack; i <= DragStripCommandRefresh; i++)
        {
            DragStripCommand command = (DragStripCommand)i;
            BOOL enabled = IsDragStripCommandEnabledInState(state, command);
            RECT button = layout.buttons[i];

            if (enabled && hoveredCommand == command && hoverBrush)
            {
                radius = GetWidthForDpi(kNavButtonCornerRadius);
                oldBrush = SelectObject(hdc, hoverBrush);
                RoundRect(hdc, button.left, button.top, button.right, button.bottom, radius, radius);
                SelectObject(hdc, oldBrush);
            }

            DrawDragStripNavigationIcon(hdc, button, command, enabled ? buttonColor : disabledColor);
        }
        if (hoverBrush)
            DeleteObject(hoverBrush);
    }

    SelectObject(hdc, oldPen);
}

LRESULT CALLBACK DragStripProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UINT state;
    POINT pt;
    DragStripCommand command;
    DragStripCommand pressedCommand;

    switch (message)
    {
    case WM_NCHITTEST:
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        ScreenToClient(hWnd, &pt);
        if (HitTestDragStripCommand(hWnd, pt) != DragStripCommandNone || IsDragStripParentBorderless(hWnd))
            return HTCLIENT;
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        GetCursorPos(&pt);
        ScreenToClient(hWnd, &pt);
        SetCursor(LoadCursor(NULL, HitTestDragStripCommand(hWnd, pt) == DragStripCommandNone && IsDragStripParentBorderless(hWnd) ? IDC_SIZEALL : IDC_ARROW));
        return TRUE;
    case WM_MOUSEMOVE:
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        command = HitTestDragStripCommand(hWnd, pt);
        SetCursor(LoadCursor(NULL, command == DragStripCommandNone && IsDragStripParentBorderless(hWnd) ? IDC_SIZEALL : IDC_ARROW));
        state = GetDragStripState(hWnd);
        if (!(state & DragStripTracking))
        {
            TRACKMOUSEEVENT tracking = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0};
            TrackMouseEvent(&tracking);
            state |= DragStripTracking;
        }
        state &= ~(DragStripHovered | kDragStripHoverCommandMask);
        if (command != DragStripCommandNone && IsDragStripNavigationCommandEnabled(hWnd, command))
            state = SetDragStripCommandInState(state, kDragStripHoverCommandMask, kDragStripHoverCommandShift, command);
        else if (command == DragStripCommandNone)
            state |= DragStripHovered;
        SetDragStripState(hWnd, state);
        return 0;
    case WM_MOUSELEAVE:
        state = GetDragStripState(hWnd);
        SetDragStripState(hWnd, state & ~(DragStripHovered | DragStripTracking | kDragStripHoverCommandMask));
        return 0;
    case WM_LBUTTONDOWN:
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        command = HitTestDragStripCommand(hWnd, pt);
        if (command != DragStripCommandNone)
        {
            if (IsDragStripNavigationCommandEnabled(hWnd, command))
            {
                state = GetDragStripState(hWnd);
                state = SetDragStripCommandInState(state, kDragStripPressedCommandMask, kDragStripPressedCommandShift, command);
                state = SetDragStripCommandInState(state, kDragStripHoverCommandMask, kDragStripHoverCommandShift, command);
                SetCapture(hWnd);
                SetDragStripState(hWnd, state);
                UpdateWindow(hWnd);
            }
            return 0;
        }

        if (!IsDragStripParentBorderless(hWnd))
            return 0;

        state = GetDragStripState(hWnd) | DragStripHovered | DragStripPressed;
        state = SetDragStripCommandInState(state, kDragStripPressedCommandMask, kDragStripPressedCommandShift, DragStripCommandNone);
        state = SetDragStripCommandInState(state, kDragStripHoverCommandMask, kDragStripHoverCommandShift, DragStripCommandNone);
        SetDragStripState(hWnd, state);
        UpdateWindow(hWnd);
        BeginParentDrag(hWnd);
        RestoreDragStripStateAfterDrag(hWnd);
        return 0;
    case WM_LBUTTONUP:
        state = GetDragStripState(hWnd);
        pressedCommand = GetDragStripCommandFromState(state, kDragStripPressedCommandMask, kDragStripPressedCommandShift);
        if (pressedCommand != DragStripCommandNone)
        {
            pt.x = (short)LOWORD(lParam);
            pt.y = (short)HIWORD(lParam);
            command = HitTestDragStripCommand(hWnd, pt);
            if (GetCapture() == hWnd)
                ReleaseCapture();
            state = SetDragStripCommandInState(state, kDragStripPressedCommandMask, kDragStripPressedCommandShift, DragStripCommandNone);
            state = SetDragStripCommandInState(state, kDragStripHoverCommandMask, kDragStripHoverCommandShift, DragStripCommandNone);
            state &= ~DragStripPressed;
            if (command == pressedCommand && IsDragStripNavigationCommandEnabled(hWnd, pressedCommand))
            {
                state = SetDragStripCommandInState(state, kDragStripHoverCommandMask, kDragStripHoverCommandShift, pressedCommand);
                SetDragStripState(hWnd, state);
                ExecuteDragStripNavigationCommand(hWnd, pressedCommand);
            }
            else
            {
                SetDragStripState(hWnd, state);
            }
            return 0;
        }
        break;
    case WM_LBUTTONDBLCLK:
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        if (HitTestDragStripCommand(hWnd, pt) != DragStripCommandNone)
            return 0;
        if (IsDragStripParentBorderless(hWnd))
            ToggleParentMaximized(hWnd);
        return 0;
    case WM_DESTROY:
        DestroyDragStripTooltip(hWnd);
        break;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        PaintDragStrip(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    default:
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

void RegisterDragStripClass()
{
    static ATOM atom = 0;
    if (atom)
        return;

    WNDCLASSEXW wcex = {0};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_DBLCLKS;
    wcex.lpfnWndProc = DragStripProc;
    wcex.hInstance = GetModuleHandle(NULL);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.lpszClassName = kDragStripClass;
    atom = RegisterClassExW(&wcex);
}

class EmbeddedWereadView
{
public:
    explicit EmbeddedWereadView(HWND hParent) : m_hParent(hParent), m_generation(++g_nextViewGeneration) { LoadSavedZoomFactor(); }

    ~EmbeddedWereadView()
    {
        Close();
    }

    HWND Parent() const
    {
        return m_hParent;
    }

    BOOL HasGeneration(UINT64 generation) const
    {
        return m_generation == generation;
    }

    BOOL IsVisible() const
    {
        return m_visible;
    }

    BOOL HasLastPageUrl() const
    {
        return m_lastPageUrl[0] != L'\0';
    }

    const wchar_t* LastPageUrl() const
    {
        return m_lastPageUrl[0] ? m_lastPageUrl : m_currentUrl;
    }

    void SetZoomFactor(double zoomFactor)
    {
        m_zoomFactor = NormalizeZoomFactor(zoomFactor);
        if (m_controller)
            m_controller->put_ZoomFactor(m_zoomFactor);
    }

    void Toggle(const wchar_t* url)
    {
        if (m_visible && IsCurrentUrl(url))
            Hide();
        else
            Show(url);
    }

    void Show(const wchar_t* url)
    {
        BOOL sameUrl = IsCurrentUrl(url);

        m_visible = TRUE;
        SetCurrentUrl(url);
        EnsureCreated();

        if (m_controller)
        {
            ApplyControllerBackground();
            ApplyPageScrollbarScript();
            SaveCurrentSessionState(TRUE);
            if (!sameUrl && m_webView)
                NavigateCurrentUrl();
            m_controller->put_IsVisible(TRUE);
            Resize(NULL);
            m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            UpdateDragStrip();
        }
    }

    void Hide()
    {
        CaptureCurrentPageUrl();
        m_visible = FALSE;
        SaveCurrentSessionState(FALSE);
        if (m_controller)
            m_controller->put_IsVisible(FALSE);
        UpdateDragStrip();
    }

    void Close()
    {
        CaptureCurrentPageUrl();
        SaveCurrentSessionState(m_visible);
        DestroyDragStrip();
        m_webView.Reset();
        if (m_controller)
        {
            m_controller->Close();
            m_controller.Reset();
        }
        m_environment.Reset();

        if (m_comInitialized)
        {
            CoUninitialize();
            m_comInitialized = false;
        }
    }

    void Resize(const RECT* bounds)
    {
        if (bounds)
        {
            m_bounds = *bounds;
            m_hasBounds = true;
        }
        else if (!m_hasBounds)
        {
            GetClientRect(m_hParent, &m_bounds);
            m_hasBounds = true;
        }

        if (m_controller && m_hasBounds)
        {
            RECT controllerBounds = GetControllerBounds();
            m_controller->put_Bounds(controllerBounds);
            m_controller->NotifyParentWindowPositionChanged();
            ApplyPageScrollbarScript();
        }

        UpdateDragStrip();
    }

    void Reload()
    {
        if (m_webView)
        {
            m_webView->Reload();
            UpdateNavigationState();
        }
    }

    BOOL IsNavigationCommandEnabled(DragStripCommand command) const
    {
        switch (command)
        {
        case DragStripCommandBack:
            return m_webView && m_canGoBack;
        case DragStripCommandForward:
            return m_webView && m_canGoForward;
        case DragStripCommandRefresh:
            return m_webView != nullptr;
        default:
            return FALSE;
        }
    }

    void ExecuteNavigationCommand(DragStripCommand command)
    {
        if (!m_webView)
            return;

        UpdateNavigationState();
        switch (command)
        {
        case DragStripCommandBack:
            if (m_canGoBack)
                m_webView->GoBack();
            break;
        case DragStripCommandForward:
            if (m_canGoForward)
                m_webView->GoForward();
            break;
        case DragStripCommandRefresh:
            m_webView->Reload();
            break;
        default:
            break;
        }
        UpdateNavigationState();
    }

    void UpdateNavigationState()
    {
        BOOL canGoBack = FALSE;
        BOOL canGoForward = FALSE;

        if (m_webView)
        {
            m_webView->get_CanGoBack(&canGoBack);
            m_webView->get_CanGoForward(&canGoForward);
        }

        m_canGoBack = canGoBack ? true : false;
        m_canGoForward = canGoForward ? true : false;
        ApplyDragStripState();
    }

    void UpdateBackground()
    {
        ApplyControllerBackground();
        RemovePageBackgroundScript();
        ApplyDragStripThemeScript();
        if (m_hDragStrip && IsWindow(m_hDragStrip))
            InvalidateRect(m_hDragStrip, NULL, FALSE);
    }

    BOOL IsCurrentUrl(const wchar_t* url) const
    {
        const wchar_t* actual = (url && url[0]) ? url : kWereadUrl;
        return _wcsicmp(m_currentUrl, actual) == 0;
    }

private:
    void SetCurrentUrl(const wchar_t* url)
    {
        const wchar_t* actual = (url && url[0]) ? url : kWereadUrl;
        StringCchCopyW(m_currentUrl, ARRAYSIZE(m_currentUrl), actual);
        if (!m_lastPageUrl[0])
            StringCchCopyW(m_lastPageUrl, ARRAYSIZE(m_lastPageUrl), actual);
    }

    void NavigateCurrentUrl()
    {
        if (!m_webView)
            return;
        if (!BuildNavigableUrl(m_currentUrl, m_navigationUrl, ARRAYSIZE(m_navigationUrl)))
            return;

        StringCchCopyW(m_lastPageUrl, ARRAYSIZE(m_lastPageUrl), m_navigationUrl);
        SaveCurrentSessionState(m_visible);
        m_webView->Navigate(m_navigationUrl);
    }

    void EnsureCreated()
    {
        if (m_controller || m_creating)
            return;

        if (!m_currentUrl[0])
            SetCurrentUrl(kWereadUrl);

        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        m_comInitialized = (hr == S_OK || hr == S_FALSE);
        m_creating = true;
        CreateWebView();
    }

    void CreateWebView()
    {
        wchar_t userDataFolder[MAX_PATH] = {0};
        GetUserDataFolder(userDataFolder, ARRAYSIZE(userDataFolder));
        HWND hParent = m_hParent;
        UINT64 generation = m_generation;

        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            NULL,
            userDataFolder,
            NULL,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [hParent, generation](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT
                {
                    EmbeddedWereadView* self = GetView(hParent, false);
                    if (!self || !self->HasGeneration(generation))
                        return S_OK;

                    if (FAILED(result) || !environment)
                    {
                        self->m_creating = false;
                        self->m_visible = FALSE;
                        self->ShowWebViewError();
                        return S_OK;
                    }

                    self->m_environment = environment;
                    HRESULT controllerHr = environment->CreateCoreWebView2Controller(
                        hParent,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [hParent, generation](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT
                            {
                                EmbeddedWereadView* self = GetView(hParent, false);
                                if (!self || !self->HasGeneration(generation))
                                    return S_OK;

                                self->m_creating = false;
                                if (FAILED(controllerResult) || !controller)
                                {
                                    self->m_visible = FALSE;
                                    self->m_environment.Reset();
                                    self->ShowWebViewError();
                                    return S_OK;
                                }

                                self->m_controller = controller;
                                self->m_controller->get_CoreWebView2(&self->m_webView);
                                self->SetZoomFactor(self->m_zoomFactor);
                                self->ApplyControllerBackground();
                                self->RegisterAcceleratorKeyHandler();
                                self->RegisterNavigationCompletedHandler();
                                self->RegisterHistoryChangedHandler();
                                self->RegisterWebMessageHandler();
                                self->RegisterZoomFactorChangedHandler();
                                self->Resize(NULL);
                                self->m_controller->put_IsVisible(self->m_visible);
                                self->UpdateDragStrip();
                                self->UpdateNavigationState();

                                if (self->m_webView)
                                    self->NavigateCurrentUrl();

                                if (self->m_visible)
                                    self->m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);

                                return S_OK;
                            }).Get());

                    if (FAILED(controllerHr))
                    {
                        self->m_creating = false;
                        self->m_visible = FALSE;
                        self->m_environment.Reset();
                        self->ShowWebViewError();
                    }

                    return S_OK;
                }).Get());

        if (FAILED(hr))
        {
            m_creating = false;
            m_visible = FALSE;
            ShowWebViewError();
        }
    }

    void RegisterAcceleratorKeyHandler()
    {
        HWND hParent = m_hParent;
        UINT64 generation = m_generation;
        if (!m_controller)
            return;

        m_controller->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [hParent, generation](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT
                {
                    EmbeddedWereadView* self = GetView(hParent, false);
                    COREWEBVIEW2_KEY_EVENT_KIND kind;
                    UINT key;

                    if (!self || !self->HasGeneration(generation) || !args)
                        return S_OK;

                    args->get_KeyEventKind(&kind);
                    if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
                        kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)
                    {
                        return S_OK;
                    }

                    args->get_VirtualKey(&key);
                    if (DispatchWereadHostShortcut(hParent, key))
                    {
                        args->put_Handled(TRUE);
                    }

                    return S_OK;
                }).Get(),
            &m_acceleratorKeyToken);
    }

    void RegisterZoomFactorChangedHandler()
    {
        HWND hParent = m_hParent;
        UINT64 generation = m_generation;
        if (!m_controller)
            return;

        m_controller->add_ZoomFactorChanged(
            Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
                [hParent, generation](ICoreWebView2Controller*, IUnknown*) -> HRESULT
                {
                    EmbeddedWereadView* self = GetView(hParent, false);
                    double zoomFactor = kDefaultZoomFactor;

                    if (!self || !self->HasGeneration(generation) || !self->m_controller)
                        return S_OK;

                    if (SUCCEEDED(self->m_controller->get_ZoomFactor(&zoomFactor)))
                    {
                        self->m_zoomFactor = NormalizeZoomFactor(zoomFactor);
                        self->SaveCurrentSessionState(self->m_visible);
                    }

                    return S_OK;
                }).Get(),
            &m_zoomFactorChangedToken);
    }

    void RegisterNavigationCompletedHandler()
    {
        HWND hParent = m_hParent;
        UINT64 generation = m_generation;
        if (!m_webView)
            return;

        m_webView->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [hParent, generation](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
                {
                    EmbeddedWereadView* self = GetView(hParent, false);
                    BOOL isSuccess = TRUE;
                    if (!self || !self->HasGeneration(generation) || !args)
                        return S_OK;

                    args->get_IsSuccess(&isSuccess);

                    if (isSuccess)
                    {
                        self->CaptureCurrentPageUrl();
                        self->SaveCurrentSessionState(self->m_visible);
                        self->RemovePageBackgroundScript();
                        self->ApplyPageScrollbarScript();
                        self->ApplyDragStripThemeScript();
                        self->UpdateNavigationState();
                    }
                    else
                    {
                        self->UpdateNavigationState();
                    }

                    return S_OK;
                }).Get(),
            &m_navigationCompletedToken);
    }

    void RegisterHistoryChangedHandler()
    {
        HWND hParent = m_hParent;
        UINT64 generation = m_generation;
        if (!m_webView)
            return;

        m_webView->add_HistoryChanged(
            Callback<ICoreWebView2HistoryChangedEventHandler>(
                [hParent, generation](ICoreWebView2*, IUnknown*) -> HRESULT
                {
                    EmbeddedWereadView* self = GetView(hParent, false);
                    if (self && self->HasGeneration(generation))
                        self->UpdateNavigationState();
                    return S_OK;
                }).Get(),
            &m_historyChangedToken);
    }

    void RegisterWebMessageHandler()
    {
        HWND hParent = m_hParent;
        UINT64 generation = m_generation;
        ComPtr<ICoreWebView2Settings> settings;

        if (!m_webView)
            return;

        if (SUCCEEDED(m_webView->get_Settings(&settings)) && settings)
            settings->put_IsWebMessageEnabled(TRUE);

        m_webView->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [hParent, generation](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
                {
                    EmbeddedWereadView* self = GetView(hParent, false);
                    LPWSTR message = NULL;

                    if (!self || !self->HasGeneration(generation) || !args)
                        return S_OK;

                    if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message)
                    {
                        if (wcscmp(message, kDragStripThemeDarkMessage) == 0)
                            self->SetDragStripDarkBackground(true);
                        else if (wcscmp(message, kDragStripThemeLightMessage) == 0)
                            self->SetDragStripDarkBackground(false);
                    }

                    if (message)
                        CoTaskMemFree(message);
                    return S_OK;
                }).Get(),
            &m_webMessageReceivedToken);
    }

    void GetUserDataFolder(wchar_t* buffer, size_t cchBuffer)
    {
        wchar_t localAppData[MAX_PATH] = {0};
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, localAppData)))
        {
            StringCchPrintfW(buffer, cchBuffer, L"%s\\Rish\\WebView2", localAppData);
            wchar_t rishFolder[MAX_PATH] = {0};
            StringCchPrintfW(rishFolder, ARRAYSIZE(rishFolder), L"%s\\Rish", localAppData);
            CreateDirectoryW(rishFolder, NULL);
            CreateDirectoryW(buffer, NULL);
            return;
        }

        GetTempPathW((DWORD)cchBuffer, buffer);
    }

    void ShowWebViewError()
    {
        MessageBoxW(
            m_hParent,
            L"WebView2 failed to start. Please install Microsoft Edge WebView2 Runtime.",
            L"Rish",
            MB_OK | MB_ICONERROR);
    }

    void ApplyControllerBackground()
    {
        ComPtr<ICoreWebView2Controller2> controller2;
        COREWEBVIEW2_COLOR color = {0};

        if (!m_controller || FAILED(m_controller.As(&controller2)) || !controller2)
            return;

        color.A = 255;
        color.R = 255;
        color.G = 255;
        color.B = 255;
        controller2->put_DefaultBackgroundColor(color);
    }

    void RemovePageBackgroundScript()
    {
        if (!m_webView)
            return;

        m_webView->ExecuteScript(kRemoveTransparentBackgroundScript, nullptr);
    }

    bool ShouldHidePageScrollbars() const
    {
        return m_visible && IsWereadDragStripEnabled(m_hParent);
    }

    void ApplyPageScrollbarScript()
    {
        if (!m_webView)
            return;

        m_webView->ExecuteScript(
            ShouldHidePageScrollbars() ? kHideScrollbarsScript : kRemoveHideScrollbarsScript,
            nullptr);
    }

    void ApplyDragStripThemeScript()
    {
        if (m_webView)
            m_webView->ExecuteScript(kDetectDragStripThemeScript, nullptr);
    }

    void CaptureCurrentPageUrl()
    {
        LPWSTR source = NULL;

        if (!m_webView)
            return;

        if (SUCCEEDED(m_webView->get_Source(&source)) && source && source[0])
            StringCchCopyW(m_lastPageUrl, ARRAYSIZE(m_lastPageUrl), source);

        if (source)
            CoTaskMemFree(source);
    }

    void SaveCurrentSessionState(BOOL reopen)
    {
        const wchar_t* url = IsWereadUrl(m_currentUrl) ? kWereadUrl : LastPageUrl();
        if (url && url[0])
            SaveWebViewSessionState(reopen, url, m_zoomFactor);
    }

    void LoadSavedZoomFactor()
    {
        WebViewSessionState state;
        if (LoadWebViewSessionState(&state))
            m_zoomFactor = NormalizeZoomFactor(state.zoomFactor);
    }

    bool ShouldReserveResizeMargin() const
    {
        LONG_PTR style;

        if (!m_visible || !m_hasBounds || !IsWindow(m_hParent))
            return false;

        style = GetWindowLongPtr(m_hParent, GWL_STYLE);
        return (style & WS_CAPTION) == 0 && IsWereadDragStripEnabled(m_hParent);
    }

    int GetBorderlessResizeMarginX() const
    {
        return GetWidthForDpi(kBorderlessResizeMargin);
    }

    int GetBorderlessResizeMarginY() const
    {
        return GetHeightForDpi(kBorderlessResizeMargin);
    }

    RECT GetControllerBounds() const
    {
        RECT bounds = m_bounds;
        int marginX;
        int marginY;
        int topReserve;

        if (!ShouldReserveResizeMargin())
            return bounds;

        marginX = GetBorderlessResizeMarginX();
        marginY = GetBorderlessResizeMarginY();
        topReserve = GetHeightForDpi(kBorderlessTopReserve);
        if (marginX <= 0 || marginY <= 0 || topReserve <= 0)
            return bounds;

        bounds.left += marginX;
        bounds.top += topReserve;
        bounds.right -= marginX;
        bounds.bottom -= marginY;
        if (bounds.left > m_bounds.right)
            bounds.left = m_bounds.right;
        if (bounds.top > m_bounds.bottom)
            bounds.top = m_bounds.bottom;
        if (bounds.right < bounds.left)
            bounds.right = bounds.left;
        if (bounds.bottom < bounds.top)
            bounds.bottom = bounds.top;
        return bounds;
    }

    bool ShouldShowDragStrip() const
    {
        if (!m_visible || !m_controller || !m_hasBounds || !IsWindow(m_hParent))
            return false;

        return true;
    }

    UINT DragStripThemeState() const
    {
        return m_dragStripDarkBackground ? DragStripDarkBackground : 0;
    }

    UINT DragStripNavigationState() const
    {
        UINT state = 0;
        if (m_canGoBack)
            state |= DragStripCanGoBack;
        if (m_canGoForward)
            state |= DragStripCanGoForward;
        if (m_webView)
            state |= DragStripCanRefresh;
        return state;
    }

    void ApplyDragStripState()
    {
        UINT state;
        DragStripCommand hoverCommand;
        DragStripCommand pressedCommand;

        if (!m_hDragStrip || !IsWindow(m_hDragStrip))
            return;

        state = GetDragStripState(m_hDragStrip);
        state &= ~(DragStripDarkBackground | kDragStripNavigationMask);
        state |= DragStripThemeState();
        state |= DragStripNavigationState();
        hoverCommand = GetDragStripCommandFromState(state, kDragStripHoverCommandMask, kDragStripHoverCommandShift);
        pressedCommand = GetDragStripCommandFromState(state, kDragStripPressedCommandMask, kDragStripPressedCommandShift);
        if (hoverCommand != DragStripCommandNone && !IsDragStripCommandEnabledInState(state, hoverCommand))
            state = SetDragStripCommandInState(state, kDragStripHoverCommandMask, kDragStripHoverCommandShift, DragStripCommandNone);
        if (pressedCommand != DragStripCommandNone && !IsDragStripCommandEnabledInState(state, pressedCommand))
            state = SetDragStripCommandInState(state, kDragStripPressedCommandMask, kDragStripPressedCommandShift, DragStripCommandNone);
        SetDragStripState(m_hDragStrip, state);
        UpdateDragStripTooltipRects(m_hDragStrip);
    }

    void SetDragStripDarkBackground(bool darkBackground)
    {
        if (m_dragStripDarkBackground == darkBackground)
            return;

        m_dragStripDarkBackground = darkBackground;
        ApplyDragStripState();
    }

    void EnsureDragStrip()
    {
        if (m_hDragStrip && IsWindow(m_hDragStrip))
            return;

        RegisterDragStripClass();
        m_hDragStrip = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_NOACTIVATE,
            kDragStripClass,
            NULL,
            WS_CHILD,
            0,
            0,
            0,
            0,
            m_hParent,
            NULL,
            GetModuleHandle(NULL),
            NULL);

        if (m_hDragStrip)
        {
            EnsureDragStripTooltip(m_hDragStrip);
            SetDragStripState(m_hDragStrip, DragStripThemeState() | DragStripNavigationState());
            UpdateDragStripTooltipRects(m_hDragStrip);
        }
    }

    void DestroyDragStrip()
    {
        if (m_hDragStrip && IsWindow(m_hDragStrip))
            DestroyWindow(m_hDragStrip);
        m_hDragStrip = NULL;
    }

    void UpdateDragStrip()
    {
        if (!ShouldShowDragStrip())
        {
            if (m_hDragStrip && IsWindow(m_hDragStrip))
            {
                SetDragStripState(m_hDragStrip, DragStripThemeState() | DragStripNavigationState());
                ShowWindow(m_hDragStrip, SW_HIDE);
            }
            return;
        }

        EnsureDragStrip();
        if (!m_hDragStrip)
            return;

        bool reserveResizeMargin = ShouldReserveResizeMargin();
        int marginX = reserveResizeMargin ? GetBorderlessResizeMarginX() : 0;
        int marginY = reserveResizeMargin ? GetBorderlessResizeMarginY() : 0;
        int stripHeight = GetHeightForDpi(kDragStripHeight);
        int x = m_bounds.left + marginX;
        int y = m_bounds.top + marginY;
        int width = m_bounds.right - m_bounds.left - marginX * 2;
        int availableHeight = m_bounds.bottom - y;

        if (!reserveResizeMargin)
        {
            int navOnlyWidth = GetWidthForDpi(kNavButtonSize * 3 + kNavButtonGap * 2 + kNavButtonRightPadding);
            if (navOnlyWidth < width)
            {
                x = m_bounds.right - navOnlyWidth;
                width = navOnlyWidth;
            }
        }

        if (width <= 0 || availableHeight <= 0)
        {
            SetDragStripState(m_hDragStrip, DragStripThemeState() | DragStripNavigationState());
            UpdateDragStripTooltipRects(m_hDragStrip);
            ShowWindow(m_hDragStrip, SW_HIDE);
            return;
        }

        if (stripHeight > availableHeight)
            stripHeight = availableHeight;
        SetWindowPos(
            m_hDragStrip,
            HWND_TOP,
            x,
            y,
            width,
            stripHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetDragStripState(m_hDragStrip, GetDragStripState(m_hDragStrip) & ~kDragStripTransientStateMask);
        ApplyDragStripState();
        UpdateDragStripTooltipRects(m_hDragStrip);
        RedrawWindow(m_hDragStrip, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }

private:
    HWND m_hParent = NULL;
    HWND m_hDragStrip = NULL;
    BOOL m_visible = FALSE;
    bool m_creating = false;
    bool m_comInitialized = false;
    bool m_hasBounds = false;
    bool m_dragStripDarkBackground = false;
    bool m_canGoBack = false;
    bool m_canGoForward = false;
    UINT64 m_generation = 0;
    RECT m_bounds = {0};
    wchar_t m_currentUrl[MAX_ONLINE_STORE_URL] = {0};
    wchar_t m_lastPageUrl[MAX_ONLINE_STORE_URL] = {0};
    wchar_t m_navigationUrl[MAX_ONLINE_STORE_URL] = {0};
    double m_zoomFactor = kDefaultZoomFactor;
    EventRegistrationToken m_acceleratorKeyToken = {0};
    EventRegistrationToken m_navigationCompletedToken = {0};
    EventRegistrationToken m_historyChangedToken = {0};
    EventRegistrationToken m_webMessageReceivedToken = {0};
    EventRegistrationToken m_zoomFactorChangedToken = {0};
    ComPtr<ICoreWebView2Environment> m_environment;
    ComPtr<ICoreWebView2Controller> m_controller;
    ComPtr<ICoreWebView2> m_webView;
};

EmbeddedWereadView* g_view = NULL;

EmbeddedWereadView* GetView(HWND hParent, bool create)
{
    if (g_view && (!IsWindow(g_view->Parent()) || g_view->Parent() != hParent))
    {
        delete g_view;
        g_view = NULL;
    }

    if (!g_view && create && IsWindow(hParent))
        g_view = new EmbeddedWereadView(hParent);

    return g_view;
}

BOOL IsDragStripNavigationCommandEnabled(HWND hWnd, DragStripCommand command)
{
    EmbeddedWereadView* view = GetView(GetParent(hWnd), false);
    return view ? view->IsNavigationCommandEnabled(command) : FALSE;
}

void ExecuteDragStripNavigationCommand(HWND hWnd, DragStripCommand command)
{
    EmbeddedWereadView* view = GetView(GetParent(hWnd), false);
    if (view)
        view->ExecuteNavigationCommand(command);
}
}

void OpenWereadWebView(HWND hParent)
{
    OpenOnlineStoreWebView(hParent, kWereadUrl);
}

void OpenOnlineStoreWebView(HWND hParent, const wchar_t* url)
{
    EmbeddedWereadView* view = GetView(hParent, true);
    if (view)
        view->Toggle(url);
}

BOOL RestoreLastOnlineStoreWebView(HWND hParent)
{
    WebViewSessionState state;
    EmbeddedWereadView* view;

    if (!LoadWebViewSessionState(&state) || !state.reopen)
        return FALSE;

    view = GetView(hParent, true);
    if (!view)
        return FALSE;

    view->SetZoomFactor(state.zoomFactor);
    view->Show(state.url);
    return TRUE;
}

BOOL OpenCustomOnlineStoreWebView(HWND hParent, int index)
{
    if (!_header || index < 0 || index >= _header->online_store_count)
        return FALSE;

    OpenOnlineStoreWebView(hParent, _header->online_stores[index].url);
    return TRUE;
}

void HideWereadWebView(HWND hParent)
{
    EmbeddedWereadView* view = GetView(hParent, false);
    if (view)
        view->Hide();
}

void CloseWereadWebView(HWND hParent)
{
    EmbeddedWereadView* view = GetView(hParent, false);
    if (view)
    {
        delete view;
        g_view = NULL;
    }
}

void ResizeWereadWebView(HWND hParent, const RECT* bounds)
{
    EmbeddedWereadView* view = GetView(hParent, false);
    if (view)
        view->Resize(bounds);
}

void RefreshWereadWebView(HWND hParent)
{
    EmbeddedWereadView* view = GetView(hParent, false);
    if (view)
        view->Reload();
}

BOOL IsOnlineStoreDeleteCommandClick(HWND hParent, int index)
{
    return IsOnlineStoreDeleteCommandClickInternal(hParent, index);
}

void UpdateWereadWebViewBackground(HWND hParent)
{
    EmbeddedWereadView* view = GetView(hParent, false);
    if (view)
        view->UpdateBackground();
}

BOOL IsWereadWebViewVisible(HWND hParent)
{
    EmbeddedWereadView* view = GetView(hParent, false);
    return view ? view->IsVisible() : FALSE;
}

void AppendOnlineBookStoreMenuItems(HMENU hMenu, HWND hParent)
{
    EmbeddedWereadView* view = GetView(hParent, false);
    BOOL visible = view ? view->IsVisible() : FALSE;
    UINT wereadFlag = (visible && view && view->IsCurrentUrl(kWereadUrl)) ? MF_CHECKED : MF_UNCHECKED;
    HMENU hOnlineBookStore = CreatePopupMenu();
    MENUINFO menuInfo = {0};

    if (!hOnlineBookStore)
        return;

    ResetOnlineStoreMenuClickState(hOnlineBookStore);

    menuInfo.cbSize = sizeof(menuInfo);
    menuInfo.fMask = MIM_STYLE;
    menuInfo.dwStyle = MNS_NOTIFYBYPOS;
    SetMenuInfo(hOnlineBookStore, &menuInfo);

    AppendMenuW(hOnlineBookStore, MF_STRING | wereadFlag, IDM_WEREAD, kWereadMenuTitle);

    if (_header && _header->online_store_count > 0)
    {
        AppendMenuW(hOnlineBookStore, MF_SEPARATOR, 0, NULL);
        for (int i = 0; i < _header->online_store_count; i++)
        {
            UINT customFlag = (visible && view && view->IsCurrentUrl(_header->online_stores[i].url)) ? MF_CHECKED : MF_UNCHECKED;
            wchar_t itemText[MAX_ONLINE_STORE_NAME + MAX_ONLINE_STORE_URL + 8] = {0};
            StringCchPrintfW(itemText, ARRAYSIZE(itemText), L"%s\t%s", GetOnlineStoreDisplayName(i), kRightClickDeleteHint);
            AppendMenuW(hOnlineBookStore, MF_STRING | customFlag, IDM_ONLINE_STORE_FIRST + i, itemText);
            RememberOnlineStoreMenuPosition(i, hOnlineBookStore);
        }
    }

    AppendMenuW(hOnlineBookStore, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hOnlineBookStore, MF_STRING, IDM_ONLINE_STORE_ADD, kAddUrlMenuTitle);

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hOnlineBookStore, kOnlineBookStoreTitle);
}

BOOL AddOnlineStoreUrl(HWND hParent)
{
    OnlineStoreDialogData data = {0};

    if (!_header)
        return FALSE;
    if (_header->online_store_count >= MAX_ONLINE_STORE_COUNT)
    {
        MessageBoxW(hParent, L"\u81ea\u5b9a\u4e49\u7f51\u5740\u6570\u91cf\u5df2\u8fbe\u4e0a\u9650\u3002", L"Rish", MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    if (DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_ONLINE_STORE_ADD), hParent, AddOnlineStoreUrlDlgProc, (LPARAM)&data) != IDOK)
        return FALSE;

    if (IsDuplicateOnlineStoreUrl(data.url))
    {
        MessageBoxW(hParent, L"\u8be5\u7f51\u5740\u5df2\u7ecf\u5b58\u5728\u3002", L"Rish", MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    StringCchCopyW(_header->online_stores[_header->online_store_count].name, MAX_ONLINE_STORE_NAME, data.name);
    StringCchCopyW(_header->online_stores[_header->online_store_count].url, MAX_ONLINE_STORE_URL, data.url);
    _header->online_store_count++;
    Save(hParent);
    return TRUE;
}

BOOL DeleteCustomOnlineStoreUrl(HWND hParent, int index)
{
    wchar_t message[MAX_ONLINE_STORE_URL + MAX_ONLINE_STORE_NAME + 64] = {0};

    if (!_header || index < 0 || index >= _header->online_store_count)
        return FALSE;

    StringCchPrintfW(
        message,
        ARRAYSIZE(message),
        L"\u5220\u9664\u8fd9\u4e2a\u7f51\u5740\uff1f\r\n%s\r\n%s",
        _header->online_stores[index].name,
        _header->online_stores[index].url);
    if (MessageBoxW(hParent, message, L"Rish", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return FALSE;

    for (int i = index; i < _header->online_store_count - 1; i++)
        _header->online_stores[i] = _header->online_stores[i + 1];

    _header->online_store_count--;
    ZeroMemory(&_header->online_stores[_header->online_store_count], sizeof(online_store_t));
    Save(hParent);
    return TRUE;
}

BOOL MeasureOnlineStoreMenuItem(HWND hParent, MEASUREITEMSTRUCT* measure)
{
    UINT id;
    int index;
    HDC hdc;
    HFONT hFont;
    HFONT hOldFont;
    LOGFONTW lf;
    SIZE textSize = {0};
    const wchar_t* name;

    UNREFERENCED_PARAMETER(hParent);

    if (!measure || measure->CtlType != ODT_MENU)
        return FALSE;

    id = measure->itemID;
    if (!IsOnlineStoreCommand(id))
        return FALSE;

    index = id - IDM_ONLINE_STORE_FIRST;
    name = GetOnlineStoreDisplayName(index);

    hdc = GetDC(NULL);
    if (!hdc)
        return FALSE;

    GetMenuFont(&lf);
    hFont = CreateFontIndirectW(&lf);
    hOldFont = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;
    GetTextExtentPoint32W(hdc, name, (int)wcslen(name), &textSize);

    if (hOldFont)
        SelectObject(hdc, hOldFont);
    if (hFont)
        DeleteObject(hFont);
    ReleaseDC(NULL, hdc);

    measure->itemHeight = max((UINT)GetSystemMetrics(SM_CYMENU), 24U);
    measure->itemWidth = (UINT)textSize.cx
        + (UINT)GetSystemMetrics(SM_CXMENUCHECK)
        + kOnlineStoreDeleteZoneWidth
        + kOnlineStoreMenuPaddingX * 2;
    return TRUE;
}

BOOL DrawOnlineStoreMenuItem(HWND hParent, DRAWITEMSTRUCT* draw)
{
    UINT id;
    int index;
    RECT rc;
    RECT checkRc;
    RECT textRc;
    RECT deleteRc;
    HBRUSH hBrush;
    HPEN hPen;
    HPEN hOldPen;
    HFONT hFont;
    HFONT hOldFont;
    LOGFONTW lf;
    COLORREF oldTextColor;
    int oldBkMode;
    BOOL selected;
    BOOL checked;
    const wchar_t* name;

    UNREFERENCED_PARAMETER(hParent);

    if (!draw || draw->CtlType != ODT_MENU)
        return FALSE;

    id = draw->itemID;
    if (!IsOnlineStoreCommand(id))
        return FALSE;

    index = id - IDM_ONLINE_STORE_FIRST;
    name = GetOnlineStoreDisplayName(index);
    selected = (draw->itemState & ODS_SELECTED) == ODS_SELECTED;
    checked = (draw->itemState & ODS_CHECKED) == ODS_CHECKED;
    rc = draw->rcItem;

    hBrush = GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_MENU);
    FillRect(draw->hDC, &rc, hBrush);

    GetMenuFont(&lf);
    hFont = CreateFontIndirectW(&lf);
    hOldFont = hFont ? (HFONT)SelectObject(draw->hDC, hFont) : NULL;

    oldTextColor = SetTextColor(draw->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
    oldBkMode = SetBkMode(draw->hDC, TRANSPARENT);

    checkRc = rc;
    checkRc.left += 2;
    checkRc.right = checkRc.left + GetSystemMetrics(SM_CXMENUCHECK);
    checkRc.top += 2;
    checkRc.bottom -= 2;
    if (checked)
        DrawFrameControl(draw->hDC, &checkRc, DFC_MENU, DFCS_MENUCHECK);

    deleteRc = rc;
    deleteRc.left = deleteRc.right - kOnlineStoreDeleteZoneWidth;
    CacheDeleteZoneRect(hParent, (HMENU)draw->hwndItem, id, &deleteRc);
    hPen = CreatePen(PS_SOLID, 1, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_3DSHADOW));
    hOldPen = hPen ? (HPEN)SelectObject(draw->hDC, hPen) : NULL;
    MoveToEx(draw->hDC, deleteRc.left, deleteRc.top + 4, NULL);
    LineTo(draw->hDC, deleteRc.left, deleteRc.bottom - 4);
    if (hOldPen)
        SelectObject(draw->hDC, hOldPen);
    if (hPen)
        DeleteObject(hPen);

    textRc = rc;
    textRc.left += GetSystemMetrics(SM_CXMENUCHECK) + kOnlineStoreMenuPaddingX;
    textRc.right = deleteRc.left - 6;
    DrawTextW(draw->hDC, name, -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    DrawTextW(draw->hDC, kDeleteIcon, -1, &deleteRc, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

    SetBkMode(draw->hDC, oldBkMode);
    SetTextColor(draw->hDC, oldTextColor);
    if (hOldFont)
        SelectObject(draw->hDC, hOldFont);
    if (hFont)
        DeleteObject(hFont);
    return TRUE;
}

int HandleOnlineStoreMenuCommand(HWND hParent, HMENU hMenu, UINT itemPosition)
{
    UINT id = GetMenuItemID(hMenu, itemPosition);

    if (id == IDM_WEREAD)
    {
        OpenWereadWebView(hParent);
        return ONLINE_STORE_MENU_WEBVIEW;
    }

    if (id == IDM_ONLINE_STORE_ADD)
        return ONLINE_STORE_MENU_ADD_REQUEST;

    if (IsOnlineStoreCommand(id))
    {
        int index = id - IDM_ONLINE_STORE_FIRST;
        return OpenCustomOnlineStoreWebView(hParent, index) ? ONLINE_STORE_MENU_WEBVIEW : ONLINE_STORE_MENU_HANDLED;
    }

    return ONLINE_STORE_MENU_NONE;
}
