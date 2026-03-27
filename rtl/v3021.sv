// V3021 / EM3021 RTC module
// TODO
//   Leap year and month corrections
//   Debug modes
//   Lock modes
//   Setting from mister
//   Save state

module bcd_counter #(
    parameter logic [7:0] MIN_VAL = 8'h00
)(
    input  logic       clk,
    input  logic       reset,
    input  logic       increment,
    input  logic [7:0] max_val,
    input  logic [7:0] reset_val,

    output logic [7:0] count,
    output logic       overflow
);

    logic [7:0] next_count;
    logic       next_overflow;

    always_comb begin
        next_count    = count;
        next_overflow = 1'b0;

        if (MIN_VAL != 0 && count < MIN_VAL) begin
            next_count = MIN_VAL;
        end else if (count > max_val) begin
            next_count = max_val;
        end else if (increment) begin
            if (count == max_val) begin
                next_count    = MIN_VAL;
                next_overflow = 1'b1;
            end else begin
                if (count[3:0] == 4'd9) begin
                    next_count = {(count[7:4] + 1'b1), 4'd0};
                end else begin
                    next_count = {count[7:4], (count[3:0] + 1'b1)};
                end
            end
        end
    end

    always_ff @(posedge clk) begin
        if (reset) begin
            count    <= reset_val;
            overflow <= 1'b0;
        end else begin
            count    <= next_count;
            overflow <= next_overflow;
        end
    end
endmodule


module V3021(
    input clk,
    input ce_32khz,
    
    input cs_n,
    input wr_n,

    input in,
    output reg out
);

reg [7:0] ram[16];
reg copy_to_clock = 0;
reg [3:0] address;
reg [7:0] data;
reg [3:0] state;
reg prev_cs_n;

wire [3:0] addr_shift = { in, address[3:1] };
wire [7:0] data_shift = { in, data[7:1] };


logic [7:0] last_day;
wire tick_second, tick_minute, tick_hour, tick_day, tick_month, tick_year, tick_week;
wire [7:0] second, minute, hour, day, month, year, weekday, week;

bcd_counter seconds_cnt( .clk, .reset(copy_to_clock), .reset_val(ram[2]), .max_val(8'h59),    .increment(tick_second), .overflow(tick_minute), .count(second) );
bcd_counter minutes_cnt( .clk, .reset(copy_to_clock), .reset_val(ram[3]), .max_val(8'h59),    .increment(tick_minute), .overflow(tick_hour),   .count(minute) );
bcd_counter hours_cnt(   .clk, .reset(copy_to_clock), .reset_val(ram[4]), .max_val(8'h23),    .increment(tick_hour),   .overflow(tick_day),    .count(hour) );
bcd_counter days_cnt(    .clk, .reset(copy_to_clock), .reset_val(ram[5]), .max_val(last_day), .increment(tick_day),    .overflow(tick_month),  .count(day) );
bcd_counter #(.MIN_VAL(8'h01))
            months_cnt(  .clk, .reset(copy_to_clock), .reset_val(ram[6]), .max_val(8'h12),    .increment(tick_month),  .overflow(tick_year),   .count(month) );
bcd_counter years_cnt(   .clk, .reset(copy_to_clock), .reset_val(ram[7]), .max_val(8'h99),    .increment(tick_year),   .overflow(),            .count(year) );
bcd_counter #(.MIN_VAL(8'h01))
            weekday_cnt( .clk, .reset(copy_to_clock), .reset_val(ram[8]), .max_val(8'h07),    .increment(tick_day),    .overflow(tick_week),   .count(weekday) );
bcd_counter week_cnt(    .clk, .reset(copy_to_clock), .reset_val(ram[9]), .max_val(8'h52),    .increment(tick_week),   .overflow(),            .count(week) );

wire is_leap = (~year[4] & (year[3:0] == 4'h0 || year[3:0] == 4'h4 || year[3:0] == 4'h8)) |
                (year[4] & (year[3:0] == 4'h2 || year[3:0] == 4'h6));

always_comb begin
    case (month)
        8'h02: last_day = is_leap ? 8'h29 : 8'h28;
        8'h04, 8'h06, 8'h09, 8'h11: last_day = 8'h30;
        default: last_day = 8'h31;
    endcase
end

assign tick_second = ce_32khz && cnt == 0;

reg [15:0] cnt = 0;
always_ff @(posedge clk) begin
    if (ce_32khz) begin
        cnt <= cnt + 1;
    end
end

always_ff @(posedge clk) begin
    prev_cs_n <= cs_n;
    copy_to_clock <= 0;
    if (prev_cs_n & ~cs_n) begin
        state <= state + 1;
        if (state == 11) state <= 0;
        case(state)
            0, 1, 2, 3: begin
                if (~wr_n) begin
                    address <= addr_shift;
                    if (state == 3) begin
                        if (addr_shift == 4'he) begin
                            state <= 0;
                            copy_to_clock <= 1;
                        end else if( addr_shift == 4'hf) begin
                            ram[0] <= {
                                week    == ram[9] ? 1'b0 : 1'b1,
                                weekday == ram[8] ? 1'b0 : 1'b1,
                                year    == ram[7] ? 1'b0 : 1'b1,
                                month   == ram[6] ? 1'b0 : 1'b1,
                                day     == ram[5] ? 1'b0 : 1'b1,
                                hour    == ram[4] ? 1'b0 : 1'b1,
                                minute  == ram[3] ? 1'b0 : 1'b1,
                                second  == ram[2] ? 1'b0 : 1'b1
                            };
                            ram[2] <= second;
                            ram[3] <= minute;
                            ram[4] <= hour;
                            ram[5] <= day;
                            ram[6] <= month;
                            ram[7] <= year;
                            ram[8] <= weekday;
                            ram[9] <= week;

                            state <= 0;
                        end else begin
                            data <= ram[addr_shift];
                        end
                    end
                end else begin
                    state <= 0;
                end
            end
            default: begin
                out <= data[0];
                data <= data_shift;
                if (~wr_n) begin
                    if (state == 11) begin
                        ram[address] <= data_shift;
                    end
                end
            end
        endcase
    end
end


endmodule
