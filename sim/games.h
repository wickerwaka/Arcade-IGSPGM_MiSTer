#ifndef GAMES_H
#define GAMES_H 1

#include <stdint.h>

enum Game : uint8_t
{
    GAME_PGM = 0,
    GAME_TESTBIOS,
    GAME_PGM_TEST,
    GAME_ESPGALBL,

    N_GAMES,

    GAME_INVALID = 0xff
};

static const uint32_t BIOS_PROG_ROM_SDR_BASE   = 0x00000000;
static const uint32_t BIOS_TILE_ROM_SDR_BASE   = 0x00100000;
static const uint32_t BIOS_MUSIC_ROM_SDR_BASE  = 0x00300000;

static const uint32_t CART_PROG_ROM_SDR_BASE   = 0x01000000;
static const uint32_t CART_TILE_ROM_SDR_BASE   = 0x02000000;
static const uint32_t CART_MUSIC_ROM_SDR_BASE  = 0x04000000;
static const uint32_t CART_B_ROM_SDR_BASE      = 0x06000000;

Game GameFind(const char *name);
const char *GameName(Game game);
const char *GameLoadedShortName();
bool GameIsPgmFilePath(const char *name);

bool GameInit(Game game);
bool GameInitPgmFile(const char *path);
bool GameInitMra(const char *mraPath);

#endif // GAMES_H
