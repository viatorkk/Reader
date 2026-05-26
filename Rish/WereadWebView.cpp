#include "WereadWebView.h"
#include "resource.h"
#include "types.h"

#include <WebView2.h>
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
const int kDragStripHeight = 12;
const wchar_t kOnlineBookStoreTitle[] = L"\u5728\u7ebf\u4e66\u57ce";
const wchar_t kWereadMenuTitle[] = L"\u5fae\u4fe1\u9605\u8bfb(&W)";
const wchar_t kAddUrlMenuTitle[] = L"\u6dfb\u52a0\u7f51\u5740...";
const wchar_t kDeleteIcon[] = L"\u00d7";
const wchar_t kRightClickDeleteHint[] = L"\u53f3\u952e\u5220\u9664";
const wchar_t kTransparentBackgroundScript[] = LR"JS(
(() => {
    const id = 'rish-webview-transparent-bg';
    let style = document.getElementById(id);
    if (!style) {
        style = document.createElement('style');
        style.id = id;
        document.documentElement.appendChild(style);
    }
    style.textContent = 'html, body, #app, .app, .wr_page, .reader, .readerContent, .readerChapterContent { background: transparent !important; background-color: transparent !important; }';
})();
)JS";
const wchar_t kRemoveTransparentBackgroundScript[] = LR"JS(
(() => {
    const style = document.getElementById('rish-webview-transparent-bg');
    if (style) {
        style.remove();
    }
})();
)JS";
const int kOnlineStoreDeleteZoneWidth = 42;
const int kOnlineStoreMenuPaddingX = 12;

struct OnlineStoreDialogData
{
    wchar_t name[MAX_ONLINE_STORE_NAME];
    wchar_t url[MAX_ONLINE_STORE_URL];
};

class EmbeddedWereadView;
EmbeddedWereadView* GetView(HWND hParent, bool create);

RECT g_deleteRects[MAX_ONLINE_STORE_COUNT] = {0};
BOOL g_deleteRectValid[MAX_ONLINE_STORE_COUNT] = {0};
HMENU g_onlineStoreMenu = NULL;
int g_onlineStoreMenuPositions[MAX_ONLINE_STORE_COUNT] = {0};

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

bool BuildNavigableUrl(const wchar_t* url, bool useHttpFallback, wchar_t* buffer, size_t cchBuffer)
{
    wchar_t trimmed[MAX_ONLINE_STORE_URL] = {0};

    CopyTrimmedUrl(url, trimmed, ARRAYSIZE(trimmed));
    if (!trimmed[0])
        return false;

    if (HasHttpScheme(trimmed))
        return SUCCEEDED(StringCchCopyW(buffer, cchBuffer, trimmed));

    return SUCCEEDED(StringCchPrintfW(buffer, cchBuffer, L"%s%s", useHttpFallback ? L"http://" : L"https://", trimmed));
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

LRESULT CALLBACK DragStripProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_NCHITTEST:
        return HTCAPTION;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_SIZEALL));
        return TRUE;
    case WM_LBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
        BeginParentDrag(hWnd);
        return 0;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
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
    wcex.lpfnWndProc = DragStripProc;
    wcex.hInstance = GetModuleHandle(NULL);
    wcex.hCursor = LoadCursor(NULL, IDC_SIZEALL);
    wcex.lpszClassName = kDragStripClass;
    atom = RegisterClassExW(&wcex);
}

class EmbeddedWereadView
{
public:
    explicit EmbeddedWereadView(HWND hParent) : m_hParent(hParent) {}

    ~EmbeddedWereadView()
    {
        Close();
    }

    HWND Parent() const
    {
        return m_hParent;
    }

    BOOL IsVisible() const
    {
        return m_visible;
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
            if (!sameUrl && m_webView)
                NavigateCurrentUrl(false);
            m_controller->put_IsVisible(TRUE);
            Resize(NULL);
            m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            UpdateDragStrip();
        }
    }

    void Hide()
    {
        m_visible = FALSE;
        if (m_controller)
            m_controller->put_IsVisible(FALSE);
        UpdateDragStrip();
    }

    void Close()
    {
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
            m_controller->put_Bounds(m_bounds);

        UpdateDragStrip();
    }

    void Reload()
    {
        if (m_webView)
            m_webView->Reload();
    }

    void UpdateBackground()
    {
        ApplyControllerBackground();
        ApplyPageBackgroundScript();
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
    }

    void NavigateCurrentUrl(bool useHttpFallback)
    {
        if (!m_webView)
            return;
        if (!BuildNavigableUrl(m_currentUrl, useHttpFallback, m_navigationUrl, ARRAYSIZE(m_navigationUrl)))
            return;

        m_pendingFallbackNavigationId = 0;
        m_canFallbackToHttp = false;
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

        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            NULL,
            userDataFolder,
            NULL,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [hParent](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT
                {
                    EmbeddedWereadView* self = GetView(hParent, false);
                    if (!self)
                        return S_OK;

                    if (FAILED(result) || !environment)
                    {
                        self->m_creating = false;
                        self->m_visible = FALSE;
                        self->ShowWebViewError();
                        return S_OK;
                    }

                    self->m_environment = environment;
                    environment->CreateCoreWebView2Controller(
                        hParent,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [hParent](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT
                            {
                                EmbeddedWereadView* self = GetView(hParent, false);
                                if (!self)
                                    return S_OK;

                                self->m_creating = false;
                                if (FAILED(controllerResult) || !controller)
                                {
                                    self->m_visible = FALSE;
                                    self->ShowWebViewError();
                                    return S_OK;
                                }

                                self->m_controller = controller;
                                self->m_controller->get_CoreWebView2(&self->m_webView);
                                self->ApplyControllerBackground();
                                self->RegisterAcceleratorKeyHandler();
                                self->RegisterNavigationCompletedHandler();
                                self->Resize(NULL);
                                self->m_controller->put_IsVisible(self->m_visible);
                                self->UpdateDragStrip();

                                if (self->m_webView)
                                    self->NavigateCurrentUrl(false);

                                if (self->m_visible)
                                    self->m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);

                                return S_OK;
                            }).Get());

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
        if (!m_controller)
            return;

        m_controller->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [hParent](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT
                {
                    COREWEBVIEW2_KEY_EVENT_KIND kind;
                    UINT key;

                    if (!args)
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

    void RegisterNavigationCompletedHandler()
    {
        HWND hParent = m_hParent;
        if (!m_webView)
            return;

        m_webView->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [hParent](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT
                {
                    EmbeddedWereadView* self = GetView(hParent, false);
                    UINT64 navigationId = 0;
                    LPWSTR uri = NULL;

                    if (!self || !args)
                        return S_OK;

                    args->get_NavigationId(&navigationId);
                    args->get_Uri(&uri);
                    if (!HasHttpScheme(self->m_currentUrl)
                        && uri
                        && IsSameOnlineStoreUrl(uri, self->m_currentUrl)
                        && _wcsnicmp(uri, L"https://", 8) == 0)
                    {
                        self->m_canFallbackToHttp = true;
                        self->m_pendingFallbackNavigationId = navigationId;
                    }
                    else
                    {
                        self->m_canFallbackToHttp = false;
                        self->m_pendingFallbackNavigationId = 0;
                    }
                    if (uri)
                        CoTaskMemFree(uri);

                    return S_OK;
                }).Get(),
            &m_navigationStartingToken);

        m_webView->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [hParent](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
                {
                    EmbeddedWereadView* self = GetView(hParent, false);
                    BOOL isSuccess = TRUE;
                    UINT64 navigationId = 0;

                    if (!self || !args)
                        return S_OK;

                    args->get_IsSuccess(&isSuccess);
                    args->get_NavigationId(&navigationId);
                    if (self->m_pendingFallbackNavigationId != 0
                        && navigationId != self->m_pendingFallbackNavigationId)
                    {
                        return S_OK;
                    }

                    if (isSuccess)
                    {
                        self->m_canFallbackToHttp = false;
                        self->m_pendingFallbackNavigationId = 0;
                        self->ApplyPageBackgroundScript();
                    }
                    else if (self->m_canFallbackToHttp)
                    {
                        self->m_canFallbackToHttp = false;
                        self->m_pendingFallbackNavigationId = 0;
                        self->NavigateCurrentUrl(true);
                    }

                    return S_OK;
                }).Get(),
            &m_navigationCompletedToken);
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

    bool IsTransparentBackgroundEnabled() const
    {
        return _header && _header->webview_transparent_bg;
    }

    void ApplyControllerBackground()
    {
        ComPtr<ICoreWebView2Controller2> controller2;
        COREWEBVIEW2_COLOR color = {0};

        if (!m_controller || FAILED(m_controller.As(&controller2)) || !controller2)
            return;

        color.A = IsTransparentBackgroundEnabled() ? 0 : 255;
        color.R = 255;
        color.G = 255;
        color.B = 255;
        controller2->put_DefaultBackgroundColor(color);
    }

    void ApplyPageBackgroundScript()
    {
        if (!m_webView)
            return;

        m_webView->ExecuteScript(
            IsTransparentBackgroundEnabled() ? kTransparentBackgroundScript : kRemoveTransparentBackgroundScript,
            nullptr);
    }

    bool ShouldShowDragStrip() const
    {
        LONG_PTR style;

        if (!m_visible || !m_controller || !m_hasBounds || !IsWindow(m_hParent))
            return false;

        style = GetWindowLongPtr(m_hParent, GWL_STYLE);
        return (style & WS_CAPTION) == 0 && IsWereadDragStripEnabled(m_hParent);
    }

    void EnsureDragStrip()
    {
        if (m_hDragStrip && IsWindow(m_hDragStrip))
            return;

        RegisterDragStripClass();
        m_hDragStrip = CreateWindowExW(
            WS_EX_LAYERED,
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
            SetLayeredWindowAttributes(m_hDragStrip, 0, 1, LWA_ALPHA);
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
                ShowWindow(m_hDragStrip, SW_HIDE);
            return;
        }

        EnsureDragStrip();
        if (!m_hDragStrip)
            return;

        int width = m_bounds.right - m_bounds.left;
        int height = m_bounds.bottom - m_bounds.top;
        if (width <= 0 || height <= 0)
            return;

        height = height < kDragStripHeight ? height : kDragStripHeight;
        SetWindowPos(
            m_hDragStrip,
            HWND_TOP,
            m_bounds.left,
            m_bounds.top,
            width,
            height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

private:
    HWND m_hParent = NULL;
    HWND m_hDragStrip = NULL;
    BOOL m_visible = FALSE;
    bool m_creating = false;
    bool m_comInitialized = false;
    bool m_hasBounds = false;
    bool m_canFallbackToHttp = false;
    UINT64 m_pendingFallbackNavigationId = 0;
    RECT m_bounds = {0};
    wchar_t m_currentUrl[MAX_ONLINE_STORE_URL] = {0};
    wchar_t m_navigationUrl[MAX_ONLINE_STORE_URL] = {0};
    EventRegistrationToken m_acceleratorKeyToken = {0};
    EventRegistrationToken m_navigationStartingToken = {0};
    EventRegistrationToken m_navigationCompletedToken = {0};
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
