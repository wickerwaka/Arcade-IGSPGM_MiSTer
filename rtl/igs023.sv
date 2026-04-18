module tile_shifter #(
    parameter TILE_WIDTH=8,
    parameter BPP=4,
    parameter LENGTH=4
    )
(
    input                                         clk,
    input                                         ce,

    input                                         load,
    input [$clog2(LENGTH)-1:0]                    load_index,
    input [4:0]                                   load_color,
    input [(TILE_WIDTH*BPP)-1:0]                    load_data,
    input                                         load_flip,

    input [$clog2(LENGTH)+$clog2(TILE_WIDTH)-1:0] tap,
    output reg [4+BPP:0]                              dot_out
);

reg [4:0] color_buf[LENGTH];
reg [(TILE_WIDTH*BPP)-1:0] pixel_buf[LENGTH];

wire [$clog2(LENGTH)-1:0] tap_index = tap[$left(tap):$clog2(TILE_WIDTH)];
wire [$clog2(TILE_WIDTH)-1:0] tap_pixel = tap[$clog2(TILE_WIDTH)-1:0];

always_ff @(posedge clk) begin
    if (load) begin

        color_buf[load_index] <= load_color;
        if (~load_flip) begin
            int i;
            for( i = 0; i < TILE_WIDTH; i = i + 1 ) begin
                pixel_buf[load_index][(BPP * ((TILE_WIDTH-1) - i)) +: BPP] <= load_data[(BPP * i) +: BPP];
            end
        end else begin
            pixel_buf[load_index] <= load_data;
        end
    end

    if (ce) begin
        dot_out <= { color_buf[tap_index], pixel_buf[tap_index][(BPP * tap_pixel) +: BPP] };
    end
end

endmodule

module IGS023 #(parameter SS_IDX=-1) (
    input clk,
    input ce_50m,
    output ce_pixel,

    input reset,

    // CPU interface
    input [23:0] cpu_addr,
    input [15:0] cpu_din,
    output reg [15:0] cpu_dout,
    input cpu_lds_n,
    input cpu_uds_n,
    input cpu_rw,
    input cpu_cs_n,
    output cpu_dtack_n,

    // VRAM interface
    output logic [15:0] vram_addr,
    input        [15:0] vram_din,
    output       [15:0] vram_dout,
    output logic        vram_we_u_n,
    output logic        vram_we_l_n,
    
    output logic [14:0] pal_addr,
    input         [7:0] pal_din,
    output        [7:0] pal_dout,
    output logic        pal_we_n,


    // ROM interface
    output reg [23:0] rom_address,
    input      [31:0] rom_data,
    output reg        rom_req,
    input             rom_ack,

    output reg        irq6,
    output reg        irq4,

    // Video interface
    output reg [14:0] color,
    output hsync,
    output hblank,
    output vsync,
    output vblank,

    ssbus_if.slave ssbus
);

typedef enum bit [2:0]
{
    CPU_ACCESS,
    FG_ATTRIB0,
    FG_ATTRIB1,
    BG_ATTRIB0,
    BG_ATTRIB1
} access_state_t;

access_state_t idx_to_state[32] = '{
    FG_ATTRIB0, FG_ATTRIB1, BG_ATTRIB0, BG_ATTRIB1, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS,
    FG_ATTRIB0, FG_ATTRIB1, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS,
    FG_ATTRIB0, FG_ATTRIB1, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS,
    FG_ATTRIB0, FG_ATTRIB1, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS, CPU_ACCESS
};

access_state_t access_state;
access_state_t next_access_state;



reg [4:0] ce_pixel_shift;
assign ce_pixel = ce_50m & ce_pixel_shift[4];
wire ce_pixel_pre1 = ce_50m & ce_pixel_shift[3];
wire ce_pixel_pre2 = ce_50m & ce_pixel_shift[2];
wire ce_pixel_post1 = ce_50m & ce_pixel_shift[0];
wire ce_pixel_post2 = ce_50m & ce_pixel_shift[1];
wire ce_pixel_post3 = ce_50m & ce_pixel_shift[2];

always_ff @(posedge clk) begin
    if (~|ce_pixel_shift) begin
        ce_pixel_shift <= 5'b00001;
    end else if (ce_50m) begin
        ce_pixel_shift <= {ce_pixel_shift[3:0], ce_pixel_shift[4]};
    end
end

reg dtack_n;
reg prev_cs_n;
wire is_reg_access = cpu_addr[21:20] == 2'b11;
wire is_vram_access = cpu_addr[21:20] == 2'b01;
wire is_pal_access = cpu_addr[21:20] == 2'b10;

reg ram_pending = 0;
reg [1:0] ram_access = 0;

reg [15:0] sprite_data[256 * 8];
reg [15:0] zoom_table[32];
reg [15:0] ctrl[16];

wire [15:0] bg_x = ctrl[3];
wire [15:0] bg_y = ctrl[2];
wire [15:0] fg_x = ctrl[6];
wire [15:0] fg_y = ctrl[5];
wire [15:0] ctrl_flags = ctrl[14];
wire bg_en = ~ctrl_flags[12];
wire fg_en = ~ctrl_flags[11];

assign cpu_dtack_n = cpu_cs_n ? 0 : dtack_n;

assign pal_dout = pal_addr[0] ? cpu_din[7:0] : cpu_din[15:8];
assign vram_dout = cpu_din;


// 0 is the start of blanking
reg [8:0] vcnt;
reg [9:0] hcnt;

// 0 is the top/right of the screen
wire [10:0] logical_hcnt = { 1'b0, hcnt } - 11'd189;
wire [10:0] logical_vcnt = { 2'b0, vcnt } - 11'd38;


assign hsync = (hcnt >= 63 && hcnt < (63 + 63));
assign hblank = hcnt < 192;
assign vsync = (vcnt >= 14 && vcnt < (14 + 8));
assign vblank = vcnt < 38;


always @(posedge clk) begin
    if (ce_pixel) begin
        hcnt <= hcnt + 1;

        if (hcnt == 639) begin
            hcnt <= 0;
            vcnt <= vcnt + 1;
            if (vcnt == 261) begin
                vcnt <= 0;
            end
        end
    end
end

wire [10:0] bg_hofs = bg_x[10:0]; // + bg_rowscroll[10:0];
wire [9:0] bg_dot;
wire [8:0] fg_dot;
reg [15:0] bg_rowscroll;
reg [15:0] bg_code, bg_attrib;
reg [15:0] fg_code, fg_attrib;

reg [(32*5)-1:0] bg_data;
reg bg_load, fg_load;
reg fg_rom_req;
reg [2:0] bg_rom_req;
reg [23:0] bg_rom_address;
reg [23:0] fg_rom_address;

reg [1:0] rom_req_ch;

wire bg_flipx = bg_attrib[6];
wire bg_flipy = bg_attrib[7];
wire fg_flipx = fg_attrib[6];
wire fg_flipy = fg_attrib[7];

wire [10:0] bg_draw_hcnt = (logical_hcnt - bg_hofs);
wire [10:0] fg_draw_hcnt = (logical_hcnt - fg_x[10:0]);

reg [1:0] bg_load_index;
reg [10:0] bg_load_hcnt, fg_load_hcnt;

tile_shifter #(
    .TILE_WIDTH(32),
    .BPP(5)
    )bg_shift(
    .clk, .ce(ce_pixel),
    .tap(bg_draw_hcnt[6:0]),
    .load_index(bg_load_hcnt[6:5]),
    .load_data(bg_data),
    .load_color(bg_attrib[5:1]),
    .load_flip(~bg_flipx),
    .dot_out(bg_dot),
    .load(bg_load)
);

tile_shifter fg_shift(
    .clk, .ce(ce_pixel),
    .tap(fg_draw_hcnt[4:0]),
    .load_index(fg_load_hcnt[4:3]),
    .load_data({rom_data}),
    .load_color(fg_attrib[5:1]),
    .load_flip(~fg_flipx),
    .dot_out(fg_dot),
    .load(fg_load)
);

wire [15:0] bg_addr           = 16'h0000;
wire [15:0] fg_addr           = 16'h4000;
wire [15:0] bg_rowscroll_addr = 16'h7000;

reg [6:0] color_read;
reg [14:0] color_addr;

always_ff @(posedge clk) begin
    if (ce_pixel) begin
        color <= {color_read, pal_din[7:0]};
        if (~&fg_dot[3:0])
            color_addr <= 14'h1000 + { 5'd0, fg_dot[8:4], fg_dot[3:0], 1'b0 };
        else
            color_addr <= 14'h0800 + { 4'd0, bg_dot, 1'b0 };
    end else if (ce_pixel_post2) begin
        color_addr[0] <= 1;
        color_read <= pal_din[6:0];
    end
end

always_comb begin
    bit [5:0] h;
    bit [10:0] v;

    vram_addr = 16'd0;
    vram_we_l_n = 1;
    vram_we_u_n = 1;

    pal_addr  = color_addr;
    pal_we_n = 1;

    v = 0;

    access_state = idx_to_state[hcnt[4:0]];
    next_access_state = idx_to_state[hcnt[4:0] + 5'd1];

    unique case(access_state)
        FG_ATTRIB0: begin
            v = logical_vcnt[10:0] - fg_y[10:0];
            vram_addr = fg_addr + { 3'b0, v[7:3], fg_load_hcnt[8:3], 2'b00 };
        end

        FG_ATTRIB1: begin
            v = logical_vcnt[10:0] - fg_y[10:0];
            vram_addr = fg_addr + { 3'b0, v[7:3], fg_load_hcnt[8:3], 2'b10 };
        end

        BG_ATTRIB0: begin
            v = logical_vcnt[10:0] + bg_y[10:0];
            vram_addr = bg_addr + { 2'b0, v[10:5], bg_load_hcnt[10:5], 2'b00 };
        end

        BG_ATTRIB1: begin
            v = logical_vcnt[10:0] + bg_y[10:0];
            vram_addr = bg_addr + { 2'b0, v[10:5], bg_load_hcnt[10:5], 2'b10 };
        end

        CPU_ACCESS: begin
            if (is_pal_access && ram_access == 2'd2) begin
                pal_addr = {cpu_addr[14:1], 1'b0};
                pal_we_n = cpu_rw;
            end else if (is_pal_access && ram_access == 2'd1) begin
                pal_addr = {cpu_addr[14:1], 1'b1};
                pal_we_n = cpu_rw;
            end else if (is_vram_access && |ram_access) begin
                vram_addr = cpu_addr[15:0];
                vram_we_l_n = cpu_lds_n | cpu_rw;
                vram_we_u_n = cpu_uds_n | cpu_rw;
            end
        end
        default: begin end
    endcase
end

reg vblank_prev;
reg hsync_prev;

wire irq6_en = ctrl[14][3];
wire irq4_en = ctrl[14][2];

reg [5:0] irq4_cnt;
always @(posedge clk) begin
    bit [10:0] v;
    bit [5:0] h;

    if (reset) begin
        dtack_n <= 1;
        ram_pending <= 0;
        ram_access <= 0;
        irq6 <= 0;
        irq4 <= 0;
    end begin
       
        // IRQ Generation
        vblank_prev <= vblank;
        hsync_prev <= hsync;
        if (vblank & ~vblank_prev & irq6_en) begin
            irq6 <= 1;
        end

        if (~irq6_en) begin
            irq6 <= 0;
        end

        if (~irq4_en) begin
            irq4 <= 0;
        end

        if (~vblank & vblank_prev) begin
            ctrl[7] <= 0;
        end else if (hsync & ~hsync_prev) begin
            ctrl[7] <= ctrl[7] + 1;

            if (irq4_cnt == 61) begin
                if (irq4_en) irq4 <= 1;
                irq4_cnt <= 0;
            end else begin
                irq4_cnt <= irq4_cnt + 1;
            end
        end

        // CPU interface handling
        prev_cs_n <= cpu_cs_n;
        if (~cpu_cs_n & prev_cs_n) begin // CS edge
            if (is_reg_access) begin
                if (cpu_rw) begin
                    cpu_dout <= ctrl[cpu_addr[15:12]];
                end else begin
                    if (~cpu_uds_n) ctrl[cpu_addr[15:12]][15:8] <= cpu_din[15:8];
                    if (~cpu_lds_n) ctrl[cpu_addr[15:12]][7:0]  <= cpu_din[7:0];
                end
                dtack_n <= 0;
            end else if (is_vram_access | is_pal_access) begin
                ram_pending <= 1;
             end
        end else if (cpu_cs_n) begin
            dtack_n <= 1;
        end

        if (ce_pixel) begin
            bg_load <= 0;
            fg_load <= 0;
            unique case(access_state)
                FG_ATTRIB0: fg_code <= vram_din;
                FG_ATTRIB1: begin
                    v = logical_vcnt[10:0] - fg_y[10:0];
                    fg_attrib <= vram_din;
                    fg_rom_address <= { 3'd0, fg_code, v[2:0], 2'd0 };
                    fg_rom_req <= 1;
                end

                BG_ATTRIB0: bg_code <= vram_din;
                BG_ATTRIB1: begin
                    v = logical_vcnt[10:0] + bg_y[10:0];
                    bg_attrib <= vram_din;
                    bg_rom_address <= { bg_code[14:0], v[4:0], 4'd0 } + { 2'b0, bg_code[14:0], v[4:0], 2'd0 };
                    bg_rom_req <= 5;
                end

                CPU_ACCESS: begin
                    if (is_pal_access) begin
                        if (ram_access == 2'd2) begin
                            cpu_dout[15:8] <= pal_din;
                            ram_access <= 2'd1;
                        end else if (ram_access == 2'd1) begin
                            cpu_dout[7:0] <= pal_din;
                            ram_access <= 0;
                            dtack_n <= 0;
                        end
                    end else if (is_vram_access) begin
                        if (|ram_access) begin
                            ram_access <= 0;
                            dtack_n <= 0;
                            cpu_dout <= vram_din;
                        end
                    end
                end
                default: begin end
            endcase

            case(next_access_state)
                CPU_ACCESS: begin
                    if (ram_pending) begin
                        ram_access <= 2'd2;
                        ram_pending <= 0;
                    end
                end

                FG_ATTRIB0: begin
                    fg_load_hcnt <= (logical_hcnt + 11'd16) - fg_x[10:0];
                end

                BG_ATTRIB0: begin
                    bg_load_hcnt <= (logical_hcnt + 11'd64) - bg_hofs;
                end


                default: begin
                end
            endcase
        end // ce_pixel

        if (rom_req == rom_ack) begin
            if (rom_req_ch == 1) begin
                bg_load <= bg_rom_req == 0;
                bg_data <= { rom_data, bg_data[(32*5)-1:32] };
                rom_req_ch <= 0;
            end else if (rom_req_ch == 2) begin
                fg_load <= 1;
                rom_req_ch <= 0;
            end else begin
                if (fg_rom_req) begin
                    rom_address <= fg_rom_address;
                    rom_req <= ~rom_req;
                    rom_req_ch <= 2;
                    fg_rom_req <= 0;
                end else if (bg_rom_req != 0) begin
                    rom_address <= bg_rom_address;
                    bg_rom_address <= bg_rom_address + 4;
                    rom_req <= ~rom_req;
                    rom_req_ch <= 1;
                    bg_rom_req <= bg_rom_req - 1;
                 end
            end
        end
    end

    ssbus.setup(SS_IDX, 16 + 32 + (256 * 8), 1);
    if (ssbus.access(SS_IDX)) begin
        if (ssbus.write) begin
            if (ssbus.addr < 32) begin
                zoom_table[ssbus.addr[4:0]] <= ssbus.data[15:0];
            end else if (ssbus.addr < 48) begin
                ctrl[ssbus.addr[3:0]] <= ssbus.data[15:0];
            end else begin
                sprite_data[ssbus.addr - 48] <= ssbus.data[15:0];
            end
            ssbus.write_ack(SS_IDX);
        end else if (ssbus.read) begin
            if (ssbus.addr < 32) begin
                ssbus.read_response(SS_IDX, { 48'b0, zoom_table[ssbus.addr[4:0]] });
            end else if (ssbus.addr < 48) begin
                ssbus.read_response(SS_IDX, { 48'b0, ctrl[ssbus.addr[3:0]] });
            end else begin
                ssbus.read_response(SS_IDX, { 48'b0, sprite_data[ssbus.addr - 48] });
            end
        end
    end
end

endmodule

