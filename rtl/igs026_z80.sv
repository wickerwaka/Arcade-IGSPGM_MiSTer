module IGS026_Z80(
    input clk,

    // CPU interface
    input [23:0] cpu_addr,
    input [15:0] cpu_din,
    output logic [15:0] cpu_dout,
    input cpu_lds_n,
    input cpu_uds_n,
    input cpu_rw,
    input cpu_cs_n,


    output logic v3021_cs_n,
    output logic v3021_wr_n,
    output logic v3021_dout,
    input  v3021_din
);

wire io_access_n = cpu_cs_n | cpu_addr[16];

always_comb begin
    v3021_dout = cpu_din[0];
    
    cpu_dout = 16'h0;

    v3021_cs_n = 1;
    v3021_wr_n = 1;

    if (~io_access_n) begin
        case(cpu_addr[3:0])
            4'h6: begin
                v3021_cs_n = 0;
                v3021_wr_n = cpu_rw;
                cpu_dout[0] = v3021_din;
            end
            default: begin
            end
        endcase
    end
end

endmodule



