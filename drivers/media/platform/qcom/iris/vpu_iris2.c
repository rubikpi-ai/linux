// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/iopoll.h>

#include "iris_hfi.h"
#include "vpu_iris2.h"
#include "vpu_iris2_buffer.h"
#include "vpu_iris2_power.h"

#define VIDEO_ARCH_LX 1

#define VCODEC_BASE_OFFS_IRIS2                 0x00000000
#define AON_MVP_NOC_RESET                      0x0001F000
#define CPU_BASE_OFFS_IRIS2                    0x000A0000
#define AON_BASE_OFFS                          0x000E0000
#define CPU_CS_BASE_OFFS_IRIS2                 (CPU_BASE_OFFS_IRIS2)
#define CPU_IC_BASE_OFFS_IRIS2                 (CPU_BASE_OFFS_IRIS2)

#define CPU_CS_A2HSOFTINTCLR_IRIS2             (CPU_CS_BASE_OFFS_IRIS2 + 0x1C)
#define CPU_CS_VCICMDARG0_IRIS2                (CPU_CS_BASE_OFFS_IRIS2 + 0x24)
#define CPU_CS_VCICMDARG1_IRIS2                (CPU_CS_BASE_OFFS_IRIS2 + 0x28)
/* HFI_CTRL_INIT */
#define CPU_CS_SCIACMD_IRIS2                   (CPU_CS_BASE_OFFS_IRIS2 + 0x48)
/* HFI_CTRL_STATUS */
#define CPU_CS_SCIACMDARG0_IRIS2               (CPU_CS_BASE_OFFS_IRIS2 + 0x4C)

/* HFI_QTBL_INFO */
#define CPU_CS_SCIACMDARG1_IRIS2               (CPU_CS_BASE_OFFS_IRIS2 + 0x50)

/* HFI_QTBL_ADDR */
#define CPU_CS_SCIACMDARG2_IRIS2               (CPU_CS_BASE_OFFS_IRIS2 + 0x54)

/* SFR_ADDR */
#define CPU_CS_SCIBCMD_IRIS2                   (CPU_CS_BASE_OFFS_IRIS2 + 0x5C)
#define CPU_CS_SCIBCMDARG0_IRIS2               (CPU_CS_BASE_OFFS_IRIS2 + 0x60)

/* UC_REGION_ADDR */
#define CPU_CS_SCIBARG1_IRIS2                  (CPU_CS_BASE_OFFS_IRIS2 + 0x64)

/* UC_REGION_ADDR */
#define CPU_CS_SCIBARG2_IRIS2                  (CPU_CS_BASE_OFFS_IRIS2 + 0x68)
#define CPU_CS_H2XSOFTINTEN_IRIS2              (CPU_CS_BASE_OFFS_IRIS2 + 0x148)
#define CPU_CS_AHB_BRIDGE_SYNC_RESET           (CPU_CS_BASE_OFFS_IRIS2 + 0x160)
#define CPU_CS_X2RPMH_IRIS2                    (CPU_CS_BASE_OFFS_IRIS2 + 0x168)

#define CPU_IC_SOFTINT_IRIS2                   (CPU_IC_BASE_OFFS_IRIS2 + 0x150)
#define CPU_IC_SOFTINT_H2A_SHFT_IRIS2          0x0

#define CPU_CS_SCIACMDARG0_HFI_CTRL_INIT_IDLE_MSG_BMSK_IRIS2     0x40000000
#define CPU_CS_SCIACMDARG0_HFI_CTRL_ERROR_STATUS_BMSK_IRIS2      0xfe
#define CPU_CS_SCIACMDARG0_HFI_CTRL_PC_READY_IRIS2               0x100

#define AON_WRAPPER_MVP_NOC_RESET_REQ          (AON_MVP_NOC_RESET + 0x000)
#define AON_WRAPPER_MVP_NOC_RESET_ACK          (AON_MVP_NOC_RESET + 0x004)

#define WRAPPER_BASE_OFFS_IRIS2                0x000B0000
#define WRAPPER_CORE_POWER_STATUS              (WRAPPER_BASE_OFFS_IRIS2 + 0x80)
#define WRAPPER_INTR_STATUS_IRIS2              (WRAPPER_BASE_OFFS_IRIS2 + 0x0C)
#define WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS2   0x8
#define WRAPPER_INTR_STATUS_A2H_BMSK_IRIS2     0x4

#define WRAPPER_INTR_MASK_IRIS2                (WRAPPER_BASE_OFFS_IRIS2 + 0x10)
#define WRAPPER_INTR_MASK_A2HWD_BMSK_IRIS2     0x8
#define WRAPPER_INTR_MASK_A2HCPU_BMSK_IRIS2    0x4

#define WRAPPER_DEBUG_BRIDGE_LPI_CONTROL_IRIS2 (WRAPPER_BASE_OFFS_IRIS2 + 0x54)
#define WRAPPER_DEBUG_BRIDGE_LPI_STATUS_IRIS2  (WRAPPER_BASE_OFFS_IRIS2 + 0x58)
#define WRAPPER_CORE_CLOCK_CONFIG_IRIS2        (WRAPPER_BASE_OFFS_IRIS2 + 0x88)

#define WRAPPER_TZ_BASE_OFFS                   0x000C0000
#define WRAPPER_TZ_CPU_STATUS                  (WRAPPER_TZ_BASE_OFFS + 0x10)

#define CTRL_INIT_IRIS2                        CPU_CS_SCIACMD_IRIS2
#define CTRL_STATUS_IRIS2                      CPU_CS_SCIACMDARG0_IRIS2
#define CTRL_ERROR_STATUS__M_IRIS2             CPU_CS_SCIACMDARG0_HFI_CTRL_ERROR_STATUS_BMSK_IRIS2
#define CTRL_INIT_IDLE_MSG_BMSK_IRIS2          CPU_CS_SCIACMDARG0_HFI_CTRL_INIT_IDLE_MSG_BMSK_IRIS2
#define CTRL_STATUS_PC_READY_IRIS2             CPU_CS_SCIACMDARG0_HFI_CTRL_PC_READY_IRIS2

#define QTBL_INFO_IRIS2                        CPU_CS_SCIACMDARG1_IRIS2
#define QTBL_ADDR_IRIS2                        CPU_CS_SCIACMDARG2_IRIS2
#define SFR_ADDR_IRIS2                         CPU_CS_SCIBCMD_IRIS2
#define UC_REGION_ADDR_IRIS2                   CPU_CS_SCIBARG1_IRIS2
#define UC_REGION_SIZE_IRIS2                   CPU_CS_SCIBARG2_IRIS2
#define WRAPPER_CORE_POWER_STATUS              0xB0080
#define WRAPPER_CORE_POWER_CONTROL             0xB0084

#define VCODEC_SS_IDLE_STATUSN                 (VCODEC_BASE_OFFS_IRIS2 + 0x70)

static int interrupt_init_iris2(struct iris_core *core)
{
	u32 mask_val;
	int ret;

	ret = read_register(core, WRAPPER_INTR_MASK_IRIS2, &mask_val);
	if (ret)
		return ret;

	mask_val &= ~(WRAPPER_INTR_MASK_A2HWD_BMSK_IRIS2 |
			WRAPPER_INTR_MASK_A2HCPU_BMSK_IRIS2);
	ret = write_register(core, WRAPPER_INTR_MASK_IRIS2, mask_val);

	return ret;
}

static int setup_ucregion_memory_map_iris2(struct iris_core *core)
{
	int ret;
	u32 value;

	value = (u32)core->iface_q_table.device_addr;
	ret = write_register(core, UC_REGION_ADDR_IRIS2, value);
	if (ret)
		return ret;

	value = SHARED_QSIZE;
	ret = write_register(core, UC_REGION_SIZE_IRIS2, value);
	if (ret)
		return ret;

	value = (u32)core->iface_q_table.device_addr;
	ret = write_register(core, QTBL_ADDR_IRIS2, value);
	if (ret)
		return ret;

	ret = write_register(core, QTBL_INFO_IRIS2, 0x01);
	if (ret)
		return ret;

	value = (u32)((u64)core->iface_q_table.kernel_vaddr);
	ret = write_register(core, CPU_CS_VCICMDARG0_IRIS2, value);
	if (ret)
		return ret;

	value = (u32)((u64)core->iface_q_table.kernel_vaddr >> 32);
	ret = write_register(core, CPU_CS_VCICMDARG1_IRIS2, value);
	if (ret)
		return ret;

	if (core->sfr.device_addr) {
		value = (u32)core->sfr.device_addr + VIDEO_ARCH_LX;
		ret = write_register(core, SFR_ADDR_IRIS2, value);
		if (ret)
			return ret;
	}

	return ret;
}

static int switch_vcodec_gdsc_mode(struct iris_core *core, bool sw_mode)
{
	void __iomem *base_addr;
	u32 val = 0;
	int ret;

	base_addr = core->reg_base;

	if (sw_mode) {
		writel_relaxed(0, base_addr + WRAPPER_CORE_POWER_CONTROL);
		ret = readl_relaxed_poll_timeout(base_addr + WRAPPER_CORE_POWER_STATUS, val,
						 val & BIT(1), 1, 200);
		if (ret)
			return ret;
	} else {
		writel_relaxed(1, base_addr + WRAPPER_CORE_POWER_CONTROL);
		ret = readl_relaxed_poll_timeout(base_addr + WRAPPER_CORE_POWER_STATUS, val,
						 !(val & BIT(1)), 1, 200);
		if (ret)
			return ret;
	}

	return 0;
}

static int boot_firmware_iris2(struct iris_core *core)
{
	u32 ctrl_init = 0, ctrl_status = 0, count = 0, max_tries = 1000;
	int ret;

	ret = setup_ucregion_memory_map_iris2(core);
	if (ret)
		return ret;

	ctrl_init = BIT(0);

	ret = write_register(core, CTRL_INIT_IRIS2, ctrl_init);
	if (ret)
		return ret;

	while (!ctrl_status && count < max_tries) {
		ret = read_register(core, CTRL_STATUS_IRIS2, &ctrl_status);
		if (ret)
			return ret;

		if ((ctrl_status & CTRL_ERROR_STATUS__M_IRIS2) == 0x4) {
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

	ret = write_register(core, CPU_CS_H2XSOFTINTEN_IRIS2, 0x1);
	if (ret)
		return ret;

	ret = write_register(core, CPU_CS_X2RPMH_IRIS2, 0x0);

	return ret;
}

static int raise_interrupt_iris2(struct iris_core *core)
{
	return write_register(core, CPU_IC_SOFTINT_IRIS2, 1 << CPU_IC_SOFTINT_H2A_SHFT_IRIS2);
}

static int clear_interrupt_iris2(struct iris_core *core)
{
	u32 intr_status = 0, mask = 0;
	int ret;

	ret = read_register(core, WRAPPER_INTR_STATUS_IRIS2, &intr_status);
	if (ret)
		return ret;

	mask = (WRAPPER_INTR_STATUS_A2H_BMSK_IRIS2 |
		WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS2 |
		CTRL_INIT_IDLE_MSG_BMSK_IRIS2);

	if (intr_status & mask) {
		core->intr_status |= intr_status;
		core->reg_count++;
	} else {
		core->spur_count++;
	}

	ret = write_register(core, CPU_CS_A2HSOFTINTCLR_IRIS2, 1);

	return ret;
}

static int watchdog_iris2(struct iris_core *core, u32 intr_status)
{
	if (intr_status & WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS2) {
		dev_err(core->dev, "%s: received watchdog interrupt\n", __func__);
		return -ETIME;
	}

	return 0;
}

static bool is_iris2_hw_power_collapsed(struct iris_core *core)
{
	u32 value = 0, pwr_status = 0;
	int ret;

	ret = read_register(core, WRAPPER_CORE_POWER_STATUS, &value);
	if (ret)
		return false;

	pwr_status = value & BIT(1);

	return pwr_status ? false : true;
}

static int power_off_iris2_hardware(struct iris_core *core)
{
	u32 value = 0;
	int ret, i;

	if (is_iris2_hw_power_collapsed(core))
		goto disable_power;

	dev_err(core->dev, "Video hw is power ON\n");

	ret = read_register(core, WRAPPER_CORE_CLOCK_CONFIG_IRIS2, &value);
	if (ret)
		goto disable_power;

	if (value) {
		ret = write_register(core, WRAPPER_CORE_CLOCK_CONFIG_IRIS2, 0);
		if (ret)
			goto disable_power;
	}

	for (i = 0; i < core->cap[NUM_VPP_PIPE].value; i++) {
		ret = read_register_with_poll_timeout(core, VCODEC_SS_IDLE_STATUSN + 4 * i,
						      0x400000, 0x400000, 2000, 20000);
	}

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
	ret = disable_power_domains(core, "vcodec0");
	if (ret) {
		dev_err(core->dev, "disable power domain vcodec failed\n");
		ret = 0;
	}

	disable_unprepare_clock(core, "vcodec_bus");
	if (ret) {
		dev_err(core->dev, "disable unprepare vcodec_bus failed\n");
		ret = 0;
	}

	disable_unprepare_clock(core, "vcodec_core");
	if (ret) {
		dev_err(core->dev, "disable unprepare vcodec_core failed\n");
		ret = 0;
	}

	return ret;
}

static int power_off_iris2_controller(struct iris_core *core)
{
	int ret;

	ret = write_register(core, CPU_CS_X2RPMH_IRIS2, 0x3);
	if (ret)
		goto disable_power;

	ret = write_register(core, WRAPPER_DEBUG_BRIDGE_LPI_CONTROL_IRIS2, 0x7);
	if (ret)
		goto disable_power;

	ret = read_register_with_poll_timeout(core, WRAPPER_DEBUG_BRIDGE_LPI_STATUS_IRIS2,
					      0x7, 0x7, 200, 2000);

	ret = write_register(core, WRAPPER_DEBUG_BRIDGE_LPI_CONTROL_IRIS2, 0x0);
	if (ret)
		goto disable_power;

	ret = read_register_with_poll_timeout(core, WRAPPER_DEBUG_BRIDGE_LPI_STATUS_IRIS2,
					      0xffffffff, 0x0, 200, 2000);

disable_power:
	ret = disable_unprepare_clock(core, "core");
	if (ret) {
		dev_err(core->dev, "disable unprepare core_clk failed\n");
		ret = 0;
	}

	ret = disable_unprepare_clock(core, "iface");
	if (ret) {
		dev_err(core->dev, "disable unprepare iface failed\n");
		ret = 0;
	}

	ret = disable_unprepare_clock(core, "bus");
	if (ret) {
		dev_err(core->dev, "disable unprepare bus failed\n");
		ret = 0;
	}

	reset_ahb2axi_bridge(core);

	ret = disable_power_domains(core, "venus");

	if (ret) {
		dev_err(core->dev, "%s: disable power domain venus failed\n", __func__);
		ret = 0;
	}

	return ret;
}

static int power_off_iris2(struct iris_core *core)
{
	if (!core->power_enabled)
		return 0;

	opp_set_rate(core, 0);
	power_off_iris2_hardware(core);
	power_off_iris2_controller(core);
	unvote_buses(core);

	if (!call_vpu_op(core, watchdog, core, core->intr_status))
		disable_irq_nosync(core->irq);

	core->power_enabled = false;

	return 0;
}

static int power_on_iris2_controller(struct iris_core *core)
{
	int ret;

	ret = enable_power_domains(core, "venus");
	if (ret)
		return ret;

	ret = reset_ahb2axi_bridge(core);
	if (ret)
		goto err_disable_power;

	ret = prepare_enable_clock(core, "bus");
	if (ret)
		goto err_disable_power;

	ret = prepare_enable_clock(core, "iface");
	if (ret)
		goto err_disable_bus;

	ret = prepare_enable_clock(core, "core");
	if (ret)
		goto err_disable_iface;

	return ret;

err_disable_iface:
	disable_unprepare_clock(core, "iface");
err_disable_bus:
	disable_unprepare_clock(core, "bus");
err_disable_power:
	disable_power_domains(core, "venus");

	return ret;
}

static int power_on_iris2_hardware(struct iris_core *core)
{
	int ret;

	ret = enable_power_domains(core, "vcodec0");
	if (ret)
		return ret;

	ret = switch_vcodec_gdsc_mode(core, true);
	if (ret)
		goto err_disable_power;

	ret = prepare_enable_clock(core, "vcodec_bus");
	if (ret)
		goto err_gdsc_switch;

	ret = prepare_enable_clock(core, "vcodec_core");
	if (ret)
		goto err_disable_bus;

	ret = switch_vcodec_gdsc_mode(core, false);
	if (ret)
		goto err_disable_clock;

	return ret;

err_disable_clock:
	disable_unprepare_clock(core, "vcodec_core");
err_disable_bus:
	disable_unprepare_clock(core, "vcodec_bus");
err_gdsc_switch:
	switch_vcodec_gdsc_mode(core, false);
err_disable_power:
	disable_power_domains(core, "vcodec0");

	return ret;
}

static int power_on_iris2(struct iris_core *core)
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

	ret = power_on_iris2_controller(core);
	if (ret)
		goto err_unvote_bus;

	ret = power_on_iris2_hardware(core);
	if (ret)
		goto err_power_off_ctrl;

	core->power_enabled = true;

	freq = core->power.clk_freq ? core->power.clk_freq :
				      (u32)ULONG_MAX;

	opp_set_rate(core, freq);

	set_preset_registers(core);

	interrupt_init_iris2(core);
	core->intr_status = 0;
	enable_irq(core->irq);

	return ret;

err_power_off_ctrl:
	power_off_iris2_controller(core);
err_unvote_bus:
	unvote_buses(core);
err:
	core->power_enabled = false;

	return ret;
}

static int prepare_pc_iris2(struct iris_core *core)
{
	u32 wfi_status = 0, idle_status = 0, pc_ready = 0;
	u32 ctrl_status = 0;
	int ret;

	ret = read_register(core, CTRL_STATUS_IRIS2, &ctrl_status);
	if (ret)
		return ret;

	pc_ready = ctrl_status & CTRL_STATUS_PC_READY_IRIS2;
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

	ret = read_register_with_poll_timeout(core, CTRL_STATUS_IRIS2,
					      CTRL_STATUS_PC_READY_IRIS2,
					      CTRL_STATUS_PC_READY_IRIS2, 250, 2500);
	if (ret)
		goto skip_power_off;

	ret = read_register_with_poll_timeout(core, WRAPPER_TZ_CPU_STATUS,
					      BIT(0), 0x1, 250, 2500);
	if (ret)
		goto skip_power_off;

	return ret;

skip_power_off:
	ret = read_register(core, CTRL_STATUS_IRIS2, &ctrl_status);
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

static const struct vpu_ops iris2_ops = {
	.boot_firmware = boot_firmware_iris2,
	.raise_interrupt = raise_interrupt_iris2,
	.clear_interrupt = clear_interrupt_iris2,
	.watchdog = watchdog_iris2,
	.power_on = power_on_iris2,
	.power_off = power_off_iris2,
	.prepare_pc = prepare_pc_iris2,
};

static const struct vpu_session_ops iris2_session_ops = {
	.int_buf_size = iris_int_buf_size_iris2,
	.calc_freq = iris_calc_freq_iris2,
	.calc_bw = iris_calc_bw_iris2,
};

int init_iris2(struct iris_core *core)
{
	core->vpu_ops = &iris2_ops;
	core->session_ops = &iris2_session_ops;

	return 0;
}
