// TVTestに字幕を表示するプラグイン
#define NOMINMAX
#include <windows.h>
#include "TVCaption3.hpp"
#include "aribcaption/src/base/wchar_helper.hpp"
#include "resource.h"

namespace
{
const UINT WM_APP_RESET_CAPTION = WM_APP + 0;
const UINT WM_APP_PROCESS_CAPTION = WM_APP + 1;
const UINT WM_APP_DONE_MOVE = WM_APP + 2;
const UINT WM_APP_DONE_SIZE = WM_APP + 3;
const UINT WM_APP_RESET_OSDS = WM_APP + 4;

const TCHAR INFO_PLUGIN_NAME[] = TEXT("TVCaption3");
const TCHAR INFO_DESCRIPTION[] = TEXT("字幕を表示");
const TCHAR TV_CAPTION2_WINDOW_CLASS[] = TEXT("TVTest TVCaption3");

const TCHAR ROMSOUND_ROM_ENABLED[] = TEXT("!00:!01:!02:!03:!04:!05:!06:!07:!08:!09:!10:!11:!12:!13:!14:!15:::");
const TCHAR ROMSOUND_CUST_ENABLED[] = TEXT("00:01:02:03:04:05:06:07:08:09:10:11:12:13:14:15:16:17:18");

enum {
    TIMER_ID_DONE_MOVE,
    TIMER_ID_DONE_SIZE,
    TIMER_ID_FLASHING_TEXTURE,
};

enum {
    ID_COMMAND_SWITCH_LANG,
    ID_COMMAND_SWITCH_SETTING,
};
}

CTVCaption2::CTVCaption2()
    : m_settingsIndex(0)
    , m_paintingMethod(0)
    , m_fNoBackground(false)
    , m_strokeWidth(0)
    , m_ornStrokeWidth(0)
    , m_fReplaceFullAlnum(false)
    , m_fReplaceDrcs(false)
    , m_fIgnoreSmall(false)
    , m_fInitializeSettingsDlg(false)
    , m_hwndPainting(nullptr)
    , m_hwndContainer(nullptr)
    , m_fNeedtoShow(false)
    , m_fShowLang2(false)
    , m_fProfileC(false)
    , m_pcr(0)
    , m_procCapTick(0)
    , m_fResetPat(false)
    , m_videoPid(-1)
    , m_pcrPid(-1)
    , m_caption1Pid(-1)
    , m_caption2Pid(-1)
{
    m_szFaceName[0][0] = 0;
    m_szFaceName[1][0] = 0;
    m_szFaceName[2][0] = 0;

    for (int index = 0; index < STREAM_MAX; ++index) {
        m_showFlags[index] = 0;
        m_delayTime[index] = 0;
        m_osdShowCount[index] = 0;
        m_clearPts[index] = -1;
    }
    PAT zeroPat = {};
    m_pat = zeroPat;
}

CTVCaption2::~CTVCaption2()
{
}


bool CTVCaption2::GetPluginInfo(TVTest::PluginInfo *pInfo)
{
    // プラグインの情報を返す
    pInfo->Type           = TVTest::PLUGIN_TYPE_NORMAL;
    pInfo->Flags          = TVTest::PLUGIN_FLAG_HASSETTINGS;
    pInfo->pszPluginName  = INFO_PLUGIN_NAME;
    pInfo->pszCopyright   = L"Copyright (c) 2022 xtne6f";
    pInfo->pszDescription = INFO_DESCRIPTION;
    return true;
}


// 初期化処理
bool CTVCaption2::Initialize()
{
    // ウィンドウクラスの登録
    WNDCLASS wc = {};
    wc.lpfnWndProc = PaintingWndProc;
    wc.hInstance = g_hinstDLL;
    wc.lpszClassName = TV_CAPTION2_WINDOW_CLASS;
    if (!RegisterClass(&wc)) return false;

    if (!CPseudoOSD::Initialize(g_hinstDLL)) return false;

    TCHAR path[MAX_PATH];
    if (!GetLongModuleFileName(g_hinstDLL, path, _countof(path))) return false;
    m_iniPath = path;
    size_t lastSep = m_iniPath.find_last_of(TEXT("/\\."));
    if (lastSep != tstring::npos && m_iniPath[lastSep] == TEXT('.')) {
        m_iniPath.erase(lastSep);
    }
    m_iniPath += TEXT(".ini");

    // 必要ならOsdCompositorを初期化(プラグインの有効無効とは独立)
    if (GetPrivateProfileInt(TEXT("Settings"), TEXT("EnOsdCompositor"), 0, m_iniPath.c_str()) != 0) {
        // フィルタグラフを取得できないバージョンではAPIフックを使う
        bool fSetHook = m_pApp->GetVersion() < TVTest::MakeVersion(0, 9, 0);
        if (m_osdCompositor.Initialize(fSetHook)) {
            m_pApp->AddLog(L"OsdCompositorを初期化しました。");
        }
    }

    // アイコンを登録
    m_pApp->RegisterPluginIconFromResource(g_hinstDLL, MAKEINTRESOURCE(IDB_ICON));

    // コマンドを登録
    TVTest::PluginCommandInfo ciList[2];
    ciList[0].ID = ID_COMMAND_SWITCH_LANG;
    ciList[0].State = TVTest::PLUGIN_COMMAND_STATE_DISABLED;
    ciList[0].pszText = L"SwitchLang";
    ciList[0].pszName = L"字幕言語切り替え";
    ciList[0].hbmIcon = static_cast<HBITMAP>(LoadImage(g_hinstDLL, MAKEINTRESOURCE(IDB_SWITCH_LANG), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    ciList[1].ID = ID_COMMAND_SWITCH_SETTING;
    ciList[1].State = TVTest::PLUGIN_COMMAND_STATE_DISABLED;
    ciList[1].pszText = L"SwitchSetting";
    ciList[1].pszName = L"表示設定切り替え";
    ciList[1].hbmIcon = static_cast<HBITMAP>(LoadImage(g_hinstDLL, MAKEINTRESOURCE(IDB_SWITCH_SETTING), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    for (int i = 0; i < _countof(ciList); ++i) {
        ciList[i].Size = sizeof(ciList[0]);
        ciList[i].Flags = ciList[i].hbmIcon ? TVTest::PLUGIN_COMMAND_FLAG_ICONIZE : 0;
        ciList[i].pszDescription = ciList[i].pszName;
        if (!m_pApp->RegisterPluginCommand(&ciList[i])) {
            m_pApp->RegisterCommand(ciList[i].ID, ciList[i].pszText, ciList[i].pszName);
        }
        if (ciList[i].hbmIcon) {
            DeleteObject(ciList[i].hbmIcon);
        }
    }

    // イベントコールバック関数を登録
    m_pApp->SetEventCallback(EventCallback, this);
    return true;
}


// 終了処理
bool CTVCaption2::Finalize()
{
    if (m_pApp->IsPluginEnabled()) EnablePlugin(false);
    m_osdCompositor.Uninitialize();
    return true;
}


// TVTestのフルスクリーンHWNDを取得する
// 必ず取得できると仮定してはいけない
HWND CTVCaption2::GetFullscreenWindow()
{
    TVTest::HostInfo hostInfo;
    if (m_pApp->GetFullscreen() && m_pApp->GetHostInfo(&hostInfo)) {
        TCHAR className[64];
        _tcsncpy_s(className, hostInfo.pszAppName, 47);
        _tcscat_s(className, TEXT(" Fullscreen"));

        HWND hwnd = nullptr;
        while ((hwnd = FindWindowEx(nullptr, hwnd, className, nullptr)) != nullptr) {
            DWORD pid;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == GetCurrentProcessId()) return hwnd;
        }
    }
    return nullptr;
}


namespace
{
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    std::pair<HWND, LPCTSTR> *params = reinterpret_cast<std::pair<HWND, LPCTSTR>*>(lParam);
    TCHAR className[64];
    if (GetClassName(hwnd, className, _countof(className)) && !_tcscmp(className, params->second)) {
        // 見つかった
        params->first = hwnd;
        return FALSE;
    }
    return TRUE;
}
}


// TVTestのVideo Containerウィンドウを探す
// 必ず取得できると仮定してはいけない
HWND CTVCaption2::FindVideoContainer()
{
    std::pair<HWND, LPCTSTR> params(nullptr, nullptr);
    TVTest::HostInfo hostInfo;
    if (m_pApp->GetHostInfo(&hostInfo)) {
        TCHAR searchName[64];
        _tcsncpy_s(searchName, hostInfo.pszAppName, 31);
        _tcscat_s(searchName, TEXT(" Video Container"));

        params.second = searchName;
        HWND hwndFull = GetFullscreenWindow();
        EnumChildWindows(hwndFull ? hwndFull : m_pApp->GetAppWindow(), EnumWindowsProc, reinterpret_cast<LPARAM>(&params));
    }
    return params.first;
}


// Video Containerウィンドウ上の映像の位置を得る
bool CTVCaption2::GetVideoContainerLayout(HWND hwndContainer, RECT *pRect, RECT *pVideoRect, RECT *pExVideoRect)
{
    RECT rc;
    if (!hwndContainer || !GetClientRect(hwndContainer, &rc)) {
        return false;
    }
    if (pRect) {
        *pRect = rc;
    }
    if (pVideoRect || pExVideoRect) {
        RECT rcPadding;
        if (!pVideoRect) pVideoRect = &rcPadding;
        if (!pExVideoRect) pExVideoRect = &rcPadding;

        // 正確に取得する術はなさそうなので中央に配置されていると仮定
        int aspectX = 16;
        int aspectY = 9;
        double cropX = 1;
        TVTest::VideoInfo vi;
        if (m_pApp->GetVideoInfo(&vi)) {
            if (vi.Height == 480 && vi.YAspect * 4 == 3 * vi.XAspect) {
                // 4:3SDを特別扱い(アスペクト比情報はいまいち正確でない可能性があるのであまり信用しない)(参考up0511mod)
                aspectX = 4;
                aspectY = 3;
            }
            if (vi.Width > 0) {
                cropX = (double)(vi.SourceRect.right - vi.SourceRect.left) / vi.Width;
                // Y方向ははみ出しやすいので考慮しない
            }
        }
        if (cropX == 0 || aspectX * cropX * rc.bottom < aspectY * rc.right) {
            // ウィンドウが動画よりもワイド
            pVideoRect->left = (rc.right - (int)(aspectX * cropX * rc.bottom / aspectY)) / 2;
            pVideoRect->right = rc.right - pVideoRect->left;
            pVideoRect->top = 0;
            pVideoRect->bottom = rc.bottom;
        }
        else {
            pVideoRect->top = (rc.bottom - (int)(aspectY * rc.right / (aspectX * cropX))) / 2;
            pVideoRect->bottom = rc.bottom - pVideoRect->top;
            pVideoRect->left = 0;
            pVideoRect->right = rc.right;
        }
        // カットした分だけ拡げる(ウィンドウからはみ出す場合もある)
        *pExVideoRect = *pVideoRect;
        if (cropX != 0) {
            int x = (int)((pExVideoRect->right - pExVideoRect->left) * (1 - cropX) / cropX) / 2;
            pExVideoRect->left -= x;
            pExVideoRect->right += x;
        }
    }
    return true;
}


// 字幕の表示方法に応じて映像サイズまたはGetVideoContainerLayout()の結果を得る
bool CTVCaption2::GetVideoSurfaceRect(HWND hwndContainer, RECT *pVideoRect, RECT *pExVideoRect)
{
    if (m_paintingMethod == 3) {
        RECT rc;
        if (m_osdCompositor.GetSurfaceRect(&rc)) {
            // 位置はわからないが正確な映像サイズを得た
            if (pVideoRect) *pVideoRect = rc;
            if (pExVideoRect) *pExVideoRect = rc;
            return true;
        }
    }
    return GetVideoContainerLayout(hwndContainer, nullptr, pVideoRect, pExVideoRect);
}


// 映像PIDを取得する(無い場合は-1)
// プラグインAPIが内部でストリームをロックするので、デッドロックを完成させないように注意
int CTVCaption2::GetVideoPid()
{
    int index = m_pApp->GetService();
    TVTest::ServiceInfo si;
    if (index >= 0 && m_pApp->GetServiceInfo(index, &si) && si.VideoPID != 0) {
        return si.VideoPID;
    }
    return -1;
}


// プラグインの有効状態が変化した
bool CTVCaption2::EnablePlugin(bool fEnable)
{
    if (fEnable) {
        // 設定の読み込み
        LoadSettings();

        m_fShowLang2 = false;
        m_fProfileC = false;
        if (ResetCaptionContext(STREAM_CAPTION) &&
            ResetCaptionContext(STREAM_SUPERIMPOSE))
        {
            m_caption1Pes.second.clear();
            m_caption2Pes.second.clear();
            m_caption1PesQueue.clear();
            m_caption2PesQueue.clear();

            // 字幕描画処理ウィンドウ作成
            m_hwndPainting = CreateWindow(TV_CAPTION2_WINDOW_CLASS, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hinstDLL, this);
            if (m_hwndPainting) {
                m_videoPid = GetVideoPid();
                m_fResetPat = true;

                // コールバックの登録
                m_pApp->SetStreamCallback(0, StreamCallback, this);
                m_pApp->SetWindowMessageCallback(WindowMsgCallback, this);

                m_pApp->SetPluginCommandState(ID_COMMAND_SWITCH_LANG, 0);
                m_pApp->SetPluginCommandState(ID_COMMAND_SWITCH_SETTING, 0);
                return true;
            }
        }
        return false;
    }
    else {
        // コールバックの登録解除
        m_pApp->SetWindowMessageCallback(nullptr, nullptr);
        m_pApp->SetStreamCallback(TVTest::STREAM_CALLBACK_REMOVE, StreamCallback);

        // 字幕描画ウィンドウの破棄
        if (m_hwndPainting) {
            DestroyWindow(m_hwndPainting);
            m_hwndPainting = nullptr;
        }
        // 内蔵音再生停止
        PlaySound(nullptr, nullptr, 0);

        for (int index = 0; index < STREAM_MAX; ++index) {
            m_captionRenderer[index] = nullptr;
            m_captionDecoder[index] = nullptr;
            m_captionContext[index] = nullptr;
        }

        m_pApp->SetPluginCommandState(ID_COMMAND_SWITCH_LANG, TVTest::PLUGIN_COMMAND_STATE_DISABLED);
        m_pApp->SetPluginCommandState(ID_COMMAND_SWITCH_SETTING, TVTest::PLUGIN_COMMAND_STATE_DISABLED);
        return true;
    }
}


// 設定の読み込み
void CTVCaption2::LoadSettings()
{
    std::vector<TCHAR> vbuf = GetPrivateProfileSectionBuffer(TEXT("Settings"), m_iniPath.c_str());
    TCHAR val[SETTING_VALUE_MAX];
    m_settingsIndex = GetBufferedProfileInt(vbuf.data(), TEXT("SettingsIndex"), 0);

    // ここからはセクション固有
    if (m_settingsIndex > 0) {
        TCHAR section[32];
        _stprintf_s(section, TEXT("Settings%d"), m_settingsIndex);
        vbuf = GetPrivateProfileSectionBuffer(section, m_iniPath.c_str());
    }
    LPCTSTR buf = vbuf.data();
    GetBufferedProfileString(buf, TEXT("FaceName"), TEXT(""), m_szFaceName[0], _countof(m_szFaceName[0]));
    GetBufferedProfileString(buf, TEXT("FaceName1"), TEXT(""), m_szFaceName[1], _countof(m_szFaceName[1]));
    GetBufferedProfileString(buf, TEXT("FaceName2"), TEXT(""), m_szFaceName[2], _countof(m_szFaceName[2]));
    m_paintingMethod    = GetBufferedProfileInt(buf, TEXT("Method"), 2);
    m_showFlags[STREAM_CAPTION]     = GetBufferedProfileInt(buf, TEXT("ShowFlags"), 65535);
    m_showFlags[STREAM_SUPERIMPOSE] = GetBufferedProfileInt(buf, TEXT("ShowFlagsSuper"), 65535);
    m_delayTime[STREAM_CAPTION]     = GetBufferedProfileInt(buf, TEXT("DelayTime"), 450);
    m_delayTime[STREAM_SUPERIMPOSE] = GetBufferedProfileInt(buf, TEXT("DelayTimeSuper"), 0);
    m_fNoBackground     = GetBufferedProfileInt(buf, TEXT("NoBackground"), 0) != 0;
    m_strokeWidth       = GetBufferedProfileInt(buf, TEXT("StrokeWidth"), 30);
    m_ornStrokeWidth    = GetBufferedProfileInt(buf, TEXT("OrnStrokeWidth"), 50);
    m_fReplaceFullAlnum = GetBufferedProfileInt(buf, TEXT("ReplaceFullAlnum"), 0) != 0;
    m_fReplaceDrcs      = GetBufferedProfileInt(buf, TEXT("ReplaceDrcs"), 0) != 0;
    m_fIgnoreSmall      = GetBufferedProfileInt(buf, TEXT("IgnoreSmall"), 0) != 0;
    GetBufferedProfileString(buf, TEXT("RomSoundList"), TEXT(""), val, _countof(val));
    m_romSoundList = val;
}


// 設定の保存
void CTVCaption2::SaveSettings() const
{
    TCHAR section[32] = TEXT("Settings");
    WritePrivateProfileInt(section, TEXT("SettingsIndex"), m_settingsIndex, m_iniPath.c_str());

    // ここからはセクション固有
    if (m_settingsIndex > 0) {
        _stprintf_s(section, TEXT("Settings%d"), m_settingsIndex);
    }
    WritePrivateProfileString(section, TEXT("FaceName"), m_szFaceName[0], m_iniPath.c_str());
    WritePrivateProfileString(section, TEXT("FaceName1"), m_szFaceName[1], m_iniPath.c_str());
    WritePrivateProfileString(section, TEXT("FaceName2"), m_szFaceName[2], m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("Method"), m_paintingMethod, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("ShowFlags"), m_showFlags[STREAM_CAPTION], m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("ShowFlagsSuper"), m_showFlags[STREAM_SUPERIMPOSE], m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("DelayTime"), m_delayTime[STREAM_CAPTION], m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("DelayTimeSuper"), m_delayTime[STREAM_SUPERIMPOSE], m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("NoBackground"), m_fNoBackground, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("StrokeWidth"), m_strokeWidth, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("OrnStrokeWidth"), m_ornStrokeWidth, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("ReplaceFullAlnum"), m_fReplaceFullAlnum, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("ReplaceDrcs"), m_fReplaceDrcs, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("IgnoreSmall"), m_fIgnoreSmall, m_iniPath.c_str());
    WritePrivateProfileString(section, TEXT("RomSoundList"), m_romSoundList.c_str(), m_iniPath.c_str());
}


// 設定の切り替え
// specIndex<0のとき次の設定に切り替え
void CTVCaption2::SwitchSettings(int specIndex)
{
    m_settingsIndex = specIndex < 0 ? m_settingsIndex + 1 : specIndex;
    TCHAR section[32];
    _stprintf_s(section, TEXT("Settings%d"), m_settingsIndex);
    if (GetPrivateProfileInt(section, TEXT("Method"), 0, m_iniPath.c_str()) == 0) {
        m_settingsIndex = 0;
    }
    WritePrivateProfileInt(TEXT("Settings"), TEXT("SettingsIndex"), m_settingsIndex, m_iniPath.c_str());
    LoadSettings();
}


void CTVCaption2::AddSettings()
{
    WritePrivateProfileInt(TEXT("Settings"), TEXT("SettingsIndex"), GetSettingsCount(), m_iniPath.c_str());
    LoadSettings();
    SaveSettings();
}


void CTVCaption2::DeleteSettings()
{
    int settingsCount = GetSettingsCount();
    if (settingsCount >= 2) {
        int lastIndex = m_settingsIndex;
        // 1つずつ手前にシフト
        for (int i = m_settingsIndex; i < settingsCount - 1; ++i) {
            WritePrivateProfileInt(TEXT("Settings"), TEXT("SettingsIndex"), i + 1, m_iniPath.c_str());
            LoadSettings();
            m_settingsIndex = i;
            SaveSettings();
        }
        // 末尾セクションを削除
        TCHAR section[32];
        _stprintf_s(section, TEXT("Settings%d"), settingsCount - 1);
        WritePrivateProfileString(section, nullptr, nullptr, m_iniPath.c_str());
        SwitchSettings(min(lastIndex, settingsCount - 2));
    }
}


int CTVCaption2::GetSettingsCount() const
{
    for (int i = 1; ; ++i) {
        TCHAR section[32];
        _stprintf_s(section, TEXT("Settings%d"), i);
        if (GetPrivateProfileInt(section, TEXT("Method"), 0, m_iniPath.c_str()) == 0) {
            return i;
        }
    }
}


// 内蔵音再生する
bool CTVCaption2::PlayRomSound(int index) const
{
    if (index < 0 || m_romSoundList.empty() || m_romSoundList[0] == TEXT(';')) return false;

    size_t i = 0;
    size_t j = m_romSoundList.find(TEXT(':'));
    for (; index > 0 && j != tstring::npos; --index) {
        i = j + 1;
        j = m_romSoundList.find(TEXT(':'), i);
    }
    if (index>0) return false;

    tstring id(m_romSoundList, i, j - i);

    if (!id.empty() && id[0] == TEXT('!')) {
        // 定義済みのサウンド
        if (id.size() == 3) {
            LPCTSTR romFound = _tcsstr(ROMSOUND_ROM_ENABLED, id.c_str());
            if (romFound) {
                // 組み込みサウンド
                size_t romIndex = (romFound - ROMSOUND_ROM_ENABLED) / 4;
                // 今のところ2～4は組み込んでいないので1とみなす
                if (2 <= romIndex && romIndex <= 4) {
                    romIndex = 1;
                }
                if (romIndex <= 13) {
                    return PlaySound(MAKEINTRESOURCE(IDW_ROM_00 + romIndex), g_hinstDLL,
                                     SND_ASYNC | SND_NODEFAULT | SND_RESOURCE) != FALSE;
                }
                return false;
            }
        }
        return PlaySound(&id.c_str()[1], nullptr, SND_ASYNC | SND_NODEFAULT | SND_ALIAS) != FALSE;
    }
    else if (!id.empty()) {
        tstring path = m_iniPath.substr(0, m_iniPath.rfind(TEXT('.'))) + TEXT('\\') + id + TEXT(".wav");
        return PlaySound(path.c_str(), nullptr, SND_ASYNC | SND_NODEFAULT | SND_FILENAME) != FALSE;
    }
    return false;
}


// イベントコールバック関数
// 何かイベントが起きると呼ばれる
LRESULT CALLBACK CTVCaption2::EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData)
{
    static_cast<void>(lParam2);
    CTVCaption2 *pThis = static_cast<CTVCaption2*>(pClientData);

    switch (Event) {
    case TVTest::EVENT_PLUGINENABLE:
        // プラグインの有効状態が変化した
        if (pThis->EnablePlugin(lParam1 != 0)) {
            pThis->PlayRomSound(lParam1 != 0 ? 17 : 18);
            return TRUE;
        }
        return 0;
    case TVTest::EVENT_PLUGINSETTINGS:
        // プラグインの設定を行う
        return pThis->PluginSettings(reinterpret_cast<HWND>(lParam1));
    case TVTest::EVENT_FULLSCREENCHANGE:
        // 全画面表示状態が変化した
        if (pThis->m_pApp->IsPluginEnabled()) {
            // オーナーが変わるので破棄する必要がある
            SendMessage(pThis->m_hwndPainting, WM_APP_RESET_OSDS, 0, 0);
        }
        break;
    case TVTest::EVENT_CHANNELCHANGE:
        // チャンネルが変更された
    case TVTest::EVENT_SERVICECHANGE:
        // サービスが変更された
    case TVTest::EVENT_SERVICEUPDATE:
        // サービスの構成が変化した
        if (pThis->m_pApp->IsPluginEnabled()) {
            SendMessage(pThis->m_hwndPainting, WM_APP_RESET_CAPTION, 0, 0);
            pThis->m_videoPid = pThis->GetVideoPid();
            pThis->m_fResetPat = true;
        }
        break;
    case TVTest::EVENT_PREVIEWCHANGE:
        // プレビュー表示状態が変化した
        if (pThis->m_pApp->IsPluginEnabled()) {
            if (lParam1 != 0) {
                pThis->m_fNeedtoShow = true;
            }
            else {
                pThis->HideAllOsds();
                pThis->m_fNeedtoShow = false;
            }
        }
        break;
    case TVTest::EVENT_COMMAND:
        // コマンドが選択された
        if (pThis->m_pApp->IsPluginEnabled()) {
            switch (static_cast<int>(lParam1)) {
            case ID_COMMAND_SWITCH_LANG:
                pThis->m_fShowLang2 = !pThis->m_fShowLang2;
                SendMessage(pThis->m_hwndPainting, WM_APP_RESET_CAPTION, 0, 0);
                pThis->m_pApp->SetPluginCommandState(ID_COMMAND_SWITCH_LANG, pThis->m_fShowLang2 ? TVTest::PLUGIN_COMMAND_STATE_CHECKED : 0);
                {
                    TCHAR str[32];
                    _stprintf_s(str, TEXT("第%d言語に切り替えました。"), pThis->m_fShowLang2 ? 2 : 1);
                    pThis->m_pApp->AddLog(str);
                }
                break;
            case ID_COMMAND_SWITCH_SETTING:
                pThis->HideAllOsds();
                pThis->SwitchSettings();
                pThis->PlayRomSound(16);
                break;
            }
        }
        return TRUE;
    case TVTest::EVENT_FILTERGRAPH_INITIALIZED:
        // フィルタグラフの初期化終了
        pThis->m_osdCompositor.OnFilterGraphInitialized(reinterpret_cast<const TVTest::FilterGraphInfo*>(lParam1)->pGraphBuilder);
        break;
    case TVTest::EVENT_FILTERGRAPH_FINALIZE:
        // フィルタグラフの終了処理開始
        pThis->m_osdCompositor.OnFilterGraphFinalize(reinterpret_cast<const TVTest::FilterGraphInfo*>(lParam1)->pGraphBuilder);
        break;
    }
    return 0;
}


// ウィンドウメッセージコールバック関数
// TRUEを返すとTVTest側でメッセージを処理しなくなる
// WM_CREATEは呼ばれない
BOOL CALLBACK CTVCaption2::WindowMsgCallback(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pResult, void *pUserData)
{
    static_cast<void>(hwnd);
    static_cast<void>(wParam);
    static_cast<void>(lParam);
    static_cast<void>(pResult);
    CTVCaption2 *pThis = static_cast<CTVCaption2*>(pUserData);

    switch (uMsg) {
    case WM_MOVE:
        SendMessage(pThis->m_hwndPainting, WM_APP_DONE_MOVE, 0, 0);
        SetTimer(pThis->m_hwndPainting, TIMER_ID_DONE_MOVE, 500, nullptr);
        break;
    case WM_SIZE:
        SendMessage(pThis->m_hwndPainting, WM_APP_DONE_SIZE, 0, 0);
        SetTimer(pThis->m_hwndPainting, TIMER_ID_DONE_SIZE, 500, nullptr);
        break;
    }
    return FALSE;
}


void CTVCaption2::HideOsds(int index, size_t osdPrepareCount)
{
    DEBUG_OUT(TEXT(__FUNCTION__) TEXT("()\n"));

    for (; m_osdShowCount[index] > 0; --m_osdShowCount[index]) {
        m_pOsdList[index].front()->Hide();
        // 表示待ちOSDの直後に移動
        for (size_t i = 0; i < m_osdShowCount[index] + osdPrepareCount - 1; ++i) {
            m_pOsdList[index][i].swap(m_pOsdList[index][i + 1]);
        }
    }
}

void CTVCaption2::DeleteTextures()
{
    if (m_paintingMethod == 3) {
        if (m_osdCompositor.DeleteTexture(0, 0)) {
            m_osdCompositor.UpdateSurface();
        }
    }
}

void CTVCaption2::HideAllOsds()
{
    for (int index = 0; index < STREAM_MAX; ++index) {
        HideOsds(index);
        m_clearPts[index] = -1;
    }
    DeleteTextures();
}

void CTVCaption2::DestroyOsds()
{
    DEBUG_OUT(TEXT(__FUNCTION__) TEXT("()\n"));

    for (int index = 0; index < STREAM_MAX; ++index) {
        m_pOsdList[index].clear();
        m_osdShowCount[index] = 0;
        m_clearPts[index] = -1;
    }
    DeleteTextures();
}


// 字幕描画のウィンドウプロシージャ
LRESULT CALLBACK CTVCaption2::PaintingWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // WM_CREATEのとき不定
    CTVCaption2 *pThis = reinterpret_cast<CTVCaption2*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_CREATE:
        {
            LPCREATESTRUCT pcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            pThis = reinterpret_cast<CTVCaption2*>(pcs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            PostMessage(hwnd, WM_APP_RESET_OSDS, 0, 0);
            pThis->m_fNeedtoShow = pThis->m_pApp->GetPreview();
        }
        return 0;
    case WM_DESTROY:
        pThis->DestroyOsds();
        return 0;
    case WM_TIMER:
        switch (wParam) {
        case TIMER_ID_DONE_MOVE:
            KillTimer(hwnd, TIMER_ID_DONE_MOVE);
            SendMessage(hwnd, WM_APP_DONE_MOVE, 0, 0);
            return 0;
        case TIMER_ID_DONE_SIZE:
            KillTimer(hwnd, TIMER_ID_DONE_SIZE);
            SendMessage(hwnd, WM_APP_DONE_SIZE, 0, 0);
            return 0;
        }
        break;
    case WM_APP_RESET_CAPTION:
        pThis->HideAllOsds();
        {
            lock_recursive_mutex lock(pThis->m_streamLock);
            if (wParam != 0) {
                pThis->m_fProfileC = wParam != 1;
            }
            pThis->m_caption1Pes.second.clear();
            pThis->m_caption2Pes.second.clear();
            pThis->m_caption1PesQueue.clear();
            pThis->m_caption2PesQueue.clear();
        }
        for (int index = 0; index < STREAM_MAX; ++index) {
            pThis->m_captionDecoder[index]->SwitchLanguage(pThis->m_fShowLang2 ? aribcaption::LanguageId::kSecond :
                                                                                 aribcaption::LanguageId::kFirst);
            pThis->m_captionDecoder[index]->SetProfile(pThis->m_fProfileC ? aribcaption::Profile::kProfileC :
                                                                            aribcaption::Profile::kProfileA);
        }
        return 0;
    case WM_APP_PROCESS_CAPTION:
        pThis->ProcessCaption(pThis->m_caption1PesQueue);
        pThis->ProcessCaption(pThis->m_caption2PesQueue);
        return 0;
    case WM_APP_DONE_MOVE:
        for (int index = 0; index < STREAM_MAX; ++index) {
            for (size_t i = 0; i < pThis->m_osdShowCount[index]; ++i) {
                pThis->m_pOsdList[index][i]->OnParentMove();
            }
        }
        return 0;
    case WM_APP_DONE_SIZE:
        pThis->OnSize(STREAM_CAPTION);
        pThis->OnSize(STREAM_SUPERIMPOSE);
        return 0;
    case WM_APP_RESET_OSDS:
        DEBUG_OUT(TEXT(__FUNCTION__) TEXT("(): WM_APP_RESET_OSDS\n"));
        pThis->DestroyOsds();
        pThis->m_hwndContainer = pThis->FindVideoContainer();
        if (pThis->m_hwndContainer) {
            if (pThis->m_paintingMethod == 2) {
                for (size_t i = 0; i < OSD_PRE_CREATE_NUM; ++i) {
                    pThis->m_pOsdList[STREAM_CAPTION].push_back(std::unique_ptr<CPseudoOSD>(new CPseudoOSD));
                    pThis->m_pOsdList[STREAM_CAPTION].back()->Create(pThis->m_hwndContainer, g_hinstDLL);
                }
            }
            pThis->m_osdCompositor.SetContainerWindow(pThis->m_hwndContainer);
        }
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


bool CTVCaption2::ResetCaptionContext(STREAM_INDEX index)
{
    auto context = std::make_unique<aribcaption::Context>();
    context->SetLogcatCallback([this](aribcaption::LogLevel level, const char *message) {
        TCHAR log[256];
        std::copy(message, message + min(strlen(message) + 1, _countof(log)), log);
        m_pApp->AddLog(log, level == aribcaption::LogLevel::kError ? TVTest::LOG_TYPE_ERROR :
                            level == aribcaption::LogLevel::kWarning ? TVTest::LOG_TYPE_WARNING : TVTest::LOG_TYPE_INFORMATION);
    });

    auto decoder = std::make_unique<aribcaption::Decoder>(*context);
    if (!decoder->Initialize(aribcaption::EncodingScheme::kAuto,
                             index == STREAM_CAPTION ? aribcaption::CaptionType::kCaption :
                                                       aribcaption::CaptionType::kSuperimpose))
    {
        return false;
    }

    auto renderer = std::make_unique<aribcaption::Renderer>(*context);
    if (!renderer->Initialize(index == STREAM_CAPTION ? aribcaption::CaptionType::kCaption :
                                                        aribcaption::CaptionType::kSuperimpose))
    {
        return false;
    }

    // 各種の描画オプションを適用
    std::vector<std::string> fontFamily;
    for (size_t i = 0; i < _countof(m_szFaceName); ++i) {
        if (m_szFaceName[i][0]) {
            fontFamily.push_back(aribcaption::wchar::WideStringToUTF8(m_szFaceName[i]));
        }
    }
    if (!fontFamily.empty()) {
        renderer->SetLanguageSpecificFontFamily(aribcaption::ThreeCC("jpn"), fontFamily);
    }
    renderer->SetForceNoBackground(m_fNoBackground);
    renderer->SetForceStrokeText(m_strokeWidth > 0);
    renderer->SetStrokeWidth(static_cast<float>((m_strokeWidth > 0 ? m_strokeWidth : m_ornStrokeWidth) / 10.0));
    renderer->SetReplaceDRCS(m_fReplaceDrcs);
    renderer->SetForceNoRuby(m_fIgnoreSmall);
    decoder->SetReplaceMSZFullWidthAlphanumeric(m_fReplaceFullAlnum);
    decoder->SwitchLanguage(m_fShowLang2 ? aribcaption::LanguageId::kSecond : aribcaption::LanguageId::kFirst);
    decoder->SetProfile(m_fProfileC ? aribcaption::Profile::kProfileC : aribcaption::Profile::kProfileA);

    m_captionContext[index].swap(context);
    m_captionDecoder[index].swap(decoder);
    m_captionRenderer[index].swap(renderer);
    return true;
}


void CTVCaption2::ProcessCaption(std::vector<std::vector<BYTE>> &streamPesQueue)
{
    bool fTextureModified = false;

    for (int index = 0; index < STREAM_MAX; ++index) {
        if (m_clearPts[index] >= 0) {
            LONGLONG shiftPcr;
            {
                lock_recursive_mutex lock(m_streamLock);
                shiftPcr = (0x200000000 + m_pcr - min(max(m_delayTime[index], -5000), 5000) * PCR_PER_MSEC) & 0x1ffffffff;
            }
            LONGLONG ptsPcrDiff = (0x200000000 + m_clearPts[index] - shiftPcr) & 0x1ffffffff;
            // 期限付き字幕を消去
            if (ptsPcrDiff >= 0x100000000) {
                HideOsds(index);
                m_clearPts[index] = -1;
                if (m_paintingMethod == 3) {
                    if (m_osdCompositor.DeleteTexture(0, index + 1)) {
                        fTextureModified = true;
                    }
                }
            }
        }
    }

    for (;;) {
        std::vector<BYTE> pes;
        LONGLONG pts = 0;
        int payloadPos = 0;
        STREAM_INDEX index = STREAM_CAPTION;
        {
            lock_recursive_mutex lock(m_streamLock);

            if (streamPesQueue.empty()) {
                break;
            }
            // 次のPESを取得する
            auto it = streamPesQueue.begin();
            PES_HEADER pesHeader;
            extract_pes_header(&pesHeader, it->data(), static_cast<int>(it->size()));
            if (pesHeader.packet_start_code_prefix) {
                LONGLONG shiftPcr = (0x200000000 + m_pcr - min(max(m_delayTime[index], -5000), 5000) * PCR_PER_MSEC) & 0x1ffffffff;
                if (pesHeader.stream_id == 0xbf) {
                    // 文字スーパー
                    pes.swap(*it);
                    // 文字スーパーは非同期PESなので字幕文取得時のPCRが表示タイミングの基準になる
                    pts = shiftPcr;
                    payloadPos = pesHeader.pes_payload_pos;
                    index = STREAM_SUPERIMPOSE;
                }
                else if (pesHeader.pts_dts_flags >= 2) {
                    // 字幕
                    LONGLONG ptsPcrDiff = (0x200000000 + pesHeader.pts - shiftPcr) & 0x1ffffffff;
                    if (ptsPcrDiff < 10000 * PCR_PER_MSEC) {
                        // 表示タイミングまで待つ
                        break;
                    }
                    // 表示タイミングから大きくずれているものは無視
                    if (ptsPcrDiff > 0x200000000 - 10000 * PCR_PER_MSEC) {
                        pes.swap(*it);
                        pts = pesHeader.pts;
                        payloadPos = pesHeader.pes_payload_pos;
                    }
                }
            }
            streamPesQueue.erase(it);
        }
        if (pes.empty() || payloadPos >= static_cast<int>(pes.size()) || !m_fNeedtoShow) {
            continue;
        }

        aribcaption::DecodeResult decodeResult;
        auto status = aribcaption::DecodeStatus::kError;
        try {
            status = m_captionDecoder[index]->Decode(pes.data() + payloadPos, pes.size() - payloadPos, pts, decodeResult);
        }
        catch (...) {
            m_pApp->AddLog(TEXT("aribcaption::Decode() raised exception"), TVTest::LOG_TYPE_ERROR);
            ResetCaptionContext(index);
        }

        RECT rcVideo;
        if (status == aribcaption::DecodeStatus::kGotCaption &&
            GetVideoSurfaceRect(m_hwndContainer, &rcVideo))
        {
            bool fHideOsds = false;
            if (decodeResult.caption->flags & aribcaption::CaptionFlags::kCaptionFlagsClearScreen) {
                // 消去
                fHideOsds = true;
                m_clearPts[index] = -1;
                if (m_osdCompositor.DeleteTexture(0, index + 1)) {
                    fTextureModified = true;
                }
            }
            if ((decodeResult.caption->flags & aribcaption::CaptionFlags::kCaptionFlagsWaitDuration) &&
                decodeResult.caption->wait_duration != aribcaption::DURATION_INDEFINITE)
            {
                // 期限付き
                m_clearPts[index] = (pts + decodeResult.caption->wait_duration * PCR_PER_MSEC) & 0x1ffffffff;
            }
            if (decodeResult.caption->has_builtin_sound) {
                if (m_showFlags[index] != 0) {
                    PlayRomSound(decodeResult.caption->builtin_sound_id);
                }
            }

            m_captionRenderer[index]->SetFrameSize(rcVideo.right - rcVideo.left, rcVideo.bottom - rcVideo.top);
            m_captionRenderer[index]->AppendCaption(std::move(*decodeResult.caption));

            size_t osdPrepareCount = 0;
            aribcaption::RenderResult renderResult;
            auto renderStatus = aribcaption::RenderStatus::kError;
            try {
                if (m_showFlags[index] != 0) {
                    renderStatus = m_captionRenderer[index]->Render(pts, renderResult);
                }
            }
            catch (...) {
                m_pApp->AddLog(TEXT("aribcaption::Render() raised exception"), TVTest::LOG_TYPE_ERROR);
                ResetCaptionContext(index);
            }

            if (renderStatus == aribcaption::RenderStatus::kGotImage) {
                // 追加
                for (auto it = renderResult.images.begin(); it != renderResult.images.end(); ++it) {
                    if (it->width > 0 && it->height > 0 && it->pixel_format == aribcaption::PixelFormat::kRGBA8888) {
                        void *pBits;
                        BITMAPINFO bmi = {};
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = it->width;
                        bmi.bmiHeader.biHeight = it->height;
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;
                        HBITMAP hbm = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
                        if (hbm) {
                            for (int y = 0; y < it->height; ++y) {
                                auto jt = it->bitmap.begin() + it->stride * y;
                                BYTE *p = static_cast<BYTE*>(pBits) + 4 * it->width * (it->height - y - 1);
                                BYTE *pEnd = static_cast<BYTE*>(pBits) + 4 * it->width * (it->height - y);
                                while (p != pEnd) {
                                    p[2] = *(jt++);
                                    p[1] = *(jt++);
                                    p[0] = *(jt++);
                                    p[3] = *(jt++);
                                    p += 4;
                                }
                            }

                            if (m_paintingMethod == 2) {
                                if (m_osdShowCount[index] + osdPrepareCount >= m_pOsdList[index].size()) {
                                    // OSDが足りない
                                    if (m_pOsdList[index].size() >= OSD_MAX_CREATE_NUM) {
                                        // 最前列から奪う
                                        if (m_osdShowCount[index] > 0) {
                                            --m_osdShowCount[index];
                                        }
                                        else {
                                            --osdPrepareCount;
                                        }
                                        m_pOsdList[index].front()->Hide();
                                        for (size_t i = 0; i < m_pOsdList[index].size() - 1; ++i) {
                                            m_pOsdList[index][i].swap(m_pOsdList[index][i + 1]);
                                        }
                                    }
                                    else {
                                        // 作成
                                        m_pOsdList[index].push_back(std::unique_ptr<CPseudoOSD>(new CPseudoOSD));
                                    }
                                }
                                CPseudoOSD &osd = *m_pOsdList[index][m_osdShowCount[index] + osdPrepareCount++];
                                osd.Create(m_hwndContainer, g_hinstDLL);
                                osd.SetImage(hbm, it->dst_x + rcVideo.left, it->dst_y + rcVideo.top, it->width, it->height);
                            }
                            else {
                                if (m_paintingMethod == 3) {
                                    if (m_osdCompositor.AddTexture(hbm, it->dst_x, it->dst_y, true, index + 1)) {
                                        fTextureModified = true;
                                    }
                                }
                                DeleteObject(hbm);
                            }
                        }
                    }
                }
            }

            // ちらつき防止のため表示処理をまとめる
            if (fHideOsds) {
                HideOsds(index, osdPrepareCount);
            }
            for (; osdPrepareCount > 0; --osdPrepareCount, ++m_osdShowCount[index]) {
                m_pOsdList[index][m_osdShowCount[index]]->Show();
            }
        }
    }
    if (fTextureModified) {
        m_osdCompositor.UpdateSurface();
    }
}


void CTVCaption2::OnSize(STREAM_INDEX index)
{
    if (m_paintingMethod != 3 && m_osdShowCount[index] > 0) {
        RECT rc;
        if (GetVideoContainerLayout(m_hwndContainer, &rc)) {
            // とりあえずはみ出ないようにする
            for (size_t i = 0; i < m_osdShowCount[index]; ++i) {
                int left, top, width, height;
                m_pOsdList[index][i]->GetPosition(&left, &top, &width, &height);
                int adjLeft = left + width >= rc.right ? rc.right - width : left;
                int adjTop = top + height >= rc.bottom ? rc.bottom - height : top;
                if (adjLeft < 0 || adjTop < 0) {
                    m_pOsdList[index][i]->Hide();
                }
                else if (left != adjLeft || top != adjTop) {
                    m_pOsdList[index][i]->SetPosition(adjLeft, adjTop, width, height);
                }
            }
        }
    }
}


// ストリームコールバック(別スレッド)
BOOL CALLBACK CTVCaption2::StreamCallback(BYTE *pData, void *pClientData)
{
    static_cast<CTVCaption2*>(pClientData)->ProcessPacket(pData);
    return TRUE;
}


namespace
{
void GetPidsFromVideoPmt(int *pPmtPid, int *pPcrPid, int *pCaption1Pid, int *pCaption2Pid, int videoPid, const PAT *pPat)
{
    for (size_t i = 0; i < pPat->pmt.size(); ++i) {
        const PMT *pPmt = &pPat->pmt[i];
        // このPMTに映像PIDが含まれるか調べる
        bool fVideoPmt = false;
        int privPid[2] = {-1, -1};
        for (int j = 0; j < pPmt->pid_count; ++j) {
            if (pPmt->stream_type[j] == PES_PRIVATE_DATA) {
                int ct = pPmt->component_tag[j];
                if (ct == 0x30 || ct == 0x87) {
                    privPid[0] = pPmt->pid[j];
                }
                else if (ct == 0x38 || ct == 0x88) {
                    privPid[1] = pPmt->pid[j];
                }
            }
            else if (pPmt->pid[j] == videoPid) {
                fVideoPmt = true;
            }
        }
        if (fVideoPmt) {
            *pPmtPid = pPat->pmt[i].pmt_pid;
            *pPcrPid = pPmt->pcr_pid;
            *pCaption1Pid = privPid[0];
            *pCaption2Pid = privPid[1];
            return;
        }
    }
    *pPmtPid = -1;
    *pPcrPid = -1;
    *pCaption1Pid = -1;
    *pCaption2Pid = -1;
}
}


void CTVCaption2::ProcessPacket(BYTE *pPacket)
{
    if (m_fResetPat) {
        PAT zeroPat = {};
        m_pat = zeroPat;
        m_pcrPid = -1;
        m_caption1Pid = -1;
        m_caption2Pid = -1;
        m_fResetPat = false;
    }

    if (extract_ts_header_error_indicator(pPacket)) {
        return;
    }
    int unitStart = extract_ts_header_unit_start(pPacket);
    int pid = extract_ts_header_pid(pPacket);
    int scr = extract_ts_header_scrambling_control(pPacket);
    int adaptation = extract_ts_header_adaptation(pPacket);
    int counter = extract_ts_header_counter(pPacket);
    int payloadSize = get_ts_payload_size(pPacket);
    const BYTE *pPayload = pPacket + 188 - payloadSize;

    // PCRを取得
    if (adaptation & 2) {
        // アダプテーションフィールドがある
        int adaptationLength = pPacket[4];
        if (adaptationLength >= 6 && !!(pPacket[5] & 0x10)) {
            // 参照PIDのときはPCRを取得する
            if (pid == m_pcrPid) {
                lock_recursive_mutex lock(m_streamLock);
                LONGLONG pcr = (pPacket[10] >> 7) |
                               (pPacket[9] << 1) |
                               (pPacket[8] << 9) |
                               (pPacket[7] << 17) |
                               (static_cast<LONGLONG>(pPacket[6]) << 25);

                // PCRの連続性チェック
                if (((0x200000000 + pcr - m_pcr) & 0x1ffffffff) >= 1000 * PCR_PER_MSEC) {
                    SendNotifyMessage(m_hwndPainting, WM_APP_RESET_CAPTION, 0, 0);
                }
                m_pcr = pcr;

                // ある程度呼び出し頻度を抑える(感覚的に遅延を感じなければOK)
                DWORD tick = GetTickCount();
                if (tick - m_procCapTick >= 100) {
                    SendNotifyMessage(m_hwndPainting, WM_APP_PROCESS_CAPTION, 0, 0);
                    m_procCapTick = tick;
                }
            }
        }
    }

    // 字幕か文字スーパーとPCRのPIDを取得
    if (!scr && payloadSize > 0) {
        // PAT監視
        int pmtPid;
        if (pid == 0) {
            extract_pat(&m_pat, pPayload, payloadSize, unitStart, counter);
            GetPidsFromVideoPmt(&pmtPid, &m_pcrPid, &m_caption1Pid, &m_caption2Pid, m_videoPid, &m_pat);
            if (pmtPid >= 0 && Is1SegPmtPid(pmtPid) != m_fProfileC) {
                SendNotifyMessage(m_hwndPainting, WM_APP_RESET_CAPTION, Is1SegPmtPid(pmtPid) ? 2 : 1, 0);
            }
            return;
        }
        // PATリストにあるPMT監視
        for (size_t i = 0; i < m_pat.pmt.size(); ++i) {
            if (pid == m_pat.pmt[i].pmt_pid/* && pid != 0*/) {
                extract_pmt(&m_pat.pmt[i], pPayload, payloadSize, unitStart, counter);
                GetPidsFromVideoPmt(&pmtPid, &m_pcrPid, &m_caption1Pid, &m_caption2Pid, m_videoPid, &m_pat);
                if (pmtPid >= 0 && Is1SegPmtPid(pmtPid) != m_fProfileC) {
                    SendNotifyMessage(m_hwndPainting, WM_APP_RESET_CAPTION, Is1SegPmtPid(pmtPid) ? 2 : 1, 0);
                }
                return;
            }
        }
    }

    // 字幕か文字スーパーのストリームを取得
    if (!scr && (pid == m_caption1Pid || pid == m_caption2Pid)) {
        lock_recursive_mutex lock(m_streamLock);

        std::pair<int, std::vector<BYTE>> &pesPair = pid == m_caption1Pid ? m_caption1Pes : m_caption2Pes;
        int &pesCounter = pesPair.first;
        std::vector<BYTE> &pes = pesPair.second;
        if (unitStart) {
            pesCounter = counter;
            pes.assign(pPayload, pPayload + payloadSize);
        }
        else if (!pes.empty()) {
            pesCounter = (pesCounter + 1) & 0x0f;
            if (pesCounter == counter) {
                pes.insert(pes.end(), pPayload, pPayload + payloadSize);
            }
            else {
                // 次のPESヘッダまで無視
                pes.clear();
            }
        }
        if (pes.size() >= 6) {
            size_t pesPacketLength = (pes[4] << 8) | pes[5];
            if (pes.size() >= 6 + pesPacketLength) {
                // PESが蓄積された
                pes.resize(6 + pesPacketLength);
                (pid == m_caption1Pid ? m_caption1PesQueue : m_caption2PesQueue).push_back(pes);
                SendNotifyMessage(m_hwndPainting, WM_APP_PROCESS_CAPTION, 0, 0);
                pes.clear();
            }
        }
    }
}


// プラグインの設定を行う
bool CTVCaption2::PluginSettings(HWND hwndOwner)
{
    if (!m_pApp->IsPluginEnabled()) {
        MessageBox(hwndOwner, TEXT("プラグインを有効にしてください。"), INFO_PLUGIN_NAME, MB_ICONERROR | MB_OK);
        return false;
    }
    if (m_pApp->QueryMessage(TVTest::MESSAGE_SHOWDIALOG)) {
        TVTest::ShowDialogInfo info;
        info.Flags = 0;
        info.hinst = g_hinstDLL;
        info.pszTemplate = MAKEINTRESOURCE(IDD_OPTIONS);
        info.pMessageFunc = TVTestSettingsDlgProc;
        info.pClientData = this;
        info.hwndOwner = hwndOwner;
        m_pApp->ShowDialog(&info);
    }
    else {
        DialogBoxParam(g_hinstDLL, MAKEINTRESOURCE(IDD_OPTIONS), hwndOwner, SettingsDlgProc, reinterpret_cast<LPARAM>(this));
    }
    return true;
}


// 設定ダイアログプロシージャ
INT_PTR CALLBACK CTVCaption2::SettingsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG) {
        SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
    }
    CTVCaption2 *pThis = reinterpret_cast<CTVCaption2*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
    return pThis ? pThis->ProcessSettingsDlg(hDlg, uMsg, wParam, lParam) : FALSE;
}


// プラグインAPI経由の設定ダイアログプロシージャ
INT_PTR CALLBACK CTVCaption2::TVTestSettingsDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam, void *pClientData)
{
    return static_cast<CTVCaption2*>(pClientData)->ProcessSettingsDlg(hDlg, uMsg, wParam, lParam);
}


void CTVCaption2::InitializeSettingsDlg(HWND hDlg)
{
    m_fInitializeSettingsDlg = true;

    CheckDlgButton(hDlg, IDC_CHECK_OSD,
        GetPrivateProfileInt(TEXT("Settings"), TEXT("EnOsdCompositor"), 0, m_iniPath.c_str()) != 0 ? BST_CHECKED : BST_UNCHECKED);

    int settingsCount = GetSettingsCount();
    SendDlgItemMessage(hDlg, IDC_COMBO_SETTINGS, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < settingsCount; ++i) {
        TCHAR text[32];
        _stprintf_s(text, TEXT("設定%d"), i);
        SendDlgItemMessage(hDlg, IDC_COMBO_SETTINGS, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    }
    SendDlgItemMessage(hDlg, IDC_COMBO_SETTINGS, CB_SETCURSEL, m_settingsIndex, 0);

    for (int i = 0; i < 3; ++i) {
        HWND hItem = GetDlgItem(hDlg, IDC_COMBO_FACE + i);
        SendMessage(hItem, CB_RESETCONTENT, 0, 0);
        SendMessage(hItem, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(TEXT(" (指定なし)")));
        AddFaceNameToComboBoxList(hDlg, IDC_COMBO_FACE + i);
        if (!m_szFaceName[i][0]) {
            SendMessage(hItem, CB_SETCURSEL, 0, 0);
        }
        else if (SendMessage(hItem, CB_SELECTSTRING, 0, reinterpret_cast<LPARAM>(m_szFaceName[i])) == CB_ERR) {
            SendMessage(hItem, CB_SETCURSEL, SendMessage(hItem, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m_szFaceName[i])), 0);
        }
    }

    static const LPCTSTR METHOD_LIST[] = { TEXT("2-レイヤードウィンドウ"), TEXT("3-映像と合成"), nullptr };
    SendDlgItemMessage(hDlg, IDC_COMBO_METHOD, CB_RESETCONTENT, 0, 0);
    AddToComboBoxList(hDlg, IDC_COMBO_METHOD, METHOD_LIST);
    SendDlgItemMessage(hDlg, IDC_COMBO_METHOD, CB_SETCURSEL, min(max(m_paintingMethod - 2, 0), 1), 0);

    CheckDlgButton(hDlg, IDC_CHECK_CAPTION, m_showFlags[STREAM_CAPTION] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_SUPERIMPOSE, m_showFlags[STREAM_SUPERIMPOSE] ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(hDlg, IDC_EDIT_DELAY, m_delayTime[STREAM_CAPTION], TRUE);
    CheckDlgButton(hDlg, IDC_CHECK_NO_BACKGROUND, m_fNoBackground ? BST_CHECKED : BST_UNCHECKED);

    SetDlgItemInt(hDlg, IDC_EDIT_STROKE_WIDTH, m_strokeWidth, FALSE);
    SetDlgItemInt(hDlg, IDC_EDIT_ORN_STROKE_WIDTH, m_ornStrokeWidth, FALSE);

    CheckDlgButton(hDlg, IDC_CHECK_REPLACE_FULL_ALNUM, m_fReplaceFullAlnum ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_REPLACE_DRCS, m_fReplaceDrcs ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_IGNORE_SMALL, m_fIgnoreSmall ? BST_CHECKED : BST_UNCHECKED);
    bool fCheckRomSound = !m_romSoundList.empty() && m_romSoundList[0] != TEXT(';');
    CheckDlgButton(hDlg, IDC_CHECK_ROMSOUND, fCheckRomSound ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(hDlg, IDC_CHECK_CUST_ROMSOUND), fCheckRomSound);
    CheckDlgButton(hDlg, IDC_CHECK_CUST_ROMSOUND, fCheckRomSound && m_romSoundList != ROMSOUND_ROM_ENABLED ? BST_CHECKED : BST_UNCHECKED);

    m_fInitializeSettingsDlg = false;
}


INT_PTR CTVCaption2::ProcessSettingsDlg(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static_cast<void>(lParam);
    bool fSave = false;
    bool fReDisp = false;

    switch (uMsg) {
    case WM_INITDIALOG:
        InitializeSettingsDlg(hDlg);
        return TRUE;
    case WM_COMMAND:
        // 初期化中の無駄な再帰を省く
        if (m_fInitializeSettingsDlg) {
            break;
        }
        switch (LOWORD(wParam)) {
        case IDOK:
        case IDCANCEL:
            HideAllOsds();
            EndDialog(hDlg, LOWORD(wParam));
            break;
        case IDC_CHECK_OSD:
            WritePrivateProfileInt(TEXT("Settings"), TEXT("EnOsdCompositor"),
                IsDlgButtonChecked(hDlg, IDC_CHECK_OSD) != BST_UNCHECKED, m_iniPath.c_str());
            break;
        case IDC_COMBO_SETTINGS:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                HideAllOsds();
                SwitchSettings(static_cast<int>(SendDlgItemMessage(hDlg, IDC_COMBO_SETTINGS, CB_GETCURSEL, 0, 0)));
                InitializeSettingsDlg(hDlg);
                fReDisp = true;
            }
            break;
        case IDC_SETTINGS_ADD:
            HideAllOsds();
            AddSettings();
            InitializeSettingsDlg(hDlg);
            fReDisp = true;
            break;
        case IDC_SETTINGS_DELETE:
            HideAllOsds();
            DeleteSettings();
            InitializeSettingsDlg(hDlg);
            fReDisp = true;
            break;
        case IDC_COMBO_FACE:
        case IDC_COMBO_FACE1:
        case IDC_COMBO_FACE2:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int i = LOWORD(wParam) - IDC_COMBO_FACE;
                GetDlgItemText(hDlg, IDC_COMBO_FACE + i, m_szFaceName[i], _countof(m_szFaceName[0]));
                if (m_szFaceName[i][0] == TEXT(' ') && m_szFaceName[i][1] == TEXT('(')) {
                    m_szFaceName[i][0] = 0;
                }
                fSave = fReDisp = true;
            }
            break;
        case IDC_COMBO_METHOD:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                HideAllOsds();
                m_paintingMethod = static_cast<int>(SendDlgItemMessage(hDlg, IDC_COMBO_METHOD, CB_GETCURSEL, 0, 0));
                m_paintingMethod = min(max(m_paintingMethod, 0), 1) + 2;
                fSave = fReDisp = true;
            }
            break;
        case IDC_CHECK_CAPTION:
            HideAllOsds();
            m_showFlags[STREAM_CAPTION] = IsDlgButtonChecked(hDlg, IDC_CHECK_CAPTION) != BST_UNCHECKED ? 65535 : 0;
            fSave = fReDisp = true;
            break;
        case IDC_CHECK_SUPERIMPOSE:
            HideAllOsds();
            m_showFlags[STREAM_SUPERIMPOSE] = IsDlgButtonChecked(hDlg, IDC_CHECK_SUPERIMPOSE) != BST_UNCHECKED ? 65535 : 0;
            fSave = fReDisp = true;
            break;
        case IDC_EDIT_DELAY:
            if (HIWORD(wParam) == EN_CHANGE) {
                m_delayTime[STREAM_CAPTION] = GetDlgItemInt(hDlg, IDC_EDIT_DELAY, nullptr, TRUE);
                fSave = true;
            }
            break;
        case IDC_CHECK_NO_BACKGROUND:
            m_fNoBackground = IsDlgButtonChecked(hDlg, IDC_CHECK_NO_BACKGROUND) != BST_UNCHECKED;
            fSave = fReDisp = true;
            break;
        case IDC_EDIT_STROKE_WIDTH:
        case IDC_EDIT_ORN_STROKE_WIDTH:
            if (HIWORD(wParam) == EN_CHANGE) {
                m_strokeWidth = GetDlgItemInt(hDlg, IDC_EDIT_STROKE_WIDTH, nullptr, FALSE);
                m_ornStrokeWidth = GetDlgItemInt(hDlg, IDC_EDIT_ORN_STROKE_WIDTH, nullptr, FALSE);
                fSave = fReDisp = true;
            }
            break;
        case IDC_CHECK_REPLACE_FULL_ALNUM:
            m_fReplaceFullAlnum = IsDlgButtonChecked(hDlg, IDC_CHECK_REPLACE_FULL_ALNUM) != BST_UNCHECKED;
            fSave = fReDisp = true;
            break;
        case IDC_CHECK_REPLACE_DRCS:
            m_fReplaceDrcs = IsDlgButtonChecked(hDlg, IDC_CHECK_REPLACE_DRCS) != BST_UNCHECKED;
            fSave = fReDisp = true;
            break;
        case IDC_CHECK_IGNORE_SMALL:
            m_fIgnoreSmall = IsDlgButtonChecked(hDlg, IDC_CHECK_IGNORE_SMALL) != BST_UNCHECKED;
            fSave = fReDisp = true;
            break;
        case IDC_CHECK_ROMSOUND:
            EnableWindow(GetDlgItem(hDlg, IDC_CHECK_CUST_ROMSOUND),
                         IsDlgButtonChecked(hDlg, IDC_CHECK_ROMSOUND) != BST_UNCHECKED);
            // FALL THROUGH!
        case IDC_CHECK_CUST_ROMSOUND:
            m_romSoundList = IsDlgButtonChecked(hDlg, IDC_CHECK_ROMSOUND) == BST_UNCHECKED ? TEXT("") :
                             IsDlgButtonChecked(hDlg, IDC_CHECK_CUST_ROMSOUND) != BST_UNCHECKED ? ROMSOUND_CUST_ENABLED :
                             ROMSOUND_ROM_ENABLED;
            fSave = true;
            break;
        default:
            return FALSE;
        }
        if (fSave) {
            SaveSettings();
        }
        if (fReDisp) {
            HideAllOsds();
            ResetCaptionContext(STREAM_CAPTION);
            ResetCaptionContext(STREAM_SUPERIMPOSE);
        }
        return TRUE;
    }
    return FALSE;
}


TVTest::CTVTestPlugin *CreatePluginClass()
{
    return new CTVCaption2;
}
