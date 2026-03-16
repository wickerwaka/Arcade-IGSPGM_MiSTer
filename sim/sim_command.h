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
    CommandType type;
    std::string filename;
    uint64_t count;

    Command(CommandType t) : type(t), count(0)
    {
    }
    Command(CommandType t, const std::string &f) : type(t), filename(f), count(0)
    {
    }
    Command(CommandType t, uint64_t c) : type(t), count(c)
    {
    }
};

class CommandQueue
{
  public:
    void add(const Command &cmd);
    bool empty() const
    {
        return commands.empty();
    }
    Command &front()
    {
        return commands.front();
    }
    void pop()
    {
        commands.pop();
    }
    size_t size() const
    {
        return commands.size();
    }

    bool parse_arguments(int argc, char **argv, std::string &game_name);
    bool parse_script_file(const std::string &filename);

    bool is_batch_mode() const
    {
        return batch_mode;
    }
    bool is_headless() const
    {
        return headless;
    }
    bool is_verbose() const
    {
        return verbose;
    }

  private:
    std::queue<Command> commands;
    bool batch_mode = false;
    bool headless = false;
    bool verbose = false;

    bool parse_script_line(const std::string &line);
    void print_usage(const char *program_name);
};

#endif