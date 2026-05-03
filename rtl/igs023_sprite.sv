import system_consts::arom_offset_t;

module IGS023_Sprite(
    input clk,
    input ce_pixel,
    input scan_active,
    input frame_reset,
    input next_line,

    input reset,

    input dma_start,

    output [11:0] color_out,

    // DMA interface
    output reg cpu_br_n,
    output reg cpu_bgack_n,
    input      cpu_bg_n,
    input      cpu_as_n,
    input      cpu_dtack_n,
    output reg [15:0] dma_addr,
    input      [15:0] dma_din,
    
    // ROM interface
    output reg [23:0] brom_address,
    input      [63:0] brom_data,
    output reg        brom_req,
    input             brom_ack,
    
    ddr_if.to_host    ddr
);

typedef enum bit [4:0] {
    DMA_IDLE, DMA_BUS_REQUEST, DMA_READ0, DMA_READ1, DMA_READ2, DMA_READ3, DMA_FINISH,
    PRESCAN_LOAD, PRESCAN_INITIAL_BROM, PRESCAN_INITIAL_BROM_WAIT, PRESCAN_INITIAL_NEXT,
    PRESCAN_NEXT, PRESCAN_SCAN_TO_START, PRESCAN_BROM_WAIT,
    DRAW_INIT, DRAW_LINE_WAIT, DRAW_SEARCH_ACTIVE_LOAD, DRAW_SEARCH_ACTIVE_CHECK,
    DRAW_ROW, DRAW_SPAN, DRAW_ROW_END, DRAW_BROM_WAIT
} dma_state_t;

dma_state_t dma_state = DMA_IDLE;
reg [2:0] sprite_component_index;
reg [8:0] sprite_index;
reg [8:0] sprite_count;

reg [15:0] sprite_d0[256];
reg [15:0] sprite_d1[256];
reg [15:0] sprite_d2[256];
reg [15:0] sprite_d3[256];
reg [15:0] sprite_d4[256];

reg [10:0] spr_x;
reg [4:0] spr_x_scale;
reg [9:0] spr_y;
reg [4:0] spr_y_scale;
reg [22:0] spr_brom_addr;
reg spr_prio;
reg [4:0] spr_palette;
reg spr_x_flip;
reg spr_y_flip;
reg [5:0] spr_width;
reg [8:0] spr_height;

wire [9:0] spr_y_end = spr_y + { 1'b0, spr_height };

typedef struct
{
    bit [63:0] brom_cache;
    bit [15:0] brom_offset;
    arom_offset_t arom_offset;
    bit [9:0] line;
    bit        active;
} volatile_sprite_state_t;

volatile_sprite_state_t sprite_state[256];
volatile_sprite_state_t spr;

wire [22:0] brom_word_address = spr_y_flip ? (spr_brom_addr - { 7'b0, spr.brom_offset }) : (spr_brom_addr + { 7'b0, spr.brom_offset });
assign brom_address = { brom_word_address, 1'b0 };

logic [15:0] spr_brom_data;
always_comb begin
    bit [1:0] ofs;
    if (spr_y_flip)
        ofs = ~spr.brom_offset[1:0];
    else
        ofs = spr.brom_offset[1:0];

    case(ofs)
        0: spr_brom_data = spr.brom_cache[15:0];
        1: spr_brom_data = spr.brom_cache[31:16];
        2: spr_brom_data = spr.brom_cache[47:32];
        3: spr_brom_data = spr.brom_cache[63:48];
        default: spr_brom_data = 0;
    endcase
end

wire spr_load = dma_state == PRESCAN_LOAD || dma_state == DRAW_SEARCH_ACTIVE_LOAD;
wire spr_store = dma_state == PRESCAN_NEXT || dma_state == DRAW_ROW_END;

function automatic [4:0] count_zeros4(input [3:0] v);
begin
   case (v)
       4'h0: count_zeros4 = 5'd4;
       4'h1, 4'h2, 4'h4, 4'h8: count_zeros4 = 5'd3;
       4'h3, 4'h5, 4'h6, 4'h9, 4'hA, 4'hC: count_zeros4 = 5'd2;
       4'h7, 4'hB, 4'hD, 4'hE: count_zeros4 = 5'd1;
       default: count_zeros4 = 5'd0; // 4'hF
   endcase
end
endfunction

function automatic [4:0] count_zeros16(input [15:0] v);
begin
   count_zeros16 = count_zeros4(v[15:12])
                 + count_zeros4(v[11:8])
                 + count_zeros4(v[7:4])
                 + count_zeros4(v[3:0]);
end
endfunction

function automatic [15:0] reverse_bits16(input [15:0] v);
begin
    reverse_bits16 = {
        v[0],
        v[1],
        v[2],
        v[3],
        v[4],
        v[5],
        v[6],
        v[7],
        v[8],
        v[9],
        v[10],
        v[11],
        v[12],
        v[13],
        v[14],
        v[15]
    };
end
endfunction

function automatic arom_offset_t add_offset(input arom_offset_t a, input [4:0] b);
begin
    bit [4:0] num;
    bit [2:0] sum3;
    case(b)
        0:  num = { 3'd0, 2'd0 };
        1:  num = { 3'd0, 2'd1 };
        2:  num = { 3'd0, 2'd2 };
        3:  num = { 3'd1, 2'd0 };
        4:  num = { 3'd1, 2'd1 };
        5:  num = { 3'd1, 2'd2 };
        6:  num = { 3'd2, 2'd0 };
        7:  num = { 3'd2, 2'd1 };
        8:  num = { 3'd2, 2'd2 };
        9:  num = { 3'd3, 2'd0 };
        10: num = { 3'd3, 2'd1 };
        11: num = { 3'd3, 2'd2 };
        12: num = { 3'd4, 2'd0 };
        13: num = { 3'd4, 2'd1 };
        14: num = { 3'd4, 2'd2 };
        15: num = { 3'd5, 2'd0 };
        16: num = { 3'd5, 2'd1 };
        default: num = { 3'd0, 2'd0 };
    endcase

    sum3 = { 1'b0, a[1:0] } + { 1'b0, num[1:0] };
    if (sum3 > 2) begin
        add_offset.words = a.words + { 21'd0, num[4:2] } + 1;
        sum3 = sum3 - 3'd3;
        add_offset.sub = sum3[1:0];
    end else begin
        add_offset.words = a.words + { 21'd0, num[4:2] };
        add_offset.sub[1:0] = sum3[1:0];
    end
end
endfunction

function automatic arom_offset_t sub_offset(input arom_offset_t a, input [4:0] b);
begin
    bit [4:0] num;
    bit [2:0] diff3;
    case(b)
        0:  num = { 3'd0, 2'd0 };
        1:  num = { 3'd0, 2'd1 };
        2:  num = { 3'd0, 2'd2 };
        3:  num = { 3'd1, 2'd0 };
        4:  num = { 3'd1, 2'd1 };
        5:  num = { 3'd1, 2'd2 };
        6:  num = { 3'd2, 2'd0 };
        7:  num = { 3'd2, 2'd1 };
        8:  num = { 3'd2, 2'd2 };
        9:  num = { 3'd3, 2'd0 };
        10: num = { 3'd3, 2'd1 };
        11: num = { 3'd3, 2'd2 };
        12: num = { 3'd4, 2'd0 };
        13: num = { 3'd4, 2'd1 };
        14: num = { 3'd4, 2'd2 };
        15: num = { 3'd5, 2'd0 };
        16: num = { 3'd5, 2'd1 };
        default: num = { 3'd0, 2'd0 };
    endcase

    if ({ 1'b0, a[1:0] } >= { 1'b0, num[1:0] }) begin
        diff3 = { 1'b0, a[1:0] } - { 1'b0, num[1:0] };
        sub_offset.words = a.words - { 21'd0, num[4:2] };
        sub_offset.sub = diff3[1:0];
    end else begin
        diff3 = { 1'b0, a[1:0] } + 3'd3 - { 1'b0, num[1:0] };
        sub_offset.words = a.words - { 21'd0, num[4:2] } - 1'b1;
        sub_offset.sub = diff3[1:0];
    end
end
endfunction

function automatic arom_offset_t inc_offset(input arom_offset_t a, input [4:0] b);
begin
    inc_offset = spr_y_flip ? sub_offset(a, b) : add_offset(a, b);
end
endfunction


reg pixel0_wr, pixel1_wr;
reg pixel_prio;
reg [4:0] pixel_palette;
reg [10:0] pixel_column;
reg [10:0] pixel_next;
reg [7:0] draw_line;
arom_offset_t pixel0_offset, pixel1_offset;
wire buffer_ready;
reg draw_complete;

// tmp_* are temporary
// spr_* are immutable per sprite values
// spr.* are mutable per sprite values
always_ff @(posedge clk) begin
    reg [5:0] tmp_x;
    reg [15:0] tmp_shifter;
    reg [3:0] tmp_shift_count;
    reg [22:0] tmp_brom_addr;
    reg tmp_y_flip;
    reg [5:0] tmp_width;
    reg [8:0] tmp_height;
    reg [31:0] tmp_addr32;


    if (reset) begin
        cpu_br_n <= 1;
        cpu_bgack_n <= 1;
        dma_state <= DMA_IDLE;
        draw_complete <= 1;
    end else begin
        pixel0_wr <= 0;
        pixel1_wr <= 0;

        if (spr_x_flip ^ spr_y_flip) begin
            pixel_column <= (spr_x + { 1'b0, spr_width, 4'b0 }) - (pixel_next + 2);
        end else begin
            pixel_column <= spr_x + pixel_next;
        end

        if (spr_load) begin
            spr_x <= sprite_d0[sprite_index][10:0];
            spr_x_scale <= sprite_d0[sprite_index][15:11];
            spr_y <= sprite_d1[sprite_index][9:0];
            spr_y_scale <= sprite_d1[sprite_index][15:11];
            tmp_y_flip = sprite_d2[sprite_index][14];
            tmp_height = sprite_d4[sprite_index][8:0];
            tmp_width = sprite_d4[sprite_index][14:9];
            tmp_brom_addr = { sprite_d2[sprite_index][6:0], sprite_d3[sprite_index] };
            if (tmp_y_flip) begin
                spr_brom_addr <= tmp_brom_addr + ({17'b0, tmp_width} * {14'b0, tmp_height});
            end else begin
                spr_brom_addr <= tmp_brom_addr;
            end
            spr_prio <= sprite_d2[sprite_index][7];
            spr_palette <= sprite_d2[sprite_index][12:8];
            spr_x_flip <= sprite_d2[sprite_index][13];
            spr_y_flip <= tmp_y_flip;
            spr_height <= tmp_height;
            spr_width <= tmp_width;
            spr <= sprite_state[sprite_index];
        end else if (spr_store) begin
            sprite_state[sprite_index] <= spr;
        end
                
        case(dma_state)
            DMA_IDLE: begin
                draw_complete <= 1;
                // DMA was never triggered, skip the dma and go to prescan
                if (frame_reset) begin
                    sprite_index <= 0;
                    dma_state <= PRESCAN_LOAD;
                end

            end

            DMA_BUS_REQUEST: begin
                if (~cpu_bg_n & cpu_as_n & ~cpu_dtack_n) begin
                    cpu_bgack_n <= 0;
                    cpu_br_n <= 1;
                    dma_state <= DMA_READ0;
                    dma_addr <= 0;
                    sprite_index <= 0;
                    sprite_component_index <= 0;
                end
            end

            DMA_READ0: dma_state <= DMA_READ1;
            DMA_READ1: dma_state <= DMA_READ2;
            DMA_READ2: dma_state <= DMA_READ3;
            DMA_READ3: begin
                case(sprite_component_index)
                    0: sprite_d0[sprite_index] <= dma_din;
                    1: sprite_d1[sprite_index] <= dma_din;
                    2: sprite_d2[sprite_index] <= dma_din;
                    3: sprite_d3[sprite_index] <= dma_din;
                    4: sprite_d4[sprite_index] <= dma_din;
                    default: begin end
                endcase
                sprite_component_index <= sprite_component_index + 1;
                dma_state <= DMA_READ0;
                dma_addr <= dma_addr + 1;

                if (sprite_component_index == 4) begin
                    if (~|dma_din[14:0]) begin // early out
                        dma_state <= DMA_FINISH;
                    end else if (sprite_index == 255) begin
                        dma_state <= DMA_FINISH;
                    end else begin
                        sprite_component_index <= 0;
                        sprite_index <= sprite_index + 1;
                    end
                end
            end

            DMA_FINISH: begin
                cpu_bgack_n <= 1;
                sprite_count <= sprite_index;
                sprite_index <= 0;
                dma_state <= PRESCAN_LOAD;
            end

            PRESCAN_LOAD: begin
                if (sprite_index == sprite_count) begin
                    dma_state <= DRAW_INIT;
                end else begin
                    dma_state <= PRESCAN_INITIAL_BROM;
                end
            end

            PRESCAN_INITIAL_BROM: begin
                spr.brom_offset <= 0;
                brom_req <= ~brom_req;
                tmp_x <= 0;
                spr.line <= spr_y;
                dma_state <= PRESCAN_INITIAL_BROM_WAIT;
            end

            PRESCAN_INITIAL_BROM_WAIT: begin
                if (brom_req == brom_ack) begin
                    spr.brom_cache <= brom_data;
                    dma_state <= PRESCAN_SCAN_TO_START;
                    spr.brom_offset <= 2;
                    tmp_addr32 = spr_y_flip ? { brom_data[47:32], brom_data[63:48] } : { brom_data[31:0] };
                    spr.arom_offset.words <= tmp_addr32[25:2];
                    spr.arom_offset.sub <= tmp_addr32[1:0];
                    spr.active <= 1;
                end
            end

            PRESCAN_SCAN_TO_START: begin
                if (~spr.line[9]) begin // if y position is no long negative we are good
                    dma_state <= PRESCAN_NEXT;
                end else begin
                    spr.arom_offset <= inc_offset(spr.arom_offset, count_zeros16(spr_brom_data));
                    spr.brom_offset <= spr.brom_offset + 1;
                    tmp_x <= tmp_x + 1;
                    if (spr.brom_offset[1:0] == 2'b11) begin
                        brom_req <= ~brom_req;
                        dma_state <= PRESCAN_BROM_WAIT;
                    end

                    if ((tmp_x + 1) == spr_width) begin
                        if ((spr.line + 1) == spr_y_end) begin
                            spr.active <= 0;
                            dma_state <= PRESCAN_NEXT; // override BROM_WAIT state
                        end
                        spr.line <= spr.line + 1;
                        tmp_x <= 0;
                    end

                end
            end

            PRESCAN_BROM_WAIT: begin
                if (brom_req == brom_ack) begin
                    spr.brom_cache <= brom_data;
                    dma_state <= PRESCAN_SCAN_TO_START;
                end
            end


            PRESCAN_NEXT: begin
                sprite_index <= sprite_index + 1;
                dma_state <= PRESCAN_LOAD;
            end

            DRAW_INIT: begin
                sprite_index <= 0;
                draw_line <= 0;
                draw_complete <= 0;
                dma_state <= DRAW_SEARCH_ACTIVE_LOAD;
            end

            DRAW_SEARCH_ACTIVE_LOAD: begin
                if (sprite_index == sprite_count) begin
                    draw_line <= draw_line + 1;
                    sprite_index <= 0;
                    if (draw_line == 224) begin
                        dma_state <= DMA_IDLE;
                    end else begin
                        dma_state <= DRAW_SEARCH_ACTIVE_LOAD; // load correct sprite index
                    end
                end else begin
                    dma_state <= DRAW_SEARCH_ACTIVE_CHECK;
                end
            end

            DRAW_SEARCH_ACTIVE_CHECK: begin
                if (spr.active && spr.line == draw_line) begin
                    dma_state <= DRAW_ROW;
                    tmp_x <= 0;
                    pixel_next <= 0;
                end else begin
                    sprite_index <= sprite_index + 1;
                    dma_state <= DRAW_SEARCH_ACTIVE_LOAD;
                end
            end

            DRAW_ROW: begin
                tmp_shifter <= spr_y_flip ? reverse_bits16(spr_brom_data) : spr_brom_data;
                tmp_shift_count <= 0;
                dma_state <= DRAW_SPAN;
                if (tmp_x == spr_width) begin
                    spr.line <= spr.line + 1;
                    if ((spr.line + 1) == spr_y_end) begin
                        spr.active <= 0;
                    end
                    dma_state <= DRAW_ROW_END;
                end
            end

            DRAW_SPAN: begin
                if (buffer_ready) begin
                    pixel_prio <= spr_prio;
                    pixel_palette <= spr_palette;

                    case(tmp_shifter[1:0])
                        2'b00: begin
                            pixel0_offset <= spr.arom_offset;
                            pixel0_wr <= 1;
                            pixel1_offset <= inc_offset(spr.arom_offset, 1);
                            pixel1_wr <= 1;
                            spr.arom_offset <= inc_offset(spr.arom_offset, 2);
                        end
                        2'b01: begin
                            pixel0_offset <= spr.arom_offset;
                            pixel1_offset <= spr.arom_offset;
                            pixel1_wr <= 1;
                            pixel0_wr <= 0;
                            spr.arom_offset <= inc_offset(spr.arom_offset, 1);
                        end
                        2'b10: begin
                            pixel0_offset <= spr.arom_offset;
                            pixel1_offset <= spr.arom_offset;
                            pixel0_wr <= 1;
                            pixel1_wr <= 0;
                            spr.arom_offset <= inc_offset(spr.arom_offset, 1);
                        end
                        2'b11: begin
                            pixel0_wr <= 0;
                            pixel1_wr <= 0;
                        end
                    endcase

                    pixel_next <= pixel_next + 2;
                    tmp_shifter <= { 2'b0, tmp_shifter[15:2] };
                    tmp_shift_count <= tmp_shift_count + 2;
                    
                    if (tmp_shift_count == 14) begin
                        spr.brom_offset <= spr.brom_offset + 1;
                        tmp_x <= tmp_x + 1;
                        if (spr.brom_offset[1:0] == 2'b11) begin
                            brom_req <= ~brom_req;
                            dma_state <= DRAW_BROM_WAIT;
                        end else begin
                            dma_state <= DRAW_ROW;
                        end
                    end
                end
            end

            DRAW_BROM_WAIT: begin
                if (brom_req == brom_ack) begin
                    spr.brom_cache <= brom_data;
                    dma_state <= DRAW_ROW;
                end
            end

            DRAW_ROW_END: begin
                sprite_index <= sprite_index + 1;
                dma_state <= DRAW_SEARCH_ACTIVE_LOAD;
            end


            default: dma_state <= DMA_IDLE;

        endcase

        if (dma_start) begin
            dma_state <= DMA_BUS_REQUEST;
            draw_complete <= 1;
            cpu_br_n <= 0;
        end

    end
end

IGS023_Buffer line_buffer(
    .clk,
    .ce_pixel,
    .scan_active,
    .frame_reset,
    .next_line,
    .draw_complete,

    .scan_color(color_out),

    .wr0(spr_x_flip ^ spr_y_flip ? pixel1_wr : pixel0_wr),
    .wr1(spr_x_flip ^ spr_y_flip ? pixel0_wr : pixel1_wr),
    .column(pixel_column[8:0]),
    .prio(pixel_prio),
    .palette(pixel_palette),
    .arom_offset0(spr_x_flip ^ spr_y_flip ? pixel1_offset : pixel0_offset),
    .arom_offset1(spr_x_flip ^ spr_y_flip ? pixel0_offset : pixel1_offset),
    .line(draw_line),
    .ready(buffer_ready),

    .ddr(ddr)
);


endmodule
