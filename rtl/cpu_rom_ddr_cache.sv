import system_consts::*;

// 68000 program-ROM cache backed by DDR.  Drop-in for the old SDRAM `rom_cache`:
// same CPU-facing interface (as_n / dtack_n / cpu_addr / data, BIOS vs CART base
// select) but fed by a DDR line cache instead of the SDRAM toggle handshake.
//
// BIOS and CART program ROM live contiguously in DDR (BIOS at
// BIOS_PROG_ROM_DDR_BASE, CART 1 MB above it), so a single ddr_rom_cache / tag
// space covers both.  `busy` drives the CPU clock-enable freeze in PGM.sv.
module cpu_rom_ddr_cache(
    input               clk,
    input               reset,

    input               cart_present,
    input  [22:0]       cart_base_addr,   // 68k word address where CART prog begins

    input               as_n,
    output              dtack_n,
    input  [22:0]       cpu_addr,         // 68k word address
    output logic [15:0] data,
    output              busy,             // high while a ROM fetch is outstanding

    ddr_if.to_host      ddr
);

    // CART region begins 0x100000 bytes into the linear DDR window (BIOS is 1 MB).
    wire cart_access = cart_present & (cpu_addr >= cart_base_addr) & ~as_n;

    wire [23:0] lin_addr = cart_access
        ? (24'h10_0000 + { (cpu_addr - cart_base_addr), 1'b0 })  // CART: 0x100000 + word*2
        : { cpu_addr, 1'b0 };                                    // BIOS: word*2

    wire        cache_ready;

    ddr_rom_cache #(
        .DATA_WIDTH (16),
        .ADDR_BITS  (24),
        .CACHE_LINES(256),
        .LINE_BYTES (32),
        .DDR_BASE   (BIOS_PROG_ROM_DDR_BASE)
    ) cache(
        .clk, .reset,
        .addr (lin_addr),
        .req  (~as_n),
        .rdata(data),
        .ready(cache_ready),
        .ddr
    );

    // dtack_n feeds a wired-OR of all bus-cycle stalls, so it must be 0 (no stall)
    // except while actively filling a ROM line - matching the old rom_cache, which
    // only raised dtack_n during a miss.  For non-ROM cycles (as_n=1) this is 0.
    assign busy    = ~as_n & ~cache_ready;
    assign dtack_n = busy;

endmodule
