`timescale 1ns/1ps

module slb_control_polar_encoder #(
    parameter MAX_N = 1024,
    parameter MAX_E = 7936
) (
    input clk,
    input rst_n,
    input start_i,
    input [10:0] k_i,
    input [13:0] e0_i,
    input [13:0] e_i,
    input [1:0] rvid_i,
    input [MAX_N-1:0] c_bits_i,
    output reg done_o,
    output reg error_o,
    output reg [13:0] out_len_o,
    output reg [MAX_E-1:0] f_bits_o,
    output reg [10:0] n_len_o,
    output reg [3:0] n_exp_o,
    output reg [1:0] npc_o,
    output reg [1:0] npc_wm_o,
    output reg [13:0] m_o,
    output reg [13:0] k0_o
);

    reg [9:0] rel_q [0:1023];
    reg [5:0] iseq0 [0:61];
    reg [5:0] iseq1 [0:61];
    reg [5:0] iseq2 [0:61];

    reg [MAX_N-1:0] temp_frozen;
    reg [MAX_N-1:0] info_mask;
    reg [MAX_N-1:0] pc_mask;
    reg [MAX_N-1:0] excluded_mask;
    reg [MAX_N-1:0] candidate_mask;
    reg [MAX_N-1:0] u_vec;
    reg [MAX_N-1:0] d_vec;
    reg [MAX_E-1:0] e_vec;
    reg [MAX_E-1:0] btmp_vec;
    reg [MAX_E-1:0] btmp_valid;

    integer k_val;
    integer e0_val;
    integer e_val;
    integer npc;
    integer npc_wm;
    integer k_with_pc;
    integer n_exp;
    integer n_len;
    integer t_val;
    integer q2_count;
    integer m_val;
    integer k0_val;
    integer limit_val;
    integer used_rows;
    integer selected;
    integer q;
    integer i;
    integer j;
    integer base;
    integer offset;
    integer step;
    integer scan;
    integer bank;
    integer row;
    integer col;
    integer seq_id;
    integer inter_col;
    integer buf_idx;
    integer fid;
    integer k_idx;
    integer source_idx;
    integer qtilde_count;
    integer best_q;
    integer best_pop;
    integer current_pop;
    reg y0;
    reg y1;
    reg y2;
    reg y3;
    reg y4;
    reg yt;

    initial begin
        `include "slb_polar_tables.vh"
    end

    function integer ceil_div;
        input integer value;
        input integer divisor;
        begin
            if (value <= 0) begin
                ceil_div = 0;
            end else begin
                ceil_div = 1 + ((value - 1) / divisor);
            end
        end
    endfunction

    function integer ceil_div_signed;
        input integer numerator;
        input integer denominator;
        begin
            if (numerator >= 0) begin
                ceil_div_signed = (numerator + denominator - 1) / denominator;
            end else begin
                ceil_div_signed = numerator / denominator;
            end
        end
    endfunction

    function integer ceil_log2;
        input integer value;
        integer current;
        integer exp;
        begin
            current = 1;
            exp = 0;
            while (current < value) begin
                current = current << 1;
                exp = exp + 1;
            end
            ceil_log2 = exp;
        end
    endfunction

    function integer compute_t;
        input integer k;
        input integer kp;
        input integer e0;
        input integer n;
        integer numerator;
        integer denominator;
        begin
            if ((e0 >= n) || ((kp * 4) > (3 * e0))) begin
                compute_t = kp;
            end else if ((k * 16) <= (7 * e0)) begin
                if ((e0 * 8) < (5 * n)) begin
                    numerator = kp * ((176 * e0) - (86 * n));
                    denominator = 32 * n;
                    compute_t = ceil_div_signed(numerator, denominator);
                end else if ((e0 * 4) < (3 * n)) begin
                    numerator = kp * ((40 * e0) - n);
                    denominator = 32 * n;
                    compute_t = ceil_div_signed(numerator, denominator);
                end else begin
                    numerator = kp * ((3 * e0) + (5 * n));
                    denominator = 8 * n;
                    compute_t = ceil_div_signed(numerator, denominator);
                end
            end else begin
                if ((e0 * 16) < (9 * n)) begin
                    numerator = kp * ((9 * n) - (2 * e0));
                    denominator = 8 * n;
                    compute_t = ceil_div_signed(numerator, denominator);
                end else begin
                    numerator = kp * ((31 * n) + e0);
                    denominator = 32 * n;
                    compute_t = ceil_div_signed(numerator, denominator);
                end
            end
        end
    endfunction

    function integer popcount10;
        input integer value;
        integer tmp;
        integer count;
        begin
            tmp = value;
            count = 0;
            while (tmp != 0) begin
                count = count + (tmp & 1);
                tmp = tmp >> 1;
            end
            popcount10 = count;
        end
    endfunction

    function [5:0] get_iseq;
        input integer seq;
        input integer col_idx;
        begin
            if (seq == 0) begin
                get_iseq = iseq0[col_idx];
            end else if (seq == 1) begin
                get_iseq = iseq1[col_idx];
            end else begin
                get_iseq = iseq2[col_idx];
            end
        end
    endfunction

    task clear_outputs;
        begin
            error_o = 1'b0;
            out_len_o = 14'd0;
            f_bits_o = {MAX_E{1'b0}};
            n_len_o = 11'd0;
            n_exp_o = 4'd0;
            npc_o = 2'd0;
            npc_wm_o = 2'd0;
            m_o = 14'd0;
            k0_o = 14'd0;
        end
    endtask

    task encode_block;
        begin
            clear_outputs();

            k_val = k_i;
            e0_val = e0_i;
            e_val = e_i;

            if ((k_val <= 0) || (e0_val <= 0) || (e_val <= 0) || (e_val > MAX_E)) begin
                error_o = 1'b1;
            end else begin
                if ((k_val >= 18) && (k_val <= 25)) begin
                    npc = 3;
                    if ((e_val + 3) > (k_val + 192)) begin
                        npc_wm = 1;
                    end else begin
                        npc_wm = 0;
                    end
                end else begin
                    npc = 0;
                    npc_wm = 0;
                end

                k_with_pc = k_val + npc;
                n_exp = ceil_log2(e0_val);
                if (n_exp < 5) begin
                    n_exp = 5;
                end
                if (n_exp > 10) begin
                    n_exp = 10;
                end
                n_len = 1 << n_exp;

                npc_o = npc[1:0];
                npc_wm_o = npc_wm[1:0];
                n_exp_o = n_exp[3:0];
                n_len_o = n_len[10:0];

                if (k_with_pc > n_len) begin
                    error_o = 1'b1;
                end else begin
                    temp_frozen = {MAX_N{1'b0}};
                    info_mask = {MAX_N{1'b0}};
                    pc_mask = {MAX_N{1'b0}};
                    excluded_mask = {MAX_N{1'b0}};
                    candidate_mask = {MAX_N{1'b0}};
                    u_vec = {MAX_N{1'b0}};
                    d_vec = {MAX_N{1'b0}};
                    e_vec = {MAX_E{1'b0}};
                    btmp_vec = {MAX_E{1'b0}};
                    btmp_valid = {MAX_E{1'b0}};

                    t_val = compute_t(k_val, k_with_pc, e0_val, n_len);

                    if (e0_val < n_len) begin
                        if ((k_val * 16) <= (7 * e0_val)) begin
                            if (n_len <= 256) begin
                                limit_val = ceil_div((3 * n_len) - (2 * e0_val), 4);
                            end else begin
                                limit_val = ceil_div((7 * n_len) - (6 * e0_val), 8);
                            end
                            for (i = 0; i < MAX_N; i = i + 1) begin
                                if (i < limit_val) begin
                                    temp_frozen[i] = 1'b1;
                                end
                            end
                        end else begin
                            for (i = 0; i < MAX_N; i = i + 1) begin
                                if ((i >= e0_val) && (i < n_len)) begin
                                    temp_frozen[i] = 1'b1;
                                end
                            end
                            if (e_val > 128) begin
                                temp_frozen[n_len / 4] = 1'b1;
                                temp_frozen[n_len / 2] = 1'b1;
                            end
                        end
                    end

                    selected = 0;
                    for (scan = 0; scan < 1024; scan = scan + 1) begin
                        q = rel_q[1023 - scan];
                        if ((q < n_len) && !temp_frozen[q] && (selected < t_val)) begin
                            info_mask[q] = 1'b1;
                            selected = selected + 1;
                        end
                    end

                    excluded_mask = info_mask;
                    for (i = 0; i < MAX_N; i = i + 1) begin
                        if (i < (n_len / 2)) begin
                            excluded_mask[i] = 1'b1;
                        end
                    end

                    q2_count = k_with_pc - t_val;
                    selected = 0;
                    for (scan = 0; scan < 1024; scan = scan + 1) begin
                        q = rel_q[1023 - scan];
                        if ((q < n_len) && !temp_frozen[q] && !excluded_mask[q] &&
                            (selected < q2_count)) begin
                            info_mask[q] = 1'b1;
                            selected = selected + 1;
                        end
                    end

                    if (npc > 0) begin
                        selected = 0;
                        for (scan = 0; scan < 1024; scan = scan + 1) begin
                            q = rel_q[scan];
                            if ((q < n_len) && info_mask[q] && (selected < (npc - npc_wm))) begin
                                pc_mask[q] = 1'b1;
                                selected = selected + 1;
                            end
                        end

                        if (npc_wm > 0) begin
                            qtilde_count = k_with_pc - npc;
                            candidate_mask = {MAX_N{1'b0}};
                            selected = 0;
                            for (scan = 0; scan < 1024; scan = scan + 1) begin
                                q = rel_q[1023 - scan];
                                if ((q < n_len) && info_mask[q] && (selected < qtilde_count)) begin
                                    candidate_mask[q] = 1'b1;
                                    selected = selected + 1;
                                end
                            end

                            best_q = 0;
                            best_pop = 99;
                            for (scan = 0; scan < 1024; scan = scan + 1) begin
                                q = rel_q[scan];
                                if ((q < n_len) && candidate_mask[q]) begin
                                    current_pop = popcount10(q);
                                    if (current_pop <= best_pop) begin
                                        best_pop = current_pop;
                                        best_q = q;
                                    end
                                end
                            end
                            pc_mask[best_q] = 1'b1;
                        end
                    end

                    k_idx = 0;
                    if (npc == 0) begin
                        for (i = 0; i < MAX_N; i = i + 1) begin
                            if (i < n_len) begin
                                if (info_mask[i]) begin
                                    u_vec[i] = c_bits_i[k_idx];
                                    k_idx = k_idx + 1;
                                end else begin
                                    u_vec[i] = 1'b0;
                                end
                            end
                        end
                    end else begin
                        y0 = 1'b0;
                        y1 = 1'b0;
                        y2 = 1'b0;
                        y3 = 1'b0;
                        y4 = 1'b0;
                        for (i = 0; i < MAX_N; i = i + 1) begin
                            if (i < n_len) begin
                                yt = y0;
                                y0 = y1;
                                y1 = y2;
                                y2 = y3;
                                y3 = y4;
                                y4 = yt;
                                if (info_mask[i]) begin
                                    if (pc_mask[i]) begin
                                        u_vec[i] = y0;
                                    end else begin
                                        u_vec[i] = c_bits_i[k_idx];
                                        k_idx = k_idx + 1;
                                        y0 = y0 ^ u_vec[i];
                                    end
                                end else begin
                                    u_vec[i] = 1'b0;
                                end
                            end
                        end
                    end

                    d_vec = u_vec;
                    step = 1;
                    while (step < n_len) begin
                        for (base = 0; base < MAX_N; base = base + 1) begin
                            if ((base < n_len) && ((base % (step * 2)) == 0)) begin
                                for (offset = 0; offset < MAX_N; offset = offset + 1) begin
                                    if (offset < step) begin
                                        d_vec[base + offset] =
                                            d_vec[base + offset] ^ d_vec[base + offset + step];
                                    end
                                end
                            end
                        end
                        step = step * 2;
                    end

                    if ((k_val * 16) > (7 * e0_val)) begin
                        if (e0_val < n_len) begin
                            m_val = e0_val;
                        end else begin
                            m_val = n_len;
                        end
                    end else begin
                        m_val = n_len;
                    end

                    if (rvid_i == 2'd0) begin
                        k0_val = 0;
                    end else if (rvid_i == 2'd1) begin
                        k0_val = (m_val / (4 * 32)) * 32;
                    end else if (rvid_i == 2'd2) begin
                        k0_val = (m_val / (2 * 32)) * 32;
                    end else begin
                        k0_val = ((3 * m_val) / (4 * 32)) * 32;
                    end
                    m_o = m_val[13:0];
                    k0_o = k0_val[13:0];

                    for (i = 0; i < MAX_E; i = i + 1) begin
                        if (i < e_val) begin
                            source_idx = (k0_val + i) % m_val;
                            e_vec[i] = d_vec[source_idx];
                        end
                    end

                    for (i = 0; i < MAX_E; i = i + 1) begin
                        if (i < e_val) begin
                            row = i / 62;
                            col = i % 62;
                            seq_id = row % 3;
                            inter_col = get_iseq(seq_id, col);
                            buf_idx = (row * 62) + inter_col;
                            btmp_vec[buf_idx] = e_vec[i];
                            btmp_valid[buf_idx] = 1'b1;
                        end
                    end

                    fid = 0;
                    used_rows = ceil_div(e_val, 62);
                    for (bank = 0; bank < 2; bank = bank + 1) begin
                        for (row = 0; row < 128; row = row + 1) begin
                            if (row < used_rows) begin
                                for (offset = 0; offset < 31; offset = offset + 1) begin
                                    buf_idx = ((row * 2) + bank) * 31 + offset;
                                    if (btmp_valid[buf_idx]) begin
                                        f_bits_o[fid] = btmp_vec[buf_idx];
                                        fid = fid + 1;
                                    end
                                end
                            end
                        end
                    end
                    out_len_o = e_val[13:0];
                end
            end
        end
    endtask

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done_o <= 1'b0;
            error_o <= 1'b0;
            out_len_o <= 14'd0;
            f_bits_o <= {MAX_E{1'b0}};
            n_len_o <= 11'd0;
            n_exp_o <= 4'd0;
            npc_o <= 2'd0;
            npc_wm_o <= 2'd0;
            m_o <= 14'd0;
            k0_o <= 14'd0;
        end else begin
            done_o <= 1'b0;
            if (start_i) begin
                encode_block();
                done_o <= 1'b1;
            end
        end
    end

endmodule
