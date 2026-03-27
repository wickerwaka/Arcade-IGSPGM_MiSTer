#include "games.h"
#include "sim_core.h"
#include "sim_sdram.h"
#include "sim_ddr.h"
#include "mra_loader.h"

#include "PGM.h"
#include "PGM___024root.h"

#include "file_search.h"
#include <string.h>
#include <cstdio>

static const char *game_names[N_GAMES] = {
    "pgm",
    "testbios",
    "espgalbl",
};

game_t game_find(const char *name)
{
    for (int i = 0; i < N_GAMES; i++)
    {
        if (!strcasecmp(name, game_names[i]))
        {
            return (game_t)i;
        }
    }

    return GAME_INVALID;
}

const char *game_name(game_t game)
{
    if (game == GAME_INVALID)
        return "INVALID";
    return game_names[game];
}

static void load_pgm()
{
    g_fs.addSearchPath("../roms/pgm.zip");

    gSimCore.mSDRAM->load_data("pgm_p02s.u20", CPU_ROM_SDR_BASE, 1);
    gSimCore.mSDRAM->load_data("pgm_t01s.rom", TILE_ROM_SDR_BASE, 1);

    gSimCore.SetGame(GAME_PGM);
}

static void load_testbios()
{
    g_fs.addSearchPath("../roms/pgm.zip");

    gSimCore.mSDRAM->load_data("testbios.bin", CPU_ROM_SDR_BASE, 1);
    gSimCore.mSDRAM->load_data("pgm_t01s.rom", TILE_ROM_SDR_BASE, 1);

    gSimCore.SetGame(GAME_PGM);
}


static void load_espgalbl()
{
    g_fs.addSearchPath("../roms/pgm.zip");
    g_fs.addSearchPath("../roms/espgalbl.zip");
    g_fs.addSearchPath("../roms/espgal.zip");

    gSimCore.mSDRAM->load_data("espgaluda_u8.bin", CPU_ROM_SDR_BASE, 1);
    gSimCore.mSDRAM->load_data("pgm_t01s.rom", TILE_ROM_SDR_BASE, 1);
    gSimCore.mSDRAM->load_data("cave_t04801w064.u19", TILE_ROM_SDR_BASE + 0x180000, 1);

    gSimCore.SetGame(GAME_PGM);
}


bool game_init(game_t game)
{
    g_fs.clearSearchPaths();
    g_fs.addSearchPath(".");
    g_fs.addSearchPath("../roms");

    switch (game)
    {
    case GAME_PGM:
        load_pgm();
        break;
    case GAME_TESTBIOS:
        load_testbios();
        break;
    case GAME_ESPGALBL:
        load_espgalbl();
        break;
    default:
        return false;
    }

    return true;
}

bool game_init_mra(const char *mra_path)
{
    g_fs.clearSearchPaths();

    // Add common ROM search paths
    std::vector<std::string> searchPaths = {".", "../roms/"};

    // Add ROM search paths
    for (const auto &path : searchPaths)
    {
        g_fs.addSearchPath(path);
    }

    // Add the directory containing the MRA file as a search path
    std::string mraPathStr(mra_path);
    size_t lastSlash = mraPathStr.find_last_of("/\\");
    if (lastSlash != std::string::npos)
    {
        g_fs.addSearchPath(mraPathStr.substr(0, lastSlash));
    }

    // Load the MRA file
    MRALoader loader;
    std::vector<uint8_t> romData;
    uint32_t address = 0;

    if (!loader.load(mra_path, romData, address))
    {
        printf("Failed to load MRA file '%s': %s\n", mra_path, loader.getLastError().c_str());
        return false;
    }

    printf("Loaded MRA: %s\n", mra_path);
    printf("ROM data size: %zu bytes\n", romData.size());

    if (address == 0)
    {
        if (!gSimCore.SendIOCTLData(0, romData))
        {
            printf("Failed to send ROM data via ioctl\n");
            return false;
        }
    }
    else
    {
        if (!gSimCore.SendIOCTLDataDDR(0, address, romData))
        {
            printf("Failed to send ROM data via DDR\n");
            return false;
        }
    }

    printf("Successfully loaded MRA: %s\n", mra_path);
    return true;
}
