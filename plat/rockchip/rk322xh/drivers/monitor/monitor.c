/*
 * Copyright (C) 2016, Fuzhou Rockchip Electronics Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
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

#include <arch_helpers.h>
#include <debug.h>
#include <efuse.h>
#include <interrupt_mgmt.h>
#include <mmio.h>
#include <platform.h>
#include <rk322xh_def.h>
#include <rockchip_exceptions.h>
#include <soc.h>

/* #define MONITOR_DBG */

#ifdef MONITOR_DBG
#define DBG_INFO(...)		tf_printf("MONITOR: "__VA_ARGS__)
#else
#define DBG_INFO(...)
#endif

#define ARCH_TIMER_TICKS_PER_SEC 24000000
/************************** config param **************************************/
#define MONITOR_POLL_SEC	10
#define MAX_WAIT_SEC		(20 * 60)	/* 20 minutes */
#define MAX_PANIC_SEC		120

#define FIQ_SEC_PHY_TIMER	29
#define MONITOR_TGT_CPU		0x1
/************************** SoC id phandle ************************************/
#define SOC_RK322XH		0x3228F
#define SOC_RK3328		0x3328
#define SOC_UNKNOWN		0xEEEE
#define SOC_ROOT		0xFFFF
/************************** SoC software id ***********************************/
#define RK322XH_SW_ID_MSK	0xFFFFFFFF
#define RK322XH_SW_ID		0x2CC117FF
#define RK3328_SW_ID		0x2A2AB17A
#define ROOT_SW_ID		0x00000000

/************************** relative register ********************************/
#define GRF_OS_REG4		0x05D8
#define PMU_PWRDN_ST		0x10
/******************************************************************************/
static uint32_t rockchip_soc_id, rockchip_sw_id;
static const int random_table[32] = {
	0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0,
	1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0
};

static inline uint64_t arch_counter_get_cntpct(void)
{
	uint64_t cval;

	isb();
	cval = read_cntpct_el0();

	return cval;
}

static inline void arch_timer_set_cntptval(uint32_t cntptval)
{
	write_cntps_tval_el1(cntptval);
}

static inline void arch_timer_set_cntpctl(uint32_t cntpctl)
{
	write_cntps_ctl_el1(cntpctl);
}

static inline void unknown_panic(void)
{
	/* DPLL 24mhz */
	DBG_INFO("%s: soc=0x%x, sw=0x%x, os=0x%x\n", __func__,
		rockchip_soc_id, rockchip_sw_id, grf_read32(GRF_OS_REG4));
	cru_write32(PLL_SLOW_MODE(DPLL_ID), CRU_CRU_MODE);
}

static inline int random_panic(void)
{
	uint64_t cntpct;
	uint32_t dpll_con0, offset;
	static uint32_t count;

	DBG_INFO("%s: soc=0x%x, sw=0x%x, os=0x%x\n", __func__,
		rockchip_soc_id, rockchip_sw_id, grf_read32(GRF_OS_REG4));
	cntpct = arch_counter_get_cntpct();
	offset = random_table[cntpct & 0x1f];
	dpll_con0 = cru_read32(PLL_CONS(DPLL_ID, 0));	/*DPLL*/
	dpll_con0 = 0x0fff0000 | (dpll_con0 + offset);	/*fbdiv: [11:0]*/
	cru_write32(dpll_con0, PLL_CONS(DPLL_ID, 0));
	count++;

	return count;
}

static int rk_get_sw_id(void)
{
	uint32_t os_reg, gate17, sw_id;

	/* enable pclk_grf, gate17[0] */
	gate17 = cru_read32(CRU_CLKGATE_CON(17));
	cru_write32(((0 << 0) | (1 << 16)), CRU_CLKGATE_CON(17));

	os_reg = grf_read32(GRF_OS_REG4);
	if ((os_reg & RK322XH_SW_ID_MSK) == RK322XH_SW_ID)
		sw_id = SOC_RK322XH;
	else if ((os_reg & RK322XH_SW_ID_MSK) == RK3328_SW_ID)
		sw_id = SOC_RK3328;
	else if ((os_reg & RK322XH_SW_ID_MSK) == ROOT_SW_ID)
		sw_id = SOC_ROOT;
	else
		sw_id = SOC_UNKNOWN;

	cru_write32(gate17 | 0xffff0000, CRU_CLKGATE_CON(17));

	return sw_id;
}

static uint32_t rk_get_efuse_id(uint8_t *efuse_buf)
{
	uint32_t soc_id;

	/* rk3228h */
	if (efuse_buf[2] == 0x23 && efuse_buf[3] == 0x82) {
		if ((efuse_buf[6] & 0x1f) == 8)/* H */
			soc_id = SOC_RK322XH;
		else
			soc_id = SOC_UNKNOWN;
	/* rk3328 */
	} else if (efuse_buf[2] == 0x33 && efuse_buf[3] == 0x82) {
		if ((efuse_buf[6] & 0x1f) == 1)/* A */
			soc_id = SOC_RK3328;
		else
			soc_id = SOC_UNKNOWN;
	/* rk2382h -> used as rk3228h */
	} else if (efuse_buf[2] == 0x32 && efuse_buf[3] == 0x28) {
		if ((efuse_buf[6] & 0x1f) == 8)/* H */
			soc_id = SOC_ROOT;
		else
			soc_id = SOC_UNKNOWN;
	/* empty efuse */
	} else if (efuse_buf[2] == 0 && efuse_buf[3] == 0) {
		soc_id = SOC_ROOT;
	/* unknown soc */
	} else {
		soc_id = SOC_UNKNOWN;
	}

	return soc_id;
}

static uint64_t soc_monitor_isr(uint32_t id,
				uint32_t flags,
				void *handle,
				void *cookie)
{
	uint32_t cntptval, panic_cnt = 0;
	static uint32_t wait_cnt, cpu_online_mask;

	/* temporary root, read new sw id */
	if (rockchip_sw_id == SOC_ROOT)
		rockchip_sw_id = rk_get_sw_id();

	/* unknown sw, panic */
	if (rockchip_sw_id == SOC_UNKNOWN) {
		DBG_INFO("unknown sw id\n");
		panic_cnt = random_panic();
	}

	/* not match and not root, panic */
	if ((rockchip_soc_id != rockchip_sw_id) &&
	    (rockchip_soc_id != SOC_ROOT) && (rockchip_sw_id != SOC_ROOT)) {
		DBG_INFO("can't match\n");
		panic_cnt = random_panic();
	}

	/* smp brought up yet and timeout to write sw series, panic */
	if (cpu_online_mask <= 0x1)
		cpu_online_mask = ~pmu_read32(PMU_PWRDN_ST) & 0x0f;
	if ((rockchip_sw_id == SOC_ROOT) && (cpu_online_mask >= 0x03) &&
	    (++wait_cnt > (MAX_WAIT_SEC / MONITOR_POLL_SEC))) {
		DBG_INFO("sw wait timeout\n");
		panic_cnt = random_panic();
	}

	/* must panic if random panic over seconds */
	if (panic_cnt > (MAX_PANIC_SEC / MONITOR_POLL_SEC)) {
		DBG_INFO("panic wait timeout\n");
		unknown_panic();
	}

	/* match, disable fiq timer */
	if ((rockchip_soc_id == rockchip_sw_id) &&
	    (rockchip_soc_id != SOC_ROOT) && (rockchip_sw_id != SOC_ROOT)) {
		wait_cnt = 0;
		arch_timer_set_cntpctl(0);
		plat_rockchip_gic_fiq_disable(FIQ_SEC_PHY_TIMER);
		DBG_INFO("match: disable fiq timer\n");
		return 0;
	}

	/* reload timer cntptval*/
	cntptval = MONITOR_POLL_SEC * ARCH_TIMER_TICKS_PER_SEC;
	arch_timer_set_cntptval(cntptval);

	DBG_INFO("soc=0x%x, sw=0x%x, timeout=%ds, panic=%ds, cpu=0x%x\n",
		 rockchip_soc_id, rockchip_sw_id,
		 (wait_cnt * MONITOR_POLL_SEC),
		 (panic_cnt * MONITOR_POLL_SEC),
		 cpu_online_mask);

	return 0;
}

static void soc_monitor_init(uint32_t monitor_sec)
{
	uint32_t cntptval, cntpctl;

	/* enable timer & interrupt */
	cntpctl = (1 << 0) | (0 << 1);
	cntptval = monitor_sec * ARCH_TIMER_TICKS_PER_SEC;
	arch_timer_set_cntptval(cntptval);
	arch_timer_set_cntpctl(cntpctl);
}

int rk_soc_monitor_init(void)
{
	int ret;
	uint32_t efuse32_buf[8] = {0};

	/* read NS-efuse */
	mode_init(1);
	mode_init(0);
	ret = rk_efuse32_readregs(RK322XH_NS_EFUSE_START,
				  RK322XH_NS_EFUSE_WORDS,
				  efuse32_buf);
	if (ret) {
		ERROR("read efuse failed!\n");
		return -1;
	}

#ifdef MONITOR_DBG
	int i;
	uint8_t *efuse8_buf = (uint8_t *)efuse32_buf;

	for (i = 0; i < 32; i++)
		DBG_INFO("efuse[%d]=0x%x\n", i, efuse8_buf[i]);
#endif

	/* get efuse chip id */
	rockchip_soc_id = rk_get_efuse_id((uint8_t *)efuse32_buf);
	if (rockchip_soc_id == SOC_UNKNOWN) {
		DBG_INFO("Unknown SoC\n");
		unknown_panic();
		return -1;
	} else if (rockchip_soc_id == SOC_ROOT) {
		DBG_INFO("Root SoC\n");
		return 0;	/* success */
	}

	/* set initial state */
	rockchip_sw_id = SOC_ROOT;

	/* register and enable fiq */
	ret = register_secfiq_handler(FIQ_SEC_PHY_TIMER, soc_monitor_isr);
	if (ret)
		return ret;

	plat_rockchip_gic_fiq_enable(FIQ_SEC_PHY_TIMER, MONITOR_TGT_CPU);

	/* init arm generic timer */
	soc_monitor_init(MONITOR_POLL_SEC);

	DBG_INFO("SoC Monitor init done. SoC: %x\n", rockchip_soc_id);

	return 0;
}

