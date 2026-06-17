// Generalized read-only DDR ROM cache.  Direct-mapped, line-fill over a DDR
// burst, registered-address hit detection with a cross-port fill hazard guard.
// Ported from arm_rom_cache.sv and parameterized by DATA_WIDTH so it can back
// both the 32-bit ARM external ROM and the 16-bit 68k program ROM.
//
//   DATA_WIDTH = 32 -> word select = addr[2]      (ARM)
//   DATA_WIDTH = 16 -> word select = addr[2:1]    (68k)
module ddr_rom_cache #(
    parameter int      DATA_WIDTH  = 32,
    parameter int      ADDR_BITS   = 23,        // byte-address window
    parameter int      CACHE_LINES = 128,
    parameter int      LINE_BYTES  = 32,
    parameter [31:0]   DDR_BASE    = 32'h0      // byte base of the ROM in DDR
)(
    input  logic                  clk,
    input  logic                  reset,

    input  logic [ADDR_BITS-1:0]  addr,         // byte address within the window
    input  logic                  req,
    output logic [DATA_WIDTH-1:0] rdata,
    output logic                  ready,

    ddr_if.to_host                ddr
);

    localparam int BEATS = LINE_BYTES / 8;       // 64-bit DDR beats per line
    localparam int OFFB  = $clog2(LINE_BYTES);
    localparam int IDXB  = $clog2(CACHE_LINES);
    localparam int TAGB  = ADDR_BITS - OFFB - IDXB;
    localparam int BW    = $clog2(BEATS);        // beat index width
    localparam int WLSB  = $clog2(DATA_WIDTH/8); // byte offset of the data word (1=16b, 2=32b)
    localparam int WSELB = 3 - WLSB;             // word-within-beat select width

    logic              cache_valid[0:CACHE_LINES-1];

    wire [IDXB-1:0]  idx = addr[OFFB+IDXB-1 : OFFB];
    wire [TAGB-1:0]  tag = addr[ADDR_BITS-1 : OFFB+IDXB];

    wire [BW-1:0]    beat_sel = addr[OFFB-1:3];  // which 64-bit beat in the line
    wire [WSELB-1:0] wsel     = addr[2:WLSB];    // which data word in the beat

    logic [ADDR_BITS-1:WLSB] word_d;
    always_ff @(posedge clk) word_d <= addr[ADDR_BITS-1:WLSB];
    wire addr_stable = (addr[ADDR_BITS-1:WLSB] == word_d);

    typedef enum logic [1:0] { IDLE, REQ, FILL } state_t;
    state_t          state;
    logic [IDXB-1:0] fill_idx;
    logic [TAGB-1:0] fill_tag;
    logic [BW-1:0]   fill_beat;
    logic [31:0]     fill_addr;                  // byte addr of the line base in DDR

    // Port A: line fill writes.  Port B: read (registered addr = idx:beat).
    wire [63:0] data_q;
    wire        fill_we = (state == FILL) & ddr.rdata_ready;
    dualport_ram_unreg #(.WIDTH(64), .WIDTHAD(IDXB+BW)) cache_data(
        .clock_a(clk), .wren_a(fill_we), .address_a({fill_idx, fill_beat}), .data_a(ddr.rdata), .q_a(),
        .clock_b(clk), .wren_b(1'b0),    .address_b({idx, beat_sel}),       .data_b(64'd0),     .q_b(data_q)
    );
    assign rdata = data_q[DATA_WIDTH*wsel +: DATA_WIDTH];

    wire tag_we = (state == FILL) & ddr.rdata_ready & (fill_beat == BW'(BEATS-1));
    wire [TAGB-1:0] tag_q;
    dualport_ram_unreg #(.WIDTH(TAGB), .WIDTHAD(IDXB)) ctag_ram(
        .clock_a(clk), .wren_a(tag_we), .address_a(fill_idx), .data_a(fill_tag), .q_a(),
        .clock_b(clk), .wren_b(1'b0),   .address_b(idx),      .data_b('0),       .q_b(tag_q)
    );

    logic just_filled;

    wire hit = req & addr_stable & cache_valid[idx] & (tag_q == tag);

    assign ready = ~req | (hit & ~just_filled);

    assign ddr.acquire    = (state != IDLE);
    assign ddr.write      = 1'b0;
    assign ddr.wdata      = 64'd0;
    assign ddr.byteenable = 8'hff;

    integer i;
    always_ff @(posedge clk) begin
        if (reset) begin
            state    <= IDLE;
            ddr.read <= 1'b0;
            ddr.addr <= 32'd0;
            ddr.burstcnt <= 8'd0;
            just_filled <= 1'b0;
            for (i = 0; i < CACHE_LINES; i = i + 1) cache_valid[i] <= 1'b0;
        end else begin
            just_filled <= 1'b0;
            case (state)
                IDLE: begin
                    ddr.read <= 1'b0;
                    // miss only when address stable (registered tag valid) and not
                    // the cycle after a fill (stale cross-port tag).
                    if (req & addr_stable & ~hit & ~just_filled) begin
                        fill_idx  <= idx;
                        fill_tag  <= tag;
                        fill_beat <= '0;
                        // line base byte address in DDR
                        fill_addr <= DDR_BASE + { {(32-ADDR_BITS){1'b0}},
                                                  addr[ADDR_BITS-1:OFFB], {OFFB{1'b0}} };
                        state     <= REQ;
                    end
                end
                REQ: begin
                    if (~ddr.busy) begin
                        ddr.read     <= 1'b1;
                        ddr.addr     <= fill_addr;
                        ddr.burstcnt <= BEATS[7:0];
                        state        <= FILL;
                    end
                end
                FILL: begin
                    if (~ddr.busy) ddr.read <= 1'b0;
                    if (ddr.rdata_ready) begin
                        fill_beat <= fill_beat + 1'b1;
                        if (fill_beat == BW'(BEATS-1)) begin
                            cache_valid[fill_idx] <= 1'b1;
                            just_filled           <= 1'b1;
                            state                 <= IDLE;
                        end
                    end
                end
                default: state <= IDLE;
            endcase
        end
    end

endmodule
