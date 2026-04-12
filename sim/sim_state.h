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
    void SetGameName(const char *gameName);

    // Save state to the specified file
    bool SaveState(const char *filename);

    // Restore state from the specified file
    bool RestoreState(const char *filename);

    // Get list of all available state files in game-specific directory
    std::vector<std::string> GetPgmstateFiles();

    // Get the full path for a state file
    std::string GetStatePath(const char *filename);

    // Create state directory if it doesn't exist
    void EnsureStateDirectory();

    // Generate next available state filename (000.pgmstate, 001.pgmstate, etc.)
    std::string GenerateNextStateName();

    // Tick the simulation for the given number of cycles
    void Tick(int count);

  private:
    PGM *mTop;
    SimDDR *mMemory;
    int mOffset;
    int mSize;
    std::string mGameName;
};
