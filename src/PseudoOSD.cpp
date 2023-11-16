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
    , m_pBits(nullptr)
    , m_fHide(true)
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
    m_fHide = true;

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
            m_fHide = false;
        }
        UpdateWindow(m_hwnd);
    }
}

void CPseudoOSD::Hide()
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_fHide = true;
    }
    ClearImage();
}

void CPseudoOSD::ClearImage()
{
    if (m_hbm) {
        DeleteObject(m_hbm);
        m_hbm = nullptr;
        m_pBits = nullptr;
    }
}

void CPseudoOSD::SetImage(HBITMAP hbm, void *pBits, int x, int y, int width, int height)
{
    SetPosition(x, y, width, height);
    ClearImage();
    m_hbm = hbm;
    m_pBits = pBits;
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

void CPseudoOSD::GetPosition(int *x, int *y, int *width, int *height) const
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

void CPseudoOSD::OnParentSize()
{
    // オーナーの最小化解除につられて復活することがあるため
    if (m_hwnd && m_fHide && IsWindowVisible(m_hwnd)) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

namespace
{
// アルファ合成する
void ComposeAlpha(BYTE *__restrict pBitsDest, const BYTE *__restrict pBits, int n)
{
    n *= 4;
    for (int i = 0; i < n; i += 4) {
        pBitsDest[i + 0] = ((pBits[i + 0] << 8) + (pBitsDest[i + 0] * (255 - pBits[i + 3]) + 255)) >> 8;
        pBitsDest[i + 1] = ((pBits[i + 1] << 8) + (pBitsDest[i + 1] * (255 - pBits[i + 3]) + 255)) >> 8;
        pBitsDest[i + 2] = ((pBits[i + 2] << 8) + (pBitsDest[i + 2] * (255 - pBits[i + 3]) + 255)) >> 8;
        pBitsDest[i + 3] = 0;
    }
}
}

void CPseudoOSD::Compose(HDC hdc, int left, int top) const
{
    if (!hdc || !m_hwnd || !IsWindowVisible(m_hwnd) || !m_hbm || m_width < 1 || m_height < 1) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    if (rc.right != m_width || rc.bottom != m_height) return;

    // OSDのイメージを描画する一時ビットマップ
    void *pBitsTmp;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = rc.right;
    bmi.bmiHeader.biHeight = rc.bottom;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HBITMAP hbmTmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &pBitsTmp, nullptr, 0);
    if (!hbmTmp) return;

    HDC hdcTmp = CreateCompatibleDC(hdc);
    if (hdcTmp) {
        HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcTmp, hbmTmp));
        // アルファ合成のために背景をコピーしておく
        BitBlt(hdcTmp, 0, 0, rc.right, rc.bottom, hdc, left, top, SRCCOPY);
        GdiFlush();
        ComposeAlpha(static_cast<BYTE*>(pBitsTmp), static_cast<BYTE*>(m_pBits), rc.right * rc.bottom);
        BitBlt(hdc, left, top, rc.right, rc.bottom, hdcTmp, 0, 0, SRCCOPY);
        SelectObject(hdcTmp, hbmOld);
        DeleteDC(hdcTmp);
    }
    DeleteObject(hbmTmp);
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
