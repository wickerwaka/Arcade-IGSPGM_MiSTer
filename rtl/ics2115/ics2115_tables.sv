// ICS2115 Lookup Tables
// Volume (4096×16), Pan Law (256×12), µ-Law Decode (256×16 signed)
//
// Volume table: registered (1-cycle latency) for timing closure
// Pan and µ-law: combinational

module ics2115_tables (
    input  logic        clk,

    // Volume table — 4096 entries, 16-bit unsigned
    // Input:  12-bit index from (vol.acc >> 14) & 0xFFF
    // Output: 15-bit linear amplitude (registered, 1-cycle latency)
    input  logic [11:0] vol_addr,
    output logic [15:0] vol_data,

    // Pan law — 256 entries, 12-bit
    // Input:  8-bit pan index
    // Output: attenuation value subtracted from volume index
    input  logic [7:0]  pan_addr,
    output logic [11:0] pan_data,

    // µ-Law decode — 256 entries, signed 16-bit
    // Input:  8-bit µ-law encoded byte
    // Output: signed 16-bit linear PCM
    input  logic [7:0]  ulaw_addr,
    output logic signed [15:0] ulaw_data
);

    // =========================================================================
    // Volume table (decision D008: patent-derived integer formula)
    // vol[i] = ((0x100 | (i & 0xFF)) << (VOLUME_BITS - 9)) >> (15 - (i >> 8))
    // VOLUME_BITS = 15: left shift by 6, right shift by (15 - exponent)
    // =========================================================================
    logic [15:0] vol_mem [0:4095];

    initial begin
        for (int i = 0; i < 4096; i++) begin
            vol_mem[i] = ((16'h100 | i[7:0]) << 6) >> (15 - i[11:8]);
        end
    end

    // Registered output — 1 cycle latency from address input
    always_ff @(posedge clk) begin
        vol_data <= vol_mem[vol_addr];
    end

    // =========================================================================
    // Pan law table
    // panlaw[0] = 0xFFF (full attenuation — special case)
    // panlaw[i] = 16 - floor(log2(i)) via priority encoder
    // =========================================================================
    always_comb begin
        if (pan_addr == 8'd0) begin
            pan_data = 12'hFFF;
        end else begin
            casez (pan_addr)
                8'b1???_????: pan_data = 12'd9;    // log2 = 7, 16 - 7 = 9
                8'b01??_????: pan_data = 12'd10;   // log2 = 6
                8'b001?_????: pan_data = 12'd11;   // log2 = 5
                8'b0001_????: pan_data = 12'd12;   // log2 = 4
                8'b0000_1???: pan_data = 12'd13;   // log2 = 3
                8'b0000_01??: pan_data = 12'd14;   // log2 = 2
                8'b0000_001?: pan_data = 12'd15;   // log2 = 1
                8'b0000_0001: pan_data = 12'd16;   // log2 = 0
                default:      pan_data = 12'd0;
            endcase
        end
    end

    // =========================================================================
    // µ-Law decode table (MIL-STD-188-113 / ITU-T G.711)
    // All bits inverted per standard.
    //   exp  = (~i >> 4) & 7
    //   mant = ~i & 0xF
    //   base = (132 << exp) - 132
    //   value = base + (mant << (exp + 3))
    //   sign: bit 7 of original byte — 1 = negative (per MAME)
    // =========================================================================
    always_comb begin
        logic [2:0]  ulaw_exp;
        logic [3:0]  ulaw_mant;
        logic [15:0] lut_base;
        logic [15:0] ulaw_value;

        ulaw_exp  = (~ulaw_addr >> 4) & 3'd7;
        ulaw_mant = ~ulaw_addr & 4'hF;

        // Precomputed segment base values: (132 << exp) - 132
        case (ulaw_exp)
            3'd0: lut_base = 16'd0;
            3'd1: lut_base = 16'd132;
            3'd2: lut_base = 16'd396;
            3'd3: lut_base = 16'd924;
            3'd4: lut_base = 16'd1980;
            3'd5: lut_base = 16'd4092;
            3'd6: lut_base = 16'd8316;
            3'd7: lut_base = 16'd16764;
        endcase

        // value = base + (mant << (exp + 3))
        case (ulaw_exp)
            3'd0: ulaw_value = lut_base + ({12'd0, ulaw_mant} << 3);
            3'd1: ulaw_value = lut_base + ({12'd0, ulaw_mant} << 4);
            3'd2: ulaw_value = lut_base + ({12'd0, ulaw_mant} << 5);
            3'd3: ulaw_value = lut_base + ({12'd0, ulaw_mant} << 6);
            3'd4: ulaw_value = lut_base + ({12'd0, ulaw_mant} << 7);
            3'd5: ulaw_value = lut_base + ({12'd0, ulaw_mant} << 8);
            3'd6: ulaw_value = lut_base + ({12'd0, ulaw_mant} << 9);
            3'd7: ulaw_value = lut_base + ({12'd0, ulaw_mant} << 10);
        endcase

        // Sign bit: bit 7 set = negative output (matches MAME)
        if (ulaw_addr[7])
            ulaw_data = -$signed({1'b0, ulaw_value[14:0]});
        else
            ulaw_data = $signed({1'b0, ulaw_value[14:0]});
    end

endmodule
