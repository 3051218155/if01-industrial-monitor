`timescale 1ns / 1ps
// MPU-6050 reader built on the MiLianKe uii2c controller.
// Reads WHO_AM_I first, then periodically reads ACCEL_XOUT_H..GYRO_ZOUT_L.

module fpga_mpu6050_reader #(
    parameter integer CLK_HZ   = 50_000_000,
    parameter integer I2C_HZ   = 400_000,
    parameter integer PERIOD_US = 2_000
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        enable,
    output wire        i2c_scl,
    inout  wire        i2c_sda,
    output reg  [7:0]  whoami,
    output reg  signed [15:0] ax,
    output reg  signed [15:0] ay,
    output reg  signed [15:0] az,
    output reg  signed [15:0] temp,
    output reg  signed [15:0] gx,
    output reg  signed [15:0] gy,
    output reg  signed [15:0] gz,
    output reg         data_valid,
    output wire        busy,
    output wire        error
);

    localparam [7:0] DEV_ADDR = 8'hD0;   // 0x68 << 1 (7-bit address, write)
    localparam [7:0] REG_WHO  = 8'h75;   // WHO_AM_I
    localparam [7:0] REG_PWR  = 8'h6B;   // PWR_MGMT_1
    localparam [7:0] REG_ACC  = 8'h3B;   // ACCEL/XYZ + TEMP + GYRO/XYZ (14 bytes)
    localparam integer PERIOD_CYCLES = (CLK_HZ / 1_000_000) * PERIOD_US;

    localparam [1:0] ST_IDLE      = 2'd0;
    localparam [1:0] ST_WAIT_BUSY = 2'd1;
    localparam [1:0] ST_WAIT_DONE = 2'd2;

    localparam [1:0] STEP_WHOAMI  = 2'd0;
    localparam [1:0] STEP_WAKE    = 2'd1;
    localparam [1:0] STEP_ACCEL   = 2'd2;

    reg [1:0]  state;
    reg [1:0]  step;
    reg [31:0] timer;
    reg [31:0] wr_data;
    reg [7:0]  wr_cnt;
    reg [7:0]  rd_cnt;
    reg        iic_req;
    reg        whoami_ok;
    reg        device_error;

    wire        iic_busy;
    wire        iic_bus_error;
    wire [111:0] rd_data;

    assign busy  = (state != ST_IDLE);
    assign error = iic_bus_error || device_error;

    function automatic known_device_id;
        input [7:0] id;
        begin
            case (id)
                8'h68, 8'h69, 8'h70, 8'h71, 8'h72,
                8'h73, 8'h74, 8'h75, 8'h98: known_device_id = 1'b1;
                default: known_device_id = 1'b0;
            endcase
        end
    endfunction

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state        <= ST_IDLE;
            step         <= STEP_WHOAMI;
            timer        <= 32'd0;
            wr_data      <= 32'd0;
            wr_cnt       <= 8'd0;
            rd_cnt       <= 8'd0;
            iic_req      <= 1'b0;
            whoami_ok    <= 1'b0;
            device_error <= 1'b0;
            whoami       <= 8'd0;
            ax           <= 16'sd0;
            ay           <= 16'sd0;
            az           <= 16'sd0;
            temp         <= 16'sd0;
            gx           <= 16'sd0;
            gy           <= 16'sd0;
            gz           <= 16'sd0;
            data_valid   <= 1'b0;
        end else begin
            data_valid <= 1'b0;

            if (!enable) begin
                state     <= ST_IDLE;
                step      <= STEP_WHOAMI;
                timer     <= 32'd0;
                iic_req   <= 1'b0;
                whoami_ok <= 1'b0;
                device_error <= 1'b0;
            end else begin
                case (state)
                    ST_IDLE: begin
                        if (step != STEP_ACCEL) begin
                            // 初始化阶段（WHO_AM_I / 唤醒）不等周期，尽快推进
                            timer   <= 32'd0;
                            iic_req <= 1'b1;
                            if (step == STEP_WHOAMI) begin
                                wr_data <= {16'd0, REG_WHO, DEV_ADDR};
                                wr_cnt  <= 8'd2;
                                rd_cnt  <= 8'd1;
                            end else begin
                                // 写 PWR_MGMT_1 = 0x00 唤醒器件（否则加速度计不更新）
                                wr_data <= {8'd0, 8'h00, REG_PWR, DEV_ADDR};
                                wr_cnt  <= 8'd3;
                                rd_cnt  <= 8'd0;
                            end
                            state <= ST_WAIT_BUSY;
                        end else if (timer == PERIOD_CYCLES - 1) begin
                            timer   <= 32'd0;
                            iic_req <= 1'b1;
                            wr_data <= {16'd0, REG_ACC, DEV_ADDR};
                            wr_cnt  <= 8'd2;
                            rd_cnt  <= 8'd14;
                            state   <= ST_WAIT_BUSY;
                        end else begin
                            timer <= timer + 1'b1;
                        end
                    end

                    ST_WAIT_BUSY: begin
                        // iic_req 必须保持到 I2C 内核真正响应为止。
                        // uii2c 内部用分频后的 scl_clk 采样（400kHz -> 2.5us），
                        // 如果这里只给一个 50MHz 周期的脉冲，请求会被漏掉，
                        // FSM 就会永远卡在 ST_WAIT_BUSY（表现为 mpu_busy 常驻）。
                        if (iic_busy) begin
                            iic_req <= 1'b0;
                            state <= ST_WAIT_DONE;
                        end
                    end

                    ST_WAIT_DONE: begin
                        if (!iic_busy) begin
                            case (step)
                                STEP_WHOAMI: begin
                                    whoami <= rd_data[7:0];
                                    if (known_device_id(rd_data[7:0])) begin
                                        whoami_ok <= 1'b1;
                                        device_error <= 1'b0;
                                        step      <= STEP_WAKE;
                                    end else begin
                                        whoami_ok <= 1'b0;
                                        device_error <= 1'b1;
                                        step <= STEP_WHOAMI;
                                    end
                                end
                                STEP_WAKE: begin
                                    step <= STEP_ACCEL;
                                end
                                default: begin
                                    // 0x3B..0x48: AX AY AZ TEMP GX GY GZ
                                    ax   <= $signed({rd_data[7:0],   rd_data[15:8]});
                                    ay   <= $signed({rd_data[23:16], rd_data[31:24]});
                                    az   <= $signed({rd_data[39:32], rd_data[47:40]});
                                    temp <= $signed({rd_data[55:48], rd_data[63:56]});
                                    gx   <= $signed({rd_data[71:64], rd_data[79:72]});
                                    gy   <= $signed({rd_data[87:80], rd_data[95:88]});
                                    gz   <= $signed({rd_data[103:96], rd_data[111:104]});
                                    if (whoami_ok)
                                        data_valid <= 1'b1;
                                end
                            endcase
                            state <= ST_IDLE;
                        end
                    end

                    default: state <= ST_IDLE;
                endcase
            end
        end
    end

    uii2c_sc #(
        .WMEN_LEN(4),
        .RMEN_LEN(14),
        .CLK_DIV(CLK_HZ / I2C_HZ)
    ) u_i2c (
        .I_clk(clk),
        .I_rstn(rst_n),
        .O_iic_scl(i2c_scl),
        .IO_iic_sda(i2c_sda),
        .I_wr_data(wr_data),
        .I_wr_cnt(wr_cnt),
        .O_rd_data(rd_data),
        .I_rd_cnt(rd_cnt),
        .I_iic_req(iic_req),
        .I_iic_mode(1'b1),
        .O_iic_busy(iic_busy),
        .O_iic_bus_error(iic_bus_error)
    );

endmodule
