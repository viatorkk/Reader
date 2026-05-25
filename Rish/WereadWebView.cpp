#include "WereadWebView.h"
#include "resource.h"

#include <WebView2.h>
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

namespace
{
const wchar_t kWereadUrl[] = L"https://weread.qq.com/";
const wchar_t kDragStripClass[] = L"RishWereadDragStrip";
const int kDragStripHeight = 12;

class EmbeddedWereadView;
EmbeddedWereadView* GetView(HWND hParent, bool create);

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

    void Toggle()
    {
        if (m_visible)
            Hide();
        else
            Show();
    }

    void Show()
    {
        m_visible = TRUE;
        EnsureCreated();

        if (m_controller)
        {
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

private:
    void EnsureCreated()
    {
        if (m_controller || m_creating)
            return;

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
                                self->RegisterAcceleratorKeyHandler();
                                self->Resize(NULL);
                                self->m_controller->put_IsVisible(self->m_visible);
                                self->UpdateDragStrip();

                                if (self->m_webView)
                                    self->m_webView->Navigate(kWereadUrl);

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
    RECT m_bounds = {0};
    EventRegistrationToken m_acceleratorKeyToken = {0};
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
    EmbeddedWereadView* view = GetView(hParent, true);
    if (view)
        view->Toggle();
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

BOOL IsWereadWebViewVisible(HWND hParent)
{
    EmbeddedWereadView* view = GetView(hParent, false);
    return view ? view->IsVisible() : FALSE;
}

void AppendWereadMenuItems(HMENU hMenu, HWND hParent)
{
    EmbeddedWereadView* view = GetView(hParent, false);
    BOOL visible = view ? view->IsVisible() : FALSE;
    UINT visibleFlag = visible ? MF_CHECKED : MF_UNCHECKED;

    AppendMenuW(hMenu, MF_STRING | visibleFlag, IDM_WEREAD, L"WeRead(&W)");
}
