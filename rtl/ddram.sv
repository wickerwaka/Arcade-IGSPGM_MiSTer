interface ddr_if;
    logic        acquire;

    logic [31:0] addr;
    logic [63:0] wdata;
    logic [63:0] rdata;
    logic        read;
    logic        write;
    logic  [7:0] burstcnt;
    logic  [7:0] byteenable;
    logic        busy;
    logic        rdata_ready;

    modport to_host(
        output addr, wdata, read, write, burstcnt, byteenable, acquire,
        input rdata, busy, rdata_ready
    );

    modport from_host(
        output rdata, busy, rdata_ready,
        input addr, wdata, read, write, burstcnt, byteenable, acquire
    );


endinterface

module ddr_mux(
    input clk,

    ddr_if.to_host x,

    ddr_if.from_host a,
    ddr_if.from_host b
);

reg a_active = 0;

always_comb begin
    a.rdata = x.rdata;
    b.rdata = x.rdata;

    if (a_active) begin
        x.addr = a.addr;
        x.wdata = a.wdata;
        x.read = a.read;
        x.write = a.write;
        x.burstcnt = a.burstcnt;
        x.byteenable = a.byteenable;

        a.busy = x.busy;
        a.rdata_ready = x.rdata_ready;
        a.rdata = x.rdata;

        b.busy = 1;
        b.rdata_ready = 0;
    end else begin
        x.addr = b.addr;
        x.wdata = b.wdata;
        x.read = b.read;
        x.write = b.write;
        x.burstcnt = b.burstcnt;
        x.byteenable = b.byteenable;

        b.busy = x.busy;
        b.rdata_ready = x.rdata_ready;
        b.rdata = x.rdata;

        a.busy = 1;
        a.rdata_ready = 0;
    end
end

assign x.acquire = a.acquire | b.acquire;

always_ff @(posedge clk) begin
    if (a.acquire & ~b.acquire) a_active <= 1;
    if (~a.acquire & b.acquire) a_active <= 0;
end

endmodule


// 4-input version of ddr_mux with the same acquire/release semantics: the
// current owner keeps the bus until it releases (no mid-burst preemption); when
// the owner releases, the bus is granted to the highest-priority requester
// (a > b > c > d).  Non-owners see busy=1 / rdata_ready=0.  Tie an unused input's
// acquire (and read/write) low to leave it out of arbitration.
module ddr_mux4(
    input clk,

    ddr_if.to_host x,

    ddr_if.from_host a,
    ddr_if.from_host b,
    ddr_if.from_host c,
    ddr_if.from_host d
);

reg [1:0] sel = 0;

wire [3:0] acq  = { d.acquire, c.acquire, b.acquire, a.acquire };
wire       any  = |acq;
wire [1:0] pick = a.acquire ? 2'd0 :
                  b.acquire ? 2'd1 :
                  c.acquire ? 2'd2 : 2'd3;

always_comb begin
    a.rdata = x.rdata; b.rdata = x.rdata; c.rdata = x.rdata; d.rdata = x.rdata;

    a.busy = 1; b.busy = 1; c.busy = 1; d.busy = 1;
    a.rdata_ready = 0; b.rdata_ready = 0; c.rdata_ready = 0; d.rdata_ready = 0;

    // default (overwritten by the selected input below)
    x.addr = a.addr; x.wdata = a.wdata; x.read = a.read; x.write = a.write;
    x.burstcnt = a.burstcnt; x.byteenable = a.byteenable;

    case (sel)
        2'd0: begin
            x.addr=a.addr; x.wdata=a.wdata; x.read=a.read; x.write=a.write;
            x.burstcnt=a.burstcnt; x.byteenable=a.byteenable;
            a.busy=x.busy; a.rdata_ready=x.rdata_ready;
        end
        2'd1: begin
            x.addr=b.addr; x.wdata=b.wdata; x.read=b.read; x.write=b.write;
            x.burstcnt=b.burstcnt; x.byteenable=b.byteenable;
            b.busy=x.busy; b.rdata_ready=x.rdata_ready;
        end
        2'd2: begin
            x.addr=c.addr; x.wdata=c.wdata; x.read=c.read; x.write=c.write;
            x.burstcnt=c.burstcnt; x.byteenable=c.byteenable;
            c.busy=x.busy; c.rdata_ready=x.rdata_ready;
        end
        2'd3: begin
            x.addr=d.addr; x.wdata=d.wdata; x.read=d.read; x.write=d.write;
            x.burstcnt=d.burstcnt; x.byteenable=d.byteenable;
            d.busy=x.busy; d.rdata_ready=x.rdata_ready;
        end
    endcase
end

assign x.acquire = a.acquire | b.acquire | c.acquire | d.acquire;

// Switch owner only when the current owner is not holding the bus, granting to
// the highest-priority requester.  While the owner asserts acquire it is kept.
always_ff @(posedge clk) begin
    if (~acq[sel] & any) sel <= pick;
end

endmodule


