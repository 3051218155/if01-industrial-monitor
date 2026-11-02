`timescale 1ns / 1ns

// Single-clock I2C master compatible with the original uii2c interface.
// The divided SCL waveform is generated as data; all state updates occur on
// I_clk so no fabric-generated clock is routed through local resources.
module uii2c_sc #(
    parameter integer WMEN_LEN = 8'd0,
    parameter integer RMEN_LEN = 8'd0,
    parameter integer CLK_DIV  = 16'd499
) (
    input  wire                        I_clk,
    input  wire                        I_rstn,
    output reg                         O_iic_scl,
    inout  wire                        IO_iic_sda,
    input  wire [WMEN_LEN*8-1:0]       I_wr_data,
    input  wire [7:0]                  I_wr_cnt,
    output reg  [RMEN_LEN*8-1:0]       O_rd_data,
    input  wire [7:0]                  I_rd_cnt,
    input  wire                        I_iic_req,
    input  wire                        I_iic_mode,
    output reg                         O_iic_busy,
    output reg                         O_iic_bus_error,
    output reg                         IO_iic_sda_dg
);

    localparam [2:0] IDLE  = 3'd0;
    localparam [2:0] START = 3'd1;
    localparam [2:0] W_WAIT = 3'd2;
    localparam [2:0] W_ACK = 3'd3;
    localparam [2:0] R_WAIT = 3'd4;
    localparam [2:0] R_ACK = 3'd5;
    localparam [2:0] STOP1 = 3'd6;
    localparam [2:0] STOP2 = 3'd7;

    localparam integer SCL_DIV = CLK_DIV / 2;
    localparam integer OFFSET = SCL_DIV - SCL_DIV / 4;

    reg [15:0] clkdiv;
    reg        scl_clk;
    reg        scl_clk_d;
    reg        scl_r;
    reg        sda_o;
    reg [7:0]  sda_r;
    reg [7:0]  sda_i_r;
    reg [7:0]  wcnt;
    reg [7:0]  rcnt;
    reg [2:0]  bcnt;
    reg        rd_req;
    reg [2:0]  state;
    reg        sda_meta;
    reg        sda_sync;

    wire sda_i = sda_sync;
    wire scl_offset = (clkdiv == OFFSET);
    wire scl_pos = scl_clk && !scl_clk_d;
    wire scl_neg = !scl_clk && scl_clk_d;

    assign IO_iic_sda = sda_o ? 1'bz : 1'b0;

    always @(posedge I_clk or negedge I_rstn) begin
        if (!I_rstn) begin
            sda_meta <= 1'b1;
            sda_sync <= 1'b1;
        end else begin
            sda_meta <= IO_iic_sda;
            sda_sync <= sda_meta;
        end
    end

    always @(*) begin
        if (state == IDLE || state == STOP1 || state == STOP2)
            scl_r = 1'b1;
        else
            scl_r = scl_clk;
    end

    always @(*) begin
        if (state == START || state == STOP1 ||
            (state == R_ACK && rcnt != I_rd_cnt))
            sda_o = 1'b0;
        else if (state == W_WAIT)
            sda_o = sda_r[7];
        else
            sda_o = 1'b1;
    end

    always @(posedge I_clk or negedge I_rstn) begin
        if (!I_rstn)
            O_iic_scl <= 1'b0;
        else
            O_iic_scl <= scl_offset ? scl_r : O_iic_scl;
    end

    always @(posedge I_clk or negedge I_rstn) begin
        if (!I_rstn) begin
            clkdiv         <= 16'd0;
            scl_clk        <= 1'b0;
            scl_clk_d      <= 1'b0;
            sda_r          <= 8'd0;
            sda_i_r        <= 8'd0;
            wcnt           <= 8'd0;
            rcnt           <= 8'd0;
            bcnt           <= 3'd0;
            rd_req         <= 1'b0;
            state          <= IDLE;
            O_rd_data      <= {RMEN_LEN*8{1'b0}};
            O_iic_busy     <= 1'b0;
            O_iic_bus_error <= 1'b0;
            IO_iic_sda_dg  <= 1'b1;
        end else begin
            scl_clk_d     <= scl_clk;
            IO_iic_sda_dg <= sda_i;

            if (clkdiv < SCL_DIV) begin
                clkdiv <= clkdiv + 1'b1;
            end else begin
                clkdiv  <= 16'd0;
                scl_clk <= !scl_clk;
            end

            if (scl_pos) begin
                if (I_iic_req || rd_req || O_iic_bus_error)
                    O_iic_busy <= 1'b1;
                else if (state == IDLE)
                    O_iic_busy <= 1'b0;

                if (state == W_ACK || state == START) begin
                    sda_r <= I_wr_data[wcnt*8 +: 8];
                    if (rd_req)
                        sda_r <= {I_wr_data[7:1], 1'b1};
                end else if (state == W_WAIT) begin
                    sda_r <= {sda_r[6:0], 1'b1};
                end

                case (state)
                    IDLE: begin
                        if (I_iic_req || rd_req) begin
                            state <= START;
                        end else begin
                            wcnt <= 8'd0;
                            rcnt <= 8'd0;
                        end
                    end
                    START: begin
                        bcnt  <= 3'd7;
                        state <= W_WAIT;
                    end
                    W_WAIT: begin
                        if (bcnt > 3'd0) begin
                            bcnt <= bcnt - 1'b1;
                        end else begin
                            wcnt  <= wcnt + 1'b1;
                            state <= W_ACK;
                        end
                    end
                    W_ACK: begin
                        if (wcnt < I_wr_cnt) begin
                            bcnt  <= 3'd7;
                            state <= W_WAIT;
                        end else if (I_rd_cnt > 0) begin
                            if (!rd_req && I_iic_mode) begin
                                rd_req <= 1'b1;
                                state  <= IDLE;
                            end else begin
                                bcnt  <= 3'd7;
                                state <= R_WAIT;
                            end
                        end else begin
                            state <= STOP1;
                        end
                    end
                    R_WAIT: begin
                        rd_req <= 1'b0;
                        bcnt   <= bcnt - 1'b1;
                        if (bcnt == 3'd0) begin
                            if (rcnt < I_rd_cnt)
                                rcnt <= rcnt + 1'b1;
                            state <= R_ACK;
                        end
                    end
                    R_ACK: begin
                        bcnt  <= 3'd7;
                        state <= (rcnt < I_rd_cnt) ? R_WAIT : STOP1;
                    end
                    STOP1: begin
                        rd_req <= 1'b0;
                        state  <= STOP2;
                    end
                    STOP2: begin
                        state <= IDLE;
                    end
                    default: state <= IDLE;
                endcase
            end

            if (scl_neg) begin
                if (state == R_WAIT)
                    sda_i_r <= {sda_i_r[6:0], sda_i};
                else if (state == R_ACK)
                    O_rd_data[(rcnt-1'b1)*8 +: 8] <= sda_i_r;
                else if (state == IDLE)
                    sda_i_r <= 8'd0;

                if (state == W_ACK && sda_i)
                    O_iic_bus_error <= 1'b1;
                else if (!I_iic_req)
                    O_iic_bus_error <= 1'b0;
            end
        end
    end

endmodule
