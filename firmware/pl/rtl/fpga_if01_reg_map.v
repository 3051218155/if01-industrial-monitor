`timescale 1ns / 1ps

// IF-01 register map: motor, RS485 UART, hardware Modbus parser and sensor.



module fpga_if01_reg_map #(
    parameter integer CLK_HZ = 50_000_000
) (

    input  wire        clk,

    input  wire        rst_n,

    input  wire [11:0] addr,

    input  wire        wr_en,

    input  wire [31:0] wr_data,

    input  wire        rd_en,

    output reg  [31:0] rd_data,

    output wire [31:0] m1_ctrl,

    output wire [31:0] m1_pwm,

    output wire [31:0] m2_ctrl,

    output wire [31:0] m2_pwm,

    output wire        pl_ready,

    output wire        rs485_de,

    output wire        rs485_tx,

    input  wire        rs485_rx,

    output wire        sensor_i2c_scl,

    inout  wire        sensor_i2c_sda,

    output wire        alert_buzzer,

    output wire        alert_led_green,

    output wire        alert_led_yellow,

    output wire        alert_led_red

);



    localparam [31:0] VERSION        = 32'h0001_0002;

    localparam [31:0] SAMPLE_MAGIC   = 32'hDEAD_BEEF;



    localparam [31:0] CTRL_AUTO_DE = 32'd2;

    localparam [31:0] CTRL_RX_EN   = 32'd4;

    localparam [31:0] CTRL_TX_EN   = 32'd8;

    localparam [31:0] CTRL_DEFAULT = CTRL_AUTO_DE | CTRL_RX_EN | CTRL_TX_EN;

    // bit0 parser enable, bit1 master mode, [15:8] slave id,
    // [31:16] valid holding-register count.
    localparam [31:0] PARSER_CTRL_DEFAULT = 32'h000C_0101;

    // 上电默认：bit0=使能, bit1=1 -> 直接使用 MPU-6050 真实数据（不再走内部模拟波形）
    localparam [31:0] SENSOR_CTRL_DEFAULT = 32'h0000_0003;

    // Fan-at-100% baseline: normal RMS is below 3000.
    localparam [31:0] ALERT_LOW_DEFAULT  = 32'd3000;

    localparam [31:0] ALERT_HIGH_DEFAULT = 32'd4500;



    reg [31:0] ctrl_reg;

    reg [31:0] sample_rate;

    reg [31:0] channel_en;

    reg [31:0] m1_ctrl_r;

    reg [31:0] m1_pwm_r;

    reg [31:0] m2_ctrl_r;

    reg [31:0] m2_pwm_r;

    reg [31:0] rs485_ctrl_r;

    reg [31:0] sensor_ctrl_r;

    reg [31:0] alert_low_r;

    reg [31:0] alert_high_r;

    reg [31:0] parser_ctrl_r;

    reg [5:0]  frame_index_r;



    assign m1_ctrl  = m1_ctrl_r;

    assign m1_pwm   = m1_pwm_r;

    assign m2_ctrl  = m2_ctrl_r;

    assign m2_pwm   = m2_pwm_r;

    assign pl_ready = 1'b1;



    wire [11:0] byte_addr = {addr[11:2], 2'b00};



    wire       tx_pulse = wr_en && (byte_addr == 8'h50);

    wire [7:0] tx_byte  = wr_data[7:0];

    // AXI read data stays combinational while RVALID is asserted, so rd_en
    // may be high for more than one cycle. POP must only happen once.
    reg rd_en_d;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            rd_en_d <= 1'b0;
        else
            rd_en_d <= rd_en;
    end

    wire rd_pulse = rd_en && !rd_en_d;
    wire rx_pop   = rd_pulse && (byte_addr == 8'h54);



    wire [7:0] rx_byte;

    wire       rx_valid;

    wire       rx_strobe;

    wire [7:0] rx_strobe_byte;

    wire       rx_overrun;

    wire       tx_busy;

    wire       de_out;



    fpga_rs485_uart #(

        .CLK_HZ(CLK_HZ),

        .BAUD(9600)

    ) u_rs485 (

        .clk(clk),

        .rst_n(rst_n),

        .tx_pulse(tx_pulse),

        .tx_byte(tx_byte),

        .rx_pop(rx_pop),

        .rx_byte(rx_byte),

        .rx_valid(rx_valid),

        .rx_strobe(rx_strobe),

        .rx_strobe_byte(rx_strobe_byte),

        .rx_overrun(rx_overrun),

        .tx_busy(tx_busy),

        .de_out(de_out),

        .tx_out(rs485_tx),

        .rx_in(rs485_rx),

        .ctrl_auto_de(rs485_ctrl_r[1]),

        .ctrl_de_force(rs485_ctrl_r[0]),

        .ctrl_rx_en(rs485_ctrl_r[2]),

        .ctrl_tx_en(rs485_ctrl_r[3])

    );



    assign rs485_de = de_out;

    wire parser_clear = wr_en && (byte_addr == 8'hFC) && wr_data[1];
    wire frame_ack    = wr_en && (byte_addr == 8'hFC) && wr_data[0];

    wire [31:0] rx_frames;
    wire [31:0] rx_valid_frames;
    wire [31:0] rx_crc_errors;
    wire [31:0] rx_length_errors;
    wire [31:0] rx_address_errors;
    wire [31:0] rx_function_errors;
    wire [31:0] rx_register_errors;
    wire [31:0] rx_gap_errors;
    wire [31:0] rx_dropped_frames;
    wire [31:0] rx_last_header;
    wire [31:0] rx_last_start;
    wire [31:0] rx_last_count;
    wire [31:0] rx_last_crc;
    wire        rx_frame_ready;
    wire [7:0]  rx_ready_length;
    wire [7:0]  rx_ready_address;
    wire [7:0]  rx_ready_function;
    wire [31:0] rx_frame_word;

    wire [31:0] tx_frames;
    wire [31:0] tx_valid_frames;
    wire [31:0] tx_crc_errors;
    wire [31:0] tx_length_errors;
    wire [31:0] tx_address_errors;
    wire [31:0] tx_function_errors;
    wire [31:0] tx_register_errors;
    wire [31:0] tx_gap_errors;
    wire [31:0] tx_dropped_frames;
    wire [31:0] tx_last_header;
    wire [31:0] tx_last_start;
    wire [31:0] tx_last_count;
    wire [31:0] tx_last_crc;

    fpga_modbus_rtu_parser #(
        .CLK_HZ(CLK_HZ),
        .BAUD(9600),
        .CAPTURE_FRAME(1)
    ) u_modbus_rx_parser (
        .clk(clk),
        .rst_n(rst_n),
        .enable(parser_ctrl_r[0]),
        .byte_valid(rx_strobe),
        .byte_in(rx_strobe_byte),
        .cfg_slave_id(parser_ctrl_r[15:8]),
        .cfg_reg_count(parser_ctrl_r[31:16]),
        .is_response(parser_ctrl_r[1]),
        .clear_stats(parser_clear),
        .frame_ack(frame_ack),
        .frame_index(frame_index_r),
        .frames(rx_frames),
        .valid_frames(rx_valid_frames),
        .crc_errors(rx_crc_errors),
        .length_errors(rx_length_errors),
        .address_errors(rx_address_errors),
        .function_errors(rx_function_errors),
        .register_errors(rx_register_errors),
        .gap_errors(rx_gap_errors),
        .dropped_frames(rx_dropped_frames),
        .last_header(rx_last_header),
        .last_start(rx_last_start),
        .last_count(rx_last_count),
        .last_crc(rx_last_crc),
        .frame_ready(rx_frame_ready),
        .ready_length(rx_ready_length),
        .ready_address(rx_ready_address),
        .ready_function(rx_ready_function),
        .frame_word(rx_frame_word)
    );

    fpga_modbus_rtu_parser #(
        .CLK_HZ(CLK_HZ),
        .BAUD(9600),
        .CAPTURE_FRAME(0)
    ) u_modbus_tx_parser (
        .clk(clk),
        .rst_n(rst_n),
        .enable(parser_ctrl_r[0]),
        .byte_valid(tx_pulse),
        .byte_in(tx_byte),
        .cfg_slave_id(parser_ctrl_r[15:8]),
        .cfg_reg_count(parser_ctrl_r[31:16]),
        .is_response(~parser_ctrl_r[1]),
        .clear_stats(parser_clear),
        .frame_ack(1'b0),
        .frame_index(6'd0),
        .frames(tx_frames),
        .valid_frames(tx_valid_frames),
        .crc_errors(tx_crc_errors),
        .length_errors(tx_length_errors),
        .address_errors(tx_address_errors),
        .function_errors(tx_function_errors),
        .register_errors(tx_register_errors),
        .gap_errors(tx_gap_errors),
        .dropped_frames(tx_dropped_frames),
        .last_header(tx_last_header),
        .last_start(tx_last_start),
        .last_count(tx_last_count),
        .last_crc(tx_last_crc),
        .frame_ready(),
        .ready_length(),
        .ready_address(),
        .ready_function(),
        .frame_word()
    );

    wire [31:0] rx_frame_status = {
        rx_ready_function,
        rx_ready_address,
        rx_ready_length,
        6'd0,
        1'b0,
        rx_frame_ready
    };



    wire [31:0] rs485_status = {28'd0, de_out, rx_overrun, rx_valid, tx_busy};

    wire signed [15:0] sensor_raw;
    wire signed [15:0] sensor_ay;
    wire signed [15:0] sensor_az;
    wire signed [15:0] sensor_temperature;
    wire signed [15:0] sensor_gx;
    wire signed [15:0] sensor_gy;
    wire signed [15:0] sensor_gz;
    wire signed [15:0] sensor_filtered;
    wire [31:0] sensor_rms;
    wire [15:0] sensor_peak;
    wire [31:0] sensor_mean_abs;
    wire [15:0] sensor_zcr;
    wire [7:0]  sensor_whoami;
    wire        sensor_sample_valid;
    wire        sensor_feature_valid;
    wire        sensor_mpu_busy;
    wire        sensor_mpu_error;
    wire [1:0]  sensor_alert_level;

    fpga_sensor_core #(
        .CLK_HZ(CLK_HZ),
        .SAMPLE_HZ(1_000),
        .WINDOW_SIZE(256)
    ) u_sensor (
        .clk(clk),
        .rst_n(rst_n),
        .enable(sensor_ctrl_r[0]),
        .source_sel(sensor_ctrl_r[1]),
        .fault_mode(sensor_ctrl_r[3:2]),
        .threshold_low(alert_low_r),
        .threshold_high(alert_high_r),
        .i2c_scl(sensor_i2c_scl),
        .i2c_sda(sensor_i2c_sda),
        .raw_sample(sensor_raw),
        .accel_y(sensor_ay),
        .accel_z(sensor_az),
        .sensor_temperature(sensor_temperature),
        .gyro_x(sensor_gx),
        .gyro_y(sensor_gy),
        .gyro_z(sensor_gz),
        .filtered_sample(sensor_filtered),
        .feature_rms(sensor_rms),
        .feature_peak(sensor_peak),
        .feature_mean_abs(sensor_mean_abs),
        .feature_zcr(sensor_zcr),
        .mpu_whoami(sensor_whoami),
        .sample_valid(sensor_sample_valid),
        .feature_valid(sensor_feature_valid),
        .mpu_busy(sensor_mpu_busy),
        .mpu_error(sensor_mpu_error),
        .alert_level(sensor_alert_level),
        .alert_buzzer(alert_buzzer),
        .alert_led_green(alert_led_green),
        .alert_led_yellow(alert_led_yellow),
        .alert_led_red(alert_led_red)
    );

    wire [31:0] sensor_status = {
        26'd0,
        sensor_alert_level,
        sensor_mpu_error,
        sensor_mpu_busy,
        sensor_feature_valid,
        sensor_sample_valid
    };

    wire perf_control_write =
        wr_en &&
        ((byte_addr == 12'h040) ||
         (byte_addr == 12'h044) ||
         (byte_addr == 12'h048) ||
         (byte_addr == 12'h04C));

    wire perf_sample_read = rd_pulse && (byte_addr == 12'h068);
    wire [31:0] perf_rd_data;

    fpga_perf_monitor #(
        .CLK_HZ(CLK_HZ),
        .DEFAULT_SAMPLE_PERIOD(32'd100000),
        .DEFAULT_SAMPLE_TIMEOUT(32'd200000)
    ) u_perf (
        .clk(clk),
        .rst_n(rst_n),
        .byte_addr(byte_addr),
        .wr_en(wr_en),
        .wr_data(wr_data),
        .rd_en(rd_en),
        .rd_data(perf_rd_data),
        .control_write(perf_control_write),
        .m1_ctrl(m1_ctrl_r),
        .m1_pwm(m1_pwm_r),
        .m2_ctrl(m2_ctrl_r),
        .m2_pwm(m2_pwm_r),
        .sample_valid(sensor_sample_valid),
        .sample_read(perf_sample_read)
    );



    always @(posedge clk or negedge rst_n) begin

        if (!rst_n) begin

            ctrl_reg      <= 32'd0;

            sample_rate   <= 32'd0;

            channel_en    <= 32'd0;

            m1_ctrl_r     <= 32'd0;

            m1_pwm_r      <= 32'd0;

            m2_ctrl_r     <= 32'd0;

            m2_pwm_r      <= 32'd0;

            rs485_ctrl_r  <= CTRL_DEFAULT;

            sensor_ctrl_r <= SENSOR_CTRL_DEFAULT;

            alert_low_r  <= ALERT_LOW_DEFAULT;

            alert_high_r <= ALERT_HIGH_DEFAULT;

            parser_ctrl_r <= PARSER_CTRL_DEFAULT;

            frame_index_r <= 6'd0;

        end else if (wr_en) begin

            case (byte_addr)

                8'h00: ctrl_reg     <= wr_data;

                8'h08: sample_rate  <= wr_data;

                8'h0C: channel_en   <= wr_data;

                8'h40: m1_ctrl_r    <= wr_data;

                8'h44: m1_pwm_r     <= wr_data;

                8'h48: m2_ctrl_r    <= wr_data;

                8'h4C: m2_pwm_r     <= wr_data;

                8'h5C: rs485_ctrl_r <= wr_data & 32'h0000_000F;

                8'h60: sensor_ctrl_r <= wr_data & 32'h0000_000F;

                8'h80: alert_low_r  <= wr_data;

                8'h84: alert_high_r <= wr_data;

                8'h90: parser_ctrl_r <= {
                    wr_data[31:16],
                    wr_data[15:8],
                    6'd0,
                    wr_data[1:0]
                };

                8'hF8: frame_index_r <= wr_data[5:0];

                default: ;

            endcase

        end

    end



    wire [31:0] status_reg = {27'd0, pl_ready, 1'b0, 1'b0, 1'b0, 1'b0};



    always @(*) begin

        rd_data = 32'd0;

        if (rd_en) begin
            if (byte_addr >= 12'h100) begin
                case (byte_addr)
                    12'h320: rd_data = {{16{sensor_ay[15]}}, sensor_ay};
                    12'h324: rd_data = {{16{sensor_az[15]}}, sensor_az};
                    12'h328: rd_data = {{16{sensor_temperature[15]}}, sensor_temperature};
                    12'h32C: rd_data = {{16{sensor_gx[15]}}, sensor_gx};
                    12'h330: rd_data = {{16{sensor_gy[15]}}, sensor_gy};
                    12'h334: rd_data = {{16{sensor_gz[15]}}, sensor_gz};
                    default: rd_data = perf_rd_data;
                endcase
            end else begin
                case (byte_addr)

                8'h00: rd_data = ctrl_reg;

                8'h04: rd_data = status_reg;

                8'h08: rd_data = SAMPLE_MAGIC;

                8'h0C: rd_data = channel_en;

                8'h10: rd_data = 32'd0;

                8'h30: rd_data = VERSION;

                8'h40: rd_data = m1_ctrl_r;

                8'h44: rd_data = m1_pwm_r;

                8'h48: rd_data = m2_ctrl_r;

                8'h4C: rd_data = m2_pwm_r;

                8'h54: rd_data = {24'd0, rx_byte};

                8'h58: rd_data = rs485_status;

                8'h5C: rd_data = rs485_ctrl_r;

                8'h60: rd_data = sensor_ctrl_r;

                8'h64: rd_data = sensor_status;

                8'h68: rd_data = {{16{sensor_raw[15]}}, sensor_raw};

                8'h6C: rd_data = {{16{sensor_filtered[15]}}, sensor_filtered};

                8'h70: rd_data = sensor_rms;

                8'h74: rd_data = {16'd0, sensor_peak};

                8'h78: rd_data = sensor_mean_abs;

                8'h7C: rd_data = {16'd0, sensor_zcr};

                8'h80: rd_data = alert_low_r;

                8'h84: rd_data = alert_high_r;

                8'h88: rd_data = {24'd0, sensor_whoami};

                8'h90: rd_data = parser_ctrl_r;

                8'h94: rd_data = rx_frames;

                8'h98: rd_data = rx_valid_frames;

                8'h9C: rd_data = rx_crc_errors;

                8'hA0: rd_data = rx_length_errors;

                8'hA4: rd_data = rx_address_errors;

                8'hA8: rd_data = rx_function_errors;

                8'hAC: rd_data = rx_register_errors;

                8'hB0: rd_data = rx_gap_errors;

                8'hB4: rd_data = rx_dropped_frames;

                8'hB8: rd_data = tx_frames;

                8'hBC: rd_data = tx_valid_frames;

                8'hC0: rd_data = tx_crc_errors;

                8'hC4: rd_data = tx_length_errors;

                8'hC8: rd_data = tx_address_errors;

                8'hCC: rd_data = tx_function_errors;

                8'hD0: rd_data = tx_register_errors;

                8'hD4: rd_data = tx_gap_errors;

                8'hD8: rd_data = rx_last_header;

                8'hDC: rd_data = rx_last_start;

                8'hE0: rd_data = rx_last_count;

                8'hE4: rd_data = rx_last_crc;

                8'hE8: rd_data = tx_last_header;

                8'hEC: rd_data = tx_last_start;

                8'hF0: rd_data = tx_last_count;

                8'hF4: rd_data = tx_last_crc;

                8'hF8: rd_data = rx_frame_word;

                8'hFC: rd_data = rx_frame_status;

                default: rd_data = 32'd0;

                endcase
            end

        end

    end



endmodule
