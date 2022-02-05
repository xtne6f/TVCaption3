#include <windows.h>
#include "PseudoOSD.hpp"

CPseudoOSD::CPseudoOSD()
    : m_hwnd(nullptr)
    , m_hwndParent(nullptr)
    , m_hwndOwner(nullptr)
    , m_x(0)
    , m_y(0)
    , m_width(0)
    , m_height(0)
    , m_hbm(nullptr)
{
}

CPseudoOSD::~CPseudoOSD()
{
    ClearImage();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
}

bool CPseudoOSD::Initialize(HINSTANCE hinst)
{
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.lpszClassName = TEXT("TVCaption3 Pseudo OSD");
    return RegisterClass(&wc) != 0;
}

bool CPseudoOSD::Create(HWND hwndParent, HINSTANCE hinst)
{
    if (m_hwnd) {
        if (m_hwndParent == hwndParent && GetWindow(m_hwnd, GW_OWNER) == m_hwndOwner) {
            return true;
        }
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    ClearImage();

    POINT pt;
    if (hwndParent && ClientToScreen(hwndParent, &pt)) {
        m_hwnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, TEXT("TVCaption3 Pseudo OSD"),
                                nullptr, WS_POPUP, pt.x, pt.y, 0, 0, hwndParent, nullptr, hinst, this);
        if (m_hwnd) {
            m_hwndParent = hwndParent;
            // WS_POPUPに親はいない。hwndParentからトップレベルまで遡ったウィンドウがオーナーになる
            m_hwndOwner = GetWindow(m_hwnd, GW_OWNER);
            m_x = 0;
            m_y = 0;
            m_width = 0;
            m_height = 0;
            return true;
        }
    }
    return false;
}

void CPseudoOSD::Show()
{
    if (m_hwnd) {
        Update();
        if (IsWindowVisible(m_hwndParent)) {
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        }
        UpdateWindow(m_hwnd);
    }
}

void CPseudoOSD::Hide()
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
    ClearImage();
}

void CPseudoOSD::ClearImage()
{
    if (m_hbm) {
        DeleteObject(m_hbm);
        m_hbm = nullptr;
    }
}

void CPseudoOSD::SetImage(HBITMAP hbm, int x, int y, int width, int height)
{
    SetPosition(x, y, width, height);
    ClearImage();
    m_hbm = hbm;
}

void CPseudoOSD::SetPosition(int x, int y, int width, int height)
{
    if (m_hwnd) {
        POINT pt;
        pt.x = m_x = x;
        pt.y = m_y = y;
        ClientToScreen(m_hwndParent, &pt);
        SetWindowPos(m_hwnd, nullptr, pt.x, pt.y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        m_width = width;
        m_height = height;
    }
}

void CPseudoOSD::GetPosition(int *x, int *y, int *width, int *height)
{
    if (x) *x = m_x;
    if (y) *y = m_y;
    if (width) *width = m_width;
    if (height) *height = m_height;
}

void CPseudoOSD::OnParentMove()
{
    SetPosition(m_x, m_y, m_width, m_height);
}

void CPseudoOSD::Update()
{
    RECT rc;
    if (GetClientRect(m_hwnd, &rc) && rc.right > 0 && rc.bottom > 0) {
        HDC hdc = GetDC(m_hwnd);
        if (hdc) {
            HDC hdcSrc = CreateCompatibleDC(hdc);
            if (hdcSrc) {
                if (m_hbm) {
                    HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcSrc, m_hbm));
                    SIZE sz;
                    sz.cx = rc.right;
                    sz.cy = rc.bottom;
                    POINT pt = {};
                    BLENDFUNCTION blend = {};
                    blend.BlendOp = AC_SRC_OVER;
                    blend.SourceConstantAlpha = 255;
                    blend.AlphaFormat = AC_SRC_ALPHA;
                    UpdateLayeredWindow(m_hwnd, hdc, nullptr, &sz, hdcSrc, &pt, 0, &blend, ULW_ALPHA);
                    SelectObject(hdcSrc, hbmOld);
                }
                DeleteDC(hdcSrc);
            }
            ReleaseDC(m_hwnd, hdc);
        }
    }
}

LRESULT CALLBACK CPseudoOSD::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_CREATE:
        {
            LPCREATESTRUCT pcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            CPseudoOSD *pThis = reinterpret_cast<CPseudoOSD*>(pcs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
        return 0;
    case WM_SIZE:
        if (IsWindowVisible(hwnd)) {
            CPseudoOSD *pThis = reinterpret_cast<CPseudoOSD*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
            pThis->Update();
        }
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
