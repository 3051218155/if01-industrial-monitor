`timescale 1ns / 1ps
// 最小 AXI4-Lite Slave，挂 IF-01 寄存器（DR1M90 M_AXI_GP0 = 0x80000000）

module fpga_axi_lite_slave #(
    parameter integer CLK_HZ = 50_000_000
) (
    input  wire        s_axi_aclk,
    input  wire        s_axi_aresetn,
    input  wire [11:0] s_axi_awaddr,
    input  wire        s_axi_awvalid,
    output reg         s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output reg         s_axi_wready,
    output reg  [1:0]  s_axi_bresp,
    output reg         s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [11:0] s_axi_araddr,
    input  wire        s_axi_arvalid,
    output reg         s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output reg  [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid,
    input  wire        s_axi_rready,
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

    reg        pending_wr;
    reg [11:0] wr_addr;
    reg        pending_rd;
    reg [11:0] rd_addr;

    wire reg_wr = pending_wr && s_axi_wvalid;
    // Keep the read mux enabled while RVALID is asserted. Otherwise RDATA
    // collapses to zero on the cycle after pending_rd is cleared.
    wire reg_rd = pending_rd || s_axi_rvalid;

    fpga_if01_reg_map #(.CLK_HZ(CLK_HZ)) u_regs (
        .clk(s_axi_aclk),
        .rst_n(s_axi_aresetn),
        .addr(pending_wr ? wr_addr : rd_addr),
        .wr_en(reg_wr),
        .wr_data(s_axi_wdata),
        .rd_en(reg_rd),
        .rd_data(s_axi_rdata),
        .m1_ctrl(m1_ctrl),
        .m1_pwm(m1_pwm),
        .m2_ctrl(m2_ctrl),
        .m2_pwm(m2_pwm),
        .pl_ready(pl_ready),
        .rs485_de(rs485_de),
        .rs485_tx(rs485_tx),
        .rs485_rx(rs485_rx),
        .sensor_i2c_scl(sensor_i2c_scl),
        .sensor_i2c_sda(sensor_i2c_sda),
        .alert_buzzer(alert_buzzer),
        .alert_led_green(alert_led_green),
        .alert_led_yellow(alert_led_yellow),
        .alert_led_red(alert_led_red)
    );

    always @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
        if (!s_axi_aresetn) begin
            s_axi_awready <= 1'b0;
            s_axi_wready  <= 1'b0;
            s_axi_bvalid  <= 1'b0;
            s_axi_bresp   <= 2'b00;
            s_axi_arready <= 1'b0;
            s_axi_rvalid  <= 1'b0;
            s_axi_rresp   <= 2'b00;
            pending_wr    <= 1'b0;
            pending_rd    <= 1'b0;
            wr_addr       <= 12'd0;
            rd_addr       <= 12'd0;
        end else begin
            // Write address
            if (s_axi_awvalid && !pending_wr && !s_axi_bvalid) begin
                s_axi_awready <= 1'b1;
                wr_addr       <= s_axi_awaddr;
                pending_wr    <= 1'b1;
            end else begin
                s_axi_awready <= 1'b0;
            end

            // Write data
            if (pending_wr && s_axi_wvalid) begin
                s_axi_wready <= 1'b1;
                s_axi_bvalid <= 1'b1;
                s_axi_bresp  <= 2'b00;
                pending_wr   <= 1'b0;
            end else begin
                s_axi_wready <= 1'b0;
            end

            if (s_axi_bvalid && s_axi_bready) begin
                s_axi_bvalid <= 1'b0;
            end

            // Read
            if (s_axi_arvalid && !pending_rd && !s_axi_rvalid) begin
                s_axi_arready <= 1'b1;
                rd_addr       <= s_axi_araddr;
                pending_rd    <= 1'b1;
            end else begin
                s_axi_arready <= 1'b0;
            end

            if (pending_rd) begin
                s_axi_rvalid <= 1'b1;
                s_axi_rresp  <= 2'b00;
                pending_rd   <= 1'b0;
            end

            if (s_axi_rvalid && s_axi_rready) begin
                s_axi_rvalid <= 1'b0;
            end
        end
    end

endmodule
