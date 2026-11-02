`timescale 1ns / 1ps
// 16-tap low-pass FIR for IF-01 vibration samples.
// Coefficients sum to 64, output = accumulator >>> 6.

module fpga_fir_filter #(
    parameter integer TAP_COUNT = 16,
    parameter integer SAMPLE_W  = 16,
    parameter integer COEFF_W   = 16,
    parameter integer SHIFT     = 6
) (
    input  wire                    clk,
    input  wire                    rst_n,
    input  wire                    enable,
    input  wire                    sample_valid,
    input  wire signed [SAMPLE_W-1:0] sample_in,
    output reg  signed [SAMPLE_W-1:0] sample_out,
    output reg                     sample_out_valid
);

    reg signed [SAMPLE_W-1:0] delay_line [0:TAP_COUNT-1];
    reg signed [31:0]         accum;
    reg [4:0]                 tap_index;
    reg                        busy;

    integer i;

    reg signed [COEFF_W-1:0] coeff;
    always @(*) begin
        case (tap_index)
            5'd0:  coeff = 16'sd1;
            5'd1:  coeff = 16'sd2;
            5'd2:  coeff = 16'sd3;
            5'd3:  coeff = 16'sd4;
            5'd4:  coeff = 16'sd5;
            5'd5:  coeff = 16'sd6;
            5'd6:  coeff = 16'sd7;
            5'd7:  coeff = 16'sd8;
            5'd8:  coeff = 16'sd8;
            5'd9:  coeff = 16'sd7;
            5'd10: coeff = 16'sd6;
            5'd11: coeff = 16'sd5;
            5'd12: coeff = 16'sd4;
            5'd13: coeff = 16'sd3;
            5'd14: coeff = 16'sd2;
            default: coeff = 16'sd1;
        endcase
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (i = 0; i < TAP_COUNT; i = i + 1)
                delay_line[i] <= {SAMPLE_W{1'b0}};
            accum            <= 32'sd0;
            tap_index        <= 5'd0;
            busy             <= 1'b0;
            sample_out       <= {SAMPLE_W{1'b0}};
            sample_out_valid <= 1'b0;
        end else begin
            sample_out_valid <= 1'b0;

            if (!enable) begin
                busy       <= 1'b0;
                tap_index  <= 5'd0;
                accum      <= 32'sd0;
            end else if (!busy) begin
                if (sample_valid) begin
                    for (i = TAP_COUNT - 1; i > 0; i = i - 1)
                        delay_line[i] <= delay_line[i-1];
                    delay_line[0] <= sample_in;
                    accum         <= 32'sd0;
                    tap_index     <= 5'd0;
                    busy          <= 1'b1;
                end
            end else begin
                if (tap_index == TAP_COUNT - 1) begin
                    sample_out       <= (accum + delay_line[tap_index] * coeff) >>> SHIFT;
                    sample_out_valid <= 1'b1;
                    busy             <= 1'b0;
                    tap_index        <= 5'd0;
                end else begin
                    accum     <= accum + delay_line[tap_index] * coeff;
                    tap_index <= tap_index + 1'b1;
                end
            end
        end
    end

endmodule
