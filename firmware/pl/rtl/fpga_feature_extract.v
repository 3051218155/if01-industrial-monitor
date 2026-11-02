`timescale 1ns / 1ps
// Windowed time-domain features:
// RMS, peak, mean absolute value and zero-crossing count.

module fpga_feature_extract #(
    parameter integer WINDOW_SIZE = 256,
    parameter integer SAMPLE_W    = 16
) (
    input  wire                    clk,
    input  wire                    rst_n,
    input  wire                    enable,
    input  wire                    sample_valid,
    input  wire signed [SAMPLE_W-1:0] sample_in,
    output reg  [31:0]             rms,
    output reg  [15:0]             peak,
    output reg  [31:0]             mean_abs,
    output reg  [15:0]             zero_crossings,
    output reg                     feature_valid
);

    localparam integer WINDOW_BITS = 8;
    localparam integer SQRT_ITER   = 16;

    reg [47:0] sum_sq;
    reg [31:0] sum_abs;
    reg [15:0] peak_acc;
    reg [15:0] zc_acc;
    reg [WINDOW_BITS:0] sample_count;
    reg signed [SAMPLE_W-1:0] prev_sample;
    reg prev_valid;

    reg [47:0] sqrt_rem;
    reg [31:0] sqrt_root;
    reg [31:0] sqrt_bit;
    reg [4:0]  sqrt_count;
    reg        sqrt_busy;

    wire signed [SAMPLE_W-1:0] sample_abs_signed =
        sample_in[SAMPLE_W-1] ? -sample_in : sample_in;
    wire [SAMPLE_W-1:0] sample_abs = sample_abs_signed;
    wire [31:0] sample_sq = sample_in * sample_in;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sum_sq          <= 48'd0;
            sum_abs         <= 32'd0;
            peak_acc        <= 16'd0;
            zc_acc          <= 16'd0;
            sample_count    <= 0;
            prev_sample     <= {SAMPLE_W{1'b0}};
            prev_valid      <= 1'b0;
            rms             <= 32'd0;
            peak            <= 16'd0;
            mean_abs        <= 32'd0;
            zero_crossings  <= 16'd0;
            feature_valid   <= 1'b0;
            sqrt_rem        <= 48'd0;
            sqrt_root       <= 32'd0;
            sqrt_bit        <= 32'd0;
            sqrt_count      <= 5'd0;
            sqrt_busy       <= 1'b0;
        end else begin
            feature_valid <= 1'b0;

            if (!enable) begin
                sum_sq       <= 48'd0;
                sum_abs      <= 32'd0;
                peak_acc     <= 16'd0;
                zc_acc       <= 16'd0;
                sample_count <= 0;
                prev_valid   <= 1'b0;
                sqrt_busy    <= 1'b0;
            end else if (!sqrt_busy) begin
                if (sample_valid) begin
                    sum_sq   <= sum_sq + sample_sq;
                    sum_abs  <= sum_abs + sample_abs;
                    if (sample_abs > peak_acc)
                        peak_acc <= sample_abs;
                    if (prev_valid && (sample_in[SAMPLE_W-1] != prev_sample[SAMPLE_W-1]))
                        zc_acc <= zc_acc + 1'b1;
                    prev_sample <= sample_in;
                    prev_valid  <= 1'b1;

                    if (sample_count == WINDOW_SIZE - 1) begin
                        // 先取"平方均值"再开方，否则喂给 sqrt 的是 256 倍
                        // 的平方和，结果会放大 16 倍并很快撞到饱和点。
                        sqrt_rem   <= (sum_sq + sample_sq) >> WINDOW_BITS;
                        sqrt_root  <= 32'd0;
                        sqrt_bit   <= 32'h4000_0000;
                        sqrt_count <= 5'd0;
                        sqrt_busy  <= 1'b1;
                        peak       <= (sample_abs > peak_acc) ? sample_abs : peak_acc;
                        mean_abs   <= (sum_abs + sample_abs) >> WINDOW_BITS;
                        zero_crossings <= zc_acc;
                        sum_sq     <= 48'd0;
                        sum_abs    <= 32'd0;
                        peak_acc   <= 16'd0;
                        zc_acc     <= 16'd0;
                        sample_count <= 0;
                    end else begin
                        sample_count <= sample_count + 1'b1;
                    end
                end
            end else begin
                if (sqrt_count == SQRT_ITER - 1) begin
                    // 本模块的 restoring sqrt 输出是真值的 2 倍，右移修正。
                    rms           <= sqrt_root >> 1;
                    feature_valid <= 1'b1;
                    sqrt_busy     <= 1'b0;
                end else if (sqrt_rem >= (sqrt_root + sqrt_bit)) begin
                    sqrt_rem   <= sqrt_rem - (sqrt_root + sqrt_bit);
                    sqrt_root  <= (sqrt_root >> 1) + sqrt_bit;
                    sqrt_bit   <= sqrt_bit >> 2;
                    sqrt_count <= sqrt_count + 1'b1;
                end else begin
                    sqrt_root  <= sqrt_root >> 1;
                    sqrt_bit   <= sqrt_bit >> 2;
                    sqrt_count <= sqrt_count + 1'b1;
                end
            end
        end
    end

endmodule
