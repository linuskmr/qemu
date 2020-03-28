/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020 Alex Richardson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory (Department of Computer Science and
 * Technology) under DARPA contract HR0011-18-C-0016 ("ECATS"), as part of the
 * DARPA SSITH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#pragma once
#include "tcg/tcg.h"
#include "tcg/tcg-op.h"
#include "cheri-translate-utils-base.h"

#ifdef TARGET_CHERI
#define _gen_cap_check(type)                                                   \
    static inline void generate_cap_##type##_check(                            \
        TCGv_cap_checked_ptr resultaddr, uint32_t capreg, TCGv offset,         \
        MemOp op)                                                              \
    {                                                                          \
        TCGv_i32 tcs = tcg_const_i32(capreg);                                  \
        TCGv_i32 tsize = tcg_const_i32(memop_size(op));                        \
        gen_helper_cap_##type##_check(resultaddr, cpu_env, tcs, offset,        \
                                      tsize);                                  \
        tcg_temp_free_i32(tsize);                                              \
        tcg_temp_free_i32(tcs);                                                \
    }                                                                          \
    static inline void generate_cap_##type##_check_imm(                        \
        TCGv_cap_checked_ptr resultaddr, uint32_t capreg, target_long offset,  \
        MemOp op)                                                              \
    {                                                                          \
        TCGv toffset = tcg_const_tl(offset);                                   \
        generate_cap_##type##_check(resultaddr, capreg, toffset, op);          \
        tcg_temp_free(toffset);                                                \
    }

_gen_cap_check(load)
_gen_cap_check(store)
_gen_cap_check(rmw)

#ifdef TARGET_MIPS
static inline void gen_load_gpr(TCGv t, int reg);
#endif

static inline void generate_get_ddc_checked_gpr_plus_offset(
    TCGv_cap_checked_ptr addr, int reg_num, target_long offset, MemOp mop,
    void (*check_ddc)(TCGv_cap_checked_ptr, TCGv_ptr, TCGv, TCGv_i32))
{
#if defined(TARGET_RISCV)
    gen_get_gpr((TCGv)addr, reg_num);
#elif defined(TARGET_MIPS)
    gen_load_gpr((TCGv)addr, reg_num);
#else
#error "Don't know how to fetch a GPR value"
#endif
    if (!__builtin_constant_p(offset) || offset != 0) {
        tcg_gen_addi_tl((TCGv)addr, (TCGv)addr, offset);
    }
    TCGv_i32 tcop = tcg_const_i32(mop);
    check_ddc(addr, cpu_env, /* overwrite addr */ (TCGv)addr, tcop);
    tcg_temp_free_i32(tcop);
}

#endif // TARGET_CHERI

static inline void gen_check_pcc_bounds_next_inst(DisasContext *ctx,
                                                  uint32_t num_bytes)
{
#ifdef TARGET_CHERI
    // Note: PC can only be incremented since a branch exits the TB, so checking
    // for pc_next < pcc.base should not be needed. Add a debug assertion in
    // case this assumption no longer holds in the future.
    tcg_debug_assert(ctx->base.pc_next >= ctx->base.pc_first);
    // XXX: __builtin_add_overflow() for end of address space?
    if (unlikely(ctx->base.pc_next + num_bytes > ctx->base.pcc_top)) {
        gen_raise_pcc_violation(&ctx->base, ctx->base.pc_next, num_bytes);
    }
#endif
}

static inline void gen_check_branch_target(DisasContext *ctx, target_ulong addr)
{
#ifdef TARGET_CHERI
    if (unlikely(!in_pcc_bounds(&ctx->base, addr))) {
        gen_raise_pcc_violation(&ctx->base, addr, 0);
    }
#endif
}

static inline void gen_check_cond_branch_target(DisasContext *ctx,
                                                TCGv branchcond,
                                                target_ulong addr)
{
#ifdef TARGET_CHERI
    // In the common case the target will be within the bounds of PCC so we
    // don't need a check no matter whether the branch is taken or not.
    if (likely(in_pcc_bounds(&ctx->base, addr)))
        return;

    TCGLabel *skip_btarget_check = gen_new_label();
    // skip the bounds violation if bcond == 0 (i.e. branch not taken)
    tcg_gen_brcondi_tl(TCG_COND_EQ, branchcond, 0, skip_btarget_check);
    // Brach taken -> raise a bounds violation exception
    gen_raise_pcc_violation(&ctx->base, addr, 0);
    gen_set_label(skip_btarget_check); // skip helper call
#endif
}
