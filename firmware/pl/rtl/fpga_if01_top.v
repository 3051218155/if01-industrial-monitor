`timescale 1ns / 1ps
// IF-01 顶层：AXI-Lite + 双路 PWM/方向 → FEP_48IO（Y12/T9/U7/V6）
// 集成：在 TD FPSoC Block Design 中例化本模块，AXI 地址 = 0x40000000

module fpga_if01_top #(
    parameter integer CLK_HZ = 50_000_000
) (
    input  wire        pl_clk,
    input  wire        pl_resetn,
    input  wire [11:0] s_axi_awaddr,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output wire [1:0]  s_axi_bresp,
    output wire        s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [11:0] s_axi_araddr,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output wire        s_axi_rvalid,
    input  wire        s_axi_rready,
    output wire        motor1_pwm,   // Y12
    output wire        motor1_dir,   // T9
    output wire        motor2_pwm,   // U7
    output wire        motor2_dir,   // V6
    output wire        rs485_de,     // N15
    input  wire        rs485_rx,     // N16
    output wire        rs485_tx,     // L17
    output wire        sensor_i2c_scl,
    inout  wire        sensor_i2c_sda,
    output wire        alert_buzzer,
    output wire        alert_led_green,
    output wire        alert_led_yellow,
    output wire        alert_led_red
);

    wire [31:0] m1_ctrl, m1_pwm, m2_ctrl, m2_pwm;
    wire        pl_ready;

    fpga_axi_lite_slave #(.CLK_HZ(CLK_HZ)) u_axi (
        .s_axi_aclk(pl_clk),
        .s_axi_aresetn(pl_resetn),
        .s_axi_awaddr(s_axi_awaddr),
        .s_axi_awvalid(s_axi_awvalid),
        .s_axi_awready(s_axi_awready),
        .s_axi_wdata(s_axi_wdata),
        .s_axi_wstrb(s_axi_wstrb),
        .s_axi_wvalid(s_axi_wvalid),
        .s_axi_wready(s_axi_wready),
        .s_axi_bresp(s_axi_bresp),
        .s_axi_bvalid(s_axi_bvalid),
        .s_axi_bready(s_axi_bready),
        .s_axi_araddr(s_axi_araddr),
        .s_axi_arvalid(s_axi_arvalid),
        .s_axi_arready(s_axi_arready),
        .s_axi_rdata(s_axi_rdata),
        .s_axi_rresp(s_axi_rresp),
        .s_axi_rvalid(s_axi_rvalid),
        .s_axi_rready(s_axi_rready),
        .m1_ctrl(m1_ctrl),
        .m1_pwm(m1_pwm),
        .m2_ctrl(m2_ctrl),
        .m2_pwm(m2_pwm),
        .pl_ready(pl_ready),
        .rs485_de(rs485_de),
        .rs485_tx(rs485_tx),
        .rs485_rx(rs485_rx),
        .sensor_i2c_scl(sensor_i2c_scl),
        .sensor_i2c_sda(sensor_i2c_sda),
        .alert_buzzer(alert_buzzer),
        .alert_led_green(alert_led_green),
        .alert_led_yellow(alert_led_yellow),
        .alert_led_red(alert_led_red)
    );

    fpga_dual_motor #(.CLK_HZ(CLK_HZ)) u_motor (
        .clk(pl_clk),
        .rst_n(pl_resetn),
        .m1_ctrl(m1_ctrl),
        .m1_pwm(m1_pwm),
        .m2_ctrl(m2_ctrl),
        .m2_pwm(m2_pwm),
        .m1_pwm_pin(motor1_pwm),
        .m1_dir_pin(motor1_dir),
        .m2_pwm_pin(motor2_pwm),
        .m2_dir_pin(motor2_dir)
    );

endmodule
