#include "sim_audio_capture.h"

#include <algorithm>

SimAudioCapture::~SimAudioCapture()
{
    Stop();
}

bool SimAudioCapture::Start(const std::string &path, uint64_t simClockHz)
{
    Stop();

    mStream.open(path, std::ios::binary | std::ios::trunc);
    if (!mStream.is_open())
    {
        return false;
    }

    mSimClockHz = simClockHz;
    mFrameStart = 0;
    mBlockSeq = 0;
    mBlockFrameCount = 0;
    mLastAudioTick = 0;
    mLastStatusTick = 0;
    mLatestRawRateHz = 0;
    mNonzeroSampleCount = 0;
    mLastLeftSample = 0;
    mLastRightSample = 0;
    mStereoFrameCount = 0;
    mEdgeCount = 0;
    return true;
}

void SimAudioCapture::Stop()
{
    if (mStream.is_open())
    {
        if (mBlockFrameCount != 0)
        {
            FlushAudioBlock(mLastAudioTick);
        }
        EmitStatus(mLastAudioTick);
        mStream.close();
    }
}

bool SimAudioCapture::IsActive() const
{
    return mStream.is_open();
}

void SimAudioCapture::Tick(uint64_t totalTicks, bool audioValid, int16_t audioLeft, int16_t audioRight)
{
    if (!IsActive())
    {
        return;
    }

    if (audioValid)
    {
        SubmitFrame(totalTicks, audioLeft, audioRight);
    }

    if ((totalTicks - mLastStatusTick) >= kStatusPeriodTicks)
    {
        EmitStatus(totalTicks);
    }
}

uint64_t SimAudioCapture::ToMicroseconds(uint64_t ticks) const
{
    if (mSimClockHz == 0)
    {
        return 0;
    }
    return (ticks * 1000000ull) / mSimClockHz;
}

uint32_t SimAudioCapture::BuildFlags() const
{
    uint32_t flags = kFlagCaptureRunning;
    if (mLatestRawRateHz != 0)
    {
        flags |= kFlagRateValid;
    }
    return flags;
}

void SimAudioCapture::SubmitFrame(uint64_t totalTicks, int16_t left, int16_t right)
{
    if (mLastAudioTick != 0 && totalTicks > mLastAudioTick)
    {
        uint64_t deltaTicks = totalTicks - mLastAudioTick;
        mLatestRawRateHz = static_cast<uint32_t>(mSimClockHz / deltaTicks);
    }

    mLastAudioTick = totalTicks;
    mLastLeftSample = left;
    mLastRightSample = right;
    mStereoFrameCount++;
    mEdgeCount++;
    if (left != 0)
    {
        mNonzeroSampleCount++;
    }
    if (right != 0)
    {
        mNonzeroSampleCount++;
    }

    mFrames[mBlockFrameCount++] = {left, right};
    if (mBlockFrameCount == kFramesPerBlock)
    {
        FlushAudioBlock(totalTicks);
    }
}

void SimAudioCapture::FlushAudioBlock(uint64_t totalTicks)
{
    if (!IsActive() || mBlockFrameCount == 0)
    {
        return;
    }

    const PacketHeader header{
        .magic = kMagic,
        .version = kVersion,
        .type = kTypeAudio,
        .payloadBytes = static_cast<uint32_t>(mBlockFrameCount * sizeof(StereoFrame)),
        .blockSeq = mBlockSeq++,
        .frameStart = mFrameStart,
        .frameCount = mBlockFrameCount,
        .tUs = ToMicroseconds(totalTicks),
        .rawLrclkHz = mLatestRawRateHz,
        .flags = BuildFlags(),
    };

    mStream.write(reinterpret_cast<const char *>(&header), sizeof(header));
    mStream.write(reinterpret_cast<const char *>(mFrames.data()), static_cast<std::streamsize>(header.payloadBytes));

    mFrameStart += mBlockFrameCount;
    mBlockFrameCount = 0;
}

void SimAudioCapture::EmitStatus(uint64_t totalTicks)
{
    if (!IsActive())
    {
        return;
    }

    const uint64_t elapsedUs64 = ToMicroseconds(totalTicks);
    const uint32_t elapsedUs = static_cast<uint32_t>(std::min<uint64_t>(elapsedUs64, 0xffffffffu));
    const StatusPayload payload{
        .uptimeMs = static_cast<uint32_t>(std::min<uint64_t>(elapsedUs64 / 1000ull, 0xffffffffu)),
        .rateHz = mLatestRawRateHz,
        .rawRateHz = mLatestRawRateHz,
        .edgeCount = mEdgeCount > 0xffffffffull ? 0xffffffffu : static_cast<uint32_t>(mEdgeCount),
        .elapsedUs = elapsedUs,
        .idleUs = 0,
        .rateStatus = mLatestRawRateHz != 0 ? 1u : 0u,
        .readyMask = 0,
        .processedDmaBlocks = mBlockSeq,
        .droppedDmaBlocks = 0,
        .droppedAudioFrames = 0,
        .channelWordCount = mStereoFrameCount > 0x7fffffffull ? 0xffffffffu : static_cast<uint32_t>(mStereoFrameCount * 2ull),
        .stereoFrameCount = mStereoFrameCount > 0xffffffffull ? 0xffffffffu : static_cast<uint32_t>(mStereoFrameCount),
        .nonzeroSampleCount = mNonzeroSampleCount,
        .lastLeftSample = mLastLeftSample,
        .lastRightSample = mLastRightSample,
        .streamDroppedPackets = 0,
        .streamDroppedBytes = 0,
        .streamQueueDepth = 0,
        .reserved = 0,
    };

    const PacketHeader header{
        .magic = kMagic,
        .version = kVersion,
        .type = kTypeStatus,
        .payloadBytes = sizeof(payload),
        .blockSeq = mBlockSeq++,
        .frameStart = mFrameStart,
        .frameCount = 0,
        .tUs = elapsedUs64,
        .rawLrclkHz = mLatestRawRateHz,
        .flags = BuildFlags(),
    };

    mStream.write(reinterpret_cast<const char *>(&header), sizeof(header));
    mStream.write(reinterpret_cast<const char *>(&payload), sizeof(payload));
    mLastStatusTick = totalTicks;
}
