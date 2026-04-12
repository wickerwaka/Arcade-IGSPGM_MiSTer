#include "sim_state.h"
#include "PGM.h"
#include "sim_ddr.h"
#include "sim_core.h"

#include <dirent.h>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include <iomanip>

SimState::SimState(PGM *top, SimDDR *memory, int offset, int size)
    : mTop(top), mMemory(memory), mOffset(offset), mSize(size), mGameName("unknown")
{
}

void SimState::SetGameName(const char *gameName)
{
    mGameName = gameName;
    EnsureStateDirectory();
}

void SimState::EnsureStateDirectory()
{
    // Create states directory if it doesn't exist
    mkdir("states", 0755);

    // Create game-specific directory
    std::string gameDir = "states/" + mGameName;
    mkdir(gameDir.c_str(), 0755);
}

std::string SimState::GetStatePath(const char *filename)
{
    return "states/" + mGameName + "/" + filename;
}

bool SimState::SaveState(const char *filename)
{
    mTop->ss_index = 0;
    mTop->ss_do_save = 1;
    gSimCore.TickUntil([&] { return mTop->ss_state_out != 0; }, 0);

    mTop->ss_do_save = 0;
    gSimCore.TickUntil([&] { return mTop->ss_state_out == 0; }, 0);

    std::string fullPath = GetStatePath(filename);
    mMemory->SaveData(fullPath.c_str(), mOffset, mSize);

    return true;
}

bool SimState::RestoreState(const char *filename)
{
    std::string fullPath = GetStatePath(filename);
    mMemory->LoadData(fullPath.c_str(), mOffset,
                        1); // Pass stride=1 explicitly

    mTop->ss_index = 0;
    mTop->ss_do_restore = 1;
    gSimCore.TickUntil([&] { return mTop->ss_state_out != 0; }, 0);

    mTop->ss_do_restore = 0;
    gSimCore.TickUntil([&] { return mTop->ss_state_out == 0; }, 0);

    return true;
}

std::vector<std::string> SimState::GetPgmstateFiles()
{
    std::vector<std::string> files;
    DIR *dir;
    struct dirent *ent;

    std::string gameDir = "states/" + mGameName;

    if ((dir = opendir(gameDir.c_str())) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            std::string filename = ent->d_name;
            // Check if filename ends with .pgmstate
            if (filename.size() > 9 && filename.substr(filename.size() - 9) == ".pgmstate")
            {
                files.push_back(filename);
            }
        }
        closedir(dir);
    }

    // Sort file names
    std::sort(files.begin(), files.end());

    return files;
}

std::string SimState::GenerateNextStateName()
{
    std::vector<std::string> existingFiles = GetPgmstateFiles();

    // Find the next available number
    int nextNum = 0;
    bool found = false;

    while (!found && nextNum < 1000)
    {
        // Generate filename with 3-digit zero-padded number
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(3) << nextNum << ".pgmstate";
        std::string candidate = ss.str();

        // Check if this filename already exists
        bool exists = false;
        for (const auto &file : existingFiles)
        {
            if (file.find(candidate.substr(0, 3)) == 0)
            {
                exists = true;
                break;
            }
        }

        if (!exists)
        {
            found = true;
            return candidate;
        }

        nextNum++;
    }

    // Fallback if somehow we have 1000 save states
    return "999.pgmstate";
}
