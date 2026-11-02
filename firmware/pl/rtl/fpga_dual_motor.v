`timescale 1ns / 1ps
// IF-01 双路电机：PWM 脚 + 方向脚 → DRV8833

module fpga_dual_motor #(
    parameter integer CLK_HZ = 50_000_000
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [31:0] m1_ctrl,
    input  wire [31:0] m1_pwm,
    input  wire [31:0] m2_ctrl,
    input  wire [31:0] m2_pwm,
    output wire        m1_pwm_pin,   // Y12 → AIN1
    output wire        m1_dir_pin,   // T9  → AIN2
    output wire        m2_pwm_pin,   // U7  → BIN1
    output wire        m2_dir_pin    // V6  → BIN2
);

    wire m1_en    = m1_ctrl[0];
    wire m1_dir   = m1_ctrl[1];
    wire m1_brake = m1_ctrl[2];
    wire m2_en    = m2_ctrl[0];
    wire m2_dir   = m2_ctrl[1];
    wire m2_brake = m2_ctrl[2];

    wire m1_pwm_w, m2_pwm_w;

    fpga_pwm #(.CLK_HZ(CLK_HZ)) u_m1_pwm (
        .clk(clk), .rst_n(rst_n), .enable(m1_en && !m1_brake),
        .duty(m1_pwm[15:0]), .pwm_out(m1_pwm_w)
    );

    fpga_pwm #(.CLK_HZ(CLK_HZ)) u_m2_pwm (
        .clk(clk), .rst_n(rst_n), .enable(m2_en && !m2_brake),
        .duty(m2_pwm[15:0]), .pwm_out(m2_pwm_w)
    );

    assign m1_pwm_pin = m1_en && !m1_brake ? m1_pwm_w : 1'b0;
    assign m1_dir_pin = m1_en && !m1_brake ? m1_dir   : 1'b0;
    assign m2_pwm_pin = m2_en && !m2_brake ? m2_pwm_w : 1'b0;
    assign m2_dir_pin = m2_en && !m2_brake ? m2_dir   : 1'b0;

endmodule
