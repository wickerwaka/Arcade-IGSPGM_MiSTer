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
    : m_top(top), m_memory(memory), m_offset(offset), m_size(size), m_game_name("unknown")
{
}

void SimState::set_game_name(const char *game_name)
{
    m_game_name = game_name;
    ensure_state_directory();
}

void SimState::ensure_state_directory()
{
    // Create states directory if it doesn't exist
    mkdir("states", 0755);

    // Create game-specific directory
    std::string game_dir = "states/" + m_game_name;
    mkdir(game_dir.c_str(), 0755);
}

std::string SimState::get_state_path(const char *filename)
{
    return "states/" + m_game_name + "/" + filename;
}

bool SimState::save_state(const char *filename)
{
    m_top->ss_index = 0;
    m_top->ss_do_save = 1;
    gSimCore.TickUntil([&] { return m_top->ss_state_out != 0; });

    m_top->ss_do_save = 0;
    gSimCore.TickUntil([&] { return m_top->ss_state_out == 0; });

    std::string full_path = get_state_path(filename);
    m_memory->save_data(full_path.c_str(), m_offset, m_size);

    return true;
}

bool SimState::restore_state(const char *filename)
{
    std::string full_path = get_state_path(filename);
    m_memory->load_data(full_path.c_str(), m_offset,
                        1); // Pass stride=1 explicitly

    m_top->ss_index = 0;
    m_top->ss_do_restore = 1;
    gSimCore.TickUntil([&] { return m_top->ss_state_out != 0; });

    m_top->ss_do_restore = 0;
    gSimCore.TickUntil([&] { return m_top->ss_state_out == 0; });

    return true;
}

std::vector<std::string> SimState::get_f2state_files()
{
    std::vector<std::string> files;
    DIR *dir;
    struct dirent *ent;

    std::string game_dir = "states/" + m_game_name;

    if ((dir = opendir(game_dir.c_str())) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            std::string filename = ent->d_name;
            // Check if filename ends with .f2state
            if (filename.size() > 8 && filename.substr(filename.size() - 8) == ".f2state")
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

std::string SimState::generate_next_state_name()
{
    std::vector<std::string> existing_files = get_f2state_files();

    // Find the next available number
    int next_num = 0;
    bool found = false;

    while (!found && next_num < 1000)
    {
        // Generate filename with 3-digit zero-padded number
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(3) << next_num << ".f2state";
        std::string candidate = ss.str();

        // Check if this filename already exists
        bool exists = false;
        for (const auto &file : existing_files)
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

        next_num++;
    }

    // Fallback if somehow we have 1000 save states
    return "999.f2state";
}
