#ifndef INCLUDE_TV_CAPTION3_HPP
#define INCLUDE_TV_CAPTION3_HPP

#include "aribcaption/aribcaption.hpp"
#include "OsdCompositor.h"
#include "PseudoOSD.hpp"
#include "Util.hpp"
#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#define TVTEST_PLUGIN_VERSION TVTEST_PLUGIN_VERSION_(0,0,14)
#include "TVTestPlugin.h"
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

// プラグインクラス
class CTVCaption2 : public TVTest::CTVTestPlugin
{
    // 作成できるOSDの最大数
    static const size_t OSD_MAX_CREATE_NUM = 50;
    // 事前に作成しておくOSDの数(作成時にウィンドウが前面にくるので、気になるなら増やす)
    static const size_t OSD_PRE_CREATE_NUM = 12;
    // 設定値の最大読み込み文字数
    static const int SETTING_VALUE_MAX = 2048;
    static const int STREAM_MAX = 2;
    enum STREAM_INDEX {
        STREAM_CAPTION,
        STREAM_SUPERIMPOSE,
    };
public:
    // CTVTestPlugin
    CTVCaption2();
    ~CTVCaption2();
    bool GetPluginInfo(TVTest::PluginInfo *pInfo);
    bool Initialize();
    bool Finalize();
private:
    HWND GetFullscreenWindow();
    HWND FindVideoContainer();
    bool GetVideoContainerLayout(HWND hwndContainer, RECT *pRect, RECT *pVideoRect = nullptr, RECT *pExVideoRect = nullptr);
    bool GetVideoSurfaceRect(HWND hwndContainer, RECT *pVideoRect = nullptr, RECT *pExVideoRect = nullptr);
    int GetVideoPid();
    bool EnablePlugin(bool fEnable);
    void LoadSettings();
    void SaveSettings() const;
    void SwitchSettings(int specIndex = -1);
    void AddSettings();
    void DeleteSettings();
    int GetSettingsCount() const;
    bool PlayRomSound(int index) const;
    static LRESULT CALLBACK EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData);
    static BOOL CALLBACK WindowMsgCallback(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pResult, void *pUserData);
    void HideOsds(int index, size_t osdPrepareCount = 0);
    void DeleteTextures();
    void HideAllOsds();
    void DestroyOsds();
    static LRESULT CALLBACK PaintingWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    bool ResetCaptionContext(STREAM_INDEX index);
    void ProcessCaption(std::vector<std::vector<BYTE>> &streamPesQueue);
    void OnSize(STREAM_INDEX index);
    static BOOL CALLBACK StreamCallback(BYTE *pData, void *pClientData);
    void ProcessPacket(BYTE *pPacket);
    bool PluginSettings(HWND hwndOwner);
    static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK TVTestSettingsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam, void *pClientData);
    void InitializeSettingsDlg(HWND hDlg);
    INT_PTR ProcessSettingsDlg(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // 設定
    tstring m_iniPath;
    TCHAR m_szFaceName[3][LF_FACESIZE];
    int m_settingsIndex;
    int m_paintingMethod;
    int m_showFlags[STREAM_MAX];
    int m_delayTime[STREAM_MAX];
    int m_strokeWidth;
    int m_ornStrokeWidth;
    bool m_fIgnoreSmall;
    tstring m_romSoundList;
    bool m_fInitializeSettingsDlg;

    // 字幕解析と描画
    std::unique_ptr<aribcaption::Context> m_captionContext[STREAM_MAX];
    std::unique_ptr<aribcaption::Decoder> m_captionDecoder[STREAM_MAX];
    std::unique_ptr<aribcaption::Renderer> m_captionRenderer[STREAM_MAX];
    HWND m_hwndPainting;
    HWND m_hwndContainer;
    std::vector<std::unique_ptr<CPseudoOSD>> m_pOsdList[STREAM_MAX];
    size_t m_osdShowCount[STREAM_MAX];
    LONGLONG m_clearPts[STREAM_MAX];
    bool m_fNeedtoShow;
    bool m_fShowLang2;
    std::atomic_bool m_fProfileC;

    // ストリーム解析
    std::recursive_mutex m_streamLock;
    LONGLONG m_pcr;
    DWORD m_procCapTick;
    std::atomic_bool m_fResetPat;
    PAT m_pat;
    std::atomic_int m_videoPid;
    int m_pcrPid;
    int m_caption1Pid;
    int m_caption2Pid;
    std::pair<int, std::vector<BYTE>> m_caption1Pes;
    std::pair<int, std::vector<BYTE>> m_caption2Pes;
    std::vector<std::vector<BYTE>> m_caption1PesQueue;
    std::vector<std::vector<BYTE>> m_caption2PesQueue;

    // レンダラで合成する(疑似でない)OSD
    COsdCompositor m_osdCompositor;
};

#endif // INCLUDE_TV_CAPTION3_HPP
