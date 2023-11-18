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
const UINT WM_APP_RESET_OSDS = WM_APP + 4;

const TCHAR INFO_PLUGIN_NAME[] = TEXT("TVCaption3");
const TCHAR INFO_DESCRIPTION[] = TEXT("字幕を表示 (ver.1.1)");
const int INFO_VERSION = 1;
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
    ID_COMMAND_CAPTURE,
    ID_COMMAND_SAVE,
};
}

CTVCaption2::CTVCaption2()
    : m_jpegQuality(0)
    , m_pngCompressionLevel(0)
    , m_settingsIndex(0)
    , m_paintingMethod(0)
    , m_fFreeType(false)
    , m_fNoBackground(false)
    , m_strokeWidth(0)
    , m_ornStrokeWidth(0)
    , m_fReplaceFullAlnum(false)
    , m_fReplaceFullJapanese(false)
    , m_fReplaceDrcs(false)
    , m_fIgnoreSmall(false)
    , m_fInitializeSettingsDlg(false)
    , m_hTVTestImage(nullptr)
    , m_hwndPainting(nullptr)
    , m_hwndContainer(nullptr)
    , m_fNeedtoShow(false)
    , m_fShowLang2(false)
    , m_fProfileC(false)
    , m_hFreetypeDll(nullptr)
    , m_procCapTick(0)
    , m_fResetPat(false)
    , m_videoPid(-1)
    , m_pcrPid(-1)
    , m_caption1Pid(-1)
    , m_caption2Pid(-1)
{
    m_szCaptureSaveFormat[0] = 0;

    for (int index = 0; index < STREAM_MAX; ++index) {
        m_showFlags[index] = 0;
        m_delayTime[index] = 0;
        m_osdShowCount[index] = 0;
        m_clearPts[index] = -1;
        m_renderedPts[index] = -1;
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
    TVTest::PluginCommandInfo ciList[4];
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
    ciList[2].ID = ID_COMMAND_CAPTURE;
    ciList[2].State = 0;
    ciList[2].pszText = L"Capture";
    ciList[2].pszName = L"字幕付き画像のコピー";
    ciList[2].hbmIcon = nullptr;
    ciList[3].ID = ID_COMMAND_SAVE;
    ciList[3].State = 0;
    ciList[3].pszText = L"Save";
    ciList[3].pszName = L"字幕付き画像の保存";
    ciList[3].hbmIcon = nullptr;
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
                m_pApp->SetVideoStreamCallback(VideoStreamCallback, this);
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
        m_pApp->SetVideoStreamCallback(nullptr);
        m_pApp->SetStreamCallback(TVTest::STREAM_CALLBACK_REMOVE, StreamCallback);

        // 字幕描画ウィンドウの破棄
        if (m_hwndPainting) {
            DestroyWindow(m_hwndPainting);
            m_hwndPainting = nullptr;
        }
        // 内蔵音再生停止
        PlaySound(nullptr, nullptr, 0);

        if (m_hTVTestImage) {
            FreeLibrary(m_hTVTestImage);
            m_hTVTestImage = nullptr;
        }

        for (int index = 0; index < STREAM_MAX; ++index) {
            m_captionRenderer[index] = nullptr;
            m_captionDecoder[index] = nullptr;
            m_captionContext[index] = nullptr;
        }
        if (m_hFreetypeDll) {
            FreeLibrary(m_hFreetypeDll);
            m_hFreetypeDll = nullptr;
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
    int iniVer = GetBufferedProfileInt(vbuf.data(), TEXT("Version"), 0);
    m_viewerClockEstimator.SetEnabled(GetBufferedProfileInt(vbuf.data(), TEXT("EstimateViewerDelay"), 1) != 0);
    GetBufferedProfileString(vbuf.data(), TEXT("CaptureFolder"), TEXT(""), val, _countof(val));
    m_captureFolder = val;
    GetBufferedProfileString(vbuf.data(), TEXT("CaptureFileName"), TEXT("Capture"), val, _countof(val));
    m_captureFileName = val;
    GetBufferedProfileString(vbuf.data(), TEXT("CaptureFileNameFormat"), TEXT(""), val, _countof(val));
    m_captureFileNameFormat = val;
    GetBufferedProfileString(vbuf.data(), TEXT("CaptureSaveFormat"), TEXT("BMP"), m_szCaptureSaveFormat, _countof(m_szCaptureSaveFormat));
    m_jpegQuality = GetBufferedProfileInt(vbuf.data(), TEXT("JpegQuality"), 90);
    m_pngCompressionLevel = GetBufferedProfileInt(vbuf.data(), TEXT("PngCompressionLevel"), 6);
    m_settingsIndex = GetBufferedProfileInt(vbuf.data(), TEXT("SettingsIndex"), 0);

    // ここからはセクション固有
    if (m_settingsIndex > 0) {
        TCHAR section[32];
        _stprintf_s(section, TEXT("Settings%d"), m_settingsIndex);
        vbuf = GetPrivateProfileSectionBuffer(section, m_iniPath.c_str());
    }
    LPCTSTR buf = vbuf.data();
    GetBufferedProfileString(buf, TEXT("FaceName"), TEXT(""), val, _countof(val));
    m_faceName[0] = val;
    GetBufferedProfileString(buf, TEXT("FaceName1"), TEXT(""), val, _countof(val));
    m_faceName[1] = val;
    GetBufferedProfileString(buf, TEXT("FaceName2"), TEXT(""), val, _countof(val));
    m_faceName[2] = val;
    m_paintingMethod    = GetBufferedProfileInt(buf, TEXT("Method"), 2);
    m_fFreeType         = GetBufferedProfileInt(buf, TEXT("FreeType"), 0) != 0;
    m_showFlags[STREAM_CAPTION]     = GetBufferedProfileInt(buf, TEXT("ShowFlags"), 65535);
    m_showFlags[STREAM_SUPERIMPOSE] = GetBufferedProfileInt(buf, TEXT("ShowFlagsSuper"), 65535);
    m_delayTime[STREAM_CAPTION]     = GetBufferedProfileInt(buf, TEXT("DelayTime"), 450);
    m_delayTime[STREAM_SUPERIMPOSE] = GetBufferedProfileInt(buf, TEXT("DelayTimeSuper"), 0);
    m_fNoBackground     = GetBufferedProfileInt(buf, TEXT("NoBackground"), 0) != 0;
    m_strokeWidth       = GetBufferedProfileInt(buf, TEXT("StrokeWidth"), 30);
    m_ornStrokeWidth    = GetBufferedProfileInt(buf, TEXT("OrnStrokeWidth"), 50);
    m_fReplaceFullAlnum = GetBufferedProfileInt(buf, TEXT("ReplaceFullAlnum"), 1) != 0;
    m_fReplaceFullJapanese = GetBufferedProfileInt(buf, TEXT("ReplaceFullJapanese"), 1) != 0;
    m_fReplaceDrcs      = GetBufferedProfileInt(buf, TEXT("ReplaceDrcs"), 0) != 0;
    m_fIgnoreSmall      = GetBufferedProfileInt(buf, TEXT("IgnoreSmall"), 0) != 0;
    GetBufferedProfileString(buf, TEXT("RomSoundList"), TEXT(""), val, _countof(val));
    m_romSoundList = val;

    if (iniVer < INFO_VERSION) {
        // デフォルトの設定キーを出力するため
        SaveSettings();
    }
}


// 設定の保存
void CTVCaption2::SaveSettings() const
{
    TCHAR section[32] = TEXT("Settings");
    WritePrivateProfileInt(section, TEXT("Version"), INFO_VERSION, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("EstimateViewerDelay"), m_viewerClockEstimator.GetEnabled(), m_iniPath.c_str());
    WritePrivateProfileString(section, TEXT("CaptureFolder"), m_captureFolder.c_str(), m_iniPath.c_str());
    WritePrivateProfileString(section, TEXT("CaptureFileName"), m_captureFileName.c_str(), m_iniPath.c_str());
    WritePrivateProfileString(section, TEXT("CaptureFileNameFormat"), m_captureFileNameFormat.c_str(), m_iniPath.c_str());
    WritePrivateProfileString(section, TEXT("CaptureSaveFormat"), m_szCaptureSaveFormat, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("JpegQuality"), m_jpegQuality, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("PngCompressionLevel"), m_pngCompressionLevel, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("SettingsIndex"), m_settingsIndex, m_iniPath.c_str());

    // ここからはセクション固有
    if (m_settingsIndex > 0) {
        _stprintf_s(section, TEXT("Settings%d"), m_settingsIndex);
    }
    WritePrivateProfileString(section, TEXT("FaceName"), m_faceName[0].c_str(), m_iniPath.c_str());
    WritePrivateProfileString(section, TEXT("FaceName1"), m_faceName[1].c_str(), m_iniPath.c_str());
    WritePrivateProfileString(section, TEXT("FaceName2"), m_faceName[2].c_str(), m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("Method"), m_paintingMethod, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("FreeType"), m_fFreeType, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("ShowFlags"), m_showFlags[STREAM_CAPTION], m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("ShowFlagsSuper"), m_showFlags[STREAM_SUPERIMPOSE], m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("DelayTime"), m_delayTime[STREAM_CAPTION], m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("DelayTimeSuper"), m_delayTime[STREAM_SUPERIMPOSE], m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("NoBackground"), m_fNoBackground, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("StrokeWidth"), m_strokeWidth, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("OrnStrokeWidth"), m_ornStrokeWidth, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("ReplaceFullAlnum"), m_fReplaceFullAlnum, m_iniPath.c_str());
    WritePrivateProfileInt(section, TEXT("ReplaceFullJapanese"), m_fReplaceFullJapanese, m_iniPath.c_str());
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
            SetTimer(pThis->m_hwndPainting, TIMER_ID_DONE_SIZE, 500, nullptr);
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
                pThis->ResetCaptionContext(STREAM_CAPTION);
                pThis->ResetCaptionContext(STREAM_SUPERIMPOSE);
                pThis->PlayRomSound(16);
                break;
            case ID_COMMAND_CAPTURE:
                pThis->OnCapture(false);
                break;
            case ID_COMMAND_SAVE:
                pThis->OnCapture(true);
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


void CTVCaption2::OnCapture(bool fSaveToFile)
{
    void *pPackDib = m_pApp->CaptureImage();
    if (!pPackDib) return;

    BITMAPINFOHEADER *pBih = static_cast<BITMAPINFOHEADER*>(pPackDib);
    if (pBih->biWidth > 0 && pBih->biHeight > 0 && (pBih->biBitCount == 24 || pBih->biBitCount == 32)) {
        // 常に24bitビットマップにする
        BITMAPINFOHEADER bih = *pBih;
        bih.biBitCount = 24;
        void *pBits;
        HBITMAP hbm = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (hbm) {
            BYTE *pBitmap = static_cast<BYTE*>(pPackDib) + sizeof(BITMAPINFOHEADER);
            if (pBih->biBitCount == 24) {
                SetDIBits(nullptr, hbm, 0, pBih->biHeight, pBitmap, reinterpret_cast<BITMAPINFO*>(pBih), DIB_RGB_COLORS);
            }
            else {
                // 32-24bit変換
                for (int y = 0; y < bih.biHeight; ++y) {
                    BYTE *pDest = static_cast<BYTE*>(pBits) + (bih.biWidth * 3 + 3) / 4 * 4 * y;
                    BYTE *pSrc = pBitmap + bih.biWidth * 4 * y;
                    for (int x = 0; x < bih.biWidth; ++x) {
                        memcpy(&pDest[3 * x], &pSrc[4 * x], 3);
                    }
                }
            }

            RECT rcVideo;
            if (GetVideoContainerLayout(m_hwndContainer, nullptr, &rcVideo)) {
                BITMAPINFOHEADER bihRes = bih;
                bihRes.biWidth = rcVideo.right - rcVideo.left;
                bihRes.biHeight = rcVideo.bottom - rcVideo.top;
                // キャプチャ画像が表示中の動画サイズと異なるときは動画サイズのビットマップに変換する
                if (bih.biWidth < bihRes.biWidth - 3 || bihRes.biWidth + 3 < bih.biWidth ||
                    bih.biHeight < bihRes.biHeight - 3 || bihRes.biHeight + 3 < bih.biHeight)
                {
                    void *pBitsRes;
                    HBITMAP hbmRes = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bihRes), DIB_RGB_COLORS, &pBitsRes, nullptr, 0);
                    if (hbmRes) {
                        HDC hdc = CreateCompatibleDC(nullptr);
                        if (hdc) {
                            HBITMAP hbmResOld = static_cast<HBITMAP>(SelectObject(hdc, hbmRes));
                            HDC hdcSrc = CreateCompatibleDC(hdc);
                            if (hdcSrc) {
                                HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcSrc, hbm));
                                int oldStretchMode = SetStretchBltMode(hdc, STRETCH_HALFTONE);
                                StretchBlt(hdc, 0, 0, bihRes.biWidth, bihRes.biHeight, hdcSrc, 0, 0, bih.biWidth, bih.biHeight, SRCCOPY);
                                SetStretchBltMode(hdc, oldStretchMode);
                                SelectObject(hdcSrc, hbmOld);
                                DeleteDC(hdcSrc);
                                DeleteObject(hbm);
                                hbm = hbmRes;
                                bih = bihRes;
                                pBits = pBitsRes;
                                hbmRes = nullptr;
                            }
                            SelectObject(hdc, hbmResOld);
                            DeleteDC(hdc);
                        }
                        if (hbmRes) {
                            DeleteObject(hbmRes);
                        }
                    }
                }

                // ビットマップに表示中のOSDを合成
                HDC hdc = CreateCompatibleDC(nullptr);
                if (hdc) {
                    HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdc, hbm));
                    for (int i = 0; i < STREAM_MAX; ++i) {
                        for (size_t j = 0; j < m_osdShowCount[i]; ++j) {
                            int left, top;
                            m_pOsdList[i][j]->GetPosition(&left, &top, nullptr, nullptr);
                            m_pOsdList[i][j]->Compose(hdc, left - rcVideo.left, top - rcVideo.top);
                        }
                    }
                    SelectObject(hdc, hbmOld);
                    DeleteDC(hdc);
                }
            }

            GdiFlush();
            int sizeImage = (bih.biWidth * 3 + 3) / 4 * 4 * bih.biHeight;
            if (fSaveToFile) {
                // ファイルに保存
                if (!m_captureFolder.empty()) {
                    LPCTSTR sep = m_captureFolder.back() == TEXT('/') || m_captureFolder.back() == TEXT('\\') ? TEXT("") : TEXT("\\");
                    tstring fileName;
                    if (!m_captureFileNameFormat.empty()) {
                        // 現在のコンテキストでファイル名をフォーマット
                        TVTest::VarStringFormatInfo info = {};
                        info.Flags = TVTest::VAR_STRING_FORMAT_FLAG_FILENAME;
                        info.pszFormat = m_captureFileNameFormat.c_str();
                        if (m_pApp->FormatVarString(&info)) {
                            fileName = info.pszResult;
                            m_pApp->MemoryFree(info.pszResult);
                        }
                    }
                    if (fileName.empty()) {
                        SYSTEMTIME st;
                        GetLocalTime(&st);
                        TCHAR t[64];
                        _stprintf_s(t, TEXT("%d%02d%02d-%02d%02d%02d"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                        fileName = m_captureFileName + t;
                    }
                    LPCTSTR ext = !_tcsicmp(m_szCaptureSaveFormat, TEXT("JPEG")) ? TEXT(".jpg") :
                                  !_tcsicmp(m_szCaptureSaveFormat, TEXT("PNG")) ? TEXT(".png") : TEXT(".bmp");
                    TCHAR suffix[4] = {};
                    bool fSaved = false;
                    for (int i = 1; i <= 30; ++i) {
                        // ファイルがなければ書きこむ
                        tstring path = m_captureFolder + sep + fileName + suffix + ext;
                        if (GetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                            if (ext[1] == TEXT('b')) {
                                fSaved = SaveImageAsBmp(path.c_str(), bih, pBits);
                            }
                            else {
                                if (!m_hTVTestImage) {
                                    m_hTVTestImage = LoadLibrary(TEXT("TVTest_Image.dll"));
                                }
                                if (!m_hTVTestImage) {
                                    m_pApp->AddLog(L"TVTest_Image.dllの読み込みに失敗しました。");
                                }
                                else {
                                    fSaved = SaveImageAsPngOrJpeg(m_hTVTestImage, path.c_str(), ext[1] == TEXT('p'),
                                                                  ext[1] == TEXT('p') ? m_pngCompressionLevel : m_jpegQuality, bih, pBits);
                                }
                            }
                            break;
                        }
                        _stprintf_s(suffix, TEXT("-%d"), i);
                    }
                    if (!fSaved) {
                        m_pApp->AddLog(L"字幕付き画像の保存に失敗しました。");
                    }
                }
            }
            else if (OpenClipboard(m_hwndPainting)) {
                // クリップボードにコピー
                if (EmptyClipboard()) {
                    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + sizeImage);
                    if (hg) {
                        BYTE *pClip = reinterpret_cast<BYTE*>(GlobalLock(hg));
                        if (pClip) {
                            memcpy(pClip, &bih, sizeof(BITMAPINFOHEADER));
                            memcpy(pClip + sizeof(BITMAPINFOHEADER), pBits, sizeImage);
                            GlobalUnlock(hg);
                            if (!SetClipboardData(CF_DIB, hg)) GlobalFree(hg);
                        }
                        else GlobalFree(hg);
                    }
                }
                CloseClipboard();
            }
            DeleteObject(hbm);
        }
    }
    m_pApp->MemoryFree(pPackDib);
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
        pThis->OnSize(STREAM_CAPTION, true);
        pThis->OnSize(STREAM_SUPERIMPOSE, true);
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
    if (m_osdCompositor.DeleteTexture(0, 0)) {
        m_osdCompositor.UpdateSurface();
    }
}

void CTVCaption2::HideAllOsds()
{
    for (int index = 0; index < STREAM_MAX; ++index) {
        HideOsds(index);
        m_clearPts[index] = -1;
        m_renderedPts[index] = -1;
    }
    DeleteTextures();
}

void CTVCaption2::DestroyOsds()
{
    DEBUG_OUT(TEXT(__FUNCTION__) TEXT("()\n"));

    for (int index = 0; index < STREAM_MAX; ++index) {
        m_pOsdList[index].clear();
        m_osdShowCount[index] = 0;
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
            pThis->OnSize(STREAM_CAPTION);
            pThis->OnSize(STREAM_SUPERIMPOSE);
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
        // aribcaption::Decoder::Flush()では不十分
        pThis->ResetCaptionDecoder(STREAM_CAPTION);
        pThis->ResetCaptionDecoder(STREAM_SUPERIMPOSE);
        pThis->m_captionRenderer[STREAM_CAPTION]->Flush();
        pThis->m_captionRenderer[STREAM_SUPERIMPOSE]->Flush();
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


bool CTVCaption2::ResetCaptionDecoder(STREAM_INDEX index, aribcaption::Context *context)
{
    auto decoder = std::make_unique<aribcaption::Decoder>(*(context ? context : m_captionContext[index].get()));
    if (!decoder->Initialize(aribcaption::EncodingScheme::kAuto,
                             index == STREAM_CAPTION ? aribcaption::CaptionType::kCaption :
                                                       aribcaption::CaptionType::kSuperimpose))
    {
        return false;
    }
    decoder->SetReplaceMSZFullWidthAlphanumeric(m_fReplaceFullAlnum);
    decoder->SetReplaceMSZFullWidthJapanese(m_fReplaceFullJapanese);
    decoder->SwitchLanguage(m_fShowLang2 ? aribcaption::LanguageId::kSecond : aribcaption::LanguageId::kFirst);
    decoder->SetProfile(m_fProfileC ? aribcaption::Profile::kProfileC : aribcaption::Profile::kProfileA);
    m_captionDecoder[index].swap(decoder);
    return true;
}


bool CTVCaption2::ResetCaptionContext(STREAM_INDEX index)
{
    auto context = std::make_unique<aribcaption::Context>();
    context->SetLogcatCallback([this](aribcaption::LogLevel level, const char *message) {
        TCHAR log[256];
        size_t len = min(strlen(message), _countof(log) - 1);
        std::copy(message, message + len, log);
        log[len] = 0;
        m_pApp->AddLog(log, level == aribcaption::LogLevel::kError ? TVTest::LOG_TYPE_ERROR :
                            level == aribcaption::LogLevel::kWarning ? TVTest::LOG_TYPE_WARNING : TVTest::LOG_TYPE_INFORMATION);
    });

    if (m_fFreeType) {
        if (!m_hFreetypeDll) {
            // 存在を確認しているだけ
            TCHAR path[MAX_PATH];
            if (GetLongModuleFileName(nullptr, path, _countof(path))) {
                tstring dllPath = path;
                size_t lastSep = dllPath.find_last_of(TEXT("/\\"));
                if (lastSep != tstring::npos) {
                    dllPath.replace(lastSep + 1, tstring::npos, TEXT("freetype.dll"));
                    m_hFreetypeDll = LoadLibrary(dllPath.c_str());
                }
            }
        }
        if (!m_hFreetypeDll) {
            m_pApp->AddLog(TEXT("freetype.dllが見つかりません。DirectWriteを使います。"), TVTest::LOG_TYPE_WARNING);
        }
    }

    auto renderer = std::make_unique<aribcaption::Renderer>(*context);
    if (!renderer->Initialize(index == STREAM_CAPTION ? aribcaption::CaptionType::kCaption :
                                                        aribcaption::CaptionType::kSuperimpose,
                              aribcaption::FontProviderType::kAuto,
                              m_fFreeType && m_hFreetypeDll ? aribcaption::TextRendererType::kFreetype :
                                                              aribcaption::TextRendererType::kDirectWrite))
    {
        return false;
    }

    if (!ResetCaptionDecoder(index, context.get())) {
        return false;
    }

    // 各種の描画オプションを適用
    std::vector<std::string> fontFamily;
    for (size_t i = 0; i < _countof(m_faceName); ++i) {
        if (!m_faceName[i].empty()) {
            fontFamily.push_back(aribcaption::wchar::WideStringToUTF8(m_faceName[i]));
        }
    }
    if (!fontFamily.empty()) {
        renderer->SetDefaultFontFamily(fontFamily, true);
    }
    renderer->SetForceNoBackground(m_fNoBackground);
    renderer->SetForceStrokeText(m_strokeWidth > 0);
    renderer->SetStrokeWidth(static_cast<float>((m_strokeWidth > 0 ? m_strokeWidth : m_ornStrokeWidth) / 10.0));
    renderer->SetReplaceDRCS(m_fReplaceDrcs);
    renderer->SetForceNoRuby(m_fIgnoreSmall);

    m_captionContext[index].swap(context);
    m_captionRenderer[index].swap(renderer);

    m_clearPts[index] = -1;
    m_renderedPts[index] = -1;
    return true;
}


void CTVCaption2::ProcessCaption(std::vector<std::vector<BYTE>> &streamPesQueue)
{
    for (int index = 0; index < STREAM_MAX; ++index) {
        if (m_clearPts[index] >= 0) {
            LONGLONG ptsPcrDiff = 0x100000000;
            LONGLONG pcr = m_viewerClockEstimator.GetViewerPcr();
            if (pcr >= 0) {
                LONGLONG shiftPcr = (0x200000000 + pcr + (450 - min(max(m_delayTime[index], -5000), 5000)) * PCR_PER_MSEC) & 0x1ffffffff;
                ptsPcrDiff = (0x200000000 + m_clearPts[index] - shiftPcr) & 0x1ffffffff;
            }
            // 期限付き字幕を消去
            if (ptsPcrDiff >= 0x100000000) {
                m_clearPts[index] = -1;
                m_renderedPts[index] = -1;
                RenderCaption(static_cast<STREAM_INDEX>(index), false);
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
                LONGLONG shiftPcr = 0;
                LONGLONG pcr = m_viewerClockEstimator.GetViewerPcr();
                if (pcr >= 0) {
                    shiftPcr = (0x200000000 + pcr + (450 - min(max(m_delayTime[index], -5000), 5000)) * PCR_PER_MSEC) & 0x1ffffffff;
                }
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

        if (status == aribcaption::DecodeStatus::kGotCaption) {
            if (decodeResult.caption->flags & aribcaption::CaptionFlags::kCaptionFlagsClearScreen) {
                // 消去
                m_clearPts[index] = -1;
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

            m_captionRenderer[index]->AppendCaption(std::move(*decodeResult.caption));
            m_renderedPts[index] = pts;
            RenderCaption(index, false);
        }
    }
}


void CTVCaption2::RenderCaption(STREAM_INDEX index, bool fRedraw)
{
    bool fTextureModified = false;
    bool fRenderedOrUnchanged = false;

    if (m_renderedPts[index] >= 0) {
        RECT rcVideo;
        if (GetVideoSurfaceRect(m_hwndContainer, &rcVideo) &&
            m_captionRenderer[index]->SetFrameSize(rcVideo.right - rcVideo.left, rcVideo.bottom - rcVideo.top))
        {
            bool fHideOsds = false;
            size_t osdPrepareCount = 0;
            aribcaption::RenderResult renderResult;
            auto renderStatus = aribcaption::RenderStatus::kError;
            try {
                if (m_showFlags[index] != 0) {
                    renderStatus = m_captionRenderer[index]->Render(m_renderedPts[index], renderResult);
                }
            }
            catch (...) {
                m_pApp->AddLog(TEXT("aribcaption::Render() raised exception"), TVTest::LOG_TYPE_ERROR);
                ResetCaptionContext(index);
            }

            if (renderStatus == aribcaption::RenderStatus::kNoImage ||
                renderStatus == aribcaption::RenderStatus::kGotImage ||
                renderStatus == aribcaption::RenderStatus::kGotImageUnchanged)
            {
                fRenderedOrUnchanged = true;
                if (renderStatus != aribcaption::RenderStatus::kGotImageUnchanged || fRedraw) {
                    fHideOsds = true;
                    if (m_osdCompositor.DeleteTexture(0, index + 1)) {
                        fTextureModified = true;
                    }
                }
            }
            if (renderStatus == aribcaption::RenderStatus::kGotImage ||
                (renderStatus == aribcaption::RenderStatus::kGotImageUnchanged && fRedraw))
            {
                int shiftY = 0;
                if (m_fProfileC) {
                    // そのままだと描画面の上方向に突き抜けるので底に詰める
                    shiftY = rcVideo.bottom - rcVideo.top;
                    for (auto it = renderResult.images.begin(); it != renderResult.images.end(); ++it) {
                        shiftY = std::min<int>(shiftY, rcVideo.bottom - rcVideo.top - (it->dst_y + it->height));
                    }
                }
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
                                if (m_paintingMethod == 2) {
                                    // 乗算済みにする
                                    while (p != pEnd) {
                                        BYTE r = *(jt++);
                                        BYTE g = *(jt++);
                                        BYTE b = *(jt++);
                                        BYTE a = *(jt++);
                                        *(p++) = (b * a + 255) >> 8;
                                        *(p++) = (g * a + 255) >> 8;
                                        *(p++) = (r * a + 255) >> 8;
                                        *(p++) = a;
                                    }
                                }
                                else {
                                    while (p != pEnd) {
                                        p[2] = *(jt++);
                                        p[1] = *(jt++);
                                        p[0] = *(jt++);
                                        p[3] = *(jt++);
                                        p += 4;
                                    }
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
                                osd.SetImage(hbm, pBits, it->dst_x + rcVideo.left, shiftY + it->dst_y + rcVideo.top, it->width, it->height);
                            }
                            else {
                                if (m_paintingMethod == 3) {
                                    if (m_osdCompositor.AddTexture(hbm, it->dst_x, shiftY + it->dst_y, true, index + 1)) {
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

    if (!fRenderedOrUnchanged) {
        // 消去
        HideOsds(index);
        if (m_osdCompositor.DeleteTexture(0, index + 1)) {
            fTextureModified = true;
        }
    }
    if (fTextureModified) {
        m_osdCompositor.UpdateSurface();
    }
}


void CTVCaption2::OnSize(STREAM_INDEX index, bool fFast)
{
    for (size_t i = 0; i < m_pOsdList[index].size(); ++i) {
        m_pOsdList[index][i]->OnParentSize();
    }
    // 合成時は映像と共に拡大縮小されるので頻繁な再描画を省略する
    if (!fFast || m_paintingMethod != 3) {
        // 再描画
        RenderCaption(index, true);
    }
}


// ストリームコールバック(別スレッド)
BOOL CALLBACK CTVCaption2::StreamCallback(BYTE *pData, void *pClientData)
{
    static_cast<CTVCaption2*>(pClientData)->ProcessPacket(pData);
    return TRUE;
}


// 映像ストリームのコールバック(別スレッド)
LRESULT CALLBACK CTVCaption2::VideoStreamCallback(DWORD Format, const void *pData, SIZE_T Size, void *pClientData)
{
    static_cast<CTVCaption2*>(pClientData)->m_viewerClockEstimator.SetVideoStream(true, static_cast<const BYTE*>(pData), Size);
    return 0;
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
                if (((0x200000000 + pcr - m_viewerClockEstimator.GetStreamPcr()) & 0x1ffffffff) >= 1000 * PCR_PER_MSEC) {
                    SendNotifyMessage(m_hwndPainting, WM_APP_RESET_CAPTION, 0, 0);
                    m_viewerClockEstimator.Reset();
                }
                m_viewerClockEstimator.SetStreamPcr(pcr);

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

    if (!scr && pid == m_videoPid) {
        m_viewerClockEstimator.SetVideoPes(false, !!unitStart, pPayload, payloadSize);
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

    CheckDlgButton(hDlg, IDC_CHECK_ESTIMATE_VIEWER_DELAY, m_viewerClockEstimator.GetEnabled() ? BST_CHECKED : BST_UNCHECKED);

    SetDlgItemText(hDlg, IDC_EDIT_CAPFOLDER, m_captureFolder.c_str());
    SendDlgItemMessage(hDlg, IDC_EDIT_CAPFOLDER, EM_LIMITTEXT, MAX_PATH - 1, 0);

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
        if (m_faceName[i].empty()) {
            SendMessage(hItem, CB_SETCURSEL, 0, 0);
        }
        else if (SendMessage(hItem, CB_SELECTSTRING, 0, reinterpret_cast<LPARAM>(m_faceName[i].c_str())) == CB_ERR) {
            SetWindowText(hItem, m_faceName[i].c_str());
        }
        SendMessage(hItem, EM_LIMITTEXT, SETTING_VALUE_MAX - 1, 0);
    }

    static const LPCTSTR METHOD_LIST[] = { TEXT("2-レイヤードウィンドウ"), TEXT("3-映像と合成"), nullptr };
    SendDlgItemMessage(hDlg, IDC_COMBO_METHOD, CB_RESETCONTENT, 0, 0);
    AddToComboBoxList(hDlg, IDC_COMBO_METHOD, METHOD_LIST);
    SendDlgItemMessage(hDlg, IDC_COMBO_METHOD, CB_SETCURSEL, min(max(m_paintingMethod - 2, 0), 1), 0);

    CheckDlgButton(hDlg, IDC_CHECK_FREETYPE, m_fFreeType ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_CAPTION, m_showFlags[STREAM_CAPTION] ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_SUPERIMPOSE, m_showFlags[STREAM_SUPERIMPOSE] ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(hDlg, IDC_EDIT_DELAY, m_delayTime[STREAM_CAPTION], TRUE);
    CheckDlgButton(hDlg, IDC_CHECK_NO_BACKGROUND, m_fNoBackground ? BST_CHECKED : BST_UNCHECKED);

    SetDlgItemInt(hDlg, IDC_EDIT_STROKE_WIDTH, m_strokeWidth, FALSE);
    SetDlgItemInt(hDlg, IDC_EDIT_ORN_STROKE_WIDTH, m_ornStrokeWidth, FALSE);

    CheckDlgButton(hDlg, IDC_CHECK_REPLACE_FULL_ALNUM, m_fReplaceFullAlnum ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_REPLACE_FULL_JAPANESE, m_fReplaceFullJapanese ? BST_CHECKED : BST_UNCHECKED);
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
            EndDialog(hDlg, LOWORD(wParam));
            break;
        case IDC_CHECK_OSD:
            WritePrivateProfileInt(TEXT("Settings"), TEXT("EnOsdCompositor"),
                IsDlgButtonChecked(hDlg, IDC_CHECK_OSD) != BST_UNCHECKED, m_iniPath.c_str());
            break;
        case IDC_CHECK_ESTIMATE_VIEWER_DELAY:
            m_viewerClockEstimator.SetEnabled(IsDlgButtonChecked(hDlg, IDC_CHECK_ESTIMATE_VIEWER_DELAY) != BST_UNCHECKED);
            fSave = true;
            break;
        case IDC_EDIT_CAPFOLDER:
            if (HIWORD(wParam) == EN_CHANGE) {
                TCHAR dir[MAX_PATH];
                GetDlgItemText(hDlg, IDC_EDIT_CAPFOLDER, dir, _countof(dir));
                m_captureFolder = dir;
                fSave = true;
            }
            break;
        case IDC_CAPFOLDER_BROWSE:
            {
                TCHAR dir[MAX_PATH], title[64];
                GetDlgItemText(hDlg, IDC_EDIT_CAPFOLDER, dir, _countof(dir));
                GetDlgItemText(hDlg, IDC_STATIC_CAPFOLDER, title, _countof(title));
                if (BrowseFolderDialog(hDlg, dir, title)) {
                    SetDlgItemText(hDlg, IDC_EDIT_CAPFOLDER, dir);
                }
            }
            break;
        case IDC_COMBO_SETTINGS:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                SwitchSettings(static_cast<int>(SendDlgItemMessage(hDlg, IDC_COMBO_SETTINGS, CB_GETCURSEL, 0, 0)));
                InitializeSettingsDlg(hDlg);
                fReDisp = true;
            }
            break;
        case IDC_SETTINGS_ADD:
            AddSettings();
            InitializeSettingsDlg(hDlg);
            fReDisp = true;
            break;
        case IDC_SETTINGS_DELETE:
            DeleteSettings();
            InitializeSettingsDlg(hDlg);
            fReDisp = true;
            break;
        case IDC_COMBO_FACE:
        case IDC_COMBO_FACE1:
        case IDC_COMBO_FACE2:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                PostMessage(hDlg, WM_COMMAND, MAKELONG(LOWORD(wParam), CBN_EDITCHANGE), lParam);
            }
            else if (HIWORD(wParam) == CBN_EDITCHANGE) {
                int i = LOWORD(wParam) - IDC_COMBO_FACE;
                TCHAR val[SETTING_VALUE_MAX];
                if (GetDlgItemText(hDlg, IDC_COMBO_FACE + i, val, _countof(val)) == 0 ||
                    (val[0] == TEXT(' ') && val[1] == TEXT('('))) {
                    val[0] = 0;
                }
                m_faceName[i] = val;
                fSave = fReDisp = true;
            }
            break;
        case IDC_COMBO_METHOD:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                m_paintingMethod = static_cast<int>(SendDlgItemMessage(hDlg, IDC_COMBO_METHOD, CB_GETCURSEL, 0, 0));
                m_paintingMethod = min(max(m_paintingMethod, 0), 1) + 2;
                fSave = fReDisp = true;
            }
            break;
        case IDC_CHECK_FREETYPE:
            m_fFreeType = IsDlgButtonChecked(hDlg, IDC_CHECK_FREETYPE) != BST_UNCHECKED;
            fSave = fReDisp = true;
            break;
        case IDC_CHECK_CAPTION:
            m_showFlags[STREAM_CAPTION] = IsDlgButtonChecked(hDlg, IDC_CHECK_CAPTION) != BST_UNCHECKED ? 65535 : 0;
            fSave = fReDisp = true;
            break;
        case IDC_CHECK_SUPERIMPOSE:
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
        case IDC_CHECK_REPLACE_FULL_JAPANESE:
            m_fReplaceFullJapanese = IsDlgButtonChecked(hDlg, IDC_CHECK_REPLACE_FULL_JAPANESE) != BST_UNCHECKED;
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
