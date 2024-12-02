// SPDX-License-Identifier: GPL-2.0+
/*
 *  HUSB302 Type-C Chip Driver
 */
#ifndef __HUSB238_H__
#define __HUSB238_H__

/* HUSB238 ADDR */
#define HUSB238                   0x08
#define HUSB238_DEBOUNCE_DELAY_MS 2000
/* REG ADDR */
#define PD_STATUS0                0x00
#define PD_STATUS1                0x01
#define SRC_PDO_5V                0x02
#define SRC_PDO_9V                0x03
#define SRC_PDO_12V               0x04
#define SRC_PDO_15V               0x05
#define SRC_PDO_18V               0x06
#define SRC_PDO_20V               0x07
#define SRC_PDO                   0x08
#define GO_COMMAND                0x09
/* SEL_SETTING */
#define SEL_Not                   0x00
#define SEL_PDO_5V                0x10
#define SEL_PDO_9V                0x20
#define SEL_PDO_12V               0x30
#define SEL_PDO_15V               0x80
#define SEL_PDO_18V               0x90
#define SEL_PDO_20V               0xA0
/* COMMAND */
#define CMD_Requests_PDO          0x01
#define CMD_Get_SRC_Cap           0x04
#define CMD_Tx_HardReset          0x10
#define PD_STATUS0_REG            0x00
#define PD_STATUS1_REG            0x01
#define SRC_PDO_5V_REG            0x02
#define SRC_PDO_9V_REG            0x03
#define SRC_PDO_12V_REG           0x04
#define SRC_PDO_15V_REG           0x05
#define SRC_PDO_18V_REG           0x06
#define SRC_PDO_20V_REG           0x07
#define SRC_PDO_REG               0x08
#define GO_COMMAND_REG            0x09
#define DPDM_STATUS_REG           0x22

struct husb238_chip {
  struct device *dev;
  struct i2c_client *i2c_client;
  struct workqueue_struct *wq;
  struct delayed_work husb238_handler;
};

#endif  //__HUSB238_H__
