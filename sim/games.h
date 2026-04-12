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

static const uint32_t CPU_ROM_SDR_BASE = 0x00000000;
static const uint32_t TILE_ROM_SDR_BASE = 0x01000000;
static const uint32_t MUSIC_ROM_SDR_BASE = 0x02000000;

Game GameFind(const char *name);
const char *GameName(Game game);

bool GameInit(Game game);
bool GameInitMra(const char *mraPath);

#endif // GAMES_H
