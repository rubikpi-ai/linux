// SPDX-License-Identifier: GPL-2.0+
/*
 *  HUSB302 Type-C Chip Driver
 */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/extcon.h>
#include <linux/gpio/consumer.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/extcon.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include "husb238.h"

/* PD_STATUS0_REG 0x00: [3:0] The current information when an explicit is established */
static const char *PD_QC_SRC_VOLTAGE[10] = {"UNATTACHED_MODE", "PD5V", "PD9V", "PD12V", "PD15V",
                            "PD18V", "PD20V", "QC9V/2A", "QC12V/1.5A",
                            "Power less than 36W, please replace the adapter"};
/* PD_STATUS0_REG 0x00: [7:4] The voltage information when an explicit is established */
static const char *PD_SRC_CURRENT[16] = {"0.5A", "0.7A", "1.00A", "1.25A", "1.50A", "1.75A", "2.00A",
                            "2.25A", "2.50A", "2.75A", "3.00A", "3.25A", "3.50A", "4.00A",
                            "4.50A", "5.00A"};
/* PD_STATUS1_REG 0x01: [1:0] Current information in TYPEC_MODE or DPDM_MODE */
static const char *DPDM_5V_CURRENT[5] = {"Default current", "1.5A", "2.4A", "3.0A"};
/* PD_STATUS1_REG 0x01: [2] Voltage information in TYPE_MODE or DPDM_MODE */
static const char *DPDM_5V_VOLTAGE[3] = {"UNATTACHED MODE", "5V"};
/* PD_STATUS1_REG 0x01: [7] 0:CC1 is connected to CC/ 1:CC2 is connected to CC */
static const char *DD_DIR[3] = {"CC1", "CC2"};
/* DPDM_STATUS_REG 0x22: [7:0] DPDM status */
static const char *DPDM_STATUS[8] = {"Apple mode support", "SDP", "CDP", "DCP mode", "QC contrast",
                      "QC 5V", "QC 9V", "QC 12V" };
static char husb_buf[100];

static int husb238_i2c_read(struct husb238_chip *chip,
			    u8 address, u8 *data)
{
	int ret = 0;
	struct device *dev = chip->dev;
	ret = i2c_smbus_read_byte_data(chip->i2c_client, address);
	*data = (u8)ret;
	if (ret < 0)
	  dev_err(dev, "cannot read %02x, ret=%d", address, ret);
	return ret;
}

static void husb238_handler_work(struct work_struct *work)
{
	struct husb238_chip *chip = container_of(work, struct husb238_chip,
						 husb238_handler.work);
	struct device *dev = chip->dev;
	int ret = 0;
	u8 status;

	ret = husb238_i2c_read(chip, PD_STATUS0_REG, &status);
	if((((status >> 4) & 0x0F) > 0x02) && ((status & 0x0F ) > 0x06)) {
		scnprintf(husb_buf, PAGE_SIZE, "%s-%s\n",
                          PD_QC_SRC_VOLTAGE[(status >> 4) & 0x0F],
                          PD_SRC_CURRENT[status & 0x0F]);
	} else {
		scnprintf(husb_buf, PAGE_SIZE, "%s\n", PD_QC_SRC_VOLTAGE[ 9 ]);
	}
}

static ssize_t power_supply_show(struct device *_dev, struct device_attribute *attr,
			     char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n",husb_buf);
}
static DEVICE_ATTR_RO(power_supply);

static int husb230_probe(struct i2c_client *client)
{
	struct husb238_chip *chip;
	struct i2c_adapter *adapter = client->adapter;
	int ret = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_I2C_BLOCK)) {
		dev_err(&client->dev,
			"I2C/SMBus block functionality not supported!\n");
		return -ENODEV;
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->i2c_client = client;
	chip->dev = &client->dev;

  	chip->wq = create_singlethread_workqueue(dev_name(chip->dev));
		if (!chip->wq)
			return -ENOMEM;

	device_create_file(&client->dev, &dev_attr_power_supply);

	INIT_DELAYED_WORK(&chip->husb238_handler, husb238_handler_work);
 	mod_delayed_work(chip->wq, &chip->husb238_handler,
				 msecs_to_jiffies(HUSB238_DEBOUNCE_DELAY_MS));
	return ret;
}

static void husb238_remove(struct i2c_client *client)
{
	struct husb238_chip *chip = i2c_get_clientdata(client);
	cancel_delayed_work(&chip->husb238_handler);
}

static const struct of_device_id husb238_dt_match[] __maybe_unused = {
	{.compatible = "husb238_pdc"},
	{},
};
MODULE_DEVICE_TABLE(of, husb238_dt_match);

static const struct i2c_device_id husb_i2c_device_id[] = {
	{"husb238", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, husb_i2c_device_id);

static struct i2c_driver husb238_driver = {
	.driver = {
		   .name = "husb238",
		   .of_match_table = of_match_ptr(husb238_dt_match),
		   },
	.probe = husb230_probe,
	.remove = husb238_remove,
};
module_i2c_driver(husb238_driver);

MODULE_AUTHOR("Weining Qi weining.qi@thundersoft.com");
MODULE_DESCRIPTION("husb238 Type-C Chip Driver");
MODULE_LICENSE("GPL");
