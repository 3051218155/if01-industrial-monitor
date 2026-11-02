`timescale 1ns / 1ps
// PS 可控 RS485 UART（9600 8N1，半双工 DE）
// 供 IF-01 寄存器 0x50~0x5C 驱动；引脚 L17/N16/N15。

module fpga_rs485_uart #(
    parameter integer CLK_HZ = 100_000_000,
    parameter integer BAUD   = 9600
) (
    input  wire       clk,
    input  wire       rst_n,
    input  wire       tx_pulse,
    input  wire [7:0] tx_byte,
    input  wire       rx_pop,
    output wire [7:0] rx_byte,
    output wire       rx_valid,
    output wire       rx_strobe,
    output wire [7:0] rx_strobe_byte,
    output wire       rx_overrun,
    output wire       tx_busy,
    output wire       de_out,
    output wire       tx_out,
    input  wire       rx_in,
    input  wire       ctrl_auto_de,
    input  wire       ctrl_de_force,
    input  wire       ctrl_rx_en,
    input  wire       ctrl_tx_en
);

    localparam integer BAUD_DIV = CLK_HZ / BAUD;
    localparam integer W        = $clog2(BAUD_DIV + 1);

    reg [W-1:0] tx_baud_cnt;
    reg [W-1:0] rx_baud_cnt;
    reg [3:0]   bit_idx;
    reg [7:0]   shifter;
    reg         tx_active;
    reg         tx_line;
    reg         de_r;

    reg [7:0]   rx_shifter;
    reg [3:0]   rx_bit_idx;
    reg         rx_active;
    reg [7:0]   rx_hold;
    reg         rx_valid_r;
    reg         rx_strobe_r;
    reg [7:0]   rx_strobe_byte_r;
    reg         rx_overrun_r;
    reg [1:0]   rx_sync;

    assign tx_busy    = tx_active;
    assign de_out     = de_r;
    assign tx_out     = tx_line;
    assign rx_byte    = rx_hold;
    assign rx_valid   = rx_valid_r;
    assign rx_strobe  = rx_strobe_r;
    assign rx_strobe_byte = rx_strobe_byte_r;
    assign rx_overrun = rx_overrun_r;

    wire tx_sample_tick = (tx_baud_cnt == BAUD_DIV - 1);
    wire rx_sample_tick = (rx_baud_cnt == BAUD_DIV - 1);
    // Synchronize the asynchronous RS485 receiver input before the UART
    // samples it. The board RS485 pins are asynchronous to the PL clock.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            rx_sync <= 2'b11;
        else
            rx_sync <= {rx_sync[0], rx_in};
    end

    wire rx_s = rx_sync[1];

    // ---------- TX ----------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_baud_cnt <= {W{1'b0}};
            bit_idx     <= 4'd0;
            shifter    <= 8'd0;
            tx_active  <= 1'b0;
            tx_line    <= 1'b1;
            de_r       <= 1'b0;
        end else begin
            if (!ctrl_tx_en) begin
                tx_active <= 1'b0;
                tx_line   <= 1'b1;
                de_r        <= ctrl_auto_de ? 1'b0 : ctrl_de_force;
                tx_baud_cnt <= {W{1'b0}};
            end else if (!tx_active) begin
                tx_line <= 1'b1;
                de_r    <= ctrl_auto_de ? 1'b0 : ctrl_de_force;
                if (tx_pulse) begin
                    tx_active <= 1'b1;
                    shifter   <= tx_byte;
                    bit_idx   <= 4'd0;
                    tx_line   <= 1'b0; // start bit
                    de_r      <= 1'b1;
                    tx_baud_cnt <= {W{1'b0}};
                end
            end else begin
                if (tx_sample_tick) begin
                    tx_baud_cnt <= {W{1'b0}};
                    if (bit_idx == 4'd9) begin
                        tx_active <= 1'b0;
                        tx_line   <= 1'b1;
                        de_r      <= ctrl_auto_de ? 1'b0 : ctrl_de_force;
                    end else begin
                        bit_idx <= bit_idx + 1'b1;
                        case (bit_idx)
                            4'd0, 4'd1, 4'd2, 4'd3,
                            4'd4, 4'd5, 4'd6, 4'd7: begin
                                tx_line <= shifter[0];
                                shifter <= {1'b0, shifter[7:1]};
                            end
                            default: tx_line <= 1'b1; // stop bit
                        endcase
                    end
                end else begin
                    tx_baud_cnt <= tx_baud_cnt + 1'b1;
                end
            end
        end
    end

    // ---------- RX ----------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rx_baud_cnt  <= {W{1'b0}};
            rx_bit_idx   <= 4'd0;
            rx_shifter   <= 8'd0;
            rx_active    <= 1'b0;
            rx_hold      <= 8'd0;
            rx_valid_r   <= 1'b0;
            rx_strobe_r  <= 1'b0;
            rx_strobe_byte_r <= 8'd0;
            rx_overrun_r <= 1'b0;
        end else begin
            rx_overrun_r <= 1'b0;
            rx_strobe_r  <= 1'b0;
            if (rx_pop)
                rx_valid_r <= 1'b0;

            if (!ctrl_rx_en || de_r) begin
                rx_active  <= 1'b0;
                rx_baud_cnt <= {W{1'b0}};
            end else if (!rx_active) begin
                if (!rx_s) begin
                    rx_active   <= 1'b1;
                    rx_bit_idx  <= 4'd0;
                    rx_baud_cnt <= BAUD_DIV / 2;
                end
            end else if (rx_sample_tick) begin
                rx_baud_cnt <= {W{1'b0}};
                if (rx_bit_idx == 4'd9) begin
                    rx_active <= 1'b0;
                end else begin
                    rx_bit_idx <= rx_bit_idx + 1'b1;
                    if (rx_bit_idx >= 4'd1 && rx_bit_idx <= 4'd8)
                        rx_shifter <= {rx_s, rx_shifter[7:1]};
                end
            end else begin
                rx_baud_cnt <= rx_baud_cnt + 1'b1;
            end

            if (rx_active && rx_bit_idx == 4'd9 && rx_sample_tick) begin
                rx_strobe_r <= 1'b1;
                rx_strobe_byte_r <= rx_shifter;
                if (rx_valid_r)
                    rx_overrun_r <= 1'b1;
                else begin
                    rx_hold    <= rx_shifter;
                    rx_valid_r <= 1'b1;
                end
            end
        end
    end

endmodule
