/*
 * Copyright (c) 2016, ARM Limited and Contributors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <debug.h>
#include <mmio.h>
#include <psci.h>
#include <rockchip_sip_svc.h>
#include <runtime_svc.h>
#include <dram.h>

#define CONFIG_DRAM_INIT	0x00
#define CONFIG_DRAM_SET_RATE	0x01
#define CONFIG_DRAM_ROUND_RATE	0x02
#define CONFIG_DRAM_SET_AT_SR	0x03
#define CONFIG_DRAM_GET_BW	0x04
#define CONFIG_DRAM_GET_RATE	0x05
#define CONFIG_DRAM_CLR_IRQ	0x06
#define CONFIG_DRAM_SET_PARAM	0x07

int ddr_smc_handler(uint64_t arg0, uint64_t arg1,
		    uint64_t id, struct arm_smccc_res *res)
{
	switch (id) {
	case CONFIG_DRAM_INIT:
		ddr_init();
		break;
	case CONFIG_DRAM_SET_RATE:
		return ddr_set_rate(arg0);
	case CONFIG_DRAM_ROUND_RATE:
		return ddr_round_rate(arg0);
	case CONFIG_DRAM_GET_RATE:
		return ddr_get_rate();
	case CONFIG_DRAM_CLR_IRQ:
		clr_dcf_irq();
		break;
	case CONFIG_DRAM_SET_PARAM:
		dts_timing_receive(arg0, arg1);
		break;
	default:
		return SIP_RET_INVALID_PARAMS;
		break;
	}

	return SIP_RET_SUCCESS;
}

