#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "m68k.h"
#include "sim_core.h"
#include "verilated_vpi.h"

class SimState;

template <typename T>
struct ControllerResult
{
    bool ok = false;
    T value{};
    std::string errorCode;
    std::string errorMessage;

    static ControllerResult<T> Success(const T &value)
    {
        ControllerResult<T> result;
        result.ok = true;
        result.value = value;
        return result;
    }

    static ControllerResult<T> Failure(const std::string &code, const std::string &message)
    {
        ControllerResult<T> result;
        result.ok = false;
        result.errorCode = code;
        result.errorMessage = message;
        return result;
    }
};

struct EmptyResult
{
};

struct InputStateResult
{
    uint32_t mButtons = 0;
};

enum class RunStopReason
{
    COMPLETED,
    CONDITION_MET,
    WATCHPOINT_HIT,
    TIMEOUT,
    ERROR
};

struct RunResult
{
    RunStopReason mReason = RunStopReason::COMPLETED;
    uint64_t mTicksExecuted = 0;
    uint64_t mFramesExecuted = 0;
};

struct SimStatus
{
    bool mInitialized = false;
    bool mRunning = false;
    bool mPaused = false;
    bool mTraceActive = false;
    bool mHeadless = false;
    uint64_t mTotalTicks = 0;
    std::string mGameName;
};

struct CpuState
{
    uint32_t mPc = 0;
    std::vector<uint32_t> mRegisters;
    std::string mDisasm;
};

struct MemoryReadResult
{
    std::string mRegion;
    uint32_t mAddress = 0;
    std::vector<uint8_t> mData;
};

struct SignalReadResult
{
    std::string mSignal;
    uint64_t mValue = 0;
    uint32_t mWidth = 0;
    std::string mValueHex;
};

struct SignalInfo
{
    std::string mName;
    uint32_t mWidth = 0;
    std::string mKind;
    std::string mSource;
};

struct SignalListResult
{
    std::vector<SignalInfo> mSignals;
};

struct ScreenshotResult
{
    std::string mPath;
};

struct StateListResult
{
    std::vector<std::string> mStates;
};

struct Condition
{
    enum class Type
    {
        SIGNAL_EQUALS,
        SIGNAL_NOT_EQUALS,
        SIGNAL_LESS_THAN,
        SIGNAL_LESS_EQUAL,
        SIGNAL_GREATER_THAN,
        SIGNAL_GREATER_EQUAL,
        CPU_PC_EQUALS,
        CPU_PC_IN_RANGE,
        CPU_PC_OUT_OF_RANGE,
        AND,
        OR,
        NOT
    };

    Type mType = Type::SIGNAL_EQUALS;
    std::string mSignal;
    uint64_t mValue = 0;
    uint64_t mValue2 = 0;
    std::vector<Condition> mChildren;
};

struct RunUntilRequest
{
    Condition mCondition;
    uint64_t mTimeoutCycles = 0;
};

class SimController
{
  public:
    ControllerResult<EmptyResult> Initialize(bool headless);
    ControllerResult<EmptyResult> Shutdown();

    ControllerResult<EmptyResult> LoadGame(const std::string &name);
    ControllerResult<EmptyResult> LoadMra(const std::string &path);
    ControllerResult<EmptyResult> Reset(uint64_t cycles);

    ControllerResult<RunResult> RunCycles(uint64_t cycles);
    ControllerResult<RunResult> RunFrames(uint64_t frames);
    ControllerResult<RunResult> RunUntil(const RunUntilRequest &request);

    ControllerResult<SimStatus> GetStatus() const;
    ControllerResult<CpuState> GetCpuState() const;

    ControllerResult<MemoryReadResult> ReadMemory(const std::string &region, uint32_t address, uint32_t size) const;
    ControllerResult<EmptyResult> WriteMemory(const std::string &region, uint32_t address, const std::vector<uint8_t> &data);
    ControllerResult<std::vector<std::string>> ListRegions() const;
    ControllerResult<SignalReadResult> ReadSignal(const std::string &signal) const;
    ControllerResult<SignalListResult> ListSignals() const;

    ControllerResult<EmptyResult> SetDipSwitchA(uint8_t value);
    ControllerResult<EmptyResult> SetDipSwitchB(uint8_t value);
    uint8_t GetDipSwitchA() const;
    uint8_t GetDipSwitchB() const;
    ControllerResult<InputStateResult> GetInputState() const;
    ControllerResult<EmptyResult> SetInput(const std::string &name, bool pressed);
    ControllerResult<EmptyResult> ClearInput(const std::string &name);
    ControllerResult<RunResult> PressInput(const std::string &name);

    ControllerResult<StateListResult> ListStates() const;
    ControllerResult<EmptyResult> SaveState(const std::string &filename);
    ControllerResult<EmptyResult> LoadState(const std::string &filename);

    ControllerResult<EmptyResult> StartTrace(const std::string &filename, int depth);
    ControllerResult<EmptyResult> StopTrace();
    ControllerResult<EmptyResult> StartAudioCapture(const std::string &filename);
    ControllerResult<EmptyResult> StopAudioCapture();
    ControllerResult<ScreenshotResult> SaveScreenshot(const std::string &path);

  private:
    bool mInitialized = false;
    bool mHeadless = false;
    SimState *mStateManager = nullptr;
    uint8_t mDipSwitchA = 0;
    uint8_t mDipSwitchB = 0;
    mutable std::unordered_map<std::string, vpiHandle> mVpiHandleCache;

    bool EvaluateCondition(const Condition &condition) const;
    ControllerResult<MemoryRegion> ParseRegion(const std::string &name) const;
    ControllerResult<SignalReadResult> ReadSignalValue(const std::string &signal) const;
    ControllerResult<SignalReadResult> ReadSignalValueBuiltin(const std::string &signal) const;
    ControllerResult<SignalReadResult> ReadSignalValueVpi(const std::string &signal) const;
    vpiHandle LookupVpiHandle(const std::string &signal) const;
    ControllerResult<SignalListResult> ListSignalsVpi() const;
    ControllerResult<uint32_t> ParseInputBits(const std::string &name) const;
    void ApplyInputState() const;
    RunStopReason ConvertTickStopReason(TickStopReason reason) const;
    ControllerResult<EmptyResult> EnsureInitialized() const;
};

extern SimController gSimController;
