/*
 *    Based on code from Cortina Systems, Inc.
 *
 *    Copyright (C) 2011 by Cortina Systems, Inc.
 *    Copyright (C) 2011 - 2012 Cavium, Inc.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/phy.h>
#include <linux/of.h>

#include "cs4321-ucode.h"

#define CS4321_GLOBAL_CHIP_ID_LSB			0x0
#define CS4321_GLOBAL_CHIP_ID_MSB			0x1
#define CS4321_GLOBAL_INGRESS_SOFT_RESET		0xC
#define CS4321_GLOBAL_EGRESS_SOFT_RESET			0xD
#define CS4321_GLOBAL_REF_SOFT_RESET			0xE
#define CS4321_GLOBAL_MPIF_SOFT_RESET			0xF
#define CS4321_GLOBAL_MPIF_RESET_DOTREG			0x10
#define CS4321_GLOBAL_INGRESS_FUNCEN			0x12
#define CS4321_GLOBAL_EGRESS_FUNCEN			0x13
#define CS4321_GLOBAL_HOST_MULTILANE_FUNCEN		0x14
#define CS4321_GLOBAL_INGRESS_CLKEN			0x15
#define CS4321_GLOBAL_INGRESS_CLKEN2			0x16
#define CS4321_GLOBAL_EGRESS_CLKEN			0x17
#define CS4321_GLOBAL_EGRESS_CLKEN2			0x18
#define CS4321_GLOBAL_HOST_MULTILANE_CLKSEL		0x19
#define CS4321_GLOBAL_MSEQCLKCTRL			0x20
#define CS4321_GLOBAL_GT_10KHZ_REF_CLK_CNT1		0x2D
#define CS4321_GLOBAL_GT_10KHZ_REF_CLK_CNT0		0x2E
#define CS4321_GLOBAL_MISC_CONFIG			0x33
#define CS4321_GPIO_GPIO3				0x10C
#define CS4321_GPIO_GPIO3_DRIVE				0x10E
#define CS4321_GPIO_GPIO10				0x136
#define CS4321_GPIO_GPIO10_OUTPUT_CFG			0x137
#define CS4321_GPIO_GPIO_INT				0x16B
#define CS4321_GPIO_GPIO_INTE				0x16C
#define CS4321_GPIO_GPIO_INTS				0x16D
#define CS4321_MSEQ_ENABLE_MSB				0x204
#define CS4321_MSEQ_SERDES_PARAM_LSB			0x205
#define CS4321_MSEQ_POWER_DOWN_LSB			0x208
#define CS4321_MSEQ_POWER_DOWN_MSB			0x209
#define CS4321_MSEQ_STATUS				0x20a
#define CS4321_MSEQ_LEAK_INTERVAL_FFE			0x21C
#define CS4321_MSEQ_COEF_DSP_DRIVE128			0x21F
#define CS4321_MSEQ_COEF_INIT_SEL			0x223
#define CS4321_MSEQ_CAL_RX_EQADJ			0x22A
#define CS4321_MSEQ_CAL_RX_PHSEL			0x22C
#define CS4321_MSEQ_CAL_RX_SLICER			0x22D
#define CS4321_MSEQ_CAL_RX_DFE_EQ			0x22E
#define CS4321_MSEQ_OPTIONS				0x240
#define CS4321_MSEQ_PC					0x243
#define CS4321_MSEQ_BANKSELECT				0x24F
#define CS4321_MSEQ_RESET_COUNT_LSB			0x250
#define CS4321_MSEQ_SPARE2_LSB				0x270
#define CS4321_MSEQ_SPARE9_LSB				0x27E
#define CS4321_MSEQ_SPARE11_LSB				0x282
#define CS4321_MSEQ_SPARE15_LSB				0x28A
#define CS4321_MSEQ_SPARE21_LSB				0x296
#define CS4321_MSEQ_SPARE23_LSB				0x29A
#define CS4321_DSP_SDS_DSP_COEF_DFE0_SELECT		0x37B
#define CS4321_DSP_SDS_DSP_COEF_LARGE_LEAK		0x382
#define CS4321_DSP_SDS_SERDES_SRX_DAC_ENABLEB_LSB	0x400
#define CS4321_DSP_SDS_SERDES_SRX_DAC_ENABLEB_MSB	0x401
#define CS4321_DSP_SDS_SERDES_SRX_DAC_BIAS_SELECT0_MSB	0x403
#define CS4321_DSP_SDS_SERDES_SRX_DAC_BIAS_SELECT1_MSB	0x405
#define CS4321_DSP_SDS_SERDES_SRX_FFE_DELAY_CTRL	0x409
#define CS4321_DSP_SDS_SERDES_SRX_FFE_INBUF_CTRL	0x40A
#define CS4321_DSP_SDS_SERDES_SRX_FFE_PGA_CTRL		0x40B
#define CS4321_DSP_SDS_SERDES_SRX_FFE_MISC		0x40C
#define CS4321_DSP_SDS_SERDES_SRX_DFE0_SELECT		0x40E
#define CS4321_DSP_SDS_SERDES_SRX_DFE_MISC		0x412
#define CS4321_DSP_SDS_SERDES_SRX_AGC_MISC		0x413
#define CS4321_LINE_SDS_COMMON_SRX0_RX_CONFIG		0x500
#define CS4321_LINE_SDS_COMMON_SRX0_RX_CLKDIV_CTRL	0x501
#define CS4321_LINE_SDS_COMMON_SRX0_RX_LOOP_FILTER	0x503
#define CS4321_LINE_SDS_COMMON_SRX0_RX_CPA		0x504
#define CS4321_LINE_SDS_COMMON_SRX0_RX_CPB		0x505
#define CS4321_LINE_SDS_COMMON_SRX0_RX_VCO_CTRL		0x507
#define CS4321_LINE_SDS_COMMON_SRX0_RX_SPARE		0x50C
#define CS4321_LINE_SDS_COMMON_RXVCO0_CONTROL		0x512
#define CS4321_LINE_SDS_COMMON_STX0_TX_OUTPUT_CTRLA	0x529
#define CS4321_LINE_SDS_COMMON_STX0_TX_OUTPUT_CTRLB	0x52A
#define CS4321_LINE_SDS_COMMON_STX0_TX_CONFIG_LOCAL_TIMING 0x52B
#define CS4321_LINE_SDS_COMMON_STXP0_TX_CONFIG		0x52C
#define CS4321_LINE_SDS_COMMON_STXP0_TX_PWRDN		0x52D
#define CS4321_LINE_SDS_COMMON_STXP0_TX_CLKOUT_CTRL	0x52F
#define CS4321_LINE_SDS_COMMON_STXP0_TX_LOOP_FILTER	0x530
#define CS4321_LINE_SDS_COMMON_TXVCO0_CONTROL		0x539
#define CS4321_HOST_SDS_COMMON_SRX0_RX_CONFIG		0x600
#define CS4321_HOST_SDS_COMMON_SRX0_RX_CLKDIV_CTRL	0x601
#define CS4321_HOST_SDS_COMMON_SRX0_RX_CLKOUT_CTRL	0x602
#define CS4321_HOST_SDS_COMMON_RXVCO0_CONTROL		0x613
#define CS4321_HOST_SDS_COMMON_STX0_TX_CONFIG_LOCAL_TIMING 0x62C
#define CS4321_HOST_SDS_COMMON_STXP0_TX_CONFIG		0x62D
#define CS4321_HOST_SDS_COMMON_STXP0_TX_PWRDN		0x62E
#define CS4321_HOST_SDS_COMMON_STXP0_TX_CLKDIV_CTRL	0x62F
#define CS4321_HOST_SDS_COMMON_STXP0_TX_CLKOUT_CTRL	0x630
#define CS4321_HOST_SDS_COMMON_TXVCO0_CONTROL		0x63A
#define CS4321_HOST_ML_SDS_COMMON_SRX0_RX_CONFIG	0x700
#define CS4321_HOST_ML_SDS_COMMON_SRX0_RX_CLKDIV_CTRL	0x701
#define CS4321_HOST_ML_SDS_COMMON_RXVCO0_CONTROL	0x70F
#define CS4321_HOST_ML_SDS_COMMON_STXP0_TX_CONFIG	0x725
#define CS4321_HOST_ML_SDS_COMMON_STXP0_TX_PWRDN	0x726
#define CS4321_HOST_ML_SDS_COMMON_STXP0_TX_CLKDIV_CTRL	0x727
#define CS4321_HOST_ML_SDS_COMMON_TXVCO0_CONTROL	0x731
#define CS4321_XGPCS_LINE_TX_TXCNTRL			0xA00
#define CS4321_XGPCS_LINE_RX_RXCNTRL			0xA20
#define CS4321_XGPCS_HOST_TX_TXCNTRL			0xA80
#define CS4321_XGPCS_HOST_RX_RXCNTRL			0xAA0
#define CS4321_GIGEPCS_LINE_CONTROL			0xC00
#define CS4321_GIGEPCS_HOST_CONTROL			0xC80
#define CS4321_HIF_COMMON_TXCONTROL3			0xD0B
#define CS4321_XGRS_LINE_TX_TXCNTRL			0xE00
#define CS4321_XGRS_LINE_RX_RXCNTRL1			0xE10
#define CS4321_XGRS_HOST_TX_TXCNTRL			0xE80
#define CS4321_XGRS_HOST_RX_RXCNTRL1			0xE90
#define CS4321_XGMAC_LINE_RX_CFG_COM			0xF00
#define CS4321_XGMAC_LINE_TX_CFG_COM			0xF40
#define CS4321_XGMAC_LINE_TX_CFG_TX			0xF41
#define CS4321_XGMAC_LINE_TX_CFG_TX_IFG			0xF43
#define CS4321_XGMAC_HOST_RX_CFG_COM			0xF80
#define CS4321_XGMAC_HOST_TX_CFG_COM			0xFC0
#define CS4321_XGMAC_HOST_TX_CFG_TX			0xFC1
#define CS4321_XGMAC_HOST_TX_CFG_TX_IFG			0xFC3
#define CS4321_MAC_LAT_CTRL_RESET			0x3000
#define CS4321_MAC_LAT_CTRL_CONFIG			0x3001
#define CS4321_RADJ_INGRESS_RX_NRA_MIN_IFG		0x3204
#define CS4321_RADJ_INGRESS_RX_NRA_SETTLE		0x3206
#define CS4321_RADJ_INGRESS_TX_ADD_FILL_DATA1		0x3210
#define CS4321_RADJ_INGRESS_TX_ADD_FILL_DATA0		0x3211
#define CS4321_RADJ_INGRESS_TX_ADD_FILL_CTRL		0x3212
#define CS4321_RADJ_INGRESS_TX_PRA_MIN_IFG		0x3214
#define CS4321_RADJ_INGRESS_TX_PRA_SETTLE		0x3216
#define CS4321_RADJ_INGRESS_MISC_RESET			0x3220
#define CS4321_RADJ_EGRESS_RX_NRA_MIN_IFG		0x3284
#define CS4321_RADJ_EGRESS_RX_NRA_SETTLE		0x3286
#define CS4321_RADJ_EGRESS_TX_ADD_FILL_DATA1		0x3290
#define CS4321_RADJ_EGRESS_TX_ADD_FILL_DATA0		0x3291
#define CS4321_RADJ_EGRESS_TX_ADD_FILL_CTRL		0x3292
#define CS4321_RADJ_EGRESS_TX_PRA_MIN_IFG		0x3294
#define CS4321_RADJ_EGRESS_TX_PRA_SETTLE		0x3296
#define CS4321_RADJ_EGRESS_MISC_RESET			0x32A0
#define CS4321_PM_CTRL					0x3400
#define CS4321_EEPROM_LOADER_CONTROL			0x3F00
#define CS4321_EEPROM_LOADER_STATUS			0x3F01


enum cs4321_host_mode {
	RXAUI,
	XAUI
};

struct cs4321_private {
	enum cs4321_host_mode mode;
};

struct cs4321_reg_modify {
	u16 reg;
	u16 mask_bits;
	u16 set_bits;
};

struct cs4321_multi_seq {
	int reg_offset;
	const struct cs4321_reg_modify *seq;
};

static const struct cs4321_reg_modify cs4321_soft_reset_registers[] = {
	/* Enable all the clocks */
	{CS4321_GLOBAL_INGRESS_CLKEN, 0, 0xffff},
	{CS4321_GLOBAL_INGRESS_CLKEN2, 0, 0xffff},
	{CS4321_GLOBAL_EGRESS_CLKEN, 0, 0xffff},
	{CS4321_GLOBAL_EGRESS_CLKEN2, 0, 0xffff},
	/* Reset MPIF registers */
	{CS4321_GLOBAL_MPIF_RESET_DOTREG, 0, 0x0},
	/* Re-assert the reset */
	{CS4321_GLOBAL_MPIF_RESET_DOTREG, 0, 0xffff},

	/* Disable all the clocks */
	{CS4321_GLOBAL_INGRESS_CLKEN, 0, 0x0},
	{CS4321_GLOBAL_INGRESS_CLKEN2, 0, 0x0},
	{CS4321_GLOBAL_EGRESS_CLKEN, 0, 0x0},
	{CS4321_GLOBAL_EGRESS_CLKEN2, 0, 0x0},
	{0}
};

static const struct cs4321_reg_modify cs4321_68xx_4_nic_init[] = {
	/* Configure chip for common reference clock */
	{CS4321_LINE_SDS_COMMON_STXP0_TX_CONFIG, 0, 0x2700},
	/* Set GPIO3 to drive low to enable laser output*/
	{CS4321_GPIO_GPIO3_DRIVE, 0, 0},
	{CS4321_GPIO_GPIO3, 0, 0x11},
	/* Set GPIO10 as GPIO_INT open-drain active low */
	{CS4321_GPIO_GPIO_INTE, 0, 0},
	{CS4321_GPIO_GPIO10_OUTPUT_CFG, 0, 6},
	{CS4321_GPIO_GPIO10, 0, 0x0719},

	{0}
};

static const struct cs4321_reg_modify cs4321_init_prefix_seq[] = {
	/* MPIF DeAssert System Reset */
	{CS4321_GLOBAL_MPIF_RESET_DOTREG, ~0x0001, 0},
	/*
	 * Make sure to stall the microsequencer before configuring
	 * the path.
	 */
	{CS4321_GLOBAL_MSEQCLKCTRL, 0, 0x8004},
	{CS4321_MSEQ_OPTIONS, 0, 0xf},
	{CS4321_MSEQ_PC, 0, 0x0},
	/*
	 * Correct some of the h/w defaults that are incorrect.
	 *
	 * The default value of the bias current is incorrect and needs to
	 * be corrected. This is normally done by Microcode but it doesn't
	 * always run.
	 */
	{CS4321_DSP_SDS_SERDES_SRX_DAC_BIAS_SELECT0_MSB, 0, 0x20},
	/*
	 * By default need to power on the voltage monitor since it is required
	 * by the temperature monitor and this is used by the microcode.
	 */
	{CS4321_LINE_SDS_COMMON_SRX0_RX_CONFIG, 0, 0x0},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_ingress_local_timing_rxaui[] = {
	{CS4321_LINE_SDS_COMMON_SRX0_RX_CONFIG, 0, 0x0000},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_CPA, 0, 0x0057},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_LOOP_FILTER, 0, 0x0007},
	{CS4321_LINE_SDS_COMMON_STX0_TX_CONFIG_LOCAL_TIMING, 0, 0x0001},
	{CS4321_LINE_SDS_COMMON_STXP0_TX_CLKOUT_CTRL, 0, 0x0864},
	{CS4321_LINE_SDS_COMMON_STXP0_TX_LOOP_FILTER, 0, 0x0027},
	{CS4321_LINE_SDS_COMMON_STXP0_TX_PWRDN, 0, 0x0000},
	{CS4321_GLOBAL_INGRESS_SOFT_RESET, ~0x1, 0x0001},
	{CS4321_GLOBAL_INGRESS_SOFT_RESET, ~0x1, 0x0000},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_egress_local_timing_rxaui[] = {
	{CS4321_HOST_SDS_COMMON_SRX0_RX_CLKDIV_CTRL, 0, 0x40d1},
	{CS4321_HOST_SDS_COMMON_SRX0_RX_CONFIG, 0, 0x000c},
	{CS4321_HOST_SDS_COMMON_STX0_TX_CONFIG_LOCAL_TIMING, 0, 0x0001},
	{CS4321_HOST_SDS_COMMON_STXP0_TX_CLKDIV_CTRL, 0, 0x4091},
	{CS4321_HOST_SDS_COMMON_STXP0_TX_CLKOUT_CTRL, 0, 0x1864},
	{CS4321_HOST_SDS_COMMON_STXP0_TX_CONFIG, 0, 0x090c},
	{CS4321_HOST_SDS_COMMON_STXP0_TX_PWRDN, 0, 0x0000},
	{CS4321_HOST_ML_SDS_COMMON_SRX0_RX_CLKDIV_CTRL, 0, 0x401d},
	{CS4321_HOST_ML_SDS_COMMON_SRX0_RX_CONFIG, 0, 0x000c},
	{CS4321_HOST_ML_SDS_COMMON_STXP0_TX_CLKDIV_CTRL, 0, 0x4019},
	{CS4321_HOST_ML_SDS_COMMON_STXP0_TX_CONFIG, 0, 0x090c},
	{CS4321_GLOBAL_EGRESS_SOFT_RESET, ~0x1, 0x0001},
	{CS4321_GLOBAL_EGRESS_SOFT_RESET, ~0x1, 0x0000},
	{CS4321_GLOBAL_EGRESS_SOFT_RESET, ~0x2, 0x0001},
	{CS4321_GLOBAL_EGRESS_SOFT_RESET, ~0x2, 0x0000},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_line_power_down[] = {
	{CS4321_LINE_SDS_COMMON_STX0_TX_OUTPUT_CTRLA, 0, 0x0000},
	{CS4321_LINE_SDS_COMMON_STX0_TX_OUTPUT_CTRLB, 0, 0x0000},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_CONFIG, 0, 0x2040},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_VCO_CTRL, 0, 0x01e7},
	{CS4321_MSEQ_POWER_DOWN_LSB, 0, 0x0000},
	{CS4321_DSP_SDS_SERDES_SRX_DAC_ENABLEB_MSB, 0, 0xffff},
	{CS4321_DSP_SDS_SERDES_SRX_DAC_ENABLEB_LSB, 0, 0xffff},
	{CS4321_DSP_SDS_SERDES_SRX_AGC_MISC, 0, 0x0705},
	{CS4321_DSP_SDS_SERDES_SRX_DFE_MISC, 0, 0x002b},
	{CS4321_DSP_SDS_SERDES_SRX_FFE_PGA_CTRL, 0, 0x0021},
	{CS4321_DSP_SDS_SERDES_SRX_FFE_MISC, 0, 0x0013},
	{CS4321_DSP_SDS_SERDES_SRX_FFE_INBUF_CTRL, 0, 0x0001},
	{CS4321_DSP_SDS_SERDES_SRX_DFE0_SELECT, 0, 0x0001},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_ingress_rxaui_pcs_ra[] = {
	/* Set fen_radj, rx_fen_xgpcs*/
	{CS4321_GLOBAL_INGRESS_FUNCEN, ~0x0081, 0x0081},
	/* Set rx_en_radj, rx_en_xgpcs*/
	{CS4321_GLOBAL_INGRESS_CLKEN, ~0x0021, 0x0021},
	/* Set tx_en_hif, tx_en_radj*/
	{CS4321_GLOBAL_INGRESS_CLKEN2, ~0x0120, 0x0120},

	{CS4321_GLOBAL_HOST_MULTILANE_CLKSEL, 0, 0x8000},
	{CS4321_GLOBAL_HOST_MULTILANE_FUNCEN, 0, 0x0006},

	{CS4321_GLOBAL_REF_SOFT_RESET, 0, 0xffff},
	{CS4321_GLOBAL_REF_SOFT_RESET, 0, 0x0000},
	/* MPIF DeAssert Ingress Reset */
	{CS4321_GLOBAL_MPIF_RESET_DOTREG, ~0x0004, 0},

	{CS4321_XGMAC_LINE_RX_CFG_COM, 0, 0x8010},
	{CS4321_XGPCS_LINE_RX_RXCNTRL, 0, 0x5000},

	{CS4321_RADJ_INGRESS_RX_NRA_MIN_IFG, 0, 0x0004},
	{CS4321_RADJ_INGRESS_RX_NRA_SETTLE, 0, 0x0000},
	{CS4321_RADJ_INGRESS_TX_ADD_FILL_CTRL, 0, 0xf001},
	{CS4321_RADJ_INGRESS_TX_ADD_FILL_DATA0, 0, 0x0707},
	{CS4321_RADJ_INGRESS_TX_ADD_FILL_DATA1, 0, 0x0707},
	{CS4321_RADJ_INGRESS_TX_PRA_MIN_IFG, 0, 0x0004},
	{CS4321_RADJ_INGRESS_TX_PRA_SETTLE, 0, 0x0000},
	{CS4321_RADJ_INGRESS_MISC_RESET, 0, 0x0000},

	{CS4321_GLOBAL_INGRESS_SOFT_RESET, 0, 0x0002},
	{CS4321_GLOBAL_INGRESS_SOFT_RESET, 0, 0x0000},
	{CS4321_RADJ_INGRESS_MISC_RESET, 0, 0x0000},

	{CS4321_PM_CTRL, 0, 0x0000},
	{CS4321_HIF_COMMON_TXCONTROL3, 0, 0x0010},

	{CS4321_MSEQ_POWER_DOWN_LSB, 0, 0xe01f},

	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_egress_rxaui_pcs_ra[] = {
	/* Set tx_fen_xgpcs, fen_radj */
	{CS4321_GLOBAL_EGRESS_FUNCEN, ~0x0180, 0x0180},
	/* Set rx_en_hif, rx_en_radj*/
	{CS4321_GLOBAL_EGRESS_CLKEN, ~0x0120, 0x0120},
	/* Set tx_en_radj, tx_en_xgpcs*/
	{CS4321_GLOBAL_EGRESS_CLKEN2, ~0x0021, 0x0021},

	{CS4321_GLOBAL_HOST_MULTILANE_CLKSEL, 0, 0x8000},
	{CS4321_GLOBAL_HOST_MULTILANE_FUNCEN, 0, 0x0006},

	{CS4321_GLOBAL_REF_SOFT_RESET, 0, 0xffff},
	{CS4321_GLOBAL_REF_SOFT_RESET, 0, 0x0000},
	/* MPIF DeAssert Egress Reset */
	{CS4321_GLOBAL_MPIF_RESET_DOTREG, ~0x0002, 0},

	{CS4321_XGMAC_LINE_TX_CFG_COM, 0, 0xc000},
	{CS4321_XGMAC_LINE_TX_CFG_TX_IFG, 0, 0x0005},
	{CS4321_XGPCS_LINE_TX_TXCNTRL, 0, 0x0000},
	{CS4321_XGRS_LINE_TX_TXCNTRL, 0, 0xc000},

	{CS4321_RADJ_EGRESS_RX_NRA_MIN_IFG, 0, 0x0004},
	{CS4321_RADJ_EGRESS_RX_NRA_SETTLE, 0, 0x0000},
	{CS4321_RADJ_EGRESS_TX_ADD_FILL_CTRL, 0, 0xf001},
	{CS4321_RADJ_EGRESS_TX_ADD_FILL_DATA0, 0, 0x0707},
	{CS4321_RADJ_EGRESS_TX_ADD_FILL_DATA1, 0, 0x0707},
	{CS4321_RADJ_EGRESS_TX_PRA_MIN_IFG, 0, 0x0004},
	{CS4321_RADJ_EGRESS_TX_PRA_SETTLE, 0, 0x0000},
	{CS4321_RADJ_EGRESS_MISC_RESET, 0, 0x0000},

	{CS4321_PM_CTRL, 0, 0x0000},
	{CS4321_HIF_COMMON_TXCONTROL3, 0, 0x0010},
	{CS4321_MSEQ_POWER_DOWN_LSB, 0, 0xe01f},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_global_timer_156_25[] = {
	{CS4321_GLOBAL_GT_10KHZ_REF_CLK_CNT0, 0, 15625},
	{CS4321_GLOBAL_GT_10KHZ_REF_CLK_CNT1, 0, 0},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_mac_latency[] = {
	{CS4321_MAC_LAT_CTRL_CONFIG, 0, 0},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_ref_clk_src_xaui_rxaui[] = {
	/* Set edc_stxp_lptime_sel = 1, edc_stxp_pilot_sel = 7 */
	{CS4321_GLOBAL_MISC_CONFIG, (u16)~0xe700, 0x2700},
	/* Set STXP_PILOT_SEL = 7, STXP_LPTIME_SEL = 1 */
	{CS4321_LINE_SDS_COMMON_STXP0_TX_CONFIG, (u16)~0xe700, 0x2700},
	{0}
};


static const struct cs4321_reg_modify cs4321_init_polarity_inv[] = {
	/* Inversion disabled*/
	/* config the slice not to invert polarity on egress */
	{CS4321_HOST_SDS_COMMON_SRX0_RX_CONFIG, ~0x0800, 0},
	/* config the slice not to invert polarity on ingress */
	{CS4321_MSEQ_ENABLE_MSB, ~0x4000, 0},
	{0}
};

#if 0
static const struct cs4321_reg_modify cs4321_hsif_elec_mode_set_cx1_pre[] = {
	/* Stop the micro-sequencer */
	{CS4321_GLOBAL_MSEQCLKCTRL, 0, 0x8004},
	{CS4321_MSEQ_OPTIONS, 0, 0xf},
	{CS4321_MSEQ_PC, 0, 0x0},

	{CS4321_MSEQ_COEF_DSP_DRIVE128, 0, 0x0114},
	{CS4321_MSEQ_COEF_INIT_SEL,     0, 0x0004},
	{CS4321_MSEQ_LEAK_INTERVAL_FFE, 0, 0x8010},
	{CS4321_MSEQ_BANKSELECT, 0, 0x2},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_CPA, 0, 0x55},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_LOOP_FILTER, 0, 0x3},
	{CS4321_DSP_SDS_SERDES_SRX_DFE0_SELECT, 0, 0x1},
	{CS4321_DSP_SDS_DSP_COEF_DFE0_SELECT, 0, 0x2},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_CPB, 0, 0x2003},
	{CS4321_DSP_SDS_SERDES_SRX_FFE_DELAY_CTRL, 0, 0xF047},
	{CS4321_MSEQ_RESET_COUNT_LSB, 0, 0x0},
	/* enable power savings, ignore optical module LOS */
	{CS4321_MSEQ_SPARE2_LSB, 0, 0x5},

	{CS4321_MSEQ_SPARE9_LSB, 0, 0x5},

	{CS4321_MSEQ_CAL_RX_PHSEL, 0, 0x23},
	{CS4321_DSP_SDS_DSP_COEF_LARGE_LEAK, 0, 0x2},
	{CS4321_DSP_SDS_SERDES_SRX_DAC_ENABLEB_LSB, 0, 0x5000},
	{CS4321_MSEQ_POWER_DOWN_LSB, 0, 0xFFFF},
	{CS4321_MSEQ_POWER_DOWN_MSB, 0, 0x0},
	{CS4321_MSEQ_CAL_RX_SLICER, 0, 0x80},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_SPARE, 0, 0xE0E0},
	{CS4321_DSP_SDS_SERDES_SRX_DAC_BIAS_SELECT1_MSB, 0, 0xff},

	{CS4321_MSEQ_SERDES_PARAM_LSB, 0, 0x0603},
	{CS4321_MSEQ_SPARE11_LSB, 0, 0x0603},


	{0}
};

static const struct cs4321_reg_modify cs4321_hsif_elec_mode_set_cx1_2in[] = {
	{CS4321_MSEQ_SPARE15_LSB, 0, 0x0603},
	{CS4321_MSEQ_SPARE21_LSB, 0, 0xE},
	{CS4321_MSEQ_SPARE23_LSB, 0, 0x0},
	{CS4321_MSEQ_CAL_RX_DFE_EQ, 0, 0x3},
	{0}
};

static const struct cs4321_reg_modify cs4321_hsif_elec_mode_set_cx1_post[] = {
	/* Restart the micro-sequencer */
	{CS4321_GLOBAL_MSEQCLKCTRL, 0, 0x4},
	{CS4321_MSEQ_OPTIONS, 0, 0x7},
	{0}
};
#endif

static const struct cs4321_reg_modify cs4321_hsif_elec_mode_set_sr_pre[] = {
	/* Stop the micro-sequencer */
	{CS4321_GLOBAL_MSEQCLKCTRL, 0, 0x8004},
	{CS4321_MSEQ_OPTIONS, 0, 0xf},
	{CS4321_MSEQ_PC, 0, 0x0},

	/* Configure the micro-sequencer for an SR transceiver */
	{CS4321_MSEQ_COEF_DSP_DRIVE128, 0, 0x0134},
	{CS4321_MSEQ_COEF_INIT_SEL, 0, 0x0006},
	{CS4321_MSEQ_LEAK_INTERVAL_FFE, 0, 0x8010},
	{CS4321_MSEQ_BANKSELECT, 0, 0x0},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_CPA, 0, 0x55},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_LOOP_FILTER, 0, 0x3},
	{CS4321_DSP_SDS_SERDES_SRX_DFE0_SELECT, 0, 0x1},
	{CS4321_DSP_SDS_DSP_COEF_DFE0_SELECT, 0, 0x2},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_CPB, 0, 0x2003},
	{CS4321_DSP_SDS_SERDES_SRX_FFE_DELAY_CTRL, 0, 0xF048},

	{CS4321_MSEQ_RESET_COUNT_LSB, 0, 0x0},
	/* enable power savings, ignore */
	{CS4321_MSEQ_SPARE2_LSB, 0, 0x5},

	{CS4321_MSEQ_SPARE9_LSB, 0, 0x5},

	{CS4321_MSEQ_CAL_RX_PHSEL, 0, 0x1e},
	{CS4321_DSP_SDS_DSP_COEF_LARGE_LEAK, 0, 0x2},
	{CS4321_DSP_SDS_SERDES_SRX_DAC_ENABLEB_LSB, 0, 0xD000},
	{CS4321_MSEQ_POWER_DOWN_LSB, 0, 0xFFFF},
	{CS4321_MSEQ_POWER_DOWN_MSB, 0, 0x0},
	{CS4321_MSEQ_CAL_RX_SLICER, 0, 0x80},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_SPARE, 0, 0xE0E0},
	{CS4321_DSP_SDS_SERDES_SRX_DAC_BIAS_SELECT1_MSB, 0, 0xff},

	/* Setup the trace lengths for the micro-sequencer */
	{CS4321_MSEQ_SERDES_PARAM_LSB, 0, 0x0603},

	{0}
};

static const struct cs4321_reg_modify cs4321_hsif_elec_mode_set_sr_2in[] = {
	{CS4321_MSEQ_CAL_RX_EQADJ, 0, 0x0},
	{0}
};

static const struct cs4321_reg_modify cs4321_hsif_elec_mode_set_sr_post[] = {
	{CS4321_MSEQ_CAL_RX_DFE_EQ, 0, 0x0},
	/* Restart the micro-sequencer */
	{CS4321_GLOBAL_MSEQCLKCTRL, 0, 0x4},
	{CS4321_MSEQ_OPTIONS, 0, 0x7},
	{0}
};

static const struct cs4321_reg_modify cs4321_resync_vcos_xaui_rxaui[] = {
	{CS4321_HOST_SDS_COMMON_RXVCO0_CONTROL,  (u16)~0x8000, 0x8000},
	{CS4321_HOST_SDS_COMMON_RXVCO0_CONTROL,  (u16)~0x8000, 0},

	{CS4321_HOST_SDS_COMMON_TXVCO0_CONTROL,  (u16)~0x8000, 0x8000},
	{CS4321_HOST_SDS_COMMON_TXVCO0_CONTROL,  (u16)~0x8000, 0},

	{CS4321_LINE_SDS_COMMON_RXVCO0_CONTROL,  (u16)~0x8000, 0x8000},
	{CS4321_LINE_SDS_COMMON_RXVCO0_CONTROL,  (u16)~0x8000, 0},

	{CS4321_LINE_SDS_COMMON_TXVCO0_CONTROL,  (u16)~0x8000, 0x8000},
	{CS4321_LINE_SDS_COMMON_TXVCO0_CONTROL,  (u16)~0x8000, 0},

	{CS4321_HOST_ML_SDS_COMMON_RXVCO0_CONTROL,  (u16)~0x8000, 0x8000},
	{CS4321_HOST_ML_SDS_COMMON_RXVCO0_CONTROL,  (u16)~0x8000, 0},

	{CS4321_HOST_ML_SDS_COMMON_TXVCO0_CONTROL,  (u16)~0x8000, 0x8000},
	{CS4321_HOST_ML_SDS_COMMON_TXVCO0_CONTROL,  (u16)~0x8000, 0},

	{0}
};

static const struct cs4321_reg_modify cs4321_toggle_resets_xaui_rxaui[] = {
	{CS4321_HOST_ML_SDS_COMMON_SRX0_RX_CONFIG, ~0x0020, 0},
	{CS4321_HOST_ML_SDS_COMMON_STXP0_TX_PWRDN, ~0x0100, 0},

	/*
	 * Now that the device is configured toggle the ingress and
	 * egress soft resets to make sure the device re-syncs
	 * properly.
	 */
	{CS4321_GLOBAL_INGRESS_SOFT_RESET, 0, 0x3},
	{CS4321_GLOBAL_EGRESS_SOFT_RESET,  0, 0x3},
	{CS4321_GLOBAL_INGRESS_SOFT_RESET, 0, 0x0000},
	{CS4321_GLOBAL_EGRESS_SOFT_RESET,  0, 0x0000},

	{0}
};

static const struct cs4321_reg_modify cs4321_init_xaui_egress[] = {
	{CS4321_LINE_SDS_COMMON_STX0_TX_CONFIG_LOCAL_TIMING, 0, 0x0001},
	{CS4321_LINE_SDS_COMMON_STXP0_TX_CLKOUT_CTRL, 0, 0x0864},
	{CS4321_LINE_SDS_COMMON_STXP0_TX_LOOP_FILTER, 0, 0x0027},
	{CS4321_LINE_SDS_COMMON_STXP0_TX_PWRDN, 0, 0x0000},

	{CS4321_HOST_SDS_COMMON_SRX0_RX_CLKDIV_CTRL, 0, 0x45d2},
	{CS4321_HOST_SDS_COMMON_SRX0_RX_CLKOUT_CTRL, 0, 0x6a03},
	{CS4321_HOST_SDS_COMMON_SRX0_RX_CONFIG, 0, 0x000c},
	{CS4321_HOST_ML_SDS_COMMON_SRX0_RX_CLKDIV_CTRL, 0, 0x412d},
	{CS4321_HOST_ML_SDS_COMMON_SRX0_RX_CONFIG, 0, 0x000c},

	{CS4321_LINE_SDS_COMMON_STX0_TX_CONFIG_LOCAL_TIMING, 0, 0x0001},
	{CS4321_LINE_SDS_COMMON_STXP0_TX_CLKOUT_CTRL, 0, 0x0864},
	{CS4321_LINE_SDS_COMMON_STXP0_TX_LOOP_FILTER, 0, 0x0027},
	{CS4321_LINE_SDS_COMMON_STXP0_TX_PWRDN, 0, 0x0000},

	{CS4321_GLOBAL_INGRESS_SOFT_RESET, 0, 0x0003},
	{CS4321_GLOBAL_EGRESS_SOFT_RESET, 0, 0xffff},
	{CS4321_GLOBAL_INGRESS_SOFT_RESET, 0, 0x0000},
	{CS4321_GLOBAL_EGRESS_SOFT_RESET, 0, 0x0000},
	{CS4321_GLOBAL_REF_SOFT_RESET, 0, 0xffff},
	{CS4321_GLOBAL_REF_SOFT_RESET, 0, 0x0000},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_xaui_ingress[] = {
	{CS4321_LINE_SDS_COMMON_SRX0_RX_CONFIG, 0, 0x0000},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_CPA, 0, 0x0057},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_LOOP_FILTER, 0, 0x0007},

	{CS4321_HOST_SDS_COMMON_STX0_TX_CONFIG_LOCAL_TIMING, 0, 0x0001},
	{CS4321_HOST_SDS_COMMON_STXP0_TX_CLKDIV_CTRL, 0, 0x4492},
	{CS4321_HOST_SDS_COMMON_STXP0_TX_CLKOUT_CTRL, 0, 0x1864},
	{CS4321_HOST_SDS_COMMON_STXP0_TX_CONFIG, 0, 0x090c},
	{CS4321_HOST_SDS_COMMON_STXP0_TX_PWRDN, 0, 0x0000},
	{CS4321_HOST_ML_SDS_COMMON_STXP0_TX_CLKDIV_CTRL, 0, 0x4429},
	{CS4321_HOST_ML_SDS_COMMON_STXP0_TX_CONFIG, 0, 0x090c},

	{CS4321_LINE_SDS_COMMON_SRX0_RX_CONFIG, 0, 0x0000},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_CPA, 0, 0x0057},
	{CS4321_LINE_SDS_COMMON_SRX0_RX_LOOP_FILTER, 0, 0x0007},

	{CS4321_GLOBAL_INGRESS_SOFT_RESET, 0, 0x0003},
	{CS4321_GLOBAL_EGRESS_SOFT_RESET, 0, 0xffff},
	{CS4321_GLOBAL_INGRESS_SOFT_RESET, 0, 0x0000},
	{CS4321_GLOBAL_EGRESS_SOFT_RESET, 0, 0x0000},
	{CS4321_GLOBAL_REF_SOFT_RESET, 0, 0xffff},
	{CS4321_GLOBAL_REF_SOFT_RESET, 0, 0x0000},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_ingress_1[] = {
	{CS4321_GLOBAL_INGRESS_SOFT_RESET, 0, 0x0002},
	{CS4321_GLOBAL_INGRESS_SOFT_RESET, 0, 0x0000},
	{CS4321_HOST_SDS_COMMON_STXP0_TX_PWRDN, 0, 0x0000},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_2e[] = {
	{CS4321_GLOBAL_HOST_MULTILANE_CLKSEL, 0, 0x8000},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_2o[] = {
	{CS4321_GLOBAL_HOST_MULTILANE_CLKSEL, 0, 0x8300},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_ingress_3[] = {
	/* Set the device in XAUI mode */
	{CS4321_GLOBAL_HOST_MULTILANE_FUNCEN, 0, 0x0005},

	/* Enable the XGPCS and the Rate Adjust block */
	/* Set fen_radj, rx_fen_xgpcs */
	{CS4321_GLOBAL_INGRESS_FUNCEN, ~0x0081, 0x0081},

	/* Setup the clock enables for the XGPCS and Rate Adjust block */
	/* Set rx_en_radj, rx_en_xgpcs */
	{CS4321_GLOBAL_INGRESS_CLKEN, ~0x0021, 0x0021},

	/* Setup the clock enables for the HIF and the Rate Adjust block */
	/* Set tx_en_hif, tx_en_radj */
	{CS4321_GLOBAL_INGRESS_CLKEN2, ~0x0120, 0x0120},

	{CS4321_GLOBAL_REF_SOFT_RESET, 0, 0xffff},
	{CS4321_GLOBAL_REF_SOFT_RESET, 0, 0x0000},
	/* MPIF DeAssert Ingress Reset */
	{CS4321_GLOBAL_MPIF_RESET_DOTREG, ~0x0004, 0},

	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_ingress_4e[] = {
	{CS4321_XGMAC_LINE_RX_CFG_COM, 0, 0x8010},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_ingress_4o[] = {
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_ingress_5[] = {
	{CS4321_XGMAC_HOST_TX_CFG_TX_IFG, 0, 0x0005},
	{CS4321_XGPCS_LINE_RX_RXCNTRL, 0, 0x5000},
	{CS4321_XGRS_HOST_TX_TXCNTRL, 0, 0xc000},
	{CS4321_GIGEPCS_LINE_CONTROL, 0, 0x0000},
	{CS4321_GIGEPCS_HOST_CONTROL, 0, 0x0000},

	{CS4321_RADJ_INGRESS_RX_NRA_MIN_IFG, 0, 0x0004},
	{CS4321_RADJ_INGRESS_RX_NRA_SETTLE, 0, 0x0000},
	{CS4321_RADJ_INGRESS_TX_ADD_FILL_CTRL, 0, 0xf001},
	{CS4321_RADJ_INGRESS_TX_ADD_FILL_DATA0, 0, 0x0707},
	{CS4321_RADJ_INGRESS_TX_ADD_FILL_DATA1, 0, 0x0707},
	{CS4321_RADJ_INGRESS_TX_PRA_MIN_IFG, 0, 0x0004},
	{CS4321_RADJ_INGRESS_TX_PRA_SETTLE, 0, 0x0000},
	{CS4321_RADJ_INGRESS_MISC_RESET, 0, 0x0000},
	{CS4321_PM_CTRL, 0, 0x0002},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_6e[] = {
	{CS4321_HIF_COMMON_TXCONTROL3, 0, 0x0010},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_6o[] = {
	{CS4321_HIF_COMMON_TXCONTROL3, 0, 0x0011},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_egress_1[] = {
	{CS4321_GLOBAL_EGRESS_SOFT_RESET, 0, 0x1},
	{CS4321_GLOBAL_EGRESS_SOFT_RESET, 0, 0x0000},
	{CS4321_LINE_SDS_COMMON_STXP0_TX_PWRDN, 0, 0x0000},

	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_egress_3[] = {
	/* Set the device in XAUI mode */
	{CS4321_GLOBAL_HOST_MULTILANE_FUNCEN, 0x0005},

	/* Enable the XGPCS and the Rate Adjust block */
	/* Set tx_fen_xgpcs, fen_radj */
	{CS4321_GLOBAL_EGRESS_FUNCEN, ~0x0180, 0x0180},

	/* Setup the clock enables for the HIF and the Rate Adjust block */
	/* Set rx_en_hif, rx_en_radj */
	{CS4321_GLOBAL_EGRESS_CLKEN, ~0x0120, 0x0120},

	/* Setup the clock enables for the XGPCS and Rate Adjust block */
	/* Set tx_en_radj, tx_en_xgpcs */
	{CS4321_GLOBAL_EGRESS_CLKEN2, ~0x0021, 0x0021},

	{CS4321_GLOBAL_REF_SOFT_RESET, 0xffff},
	{CS4321_GLOBAL_REF_SOFT_RESET, 0x0000},

	/* MPIF DeAssert Egress Reset */
	{CS4321_GLOBAL_MPIF_RESET_DOTREG, ~0x0002, 0},

	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_egress_4e[] = {
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_egress_4o[] = {
	{CS4321_XGMAC_LINE_TX_CFG_COM, 0, 0xc000},
	{0}
};

static const struct cs4321_reg_modify cs4321_init_dpath_xaui_pcs_ra_egress_5[] = {
	{CS4321_XGMAC_LINE_TX_CFG_TX_IFG, 0, 0x0005},
	{CS4321_XGPCS_LINE_TX_TXCNTRL, 0, 0x0000},
	{CS4321_XGRS_LINE_TX_TXCNTRL, 0, 0xc000},

	{CS4321_RADJ_EGRESS_RX_NRA_MIN_IFG, 0, 0x0004},
	{CS4321_RADJ_EGRESS_RX_NRA_SETTLE, 0, 0x0000},
	{CS4321_RADJ_EGRESS_TX_ADD_FILL_CTRL, 0, 0xf001},
	{CS4321_RADJ_EGRESS_TX_ADD_FILL_DATA0, 0, 0x0707},
	{CS4321_RADJ_EGRESS_TX_ADD_FILL_DATA1, 0, 0x0707},
	{CS4321_RADJ_EGRESS_TX_PRA_MIN_IFG, 0, 0x0004},
	{CS4321_RADJ_EGRESS_TX_PRA_SETTLE, 0, 0x0000},
	{CS4321_RADJ_EGRESS_MISC_RESET, 0, 0x0000},
	{CS4321_PM_CTRL, 0, 0x0002},
	{0}
};

const struct cs4321_multi_seq cs4321_init_rxaui_seq[] = {
	{0, cs4321_init_prefix_seq},
	{0, cs4321_init_egress_local_timing_rxaui},
	{0, cs4321_init_ingress_local_timing_rxaui},
	{0, cs4321_init_dpath_ingress_rxaui_pcs_ra},
	{0, cs4321_init_dpath_egress_rxaui_pcs_ra},
	{0, cs4321_resync_vcos_xaui_rxaui},
	{0, cs4321_toggle_resets_xaui_rxaui},
	{0, cs4321_hsif_elec_mode_set_sr_pre},
	{0, cs4321_hsif_elec_mode_set_sr_2in},
	{0, cs4321_hsif_elec_mode_set_sr_post},
	{0, cs4321_init_global_timer_156_25},
	{0, cs4321_init_mac_latency},
	{0, cs4321_init_ref_clk_src_xaui_rxaui},
	{0, cs4321_init_polarity_inv},

	{0, NULL}
};

const struct cs4321_multi_seq cs4321_init_xaui_seq[] = {
	{0, cs4321_init_prefix_seq},
	/* Init egress even and odd */
	{0, cs4321_init_xaui_egress},
	{1, cs4321_init_xaui_egress},

	/* Init ingress even and odd */
	{0, cs4321_init_xaui_ingress},
	{1, cs4321_init_xaui_ingress},

	/* dpath ingress even and odd */
	{0, cs4321_init_dpath_xaui_pcs_ra_ingress_1},
	{0, cs4321_init_dpath_xaui_pcs_ra_2e},
	{0, cs4321_init_dpath_xaui_pcs_ra_ingress_3},
	{0, cs4321_init_dpath_xaui_pcs_ra_ingress_4e},
	{0, cs4321_init_dpath_xaui_pcs_ra_ingress_5},
	{0, cs4321_init_dpath_xaui_pcs_ra_6e},


	{1, cs4321_init_dpath_xaui_pcs_ra_ingress_1},
	{1, cs4321_init_dpath_xaui_pcs_ra_2o},
	{1, cs4321_init_dpath_xaui_pcs_ra_ingress_3},
	{1, cs4321_init_dpath_xaui_pcs_ra_ingress_4o},
	{1, cs4321_init_dpath_xaui_pcs_ra_ingress_5},
	{1, cs4321_init_dpath_xaui_pcs_ra_6o},

	/* dpath egress even and odd */
	{0, cs4321_init_dpath_xaui_pcs_ra_egress_1},
	{0, cs4321_init_dpath_xaui_pcs_ra_2e},
	{0, cs4321_init_dpath_xaui_pcs_ra_egress_3},
	{0, cs4321_init_dpath_xaui_pcs_ra_egress_4e},
	{0, cs4321_init_dpath_xaui_pcs_ra_egress_5},
	{0, cs4321_init_dpath_xaui_pcs_ra_6e},

	{1, cs4321_init_dpath_xaui_pcs_ra_egress_1},
	{1, cs4321_init_dpath_xaui_pcs_ra_2o},
	{1, cs4321_init_dpath_xaui_pcs_ra_egress_3},
	{1, cs4321_init_dpath_xaui_pcs_ra_egress_4o},
	{1, cs4321_init_dpath_xaui_pcs_ra_egress_5},
	{1, cs4321_init_dpath_xaui_pcs_ra_6o},

	/* power down the odd slice's line side */
	{1, cs4321_init_line_power_down},

	{0, cs4321_resync_vcos_xaui_rxaui},
	{0, cs4321_toggle_resets_xaui_rxaui},
	{0, cs4321_hsif_elec_mode_set_sr_pre},
	{0, cs4321_hsif_elec_mode_set_sr_2in},
	{0, cs4321_hsif_elec_mode_set_sr_post},
	{0, cs4321_init_global_timer_156_25},
	{0, cs4321_init_mac_latency},
	{0, cs4321_init_ref_clk_src_xaui_rxaui},
	{0, cs4321_init_polarity_inv},

	{0, NULL}
};

static int cs4321_phy_read_x(struct phy_device *phydev, int off, u16 regnum)
{
	return mdiobus_read(phydev->bus, phydev->addr + off,
			    MII_ADDR_C45 | regnum);
}

static int cs4321_phy_write_x(struct phy_device *phydev, int off,
			      u16 regnum, u16 val)
{
	return mdiobus_write(phydev->bus, phydev->addr + off,
			     MII_ADDR_C45 | regnum, val);
}
static int cs4321_phy_read(struct phy_device *phydev, u16 regnum)
{
	return cs4321_phy_read_x(phydev, 0, regnum);
}

static int cs4321_phy_write(struct phy_device *phydev, u16 regnum, u16 val)
{
	return cs4321_phy_write_x(phydev, 0, regnum, val);
}

static int cs4321_write_seq_x(struct phy_device *phydev, int off,
			    const struct cs4321_reg_modify *seq)
{
	int last_reg = -1;
	int last_val = 0;
	int ret = 0;

	while (seq->reg) {
		if (seq->mask_bits) {
			if (last_reg != seq->reg) {
				ret = cs4321_phy_read_x(phydev, off, seq->reg);
				if (ret < 0)
					goto err;
				last_val = ret;
			}
			last_val &= seq->mask_bits;
		} else {
			last_val = 0;
		}
		last_val |= seq->set_bits;
		ret = cs4321_phy_write_x(phydev, off, seq->reg, last_val);
		if (ret < 0)
			goto err;
		seq++;
	}
err:
	return ret;
}

static int cs4321_write_multi_seq(struct phy_device *phydev,
				  const struct cs4321_multi_seq *m)
{
	int ret = 0;

	while (m->seq) {
		ret = cs4321_write_seq_x(phydev, m->reg_offset, m->seq);
		if (ret)
			goto err;
		m++;
	}

err:
	return ret;
}

static int cs4321_write_seq(struct phy_device *phydev,
			    const struct cs4321_reg_modify *seq)
{
	return cs4321_write_seq_x(phydev, 0, seq);
}

static int cs4321_write_multi_reg(struct phy_device *phydev, u16 *dat, int cnt)
{
	int i;
	int ret = 0;
	for (i = 0; i < (cnt * 2); i += 2) {
		u16 reg = dat[i];
		u16 val = dat[i + 1];
		ret = cs4321_phy_write(phydev, reg, val);
		if (ret)
			break;
	}
	return ret;
}

static int cs4321_write_microcode_slice(struct phy_device *phydev, int s)
{
	int i;
	int ret;

	ret = cs4321_phy_write(phydev, 0x024f, s);
	if (ret)
		return ret;

	for (i = 0; i < 512; i++) {
		ret = cs4321_phy_write(phydev,
				       0x0201,
				       cs4321_microcode_slices[s][2 * i + 0]);
		if (ret)
			return ret;

		ret = cs4321_phy_write(phydev,
				       0x0202,
				       cs4321_microcode_slices[s][2 * i + 1]);
		if (ret)
			return ret;
		ret = cs4321_phy_write(phydev, 0x0200, 0x9000 + i);
		if (ret)
			return ret;
	}
	return 0;
}

static int cs4321_write_microcode(struct phy_device *phydev)
{
	int i;
	int ret;

	ret = cs4321_write_multi_reg(phydev,
				     cs4321_microcode_prolog,
				     ARRAY_SIZE(cs4321_microcode_prolog) / 2);
	if (ret)
		return ret;

	for (i = 0; i < 8; i++) {
		ret = cs4321_write_microcode_slice(phydev, i);
		if (ret)
			return ret;
	}
	ret = cs4321_write_multi_reg(phydev,
				     cs4321_microcode_epilog,
				     ARRAY_SIZE(cs4321_microcode_epilog) / 2);
	return ret;
}

static int cs4321_reset(struct phy_device *phydev)
{
	int ret;
	int retry;

	ret = cs4321_phy_write(phydev, CS4321_GLOBAL_MPIF_SOFT_RESET, 0xdead);
	if (ret)
		goto err;

	msleep(100);

	/* Disable eeprom loading */
	ret = cs4321_phy_write(phydev, CS4321_EEPROM_LOADER_CONTROL, 2);
	if (ret)
		goto err;

	retry = 0;
	do {
		if (retry > 0)
			mdelay(1);
		ret = cs4321_phy_read(phydev, CS4321_EEPROM_LOADER_STATUS);
		if (ret < 0)
			goto err;
		retry++;
	} while ((ret & 4) == 0 && retry < 10);

	if ((ret & 4) == 0) {
		ret = -ENXIO;
		goto err;
	}

	ret = cs4321_write_seq(phydev, cs4321_soft_reset_registers);
	if (ret)
		goto err;

	ret = cs4321_write_microcode(phydev);
	if (ret)
		goto err;


	ret = cs4321_write_seq(phydev, cs4321_68xx_4_nic_init);
	if (ret)
		goto err;

err:
	return ret;
}

int cs4321_init_rxaui(struct phy_device *phydev)
{
	return cs4321_write_multi_seq(phydev,
				      cs4321_init_rxaui_seq);
}

int cs4321_init_xaui(struct phy_device *phydev)
{
	return cs4321_write_multi_seq(phydev,
				      cs4321_init_xaui_seq);

}

int cs4321_config_init(struct phy_device *phydev)
{
	int ret;
	struct cs4321_private *p = phydev->priv;
	const struct cs4321_multi_seq *init_seq;

	ret = cs4321_reset(phydev);
	if (ret)
		goto err;

	init_seq = (p->mode == XAUI) ?
		cs4321_init_xaui_seq : cs4321_init_rxaui_seq;

	ret = cs4321_write_multi_seq(phydev, init_seq);

	phydev->state = PHY_NOLINK;

err:
	return ret;
}

int cs4321_probe(struct phy_device *phydev)
{
	int ret = 0;
	int id_lsb, id_msb;
	enum cs4321_host_mode host_mode;
	const char *prop_val;
	struct cs4321_private *p;
	/*
	 * CS4312 keeps its ID values in non-standard registers, make
	 * sure we are talking to what we think we are.
	 */
	id_lsb = cs4321_phy_read(phydev, CS4321_GLOBAL_CHIP_ID_LSB);
	if (id_lsb < 0) {
		ret = id_lsb;
		goto err;
	}

	id_msb = cs4321_phy_read(phydev, CS4321_GLOBAL_CHIP_ID_MSB);
	if (id_msb < 0) {
		ret = id_msb;
		goto err;
	}

	if (id_lsb != 0x23E5 || id_msb != 0x1002) {
		ret = -ENODEV;
		goto err;
	}
	ret = of_property_read_string(phydev->dev.of_node,
				      "cortina,host-mode", &prop_val);
	if (ret)
		goto err;

	if (strcmp(prop_val, "rxaui") == 0)
		host_mode = RXAUI;
	else if (strcmp(prop_val, "xaui") == 0)
		host_mode = XAUI;
	else {
		dev_err(&phydev->dev,
			"Invalid \"cortina,host-mode\" property: \"%s\"\n",
			prop_val);
		ret = -EINVAL;
		goto err;
	}
	p = devm_kzalloc(&phydev->dev, sizeof(*p), GFP_KERNEL);
	if (!p) {
		ret = -ENOMEM;
		goto err;
	}
	p->mode = host_mode;
	phydev->priv = p;
err:
	return ret;
}

int cs4321_config_aneg(struct phy_device *phydev)
{
	return -EINVAL;
}

int cs4321_read_status(struct phy_device *phydev)
{
	int gpio_int_status;
	int ret = 0;

	gpio_int_status = cs4321_phy_read(phydev, CS4321_GPIO_GPIO_INTS);
	if (gpio_int_status < 0) {
		ret = gpio_int_status;
		goto err;
	}
	if (gpio_int_status & 0x8) {
		/* Up when edc_convergedS set. */
		phydev->speed = 10000;
		phydev->duplex = 1;
		phydev->link = 1;
	} else {
		phydev->link = 0;
	}

err:
	return ret;
}

static struct of_device_id cs4321_match[] = {
	{
		.compatible = "cortina,cs4321",
	},
	{
		.compatible = "cortina,cs4318",
	},
	{},
};
MODULE_DEVICE_TABLE(of, cs4321_match);

static struct phy_driver cs4321_phy_driver = {
	.phy_id		= 0xffffffff,
	.phy_id_mask	= 0xffffffff,
	.name		= "Cortina CS4321",
	.config_init	= cs4321_config_init,
	.probe		= cs4321_probe,
	.config_aneg	= cs4321_config_aneg,
	.read_status	= cs4321_read_status,
	.driver		= {
		.owner = THIS_MODULE,
		.of_match_table = cs4321_match,
	},
};

static int __init cs4321_drv_init(void)
{
	int ret;

	ret = phy_driver_register(&cs4321_phy_driver);

	return ret;
}
module_init(cs4321_drv_init);

static void __exit cs4321_drv_exit(void)
{
	phy_driver_unregister(&cs4321_phy_driver);
}
module_exit(cs4321_drv_exit);
