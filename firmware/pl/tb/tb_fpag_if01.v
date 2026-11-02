`timescale 1ns / 1ps

module tb_fpag_if01;
    reg clk = 0;
    reg rst_n = 0;
    always #10 clk = ~clk;

    reg [11:0] awaddr;
    reg awvalid;
    wire awready;
    reg [31:0] wdata;
    reg [3:0] wstrb;
    reg wvalid;
    wire wready;
    wire [1:0] bresp;
    wire bvalid;
    reg bready;
    reg [11:0] araddr;
    reg arvalid;
    wire arready;
    wire [31:0] rdata;
    wire [1:0] rresp;
    wire rvalid;
    reg rready;
    wire m1_pwm, m1_dir, m2_pwm, m2_dir;
    wire rs485_tx, rs485_de;
    reg  rs485_rx = 1'b1;
    wire sensor_i2c_scl;
    wire sensor_i2c_sda = 1'b1;
    wire alert_buzzer, alert_led_green, alert_led_yellow, alert_led_red;

    fpag_if01_top dut (
        .pl_clk(clk), .pl_resetn(rst_n),
        .s_axi_awaddr(awaddr), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .motor1_pwm(m1_pwm), .motor1_dir(m1_dir), .motor2_pwm(m2_pwm), .motor2_dir(m2_dir),
        .rs485_tx(rs485_tx), .rs485_rx(rs485_rx), .rs485_de(rs485_de),
        .sensor_i2c_scl(sensor_i2c_scl), .sensor_i2c_sda(sensor_i2c_sda),
        .alert_buzzer(alert_buzzer),
        .alert_led_green(alert_led_green),
        .alert_led_yellow(alert_led_yellow),
        .alert_led_red(alert_led_red)
    );

    task axi_write(input [11:0] addr, input [31:0] data);
        begin
            @(posedge clk);
            awaddr = addr; awvalid = 1; wdata = data; wstrb = 4'hF; wvalid = 1; bready = 1;
            wait (awready && wready);
            @(posedge clk);
            awvalid = 0; wvalid = 0;
            wait (bvalid);
            @(posedge clk);
            bready = 0;
        end
    endtask

    task axi_read(input [11:0] addr, output [31:0] data);
        begin
            @(posedge clk);
            araddr = addr; arvalid = 1; rready = 1;
            wait (arready);
            @(posedge clk);
            arvalid = 0;
            wait (rvalid);
            data = rdata;
            @(posedge clk);
            rready = 0;
        end
    endtask

    task send_rs485_byte(input [7:0] data);
        integer i;
        begin
            rs485_rx = 1'b0;
            #104167;
            for (i = 0; i < 8; i = i + 1) begin
                rs485_rx = data[i];
                #104167;
            end
            rs485_rx = 1'b1;
            #104167;
        end
    endtask

    reg [31:0] rd;
    initial begin
        #100 rst_n = 1;
        axi_read(12'h008, rd);
        if (rd !== 32'hDEAD_BEEF) $fatal(1, "bad magic %h", rd);
        axi_read(12'h030, rd);
        if (rd !== 32'h0001_0001) $fatal(1, "bad version %h", rd);
        axi_write(12'h040, 32'h1);
        axi_write(12'h044, 32'd500);
        #500000;

        // Verify TX/DE and inject an external RS485 byte on RX.
        axi_write(12'h05C, 32'h0000_000E); // AUTO_DE | RX_EN | TX_EN
        axi_write(12'h050, 32'h0000_0055);
        #2000;
        axi_read(12'h058, rd);
        if (!rd[0] || !rd[3]) $fatal(1, "RS485 TX/DE not active %h", rd);
        begin : wait_rs485_tx
            integer i;
            for (i = 0; i < 20000; i = i + 1) begin
                axi_read(12'h058, rd);
                if (!rd[0]) disable wait_rs485_tx;
                #1000;
            end
            $fatal(1, "RS485 TX timeout");
        end

        send_rs485_byte(8'hA5);
        begin : wait_rs485_rx
            integer i;
            for (i = 0; i < 20000; i = i + 1) begin
                axi_read(12'h058, rd);
                if (rd[1]) disable wait_rs485_rx;
                #1000;
            end
            $fatal(1, "RS485 RX timeout");
        end
        axi_read(12'h054, rd);
        if (rd[7:0] !== 8'hA5) $fatal(1, "bad RS485 RX byte %h", rd);

        $display("TB PASS");
        $finish;
    end
endmodule
