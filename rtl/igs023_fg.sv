module IGS023_FG(
    input clk,
    input ce_33m,
    input ce_pixel,

    input start_read,
    input scan_active,

    input [8:0] x,
    input [7:0] y,

    output [8:0] color_out,

    // VRAM interface
    output logic [14:0] vram_addr,
    input        [7:0]  vram_din,
    output              fpga_vram_master, // on FPGA this is when we really own the bus, we give it up early for the BG to access
    output              real_vram_master, // on real hardware this is when the FG would own the VRAM bus
    
    // ROM interface
    output            rom_master,
    output reg [23:0] rom_address,
    input      [31:0] rom_data,
    output reg        rom_req,
    input             rom_ack,
    input             secondary_rom_req
);

// Reads 57 FG tiles during hblank and fills a line buffer with data
// 33mhz clock
// starts 180ns before hblank
// Reads each tile twice it seems, 8 reads * 57 = 456
// We are going to read from ram as fast as we can, stall for SDRam reads and count the cycles

typedef enum logic[2:0] {
    READ0, READ1, READ2, READ3, ROM_REQ, ROM_WAIT, DONE
} read_state_t;

read_state_t read_state = READ0;

reg reading_vram;
assign real_vram_master = reading_vram;
assign fpga_vram_master = reading_vram && (read_state != DONE);
assign rom_master = reading_vram && (read_state != DONE);

reg [6:0] vram_row_addr;
reg [7:0] tile_addr;

reg [15:0] tile_code;
reg [7:0] tile_attrib;

reg flip_x;
reg [4:0] palette;

assign vram_addr = { vram_row_addr, tile_addr };

reg [31:0] color_buffer[64];
reg [4:0]  palette_buffer[64];

reg [5:0] buffer_idx;
reg [2:0] pixel_idx;

assign color_out = {palette_buffer[buffer_idx], color_buffer[buffer_idx][4*pixel_idx +: 4]};

reg [8:0] read_counter;
reg first_read;
always_ff @(posedge clk) begin
    if (start_read) begin
        reading_vram <= 1;
        read_state <= READ0;
        vram_row_addr <= { 2'b10, y[7:3] };
        tile_addr <= {x[8:3], 2'b00};
        read_counter <= 0;
        buffer_idx <= 0;
        pixel_idx <= x[2:0];
        rom_req <= secondary_rom_req;
        first_read <= 1;
    end else if (reading_vram) begin
        if (ce_33m) begin
            read_counter <= read_counter + 1;
            if (read_counter == (((8 * 58) + 0) - 1)) begin
                reading_vram <= 0;
                buffer_idx <= 0;
            end
        end

        case(read_state)
            READ0: begin
                tile_addr <= tile_addr + 1;
                read_state <= READ1;
            end
            READ1: begin
                tile_code[7:0] <= vram_din;
                tile_addr <= tile_addr + 1;
                read_state <= READ2;
            end
            READ2: begin
                tile_code[15:8] <= vram_din;
                tile_addr <= tile_addr + 1;
                read_state <= READ3;
            end
            READ3: begin
                tile_attrib[7:0] <= vram_din;
                tile_addr <= tile_addr + 1;
                first_read <= 0;
                read_state <= first_read ? ROM_REQ : ROM_WAIT;
            end
            ROM_WAIT: begin
                if (rom_req == rom_ack) begin
                    palette_buffer[buffer_idx] <= palette;
                    color_buffer[buffer_idx] <= flip_x
                        ? { rom_data[3:0], rom_data[7:4], rom_data[11:8], rom_data[15:12], rom_data[19:16], rom_data[23:20], rom_data[27:24], rom_data[31:28] }
                        : rom_data;

                    if (buffer_idx == 56) begin
                        read_state <= DONE;
                    end else begin
                        read_state <= ROM_REQ;
                    end
                    buffer_idx <= buffer_idx + 1;
                end
            end
            ROM_REQ: begin
                if (rom_req == rom_ack) begin // ensure bg rom request has finished
                    rom_address <= { 3'd0, tile_code, tile_attrib[7] ? ~y[2:0] : y[2:0], 2'd0 };
                    rom_req <= ~rom_req;
                    read_state <= READ0;
                    flip_x <= tile_attrib[6];
                    palette <= tile_attrib[5:1];
                end
            end
            DONE: begin rom_req <= secondary_rom_req; end
            default: read_state <= DONE;
        endcase
    end else if (ce_pixel & scan_active) begin
        pixel_idx <= pixel_idx + 1;
        if (&pixel_idx) buffer_idx <= buffer_idx + 1;
    end
end

endmodule
