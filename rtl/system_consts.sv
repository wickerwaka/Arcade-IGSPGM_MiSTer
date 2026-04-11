package system_consts;
    parameter int SSIDX_GLOBAL = 0;
    parameter int SSIDX_WORK_RAM = 1;
    parameter int SSIDX_VIDEO_RAM = 2;
    parameter int SSIDX_PAL_RAM = 3;
    parameter int SSIDX_IGS023 = 4;
    parameter int SSIDX_Z80_RAM = 5;
    parameter int SSIDX_Z80 = 6;
    parameter int SSIDX_IGS026_X = 7;

    parameter bit [31:0] SS_DDR_BASE       = 32'h3E00_0000;
    parameter bit [31:0] B_ROM_DDR_BASE    = 32'h3800_0000;
    parameter bit [31:0] A_ROM_DDR_BASE    = 32'h3900_0000;
    parameter bit [31:0] DOWNLOAD_DDR_BASE = 32'h3000_0000;

    parameter bit [31:0] CPU_ROM_SDR_BASE       = 32'h0000_0000;
    parameter bit [31:0] TILE_ROM_SDR_BASE      = 32'h0100_0000;
    parameter bit [31:0] MUSIC_ROM_SDR_BASE     = 32'h0200_0000;

    typedef enum bit [3:0] {
        STORAGE_SDR,
        STORAGE_DDR,
        STORAGE_BLOCK
    } region_storage_t;

    typedef enum bit [3:0] {
        ENCODING_NORMAL
    } region_encoding_t;

    typedef struct packed {
        bit [31:0] base_addr;
        region_storage_t storage;
        region_encoding_t encoding;
    } region_t;

    parameter region_t REGION_CPU_ROM       = '{ base_addr:CPU_ROM_SDR_BASE,       storage:STORAGE_SDR,   encoding:ENCODING_NORMAL };
    parameter region_t REGION_TILE_ROM      = '{ base_addr:TILE_ROM_SDR_BASE,      storage:STORAGE_SDR,   encoding:ENCODING_NORMAL };
    parameter region_t REGION_MUSIC_ROM     = '{ base_addr:MUSIC_ROM_SDR_BASE,     storage:STORAGE_SDR,   encoding:ENCODING_NORMAL };
    parameter region_t REGION_A_ROM         = '{ base_addr:A_ROM_DDR_BASE,         storage:STORAGE_DDR,   encoding:ENCODING_NORMAL };
    parameter region_t REGION_B_ROM         = '{ base_addr:B_ROM_DDR_BASE,         storage:STORAGE_DDR,   encoding:ENCODING_NORMAL };

    parameter region_t LOAD_REGIONS[5] = '{
        REGION_CPU_ROM,
        REGION_TILE_ROM,
        REGION_MUSIC_ROM,
        REGION_A_ROM,
        REGION_B_ROM
    };

    typedef enum bit [7:0] {
        GAME_PGM
    } game_t;

    typedef struct packed {
        game_t    game;
        bit [7:0] unused;
    } board_cfg_t;

endpackage


