/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _F_QDSS_H
#define _F_QDSS_H

#include <linux/completion.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>
#include <linux/soc/qcom/usb_qdss.h>

/**
 *  struct usb_qdss_ch - Usb qdss channel structure
 *  @name: Used to store channel name.
 *  @list: Current active list in a function.
 *  @notify: usb_notifier use to notify the coresight driver.
 *  @priv: Private data of the channel used for referencing tmc.
 */
struct usb_qdss_ch {
	const char *name;
	struct list_head list;
	void (*notify)(void *priv, unsigned int event,
		struct qdss_request *d_req, struct usb_qdss_ch *ch);
	void *priv;
};

/**
 *  struct gqdss - Structure of qdss saving the function & ep instance
 *  @function: Reference to usb_function  for configfs linkage.
 *  @data: Usb endpoint reference used to data transfer.
 */
struct gqdss {
	struct usb_function function;
	struct usb_ep *data;
};

/**
 *  struct f_qdss - Usb qdss function driver structure
 *  @port: Contains the function & ep data info.
 *  @gadget: Reference to usb_gadget driver.
 *  @data_iface_id: Saves the usb_interface_id to data transfers.
 *  @usb_connected: Indicated if usb is connected.
 *  @ch: Reference to the channel used by coresight for data transfer.
 *  @data_write_pool: Used during allocation to save the requests to be used.
 *  @queued_data_pool: Used to mark the request in process state, waiting for
 *		       completion.
 *  @dequeued_data_pool: Used when channel is closed to wait for request to be
 *			 drained.
 *  @connect_w: Connect workqueue used to send notification to coresight driver.
 *  @disconnect_w: Disconnect workqueue used to send notification to coresight
 *		   driver.
 *  @lock: Spinlock used to protect the port instance from racing.
 *  @data_enabled: Used to mark if data flow enabled.
 *  @wq: Workqueue to carry out the conect/disconnect works.
 *  @mutex: Used to protect the data_write_pool & open variable.
 *  @opened: Used to mark channel open/close.
 *  @dequeue_done: Completion marker for dequeue completion.
 */
struct f_qdss {
	struct gqdss port;
	struct usb_gadget *gadget;
	u8 data_iface_id;
	int usb_connected;
	struct usb_qdss_ch ch;

	struct list_head data_write_pool;
	struct list_head queued_data_pool;
	struct list_head dequeued_data_pool;

	struct work_struct connect_w;
	struct work_struct disconnect_w;
	spinlock_t lock;
	unsigned int data_enabled:1;
	struct workqueue_struct *wq;

	struct mutex mutex;
	bool opened;	/* protected by 'mutex' */
	struct completion dequeue_done;
};

/**
 *  struct usb_qdss_opts - Usb qdss ops structure used for functionality
 *			   with configfs.
 *  @func_inst: usb function instacne reference to the driver.
 *  @usb_qdss: References to f_qdss variable.
 *  @channel_name: Name of the active channel.
 */
struct usb_qdss_opts {
	struct usb_function_instance func_inst;
	struct f_qdss *usb_qdss;
	char *channel_name;
};

/**
 *  struct qdss_req - Usb qdss request structure for write operation
 *		      by coresight driver.
 *  @usb_req: Reference to the usb requests to be queued.
 *  @qdss_req: Reference to the qdss_request used by coresight.
 *  @list: List head to save the request during ops for moving from
 *	   one pool to another.
 */
struct qdss_req {
	struct usb_request *usb_req;
	struct qdss_request *qdss_req;
	struct list_head list;
};

#endif /* _F_QDSS_H */
