/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __LINUX_USB_QDSS_H
#define __LINUX_USB_QDSS_H

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>

/**
 *  struct qdss_request - QDSS request structure for usb 7 coresight usage.
 *  @buf: Buffer point to store the data.
 *  @length: Request length
 *  @actual: Actual length of the data buffer.
 *  @status: Current status of the buffer.
 *  @context: Pointer to save the context of the request.
 *  @sg:Pointer to the scatterlist.
 *  @num_sgs: Number of scatter gather lists required.
 */
struct qdss_request {
	char *buf;
	int length;
	int actual;
	int status;
	void *context;
	struct scatterlist *sg;
	unsigned int num_sgs;
};

/** enum qdss_state - QDSS state enum */
enum qdss_state {
	USB_QDSS_CONNECT,
	USB_QDSS_DISCONNECT,
	USB_QDSS_DATA_WRITE_DONE,
};

/* References of usb_qdss_ch used in functions below */
struct usb_qdss_ch;

#if IS_ENABLED(CONFIG_USB_F_QDSS)

/**
 * usb_qdss_open - Used to open a valid usb channel to perform the r/w operations.
 * @name: Name of the channel
 * @priv: variable to save the reference of qdss instance.
 * @notify: usb_notifer used to notify the coresight driver.
 */
struct usb_qdss_ch *usb_qdss_open(const char *name, void *priv,
	void (*notify)(void *priv, unsigned int event,
		struct qdss_request *d_req, struct usb_qdss_ch *ch));
/**
 * usb_qdss_close - Used to close the current active usb channel.
 *	 As part of the execution, function will dequeue any pending
 *	 requests & calls free_req.
 * @ch: Valid usb channel to send data.
 */
void usb_qdss_close(struct usb_qdss_ch *ch);
/**
 * usb_qdss_alloc_req - Accepts the usb channel and number of write buffers
 *	which is used to call the usb_ep_alloc_request. Further the completion
 *	handler qdss_write_complete is assigned which would be called on
 *	completion callback.
 * @ch: Valid usb channel to send data.
 */
int usb_qdss_alloc_req(struct usb_qdss_ch *ch, int n_write);
/**
 * usb_qdss_alloc_req - As the name suggestions, this functions frees the usb
 *	requests from the active pool.
 * @ch: Valid usb channel to send data.
 * @n_write:Number of write buffers.
 */
void usb_qdss_free_req(struct usb_qdss_ch *ch);
/**
 * usb_qdss_write - This function is responsible for performing ep_queue
 *	operation to usb gadget driver.
 * @ch: Valid usb channel to send data.
 * @d_req: qdss request which need to be queued.
 */
int usb_qdss_write(struct usb_qdss_ch *ch, struct qdss_request *d_req);
#else
static inline struct usb_qdss_ch *usb_qdss_open(const char *name, void *priv,
		void (*n)(void *, unsigned int event,
		struct qdss_request *d, struct usb_qdss_ch *c))
{
	return ERR_PTR(-ENODEV);
}

static inline int usb_qdss_write(struct usb_qdss_ch *c, struct qdss_request *d)
{
	return -ENODEV;
}

static inline int usb_qdss_alloc_req(struct usb_qdss_ch *c, int n_wr)
{
	return -ENODEV;
}

static inline void usb_qdss_close(struct usb_qdss_ch *ch) { }

static inline void usb_qdss_free_req(struct usb_qdss_ch *ch) { }
#endif /* CONFIG_USB_F_QDSS */

#endif /* __LINUX_USB_QDSS_H */
