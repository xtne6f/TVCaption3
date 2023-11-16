#ifndef INCLUDE_UTIL_HPP
#define INCLUDE_UTIL_HPP

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>
#include <tchar.h>

#ifdef NDEBUG
#define DEBUG_OUT(x)
#else
#define DEBUG_OUT(x) OutputDebugString(x)
#define DDEBUG_OUT
#endif

using std::max;
using std::min;

typedef std::lock_guard<std::recursive_mutex> lock_recursive_mutex;

typedef std::basic_string<TCHAR> tstring;

static const size_t READ_FILE_MAX_SIZE = 64 * 1024 * 1024;

std::vector<TCHAR> GetPrivateProfileSectionBuffer(LPCTSTR lpAppName, LPCTSTR lpFileName);
void GetBufferedProfileString(LPCTSTR lpBuff, LPCTSTR lpKeyName, LPCTSTR lpDefault, LPTSTR lpReturnedString, DWORD nSize);
int GetBufferedProfileInt(LPCTSTR lpBuff, LPCTSTR lpKeyName, int nDefault);
BOOL WritePrivateProfileInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, int value, LPCTSTR lpFileName);
DWORD GetLongModuleFileName(HMODULE hModule, LPTSTR lpFileName, DWORD nSize);
void AddToComboBoxList(HWND hDlg, int id, const LPCTSTR *pList);
void AddFaceNameToComboBoxList(HWND hDlg, int id);
inline bool Is1SegPmtPid(int pid) { return 0x1FC8 <= pid && pid <= 0x1FCF; }

static const int PCR_PER_MSEC = 90;

#define H_262_VIDEO         0x02
#define PES_PRIVATE_DATA    0x06
#define AVC_VIDEO           0x1B
#define H_265_VIDEO         0x24

typedef struct {
    int           adaptation_field_length;
    int           discontinuity_counter;
    int           random_access_indicator;
    int           elementary_stream_priority_indicator;
    int           pcr_flag;
    int           opcr_flag;
    int           splicing_point_flag;
    int           transport_private_data_flag;
    int           adaptation_field_extension_flag;
    long long     pcr;
} ADAPTATION_FIELD; // (partial)

typedef struct {
    int             pointer_field;
    int             table_id;
    int             section_length;
    int             version_number;
    int             current_next_indicator;
    int             continuity_counter;
    int             data_count;
    unsigned char   data[1025];
} PSI;

typedef struct {
    int             pmt_pid;
    int             program_number;
    int             version_number;
    int             pcr_pid;
    int             pid_count;
    unsigned char   stream_type[256];
    unsigned short  pid[256]; // PESの一部に限定
    unsigned char   component_tag[256];
    PSI             psi;
} PMT;

typedef struct {
    int             transport_stream_id;
    int             version_number;
    std::vector<PMT> pmt;
    PSI             psi;
} PAT;

typedef struct {
    int           packet_start_code_prefix;
    int           stream_id;
    int           pes_packet_length;
    int           pts_dts_flags;
    long long     pts;
    long long     dts;
    int           pes_payload_pos;
} PES_HEADER; // (partial)

void extract_pat(PAT *pat, const unsigned char *payload, int payload_size, int unit_start, int counter);
void extract_pmt(PMT *pmt, const unsigned char *payload, int payload_size, int unit_start, int counter);
void extract_pes_header(PES_HEADER *dst, const unsigned char *payload, int payload_size/*, int stream_type*/);
int get_ts_payload_size(const unsigned char *packet);

inline int extract_ts_header_error_indicator(const unsigned char *packet) { return !!(packet[1] & 0x80); }
inline int extract_ts_header_unit_start(const unsigned char *packet) { return !!(packet[1] & 0x40); }
inline int extract_ts_header_pid(const unsigned char *packet) { return ((packet[1] & 0x1f) << 8) | packet[2]; }
inline int extract_ts_header_scrambling_control(const unsigned char *packet) { return packet[3] >> 6; }
inline int extract_ts_header_adaptation(const unsigned char *packet) { return (packet[3] >> 4) & 0x03; }
inline int extract_ts_header_counter(const unsigned char *packet) { return packet[3] & 0x0f; }

bool BrowseFolderDialog(HWND hwndOwner, TCHAR (&szDirectory)[MAX_PATH], LPCTSTR pszTitle);

bool SaveImageAsBmp(LPCTSTR fileName, const BITMAPINFOHEADER &bih, const void *pBits);
bool SaveImageAsPngOrJpeg(HMODULE hTVTestImage, LPCTSTR fileName, bool pngOrJpeg, int compressionLevelOrQuality, const BITMAPINFOHEADER &bih, const void *pBits);

#endif // INCLUDE_UTIL_HPP
