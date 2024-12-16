// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <asm/unaligned.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/iio/adc/qcom-vadc-common.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#include <dt-bindings/iio/qcom,spmi-vadc.h>

#define ADC5_GEN3_HS				0x45
#define ADC5_GEN3_HS_BUSY			BIT(7)
#define ADC5_GEN3_HS_READY			BIT(0)

#define ADC5_GEN3_STATUS1			0x46
#define ADC5_GEN3_STATUS1_CONV_FAULT		BIT(7)
#define ADC5_GEN3_STATUS1_THR_CROSS		BIT(6)
#define ADC5_GEN3_STATUS1_EOC			BIT(0)

#define ADC5_GEN3_TM_EN_STS			0x47
#define ADC5_GEN3_TM_HIGH_STS			0x48
#define ADC5_GEN3_TM_LOW_STS			0x49

#define ADC5_GEN3_EOC_STS			0x4a
#define ADC5_GEN3_EOC_CHAN_0			BIT(0)

#define ADC5_GEN3_EOC_CLR			0x4b
#define ADC5_GEN3_TM_HIGH_STS_CLR		0x4c
#define ADC5_GEN3_TM_LOW_STS_CLR		0x4d
#define ADC5_GEN3_CONV_ERR_CLR			0x4e
#define ADC5_GEN3_CONV_ERR_CLR_REQ		BIT(0)

#define ADC5_GEN3_SID				0x4f
#define ADC5_GEN3_SID_MASK			GENMASK(3, 0)

#define ADC5_GEN3_PERPH_CH			0x50
#define ADC5_GEN3_CHAN_CONV_REQ			BIT(7)

#define ADC5_GEN3_TIMER_SEL			0x51
#define ADC5_GEN3_TIME_IMMEDIATE		0x1

#define ADC5_GEN3_DIG_PARAM			0x52
#define ADC5_GEN3_DIG_PARAM_CAL_SEL_MASK	GENMASK(5, 4)
#define ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_MASK	GENMASK(3, 2)

#define ADC5_GEN3_FAST_AVG			0x53
#define ADC5_GEN3_FAST_AVG_CTL_EN		BIT(7)
#define ADC5_GEN3_FAST_AVG_CTL_SAMPLES_MASK	GENMASK(2, 0)

#define ADC5_GEN3_ADC_CH_SEL_CTL		0x54
#define ADC5_GEN3_DELAY_CTL			0x55
#define ADC5_GEN3_HW_SETTLE_DELAY_MASK		GENMASK(3, 0)

#define ADC5_GEN3_CH_EN				0x56
#define ADC5_GEN3_HIGH_THR_INT_EN		BIT(1)
#define ADC5_GEN3_LOW_THR_INT_EN		BIT(0)

#define ADC5_GEN3_LOW_THR0			0x57
#define ADC5_GEN3_LOW_THR1			0x58
#define ADC5_GEN3_HIGH_THR0			0x59
#define ADC5_GEN3_HIGH_THR1			0x5a

#define ADC5_GEN3_CH_DATA0(channel)	(0x5c + (channel) * 2)
#define ADC5_GEN3_CH_DATA1(channel)	(0x5d + (channel) * 2)

#define ADC5_GEN3_CONV_REQ			0xe5
#define ADC5_GEN3_CONV_REQ_REQ			BIT(0)

#define ADC5_GEN3_VIRTUAL_SID_MASK			GENMASK(15, 8)
#define ADC5_GEN3_CHANNEL_MASK			GENMASK(7, 0)
#define V_CHAN(x)		\
	(FIELD_PREP(ADC5_GEN3_VIRTUAL_SID_MASK, (x).sid) | (x).channel)	\

enum adc5_cal_method {
	ADC5_NO_CAL = 0,
	ADC5_RATIOMETRIC_CAL,
	ADC5_ABSOLUTE_CAL
};

enum adc5_time_select {
	MEAS_INT_DISABLE = 0,
	MEAS_INT_IMMEDIATE,
	MEAS_INT_50MS,
	MEAS_INT_100MS,
	MEAS_INT_1S,
	MEAS_INT_NONE,
};

struct adc5_sdam_data {
	u16			base_addr;
	const char		*irq_name;
	int			irq;
};

/**
 * struct adc5_channel_prop - ADC channel property.
 * @channel: channel number, refer to the channel list.
 * @cal_method: calibration method.
 * @decimation: sampling rate supported for the channel.
 * @sid: slave id of PMIC owning the channel.
 * @prescale: channel scaling performed on the input signal.
 * @hw_settle_time: the time between AMUX being configured and the
 *	start of conversion.
 * @avg_samples: ability to provide single result from the ADC
 *	that is an average of multiple measurements.
 * @sdam_index: Index for which SDAM this channel is on.
 * @scale_fn_type: Represents the scaling function to convert voltage
 *	physical units desired by the client for the channel.
 * @label: Channel name used in device tree.
 * @chip: pointer to top-level ADC device structure.
 * @adc_tm: indicates if the channel is used for TM measurements.
 * @tm_chan_index: TM channel number used (ranging from 1-7).
 * @timer: time period of recurring TM measurement.
 * @tzd: pointer to thermal device corresponding to TM channel.
 * @high_thr_en: TM high threshold crossing detection enabled.
 * @low_thr_en: TM low threshold crossing detection enabled.
 * @last_temp: last temperature that caused threshold violation,
 *	or a thermal TM channel.
 * @last_temp_set: indicates if last_temp is stored.
 */
struct adc5_channel_prop {
	unsigned int			channel;
	enum adc5_cal_method		cal_method;
	unsigned int			decimation;
	unsigned int			sid;
	unsigned int			prescale;
	unsigned int			hw_settle_time;
	unsigned int			avg_samples;
	unsigned int			sdam_index;

	enum vadc_scale_fn_type		scale_fn_type;
	const char			*label;

	struct adc5_chip		*chip;
	/* TM(thermal monitoring related) properties */
	bool				adc_tm;
	unsigned int			tm_chan_index;
	unsigned int			timer;
	struct thermal_zone_device	*tzd;
	bool				high_thr_en;
	bool				low_thr_en;
	int				last_temp;
	bool				last_temp_set;
};

/**
 * struct adc5_chip - ADC private structure.
 * @regmap: SPMI ADC5 Gen3 peripheral register map field.
 * @dev: SPMI ADC5 Gen3 device.
 * @base: pointer to array of ADC peripheral base and interrupt.
 * @num_sdams: number of SDAMs (Shared Direct Access Memory Module) being used.
 * @nchannels: number of ADC channels.
 * @chan_props: array of ADC channel properties.
 * @iio_chans: array of IIO channels specification.
 * @complete: ADC result notification after interrupt is received.
 * @lock: ADC lock for access to the peripheral, to prevent concurrent
 * requests from multiple clients.
 * @data: software configuration data.
 * @n_tm_channels: number of ADC channels used for TM measurements.
 * @tm_handler_work: scheduled work for handling TM threshold violation.
 */
struct adc5_chip {
	struct regmap			*regmap;
	struct device			*dev;
	struct adc5_sdam_data		*base;
	unsigned int			num_sdams;
	unsigned int			nchannels;
	struct adc5_channel_prop	*chan_props;
	struct iio_chan_spec		*iio_chans;
	struct completion		complete;
	struct mutex			lock;
	const struct adc5_data		*data;
	/* TM properties */
	unsigned int			n_tm_channels;
	struct work_struct		tm_handler_work;
};

static int adc5_gen3_read(struct adc5_chip *adc, unsigned int sdam_index,
				u16 offset, u8 *data, int len)
{
	return regmap_bulk_read(adc->regmap, adc->base[sdam_index].base_addr + offset, data, len);
}

static int adc5_gen3_write(struct adc5_chip *adc, unsigned int sdam_index,
				u16 offset, u8 *data, int len)
{
	return regmap_bulk_write(adc->regmap, adc->base[sdam_index].base_addr + offset, data, len);
}

static int adc5_gen3_read_voltage_data(struct adc5_chip *adc, u16 *data, u8 sdam_index)
{
	int ret;
	u8 rslt[2];

	ret = adc5_gen3_read(adc, sdam_index, ADC5_GEN3_CH_DATA0(0), rslt, 2);
	if (ret)
		return ret;

	*data = get_unaligned_le16(rslt);

	if (*data == ADC5_USR_DATA_CHECK) {
		dev_err(adc->dev, "Invalid data:%#x\n", *data);
		return -EINVAL;
	}

	dev_dbg(adc->dev, "voltage raw code:%#x\n", *data);

	return 0;
}

static void adc5_gen3_update_dig_param(struct adc5_chip *adc,
			struct adc5_channel_prop *prop, u8 *data)
{
	/* Update calibration select and decimation ratio select*/
	*data &= ~(ADC5_GEN3_DIG_PARAM_CAL_SEL_MASK | ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_MASK);
	*data |= FIELD_PREP(ADC5_GEN3_DIG_PARAM_CAL_SEL_MASK, prop->cal_method);
	*data |= FIELD_PREP(ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_MASK, prop->decimation);
}

# define ADC5_GEN3_READ_CONFIG_REGS 7

static int adc5_gen3_configure(struct adc5_chip *adc,
			struct adc5_channel_prop *prop)
{
	u8 sdam_index = prop->sdam_index;
	u8 conv_req = 0;
	u8 buf[ADC5_GEN3_READ_CONFIG_REGS];
	int ret;

	/* Reserve channel 0 of first SDAM for immediate conversions */
	if (prop->adc_tm)
		sdam_index = 0;

	ret = adc5_gen3_read(adc, sdam_index, ADC5_GEN3_SID, buf, sizeof(buf));
	if (ret)
		return ret;

	/* Write SID */
	buf[0] = FIELD_PREP(ADC5_GEN3_SID_MASK, prop->sid);

	/*
	 * Use channel 0 by default for immediate conversion and
	 * to indicate there is an actual conversion request
	 */
	buf[1] = ADC5_GEN3_CHAN_CONV_REQ | 0;

	buf[2] = ADC5_GEN3_TIME_IMMEDIATE;

	/* Digital param selection */
	adc5_gen3_update_dig_param(adc, prop, &buf[3]);

	/* Update fast average sample value */
	buf[4] &= ~ADC5_GEN3_FAST_AVG_CTL_SAMPLES_MASK;
	buf[4] |= prop->avg_samples | ADC5_GEN3_FAST_AVG_CTL_EN;

	/* Select ADC channel */
	buf[5] = prop->channel;

	/* Select HW settle delay for channel */
	buf[6] = FIELD_PREP(ADC5_GEN3_HW_SETTLE_DELAY_MASK, prop->hw_settle_time);

	reinit_completion(&adc->complete);

	ret = adc5_gen3_write(adc, sdam_index, ADC5_GEN3_SID, buf, sizeof(buf));
	if (ret)
		return ret;

	conv_req = ADC5_GEN3_CONV_REQ_REQ;
	return adc5_gen3_write(adc, sdam_index, ADC5_GEN3_CONV_REQ, &conv_req, 1);
}

/**
 * Worst case delay from PBS in readying handshake bit
 * can be up to 15ms, when PBS is busy running other
 * simultaneous transactions, while in the best case, it is
 * already ready at this point. Assigning polling delay and
 * retry count accordingly.
 */

#define ADC5_GEN3_HS_DELAY_MIN_US		100
#define ADC5_GEN3_HS_DELAY_MAX_US		110
#define ADC5_GEN3_HS_RETRY_COUNT		150

static int adc5_gen3_poll_wait_hs(struct adc5_chip *adc,
				unsigned int sdam_index)
{
	u8 conv_req = ADC5_GEN3_CONV_REQ_REQ;
	u8 status = 0;
	int ret, count;

	for (count = 0; count < ADC5_GEN3_HS_RETRY_COUNT; count++) {
		ret = adc5_gen3_read(adc, sdam_index, ADC5_GEN3_HS, &status, 1);
		if (ret)
			return ret;

		if (status == ADC5_GEN3_HS_READY) {
			ret = adc5_gen3_read(adc, sdam_index, ADC5_GEN3_CONV_REQ,
					&conv_req, 1);
			if (ret)
				return ret;

			if (!conv_req)
				return 0;
		}

		usleep_range(ADC5_GEN3_HS_DELAY_MIN_US,
			ADC5_GEN3_HS_DELAY_MAX_US);
	}

	dev_err(adc->dev, "Setting HS ready bit timed out, status:%#x\n", status);
	return -ETIMEDOUT;
}

/**
 * Worst case delay from PBS for conversion time can be
 * up to 500ms, when PBS has timed out twice, once for
 * the initial attempt and once for a retry of the same
 * transaction.
 */

#define ADC5_GEN3_CONV_TIMEOUT_MS	501

static int adc5_gen3_do_conversion(struct adc5_chip *adc,
			struct adc5_channel_prop *prop,
			u16 *data_volt)
{
	u8 val, sdam_index = prop->sdam_index;
	unsigned long rc;
	int ret;

	/* Reserve channel 0 of first SDAM for immediate conversions */
	if (prop->adc_tm)
		sdam_index = 0;

	mutex_lock(&adc->lock);
	ret = adc5_gen3_poll_wait_hs(adc, 0);
	if (ret)
		goto unlock;

	ret = adc5_gen3_configure(adc, prop);
	if (ret) {
		dev_err(adc->dev, "ADC configure failed with %d\n", ret);
		goto unlock;
	}

	/* No support for polling mode at present*/
	rc = wait_for_completion_timeout(&adc->complete,
					msecs_to_jiffies(ADC5_GEN3_CONV_TIMEOUT_MS));
	if (!rc) {
		dev_err(adc->dev, "Reading ADC channel %s timed out\n",
			prop->label);
		ret = -ETIMEDOUT;
		goto unlock;
	}

	ret = adc5_gen3_read_voltage_data(adc, data_volt, sdam_index);
	if (ret)
		goto unlock;

	val = BIT(0);
	ret = adc5_gen3_write(adc, sdam_index, ADC5_GEN3_EOC_CLR, &val, 1);
	if (ret)
		goto unlock;

	/* To indicate conversion request is only to clear a status */
	val = 0;
	ret = adc5_gen3_write(adc, sdam_index, ADC5_GEN3_PERPH_CH, &val, 1);
	if (ret)
		goto unlock;

	val = ADC5_GEN3_CONV_REQ_REQ;
	ret = adc5_gen3_write(adc, sdam_index, ADC5_GEN3_CONV_REQ, &val, 1);

unlock:
	mutex_unlock(&adc->lock);

	return ret;
}

static int get_sdam_from_irq(struct adc5_chip *adc, int irq)
{
	int i;

	for (i = 0; i < adc->num_sdams; i++) {
		if (adc->base[i].irq == irq)
			return i;
	}
	return -ENOENT;
}

static irqreturn_t adc5_gen3_isr(int irq, void *dev_id)
{
	struct adc5_chip *adc = dev_id;
	u8 status, tm_status[2], eoc_status, val;
	int ret, sdam_num;

	sdam_num = get_sdam_from_irq(adc, irq);
	if (sdam_num < 0) {
		dev_err(adc->dev, "adc irq %d not associated with an sdam\n", irq);
		return IRQ_HANDLED;
	}

	ret = adc5_gen3_read(adc, sdam_num, ADC5_GEN3_STATUS1, &status, 1);
	if (ret) {
		dev_err(adc->dev, "adc read status1 failed with %d\n", ret);
		return IRQ_HANDLED;
	}

	ret = adc5_gen3_read(adc, sdam_num, ADC5_GEN3_EOC_STS, &eoc_status, 1);
	if (ret) {
		dev_err(adc->dev, "adc read eoc status failed with %d\n", ret);
		return IRQ_HANDLED;
	}

	if (status & ADC5_GEN3_STATUS1_CONV_FAULT) {
		dev_err_ratelimited(adc->dev, "Unexpected conversion fault, status:%#x, eoc_status:%#x\n",
				status, eoc_status);
		val = ADC5_GEN3_CONV_ERR_CLR_REQ;
		ret = adc5_gen3_write(adc, sdam_num, ADC5_GEN3_CONV_ERR_CLR, &val, 1);
		if (ret < 0)
			return IRQ_HANDLED;

		/* To indicate conversion request is only to clear a status */
		val = 0;
		ret = adc5_gen3_write(adc, sdam_num, ADC5_GEN3_PERPH_CH, &val, 1);
		if (ret < 0)
			return IRQ_HANDLED;

		val = ADC5_GEN3_CONV_REQ_REQ;
		ret = adc5_gen3_write(adc, sdam_num, ADC5_GEN3_CONV_REQ, &val, 1);

		return IRQ_HANDLED;
	}

	/* CHAN0 is the preconfigured channel for immediate conversion */
	if (eoc_status & ADC5_GEN3_EOC_CHAN_0)
		complete(&adc->complete);

	ret = adc5_gen3_read(adc, sdam_num, ADC5_GEN3_TM_HIGH_STS, tm_status, 2);
	if (ret) {
		dev_err(adc->dev, "adc read TM status failed with %d\n", ret);
		return IRQ_HANDLED;
	}

	if (tm_status[0] || tm_status[1])
		schedule_work(&adc->tm_handler_work);

	dev_dbg(adc->dev, "Interrupt status:%#x, EOC status:%#x, high:%#x, low:%#x\n",
			status, eoc_status, tm_status[0], tm_status[1]);

	return IRQ_HANDLED;
}

static void tm_handler_work(struct work_struct *work)
{
	struct adc5_chip *adc = container_of(work, struct adc5_chip, tm_handler_work);
	struct adc5_channel_prop *chan_prop;
	u8 tm_status[2] = {0};
	u8 buf[16] = {0};
	u8 val;
	int ret, i, sdam_index = -1;

	for (i = 0; i < adc->nchannels; i++) {
		bool upper_set = false, lower_set = false;
		int temp, offset;
		u16 code = 0;

		chan_prop = &adc->chan_props[i];
		offset = chan_prop->tm_chan_index;

		if (!chan_prop->adc_tm)
			continue;

		mutex_lock(&adc->lock);
		if (chan_prop->sdam_index != sdam_index) {
			sdam_index = chan_prop->sdam_index;
			ret = adc5_gen3_read(adc, sdam_index, ADC5_GEN3_TM_HIGH_STS,
					tm_status, 2);
			if (ret) {
				dev_err(adc->dev, "adc read TM status failed with %d\n", ret);
				goto out;
			}

			ret = adc5_gen3_write(adc, sdam_index, ADC5_GEN3_TM_HIGH_STS_CLR,
					tm_status, 2);
			if (ret) {
				dev_err(adc->dev, "adc write TM status failed with %d\n", ret);
				goto out;
			}

			/* To indicate conversion request is only to clear a status */
			val = 0;
			ret = adc5_gen3_write(adc, sdam_index, ADC5_GEN3_PERPH_CH, &val, 1);
			if (ret) {
				dev_err(adc->dev, "adc write status clear conv_req failed with %d\n",
						ret);
				goto out;
			}

			val = ADC5_GEN3_CONV_REQ_REQ;
			ret = adc5_gen3_write(adc, sdam_index, ADC5_GEN3_CONV_REQ, &val, 1);
			if (ret) {
				dev_err(adc->dev, "adc write conv_req failed with %d\n", ret);
				goto out;
			}

			ret = adc5_gen3_read(adc, sdam_index, ADC5_GEN3_CH_DATA0(0), buf,
					sizeof(buf));
			if (ret < 0) {
				dev_err(adc->dev, "adc read data failed with %d\n", ret);
				goto out;
			}
		}

		if ((tm_status[0] & BIT(offset)) && (chan_prop->high_thr_en))
			upper_set = true;

		if ((tm_status[1] & BIT(offset)) && (chan_prop->low_thr_en))
			lower_set = true;

		mutex_unlock(&adc->lock);

		if (!(upper_set || lower_set))
			continue;

		code = get_unaligned_le16(&buf[2 * offset]);
		pr_debug("ADC_TM threshold code:%#x\n", code);

		ret = qcom_adc5_hw_scale(chan_prop->scale_fn_type,
			chan_prop->prescale, adc->data, code, &temp);
		if (ret) {
			dev_err(adc->dev, "Invalid temperature reading, ret = %d, code=%#x\n",
					ret, code);
			continue;
		}

		chan_prop->last_temp = temp;
		chan_prop->last_temp_set = true;
		thermal_zone_device_update(chan_prop->tzd, THERMAL_TRIP_VIOLATED);
	}

	return;

out:
	mutex_unlock(&adc->lock);
}

static int adc5_gen3_fwnode_xlate(struct iio_dev *indio_dev,
				const struct fwnode_reference_args *iiospec)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	int i, v_channel;

	for (i = 0; i < adc->nchannels; i++) {
		v_channel = V_CHAN(adc->chan_props[i]);
		if (v_channel == iiospec->args[0])
			return i;
	}

	return -ENOENT;
}

static int adc5_gen3_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	struct adc5_channel_prop *prop;
	u16 adc_code_volt;
	int ret;

	prop = &adc->chan_props[chan->address];

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = adc5_gen3_do_conversion(adc, prop,
					&adc_code_volt);
		if (ret)
			return ret;

		ret = qcom_adc5_hw_scale(prop->scale_fn_type,
			prop->prescale, adc->data,
			adc_code_volt, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_RAW:
		ret = adc5_gen3_do_conversion(adc, prop,
					&adc_code_volt);
		if (ret)
			return ret;
		*val = (int)adc_code_volt;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int adc5_gen3_read_label(struct iio_dev *indio_dev,
			const struct iio_chan_spec *chan, char *label)
{
	struct adc5_chip *adc = iio_priv(indio_dev);
	struct adc5_channel_prop *prop;

	prop = &adc->chan_props[chan->address];
	return snprintf(label, PAGE_SIZE, "%s\n", prop->label);
}

static const struct iio_info adc5_gen3_info = {
	.read_raw = adc5_gen3_read_raw,
	.read_label = adc5_gen3_read_label,
	.fwnode_xlate = adc5_gen3_fwnode_xlate,
};

static int adc_tm_gen3_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct adc5_channel_prop *prop = tz->devdata;
	struct adc5_chip *adc;
	u16 adc_code_volt;
	int ret;

	if (!prop || !prop->chip)
		return -EINVAL;

	adc = prop->chip;

	if (prop->last_temp_set) {
		pr_debug("last_temp: %d\n", prop->last_temp);
		prop->last_temp_set = false;
		*temp = prop->last_temp;
		return 0;
	}

	ret = adc5_gen3_do_conversion(adc, prop, &adc_code_volt);
	if (ret < 0)
		return ret;

	return qcom_adc5_hw_scale(prop->scale_fn_type,
		prop->prescale, adc->data,
		adc_code_volt, temp);
}

static int _adc_tm5_gen3_disable_channel(struct adc5_channel_prop *prop)
{
	struct adc5_chip *adc = prop->chip;
	int ret;
	u8 val;

	prop->high_thr_en = false;
	prop->low_thr_en = false;

	val = MEAS_INT_DISABLE;
	ret = adc5_gen3_write(adc, prop->sdam_index, ADC5_GEN3_TIMER_SEL, &val, 1);
	if (ret)
		return ret;

	/* To indicate there is an actual conversion request */
	val = ADC5_GEN3_CHAN_CONV_REQ | prop->tm_chan_index;
	ret = adc5_gen3_write(adc, prop->sdam_index, ADC5_GEN3_PERPH_CH, &val, 1);
	if (ret)
		return ret;

	val = ADC5_GEN3_CONV_REQ_REQ;
	return adc5_gen3_write(adc, prop->sdam_index, ADC5_GEN3_CONV_REQ, &val, 1);
}

static int adc_tm5_gen3_disable_channel(struct adc5_channel_prop *prop)
{
	return _adc_tm5_gen3_disable_channel(prop);
}

# define ADC_TM5_GEN3_CONFIG_REGS 12

static int adc_tm5_gen3_configure(struct adc5_channel_prop *prop,
					int low_temp, int high_temp)
{
	struct adc5_chip *adc = prop->chip;
	u8 conv_req = 0, buf[ADC_TM5_GEN3_CONFIG_REGS];
	u16 adc_code;
	int ret;

	ret = adc5_gen3_poll_wait_hs(adc, prop->sdam_index);
	if (ret < 0)
		return ret;

	ret = adc5_gen3_read(adc, prop->sdam_index, ADC5_GEN3_SID, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	/* Write SID */
	buf[0] = FIELD_PREP(ADC5_GEN3_SID_MASK, prop->sid);

	/*
	 * Select TM channel and indicate there is an actual
	 * conversion request
	 */
	buf[1] = ADC5_GEN3_CHAN_CONV_REQ | prop->tm_chan_index;

	buf[2] = prop->timer;

	/* Digital param selection */
	adc5_gen3_update_dig_param(adc, prop, &buf[3]);

	/* Update fast average sample value */
	buf[4] &= ~ADC5_GEN3_FAST_AVG_CTL_SAMPLES_MASK;
	buf[4] |= prop->avg_samples | ADC5_GEN3_FAST_AVG_CTL_EN;

	/* Select ADC channel */
	buf[5] = prop->channel;

	/* Select HW settle delay for channel */
	buf[6] = FIELD_PREP(ADC5_GEN3_HW_SETTLE_DELAY_MASK, prop->hw_settle_time);

	/* High temperature corresponds to low voltage threshold */
	if (high_temp != INT_MAX) {
		prop->low_thr_en = true;
		adc_code = qcom_adc_tm5_gen2_temp_res_scale(high_temp);
		put_unaligned_le16(adc_code, &buf[8]);
	} else {
		prop->low_thr_en = false;
	}

	/* Low temperature corresponds to high voltage threshold */
	if (low_temp != -INT_MAX) {
		prop->high_thr_en = true;
		adc_code = qcom_adc_tm5_gen2_temp_res_scale(low_temp);
		put_unaligned_le16(adc_code, &buf[10]);
	} else {
		prop->high_thr_en = false;
	}

	buf[7] = 0;
	if (prop->high_thr_en)
		buf[7] |= ADC5_GEN3_HIGH_THR_INT_EN;
	if (prop->low_thr_en)
		buf[7] |= ADC5_GEN3_LOW_THR_INT_EN;

	ret = adc5_gen3_write(adc, prop->sdam_index, ADC5_GEN3_SID, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	conv_req = ADC5_GEN3_CONV_REQ_REQ;
	return adc5_gen3_write(adc, prop->sdam_index, ADC5_GEN3_CONV_REQ, &conv_req, 1);
}

static int adc_tm5_gen3_set_trip_temp(struct thermal_zone_device *tz,
					int low_temp, int high_temp)
{
	struct adc5_channel_prop *prop = tz->devdata;
	struct adc5_chip *adc;
	int ret;

	if (!prop || !prop->chip)
		return -EINVAL;

	adc = prop->chip;

	dev_dbg(adc->dev, "channel:%s, low_temp(mdegC):%d, high_temp(mdegC):%d\n",
		prop->label, low_temp, high_temp);

	mutex_lock(&adc->lock);
	if (high_temp == INT_MAX && low_temp <= -INT_MAX)
		ret = adc_tm5_gen3_disable_channel(prop);
	else
		ret = adc_tm5_gen3_configure(prop, low_temp, high_temp);
	mutex_unlock(&adc->lock);

	return ret;
}

static const struct thermal_zone_device_ops adc_tm_ops = {
	.get_temp = adc_tm_gen3_get_temp,
	.set_trips = adc_tm5_gen3_set_trip_temp,
};

static int adc_tm_register_tzd(struct adc5_chip *adc)
{
	unsigned int i, channel;
	struct thermal_zone_device *tzd;

	for (i = 0; i < adc->nchannels; i++) {
		channel = V_CHAN(adc->chan_props[i]);

		if (!adc->chan_props[i].adc_tm)
			continue;
		tzd = devm_thermal_of_zone_register(adc->dev, channel,
			&adc->chan_props[i], &adc_tm_ops);

		if (IS_ERR(tzd)) {
			if (PTR_ERR(tzd) == -ENODEV) {
				dev_warn(adc->dev, "thermal sensor on channel %d is not used\n",
					 channel);
				continue;
			}

			dev_err(adc->dev, "Error registering TZ zone:%ld for channel:%d\n",
				PTR_ERR(tzd), adc->chan_props[i].channel);
			return PTR_ERR(tzd);
		}
		adc->chan_props[i].tzd = tzd;
	}

	return 0;
}

static void adc5_gen3_disable(void *data)
{
	struct adc5_chip *adc = data;
	int i;

	if (adc->n_tm_channels)
		cancel_work_sync(&adc->tm_handler_work);

	for (i = 0; i < adc->num_sdams; i++)
		free_irq(adc->base[i].irq, adc);

	mutex_lock(&adc->lock);
	/* Disable all available TM channels */
	for (i = 0; i < adc->nchannels; i++) {
		if (!adc->chan_props[i].adc_tm)
			continue;
		adc5_gen3_poll_wait_hs(adc, adc->chan_props[i].sdam_index);
		_adc_tm5_gen3_disable_channel(&adc->chan_props[i]);
	}

	mutex_unlock(&adc->lock);
}

struct adc5_channels {
	unsigned int prescale_index;
	enum iio_chan_type type;
	long info_mask;
	enum vadc_scale_fn_type scale_fn_type;
};

/* In these definitions, _pre refers to an index into adc5_prescale_ratios. */
#define ADC5_CHAN(_type, _mask, _pre, _scale)	\
	{						\
		.prescale_index = _pre,			\
		.type = _type,				\
		.info_mask = _mask,			\
		.scale_fn_type = _scale,		\
	},						\

#define ADC5_CHAN_TEMP(_pre, _scale)		\
	ADC5_CHAN(IIO_TEMP, BIT(IIO_CHAN_INFO_PROCESSED), _pre, _scale)	\

#define ADC5_CHAN_VOLT(_pre, _scale)		\
	ADC5_CHAN(IIO_VOLTAGE, BIT(IIO_CHAN_INFO_PROCESSED), _pre, _scale)	\

#define ADC5_CHAN_CUR(_pre, _scale)		\
	ADC5_CHAN(IIO_CURRENT, BIT(IIO_CHAN_INFO_PROCESSED), _pre, _scale)	\

static const struct adc5_channels adc5_gen3_chans_pmic[ADC5_MAX_CHANNEL] = {
	[ADC5_GEN3_OFFSET_REF]		= ADC5_CHAN_VOLT(0, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_1P25VREF]		= ADC5_CHAN_VOLT(0, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_VPH_PWR]		= ADC5_CHAN_VOLT(1, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_VBAT_SNS_QBG]	= ADC5_CHAN_VOLT(1, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_USB_SNS_V_16]	= ADC5_CHAN_TEMP(8, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_VIN_DIV16_MUX]	= ADC5_CHAN_TEMP(8, SCALE_HW_CALIB_DEFAULT)
	[ADC5_GEN3_DIE_TEMP]		= ADC5_CHAN_TEMP(0,
						SCALE_HW_CALIB_PMIC_THERM_PM7)
	[ADC5_GEN3_TEMP_ALARM_LITE]	= ADC5_CHAN_TEMP(0,
						SCALE_HW_CALIB_PMIC_THERM_PM7)
	[ADC5_GEN3_AMUX1_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX2_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX3_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX4_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX5_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX6_THM_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX1_GPIO_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX2_GPIO_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX3_GPIO_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
	[ADC5_GEN3_AMUX4_GPIO_100K_PU]	= ADC5_CHAN_TEMP(0,
					SCALE_HW_CALIB_THERM_100K_PU_PM7)
};

static int adc5_gen3_get_fw_channel_data(struct adc5_chip *adc,
				    struct adc5_channel_prop *prop,
				    struct fwnode_handle *fwnode,
				    const struct adc5_data *data)
{
	const char *name = fwnode_get_name(fwnode);
	const char *channel_name;
	struct device *dev = adc->dev;
	u32 chan, value, varr[2], sid = 0;
	int ret, val;

	ret = fwnode_property_read_u32(fwnode, "reg", &chan);
	if (ret < 0)
		return dev_err_probe(dev, ret, "invalid channel number %s\n", name);

	/*
	 * Value read from "reg" is virtual channel number
	 * virtual channel number = sid << 8 | channel number
	 */

	sid = FIELD_GET(ADC5_GEN3_VIRTUAL_SID_MASK, chan);
	chan = FIELD_GET(ADC5_GEN3_CHANNEL_MASK, chan);

	if (chan > ADC5_OFFSET_EXT2)
		return dev_err_probe(dev, -EINVAL, "%s invalid channel number %d\n", name, chan);

	prop->channel = chan;
	prop->sid = sid;

	ret = fwnode_property_read_string(fwnode, "label", &channel_name);
	if (ret)
		channel_name = name;
	prop->label = channel_name;

	prop->decimation = ADC5_DECIMATION_DEFAULT;
	ret = fwnode_property_read_u32(fwnode, "qcom,decimation", &value);
	if (!ret) {
		ret = qcom_adc5_decimation_from_dt(value, data->decimation);
		if (ret < 0)
			return dev_err_probe(dev, ret, "%#x invalid decimation %d\n",
				chan, value);
		prop->decimation = ret;
	}

	prop->prescale = adc->data->adc_chans[prop->channel].prescale_index;
	ret = fwnode_property_read_u32_array(fwnode, "qcom,pre-scaling", varr, 2);
	if (!ret) {
		ret = qcom_adc5_prescaling_from_dt(varr[0], varr[1]);
		if (ret < 0)
			return dev_err_probe(dev, ret, "%#x invalid pre-scaling <%d %d>\n",
				chan, varr[0], varr[1]);
		prop->prescale = ret;
	}

	prop->hw_settle_time = VADC_DEF_HW_SETTLE_TIME;
	ret = fwnode_property_read_u32(fwnode, "qcom,hw-settle-time", &value);
	if (!ret) {
		ret = qcom_adc5_hw_settle_time_from_dt(value,
						data->hw_settle_1);
		if (ret < 0)
			return dev_err_probe(dev, ret, "%#x invalid hw-settle-time %d us\n",
				chan, value);
		prop->hw_settle_time = ret;
	}

	prop->avg_samples = VADC_DEF_AVG_SAMPLES;
	ret = fwnode_property_read_u32(fwnode, "qcom,avg-samples", &value);
	if (!ret) {
		ret = qcom_adc5_avg_samples_from_dt(value);
		if (ret < 0)
			return dev_err_probe(dev, ret, "%#x invalid avg-samples %d\n",
				chan, value);
		prop->avg_samples = ret;
	}

	if (fwnode_property_read_bool(fwnode, "qcom,ratiometric"))
		prop->cal_method = ADC5_RATIOMETRIC_CAL;
	else
		prop->cal_method = ADC5_ABSOLUTE_CAL;

	prop->timer = MEAS_INT_IMMEDIATE;

	prop->adc_tm = fwnode_property_read_bool(fwnode, "qcom,adc-tm");

	if (prop->adc_tm) {
		adc->n_tm_channels++;
		if (adc->n_tm_channels > ((adc->num_sdams * 8) - 1))
			return dev_err_probe(adc->dev, -EINVAL,
				"Number of TM nodes %u greater than channels supported:%u\n",
				adc->n_tm_channels, (adc->num_sdams * 8) - 1);

		val = adc->n_tm_channels / 8;
		prop->sdam_index = val;
		prop->tm_chan_index = adc->n_tm_channels - (8*val);

		prop->timer = MEAS_INT_1S;
	}

	return 0;
}

static const struct adc5_data adc5_gen3_data_pmic = {
	.full_scale_code_volt = 0x70e4,
	.adc_chans = adc5_gen3_chans_pmic,
	.info = &adc5_gen3_info,
	.decimation = (unsigned int [ADC5_DECIMATION_SAMPLES_MAX])
				{85, 340, 1360},
	.hw_settle_1 = (unsigned int [VADC_HW_SETTLE_SAMPLES_MAX])
				{15, 100, 200, 300, 400, 500, 600, 700,
				1000, 2000, 4000, 8000, 16000, 32000,
				64000, 128000},
};

static const struct of_device_id adc5_match_table[] = {
	{
		.compatible = "qcom,spmi-adc5-gen3",
		.data = &adc5_gen3_data_pmic,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, adc5_match_table);

static int adc5_get_fw_data(struct adc5_chip *adc)
{
	const struct adc5_channels *adc_chan;
	struct iio_chan_spec *iio_chan;
	struct adc5_channel_prop *chan_props;
	struct fwnode_handle *child;
	unsigned int index = 0;
	int ret;

	adc->nchannels = device_get_child_node_count(adc->dev);
	if (!adc->nchannels)
		return -EINVAL;

	adc->iio_chans = devm_kcalloc(adc->dev, adc->nchannels,
				       sizeof(*adc->iio_chans), GFP_KERNEL);
	if (!adc->iio_chans)
		return -ENOMEM;

	adc->chan_props = devm_kcalloc(adc->dev, adc->nchannels,
					sizeof(*adc->chan_props), GFP_KERNEL);
	if (!adc->chan_props)
		return -ENOMEM;

	chan_props = adc->chan_props;
	adc->n_tm_channels = 0;
	iio_chan = adc->iio_chans;
	adc->data = device_get_match_data(adc->dev);
	if (!adc->data)
		adc->data = &adc5_gen3_data_pmic;

	device_for_each_child_node(adc->dev, child) {
		ret = adc5_gen3_get_fw_channel_data(adc, chan_props, child, adc->data);
		if (ret < 0) {
			fwnode_handle_put(child);
			return ret;
		}

		chan_props->chip = adc;
		chan_props->scale_fn_type =
			adc->data->adc_chans[chan_props->channel].scale_fn_type;
		adc_chan = &adc->data->adc_chans[chan_props->channel];
		iio_chan->channel = V_CHAN(*chan_props);
		iio_chan->info_mask_separate = adc_chan->info_mask;
		iio_chan->type = adc_chan->type;
		iio_chan->address = index;
		iio_chan->indexed = 1;
		iio_chan++;
		chan_props++;
		index++;
	}

	return 0;
}

static int adc5_gen3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct adc5_chip *adc;
	struct regmap *regmap;
	int ret, i;
	u32 *reg;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->regmap = regmap;
	adc->dev = dev;

	ret = device_property_count_u32(dev, "reg");
	if (ret < 0)
		return ret;

	adc->num_sdams = ret;

	reg = kcalloc(adc->num_sdams, sizeof(u32), GFP_KERNEL);
	if (!reg)
		return -ENOMEM;

	ret = device_property_read_u32_array(dev, "reg", reg, adc->num_sdams);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read reg property, ret = %d\n", ret);

	adc->base = devm_kcalloc(dev, adc->num_sdams, sizeof(*adc->base), GFP_KERNEL);
	if (!adc->base)
		return -ENOMEM;

	platform_set_drvdata(pdev, indio_dev);
	init_completion(&adc->complete);
	mutex_init(&adc->lock);

	for (i = 0; i < adc->num_sdams; i++) {
		adc->base[i].base_addr = reg[i];

		adc->base[i].irq_name = devm_kasprintf(dev, GFP_KERNEL, "adc-sdam%d", i);
		if (!adc->base[i].irq_name) {
			kfree(reg);
			ret = -ENOMEM;
			goto err_irq;
		}

		ret = platform_get_irq_byname(pdev, adc->base[i].irq_name);
		if (ret < 0) {
			kfree(reg);
			dev_err(dev, "Getting IRQ %d by name failed, ret = %d\n",
					adc->base[i].irq, ret);
			goto err_irq;
		}
		adc->base[i].irq = ret;

		ret = request_irq(adc->base[i].irq, adc5_gen3_isr, 0, adc->base[i].irq_name, adc);
		if (ret < 0) {
			kfree(reg);
			dev_err(dev, "Failed to request SDAM%d irq, ret = %d\n", i, ret);
			goto err_irq;
		}
	}
	kfree(reg);

	ret = devm_add_action(dev, adc5_gen3_disable, adc);
	if (ret < 0) {
		dev_err(dev, "failed to register adc disablement devm action, %d\n", ret);
		goto err_irq;
	}

	ret = adc5_get_fw_data(adc);
	if (ret < 0) {
		dev_err(dev, "adc get dt data failed, ret = %d\n", ret);
		goto err_irq;
	}

	ret = adc_tm_register_tzd(adc);
	if (ret < 0)
		goto err_irq;

	if (adc->n_tm_channels)
		INIT_WORK(&adc->tm_handler_work, tm_handler_work);

	indio_dev->name = pdev->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &adc5_gen3_info;
	indio_dev->channels = adc->iio_chans;
	indio_dev->num_channels = adc->nchannels;

	ret = devm_iio_device_register(dev, indio_dev);
	if (!ret)
		return 0;

err_irq:
	for (i = 0; i < adc->num_sdams; i++)
		free_irq(adc->base[i].irq, adc);

	return ret;
}

static struct platform_driver adc5_gen3_driver = {
	.driver = {
		.name = "qcom-spmi-adc5-gen3",
		.of_match_table = adc5_match_table,
	},
	.probe = adc5_gen3_probe,
};
module_platform_driver(adc5_gen3_driver);

MODULE_DESCRIPTION("Qualcomm Technologies Inc. PMIC5 Gen3 ADC driver");
MODULE_LICENSE("GPL");
