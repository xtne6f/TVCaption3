// TVTestのストリームコールバックから表示までの遅延を推定する
#ifndef INCLUDE_VIEWER_CLOCK_ESTIMATOR_HPP
#define INCLUDE_VIEWER_CLOCK_ESTIMATOR_HPP

#include "Util.hpp"
#include <deque>
#include <utility>

class CViewerClockEstimator
{
public:
    CViewerClockEstimator();
    void SetEnabled(bool fEnabled);
    bool GetEnabled() const;
    void SetStreamPcr(LONGLONG pcr);
    LONGLONG GetStreamPcr() const;
    void Reset();
    void SetVideoPes(bool fViewer, bool fUnitStart, const BYTE *data, size_t len);
    void SetVideoStream(bool fViewer, const BYTE *data, size_t len);
    LONGLONG GetViewerPcr() const;

private:
    DWORD CalcCrc32(BYTE b, DWORD crc = 0xffffffff) const {
        return (crc << 8) ^ m_crcTable[(crc >> 24) ^ b];
    }

    // 遅延量の推定の既定値
    static const DWORD DEFAULT_PCR_DIFF = 450 * PCR_PER_MSEC;
    // 遅延量の推定の最大値
    static const DWORD STREAM_PCR_QUEUE_DIFF = 10000 * PCR_PER_MSEC;
    // 表示に近い側のストリームの観測範囲
    static const DWORD VIEWER_PCR_QUEUE_DIFF = 1000 * PCR_PER_MSEC;

    mutable std::recursive_mutex m_streamLock;
    bool m_fEnabled;
    LONGLONG m_streamPcr;
    LONGLONG m_updateDiffPcr;
    int m_pesHeaderRemain[2];
    int m_unitState[2];
    size_t m_unitSize[2];
    DWORD m_crc[2];
    std::deque<std::pair<DWORD, DWORD>> m_crcPcrDeq[2];
    std::vector<std::pair<DWORD, int>> m_pcrDiffs;
    DWORD m_pcrDiff;
    DWORD m_crcTable[256];
};

#endif // INCLUDE_VIEWER_CLOCK_ESTIMATOR_HPP
