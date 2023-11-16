#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include "Util.hpp"

// 必要なバッファを確保してGetPrivateProfileSection()を呼ぶ
std::vector<TCHAR> GetPrivateProfileSectionBuffer(LPCTSTR lpAppName, LPCTSTR lpFileName)
{
    std::vector<TCHAR> buf(4096);
    for (;;) {
        DWORD len = GetPrivateProfileSection(lpAppName, buf.data(), static_cast<DWORD>(buf.size()), lpFileName);
        if (len < buf.size() - 2) {
            buf.resize(len + 1);
            break;
        }
        if (buf.size() >= READ_FILE_MAX_SIZE / 2) {
            buf.assign(1, TEXT('\0'));
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return buf;
}

// GetPrivateProfileSection()で取得したバッファから、キーに対応する文字列を取得する
void GetBufferedProfileString(LPCTSTR lpBuff, LPCTSTR lpKeyName, LPCTSTR lpDefault, LPTSTR lpReturnedString, DWORD nSize)
{
    size_t nKeyLen = _tcslen(lpKeyName);
    while (*lpBuff) {
        size_t nLen = _tcslen(lpBuff);
        if (!_tcsnicmp(lpBuff, lpKeyName, nKeyLen) && lpBuff[nKeyLen] == TEXT('=')) {
            if ((lpBuff[nKeyLen + 1] == TEXT('\'') || lpBuff[nKeyLen + 1] == TEXT('"')) &&
                nLen >= nKeyLen + 3 && lpBuff[nKeyLen + 1] == lpBuff[nLen - 1])
            {
                _tcsncpy_s(lpReturnedString, nSize, lpBuff + nKeyLen + 2, min(nLen - nKeyLen - 3, static_cast<size_t>(nSize - 1)));
            }
            else {
                _tcsncpy_s(lpReturnedString, nSize, lpBuff + nKeyLen + 1, _TRUNCATE);
            }
            return;
        }
        lpBuff += nLen + 1;
    }
    _tcsncpy_s(lpReturnedString, nSize, lpDefault, _TRUNCATE);
}

// GetPrivateProfileSection()で取得したバッファから、キーに対応する数値を取得する
int GetBufferedProfileInt(LPCTSTR lpBuff, LPCTSTR lpKeyName, int nDefault)
{
    TCHAR sz[16];
    GetBufferedProfileString(lpBuff, lpKeyName, TEXT(""), sz, _countof(sz));
    LPTSTR endp;
    int nRet = _tcstol(sz, &endp, 10);
    return endp == sz ? nDefault : nRet;
}

BOOL WritePrivateProfileInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, int value, LPCTSTR lpFileName)
{
    TCHAR sz[16];
    _stprintf_s(sz, TEXT("%d"), value);
    return WritePrivateProfileString(lpAppName, lpKeyName, sz, lpFileName);
}

DWORD GetLongModuleFileName(HMODULE hModule, LPTSTR lpFileName, DWORD nSize)
{
    TCHAR longOrShortName[MAX_PATH];
    DWORD nRet = GetModuleFileName(hModule, longOrShortName, MAX_PATH);
    if (nRet && nRet < MAX_PATH) {
        nRet = GetLongPathName(longOrShortName, lpFileName, nSize);
        if (nRet < nSize) return nRet;
    }
    return 0;
}

void AddToComboBoxList(HWND hDlg, int id, const LPCTSTR *pList)
{
    for (; *pList; ++pList) {
        SendDlgItemMessage(hDlg, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(*pList));
    }
}

namespace
{
int CALLBACK EnumAddFaceNameProc(ENUMLOGFONTEX *lpelfe, NEWTEXTMETRICEX *, int FontType, LPARAM lParam)
{
    static_cast<void>(FontType);
    if (lpelfe->elfLogFont.lfFaceName[0] != TEXT('@')) {
        std::vector<LOGFONT> &lfList = *reinterpret_cast<std::vector<LOGFONT>*>(lParam);
        for (size_t i = 0; i < lfList.size(); ++i) {
            if (!_tcscmp(lfList[i].lfFaceName, lpelfe->elfLogFont.lfFaceName)) {
                return TRUE;
            }
        }
        lfList.push_back(lpelfe->elfLogFont);
    }
    return TRUE;
}
}

void AddFaceNameToComboBoxList(HWND hDlg, int id)
{
    HDC hdc = GetDC(hDlg);
    LOGFONT lf;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfPitchAndFamily = 0;
    lf.lfFaceName[0] = 0;
    std::vector<LOGFONT> lfList;
    EnumFontFamiliesEx(hdc, &lf, reinterpret_cast<FONTENUMPROC>(EnumAddFaceNameProc),
                       reinterpret_cast<LPARAM>(&lfList), 0);
    ReleaseDC(hDlg, hdc);
    for (size_t i = 0; i < lfList.size(); ++i) {
        SendDlgItemMessage(hDlg, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(lfList[i].lfFaceName));
    }
}

namespace
{
void extract_psi(PSI *psi, const unsigned char *payload, int payload_size, int unit_start, int counter)
{
    int pointer;
    int section_length;
    const unsigned char *table;

    if (unit_start) {
        psi->continuity_counter = 0x20|counter;
        psi->data_count = psi->version_number = 0;
    }
    else {
        psi->continuity_counter = (psi->continuity_counter+1)&0x2f;
        if (psi->continuity_counter != (0x20|counter)) {
            psi->continuity_counter = psi->data_count = psi->version_number = 0;
            return;
        }
    }
    if (psi->data_count + payload_size <= sizeof(psi->data)) {
        memcpy(psi->data + psi->data_count, payload, payload_size);
        psi->data_count += payload_size;
    }
    // TODO: CRC32

    // psi->version_number != 0 のとき各フィールドは有効
    if (psi->data_count >= 1) {
        pointer = psi->data[0];
        if (psi->data_count >= pointer + 4) {
            section_length = ((psi->data[pointer+2]&0x03)<<8) | psi->data[pointer+3];
            if (section_length >= 3 && psi->data_count >= pointer + 4 + section_length) {
                table = psi->data + 1 + pointer;
                psi->pointer_field          = pointer;
                psi->table_id               = table[0];
                psi->section_length         = section_length;
                psi->version_number         = 0x20 | ((table[5]>>1)&0x1f);
                psi->current_next_indicator = table[5] & 0x01;
            }
        }
    }
}
}

// 参考: ITU-T H.222.0 Sec.2.4.4.3 および ARIB TR-B14 第一分冊第二編8.2
void extract_pat(PAT *pat, const unsigned char *payload, int payload_size, int unit_start, int counter)
{
    int program_number;
    int pos;
    size_t pmt_pos, i;
    int pid;
    const unsigned char *table;

    extract_psi(&pat->psi, payload, payload_size, unit_start, counter);

    if (pat->psi.version_number &&
        pat->psi.current_next_indicator &&
        pat->psi.table_id == 0 &&
        pat->psi.section_length >= 5)
    {
        // PAT更新
        table = pat->psi.data + 1 + pat->psi.pointer_field;
        pat->transport_stream_id = (table[3]<<8) | table[4];
        pat->version_number = pat->psi.version_number;

        // 受信済みPMTを調べ、必要ならば新規に生成する
        pmt_pos = 0;
        pos = 3 + 5;
        while (pos + 3 < 3 + pat->psi.section_length - 4/*CRC32*/) {
            program_number = (table[pos]<<8) | (table[pos+1]);
            if (program_number != 0) {
                pid = ((table[pos+2]&0x1f)<<8) | table[pos+3];
                for (i = pmt_pos; i < pat->pmt.size(); ++i) {
                    if (pat->pmt[i].pmt_pid == pid) {
                        if (i != pmt_pos) {
                            PMT sw = pat->pmt[i];
                            pat->pmt[i] = pat->pmt[pmt_pos];
                            pat->pmt[pmt_pos] = sw;
                        }
                        ++pmt_pos;
                        break;
                    }
                }
                if (i == pat->pmt.size()) {
                    pat->pmt.insert(pat->pmt.begin() + pmt_pos, PMT());
                    pat->pmt[pmt_pos++].pmt_pid = pid;
                }
            }
            pos += 4;
        }
        // PATから消えたPMTを破棄する
        pat->pmt.resize(pmt_pos);
    }
}

// 参考: ITU-T H.222.0 Sec.2.4.4.8 および ARIB TR-B14 第二分冊第四編第3部
void extract_pmt(PMT *pmt, const unsigned char *payload, int payload_size, int unit_start, int counter)
{
    int program_info_length;
    int es_info_length;
    int stream_type;
    int info_pos;
    int pos;
    const unsigned char *table;

    extract_psi(&pmt->psi, payload, payload_size, unit_start, counter);

    if (pmt->psi.version_number &&
        pmt->psi.current_next_indicator &&
        pmt->psi.table_id == 2 &&
        pmt->psi.section_length >= 9)
    {
        // PMT更新
        table = pmt->psi.data + 1 + pmt->psi.pointer_field;
        pmt->program_number = (table[3]<<8) | table[4];
        pmt->version_number = pmt->psi.version_number;
        pmt->pcr_pid        = ((table[8]&0x1f)<<8) | table[9];
        program_info_length = ((table[10]&0x03)<<8) | table[11];

        pmt->pid_count = 0;
        pos = 3 + 9 + program_info_length;
        while (pos + 4 < 3 + pmt->psi.section_length - 4/*CRC32*/) {
            stream_type = table[pos];
            es_info_length = (table[pos+3]&0x03)<<8 | table[pos+4];
            if ((stream_type == H_262_VIDEO ||
                 stream_type == PES_PRIVATE_DATA ||
                 stream_type == AVC_VIDEO ||
                 stream_type == H_265_VIDEO) &&
                pos + 5 + es_info_length <= 3 + pmt->psi.section_length - 4/*CRC32*/)
            {
                pmt->stream_type[pmt->pid_count] = (unsigned char)stream_type;
                pmt->pid[pmt->pid_count] = (table[pos+1]&0x1f)<<8 | table[pos+2];
                // ストリーム識別記述子を探す(運用規定と異なり必ずしも先頭に配置されない)
                // 映像系ストリームの情報は再生中の映像PIDとのマッチングに使うだけなので、component_tagの情報は必須でない
                pmt->component_tag[pmt->pid_count] = 0xff;
                for (info_pos = 0; info_pos + 2 < es_info_length; ) {
                    if (table[pos+5+info_pos] == 0x52) {
                        pmt->component_tag[pmt->pid_count] = table[pos+5+info_pos+2];
                        break;
                    }
                    info_pos += 2 + table[pos+5+info_pos+1];
                }
                ++pmt->pid_count;
            }
            pos += 5 + es_info_length;
        }
    }
}

#define PROGRAM_STREAM_MAP          0xBC
#define PADDING_STREAM              0xBE
#define PRIVATE_STREAM_2            0xBF
#define ECM                         0xF0
#define EMM                         0xF1
#define PROGRAM_STREAM_DIRECTORY    0xFF
#define DSMCC_STREAM                0xF2
#define ITU_T_REC_TYPE_E_STREAM     0xF8

// 参考: ITU-T H.222.0 Sec.2.4.3.6
void extract_pes_header(PES_HEADER *dst, const unsigned char *payload, int payload_size/*, int stream_type*/)
{
    const unsigned char *p;

    dst->packet_start_code_prefix = 0;
    if (payload_size < 19) return;

    p = payload;

    dst->packet_start_code_prefix = (p[0]<<16) | (p[1]<<8) | p[2];
    if (dst->packet_start_code_prefix != 1) {
        dst->packet_start_code_prefix = 0;
        return;
    }

    dst->stream_id         = p[3];
    dst->pes_packet_length = (p[4]<<8) | p[5];
    dst->pts_dts_flags     = 0;
    dst->pes_payload_pos   = 6;
    if (dst->stream_id != PROGRAM_STREAM_MAP &&
        dst->stream_id != PADDING_STREAM &&
        dst->stream_id != PRIVATE_STREAM_2 &&
        dst->stream_id != ECM &&
        dst->stream_id != EMM &&
        dst->stream_id != PROGRAM_STREAM_DIRECTORY &&
        dst->stream_id != DSMCC_STREAM &&
        dst->stream_id != ITU_T_REC_TYPE_E_STREAM)
    {
        dst->pts_dts_flags = (p[7]>>6) & 0x03;
        if (dst->pts_dts_flags >= 2) {
            dst->pts = ((long long)(p[9]&0x0e)<<29) | (p[10]<<22) | ((p[11]&0xfe)<<14) | (p[12]<<7) | (p[13]>>1);
            if (dst->pts_dts_flags == 3) {
                dst->dts = ((long long)(p[14]&0x0e)<<29) | (p[15]<<22) | ((p[16]&0xfe)<<14) | (p[17]<<7) | (p[18]>>1);
            }
        }
        dst->pes_payload_pos = 9 + p[8];
    }
    // スタフィング(Sec.2.4.3.5)によりPESのうちここまでは必ず読める
}

int get_ts_payload_size(const unsigned char *packet)
{
    int adaptation = extract_ts_header_adaptation(packet);
    if (adaptation & 1) {
        if (adaptation == 3) {
            int adaptation_length = packet[4];
            if (adaptation_length <= 183) {
                return 183 - adaptation_length;
            }
        }
        else {
            return 184;
        }
    }
    return 0;
}

namespace
{
int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
    static_cast<void>(lParam);
    if (uMsg == BFFM_INITIALIZED && reinterpret_cast<LPCTSTR>(lpData)[0]) {
        SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
    }
    return 0;
}
}

bool BrowseFolderDialog(HWND hwndOwner, TCHAR (&szDirectory)[MAX_PATH], LPCTSTR pszTitle)
{
    TCHAR szDisplayName[MAX_PATH];
    BROWSEINFO bi;
    bi.hwndOwner = hwndOwner;
    bi.pidlRoot = nullptr;
    bi.pszDisplayName = szDisplayName;
    bi.lpszTitle = pszTitle;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseCallbackProc;
    bi.lParam = reinterpret_cast<LPARAM>(szDirectory);
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolder(&bi);
    if (pidl) {
        if (SHGetPathFromIDList(pidl, szDirectory)) {
            CoTaskMemFree(pidl);
            return true;
        }
        CoTaskMemFree(pidl);
    }
    return false;
}

bool SaveImageAsBmp(LPCTSTR fileName, const BITMAPINFOHEADER &bih, const void *pBits)
{
    if (bih.biBitCount == 24) {
        HANDLE hFile = CreateFile(fileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            BITMAPFILEHEADER bmfHeader = {};
            bmfHeader.bfType = 0x4D42;
            bmfHeader.bfOffBits = sizeof(bmfHeader) + sizeof(bih);
            bmfHeader.bfSize = bmfHeader.bfOffBits + (bih.biWidth * 3 + 3) / 4 * 4 * bih.biHeight;
            DWORD dwWritten;
            WriteFile(hFile, &bmfHeader, sizeof(bmfHeader), &dwWritten, nullptr);
            WriteFile(hFile, &bih, sizeof(bih), &dwWritten, nullptr);
            WriteFile(hFile, pBits, bmfHeader.bfSize - bmfHeader.bfOffBits, &dwWritten, nullptr);
            CloseHandle(hFile);
            return true;
        }
    }
    return false;
}

struct ImageSaveInfo
{
    LPCTSTR pszFileName;
    LPCTSTR pszFormat;
    LPCTSTR pszOption;
    const BITMAPINFO *pbmi;
    const void *pBits;
    LPCTSTR pszComment;
};

bool SaveImageAsPngOrJpeg(HMODULE hTVTestImage, LPCTSTR fileName, bool pngOrJpeg, int compressionLevelOrQuality, const BITMAPINFOHEADER &bih, const void *pBits)
{
    BOOL (WINAPI *pfnSaveImage)(const ImageSaveInfo *) =
        reinterpret_cast<BOOL (WINAPI *)(const ImageSaveInfo *)>(::GetProcAddress(hTVTestImage, "SaveImage"));
    if (pfnSaveImage) {
        ImageSaveInfo info;
        info.pszFileName = fileName;
        info.pszFormat = pngOrJpeg ? TEXT("PNG") : TEXT("JPEG");
        TCHAR option[16];
        _stprintf_s(option, TEXT("%d"), min(max(compressionLevelOrQuality, 0), pngOrJpeg ? 9 : 100));
        info.pszOption = option;
        BITMAPINFO bmi = {};
        bmi.bmiHeader = bih;
        info.pbmi = &bmi;
        info.pBits = pBits;
        info.pszComment = nullptr;
        return !!pfnSaveImage(&info);
    }
    return false;
}
