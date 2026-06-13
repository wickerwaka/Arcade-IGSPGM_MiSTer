// Horizontal scaler for consumer CRT width correction.
//

// Input line timing (50MHz clocks, 0 = hblank rising edge):
//   total 3200, hblank 0..959, hsync 315..629, active 960..3199 (448 px)
module video_hscale(
    input clk, // 50MHz

    input enable,
    input signed [4:0] scale,  // k: active width = 448 * (80+k)/80 input px
    input signed [4:0] offset, // hsync position adjust, input pixels

    output reg en_lat, // enable latched at vblank, for downstream muxes

    // Input stream at ce_pix_in rate (10MHz)
    input ce_pix_in,
    input [7:0] r_in,
    input [7:0] g_in,
    input [7:0] b_in,
    input hb_in,
    input vb_in,
    input vs_in,

    // Output stream, one pixel per clk
    output reg [7:0] r_out,
    output reg [7:0] g_out,
    output reg [7:0] b_out,
    output reg hs_out,
    output reg hb_out,
    output reg vs_out,
    output reg vb_out
);

reg hb_in_d, vb_in_d;
wire line_start = hb_in & ~hb_in_d;

reg [11:0] line_clk;

// Parameters latched at vblank rising edge so geometry only changes
// between frames
reg [6:0] step;        // 80 + k, output clocks per input pixel * 16
reg [11:0] active_len; // 28 * step = 448 * step / 16 output clocks
reg [11:0] rd_start;   // line_clk where readout begins
reg [11:0] hs_start;
reg [11:0] hs_end;
reg [11:0] blank_lc;   // line_clk in the next line where the active spill ends

wire signed [5:0] k = {scale[4], scale};
wire [5:0] abs_k = scale[4] ? -k[5:0] : k[5:0];
// Signed throughout: a single unsigned operand would make the whole
// expression unsigned and zero-extend negative offsets
wire signed [11:0] hs_pos = 12'sd315 + 12'sd14 * signed'({6'b0, abs_k}) + 12'sd5 * signed'({{7{offset[4]}}, offset});

always_ff @(posedge clk) begin
    vb_in_d <= vb_in;
    if (vb_in & ~vb_in_d) begin
        en_lat <= enable;
        step <= 7'(80 + k);
        active_len <= 12'(28 * (80 + k));
        // Readout must trail the input pixel writes. Downscale reads
        // faster than 1px/5clk, so it starts later to avoid underrunning
        // at the end of the line. Max buffer occupancy stays < 128 px.
        rd_start <= 12'(976 + (scale[4] ? 28 * abs_k : 6'd0));
        // Base 315, auto-center shift 14*|k| (half the width delta works
        // out the same for both scale signs because of rd_start), user
        // offset in input pixels
        hs_start <= unsigned'(hs_pos);
        hs_end <= unsigned'(hs_pos + 12'sd315);
        // rd_start + active_len - 3200: where the readout (and its spill
        // into the next line) finishes. 16 for downscale/100%, up to 436.
        blank_lc <= 12'(16 + (scale[4] ? 6'd0 : 28 * k[5:0]));
    end
end

// Output vblank/vsync are regenerated, not passed through: each line's
// input vblank/vsync is captured at line_start, then applied at the point
// where this scaled line's active readout actually ends (blank_lc into
// the line). This aligns the vertical blanking edges with the spilled
// active region so the DE waveform has one clean pulse per line.
reg vbl_line, vsl_line;

// Write side: store input pixels at ce_pix_in during active video
reg [8:0] wr_idx;
wire wr_en = ce_pix_in & ~hb_in;

// Read side: Bresenham DDA, advances the read index every step/16 clocks.
// acc/step is the sub-pixel phase, available here if interpolation between
// adjacent pixels is ever wanted.
reg [11:0] active_cnt;
reg [8:0] rd_idx;
reg [6:0] acc;
reg reading_cur_line; // readout spills past the line wrap at high scales

wire rd_active = |active_cnt;
// Don't start a readout on vblank lines: they carry no visible content,
// and a vblank-line readout would spill garbage into the first active
// line right where vb_out releases, leaving a short DE sliver. (A real
// active line's spill into the first vblank line is fine - vb_out rises
// over it.) vbl_line is latched at line_start, valid well before rd_start.
wire rd_load = (line_clk == rd_start) & ~vbl_line;

wire [23:0] rd_q;

dualport_ram_unreg #(.WIDTH(24), .WIDTHAD(7)) line_buf(
    .clock_a(clk),
    .wren_a(wr_en),
    .address_a(wr_idx[6:0]),
    .data_a({r_in, g_in, b_in}),
    .q_a(),

    .clock_b(clk),
    .wren_b(0),
    .address_b(rd_idx[6:0]),
    .data_b(0),
    .q_b(rd_q)
);

// Output pipeline: address (0) -> ram q (1) -> registered output (2).
// The first address cycle is one clock after rd_load, so hsync gets an
// extra delay stage to keep the same alignment with the active region.
reg rd_active_d1;
reg hs_d1, hs_d2;

reg debug_underrun /* verilator public_flat */;
reg debug_overflow /* verilator public_flat */;

always_ff @(posedge clk) begin
    hb_in_d <= hb_in;

    if (line_start) begin
        line_clk <= 0;
        wr_idx <= 0;
        reading_cur_line <= 0;
        // Capture this line's vblank/vsync (valid at the line boundary)
        vbl_line <= vb_in;
        vsl_line <= vs_in;
    end else begin
        line_clk <= line_clk == 12'd3199 ? 12'd0 : line_clk + 12'd1;
    end

    // Flip the regenerated vertical blanking where the active spill ends
    if (line_clk == blank_lc) begin
        vb_out <= vbl_line;
        vs_out <= vsl_line;
    end

    if (wr_en) begin
        wr_idx <= wr_idx + 9'd1;
    end

    if (rd_load) begin
        active_cnt <= active_len;
        rd_idx <= 0;
        acc <= 0;
        reading_cur_line <= 1;
    end else if (rd_active) begin
        active_cnt <= active_cnt - 12'd1;
        if (acc + 7'd16 >= step) begin
            acc <= acc + 7'd16 - step;
            rd_idx <= rd_idx + 9'd1;
        end else begin
            acc <= acc + 7'd16;
        end
    end

    if (reading_cur_line & rd_active & (rd_idx >= wr_idx)) debug_underrun <= 1;
    if (reading_cur_line & ((wr_idx - rd_idx) >= 9'd128)) debug_overflow <= 1;

    rd_active_d1 <= rd_active;
    hs_d1 <= line_clk >= hs_start && line_clk < hs_end;
    hs_d2 <= hs_d1;

    {r_out, g_out, b_out} <= rd_q;
    hb_out <= ~rd_active_d1;
    hs_out <= hs_d2;
end

endmodule
