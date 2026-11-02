`timescale 1ns / 1ps
// Sensor -> FIR -> feature -> alert pipeline.
// source_sel = 0: internal test waveform, 1: MPU-6050 on J12 I2C.

module fpga_sensor_core #(
    parameter integer CLK_HZ     = 50_000_000,
    parameter integer SAMPLE_HZ  = 1_000,
    parameter integer WINDOW_SIZE = 256,
    // 蜂鸣器模块极性：0 = 高电平触发（严重时输出 3.3V 让它响）—— 实测本模块为高触发
    //                   1 = 低电平触发（严重时输出 0V 让它响）
    parameter integer BUZZER_ACTIVE_LOW = 0
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        enable,
    input  wire        source_sel,
    input  wire [1:0]  fault_mode,
    input  wire [31:0] threshold_low,
    input  wire [31:0] threshold_high,
    output wire        i2c_scl,
    inout  wire        i2c_sda,
    output wire signed [15:0] raw_sample,
    output wire signed [15:0] accel_y,
    output wire signed [15:0] accel_z,
    output wire signed [15:0] sensor_temperature,
    output wire signed [15:0] gyro_x,
    output wire signed [15:0] gyro_y,
    output wire signed [15:0] gyro_z,
    output wire signed [15:0] filtered_sample,
    output wire [31:0] feature_rms,
    output wire [15:0] feature_peak,
    output wire [31:0] feature_mean_abs,
    output wire [15:0] feature_zcr,
    output wire [7:0]  mpu_whoami,
    output wire        sample_valid,
    output wire        feature_valid,
    output wire        mpu_busy,
    output wire        mpu_error,
    output wire [1:0]  alert_level,
    output wire        alert_buzzer,
    output wire        alert_led_green,
    output wire        alert_led_yellow,
    output wire        alert_led_red
);

    wire        sim_valid;
    wire signed [15:0] sim_sample;
    wire        mpu_valid;
    wire signed [15:0] mpu_ax;
    wire signed [15:0] mpu_ay;
    wire signed [15:0] mpu_az;
    wire signed [15:0] mpu_temp;
    wire signed [15:0] mpu_gx;
    wire signed [15:0] mpu_gy;
    wire signed [15:0] mpu_gz;

    wire signed [15:0] fir_sample;
    wire               fir_valid;

    fpga_sensor_sim #(
        .CLK_HZ(CLK_HZ),
        .SAMPLE_HZ(SAMPLE_HZ)
    ) u_sim (
        .clk(clk),
        .rst_n(rst_n),
        .enable(enable && !source_sel),
        .fault_mode(fault_mode),
        .sample_valid(sim_valid),
        .sample_data(sim_sample)
    );

    fpga_mpu6050_reader #(
        .CLK_HZ(CLK_HZ),
        .I2C_HZ(100_000),
        .PERIOD_US(2_000)
    ) u_mpu (
        .clk(clk),
        .rst_n(rst_n),
        .enable(enable && source_sel),
        .i2c_scl(i2c_scl),
        .i2c_sda(i2c_sda),
        .whoami(mpu_whoami),
        .ax(mpu_ax),
        .ay(mpu_ay),
        .az(mpu_az),
        .temp(mpu_temp),
        .gx(mpu_gx),
        .gy(mpu_gy),
        .gz(mpu_gz),
        .data_valid(mpu_valid),
        .busy(mpu_busy),
        .error(mpu_error)
    );

    assign sample_valid = source_sel ? mpu_valid : sim_valid;
    assign raw_sample   = source_sel ? mpu_ax : sim_sample;
    assign accel_y         = mpu_ay;
    assign accel_z         = mpu_az;
    assign sensor_temperature = mpu_temp;
    assign gyro_x          = mpu_gx;
    assign gyro_y          = mpu_gy;
    assign gyro_z          = mpu_gz;

    fpga_fir_filter #(
        .TAP_COUNT(16),
        .SAMPLE_W(16),
        .COEFF_W(16),
        .SHIFT(6)
    ) u_fir (
        .clk(clk),
        .rst_n(rst_n),
        .enable(enable),
        .sample_valid(sample_valid),
        .sample_in(raw_sample),
        .sample_out(fir_sample),
        .sample_out_valid(fir_valid)
    );

    // ---- 去直流（高通）：减掉重力/零偏，只留下振动分量 ----
    // MPU 静止时某轴会有几千到一万多的直流偏置，如果不减掉，RMS 会被
    // 直流完全主导，手压电机造成的交流变化根本看不出来。
    // 用一阶低通估计直流（时间常数约 256 个采样），再相减。
    reg signed [31:0] dc_est;                 // Q8 精度的直流估计
    wire signed [31:0] dc_val  = dc_est >>> 8;
    wire signed [31:0] ac_full = $signed(fir_sample) - dc_val;
    wire signed [15:0] ac_sample =
        (ac_full >  32'sd32767) ?  16'sd32767 :
        (ac_full < -32'sd32768) ? -16'sd32768 :
        ac_full[15:0];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            dc_est <= 32'sd0;
        else if (!enable)
            dc_est <= 32'sd0;
        else if (fir_valid)
            dc_est <= dc_est + ($signed(fir_sample) - dc_val);
    end

    // 0x6C 现在输出的是"去直流后的振动信号"，不再含重力偏置。
    assign filtered_sample = ac_sample;

    fpga_feature_extract #(
        .WINDOW_SIZE(WINDOW_SIZE),
        .SAMPLE_W(16)
    ) u_features (
        .clk(clk),
        .rst_n(rst_n),
        .enable(enable),
        .sample_valid(fir_valid),
        .sample_in(ac_sample),
        .rms(feature_rms),
        .peak(feature_peak),
        .mean_abs(feature_mean_abs),
        .zero_crossings(feature_zcr),
        .feature_valid(feature_valid)
    );

    wire buzzer_active_high;

    fpga_alert_gpio u_alert (
        .clk(clk),
        .rst_n(rst_n),
        .enable(enable),
        .value(feature_rms),
        .threshold_low(threshold_low),
        .threshold_high(threshold_high),
        .alert_level(alert_level),
        .buzzer(buzzer_active_high),
        .led_green(),
        .led_yellow(),
        .led_red()
    );

    // 蜂鸣器极性转换：高电平触发的模块，严重告警时给 3.3V，平时 0V。
    assign alert_buzzer = BUZZER_ACTIVE_LOW ? ~buzzer_active_high : buzzer_active_high;

    // 共阴极 RGB 彩灯模块（PH2.0-5P，R/G/B 各一根控制线，公共端接 GND）。
    // 三档用三种差别明显的颜色（绿/黄容易混，改用蓝）：
    //   level 0 正常  -> 绿 (G)
    //   level 1 预警  -> 蓝 (B)
    //   level 2 严重  -> 红 (R)
    // 引脚对应：V11 -> G，Y7 -> R，Y9 -> B。
    assign alert_led_green  = (alert_level == 2'd0);  // V11 -> G
    assign alert_led_yellow = (alert_level == 2'd2);  // Y7  -> R
    assign alert_led_red    = (alert_level == 2'd1);  // Y9  -> B

endmodule
