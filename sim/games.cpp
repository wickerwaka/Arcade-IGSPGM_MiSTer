#include "games.h"
#include "sim_core.h"
#include "sim_sdram.h"
#include "sim_ddr.h"
#include "mra_loader.h"
#include "sim_hierarchy.h"
#include "PGM.h"
#include "PGM___024root.h"

#include "file_search.h"
#include <string.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
struct PgmEntry
{
    uint32_t mapping = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
};

constexpr size_t kPgmHeaderSize = 1024;
constexpr std::array<uint8_t, 6> kPgmMagic = {'I', 'G', 'S', 'P', 'G', 'M'};
constexpr uint8_t kPgmVersionMajor = 0x00;
constexpr uint8_t kPgmVersionMinor = 0x10;

uint32_t ReadLe32(const uint8_t *data)
{
    return (static_cast<uint32_t>(data[0]) << 0) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

bool EndsWithCaseInsensitive(const std::string &value, const std::string &suffix)
{
    if (value.size() < suffix.size())
        return false;

    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin(),
                      [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
}

std::string ReadPaddedString(const std::vector<uint8_t> &buffer, size_t offset, size_t size)
{
    if (offset >= buffer.size())
        return {};

    const size_t available = std::min(size, buffer.size() - offset);
    std::string value(reinterpret_cast<const char *>(buffer.data() + offset), available);
    const size_t nul = value.find('\0');
    if (nul != std::string::npos)
        value.resize(nul);
    return value;
}

void ClearCartConfig()
{
    gSimCore.mTop->rootp->sim_top__DOT__cart_present = 0;
    gSimCore.mTop->rootp->sim_top__DOT__cart_prog_base = 0;
    gSimCore.mTop->rootp->sim_top__DOT__cart_tile_base = 0;
    gSimCore.mTop->rootp->sim_top__DOT__cart_music_base = 0;
}

bool LoadBasePgmBios()
{
    gFileSearch.AddSearchPath("../roms/pgm.zip");

    if (!gSimCore.mSDRAM->LoadData16be("pgm_p02s.u20", BIOS_PROG_ROM_SDR_BASE, 2))
        return false;
    if (!gSimCore.mSDRAM->LoadData("pgm_t01s.rom", BIOS_TILE_ROM_SDR_BASE, 1))
        return false;
    if (!gSimCore.mSDRAM->LoadData("pgm_m01s.rom", BIOS_MUSIC_ROM_SDR_BASE, 1))
        return false;

    return true;
}

bool LoadFileExact(const char *path, std::vector<uint8_t> &buffer)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        printf("Failed to open PGM file: %s\n", path);
        return false;
    }

    buffer.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (!file.good() && !file.eof())
    {
        printf("Failed to read PGM file: %s\n", path);
        return false;
    }

    return true;
}

bool ParsePgmEntry(const std::vector<uint8_t> &buffer, size_t offset, PgmEntry &entry)
{
    if (offset + 12 > buffer.size())
        return false;

    entry.mapping = ReadLe32(buffer.data() + offset + 0);
    entry.offset = ReadLe32(buffer.data() + offset + 4);
    entry.size = ReadLe32(buffer.data() + offset + 8);
    return true;
}

bool ValidatePgmEntry(const char *name, const PgmEntry &entry, size_t fileSize)
{
    if (entry.offset == 0 || entry.size == 0)
        return true;

    if (entry.offset > fileSize || entry.size > fileSize - entry.offset)
    {
        printf("Invalid %s entry: offset=0x%08X size=0x%08X file_size=0x%zx\n", name, entry.offset, entry.size, fileSize);
        return false;
    }

    return true;
}

bool LoadPgmEntry(const std::vector<uint8_t> &buffer, const char *name, const PgmEntry &entry, uint32_t destBase)
{
    if (entry.offset == 0 || entry.size == 0)
    {
        printf("Skipping %s (not present)\n", name);
        return true;
    }

    gSimCore.mSDRAM->Write(destBase, entry.size, buffer.data() + entry.offset);
    printf("Loaded %s: %u bytes from file offset 0x%08X to SDRAM 0x%08X (mapping 0x%08X)\n",
           name, entry.size, entry.offset, destBase, entry.mapping);
    return true;
}
}

static const char *gGameNames[N_GAMES] = {
    "pgm",
    "testbios",
    "pgm_test",
    "espgalbl",
};

static std::string gLoadedGameShortName = "unknown";

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

const char *GameLoadedShortName()
{
    return gLoadedGameShortName.c_str();
}

bool GameIsPgmFilePath(const char *name)
{
    return name != nullptr && EndsWithCaseInsensitive(name, ".pgm");
}

static void LoadPgm()
{
    LoadBasePgmBios();
    ClearCartConfig();
    gLoadedGameShortName = "pgm";
    gSimCore.SetGame(GAME_PGM);
}

static void LoadPgmTest()
{
    gFileSearch.AddSearchPath("../testroms/build/pgm_test/pgm/");
    LoadBasePgmBios();
    ClearCartConfig();
    gLoadedGameShortName = "pgm_test";
    gSimCore.SetGame(GAME_PGM_TEST);
}


static void LoadTestbios()
{
    gFileSearch.AddSearchPath("../roms/pgm.zip");

    gSimCore.mSDRAM->LoadData16be("testbios.bin", BIOS_PROG_ROM_SDR_BASE, 2);
    gSimCore.mSDRAM->LoadData("pgm_t01s.rom", BIOS_TILE_ROM_SDR_BASE, 1);

    ClearCartConfig();
    gLoadedGameShortName = "testbios";
    gSimCore.SetGame(GAME_PGM);
}


static void LoadEspgalbl()
{
    LoadPgm();

    gFileSearch.AddSearchPath("../roms/espgalbl.zip");
    gFileSearch.AddSearchPath("../roms/espgal.zip");

    gSimCore.mSDRAM->LoadData16be("espgaluda_u8.bin", CART_PROG_ROM_SDR_BASE, 2);
    gSimCore.mSDRAM->LoadData("cave_t04801w064.u19", CART_TILE_ROM_SDR_BASE, 1);

    gSimCore.mTop->rootp->sim_top__DOT__cart_present = 1;
    gSimCore.mTop->rootp->sim_top__DOT__cart_prog_base = 0;
    gSimCore.mTop->rootp->sim_top__DOT__cart_tile_base = 0x180000;
    gSimCore.mTop->rootp->sim_top__DOT__cart_music_base = 0;

    gLoadedGameShortName = "espgalbl";
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

bool GameInitPgmFile(const char *path)
{
    if (path == nullptr)
        return false;

    std::vector<uint8_t> buffer;
    if (!LoadFileExact(path, buffer))
        return false;

    if (buffer.size() < kPgmHeaderSize)
    {
        printf("Invalid PGM file '%s': too small (%zu bytes)\n", path, buffer.size());
        return false;
    }

    if (!std::equal(kPgmMagic.begin(), kPgmMagic.end(), buffer.begin()))
    {
        printf("Invalid PGM file '%s': bad magic\n", path);
        return false;
    }

    if (buffer[6] != kPgmVersionMajor || buffer[7] != kPgmVersionMinor)
    {
        printf("Invalid PGM file '%s': unsupported version %02X%02X\n", path, buffer[6], buffer[7]);
        return false;
    }

    const std::string shortName = ReadPaddedString(buffer, 24, 16);

    PgmEntry romP;
    PgmEntry romT;
    PgmEntry romM;
    PgmEntry romB;
    PgmEntry romA;
    if (!ParsePgmEntry(buffer, 512 + 0 * 12, romP) || !ParsePgmEntry(buffer, 512 + 1 * 12, romT) ||
        !ParsePgmEntry(buffer, 512 + 2 * 12, romM) || !ParsePgmEntry(buffer, 512 + 3 * 12, romB) ||
        !ParsePgmEntry(buffer, 512 + 4 * 12, romA))
    {
        printf("Invalid PGM file '%s': truncated header\n", path);
        return false;
    }

    if (!ValidatePgmEntry("romP", romP, buffer.size()) || !ValidatePgmEntry("romT", romT, buffer.size()) ||
        !ValidatePgmEntry("romM", romM, buffer.size()) || !ValidatePgmEntry("romB", romB, buffer.size()) ||
        !ValidatePgmEntry("romA", romA, buffer.size()))
    {
        return false;
    }

    gFileSearch.ClearSearchPaths();
    gFileSearch.AddSearchPath(".");
    if (!LoadBasePgmBios())
        return false;

    ClearCartConfig();
    gSimCore.mTop->rootp->sim_top__DOT__cart_present = 1;

    if (!LoadPgmEntry(buffer, "romP", romP, CART_PROG_ROM_SDR_BASE))
        return false;
    if (!LoadPgmEntry(buffer, "romT", romT, CART_TILE_ROM_SDR_BASE))
        return false;
    if (!LoadPgmEntry(buffer, "romM", romM, CART_MUSIC_ROM_SDR_BASE))
        return false;

    if (romP.offset != 0 && romP.size != 0)
        gSimCore.mTop->rootp->sim_top__DOT__cart_prog_base = romP.mapping;
    if (romT.offset != 0 && romT.size != 0)
        gSimCore.mTop->rootp->sim_top__DOT__cart_tile_base = romT.mapping;
    if (romM.offset != 0 && romM.size != 0)
        gSimCore.mTop->rootp->sim_top__DOT__cart_music_base = romM.mapping;

    gLoadedGameShortName = shortName.empty() ? "pgm" : shortName;

    printf("Loaded PGM cart file: %s\n", path);
    gSimCore.SetGame(GAME_PGM);
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
