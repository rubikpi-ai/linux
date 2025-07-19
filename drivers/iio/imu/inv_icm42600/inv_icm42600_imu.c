// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Invensense, Inc.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/math64.h>

#include <linux/iio/buffer.h>
#include <linux/iio/common/inv_sensors_timestamp.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>

#include "inv_icm42600.h"
#include "inv_icm42600_temp.h"
#include "inv_icm42600_buffer.h"

enum inv_icm42600_imu_scan {
	INV_ICM42600_IMU_SCAN_ACCEL_X,
	INV_ICM42600_IMU_SCAN_ACCEL_Y,
	INV_ICM42600_IMU_SCAN_ACCEL_Z,
	INV_ICM42600_IMU_SCAN_GYRO_X,
	INV_ICM42600_IMU_SCAN_GYRO_Y,
	INV_ICM42600_IMU_SCAN_GYRO_Z,
	INV_ICM42600_IMU_SCAN_TEMP,
	INV_ICM42600_IMU_SCAN_TIMESTAMP,
};

#define INV_ICM42600_IMU_ACCEL_CHAN(_modifier, _index, _ext_info)		\
	{								\
		.type = IIO_ACCEL,					\
		.modified = 1,						\
		.channel2 = _modifier,					\
		.info_mask_separate =					\
			BIT(IIO_CHAN_INFO_RAW) |			\
			BIT(IIO_CHAN_INFO_CALIBBIAS),			\
		.info_mask_shared_by_type =				\
			BIT(IIO_CHAN_INFO_SCALE),			\
		.info_mask_shared_by_type_available =			\
			BIT(IIO_CHAN_INFO_SCALE) |			\
			BIT(IIO_CHAN_INFO_CALIBBIAS),			\
		.info_mask_shared_by_all =				\
			BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
		.info_mask_shared_by_all_available =			\
			BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
		.scan_index = _index,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 16,					\
			.storagebits = 16,				\
			.endianness = IIO_LE,				\
		},							\
		.ext_info = _ext_info,					\
	}

#define INV_ICM42600_IMU_GYRO_CHAN(_modifier, _index, _ext_info)		\
	{								\
		.type = IIO_ANGL_VEL,					\
		.modified = 1,						\
		.channel2 = _modifier,					\
		.info_mask_separate =					\
			BIT(IIO_CHAN_INFO_RAW) |			\
			BIT(IIO_CHAN_INFO_CALIBBIAS),			\
		.info_mask_shared_by_type =				\
			BIT(IIO_CHAN_INFO_SCALE),			\
		.info_mask_shared_by_type_available =			\
			BIT(IIO_CHAN_INFO_SCALE) |			\
			BIT(IIO_CHAN_INFO_CALIBBIAS),			\
		.info_mask_shared_by_all =				\
			BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
		.info_mask_shared_by_all_available =			\
			BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
		.scan_index = _index,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 16,					\
			.storagebits = 16,				\
			.endianness = IIO_LE,				\
		},							\
		.ext_info = _ext_info,					\
	}

static const struct iio_chan_spec_ext_info inv_icm42600_imu_ext_infos[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_ALL, inv_icm42600_get_mount_matrix),
	{},
};

static const struct iio_chan_spec inv_icm42600_imu_channels[] = {
	INV_ICM42600_IMU_ACCEL_CHAN(IIO_MOD_X, INV_ICM42600_IMU_SCAN_ACCEL_X,
					inv_icm42600_imu_ext_infos),
	INV_ICM42600_IMU_ACCEL_CHAN(IIO_MOD_Y, INV_ICM42600_IMU_SCAN_ACCEL_Y,
					inv_icm42600_imu_ext_infos),
	INV_ICM42600_IMU_ACCEL_CHAN(IIO_MOD_Z, INV_ICM42600_IMU_SCAN_ACCEL_Z,
					inv_icm42600_imu_ext_infos),
	INV_ICM42600_IMU_GYRO_CHAN(IIO_MOD_X, INV_ICM42600_IMU_SCAN_GYRO_X,
					inv_icm42600_imu_ext_infos),
	INV_ICM42600_IMU_GYRO_CHAN(IIO_MOD_Y, INV_ICM42600_IMU_SCAN_GYRO_Y,
					inv_icm42600_imu_ext_infos),
	INV_ICM42600_IMU_GYRO_CHAN(IIO_MOD_Z, INV_ICM42600_IMU_SCAN_GYRO_Z,
					inv_icm42600_imu_ext_infos),
	INV_ICM42600_TEMP_CHAN(INV_ICM42600_IMU_SCAN_TEMP),
	IIO_CHAN_SOFT_TIMESTAMP(INV_ICM42600_IMU_SCAN_TIMESTAMP),
};

/*
 * IIO buffer data: size must be a power of 2 and timestamp aligned
 * 24 bytes: 6 bytes acceleration, 6 bytes gyroscope, 2 bytes temperature, 8 bytes timestamp
 */
struct inv_icm42600_imu_buffer {
	struct inv_icm42600_fifo_sensor_data accel;
	struct inv_icm42600_fifo_sensor_data gyro;
	int16_t temp;
	int64_t timestamp __aligned(8);
};

#define INV_ICM42600_SCAN_MASK_ACCEL_3AXIS				\
	(BIT(INV_ICM42600_IMU_SCAN_ACCEL_X) |				\
	BIT(INV_ICM42600_IMU_SCAN_ACCEL_Y) |				\
	BIT(INV_ICM42600_IMU_SCAN_ACCEL_Z))

#define INV_ICM42600_SCAN_MASK_GYRO_3AXIS				\
	(BIT(INV_ICM42600_IMU_SCAN_GYRO_X) |				\
	BIT(INV_ICM42600_IMU_SCAN_GYRO_Y) |				\
	BIT(INV_ICM42600_IMU_SCAN_GYRO_Z))

#define INV_ICM42600_SCAN_MASK_TEMP	BIT(INV_ICM42600_IMU_SCAN_TEMP)

static const unsigned long inv_icm42600_imu_scan_masks[] = {
	/* 3-axis accel + 3-axis gyro + temperature */
	INV_ICM42600_SCAN_MASK_ACCEL_3AXIS | INV_ICM42600_SCAN_MASK_GYRO_3AXIS | INV_ICM42600_SCAN_MASK_TEMP,
	/* 3-axis accel + 3-axis gyro */
	INV_ICM42600_SCAN_MASK_ACCEL_3AXIS | INV_ICM42600_SCAN_MASK_GYRO_3AXIS,
	/* 3-axis accel + temperature */
	INV_ICM42600_SCAN_MASK_ACCEL_3AXIS | INV_ICM42600_SCAN_MASK_TEMP,
	/* 3-axis gyro + temperature */
	INV_ICM42600_SCAN_MASK_GYRO_3AXIS | INV_ICM42600_SCAN_MASK_TEMP,
	/* 3-axis accel */
	INV_ICM42600_SCAN_MASK_ACCEL_3AXIS,
	/* 3-axis gyro */
	INV_ICM42600_SCAN_MASK_GYRO_3AXIS,
	0,
};

/* Enable accelerometer and/or gyroscope sensors and FIFO write */
static int inv_icm42600_imu_update_scan_mode(struct iio_dev *indio_dev,
					    const unsigned long *scan_mask)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_sensors_timestamp *ts = iio_priv(indio_dev);
	struct inv_icm42600_sensor_conf accel_conf = INV_ICM42600_SENSOR_CONF_INIT;
	struct inv_icm42600_sensor_conf gyro_conf = INV_ICM42600_SENSOR_CONF_INIT;
	unsigned int fifo_en = 0;
	unsigned int sleep_accel = 0;
	unsigned int sleep_gyro = 0;
	unsigned int sleep_temp = 0;
	unsigned int sleep;
	int ret;

	mutex_lock(&st->lock);

	if (*scan_mask & INV_ICM42600_SCAN_MASK_TEMP) {
		/* enable temp sensor */
		ret = inv_icm42600_set_temp_conf(st, true, &sleep_temp);
		if (ret)
			goto out_unlock;
		fifo_en |= INV_ICM42600_SENSOR_TEMP;
	}

	if (*scan_mask & INV_ICM42600_SCAN_MASK_ACCEL_3AXIS) {
		/* enable accel sensor */
		accel_conf.mode = INV_ICM42600_SENSOR_MODE_LOW_NOISE;
		ret = inv_icm42600_set_accel_conf(st, &accel_conf, &sleep_accel);
		if (ret)
			goto out_unlock;
		fifo_en |= INV_ICM42600_SENSOR_ACCEL;
	}

	if (*scan_mask & INV_ICM42600_SCAN_MASK_GYRO_3AXIS) {
		/* enable gyro sensor */
		gyro_conf.mode = INV_ICM42600_SENSOR_MODE_LOW_NOISE;
		ret = inv_icm42600_set_gyro_conf(st, &gyro_conf, &sleep_gyro);
		if (ret)
			goto out_unlock;
		fifo_en |= INV_ICM42600_SENSOR_GYRO;
	}

	/* update data FIFO write */
	inv_sensors_timestamp_apply_odr(ts, 0, 0, 0);
	ret = inv_icm42600_buffer_set_fifo_en(st, fifo_en | st->fifo.en);
	if (ret)
		goto out_unlock;

	ret = inv_icm42600_buffer_update_watermark(st);

out_unlock:
	mutex_unlock(&st->lock);
	/* sleep maximum required time */
	sleep = sleep_accel;
	if (sleep_gyro > sleep)
		sleep = sleep_gyro;
	if (sleep_temp > sleep)
		sleep = sleep_temp;
	if (sleep)
		msleep(sleep);
	return ret;
}

/* Read accelerometer or gyroscope sensor raw data */
static int inv_icm42600_imu_read_sensor(struct inv_icm42600_state *st,
					struct iio_chan_spec const *chan,
					int16_t *val)
{
	struct device *dev = regmap_get_device(st->map);
	struct inv_icm42600_sensor_conf conf = INV_ICM42600_SENSOR_CONF_INIT;
	unsigned int reg;
	__be16 *data;
	int ret;

	switch (chan->type) {
	case IIO_ACCEL:
		switch (chan->channel2) {
		case IIO_MOD_X:
			reg = INV_ICM42600_REG_ACCEL_DATA_X;
			break;
		case IIO_MOD_Y:
			reg = INV_ICM42600_REG_ACCEL_DATA_Y;
			break;
		case IIO_MOD_Z:
			reg = INV_ICM42600_REG_ACCEL_DATA_Z;
			break;
		default:
			return -EINVAL;
		}
		conf.mode = INV_ICM42600_SENSOR_MODE_LOW_NOISE;
		ret = inv_icm42600_set_accel_conf(st, &conf, NULL);
		break;
	case IIO_ANGL_VEL:
		switch (chan->channel2) {
		case IIO_MOD_X:
			reg = INV_ICM42600_REG_GYRO_DATA_X;
			break;
		case IIO_MOD_Y:
			reg = INV_ICM42600_REG_GYRO_DATA_Y;
			break;
		case IIO_MOD_Z:
			reg = INV_ICM42600_REG_GYRO_DATA_Z;
			break;
		default:
			return -EINVAL;
		}
		conf.mode = INV_ICM42600_SENSOR_MODE_LOW_NOISE;
		ret = inv_icm42600_set_gyro_conf(st, &conf, NULL);
		break;
	default:
		return -EINVAL;
	}

	pm_runtime_get_sync(dev);
	mutex_lock(&st->lock);

	if (ret)
		goto exit;

	/* read accel register data */
	data = (__be16 *)&st->buffer[0];
	ret = regmap_bulk_read(st->map, reg, data, sizeof(*data));
	if (ret)
		goto exit;

	*val = (int16_t)be16_to_cpup(data);
	if (*val == INV_ICM42600_DATA_INVALID)
		ret = -EINVAL;
exit:
	mutex_unlock(&st->lock);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return ret;
}

/* Calibration bias values for accelerometer or gyroscope */
static int inv_icm42600_imu_read_offset(struct inv_icm42600_state *st,
					struct iio_chan_spec const *chan,
					int *val, int *val2)
{
	if (chan->type == IIO_ACCEL) {
		return inv_icm42600_accel_read_offset(st, chan, val, val2);
	} else if (chan->type == IIO_ANGL_VEL) {
		return inv_icm42600_gyro_read_offset(st, chan, val, val2);
	}
	return -EINVAL;
}

static int inv_icm42600_imu_write_offset(struct inv_icm42600_state *st,
					 struct iio_chan_spec const *chan,
					 int val, int val2)
{
	if (chan->type == IIO_ACCEL) {
		return inv_icm42600_accel_write_offset(st, chan, val, val2);
	} else if (chan->type == IIO_ANGL_VEL) {
		return inv_icm42600_gyro_write_offset(st, chan, val, val2);
	}
	return -EINVAL;
}

/* Raw IIO_CHAN_INFO_RAW interface */
static int inv_icm42600_imu_read_raw(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     int *val, int *val2, long mask)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	int16_t data;
	int ret;

	switch (chan->type) {
	case IIO_ACCEL:
	case IIO_ANGL_VEL:
		break;
	case IIO_TEMP:
		return inv_icm42600_temp_read_raw(indio_dev, chan, val, val2, mask);
	default:
		return -EINVAL;
	}

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = inv_icm42600_imu_read_sensor(st, chan, &data);
		iio_device_release_direct_mode(indio_dev);
		if (ret)
			return ret;
		*val = data;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_ACCEL)
			return inv_icm42600_accel_read_scale(st, val, val2);
		else if (chan->type == IIO_ANGL_VEL)
			return inv_icm42600_gyro_read_scale(st, val, val2);
		return -EINVAL;
	case IIO_CHAN_INFO_SAMP_FREQ:
		/* Take the accelerometer ODR as representative of both sensors */
		return inv_icm42600_accel_read_odr(st, val, val2);
	case IIO_CHAN_INFO_CALIBBIAS:
		return inv_icm42600_imu_read_offset(st, chan, val, val2);
	default:
		return -EINVAL;
	}
}

static int inv_icm42600_imu_read_avail(struct iio_dev *indio_dev,
				       struct iio_chan_spec const *chan,
				       const int **vals,
				       int *type, int *length, long mask)
{
	switch (chan->type) {
	case IIO_ACCEL:
		switch (mask) {
		case IIO_CHAN_INFO_SCALE:
			*vals = inv_icm42600_accel_scale;
			*type = IIO_VAL_INT_PLUS_NANO;
			*length = ARRAY_SIZE(inv_icm42600_accel_scale);
			return IIO_AVAIL_LIST;
		case IIO_CHAN_INFO_CALIBBIAS:
			*vals = inv_icm42600_accel_calibbias;
			*type = IIO_VAL_INT_PLUS_MICRO;
			return IIO_AVAIL_RANGE;
		default:
			return -EINVAL;
		}
	case IIO_ANGL_VEL:
		switch (mask) {
		case IIO_CHAN_INFO_SCALE:
			*vals = inv_icm42600_gyro_scale;
			*type = IIO_VAL_INT_PLUS_NANO;
			*length = ARRAY_SIZE(inv_icm42600_gyro_scale);
			return IIO_AVAIL_LIST;
		case IIO_CHAN_INFO_CALIBBIAS:
			*vals = inv_icm42600_gyro_calibbias;
			*type = IIO_VAL_INT_PLUS_NANO;
			return IIO_AVAIL_RANGE;
		default:
			return -EINVAL;
		}
	case IIO_TEMP:
		// Temperature uses the same shared sampling frequency
		break;
	default:
		return -EINVAL;
	}

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = inv_icm42600_accel_odr;
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = ARRAY_SIZE(inv_icm42600_accel_odr);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int inv_icm42600_imu_write_raw(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      int val, int val2, long mask)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_icm42600_sensor_conf conf = {-1, -1, -1, -1};
	int ret;
	int idx;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		
		if (chan->type == IIO_ACCEL)
			ret = inv_icm42600_accel_write_scale(st, val, val2);
		else if (chan->type == IIO_ANGL_VEL)
			ret = inv_icm42600_gyro_write_scale(st, val, val2);
		else
			ret = -EINVAL;
		
		iio_device_release_direct_mode(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		// Set same sampling frequency for both accel and gyro
		// First for accel
		ret = inv_icm42600_accel_write_odr(indio_dev, val, val2);
		if (ret)
			return ret;
		
		// Then for gyro
		for (idx = 0; idx < INV_ICM42600_GYRO_ODR_LEN; idx += 2) {
			if (val == inv_icm42600_gyro_odr[idx] &&
			    val2 == inv_icm42600_gyro_odr[idx + 1]) {
				conf.odr = inv_icm42600_gyro_odr_conv[idx / 2];
				break;
			}
		}
		
		if (conf.odr < 0)
			return -EINVAL;
		
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		
		ret = inv_icm42600_set_gyro_conf(st, &conf, NULL);
		
		iio_device_release_direct_mode(indio_dev);
		return ret;
		
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		
		ret = inv_icm42600_imu_write_offset(st, chan, val, val2);
		
		iio_device_release_direct_mode(indio_dev);
		return ret;
	default:
		return -EINVAL;
	}
}

static int inv_icm42600_imu_write_raw_get_fmt(struct iio_dev *indio_dev,
					      struct iio_chan_spec const *chan,
					      long mask)
{
	switch (chan->type) {
	case IIO_ACCEL:
	case IIO_ANGL_VEL:
		break;
	default:
		return -EINVAL;
	}

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBBIAS:
		if (chan->type == IIO_ACCEL)
			return IIO_VAL_INT_PLUS_MICRO;
		else // IIO_ANGL_VEL
			return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static int inv_icm42600_imu_hwfifo_set_watermark(struct iio_dev *indio_dev,
						 unsigned int val)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	int ret;

	mutex_lock(&st->lock);

	st->fifo.watermark.accel = val;
	st->fifo.watermark.gyro = val;
	ret = inv_icm42600_buffer_update_watermark(st);

	mutex_unlock(&st->lock);

	return ret;
}

static int inv_icm42600_imu_hwfifo_flush(struct iio_dev *indio_dev,
					 unsigned int count)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	int ret;

	if (count == 0)
		return 0;

	mutex_lock(&st->lock);

	ret = inv_icm42600_buffer_hwfifo_flush(st, count);
	if (!ret)
		ret = st->fifo.nb.total;

	mutex_unlock(&st->lock);

	return ret;
}

static const struct iio_info inv_icm42600_imu_info = {
	.read_raw = inv_icm42600_imu_read_raw,
	.read_avail = inv_icm42600_imu_read_avail,
	.write_raw = inv_icm42600_imu_write_raw,
	.write_raw_get_fmt = inv_icm42600_imu_write_raw_get_fmt,
	.debugfs_reg_access = inv_icm42600_debugfs_reg,
	.update_scan_mode = inv_icm42600_imu_update_scan_mode,
	.hwfifo_set_watermark = inv_icm42600_imu_hwfifo_set_watermark,
	.hwfifo_flush_to_buffer = inv_icm42600_imu_hwfifo_flush,
};

struct iio_dev *inv_icm42600_imu_init(struct inv_icm42600_state *st)
{
	struct device *dev = regmap_get_device(st->map);
	const char *name;
	struct inv_sensors_timestamp_chip ts_chip;
	struct inv_sensors_timestamp *ts;
	struct iio_dev *indio_dev;
	int ret;

	name = devm_kasprintf(dev, GFP_KERNEL, "%s-imu", st->name);
	if (!name)
		return ERR_PTR(-ENOMEM);

	indio_dev = devm_iio_device_alloc(dev, sizeof(*ts));
	if (!indio_dev)
		return ERR_PTR(-ENOMEM);

	/*
	 * clock period is 32kHz (31250ns)
	 * jitter is +/- 2% (20 per mille)
	 */
	ts_chip.clock_period = 31250;
	ts_chip.jitter = 20;
	ts_chip.init_period = inv_icm42600_odr_to_period(st->conf.accel.odr);
	ts = iio_priv(indio_dev);
	inv_sensors_timestamp_init(ts, &ts_chip);

	iio_device_set_drvdata(indio_dev, st);
	indio_dev->name = name;
	indio_dev->info = &inv_icm42600_imu_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = inv_icm42600_imu_channels;
	indio_dev->num_channels = ARRAY_SIZE(inv_icm42600_imu_channels);
	indio_dev->available_scan_masks = inv_icm42600_imu_scan_masks;
	indio_dev->setup_ops = &inv_icm42600_buffer_ops;

	ret = devm_iio_kfifo_buffer_setup(dev, indio_dev,
					  &inv_icm42600_buffer_ops);
	if (ret)
		return ERR_PTR(ret);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return ERR_PTR(ret);

	return indio_dev;
}

int inv_icm42600_imu_parse_fifo(struct iio_dev *indio_dev)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_sensors_timestamp *ts = iio_priv(indio_dev);
	ssize_t i, size;
	unsigned int no;
	const void *accel, *gyro, *timestamp;
	const int8_t *temp;
	unsigned int odr;
	int64_t ts_val;
	struct inv_icm42600_imu_buffer buffer;

	/* parse all fifo packets */
	for (i = 0, no = 0; i < st->fifo.count; i += size, ++no) {
		size = inv_icm42600_fifo_decode_packet(&st->fifo.data[i],
				&accel, &gyro, &temp, &timestamp, &odr);
		/* quit if error or FIFO is empty */
		if (size <= 0)
			return size;

		/* skip if all data is invalid or not needed */
		if ((accel == NULL || !inv_icm42600_fifo_is_data_valid(accel)) &&
		    (gyro == NULL || !inv_icm42600_fifo_is_data_valid(gyro)))
			continue;

		/* update odr */
		inv_sensors_timestamp_apply_odr(ts, st->fifo.period,
						st->fifo.nb.total, no);

		/* buffer is copied to userspace, zeroing it to avoid any data leak */
		memset(&buffer, 0, sizeof(buffer));
		
		/* Copy accel data if valid */
		if (accel != NULL && inv_icm42600_fifo_is_data_valid(accel))
			memcpy(&buffer.accel, accel, sizeof(buffer.accel));
		
		/* Copy gyro data if valid */
		if (gyro != NULL && inv_icm42600_fifo_is_data_valid(gyro))
			memcpy(&buffer.gyro, gyro, sizeof(buffer.gyro));
		
		/* convert 8 bits FIFO temperature in high resolution format */
		buffer.temp = temp ? (*temp * 64) : 0;
		ts_val = inv_sensors_timestamp_pop(ts);
		iio_push_to_buffers_with_timestamp(indio_dev, &buffer, ts_val);
	}

	return 0;
}
