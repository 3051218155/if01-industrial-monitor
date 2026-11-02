`timescale 1ns / 1ps
// Modbus RTU frame validator with optional frame capture for PS delivery.

module fpga_modbus_rtu_parser #(
    parameter integer CLK_HZ         = 50_000_000,
    parameter integer BAUD           = 9600,
    parameter integer CAPTURE_FRAME  = 1
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        enable,
    input  wire        byte_valid,
    input  wire [7:0]  byte_in,
    input  wire [7:0]  cfg_slave_id,
    input  wire [15:0] cfg_reg_count,
    input  wire        is_response,
    input  wire        clear_stats,
    input  wire        frame_ack,
    input  wire [5:0]  frame_index,
    output reg  [31:0] frames,
    output reg  [31:0] valid_frames,
    output reg  [31:0] crc_errors,
    output reg  [31:0] length_errors,
    output reg  [31:0] address_errors,
    output reg  [31:0] function_errors,
    output reg  [31:0] register_errors,
    output reg  [31:0] gap_errors,
    output reg  [31:0] dropped_frames,
    output reg  [31:0] last_header,
    output reg  [31:0] last_start,
    output reg  [31:0] last_count,
    output reg  [31:0] last_crc,
    output reg         frame_ready,
    output reg  [7:0]  ready_length,
    output reg  [7:0]  ready_address,
    output reg  [7:0]  ready_function,
    output wire [31:0] frame_word
);

    localparam integer BAUD_DIV         = CLK_HZ / BAUD;
    localparam integer INTER_CHAR_CYCLE = BAUD_DIV * 15;
    localparam integer FRAME_GAP_CYCLE  = BAUD_DIV * 35;

    reg [7:0]  frame_buf [0:255];
    reg [7:0]  byte_count;
    reg [15:0] crc_calc;
    reg [7:0]  last_byte;
    reg [7:0]  previous_byte;
    reg [31:0] idle_cycles;
    reg        overflow;
    reg        blocked;

    reg        addr_ok;
    reg        function_ok;
    reg        length_ok;
    reg        register_ok;
    reg        crc_ok;
    reg        frame_ok;
    reg [7:0]  error_flags;
    reg [15:0] field_start;
    reg [15:0] field_count;
    reg [16:0] field_end;
    reg [15:0] crc_received;
    reg [15:0] response_bytes;

    wire [7:0] frame_byte_base = {frame_index, 2'b00};
    assign frame_word = {
        frame_buf[frame_byte_base + 8'd3],
        frame_buf[frame_byte_base + 8'd2],
        frame_buf[frame_byte_base + 8'd1],
        frame_buf[frame_byte_base]
    };

    function [15:0] crc16_update;
        input [15:0] crc_in;
        input [7:0]  data_in;
        integer bit_index;
        reg [15:0] work;
        begin
            work = crc_in ^ {8'd0, data_in};
            for (bit_index = 0; bit_index < 8; bit_index = bit_index + 1) begin
                if (work[0])
                    work = (work >> 1) ^ 16'hA001;
                else
                    work = work >> 1;
            end
            crc16_update = work;
        end
    endfunction

    always @(*) begin
        field_start = {frame_buf[2], frame_buf[3]};
        field_count = {frame_buf[4], frame_buf[5]};
        field_end   = {1'b0, field_start} + {1'b0, field_count};
        crc_received = {last_byte, previous_byte};
        response_bytes = 16'd0;

        addr_ok     = (byte_count >= 8'd1) &&
                      ((cfg_slave_id == 8'd0)
                           ? ((frame_buf[0] >= 8'd1) &&
                              (frame_buf[0] <= 8'd6))
                           : (frame_buf[0] == cfg_slave_id));
        function_ok = 1'b0;
        length_ok   = 1'b0;
        register_ok = 1'b1;
        crc_ok      = (byte_count >= 8'd4) && (crc_calc == crc_received);

        if (!is_response) begin
            length_ok = (byte_count == 8'd8);
            if ((frame_buf[1] == 8'h03) || (frame_buf[1] == 8'h06)) begin
                function_ok = 1'b1;
                register_ok = (field_start < cfg_reg_count);
                if (frame_buf[1] == 8'h03) begin
                    register_ok = register_ok &&
                                  (field_count > 16'd0) &&
                                  (field_count <= 16'd125) &&
                                  (field_end <= cfg_reg_count);
                end
            end
        end else begin
            response_bytes = {8'd0, frame_buf[2]};
            if (frame_buf[1][7]) begin
                length_ok = (byte_count == 8'd5);
                function_ok = (frame_buf[1] == 8'h83) ||
                              (frame_buf[1] == 8'h86);
                register_ok = function_ok;
            end else if (frame_buf[1] == 8'h03) begin
                length_ok = (byte_count >= 8'd7) &&
                            ({8'd0, byte_count} ==
                             (16'd5 + response_bytes)) &&
                            (response_bytes != 16'd0) &&
                            (response_bytes[0] == 1'b0);
                function_ok = 1'b1;
                register_ok = 1'b1;
            end else if (frame_buf[1] == 8'h06) begin
                length_ok = (byte_count == 8'd8);
                function_ok = 1'b1;
                register_ok = (field_start < cfg_reg_count);
            end
        end

        frame_ok = enable &&
                   !overflow &&
                   crc_ok &&
                   addr_ok &&
                   function_ok &&
                   length_ok &&
                   register_ok;
        error_flags = {
            2'b00,
            frame_ok,
            register_ok,
            function_ok,
            addr_ok,
            length_ok,
            crc_ok
        };
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            byte_count      <= 8'd0;
            crc_calc        <= 16'hFFFF;
            last_byte       <= 8'd0;
            previous_byte   <= 8'd0;
            idle_cycles     <= 32'd0;
            overflow        <= 1'b0;
            blocked         <= 1'b0;
            frames          <= 32'd0;
            valid_frames    <= 32'd0;
            crc_errors      <= 32'd0;
            length_errors   <= 32'd0;
            address_errors  <= 32'd0;
            function_errors <= 32'd0;
            register_errors <= 32'd0;
            gap_errors      <= 32'd0;
            dropped_frames  <= 32'd0;
            last_header     <= 32'd0;
            last_start      <= 32'd0;
            last_count      <= 32'd0;
            last_crc        <= 32'd0;
            frame_ready     <= 1'b0;
            ready_length    <= 8'd0;
            ready_address   <= 8'd0;
            ready_function  <= 8'd0;
        end else begin
            if (clear_stats) begin
                frames          <= 32'd0;
                valid_frames    <= 32'd0;
                crc_errors      <= 32'd0;
                length_errors   <= 32'd0;
                address_errors  <= 32'd0;
                function_errors <= 32'd0;
                register_errors <= 32'd0;
                gap_errors      <= 32'd0;
                dropped_frames  <= 32'd0;
                last_header     <= 32'd0;
                last_start      <= 32'd0;
                last_count      <= 32'd0;
                last_crc        <= 32'd0;
            end

            if (frame_ack)
                frame_ready <= 1'b0;

            if (idle_cycles != 32'hFFFF_FFFF)
                idle_cycles <= idle_cycles + 1'b1;

            if (!enable) begin
                byte_count    <= 8'd0;
                crc_calc      <= 16'hFFFF;
                last_byte     <= 8'd0;
                previous_byte <= 8'd0;
                overflow      <= 1'b0;
                blocked       <= 1'b0;
            end else if (byte_valid) begin
                if (blocked) begin
                    idle_cycles <= 32'd0;
                end else if (CAPTURE_FRAME && frame_ready && !frame_ack) begin
                    if (byte_count == 8'd0) begin
                        dropped_frames <= dropped_frames + 1'b1;
                        blocked <= 1'b1;
                    end
                    idle_cycles <= 32'd0;
                end else if ((byte_count != 8'd0) &&
                             (idle_cycles > INTER_CHAR_CYCLE)) begin
                    gap_errors    <= gap_errors + 1'b1;
                    byte_count    <= 8'd1;
                    crc_calc      <= 16'hFFFF;
                    last_byte     <= byte_in;
                    previous_byte <= 8'd0;
                    overflow      <= 1'b0;
                    idle_cycles   <= 32'd0;
                    if (CAPTURE_FRAME)
                        frame_buf[0] <= byte_in;
                end else begin
                    if (!overflow) begin
                        if (CAPTURE_FRAME || (byte_count < 8'd8))
                            frame_buf[byte_count] <= byte_in;
                        if (byte_count == 8'd255) begin
                            overflow <= 1'b1;
                        end else begin
                            byte_count <= byte_count + 1'b1;
                        end
                        if (byte_count >= 8'd2)
                            crc_calc <= crc16_update(crc_calc, previous_byte);
                        previous_byte <= last_byte;
                        last_byte <= byte_in;
                    end
                    idle_cycles <= 32'd0;
                end
            end else if (blocked && (idle_cycles >= FRAME_GAP_CYCLE)) begin
                blocked <= 1'b0;
            end else if ((byte_count != 8'd0) &&
                         (idle_cycles >= FRAME_GAP_CYCLE)) begin
                frames <= frames + 1'b1;
                last_header <= {
                    error_flags,
                    byte_count,
                    frame_buf[1],
                    frame_buf[0]
                };
                last_start <= {16'd0, field_start};
                last_count <= {16'd0, field_count};
                last_crc   <= {crc_calc, crc_received};

                if (frame_ok)
                    valid_frames <= valid_frames + 1'b1;
                if (!crc_ok)
                    crc_errors <= crc_errors + 1'b1;
                if (!length_ok)
                    length_errors <= length_errors + 1'b1;
                if (!addr_ok)
                    address_errors <= address_errors + 1'b1;
                if (!function_ok)
                    function_errors <= function_errors + 1'b1;
                if (!register_ok)
                    register_errors <= register_errors + 1'b1;

                if (CAPTURE_FRAME && frame_ok && !frame_ready) begin
                    frame_ready    <= 1'b1;
                    ready_length   <= byte_count;
                    ready_address  <= frame_buf[0];
                    ready_function <= frame_buf[1];
                end else if (CAPTURE_FRAME && frame_ok && frame_ready) begin
                    dropped_frames <= dropped_frames + 1'b1;
                end

                byte_count    <= 8'd0;
                crc_calc      <= 16'hFFFF;
                last_byte     <= 8'd0;
                previous_byte <= 8'd0;
                overflow      <= 1'b0;
                idle_cycles   <= 32'd0;
            end
        end
    end

endmodule
