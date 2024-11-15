// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>

#include "iris_hfi.h"
#include "vpu_iris3.h"
#include "vpu_iris3_buffer.h"
#include "vpu_iris3_power.h"

#define VIDEO_ARCH_LX 1

#define VCODEC_BASE_OFFS_IRIS3                 0x00000000
#define AON_MVP_NOC_RESET                      0x0001F000
#define CPU_BASE_OFFS_IRIS3                    0x000A0000
#define AON_BASE_OFFS                          0x000E0000
#define CPU_CS_BASE_OFFS_IRIS3                 (CPU_BASE_OFFS_IRIS3)
#define CPU_IC_BASE_OFFS_IRIS3                 (CPU_BASE_OFFS_IRIS3)

#define CPU_CS_A2HSOFTINTCLR_IRIS3             (CPU_CS_BASE_OFFS_IRIS3 + 0x1C)
#define CPU_CS_VCICMDARG0_IRIS3                (CPU_CS_BASE_OFFS_IRIS3 + 0x24)
#define CPU_CS_VCICMDARG1_IRIS3                (CPU_CS_BASE_OFFS_IRIS3 + 0x28)
/* HFI_CTRL_INIT */
#define CPU_CS_SCIACMD_IRIS3                   (CPU_CS_BASE_OFFS_IRIS3 + 0x48)
/* HFI_CTRL_STATUS */
#define CPU_CS_SCIACMDARG0_IRIS3               (CPU_CS_BASE_OFFS_IRIS3 + 0x4C)
/* HFI_QTBL_INFO */
#define CPU_CS_SCIACMDARG1_IRIS3               (CPU_CS_BASE_OFFS_IRIS3 + 0x50)
/* HFI_QTBL_ADDR */
#define CPU_CS_SCIACMDARG2_IRIS3               (CPU_CS_BASE_OFFS_IRIS3 + 0x54)
/* SFR_ADDR */
#define CPU_CS_SCIBCMD_IRIS3                   (CPU_CS_BASE_OFFS_IRIS3 + 0x5C)
#define CPU_CS_SCIBCMDARG0_IRIS3               (CPU_CS_BASE_OFFS_IRIS3 + 0x60)
/* UC_REGION_ADDR */
#define CPU_CS_SCIBARG1_IRIS3                  (CPU_CS_BASE_OFFS_IRIS3 + 0x64)
/* UC_REGION_ADDR */
#define CPU_CS_SCIBARG2_IRIS3                  (CPU_CS_BASE_OFFS_IRIS3 + 0x68)
#define CPU_CS_H2XSOFTINTEN_IRIS3              (CPU_CS_BASE_OFFS_IRIS3 + 0x148)
#define CPU_CS_AHB_BRIDGE_SYNC_RESET           (CPU_CS_BASE_OFFS_IRIS3 + 0x160)
#define CPU_CS_X2RPMH_IRIS3                    (CPU_CS_BASE_OFFS_IRIS3 + 0x168)

#define CPU_IC_SOFTINT_IRIS3                   (CPU_IC_BASE_OFFS_IRIS3 + 0x150)
#define CPU_IC_SOFTINT_H2A_SHFT_IRIS3          0x0

#define CPU_CS_SCIACMDARG0_HFI_CTRL_INIT_IDLE_MSG_BMSK_IRIS3     0x40000000
#define CPU_CS_SCIACMDARG0_HFI_CTRL_ERROR_STATUS_BMSK_IRIS3	     0xfe
#define CPU_CS_SCIACMDARG0_HFI_CTRL_PC_READY_IRIS3               0x100

#define AON_WRAPPER_MVP_NOC_RESET_REQ          (AON_MVP_NOC_RESET + 0x000)
#define AON_WRAPPER_MVP_NOC_RESET_ACK          (AON_MVP_NOC_RESET + 0x004)

#define WRAPPER_BASE_OFFS_IRIS3                0x000B0000
#define WRAPPER_INTR_STATUS_IRIS3              (WRAPPER_BASE_OFFS_IRIS3 + 0x0C)
#define WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS3   0x8
#define WRAPPER_INTR_STATUS_A2H_BMSK_IRIS3     0x4

#define WRAPPER_INTR_MASK_IRIS3	               (WRAPPER_BASE_OFFS_IRIS3 + 0x10)
#define WRAPPER_INTR_MASK_A2HWD_BMSK_IRIS3     0x8
#define WRAPPER_INTR_MASK_A2HCPU_BMSK_IRIS3    0x4

#define WRAPPER_DEBUG_BRIDGE_LPI_CONTROL_IRIS3 (WRAPPER_BASE_OFFS_IRIS3 + 0x54)
#define WRAPPER_DEBUG_BRIDGE_LPI_STATUS_IRIS3  (WRAPPER_BASE_OFFS_IRIS3 + 0x58)
#define WRAPPER_IRIS_CPU_NOC_LPI_CONTROL       (WRAPPER_BASE_OFFS_IRIS3 + 0x5C)
#define WRAPPER_IRIS_CPU_NOC_LPI_STATUS	       (WRAPPER_BASE_OFFS_IRIS3 + 0x60)
#define WRAPPER_CORE_POWER_STATUS              (WRAPPER_BASE_OFFS_IRIS3 + 0x80)
#define WRAPPER_CORE_CLOCK_CONFIG_IRIS3        (WRAPPER_BASE_OFFS_IRIS3 + 0x88)

#define WRAPPER_TZ_BASE_OFFS                   0x000C0000
#define WRAPPER_TZ_CPU_STATUS                  (WRAPPER_TZ_BASE_OFFS + 0x10)
#define WRAPPER_TZ_CTL_AXI_CLOCK_CONFIG        (WRAPPER_TZ_BASE_OFFS + 0x14)
#define WRAPPER_TZ_QNS4PDXFIFO_RESET           (WRAPPER_TZ_BASE_OFFS + 0x18)

#define CTRL_INIT_IRIS3                        CPU_CS_SCIACMD_IRIS3
#define CTRL_STATUS_IRIS3                      CPU_CS_SCIACMDARG0_IRIS3
#define CTRL_ERROR_STATUS__M_IRIS3             CPU_CS_SCIACMDARG0_HFI_CTRL_ERROR_STATUS_BMSK_IRIS3
#define CTRL_INIT_IDLE_MSG_BMSK_IRIS3          CPU_CS_SCIACMDARG0_HFI_CTRL_INIT_IDLE_MSG_BMSK_IRIS3
#define CTRL_STATUS_PC_READY_IRIS3             CPU_CS_SCIACMDARG0_HFI_CTRL_PC_READY_IRIS3

#define QTBL_INFO_IRIS3                        CPU_CS_SCIACMDARG1_IRIS3
#define QTBL_ADDR_IRIS3                        CPU_CS_SCIACMDARG2_IRIS3
#define SFR_ADDR_IRIS3                         CPU_CS_SCIBCMD_IRIS3
#define UC_REGION_ADDR_IRIS3                   CPU_CS_SCIBARG1_IRIS3
#define UC_REGION_SIZE_IRIS3                   CPU_CS_SCIBARG2_IRIS3

#define AON_WRAPPER_MVP_NOC_LPI_CONTROL        (AON_BASE_OFFS)
#define AON_WRAPPER_MVP_NOC_LPI_STATUS         (AON_BASE_OFFS + 0x4)

#define VCODEC_SS_IDLE_STATUSN                 (VCODEC_BASE_OFFS_IRIS3 + 0x70)

static int interrupt_init_iris3(struct iris_core *core)
{
	u32 mask_val;
	int ret;

	ret = read_register(core, WRAPPER_INTR_MASK_IRIS3, &mask_val);
	if (ret)
		return ret;

	mask_val &= ~(WRAPPER_INTR_MASK_A2HWD_BMSK_IRIS3 |
		      WRAPPER_INTR_MASK_A2HCPU_BMSK_IRIS3);
	ret = write_register(core, WRAPPER_INTR_MASK_IRIS3, mask_val);

	return ret;
}

static int setup_ucregion_memory_map_iris3(struct iris_core *core)
{
	int ret;
	u32 value;

	value = (u32)core->iface_q_table.device_addr;
	ret = write_register(core, UC_REGION_ADDR_IRIS3, value);
	if (ret)
		return ret;

	value = SHARED_QSIZE;
	ret = write_register(core, UC_REGION_SIZE_IRIS3, value);
	if (ret)
		return ret;

	value = (u32)core->iface_q_table.device_addr;
	ret = write_register(core, QTBL_ADDR_IRIS3, value);
	if (ret)
		return ret;

	ret = write_register(core, QTBL_INFO_IRIS3, 0x01);
	if (ret)
		return ret;

	value = (u32)((u64)core->iface_q_table.kernel_vaddr);
	ret = write_register(core, CPU_CS_VCICMDARG0_IRIS3, value);
	if (ret)
		return ret;

	value = (u32)((u64)core->iface_q_table.kernel_vaddr >> 32);
	ret = write_register(core, CPU_CS_VCICMDARG1_IRIS3, value);
	if (ret)
		return ret;

	if (core->sfr.device_addr) {
		value = (u32)core->sfr.device_addr + VIDEO_ARCH_LX;
		ret = write_register(core, SFR_ADDR_IRIS3, value);
		if (ret)
			return ret;
	}

	return ret;
}

static int boot_firmware_iris3(struct iris_core *core)
{
	u32 ctrl_init = 0, ctrl_status = 0, count = 0, max_tries = 1000;
	int ret;

	ret = setup_ucregion_memory_map_iris3(core);
	if (ret)
		return ret;

	ctrl_init = BIT(0);

	ret = write_register(core, CTRL_INIT_IRIS3, ctrl_init);
	if (ret)
		return ret;

	while (!ctrl_status && count < max_tries) {
		ret = read_register(core, CTRL_STATUS_IRIS3, &ctrl_status);
		if (ret)
			return ret;

		if ((ctrl_status & CTRL_ERROR_STATUS__M_IRIS3) == 0x4) {
			dev_err(core->dev, "invalid setting for UC_REGION\n");
			break;
		}

		usleep_range(50, 100);
		count++;
	}

	if (count >= max_tries) {
		dev_err(core->dev, "Error booting up vidc firmware\n");
		return -ETIME;
	}

	ret = write_register(core, CPU_CS_H2XSOFTINTEN_IRIS3, 0x1);
	if (ret)
		return ret;

	ret = write_register(core, CPU_CS_X2RPMH_IRIS3, 0x0);

	return ret;
}

static int raise_interrupt_iris3(struct iris_core *core)
{
	return write_register(core, CPU_IC_SOFTINT_IRIS3, 1 << CPU_IC_SOFTINT_H2A_SHFT_IRIS3);
}

static int clear_interrupt_iris3(struct iris_core *core)
{
	u32 intr_status = 0, mask = 0;
	int ret;

	ret = read_register(core, WRAPPER_INTR_STATUS_IRIS3, &intr_status);
	if (ret)
		return ret;

	mask = (WRAPPER_INTR_STATUS_A2H_BMSK_IRIS3 |
		WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS3 |
		CTRL_INIT_IDLE_MSG_BMSK_IRIS3);

	if (intr_status & mask) {
		core->intr_status |= intr_status;
		core->reg_count++;
	} else {
		core->spur_count++;
	}

	ret = write_register(core, CPU_CS_A2HSOFTINTCLR_IRIS3, 1);

	return ret;
}

static int watchdog_iris3(struct iris_core *core, u32 intr_status)
{
	if (intr_status & WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS3) {
		dev_err(core->dev, "%s: received watchdog interrupt\n", __func__);
		return -ETIME;
	}

	return 0;
}

static bool is_iris3_hw_power_collapsed(struct iris_core *core)
{
	u32 value = 0, pwr_status = 0;
	int ret;

	ret = read_register(core, WRAPPER_CORE_POWER_STATUS, &value);
	if (ret)
		return false;

	pwr_status = value & BIT(1);

	return pwr_status ? false : true;
}

static int power_off_iris3_hardware(struct iris_core *core)
{
	u32 value = 0;
	int ret, i;

	if (is_iris3_hw_power_collapsed(core))
		goto disable_power;

	dev_err(core->dev, "Video hw is power ON\n");

	ret = read_register(core, WRAPPER_CORE_CLOCK_CONFIG_IRIS3, &value);
	if (ret)
		goto disable_power;

	if (value) {
		ret = write_register(core, WRAPPER_CORE_CLOCK_CONFIG_IRIS3, 0);
		if (ret)
			goto disable_power;
	}

	for (i = 0; i < core->cap[NUM_VPP_PIPE].value; i++) {
		ret = read_register_with_poll_timeout(core, VCODEC_SS_IDLE_STATUSN + 4 * i,
						      0x400000, 0x400000, 2000, 20000);
	}

	ret = write_register(core, AON_WRAPPER_MVP_NOC_RESET_REQ, 0x3);
	if (ret)
		goto disable_power;

	ret = read_register_with_poll_timeout(core, AON_WRAPPER_MVP_NOC_RESET_ACK,
					      0x3, 0x3, 200, 2000);
	ret = write_register(core, AON_WRAPPER_MVP_NOC_RESET_REQ, 0x0);
	if (ret)
		goto disable_power;

	ret = read_register_with_poll_timeout(core, AON_WRAPPER_MVP_NOC_RESET_ACK,
					      0x3, 0x0, 200, 2000);

	ret = write_register(core, CPU_CS_AHB_BRIDGE_SYNC_RESET, 0x3);
	if (ret)
		goto disable_power;
	ret = write_register(core, CPU_CS_AHB_BRIDGE_SYNC_RESET, 0x2);
	if (ret)
		goto disable_power;
	ret = write_register(core, CPU_CS_AHB_BRIDGE_SYNC_RESET, 0x0);
	if (ret)
		goto disable_power;

disable_power:
	ret = disable_power_domains(core, "vcodec");
	if (ret) {
		dev_err(core->dev, "disable power domain vcodec failed\n");
		ret = 0;
	}

	disable_unprepare_clock(core, "vcodec_core");
	if (ret) {
		dev_err(core->dev, "disable unprepare vcodec_core failed\n");
		ret = 0;
	}

	return ret;
}

static int power_off_iris3_controller(struct iris_core *core)
{
	int ret;

	ret = write_register(core, CPU_CS_X2RPMH_IRIS3, 0x3);
	if (ret)
		goto disable_power;

	ret = write_register_masked(core, AON_WRAPPER_MVP_NOC_LPI_CONTROL,
				    0x1, BIT(0));
	if (ret)
		goto disable_power;

	ret = read_register_with_poll_timeout(core, AON_WRAPPER_MVP_NOC_LPI_STATUS,
					      0x1, 0x1, 200, 2000);

	ret = write_register_masked(core, WRAPPER_IRIS_CPU_NOC_LPI_CONTROL,
				    0x1, BIT(0));
	if (ret)
		goto disable_power;

	ret = read_register_with_poll_timeout(core, WRAPPER_IRIS_CPU_NOC_LPI_STATUS,
					      0x1, 0x1, 200, 2000);

	ret = write_register(core, WRAPPER_DEBUG_BRIDGE_LPI_CONTROL_IRIS3, 0x0);
	if (ret)
		goto disable_power;

	ret = read_register_with_poll_timeout(core, WRAPPER_DEBUG_BRIDGE_LPI_STATUS_IRIS3,
					      0xffffffff, 0x0, 200, 2000);

	ret = write_register(core, WRAPPER_TZ_CTL_AXI_CLOCK_CONFIG, 0x3);
	if (ret)
		goto disable_power;

	ret = write_register(core, WRAPPER_TZ_QNS4PDXFIFO_RESET, 0x1);
	if (ret)
		goto disable_power;

	ret = write_register(core, WRAPPER_TZ_QNS4PDXFIFO_RESET, 0x0);
	if (ret)
		goto disable_power;

	ret = write_register(core, WRAPPER_TZ_CTL_AXI_CLOCK_CONFIG, 0x0);
	if (ret)
		goto disable_power;

disable_power:
	disable_unprepare_clock(core, "core_clk");
	if (ret) {
		dev_err(core->dev, "disable unprepare core_clk failed\n");
		ret = 0;
	}

	/* power down process */
	ret = disable_power_domains(core, "iris-ctl");
	if (ret) {
		dev_err(core->dev, "disable power domain iris-ctl failed\n");
		ret = 0;
	}

	return ret;
}

static int power_off_iris3(struct iris_core *core)
{
	if (!core->power_enabled)
		return 0;

	opp_set_rate(core, 0);
	power_off_iris3_hardware(core);
	power_off_iris3_controller(core);
	unvote_buses(core);

	if (!call_vpu_op(core, watchdog, core, core->intr_status))
		disable_irq_nosync(core->irq);

	core->power_enabled = false;

	return 0;
}

static int power_on_iris3_controller(struct iris_core *core)
{
	int ret;

	ret = enable_power_domains(core, "iris-ctl");
	if (ret)
		return ret;

	ret = reset_ahb2axi_bridge(core);
	if (ret)
		goto err_disable_power;

	ret = prepare_enable_clock(core, "gcc_video_axi0");
	if (ret)
		goto err_disable_power;

	ret = prepare_enable_clock(core, "core_clk");
	if (ret)
		goto err_disable_clock;

	return ret;

err_disable_clock:
	disable_unprepare_clock(core, "gcc_video_axi0");
err_disable_power:
	disable_power_domains(core, "iris-ctl");

	return ret;
}

static int power_on_iris3_hardware(struct iris_core *core)
{
	int ret;

	ret = enable_power_domains(core, "vcodec");
	if (ret)
		return ret;

	ret = prepare_enable_clock(core, "vcodec_core");
	if (ret)
		goto err_disable_power;

	return ret;

err_disable_power:
	disable_power_domains(core, "vcodec");

	return ret;
}

static int power_on_iris3(struct iris_core *core)
{
	u32 freq = 0;
	int ret;

	if (core->power_enabled)
		return 0;

	if (!core_in_valid_state(core))
		return -EINVAL;

	ret = vote_buses(core, INT_MAX);
	if (ret)
		goto err;

	ret = power_on_iris3_controller(core);
	if (ret)
		goto err_unvote_bus;

	ret = power_on_iris3_hardware(core);
	if (ret)
		goto err_power_off_ctrl;

	core->power_enabled = true;

	freq = core->power.clk_freq ? core->power.clk_freq :
				      (u32)ULONG_MAX;

	opp_set_rate(core, freq);

	set_preset_registers(core);

	interrupt_init_iris3(core);
	core->intr_status = 0;
	enable_irq(core->irq);

	return ret;

err_power_off_ctrl:
	power_off_iris3_controller(core);
err_unvote_bus:
	unvote_buses(core);
err:
	core->power_enabled = false;

	return ret;
}

static int prepare_pc_iris3(struct iris_core *core)
{
	u32 wfi_status = 0, idle_status = 0, pc_ready = 0;
	u32 ctrl_status = 0;
	int ret;

	ret = read_register(core, CTRL_STATUS_IRIS3, &ctrl_status);
	if (ret)
		return ret;

	pc_ready = ctrl_status & CTRL_STATUS_PC_READY_IRIS3;
	idle_status = ctrl_status & BIT(30);

	if (pc_ready)
		return 0;

	ret = read_register(core, WRAPPER_TZ_CPU_STATUS, &wfi_status);
	if (ret)
		return ret;

	wfi_status &= BIT(0);
	if (!wfi_status || !idle_status)
		goto skip_power_off;

	ret = prepare_pc(core);
	if (ret)
		goto skip_power_off;

	ret = read_register_with_poll_timeout(core, CTRL_STATUS_IRIS3,
					      CTRL_STATUS_PC_READY_IRIS3,
					      CTRL_STATUS_PC_READY_IRIS3, 250, 2500);
	if (ret)
		goto skip_power_off;

	ret = read_register_with_poll_timeout(core, WRAPPER_TZ_CPU_STATUS,
					      BIT(0), 0x1, 250, 2500);
	if (ret)
		goto skip_power_off;

	return ret;

skip_power_off:
	ret = read_register(core, CTRL_STATUS_IRIS3, &ctrl_status);
	if (ret)
		return ret;

	ret = read_register(core, WRAPPER_TZ_CPU_STATUS, &wfi_status);
	if (ret)
		return ret;

	wfi_status &= BIT(0);
	dev_err(core->dev, "Skip PC, wfi=%#x, idle=%#x, pcr=%#x, ctrl=%#x)\n",
		wfi_status, idle_status, pc_ready, ctrl_status);

	return -EAGAIN;
}

static const struct vpu_ops iris3_ops = {
	.boot_firmware = boot_firmware_iris3,
	.raise_interrupt = raise_interrupt_iris3,
	.clear_interrupt = clear_interrupt_iris3,
	.watchdog = watchdog_iris3,
	.power_on = power_on_iris3,
	.power_off = power_off_iris3,
	.prepare_pc = prepare_pc_iris3,
};

static const struct vpu_session_ops iris3_session_ops = {
	.int_buf_size = iris_int_buf_size_iris3,
	.calc_freq = iris_calc_freq_iris3,
	.calc_bw = iris_calc_bw_iris3,
};

int init_iris3(struct iris_core *core)
{
	core->vpu_ops = &iris3_ops;
	core->session_ops = &iris3_session_ops;

	return 0;
}
