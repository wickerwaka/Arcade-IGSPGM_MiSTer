// ICS2115 Voice Oscillator — Sequential FSM Processing Pipeline
// Processes one voice per invocation: ROM fetch → interpolation → volume/pan → mix
//
// Processing order per spec §8.4 and MAME fill_output():
//   1. Volume/pan lookup (using CURRENT vol.acc)
//   2. Sample fetch and interpolation (at CURRENT osc.acc position)
//   3. Scale sample by volume, accumulate into stereo bus
//   4. Oscillator accumulator update — happens AFTER sample fetch
//   5. Oscillator boundary check (loop/stop)

module ics2115_osc
    import ics2115_pkg::*;
(
    input  logic        clk,
    input  logic        ce,
    input  logic        reset_n,
    input  logic        clear,

    // Control interface
    input  logic        start,          // pulse to begin processing
    output logic        done,           // asserted when processing complete
    output logic        irq_osc,        // oscillator boundary IRQ fired
    output logic        irq_vol,        // volume boundary IRQ fired

    // Voice state — read at start, written back at done
    input  voice_state_t voice_in,
    output voice_state_t voice_out,

    // ROM interface — top-level translates byte addr to word addr
    output logic [23:0] rom_byte_addr,  // 24-bit byte address
    output logic        rom_rd,         // read strobe
    input  logic [15:0] rom_data,       // 16-bit word from ROM
    input  logic        rom_data_valid, // handshake: data is valid this cycle

    // Table interfaces — directly wired to ics2115_tables
    output logic [11:0] vol_tbl_addr,
    input  logic [15:0] vol_tbl_data,
    output logic [7:0]  pan_tbl_addr,
    input  logic [11:0] pan_tbl_data,

    // Audio accumulation — signed accumulators, caller sums across voices
    output logic signed [23:0] audio_left,
    output logic signed [23:0] audio_right,
    output logic               audio_valid     // pulse when this voice's contribution is ready
);

    // =========================================================================
    // FSM state encoding
    // =========================================================================
    typedef enum logic [4:0] {
        ST_IDLE             = 5'd0,
        ST_VOL_LOOKUP       = 5'd1,
        ST_PAN_LOOKUP_L     = 5'd2,
        ST_PAN_LOOKUP_R     = 5'd3,
        ST_VOL_WAIT_L       = 5'd4,     // wait for left vol table result
        ST_SAMPLE_FETCH_1   = 5'd5,
        ST_VOL_WAIT_R       = 5'd6,     // wait for right vol table result
        ST_SAMPLE_FETCH_2   = 5'd7,
        ST_SAMPLE_WAIT      = 5'd8,
        ST_INTERPOLATE      = 5'd9,
        ST_MIX              = 5'd10,
        ST_OSC_UPDATE       = 5'd11,
        ST_VOL_ENV_UPDATE   = 5'd14,    // volume envelope accumulator update
        ST_DONE             = 5'd16
    } osc_state_t;

    osc_state_t state, state_next;

    // =========================================================================
    // Internal registers
    // =========================================================================
    voice_state_t v;                    // working copy of voice state

    logic [11:0] volacc;                // (vol.acc >> 14) & 0xFFF
    logic signed [12:0] vlefti_s;       // signed left vol index
    logic signed [12:0] vrighti_s;      // signed right vol index
    logic [15:0] vleft;                 // left volume
    logic [15:0] vright;                // right volume

    logic signed [15:0] sample1;
    logic signed [15:0] sample2;
    logic [19:0] cur_addr;              // acc >> 12, 20-bit byte addr in bank
    logic [19:0] next_addr;             // next sample addr for interpolation
    logic signed [15:0] interp_sample;

    logic irq_osc_r, irq_vol_r;

    // Volume envelope step derived from per-voice VMode/VIncr.
    logic [25:0] vol_step;

    logic osc_conf_8bit;
    logic osc_conf_ulaw;
    logic osc_conf_8bit_linear;
    logic osc_conf_16bit;

    always_comb begin
        osc_conf_8bit = 0;
        osc_conf_ulaw = 0;
        osc_conf_8bit_linear = 0;
        osc_conf_16bit = 0;

        if (~v.osc_conf[OSC_16BIT]) begin
            osc_conf_8bit = 1;
            if (v.osc_conf[OSC_ULAW])
                osc_conf_ulaw = 1;
            else
                osc_conf_8bit_linear = 1;
        end else begin
            osc_conf_16bit = 1;
        end
    end

    function automatic logic [25:0] calc_vol_step(
        input logic [1:0] mode,
        input logic [7:0] incr
    );
        logic [5:0] mant;
        begin
            mant = {1'b1, incr[4:0]};
            calc_vol_step = 26'd0;

            case (mode)
                // Very slow mode.  Long hardware captures show no movement for
                // incr <= 223 and period ~= 4177920 / (incr - 192) for 224..255.
                2'b00: begin
                    if (incr[7:5] == 3'b111)
                        calc_vol_step = {16'd0, mant, 4'd0};       // mant * 16
                end

                // Compact/floating-style modes.  Only VMode[1:0] is considered
                // significant; modes 1 and 3 share the same measured rate.
                2'b01, 2'b11: begin
                    if (incr != 8'd0) begin
                        case (incr[6:5])
                            2'd0: calc_vol_step = {13'd0, mant, 7'd0};  // mant * 128
                            2'd1: calc_vol_step = {12'd0, mant, 8'd0};  // mant * 256
                            2'd2: calc_vol_step = {11'd0, mant, 9'd0};  // mant * 512
                            2'd3: calc_vol_step = {10'd0, mant, 10'd0}; // mant * 1024
                        endcase
                    end
                end

                // Linear mode: period ~= 65280 / incr.
                2'b10: begin
                    calc_vol_step = {8'd0, incr, 10'd0};          // incr * 1024
                end
            endcase
        end
    endfunction

    always_comb begin
        vol_step = calc_vol_step(v.vol_mode[1:0], v.vol_incr);
    end

    // Interpolation fraction: acc[8:0] = 9-bit (29-bit storage, not MAME's 32-bit [11:3])
    logic [8:0] interp_fract;
    assign interp_fract = v.osc_acc[8:0];

    // Interpolation: combinational from sample1, sample2, fract
    logic signed [16:0] interp_diff;
    logic signed [24:0] interp_raw;
    assign interp_diff = sample2 - sample1;
    assign interp_raw  = ($signed({sample1[15], sample1}) <<< 9) +
                         (interp_diff * $signed({1'b0, interp_fract}));

    // Mix: sample × volume (signed × unsigned)
    logic signed [31:0] mix_l, mix_r;
    assign mix_l = interp_sample * $signed({1'b0, vleft});
    assign mix_r = interp_sample * $signed({1'b0, vright});

    // Direction after bidir flip
    logic new_invert;
    always_comb begin
        if (v.osc_conf[OSC_BIDIR])
            new_invert = ~v.osc_conf[OSC_INVERT];
        else
            new_invert = v.osc_conf[OSC_INVERT];
    end

    // Construct byte address: (saddr[3:0] << 20) | addr[19:0]
    // saddr is 8-bit but only low 4 bits used for 24-bit addressing
    function automatic logic [23:0] make_rom_addr(
        input logic [7:0]  saddr,
        input logic [19:0] addr
    );
        return {saddr[3:0], addr};
    endfunction

    function automatic logic signed [15:0] ulaw_decode(input logic [7:0] code);
        logic [2:0]  ulaw_exp;
        logic [3:0]  ulaw_mant;
        logic [15:0] lut_base;
        logic [15:0] ulaw_value;
        begin
            ulaw_exp  = (~code >> 4) & 3'd7;
            ulaw_mant = ~code & 4'hF;

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

            if (code[7])
                ulaw_decode = $signed({1'b0, ulaw_value[14:0]});
            else
                ulaw_decode = -$signed({1'b0, ulaw_value[14:0]});
        end
    endfunction

    // =========================================================================
    // FSM next-state logic
    // =========================================================================
    always_comb begin
        state_next = state;
        case (state)
            ST_IDLE:           if (start) state_next = ST_VOL_LOOKUP;
            ST_VOL_LOOKUP:     state_next = ST_PAN_LOOKUP_L;
            ST_PAN_LOOKUP_L:   state_next = ST_PAN_LOOKUP_R;
            ST_PAN_LOOKUP_R:   state_next = ST_VOL_WAIT_L;
            ST_VOL_WAIT_L:     state_next = ST_SAMPLE_FETCH_1;
            ST_SAMPLE_FETCH_1: state_next = ST_VOL_WAIT_R;
            ST_VOL_WAIT_R:     if (rom_data_valid) state_next = ST_SAMPLE_FETCH_2;
            ST_SAMPLE_FETCH_2: state_next = ST_SAMPLE_WAIT;
            ST_SAMPLE_WAIT:    if (rom_data_valid) state_next = ST_INTERPOLATE;
            ST_INTERPOLATE:    state_next = ST_MIX;
            ST_MIX:            state_next = ST_OSC_UPDATE;
            ST_OSC_UPDATE:     state_next = ST_VOL_ENV_UPDATE;
            ST_VOL_ENV_UPDATE: state_next = ST_DONE;
            ST_DONE:             state_next = ST_IDLE;
            default:           state_next = ST_IDLE;
        endcase
    end

    // =========================================================================
    // FSM — registered data processing
    // =========================================================================
    always_ff @(posedge clk) begin
        if (!reset_n || clear) begin
            state         <= ST_IDLE;
            done          <= 1'b0;
            audio_valid   <= 1'b0;
            irq_osc_r    <= 1'b0;
            irq_vol_r    <= 1'b0;
            rom_rd        <= 1'b0;
            rom_byte_addr <= 24'd0;
            vleft         <= 16'd0;
            vright        <= 16'd0;
            sample1       <= 16'sd0;
            sample2       <= 16'sd0;
            interp_sample <= 16'sd0;
            audio_left    <= 24'sd0;
            audio_right   <= 24'sd0;
            volacc        <= 12'd0;
            vlefti_s      <= 13'sd0;
            vrighti_s     <= 13'sd0;
            cur_addr      <= 20'd0;
            next_addr     <= 20'd0;
            vol_tbl_addr  <= 12'd0;
            pan_tbl_addr  <= 8'd0;
            v             <= '0;
        end else if (ce) begin
            // Defaults — pulsed signals cleared each cycle
            done        <= 1'b0;
            audio_valid <= 1'b0;
            rom_rd      <= 1'b0;

            state <= state_next;

            case (state)

                // ─────────────────────────────────────────────────────────────
                // IDLE: Latch voice state on start
                // ─────────────────────────────────────────────────────────────
                ST_IDLE: begin
                    if (start) begin
                        v           <= voice_in;
                        irq_osc_r   <= 1'b0;
                        irq_vol_r   <= 1'b0;
                        audio_left  <= 24'sd0;
                        audio_right <= 24'sd0;
                    end
                end

                // ─────────────────────────────────────────────────────────────
                // VOL_LOOKUP: Extract 12-bit vol index, request left pan atten
                // ─────────────────────────────────────────────────────────────
                ST_VOL_LOOKUP: begin
                    volacc       <= v.vol_acc[25:14];
                    pan_tbl_addr <= 8'd255 - v.vol_pan;
                end

                // ─────────────────────────────────────────────────────────────
                // PAN_LOOKUP_L: Pan table returns left atten (combinational).
                // Compute left vol index. Request right pan atten.
                // ─────────────────────────────────────────────────────────────
                ST_PAN_LOOKUP_L: begin
                    // left index = volacc - panlaw[255-pan]
                    vlefti_s     <= $signed({1'b0, volacc}) - $signed({1'b0, pan_tbl_data});
                    pan_tbl_addr <= v.vol_pan;
                end

                // ─────────────────────────────────────────────────────────────
                // PAN_LOOKUP_R: Pan table returns right atten. Compute right
                // vol index. Issue left volume table lookup.
                // Precompute cur_addr and next_addr for sample fetch.
                // ─────────────────────────────────────────────────────────────
                ST_PAN_LOOKUP_R: begin
                    // right index = volacc - panlaw[pan]
                    vrighti_s <= $signed({1'b0, volacc}) - $signed({1'b0, pan_tbl_data});

                    // Issue left volume table lookup (registered, 1-cycle latency)
                    if (vlefti_s > 13'sd0)
                        vol_tbl_addr <= vlefti_s[11:0];
                    else
                        vol_tbl_addr <= 12'd0;

                    // acc is 29-bit 20.9 format. acc[28:9] = 20-bit integer address
                    cur_addr <= v.osc_acc[28:9];

                    next_addr <= v.osc_acc[28:9] + 20'd1;
                end

                // ─────────────────────────────────────────────────────────────
                // VOL_WAIT_L: Wait for left volume table registered output.
                // The vol_tbl_addr was set in ST_PAN_LOOKUP_R. Table registers
                // it this cycle. Result available next cycle (ST_SAMPLE_FETCH_1).
                // ─────────────────────────────────────────────────────────────
                ST_VOL_WAIT_L: begin
                    // Nothing to do — just waiting for vol table pipeline
                end

                // ─────────────────────────────────────────────────────────────
                // SAMPLE_FETCH_1: Read left vol result, issue right vol lookup,
                // issue first ROM read (sample1)
                // ─────────────────────────────────────────────────────────────
                ST_SAMPLE_FETCH_1: begin
                    // Left volume arrived (1-cycle latency).
                    if (vlefti_s > 13'sd0)
                        vleft <= vol_tbl_data;
                    else
                        vleft <= 16'd0;

                    // Issue right volume lookup
                    if (vrighti_s > 13'sd0)
                        vol_tbl_addr <= vrighti_s[11:0];
                    else
                        vol_tbl_addr <= 12'd0;

                    // Issue ROM read for sample1
                    rom_byte_addr <= make_rom_addr(v.osc_saddr, cur_addr);
                    rom_rd        <= 1'b1;
                end

                // ─────────────────────────────────────────────────────────────
                // VOL_WAIT_R: Wait for right volume table + ROM data pipeline.
                // vol_tbl_addr for right was set in ST_SAMPLE_FETCH_1.
                // ROM read for sample1 was issued in ST_SAMPLE_FETCH_1.
                // Both registered outputs arrive next cycle (ST_SAMPLE_FETCH_2).
                // ─────────────────────────────────────────────────────────────
                ST_VOL_WAIT_R: begin
                    // Nothing to do — just waiting for vol table + ROM pipeline
                end

                // ─────────────────────────────────────────────────────────────
                // SAMPLE_FETCH_2: ROM data for sample1 arrived. Latch sample1.
                // Read right vol result. Issue ROM read for sample2.
                // ─────────────────────────────────────────────────────────────
                ST_SAMPLE_FETCH_2: begin
                    // Right volume arrived.
                    if (vrighti_s > 13'sd0)
                        vright <= vol_tbl_data;
                    else
                        vright <= 16'd0;

                    // Decode sample1 from ROM data
                    if (osc_conf_ulaw) begin
                        if (~cur_addr[0])
                            sample1 <= ulaw_decode(rom_data[15:8]);
                        else
                            sample1 <= ulaw_decode(rom_data[7:0]);
                    end else if (osc_conf_8bit_linear) begin
                        // 8-bit signed: extract byte and shift left by 8.
                        // MAME/reference: (s8(sample_byte) << 8).  The low
                        // byte must be zero, not sign/LSB replicated.
                        if (~cur_addr[0])
                            sample1 <= $signed({ rom_data[15:8], 8'h00 });
                        else
                            sample1 <= $signed({ rom_data[7:0],  8'h00 });
                    end else begin
                        // 16-bit: ROM word is the sample (word-aligned assumption)
                        sample1 <= $signed(rom_data);
                    end

                    // Issue ROM read for sample2
                    rom_byte_addr <= make_rom_addr(v.osc_saddr, next_addr);
                    rom_rd        <= 1'b1;
                end

                // ─────────────────────────────────────────────────────────────
                // SAMPLE_WAIT: ROM data for sample2 arrived. Latch sample2.
                // ─────────────────────────────────────────────────────────────
                ST_SAMPLE_WAIT: begin
                    if (osc_conf_ulaw) begin
                        if (~next_addr[0])
                            sample2 <= ulaw_decode(rom_data[15:8]);
                        else
                            sample2 <= ulaw_decode(rom_data[7:0]);
                    end else if (osc_conf_8bit_linear) begin
                        // Decode sample2 from ROM data: (s8(sample_byte) << 8).
                        if (~next_addr[0])
                            sample2 <= $signed({ rom_data[15:8], 8'h00 });
                        else
                            sample2 <= $signed({ rom_data[7:0],  8'h00 });
                    end else begin
                        sample2 <= $signed(rom_data);
                    end
                end

                // ─────────────────────────────────────────────────────────────
                // INTERPOLATE: Linear interpolation between sample1 & sample2
                // ─────────────────────────────────────────────────────────────
                ST_INTERPOLATE: begin
                    interp_sample <= interp_raw[24:9];
                end

                // ─────────────────────────────────────────────────────────────
                // MIX: Scale interpolated sample by volume, output audio
                // ─────────────────────────────────────────────────────────────
                ST_MIX: begin
                    if (!v.osc_ctl[OSC_STOP]) begin
                        audio_left  <= mix_l >>> 15;
                        audio_right <= mix_r >>> 15;
                    end else begin
                        audio_left  <= 24'sd0;
                        audio_right <= 24'sd0;
                    end
                    audio_valid <= 1'b1;
                end

                // ─────────────────────────────────────────────────────────────
                // OSC_UPDATE: Advance oscillator accumulator (spec §6.1)
                // fc bit 0 is unused; MAME uses fc<<2 on 32-bit acc,
                // equivalent to fc>>1 in 29-bit space. Step = fc[15:1].
                // ─────────────────────────────────────────────────────────────
                ST_OSC_UPDATE: begin
                    if (!v.osc_ctl[OSC_STOP] && !v.osc_ctl[OSC_DONE]) begin
                        logic [28:0] next_osc;
                        logic signed [29:0] osc_left;

                        if (v.osc_ctl[OSC_INVERT]) begin
                            next_osc = v.osc_acc - {14'd0, v.osc_fc[15:1]};
                            osc_left = $signed({1'b0, next_osc}) - $signed({1'b0, v.osc_start});
                        end else begin
                            next_osc = v.osc_acc + {14'd0, v.osc_fc[15:1]};
                            osc_left = $signed({1'b0, v.osc_end}) - $signed({1'b0, next_osc});
                        end

                        if (osc_left >= 28'sd0) begin
                            v.osc_acc <= next_osc;
                        end else begin
                            // Fire IRQ if enabled
                            if (v.osc_conf[OSC_IRQ]) begin
                                v.osc_conf[OSC_IRQ_PEND] <= 1'b1;
                                irq_osc_r <= 1'b1;
                            end

                            if (v.osc_conf[OSC_LOOP]) begin
                                // Bidirectional: flip direction
                                if (v.osc_conf[OSC_BIDIR])
                                    v.osc_conf[OSC_INVERT] <= ~v.osc_conf[OSC_INVERT];

                                // Wrap accumulator using new_invert (post-flip direction)
                                if (new_invert) begin
                                    // Now heading reverse: acc = end + left
                                    // left is negative, so acc = end - |overshoot|
                                    v.osc_acc <= v.osc_end[28:0] + osc_left[28:0];
                                end else begin
                                    // Now heading forward: acc = start - left
                                    // left is negative, so acc = start + |overshoot|
                                    v.osc_acc <= v.osc_start[28:0] - osc_left[28:0];
                                end
                            end else begin
                                // One-shot: stop voice, clamp to boundary
                                v.osc_ctl[OSC_DONE] <= 1'b1;
                            end
                        end
                    end
                end

                // ─────────────────────────────────────────────────────────────
                // VOL_ENV_UPDATE: Check boundary and handle loop/wrap/done
                // ─────────────────────────────────────────────────────────────
                ST_VOL_ENV_UPDATE: begin : vol_env_boundary_blk
                    // Compute distance to boundary using signed arithmetic
                    // vol_acc was already updated in the previous state
                    logic signed [26:0] vol_left;
                    logic [25:0] next_vol;

                    // Skip envelope only when done or stopped.  A zero step
                    // (VIncr/VMode with no movement) must STILL evaluate the
                    // boundary: the BIOS voice teardown collapses the window
                    // (VolStart=VolEnd=1) and polls VCtl until DONE sets —
                    // hardware completes that instantly even with vincr=0.
                    // With step 0 inside a valid window, vol_left >= 0 and
                    // nothing changes (static volume stays safe).
                    if (!(v.vol_ctrl[VOL_DONE] || v.vol_ctrl[VOL_STOP])) begin
                        // Update accumulator by the per-voice VMode/VIncr step.
                        if (v.vol_ctrl[VOL_INVERT]) begin
                            next_vol = v.vol_acc - vol_step;
                            vol_left = $signed({1'b0, next_vol}) - $signed({1'b0, v.vol_start});
                        end else begin
                            next_vol = v.vol_acc + vol_step;
                            vol_left = $signed({1'b0, v.vol_end}) - $signed({1'b0, next_vol});
                        end

                        if (vol_left >= 27'sd0) begin
                            // Still within bounds
                            v.vol_acc <= next_vol;
                        end else begin
                            // Boundary crossed or exactly reached

                            // The engine clears the rollover flag at the
                            // boundary: the BIOS voice-teardown collapses the
                            // ramp (VolStart=VolEnd) and polls VCtl bit2
                            // until clear — with a stored-byte readback this
                            // is the only thing that terminates that poll.
                            v.vol_ctrl[VOL_ROLLOVER] <= 1'b0;

                            // Fire IRQ if enabled
                            if (v.vol_ctrl[VOL_IRQ]) begin
                                v.vol_ctrl[VOL_IRQ_PEND] <= 1'b1;
                                irq_vol_r <= 1'b1;
                            end

                            if (v.vol_ctrl[VOL_LOOP]) begin
                                if (v.vol_ctrl[VOL_BIDIR]) begin
                                    if (!v.vol_ctrl[VOL_INVERT]) begin
                                        v.vol_ctrl[VOL_INVERT] <= ~v.vol_ctrl[VOL_INVERT];
                                    end
                                end else begin
                                    if (v.vol_ctrl[VOL_INVERT]) begin
                                        v.vol_acc <= v.vol_end - (v.vol_start - next_vol);
                                    end else begin
                                        v.vol_acc <= v.vol_start + (next_vol - v.vol_start);
                                    end
                                end
                            end else begin
                                // No loop: envelope is done
                                v.vol_ctrl[VOL_DONE] <= 1'b1;
                            end
                        end
                    end
                end

                // ─────────────────────────────────────────────────────────────
                // DONE: Signal completion
                // ─────────────────────────────────────────────────────────────
                ST_DONE: begin
                    done <= 1'b1;
                end

                default: ;
            endcase
        end
    end

    // =========================================================================
    // Output assignments
    // =========================================================================
    assign voice_out = v;
    assign irq_osc  = irq_osc_r;
    assign irq_vol  = irq_vol_r;

endmodule
