#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

class SimAudioCapture
{
  public:
    static constexpr uint32_t kMagic = 0x414D4750u;
    static constexpr uint16_t kVersion = 1u;
    static constexpr uint16_t kTypeAudio = 1u;
    static constexpr uint16_t kTypeStatus = 2u;
    static constexpr uint32_t kFlagRateValid = 1u << 0;
    static constexpr uint32_t kFlagCaptureRunning = 1u << 1;
    static constexpr uint32_t kFramesPerBlock = 128u;
    static constexpr uint64_t kStatusPeriodTicks = 50'000'000ull;

    struct StereoFrame
    {
        int16_t left;
        int16_t right;
    };

    SimAudioCapture() = default;
    ~SimAudioCapture();

    bool Start(const std::string &path, uint64_t simClockHz);
    void Stop();
    bool IsActive() const;
    void Tick(uint64_t totalTicks, bool audioValid, int16_t audioLeft, int16_t audioRight);

  private:
    struct __attribute__((packed)) PacketHeader
    {
        uint32_t magic;
        uint16_t version;
        uint16_t type;
        uint32_t payloadBytes;
        uint32_t blockSeq;
        uint64_t frameStart;
        uint32_t frameCount;
        uint64_t tUs;
        uint32_t rawLrclkHz;
        uint32_t flags;
    };

    struct __attribute__((packed)) StatusPayload
    {
        uint32_t uptimeMs;
        uint32_t rateHz;
        uint32_t rawRateHz;
        uint32_t edgeCount;
        uint32_t elapsedUs;
        uint32_t idleUs;
        uint32_t rateStatus;
        uint32_t readyMask;
        uint32_t processedDmaBlocks;
        uint32_t droppedDmaBlocks;
        uint32_t droppedAudioFrames;
        uint32_t channelWordCount;
        uint32_t stereoFrameCount;
        uint32_t nonzeroSampleCount;
        int16_t lastLeftSample;
        int16_t lastRightSample;
        uint32_t streamDroppedPackets;
        uint32_t streamDroppedBytes;
        uint32_t streamQueueDepth;
        uint32_t reserved;
    };

    static_assert(sizeof(PacketHeader) == 44, "PacketHeader size must match PGMAudioExtractor protocol");
    static_assert(sizeof(StatusPayload) == 76, "StatusPayload size must match PGMAudioExtractor protocol");

    uint64_t ToMicroseconds(uint64_t ticks) const;
    uint32_t BuildFlags() const;
    void SubmitFrame(uint64_t totalTicks, int16_t left, int16_t right);
    void FlushAudioBlock(uint64_t totalTicks);
    void EmitStatus(uint64_t totalTicks);

    std::ofstream mStream;
    uint64_t mSimClockHz = 0;
    uint64_t mFrameStart = 0;
    uint32_t mBlockSeq = 0;
    uint32_t mBlockFrameCount = 0;
    uint64_t mLastAudioTick = 0;
    uint64_t mLastStatusTick = 0;
    uint32_t mLatestRawRateHz = 0;
    uint32_t mNonzeroSampleCount = 0;
    int16_t mLastLeftSample = 0;
    int16_t mLastRightSample = 0;
    uint64_t mStereoFrameCount = 0;
    uint64_t mEdgeCount = 0;
    std::array<StereoFrame, kFramesPerBlock> mFrames{};
};
