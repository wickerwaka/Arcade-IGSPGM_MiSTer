import system_consts::*;

module rom_cache(
    input clk,
    input reset,

    output reg [26:0]   sdr_addr,
    input [63:0]        sdr_data,
    output reg          sdr_req,
    input               sdr_ack,

    input               extra_rom_n,

    input               as_n,
    output              dtack_n,
    input  [22:0]       cpu_addr,
    output logic [15:0] data
);

localparam CACHE_WIDTH = 8;

wire [22-CACHE_WIDTH:0] tag = { version, cpu_addr[22:CACHE_WIDTH+2] };
wire [CACHE_WIDTH-1:0] index = cpu_addr[CACHE_WIDTH+1:2];

reg [1:0] version;
reg [63:0] cache_data[2**CACHE_WIDTH];
reg [22-CACHE_WIDTH:0] cache_tag[2**CACHE_WIDTH];

reg [63:0] cache_line;
reg [22-CACHE_WIDTH:0] cached_tag;

always_comb begin
    unique case(cpu_addr[1:0])
    2'b00: data = cache_line[15:0];
    2'b01: data = cache_line[31:16];
    2'b10: data = cache_line[47:32];
    2'b11: data = cache_line[63:48];
    endcase
end

enum { IDLE, CACHE_CHECK, SDR_WAIT, READY } state = IDLE;

assign dtack_n = ~( state == IDLE || state == READY );

reg prev_reset;

always_ff @(posedge clk) begin
    cache_line <= cache_data[index];
    cached_tag <= cache_tag[index];

    prev_reset <= reset;

    if (reset) begin
        state <= IDLE;
        if (~prev_reset) version <= version + 2'd1;
    end else begin
        if (~as_n && state == IDLE) begin
            state <= CACHE_CHECK;
        end else if (state == CACHE_CHECK) begin
            if (cached_tag == tag) begin
                state <= READY;
            end else begin
                if (~extra_rom_n) begin
                    sdr_addr <= CPU_ROM_SDR_BASE[26:0] + { 3'b0, cpu_addr[22:2], 3'b000 };
                end else begin
                    sdr_addr <= CPU_ROM_SDR_BASE[26:0] + { 3'b0, cpu_addr[22:2], 3'b000 };
                end
                sdr_req <= ~sdr_req;
                state <= SDR_WAIT;
            end
        end else if (state == SDR_WAIT) begin
            if (sdr_req == sdr_ack) begin
                cache_tag[index] <= tag;
                cache_data[index] <= sdr_data;
                cache_line <= sdr_data;
                state <= READY;
            end
        end else if (as_n && state == READY) begin
            state <= IDLE;
        end
    end
end

endmodule

module rom_cache2(
    input clk,

    output reg [26:0]   sdr_addr,
    input [63:0]        sdr_data,
    output reg          sdr_req,
    input               sdr_ack,

    input  [26:0]       addr,
    input               read,
    output logic [15:0] data,
    output              data_valid
);

localparam CACHE_WIDTH = 10;

wire [26-CACHE_WIDTH-3:0] tag = addr[26:CACHE_WIDTH+3];
wire [CACHE_WIDTH-1:0] index = addr[CACHE_WIDTH+2:3];

reg [63:0] cache_data[2**CACHE_WIDTH];
reg [26-CACHE_WIDTH-3:0] cache_tag[2**CACHE_WIDTH];

reg [63:0] cache_line;
reg [26-CACHE_WIDTH-3:0] cached_tag;
reg [CACHE_WIDTH-1:0] cached_index;

always_comb begin
    unique case(addr[2:1])
    2'b00: data = cache_line[15:0];
    2'b01: data = cache_line[31:16];
    2'b10: data = cache_line[47:32];
    2'b11: data = cache_line[63:48];
    endcase
end

enum { IDLE, CACHE_CHECK, SDR_WAIT } state = IDLE;

reg [26:0] addr2;
assign data_valid = (index == cached_index) && (tag == cached_tag);

always_ff @(posedge clk) begin
    cache_line   <= cache_data[index];
    cached_tag   <= cache_tag[index];
    cached_index <= index;

    if (read && state == IDLE) begin
        state <= CACHE_CHECK;
    end else if (state == CACHE_CHECK) begin
        if ((cached_index == index) && (cached_tag == tag)) begin
            state <= IDLE;
        end else begin
            sdr_addr <= { addr[26:3], 3'b000 };
            addr2 <= addr;
            sdr_req <= ~sdr_req;
            state <= SDR_WAIT;
        end
    end else if (state == SDR_WAIT) begin
        if (sdr_req == sdr_ack) begin
            cache_tag[addr2[CACHE_WIDTH+2:3]] <= addr2[26:CACHE_WIDTH+3];
            cache_data[addr2[CACHE_WIDTH+2:3]] <= sdr_data;
            cache_line <= sdr_data;
            cached_tag <= addr2[26:CACHE_WIDTH+3];
            cached_index <= addr2[CACHE_WIDTH+2:3];
            state <= IDLE;
        end
    end
end

endmodule
