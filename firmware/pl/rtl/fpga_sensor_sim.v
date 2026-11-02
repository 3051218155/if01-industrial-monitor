`timescale 1ns / 1ps
// Internal vibration source for PL algorithm bring-up before MPU is wired.

module fpga_sensor_sim #(
    parameter integer CLK_HZ  = 50_000_000,
    parameter integer SAMPLE_HZ = 1_000
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        enable,
    input  wire [1:0]  fault_mode,
    output reg         sample_valid,
    output reg signed [15:0] sample_data
);

    localparam integer SAMPLE_DIV = CLK_HZ / SAMPLE_HZ;

    reg [31:0] sample_div;
    reg [5:0]  phase;
    reg [15:0] lfsr;
    reg [7:0]  impulse_count;

    reg signed [15:0] sine_sample;

    always @(*) begin
        case (phase[5:0])
            6'd0:  sine_sample = 16'sd0;
            6'd1:  sine_sample = 16'sd392;
            6'd2:  sine_sample = 16'sd784;
            6'd3:  sine_sample = 16'sd1176;
            6'd4:  sine_sample = 16'sd1568;
            6'd5:  sine_sample = 16'sd1960;
            6'd6:  sine_sample = 16'sd2352;
            6'd7:  sine_sample = 16'sd2744;
            6'd8:  sine_sample = 16'sd3135;
            6'd9:  sine_sample = 16'sd3527;
            6'd10: sine_sample = 16'sd3919;
            6'd11: sine_sample = 16'sd4311;
            6'd12: sine_sample = 16'sd4703;
            6'd13: sine_sample = 16'sd5095;
            6'd14: sine_sample = 16'sd5487;
            6'd15: sine_sample = 16'sd5879;
            6'd16: sine_sample = 16'sd6271;
            6'd17: sine_sample = 16'sd6663;
            6'd18: sine_sample = 16'sd7055;
            6'd19: sine_sample = 16'sd7447;
            6'd20: sine_sample = 16'sd7839;
            6'd21: sine_sample = 16'sd8231;
            6'd22: sine_sample = 16'sd8623;
            6'd23: sine_sample = 16'sd9015;
            6'd24: sine_sample = 16'sd9407;
            6'd25: sine_sample = 16'sd9799;
            6'd26: sine_sample = 16'sd10191;
            6'd27: sine_sample = 16'sd10583;
            6'd28: sine_sample = 16'sd10975;
            6'd29: sine_sample = 16'sd11367;
            6'd30: sine_sample = 16'sd11759;
            6'd31: sine_sample = 16'sd12151;
            6'd32: sine_sample = 16'sd12543;
            6'd33: sine_sample = 16'sd12935;
            6'd34: sine_sample = 16'sd13327;
            6'd35: sine_sample = 16'sd13719;
            6'd36: sine_sample = 16'sd14111;
            6'd37: sine_sample = 16'sd14503;
            6'd38: sine_sample = 16'sd14895;
            6'd39: sine_sample = 16'sd15287;
            6'd40: sine_sample = 16'sd15679;
            6'd41: sine_sample = 16'sd16071;
            6'd42: sine_sample = 16'sd16463;
            6'd43: sine_sample = 16'sd16855;
            6'd44: sine_sample = 16'sd17247;
            6'd45: sine_sample = 16'sd17639;
            6'd46: sine_sample = 16'sd18031;
            6'd47: sine_sample = 16'sd18423;
            6'd48: sine_sample = 16'sd18815;
            6'd49: sine_sample = 16'sd19207;
            6'd50: sine_sample = 16'sd19599;
            6'd51: sine_sample = 16'sd19991;
            6'd52: sine_sample = 16'sd20383;
            6'd53: sine_sample = 16'sd20775;
            6'd54: sine_sample = 16'sd21167;
            6'd55: sine_sample = 16'sd21559;
            6'd56: sine_sample = 16'sd21951;
            6'd57: sine_sample = 16'sd22343;
            6'd58: sine_sample = 16'sd22735;
            6'd59: sine_sample = 16'sd23127;
            6'd60: sine_sample = 16'sd23519;
            6'd61: sine_sample = 16'sd23911;
            6'd62: sine_sample = 16'sd24303;
            default: sine_sample = 16'sd24695;
        endcase
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sample_div    <= 32'd0;
            phase         <= 6'd0;
            lfsr          <= 16'hACE1;
            impulse_count <= 8'd0;
            sample_valid  <= 1'b0;
            sample_data   <= 16'sd0;
        end else begin
            sample_valid <= 1'b0;
            if (!enable) begin
                sample_div <= 32'd0;
            end else if (sample_div == SAMPLE_DIV - 1) begin
                sample_div <= 32'd0;
                sample_valid <= 1'b1;
                phase <= phase + 6'd1;
                impulse_count <= impulse_count + 1'b1;
                lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};

                case (fault_mode)
                    2'd0: sample_data <= (sine_sample >>> 2) + $signed({8'd0, lfsr[7:0]} >>> 3);
                    2'd1: sample_data <= (sine_sample >>> 2) +
                                         ((impulse_count[5:0] == 6'd0) ? 16'sd7000 : 16'sd0) +
                                         $signed({8'd0, lfsr[7:0]} >>> 3);
                    2'd2: sample_data <= (sine_sample >>> 1) +
                                         $signed({8'd0, lfsr[7:0]} >>> 2);
                    default: sample_data <= (sine_sample >>> 1) +
                                            ((impulse_count[4:0] == 5'd0) ? 16'sd9000 : 16'sd0);
                endcase
            end else begin
                sample_div <= sample_div + 1'b1;
            end
        end
    end

endmodule
