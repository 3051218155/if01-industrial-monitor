`timescale 1ns / 1ps
// IF-01: PWM 0~1000 -> duty, 50 kHz (50 MHz clock / 1000)

module fpga_pwm #(
    parameter integer CLK_HZ = 50_000_000,
    parameter integer PWM_HZ = 50_000,
    parameter integer MAX_DUTY = 1000
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        enable,
    input  wire [15:0] duty,   // 0..1000
    output reg         pwm_out
);

    localparam integer PERIOD = CLK_HZ / PWM_HZ;

    reg [15:0] cnt;

    wire [15:0] duty_clamped = (duty > MAX_DUTY) ? MAX_DUTY[15:0] : duty;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cnt     <= 16'd0;
            pwm_out <= 1'b0;
        end else if (!enable || duty_clamped == 16'd0) begin
            cnt     <= 16'd0;
            pwm_out <= 1'b0;
        end else begin
            if (cnt >= PERIOD - 1) begin
                cnt <= 16'd0;
            end else begin
                cnt <= cnt + 16'd1;
            end
            pwm_out <= (cnt < duty_clamped);
        end
    end

endmodule
