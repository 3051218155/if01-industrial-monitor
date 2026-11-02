`timescale 1ns / 1ps
// Three-level alert output for buzzer and RGB LED.

module fpga_alert_gpio (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        enable,
    input  wire [31:0] value,
    input  wire [31:0] threshold_low,
    input  wire [31:0] threshold_high,
    output reg  [1:0]  alert_level,
    output reg         buzzer,
    output reg         led_green,
    output reg         led_yellow,
    output reg         led_red
);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            alert_level <= 2'd0;
            buzzer      <= 1'b0;
            led_green   <= 1'b1;
            led_yellow  <= 1'b0;
            led_red     <= 1'b0;
        end else if (!enable) begin
            alert_level <= 2'd0;
            buzzer      <= 1'b0;
            led_green   <= 1'b1;
            led_yellow  <= 1'b0;
            led_red     <= 1'b0;
        end else if (value >= threshold_high) begin
            alert_level <= 2'd2;
            buzzer      <= 1'b1;
            led_green   <= 1'b0;
            led_yellow  <= 1'b0;
            led_red     <= 1'b1;
        end else if (value >= threshold_low) begin
            alert_level <= 2'd1;
            buzzer      <= 1'b0;
            led_green   <= 1'b0;
            led_yellow  <= 1'b1;
            led_red     <= 1'b0;
        end else begin
            alert_level <= 2'd0;
            buzzer      <= 1'b0;
            led_green   <= 1'b1;
            led_yellow  <= 1'b0;
            led_red     <= 1'b0;
        end
    end

endmodule
