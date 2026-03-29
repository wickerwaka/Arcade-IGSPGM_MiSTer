#ifndef GAMES_H
#define GAMES_H 1

#include <stdint.h>

enum game_t : uint8_t
{
    GAME_PGM = 0,
    GAME_TESTBIOS,
    GAME_ESPGALBL,

    N_GAMES,

    GAME_INVALID = 0xff
};

static const uint32_t CPU_ROM_SDR_BASE = 0x00000000;
static const uint32_t TILE_ROM_SDR_BASE = 0x01000000;
static const uint32_t MUSIC_ROM_SDR_BASE = 0x02000000;

game_t game_find(const char *name);
const char *game_name(game_t game);

bool game_init(game_t game);
bool game_init_mra(const char *mra_path);

#endif // GAMES_H
