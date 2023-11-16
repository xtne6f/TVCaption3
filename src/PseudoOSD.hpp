#ifndef INCLUDE_PSEUDO_OSD_HPP
#define INCLUDE_PSEUDO_OSD_HPP

class CPseudoOSD
{
public:
    CPseudoOSD();
    ~CPseudoOSD();
    static bool Initialize(HINSTANCE hinst);
    bool Create(HWND hwndParent, HINSTANCE hinst);
    void Show();
    void Hide();
    void ClearImage();
    // 画像を設定する。受けとったビットマップはクラス側で破棄する
    // 乗算済みアルファの32bitビットマップであること
    void SetImage(HBITMAP hbm, void *pBits, int x, int y, int width, int height);
    void GetPosition(int *x, int *y, int *width, int *height) const;
    void OnParentMove();
    void OnParentSize();
    // 表示中のOSDのイメージをhdcに合成する
    void Compose(HDC hdc, int left, int top) const;
private:
    CPseudoOSD(const CPseudoOSD &);
    CPseudoOSD &operator=(const CPseudoOSD &);
    void SetPosition(int x, int y, int width, int height);
    void Update();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd;
    HWND m_hwndParent;
    HWND m_hwndOwner;
    int m_x;
    int m_y;
    int m_width;
    int m_height;
    HBITMAP m_hbm;
    void *m_pBits;
    bool m_fHide;
};

#endif // INCLUDE_PSEUDO_OSD_HPP
