// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Microchip USB5807 USB 3.1 Hub
 * Configuration via SMBus.
 *
 * Copyright (c) 2023 Topic Embedded Products
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>

#define USB5807_CMD_ATTACH 0xAA55
#define USB5807_CMD_CONFIG 0x9937

#define USB5807_REG_LANE_SWAP 0x30FA

#define USB5807_NUM_PORTS 7


static int usb5807_write(struct i2c_client *i2c, void *buf, u8 len)
{
	int ret;
	struct i2c_msg msg = {
		.addr	= i2c->addr,
		.flags  = 0x0,
		.len	= len,
		.buf	= buf,
	};

	ret = i2c_transfer(i2c->adapter, &msg, 1);
	return ret < 0 ? ret : 0;
}

/*
 * Send a command sequence, which is an I2C write transaction, with the command
 * word in big endian and a terminating "0" byte.
 */
static int usb5807_command(struct i2c_client *i2c, u16 cmd)
{
	u8 buf[3] = {cmd >> 8, cmd & 0xff, 0};

	return usb5807_write(i2c, buf, sizeof(buf));
}

static int usb5807_prepare_reg_u8(struct i2c_client *i2c, u16 reg, u8 value)
{
	u8 buf[] = {
		0x00,
		0x00,		/* Memory offset */
		1 + 4,		/* Transaction size */
		0x00,		/* 0 = Register write operation */
		1,		/* Size of register data */
		(reg >> 8) & 0xff,
		reg & 0xff,	/* Register offset */
		value,		/* Register data */
		0		/* Terminating zero */
	};

	return usb5807_write(i2c, buf, sizeof(buf));
}

/*
 * Write an 8-bit register. First we must write the "set register" operation to
 * the chip's internal memory at offset 0, then issue a command to execute said
 * operation.
 */
static int usb5807_write_reg_u8(struct i2c_client *i2c, u16 reg, u8 value)
{
	int ret;

	ret = usb5807_prepare_reg_u8(i2c, reg, value);
	if (ret)
		return ret;

	return usb5807_command(i2c, USB5807_CMD_CONFIG);
}

/* Decode array of port numbers property into bit mask */
static u8 usb5807_get_ports_field(struct device *dev, const char *prop_name)
{
	struct property *prop;
	const __be32 *p;
	u32 port;
	u8 result = 0;

	of_property_for_each_u32(dev->of_node, prop_name, prop, p, port) {
		if (port < USB5807_NUM_PORTS)
			result |= BIT(port);
		else
			dev_warn(dev, "%s: port %u doesn't exist\n", prop_name,
				 port);
	}
	return result;
}

static int usb5807_i2c_probe(struct i2c_client *i2c)
{
	struct device_node *np = i2c->dev.of_node;
	struct gpio_desc *reset_gpio;
	int ret;
	u8 val;

	/* Reset the chip to bring it into configuration mode */
	reset_gpio = devm_gpiod_get_optional(&i2c->dev, "reset",
					     GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio)) {
		return dev_err_probe(&i2c->dev, PTR_ERR(reset_gpio),
				     "Failed to request reset GPIO\n");
	}
	/* Reset timing: Assert for >= 5 us */
	usleep_range(5, 10);

	/* Lock the bus for >= 1ms while the hub reads the I2C strapping */
	i2c_lock_bus(i2c->adapter, I2C_LOCK_SEGMENT);

	gpiod_set_value_cansleep(reset_gpio, 0);
	usleep_range(1000, 2000);

	i2c_unlock_bus(i2c->adapter, I2C_LOCK_SEGMENT);

	/* The hub device needs additional time to boot up */
	msleep(20);

	val = usb5807_get_ports_field(&i2c->dev, "swap-dx-lanes");
	if (val) {
		ret = usb5807_write_reg_u8(i2c, USB5807_REG_LANE_SWAP, val);
		if (ret < 0)
			dev_err(&i2c->dev, "Failed writing config: %d\n", ret);
	}

	/*
	 * Send the "Attach" command which makes the device disappear from the
	 * I2C bus and starts USB enumeration.
	 */
	ret = usb5807_command(i2c, USB5807_CMD_ATTACH);
	if (ret) {
		dev_err(&i2c->dev, "Failed sending ATTACH command: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id usb5807_of_match[] = {
	{ .compatible = "microchip,usb5807" },
	{ } /* sentinel */
};
MODULE_DEVICE_TABLE(of, usb5807_of_match);

static const struct i2c_device_id usb5807_id[] = {
	{ "usb5807", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, usb5807_id);

static struct i2c_driver usb5807_i2c_driver = {
	.driver = {
		.name = "usb5807",
		.of_match_table = of_match_ptr(usb5807_of_match),
	},
	.probe    = usb5807_i2c_probe,
	.id_table = usb5807_id,
};

module_i2c_driver(usb5807_i2c_driver);

MODULE_DESCRIPTION("USB5807 USB 3.1 Hub Controller Driver");
MODULE_LICENSE("GPL");
