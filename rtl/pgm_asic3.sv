// SPDX-License-Identifier: BSD-3-Clause
//
// PGM ASIC3 protection device used by Oriental Legend.
// Converted from MAME src/mame/igs/pgmprot_orlegend.cpp
// copyright-holders: Olivier Galibert, iq_132

module pgm_asic3 #(
    parameter int SS_IDX = -1
)(
    input  logic        clk,
    input  logic        reset,

    // MAME "Region" port value:
    //   0,1 = World variants
    //   2   = Korea
    //   3   = China
    //   4   = Taiwan / orlegend111t protection sequence
    input  logic [2:0]  region,

    // 68000-style 16-bit CPU interface for the $c04000-$c0400f window.
    // cpu_addr is the low byte-address nibble.  Word offset 0 selects the
    // command register; any other word offset is a data access, matching
    // pgm_asic3_w(offset == 0 ? command : data) in MAME.
    input  logic [3:0]  cpu_addr,
    input  logic [15:0] cpu_din,
    output logic [15:0] cpu_dout,
    input  logic        cpu_uds_n,
    input  logic        cpu_lds_n,
    input  logic        cpu_rw,      // 1 = read, 0 = write
    input  logic        cpu_cs_n,

    ssbus_if.slave ssbus
);

    logic [7:0]  asic3_reg;
    logic [7:0]  asic3_latch [0:2];
    logic [7:0]  asic3_x;
    logic [15:0] asic3_hilo;
    logic [15:0] asic3_hold;

    logic write_active_d;

    localparam int SS_WORD_COUNT = 3;
    localparam int SS_WORD_REGS = 0;
    localparam int SS_WORD_HILO = 1;
    localparam int SS_WORD_HOLD = 2;

    wire ss_access = ssbus.access(SS_IDX);

    wire access_active = !cpu_cs_n && (!cpu_uds_n || !cpu_lds_n);
    wire write_active  = access_active && !cpu_rw;
    wire write_pulse   = write_active && !write_active_d;
    wire command_write = cpu_addr[3:1] == 3'd0;

    function automatic logic bit_at16(input logic [15:0] value, input int index);
        if (index >= 0 && index < 16)
            bit_at16 = value[index];
        else
            bit_at16 = 1'b0;
    endfunction

    function automatic logic [15:0] compute_hold(
        input logic [15:0] old_hold,
        input logic [7:0]  x,
        input logic [2:0]  region_value,
        input logic [2:0]  y,
        input logic [15:0] z
    );
        logic [15:0] next_hold;

        next_hold = {old_hold[14:0], old_hold[15]};
        next_hold = next_hold ^ 16'h2bad;
        next_hold = next_hold ^ {15'd0, bit_at16(z, y)};
        next_hold = next_hold ^ (16'(x[2]) << 10);
        next_hold = next_hold ^ {15'd0, old_hold[5]};

        unique case (region_value)
            3'd0,
            3'd1: begin
                next_hold = next_hold ^ {15'd0, old_hold[10]};
                next_hold = next_hold ^ {15'd0, old_hold[8]};
                next_hold = next_hold ^ (16'(x[0]) << 1);
                next_hold = next_hold ^ (16'(x[1]) << 6);
                next_hold = next_hold ^ (16'(x[3]) << 14);
            end

            3'd2: begin
                next_hold = next_hold ^ {15'd0, old_hold[10]};
                next_hold = next_hold ^ {15'd0, old_hold[8]};
                next_hold = next_hold ^ (16'(x[0]) << 4);
                next_hold = next_hold ^ (16'(x[1]) << 6);
                next_hold = next_hold ^ (16'(x[3]) << 12);
            end

            3'd3: begin
                next_hold = next_hold ^ {15'd0, old_hold[7]};
                next_hold = next_hold ^ {15'd0, old_hold[6]};
                next_hold = next_hold ^ (16'(x[0]) << 4);
                next_hold = next_hold ^ (16'(x[1]) << 6);
                next_hold = next_hold ^ (16'(x[3]) << 12);
            end

            3'd4: begin
                next_hold = next_hold ^ {15'd0, old_hold[7]};
                next_hold = next_hold ^ {15'd0, old_hold[6]};
                next_hold = next_hold ^ (16'(x[0]) << 3);
                next_hold = next_hold ^ (16'(x[1]) << 8);
                next_hold = next_hold ^ (16'(x[3]) << 14);
            end

            default: begin
            end
        endcase

        return next_hold;
    endfunction

    function automatic logic [7:0] bitswap_hold(input logic [15:0] hold);
        bitswap_hold = {
            hold[5],
            hold[2],
            hold[9],
            hold[7],
            hold[10],
            hold[13],
            hold[12],
            hold[15]
        };
    endfunction

    function automatic logic [7:0] read_data(
        input logic [7:0]  reg_value,
        input logic [7:0]  latch0,
        input logic [7:0]  latch1,
        input logic [7:0]  latch2,
        input logic [15:0] hold,
        input logic [2:0]  region_value
    );
        unique case (reg_value)
            8'h00: read_data = (latch0 & 8'hf7) | (({5'd0, region_value} << 3) & 8'h08);
            8'h01: read_data = latch1;
            8'h02: read_data = (latch2 & 8'h7f) | (({5'd0, region_value} << 6) & 8'h80);
            8'h03: read_data = bitswap_hold(hold);

            // Signature / self-test constants from MAME.
            8'h20: read_data = 8'h49; // "IGS"
            8'h21: read_data = 8'h47;
            8'h22: read_data = 8'h53;

            8'h24: read_data = 8'h41;
            8'h25: read_data = 8'h41;
            8'h26: read_data = 8'h7f;
            8'h27: read_data = 8'h41;
            8'h28: read_data = 8'h41;

            8'h2a: read_data = 8'h3e;
            8'h2b: read_data = 8'h41;
            8'h2c: read_data = 8'h49;
            8'h2d: read_data = 8'hf9;
            8'h2e: read_data = 8'h0a;

            8'h30: read_data = 8'h26;
            8'h31: read_data = 8'h49;
            8'h32: read_data = 8'h49;
            8'h33: read_data = 8'h49;
            8'h34: read_data = 8'h32;

            default: read_data = 8'h00;
        endcase
    endfunction

    always_comb begin
        cpu_dout = {8'h00, read_data(asic3_reg,
                                      asic3_latch[0],
                                      asic3_latch[1],
                                      asic3_latch[2],
                                      asic3_hold,
                                      region)};
    end

    always_ff @(posedge clk) begin
        if (reset) begin
            asic3_reg <= 8'd0;
            asic3_latch[0] <= 8'd0;
            asic3_latch[1] <= 8'd0;
            asic3_latch[2] <= 8'd0;
            asic3_x <= 8'd0;
            asic3_hilo <= 16'd0;
            asic3_hold <= 16'd0;
            write_active_d <= 1'b0;
        end else begin
            write_active_d <= write_active;
            ssbus.setup(SS_IDX, SS_WORD_COUNT[31:0], 2);

            if (ss_access) begin
                if (ssbus.read) begin
                    unique case (ssbus.addr)
                        SS_WORD_REGS: ssbus.read_response(SS_IDX, {32'd0, asic3_latch[2], asic3_latch[1], asic3_latch[0], asic3_reg});
                        SS_WORD_HILO: ssbus.read_response(SS_IDX, {32'd0, 8'd0, asic3_x, asic3_hilo});
                        SS_WORD_HOLD: ssbus.read_response(SS_IDX, {32'd0, 16'd0, asic3_hold});
                        default: ssbus.read_response(SS_IDX, 64'd0);
                    endcase
                end else if (ssbus.write) begin
                    unique case (ssbus.addr)
                        SS_WORD_REGS: begin
                            asic3_reg <= ssbus.data[7:0];
                            asic3_latch[0] <= ssbus.data[15:8];
                            asic3_latch[1] <= ssbus.data[23:16];
                            asic3_latch[2] <= ssbus.data[31:24];
                        end
                        SS_WORD_HILO: begin
                            asic3_hilo <= ssbus.data[15:0];
                            asic3_x <= ssbus.data[23:16];
                        end
                        SS_WORD_HOLD: begin
                            asic3_hold <= ssbus.data[15:0];
                        end
                        default: begin
                        end
                    endcase
                    ssbus.write_ack(SS_IDX);
                end
            end else if (write_pulse) begin
                if (command_write) begin
                    asic3_reg <= cpu_din[7:0];
                end else begin
                    unique case (asic3_reg)
                        8'h00,
                        8'h01,
                        8'h02: begin
                            asic3_latch[asic3_reg[1:0]] <= {cpu_din[6:0], 1'b0};
                        end

                        // 0x03, 0x04, 0x05 are intentionally ignored by MAME.

                        8'h40: begin
                            asic3_hilo <= {asic3_hilo[7:0], 8'h00} | cpu_din;
                        end

                        // 0x41-0x47 are intentionally ignored by MAME.
                        8'h41, 8'h42, 8'h43, 8'h44,
                        8'h45, 8'h46, 8'h47: begin
                        end

                        8'h48: begin
                            asic3_x <= {
                                4'd0,
                                ((asic3_hilo & 16'h0a00) == 16'h0000),
                                ((asic3_hilo & 16'h9000) == 16'h0000),
                                ((asic3_hilo & 16'h0006) == 16'h0000),
                                ((asic3_hilo & 16'h0090) == 16'h0000)
                            };
                        end

                        // 0x50 is intentionally ignored by MAME.

                        8'h80, 8'h81, 8'h82, 8'h83,
                        8'h84, 8'h85, 8'h86, 8'h87: begin
                            asic3_hold <= compute_hold(asic3_hold,
                                                       asic3_x,
                                                       region,
                                                       asic3_reg[2:0],
                                                       cpu_din);
                        end

                        8'ha0: begin
                            asic3_hold <= 16'd0;
                        end

                        default: begin
                        end
                    endcase
                end
            end
        end
    end

endmodule
