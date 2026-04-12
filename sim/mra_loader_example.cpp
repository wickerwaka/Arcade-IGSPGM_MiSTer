// Example of how to integrate MRA loader with the simulator
// This shows how to load an MRA file and get the byte stream
// that would be sent to the core

#include "mra_loader.h"
#include "file_search.h"
#include <iostream>
#include <vector>

// Example function showing how to use MRA loader in the simulator
bool LoadGameFromMRA(const std::string &mraPath, const std::string &romPath)
{
    // Add ROM search path
    gFileSearch.AddSearchPath(romPath);

    // Also add the directory containing the MRA file as a search path
    size_t lastSlash = mraPath.find_last_of("/\\");
    if (lastSlash != std::string::npos)
    {
        gFileSearch.AddSearchPath(mraPath.substr(0, lastSlash));
    }

    // Load the MRA
    MRALoader loader;
    std::vector<uint8_t> romData;

    uint32_t address = 0;
    if (!loader.Load(mraPath, romData, address))
    {
        std::cerr << "Failed to load MRA: " << loader.GetLastError() << std::endl;
        return false;
    }

    std::cout << "Loaded " << romData.size() << " bytes of ROM data" << std::endl;

    // At this point, romData contains the exact byte stream that MiSTer
    // would send to the core via ioctl. The format matches what the
    // core expects, with the header bytes and all ROM regions concatenated
    // according to the MRA specification.

    // To integrate with the existing simulator:
    // 1. Parse the header bytes to determine ROM layout
    // 2. Extract each region based on the offsets in the header
    // 3. Load into appropriate memory regions (SDRAM, DDR, etc.)

    // Example of parsing the header (first bytes usually contain region info)
    if (romData.size() >= 2)
    {
        uint8_t header1 = romData[0];
        uint8_t header2 = romData[1];
        std::cout << "Header bytes: " << std::hex << (int)header1 << " " << (int)header2 << std::endl;
    }

    // The actual integration would parse the romData according to
    // the format expected by PGM.sv and load it into the simulator's
    // memory subsystems

    return true;
}

// Example usage:
// LoadGameFromMRA("releases/Drift Out (Europe).mra", "roms/");
