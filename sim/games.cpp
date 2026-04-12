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

static const char *gGameNames[N_GAMES] = {
    "pgm",
    "testbios",
    "pgm_test",
    "espgalbl",
};

Game GameFind(const char *name)
{
    for (int i = 0; i < N_GAMES; i++)
    {
        if (!strcasecmp(name, gGameNames[i]))
        {
            return (Game)i;
        }
    }

    return GAME_INVALID;
}

const char *GameName(Game game)
{
    if (game == GAME_INVALID)
        return "INVALID";
    return gGameNames[game];
}

static void LoadPgm()
{
    gFileSearch.AddSearchPath("../roms/pgm.zip");

    gSimCore.mSDRAM->LoadData("pgm_p02s.u20", CPU_ROM_SDR_BASE, 1);
    gSimCore.mSDRAM->LoadData("pgm_t01s.rom", TILE_ROM_SDR_BASE, 1);
    gSimCore.mSDRAM->LoadData("pgm_m01s.rom", MUSIC_ROM_SDR_BASE, 1);

    gSimCore.SetGame(GAME_PGM);
}

static void LoadPgmTest()
{
    gFileSearch.AddSearchPath("../testroms/build/pgm_test/pgm/");
    gFileSearch.AddSearchPath("../roms/pgm.zip");

    gSimCore.mSDRAM->LoadData("pgm_p02s.u20", CPU_ROM_SDR_BASE, 1);
    gSimCore.mSDRAM->LoadData("pgm_t01s.rom", TILE_ROM_SDR_BASE, 1);
    gSimCore.mSDRAM->LoadData("pgm_m01s.rom", MUSIC_ROM_SDR_BASE, 1);

    gSimCore.SetGame(GAME_PGM_TEST);
}


static void LoadTestbios()
{
    gFileSearch.AddSearchPath("../roms/pgm.zip");

    gSimCore.mSDRAM->LoadData("testbios.bin", CPU_ROM_SDR_BASE, 1);
    gSimCore.mSDRAM->LoadData("pgm_t01s.rom", TILE_ROM_SDR_BASE, 1);

    gSimCore.SetGame(GAME_PGM);
}


static void LoadEspgalbl()
{
    gFileSearch.AddSearchPath("../roms/pgm.zip");
    gFileSearch.AddSearchPath("../roms/espgalbl.zip");
    gFileSearch.AddSearchPath("../roms/espgal.zip");

    gSimCore.mSDRAM->LoadData("espgaluda_u8.bin", CPU_ROM_SDR_BASE, 1);
    gSimCore.mSDRAM->LoadData("pgm_t01s.rom", TILE_ROM_SDR_BASE, 1);
    gSimCore.mSDRAM->LoadData("cave_t04801w064.u19", TILE_ROM_SDR_BASE + 0x180000, 1);

    gSimCore.SetGame(GAME_PGM);
}


bool GameInit(Game game)
{
    gFileSearch.ClearSearchPaths();
    gFileSearch.AddSearchPath(".");

    switch (game)
    {
    case GAME_PGM:
        LoadPgm();
        break;
    case GAME_TESTBIOS:
        LoadTestbios();
        break;
    case GAME_PGM_TEST:
        LoadPgmTest();
        break;
    case GAME_ESPGALBL:
        LoadEspgalbl();
        break;
    default:
        return false;
    }

    return true;
}

bool GameInitMra(const char *mraPath)
{
    gFileSearch.ClearSearchPaths();

    // Add common ROM search paths
    std::vector<std::string> searchPaths = {".", "../roms/"};

    // Add ROM search paths
    for (const auto &path : searchPaths)
    {
        gFileSearch.AddSearchPath(path);
    }

    // Add the directory containing the MRA file as a search path
    std::string mraPathStr(mraPath);
    size_t lastSlash = mraPathStr.find_last_of("/\\");
    if (lastSlash != std::string::npos)
    {
        gFileSearch.AddSearchPath(mraPathStr.substr(0, lastSlash));
    }

    // Load the MRA file
    MRALoader loader;
    std::vector<uint8_t> romData;
    uint32_t address = 0;

    if (!loader.Load(mraPath, romData, address))
    {
        printf("Failed to load MRA file '%s': %s\n", mraPath, loader.GetLastError().c_str());
        return false;
    }

    printf("Loaded MRA: %s\n", mraPath);
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

    printf("Successfully loaded MRA: %s\n", mraPath);
    return true;
}
