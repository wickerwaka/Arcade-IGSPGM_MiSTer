#ifndef SIM_COMMAND_H
#define SIM_COMMAND_H

#include <string>
#include <queue>
#include <vector>

enum class CommandType
{
    LOAD_STATE,
    SAVE_STATE,
    RUN_CYCLES,
    RUN_FRAMES,
    SCREENSHOT,
    TRACE_START,
    TRACE_STOP,
    SCRIPT_FILE,
    LOAD_GAME,
    LOAD_MRA,
    RESET,
    SET_DIPSWITCH_A,
    SET_DIPSWITCH_B,
    EXIT
};

struct Command
{
    CommandType mType;
    std::string mFilename;
    uint64_t mCount;

    Command(CommandType t) : mType(t), mCount(0)
    {
    }
    Command(CommandType t, const std::string &f) : mType(t), mFilename(f), mCount(0)
    {
    }
    Command(CommandType t, uint64_t c) : mType(t), mCount(c)
    {
    }
};

class CommandQueue
{
  public:
    void Add(const Command &cmd);
    bool Empty() const
    {
        return mCommands.empty();
    }
    Command &Front()
    {
        return mCommands.front();
    }
    void Pop()
    {
        mCommands.pop();
    }
    size_t Size() const
    {
        return mCommands.size();
    }

    bool ParseArguments(int argc, char **argv, std::string &gameName);
    bool ParseScriptFile(const std::string &filename);

    bool IsBatchMode() const
    {
        return mBatchMode;
    }
    bool IsHeadless() const
    {
        return mHeadless;
    }
    bool IsVerbose() const
    {
        return mVerbose;
    }

  private:
    std::queue<Command> mCommands;
    bool mBatchMode = false;
    bool mHeadless = false;
    bool mVerbose = false;

    bool ParseScriptLine(const std::string &line);
    void PrintUsage(const char *programName);
};

#endif
