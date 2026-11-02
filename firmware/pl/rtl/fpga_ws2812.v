`timescale 1ns / 1ps
// ############################################################
// 【当前未使用 / 可选模块】
// 本项目实际采购的是「共阴极 RGB 彩灯模块（PH2.0-5P，R/G/B 三根
// 独立控制线）」，不是幻彩串行灯，因此本模块没有例化。
// 告警灯的实际驱动逻辑在 pl/rtl/fpga_sensor_core.v 里。
//
// 保留本文件仅供以后换用 WS2812/SK6812 幻彩灯时参考。
// ############################################################
//
// WS2812 / SK6812 幻彩 RGB 单线驱动。
//
// 把三级告警状态映射成颜色，并**持续刷新**（每帧 24 bit + 复位间隔）：
//   alert_level 0 -> 绿    1 -> 黄    2 -> 红
//
// 时序（WS2812B 典型值，800kHz）：
//   T0H = 350ns, T1H = 700ns, 位周期 = 1250ns, 复位低电平 = 300us
//   数据顺序 GRB，MSB first
//
// 注意：CLK_HZ 必须与实际 pl_clk 一致，否则颜色会错乱（时序偏了）。

module fpga_ws2812 #(
    parameter integer CLK_HZ    = 50_000_000,
    parameter integer RESET_US  = 300,
    parameter integer T0H_NS    = 350,
    parameter integer T1H_NS    = 700,
    parameter integer PERIOD_NS = 1250
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        enable,
    input  wire [1:0]  alert_level,
    output reg         dout
);

    // 50MHz -> TICKS_PER_US = 50
    localparam integer TICKS_PER_US = (CLK_HZ + 999_999) / 1_000_000;
    localparam integer T_PER = (TICKS_PER_US * PERIOD_NS) / 1000;  // 62
    localparam integer T_0H  = (TICKS_PER_US * T0H_NS)    / 1000;  // 17
    localparam integer T_1H  = (TICKS_PER_US * T1H_NS)    / 1000;  // 35
    localparam integer T_RST = TICKS_PER_US * RESET_US;            // 15000

    // {G[7:0], R[7:0], B[7:0]}
    localparam [23:0] COLOR_GREEN  = 24'hFF_00_00;
    localparam [23:0] COLOR_YELLOW = 24'hFF_FF_00;
    localparam [23:0] COLOR_RED    = 24'h00_FF_00;

    localparam [1:0] S_RESET = 2'd0;
    localparam [1:0] S_BIT   = 2'd1;

    reg [1:0]  state;
    reg [15:0] tick;
    reg [4:0]  bit_idx;
    reg [23:0] shifter;
    reg [31:0] rst_cnt;

    wire [23:0] color_now = (alert_level == 2'd0) ? COLOR_GREEN :
                            (alert_level == 2'd1) ? COLOR_YELLOW : COLOR_RED;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state   <= S_RESET;
            tick    <= 16'd0;
            bit_idx <= 5'd0;
            shifter <= 24'd0;
            rst_cnt <= 32'd0;
            dout    <= 1'b0;
        end else if (!enable) begin
            state   <= S_RESET;
            tick    <= 16'd0;
            bit_idx <= 5'd0;
            rst_cnt <= 32'd0;
            dout    <= 1'b0;
        end else begin
            case (state)
                S_RESET: begin
                    dout <= 1'b0;
                    if (rst_cnt >= T_RST) begin
                        rst_cnt <= 32'd0;
                        shifter <= color_now;
                        bit_idx <= 5'd0;
                        tick    <= 16'd0;
                        state   <= S_BIT;
                    end else begin
                        rst_cnt <= rst_cnt + 1'b1;
                    end
                end

                S_BIT: begin
                    dout <= (tick < (shifter[23] ? T_1H : T_0H)) ? 1'b1 : 1'b0;

                    if (tick >= (T_PER - 1)) begin
                        tick    <= 16'd0;
                        shifter <= {shifter[22:0], 1'b0};
                        if (bit_idx >= 5'd23) begin
                            bit_idx <= 5'd0;
                            state   <= S_RESET;
                        end else begin
                            bit_idx <= bit_idx + 1'b1;
                        end
                    end else begin
                        tick <= tick + 1'b1;
                    end
                end

                default: state <= S_RESET;
            endcase
        end
    end

endmodule
