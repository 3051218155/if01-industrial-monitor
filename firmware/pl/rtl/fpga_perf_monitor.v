`timescale 1ns / 1ps
// IF-01 quantitative timing monitor.
//
// All times are CLK_HZ cycles. Control latency covers the interval from an
// AXI-Lite motor-register write to the corresponding register-value change
// observed by the PWM path. Sample latency covers sample_valid to the first
// AXI-Lite read of SENSOR_RAW after that sample.

module fpga_perf_monitor #(
    parameter integer CLK_HZ = 50_000_000,
    parameter [31:0] DEFAULT_SAMPLE_PERIOD  = 32'd100000,
    parameter [31:0] DEFAULT_SAMPLE_TIMEOUT = 32'd200000
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire [11:0] byte_addr,
    input  wire        wr_en,
    input  wire [31:0] wr_data,
    input  wire        rd_en,
    output reg  [31:0] rd_data,
    input  wire        control_write,
    input  wire [31:0] m1_ctrl,
    input  wire [31:0] m1_pwm,
    input  wire [31:0] m2_ctrl,
    input  wire [31:0] m2_pwm,
    input  wire        sample_valid,
    input  wire        sample_read
);

    reg        perf_enable;
    reg [31:0] sample_period_cycles;
    reg [31:0] sample_timeout_cycles;
    reg [63:0] timestamp;

    reg [31:0] m1_ctrl_d;
    reg [31:0] m1_pwm_d;
    reg [31:0] m2_ctrl_d;
    reg [31:0] m2_pwm_d;

    reg        control_pending;
    reg [31:0] control_start;
    reg [31:0] control_writes;
    reg [31:0] control_events;
    reg [31:0] control_last;
    reg [31:0] control_min;
    reg [31:0] control_max;
    reg [63:0] control_sum;
    reg [31:0] control_overflows;

    reg        have_sample;
    reg [31:0] last_sample_time;
    reg [31:0] sample_valid_count;
    reg [31:0] sample_interval_events;
    reg [31:0] sample_interval_last;
    reg [31:0] sample_interval_min;
    reg [31:0] sample_interval_max;
    reg [63:0] sample_interval_sum;
    reg [31:0] sample_timeouts;
    reg        last_interval_timeout;

    reg [31:0] sample_jitter_last;
    reg [31:0] sample_jitter_min;
    reg [31:0] sample_jitter_max;
    reg [63:0] sample_jitter_sum;

    reg        sample_pending;
    reg [31:0] sample_pending_start;
    reg [31:0] sample_pending_overwrites;
    reg [31:0] sample_read_events;
    reg [31:0] sample_read_last;
    reg [31:0] sample_read_min;
    reg [31:0] sample_read_max;
    reg [63:0] sample_read_sum;
    reg [31:0] sample_read_misses;
    reg [31:0] control_hist [0:31];
    reg [31:0] sample_interval_hist [0:31];
    reg [31:0] sample_read_hist [0:31];
    integer hist_index;

    wire clear_stats = wr_en && (byte_addr == 12'h100) && wr_data[1];
    wire motor_changed =
        (m1_ctrl != m1_ctrl_d) ||
        (m1_pwm  != m1_pwm_d)  ||
        (m2_ctrl != m2_ctrl_d) ||
        (m2_pwm  != m2_pwm_d);

    wire [31:0] interval_cycles = timestamp[31:0] - last_sample_time;
    wire [31:0] jitter_cycles =
        (interval_cycles >= sample_period_cycles)
        ? (interval_cycles - sample_period_cycles)
        : (sample_period_cycles - interval_cycles);
    wire [31:0] read_latency = timestamp[31:0] - sample_pending_start;
    wire [31:0] control_latency = timestamp[31:0] - control_start;

    function [5:0] log2_bin;
        input [31:0] value;
        begin
            if (value == 32'd0)
                log2_bin = 6'd0;
            else if (value == 32'd1)
                log2_bin = 6'd1;
            else if (value <= 32'd3)
                log2_bin = 6'd2;
            else if (value <= 32'd7)
                log2_bin = 6'd3;
            else if (value <= 32'd15)
                log2_bin = 6'd4;
            else if (value <= 32'd31)
                log2_bin = 6'd5;
            else if (value <= 32'd63)
                log2_bin = 6'd6;
            else if (value <= 32'd127)
                log2_bin = 6'd7;
            else if (value <= 32'd255)
                log2_bin = 6'd8;
            else if (value <= 32'd511)
                log2_bin = 6'd9;
            else if (value <= 32'd1023)
                log2_bin = 6'd10;
            else if (value <= 32'd2047)
                log2_bin = 6'd11;
            else if (value <= 32'd4095)
                log2_bin = 6'd12;
            else if (value <= 32'd8191)
                log2_bin = 6'd13;
            else if (value <= 32'd16383)
                log2_bin = 6'd14;
            else if (value <= 32'd32767)
                log2_bin = 6'd15;
            else if (value <= 32'd65535)
                log2_bin = 6'd16;
            else if (value <= 32'd131071)
                log2_bin = 6'd17;
            else if (value <= 32'd262143)
                log2_bin = 6'd18;
            else if (value <= 32'd524287)
                log2_bin = 6'd19;
            else if (value <= 32'd1048575)
                log2_bin = 6'd20;
            else if (value <= 32'd2097151)
                log2_bin = 6'd21;
            else if (value <= 32'd4194303)
                log2_bin = 6'd22;
            else if (value <= 32'd8388607)
                log2_bin = 6'd23;
            else if (value <= 32'd16777215)
                log2_bin = 6'd24;
            else if (value <= 32'd33554431)
                log2_bin = 6'd25;
            else if (value <= 32'd67108863)
                log2_bin = 6'd26;
            else if (value <= 32'd134217727)
                log2_bin = 6'd27;
            else if (value <= 32'd268435455)
                log2_bin = 6'd28;
            else if (value <= 32'd536870911)
                log2_bin = 6'd29;
            else if (value <= 32'd1073741823)
                log2_bin = 6'd30;
            else
                log2_bin = 6'd31;
        end
    endfunction

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            perf_enable           <= 1'b1;
            sample_period_cycles  <= DEFAULT_SAMPLE_PERIOD;
            sample_timeout_cycles <= DEFAULT_SAMPLE_TIMEOUT;
            timestamp             <= 64'd0;
            m1_ctrl_d             <= 32'd0;
            m1_pwm_d              <= 32'd0;
            m2_ctrl_d             <= 32'd0;
            m2_pwm_d              <= 32'd0;
            control_pending       <= 1'b0;
            control_start         <= 32'd0;
            control_writes        <= 32'd0;
            control_events        <= 32'd0;
            control_last          <= 32'd0;
            control_min           <= 32'hFFFF_FFFF;
            control_max           <= 32'd0;
            control_sum           <= 64'd0;
            control_overflows     <= 32'd0;
            have_sample           <= 1'b0;
            last_sample_time      <= 32'd0;
            sample_valid_count    <= 32'd0;
            sample_interval_events <= 32'd0;
            sample_interval_last  <= 32'd0;
            sample_interval_min   <= 32'hFFFF_FFFF;
            sample_interval_max   <= 32'd0;
            sample_interval_sum   <= 64'd0;
            sample_timeouts       <= 32'd0;
            last_interval_timeout <= 1'b0;
            sample_jitter_last    <= 32'd0;
            sample_jitter_min     <= 32'hFFFF_FFFF;
            sample_jitter_max     <= 32'd0;
            sample_jitter_sum     <= 64'd0;
            sample_pending        <= 1'b0;
            sample_pending_start  <= 32'd0;
            sample_pending_overwrites <= 32'd0;
            sample_read_events    <= 32'd0;
            sample_read_last      <= 32'd0;
            sample_read_min       <= 32'hFFFF_FFFF;
            sample_read_max       <= 32'd0;
            sample_read_sum       <= 64'd0;
            sample_read_misses    <= 32'd0;
            for (hist_index = 0; hist_index < 32; hist_index = hist_index + 1) begin
                control_hist[hist_index] <= 32'd0;
                sample_interval_hist[hist_index] <= 32'd0;
                sample_read_hist[hist_index] <= 32'd0;
            end
        end else begin
            timestamp <= timestamp + 1'b1;
            m1_ctrl_d <= m1_ctrl;
            m1_pwm_d  <= m1_pwm;
            m2_ctrl_d <= m2_ctrl;
            m2_pwm_d  <= m2_pwm;

            if (wr_en && (byte_addr == 12'h100))
                perf_enable <= wr_data[0];
            if (wr_en && (byte_addr == 12'h17C))
                sample_period_cycles <= wr_data;
            if (wr_en && (byte_addr == 12'h180))
                sample_timeout_cycles <= wr_data;

            if (clear_stats) begin
                control_pending           <= 1'b0;
                control_start             <= 32'd0;
                control_writes            <= 32'd0;
                control_events            <= 32'd0;
                control_last              <= 32'd0;
                control_min               <= 32'hFFFF_FFFF;
                control_max               <= 32'd0;
                control_sum               <= 64'd0;
                control_overflows         <= 32'd0;
                have_sample               <= 1'b0;
                last_sample_time          <= 32'd0;
                sample_valid_count        <= 32'd0;
                sample_interval_events    <= 32'd0;
                sample_interval_last      <= 32'd0;
                sample_interval_min       <= 32'hFFFF_FFFF;
                sample_interval_max       <= 32'd0;
                sample_interval_sum       <= 64'd0;
                sample_timeouts           <= 32'd0;
                last_interval_timeout     <= 1'b0;
                sample_jitter_last        <= 32'd0;
                sample_jitter_min         <= 32'hFFFF_FFFF;
                sample_jitter_max         <= 32'd0;
                sample_jitter_sum         <= 64'd0;
                sample_pending            <= 1'b0;
                sample_pending_start      <= 32'd0;
                sample_pending_overwrites <= 32'd0;
                sample_read_events        <= 32'd0;
                sample_read_last          <= 32'd0;
                sample_read_min           <= 32'hFFFF_FFFF;
                sample_read_max           <= 32'd0;
                sample_read_sum           <= 64'd0;
                sample_read_misses        <= 32'd0;
                for (hist_index = 0; hist_index < 32;
                     hist_index = hist_index + 1) begin
                    control_hist[hist_index] <= 32'd0;
                    sample_interval_hist[hist_index] <= 32'd0;
                    sample_read_hist[hist_index] <= 32'd0;
                end
            end else if (perf_enable) begin
                if (control_pending && motor_changed) begin
                    control_pending <= 1'b0;
                    control_events  <= control_events + 1'b1;
                    control_last    <= control_latency;
                    control_sum     <= control_sum + {32'd0, control_latency};
                    if (control_latency < control_min)
                        control_min <= control_latency;
                    if (control_latency > control_max)
                        control_max <= control_latency;
                    control_hist[log2_bin(control_latency)] <=
                        control_hist[log2_bin(control_latency)] + 1'b1;
                end else if (control_pending && control_write) begin
                    control_overflows <= control_overflows + 1'b1;
                end

                if (control_write) begin
                    control_pending <= 1'b1;
                    control_start   <= timestamp[31:0];
                    control_writes  <= control_writes + 1'b1;
                end

                if (sample_valid) begin
                    sample_valid_count <= sample_valid_count + 1'b1;
                    if (have_sample) begin
                        sample_interval_events <= sample_interval_events + 1'b1;
                        sample_interval_last   <= interval_cycles;
                        sample_interval_sum    <=
                            sample_interval_sum + {32'd0, interval_cycles};
                        if (interval_cycles < sample_interval_min)
                            sample_interval_min <= interval_cycles;
                        if (interval_cycles > sample_interval_max)
                            sample_interval_max <= interval_cycles;
                        sample_interval_hist[log2_bin(interval_cycles)] <=
                            sample_interval_hist[log2_bin(interval_cycles)] + 1'b1;

                        sample_jitter_last <= jitter_cycles;
                        sample_jitter_sum  <=
                            sample_jitter_sum + {32'd0, jitter_cycles};
                        if (jitter_cycles < sample_jitter_min)
                            sample_jitter_min <= jitter_cycles;
                        if (jitter_cycles > sample_jitter_max)
                            sample_jitter_max <= jitter_cycles;

                        last_interval_timeout <=
                            (interval_cycles > sample_timeout_cycles);
                        if (interval_cycles > sample_timeout_cycles)
                            sample_timeouts <= sample_timeouts + 1'b1;
                    end
                    have_sample      <= 1'b1;
                    last_sample_time <= timestamp[31:0];

                    if (sample_pending)
                        sample_pending_overwrites <=
                            sample_pending_overwrites + 1'b1;
                    sample_pending       <= 1'b1;
                    sample_pending_start <= timestamp[31:0];
                end

                if (sample_read) begin
                    if (sample_pending) begin
                        sample_pending     <= 1'b0;
                        sample_read_events <= sample_read_events + 1'b1;
                        sample_read_last   <= read_latency;
                        sample_read_sum    <=
                            sample_read_sum + {32'd0, read_latency};
                        if (read_latency < sample_read_min)
                            sample_read_min <= read_latency;
                        if (read_latency > sample_read_max)
                            sample_read_max <= read_latency;
                        sample_read_hist[log2_bin(read_latency)] <=
                            sample_read_hist[log2_bin(read_latency)] + 1'b1;
                    end else begin
                        sample_read_misses <= sample_read_misses + 1'b1;
                    end
                end
            end
        end
    end

    always @(*) begin
        rd_data = 32'd0;
        if (rd_en) begin
            case (byte_addr)
                12'h100: rd_data = {30'd0, perf_enable, 1'b0};
                12'h104: rd_data = {
                    28'd0,
                    last_interval_timeout,
                    sample_pending,
                    control_pending,
                    perf_enable
                };
                12'h108: rd_data = timestamp[31:0];
                12'h10C: rd_data = timestamp[63:32];
                12'h110: rd_data = control_writes;
                12'h114: rd_data = control_events;
                12'h118: rd_data = control_last;
                12'h11C: rd_data = control_min;
                12'h120: rd_data = control_max;
                12'h124: rd_data = control_sum[31:0];
                12'h128: rd_data = control_sum[63:32];
                12'h12C: rd_data = control_overflows;
                12'h130: rd_data = sample_interval_events;
                12'h134: rd_data = sample_interval_last;
                12'h138: rd_data = sample_interval_min;
                12'h13C: rd_data = sample_interval_max;
                12'h140: rd_data = sample_interval_sum[31:0];
                12'h144: rd_data = sample_interval_sum[63:32];
                12'h148: rd_data = sample_timeouts;
                12'h14C: rd_data = sample_pending_overwrites;
                12'h150: rd_data = sample_jitter_last;
                12'h154: rd_data = sample_jitter_min;
                12'h158: rd_data = sample_jitter_max;
                12'h15C: rd_data = sample_jitter_sum[31:0];
                12'h160: rd_data = sample_jitter_sum[63:32];
                12'h164: rd_data = sample_read_events;
                12'h168: rd_data = sample_read_last;
                12'h16C: rd_data = sample_read_min;
                12'h170: rd_data = sample_read_max;
                12'h174: rd_data = sample_read_sum[31:0];
                12'h178: rd_data = sample_read_sum[63:32];
                12'h17C: rd_data = sample_period_cycles;
                12'h180: rd_data = sample_timeout_cycles;
                12'h184: rd_data = CLK_HZ;
                12'h188: rd_data = sample_read_misses;
                12'h18C: rd_data = sample_valid_count;
                default: rd_data = 32'd0;
            endcase
            if ((byte_addr >= 12'h1A0) && (byte_addr <= 12'h21C))
                rd_data = control_hist[(byte_addr - 12'h1A0) >> 2];
            else if ((byte_addr >= 12'h220) && (byte_addr <= 12'h29C))
                rd_data = sample_read_hist[(byte_addr - 12'h220) >> 2];
            else if ((byte_addr >= 12'h2A0) && (byte_addr <= 12'h31C))
                rd_data = sample_interval_hist[(byte_addr - 12'h2A0) >> 2];
        end
    end

endmodule
