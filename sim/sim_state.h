#pragma once

#include <vector>
#include <string>

// Forward declarations
class PGM;
class SimDDR;

class SimState
{
  public:
    SimState(PGM *top, SimDDR *memory, int offset, int size);

    // Set the current game name for directory organization
    void set_game_name(const char *game_name);

    // Save state to the specified file
    bool save_state(const char *filename);

    // Restore state from the specified file
    bool restore_state(const char *filename);

    // Get list of all available state files in game-specific directory
    std::vector<std::string> get_f2state_files();

    // Get the full path for a state file
    std::string get_state_path(const char *filename);

    // Create state directory if it doesn't exist
    void ensure_state_directory();

    // Generate next available state filename (000.f2state, 001.f2state, etc.)
    std::string generate_next_state_name();

    // Tick the simulation for the given number of cycles
    void tick(int count);

  private:
    PGM *m_top;
    SimDDR *m_memory;
    int m_offset;
    int m_size;
    std::string m_game_name;
};
