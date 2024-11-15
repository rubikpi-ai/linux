// SPDX-License-Identifier: GPL-2.0-only
/*
 * f_qdss.c -- QDSS function Driver
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/usb/cdc.h>

#include "f_qdss.h"

static DEFINE_SPINLOCK(channel_lock);
static LIST_HEAD(usb_qdss_ch_list);

static struct usb_interface_descriptor qdss_data_intf_desc = {
	.bLength            =	sizeof(qdss_data_intf_desc),
	.bDescriptorType    =	USB_DT_INTERFACE,
	.bAlternateSetting  =   0,
	.bNumEndpoints      =	1,
	.bInterfaceClass    =	USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass =	USB_SUBCLASS_VENDOR_SPEC,
	.bInterfaceProtocol =	0x70,
};

static struct usb_endpoint_descriptor qdss_hs_data_desc = {
	.bLength              =	 USB_DT_ENDPOINT_SIZE,
	.bDescriptorType      =	 USB_DT_ENDPOINT,
	.bEndpointAddress     =	 USB_DIR_IN,
	.bmAttributes         =	 USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize       =	 cpu_to_le16(512),
};

static struct usb_endpoint_descriptor qdss_ss_data_desc = {
	.bLength              =	 USB_DT_ENDPOINT_SIZE,
	.bDescriptorType      =	 USB_DT_ENDPOINT,
	.bEndpointAddress     =	 USB_DIR_IN,
	.bmAttributes         =  USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize       =	 cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor qdss_data_ep_comp_desc = {
	.bLength              =	 sizeof(qdss_data_ep_comp_desc),
	.bDescriptorType      =	 USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst            =	 1,
	.bmAttributes         =	 0,
	.wBytesPerInterval    =	 0,
};

static struct usb_endpoint_descriptor qdss_fs_data_desc = {
	.bLength            =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    =	USB_DT_ENDPOINT,
	.bEndpointAddress   =	USB_DIR_IN,
	.bmAttributes       =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize     =	cpu_to_le16(64),
};

static struct usb_descriptor_header *qdss_fs_data_only_desc[] = {
	(struct usb_descriptor_header *) &qdss_data_intf_desc,
	(struct usb_descriptor_header *) &qdss_fs_data_desc,
	NULL,
};

static struct usb_descriptor_header *qdss_hs_data_only_desc[] = {
	(struct usb_descriptor_header *) &qdss_data_intf_desc,
	(struct usb_descriptor_header *) &qdss_hs_data_desc,
	NULL,
};

static struct usb_descriptor_header *qdss_ss_data_only_desc[] = {
	(struct usb_descriptor_header *) &qdss_data_intf_desc,
	(struct usb_descriptor_header *) &qdss_ss_data_desc,
	(struct usb_descriptor_header *) &qdss_data_ep_comp_desc,
	NULL,
};

/* string descriptors: */
#define QDSS_DATA_IDX	0

static struct usb_string qdss_string_defs[] = {
	[QDSS_DATA_IDX].s = "QDSS DATA",
	{}, /* end of list */
};

static struct usb_gadget_strings qdss_string_table = {
	.language =		0x0409,
	.strings =		qdss_string_defs,
};

static struct usb_gadget_strings *qdss_strings[] = {
	&qdss_string_table,
	NULL,
};

static void qdss_disable(struct usb_function *f);

static inline struct f_qdss *func_to_qdss(struct usb_function *f)
{
	return container_of(f, struct f_qdss, port.function);
}

static
struct usb_qdss_opts *to_fi_usb_qdss_opts(struct usb_function_instance *fi)
{
	return container_of(fi, struct usb_qdss_opts, func_inst);
}

/*----------------------------------------------------------------------*/

static void qdss_write_complete(struct usb_ep *ep,
				struct usb_request *req)
{
	struct f_qdss *qdss = ep->driver_data;
	struct qdss_req *qreq = req->context;
	struct qdss_request *d_req = qreq->qdss_req;
	unsigned long flags;

	spin_lock_irqsave(&qdss->lock, flags);
	list_move_tail(&qreq->list, &qdss->data_write_pool);

	/*
	 * When channel is closed, we move all queued requests to
	 * dequeued_data_pool list and wait for it to be drained.
	 * Signal the completion here if the channel is closed
	 * and both queued & dequeued lists are empty.
	 */
	if (!qdss->opened && list_empty(&qdss->dequeued_data_pool) &&
			list_empty(&qdss->queued_data_pool))
		complete(&qdss->dequeue_done);

	if (req->length != 0) {
		d_req->actual = req->actual;
		d_req->status = req->status;
	}
	spin_unlock_irqrestore(&qdss->lock, flags);

	if (qdss->ch.notify)
		qdss->ch.notify(qdss->ch.priv,
			USB_QDSS_DATA_WRITE_DONE, d_req, NULL);
}

static void qdss_free_reqs(struct f_qdss *qdss)
{
	struct list_head *act, *tmp;
	struct qdss_req *qreq;
	unsigned long flags;

	lockdep_assert_held(&qdss->mutex);
	/* These are critical list operations which can race
	 * with qdss_write/write_complete, therefore protect
	 * them under spin_lock.
	 */
	spin_lock_irqsave(&qdss->lock, flags);

	list_for_each_safe(act, tmp, &qdss->data_write_pool) {
		qreq = list_entry(act, struct qdss_req, list);
		list_del(&qreq->list);
		usb_ep_free_request(qdss->port.data, qreq->usb_req);
		kfree(qreq);
	}

	spin_unlock_irqrestore(&qdss->lock, flags);
}

void usb_qdss_free_req(struct usb_qdss_ch *ch)
{
	struct f_qdss *qdss = container_of(ch, struct f_qdss, ch);

	if (!ch) {
		pr_err("%s: ch is NULL\n", __func__);
		return;
	}

	mutex_lock(&qdss->mutex);
	if (!qdss->opened)
		pr_err("%s: channel %s closed\n", __func__, ch->name);
	else
		qdss_free_reqs(qdss);
	mutex_unlock(&qdss->mutex);
}
EXPORT_SYMBOL_GPL(usb_qdss_free_req);

int usb_qdss_alloc_req(struct usb_qdss_ch *ch, int no_write_buf)
{
	struct f_qdss *qdss = container_of(ch, struct f_qdss, ch);
	struct usb_request *req;
	struct usb_ep *in;
	struct list_head *list_pool;
	int i;
	struct qdss_req *qreq;
	unsigned long flags;

	if (!ch) {
		pr_err("%s: ch is NULL\n", __func__);
		return -EINVAL;
	}

	if (!qdss) {
		pr_err("%s: %s closed\n", __func__, ch->name);
		return -ENODEV;
	}

	mutex_lock(&qdss->mutex);

	in = qdss->port.data;
	list_pool = &qdss->data_write_pool;

	for (i = 0; i < no_write_buf; i++) {
		qreq = kzalloc(sizeof(struct qdss_req), GFP_KERNEL);
		if (!qreq)
			goto fail;

		req = usb_ep_alloc_request(in, GFP_KERNEL);
		if (!req) {
			pr_err("%s: data in allocation err\n", __func__);
			kfree(qreq);
			goto fail;
		}
		/* Insure qreq is protected while assigning the request
		 * This can potentially race with free_req
		 */

		spin_lock_irqsave(&qdss->lock, flags);
		qreq->usb_req = req;
		req->context = qreq;
		req->complete = qdss_write_complete;
		list_add_tail(&qreq->list, list_pool);
		spin_unlock_irqrestore(&qdss->lock, flags);
	}

	mutex_unlock(&qdss->mutex);
	return 0;

fail:
	qdss_free_reqs(qdss);
	mutex_unlock(&qdss->mutex);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(usb_qdss_alloc_req);

static void clear_eps(struct usb_function *f)
{
	struct f_qdss *qdss = func_to_qdss(f);

	if (qdss->port.data)
		qdss->port.data->driver_data = NULL;
}

static int qdss_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_gadget *gadget = c->cdev->gadget;
	struct f_qdss *qdss = func_to_qdss(f);
	struct usb_ep *ep;
	int iface, id, ret;

	/* Allocate data I/F */
	iface = usb_interface_id(c, f);
	if (iface < 0) {
		pr_err("interface allocation error\n");
		return iface;
	}
	qdss_data_intf_desc.bInterfaceNumber = iface;
	qdss->data_iface_id = iface;

	if (!qdss_string_defs[QDSS_DATA_IDX].id) {
		id = usb_string_id(c->cdev);
		if (id < 0)
			return id;

		qdss_string_defs[QDSS_DATA_IDX].id = id;
		qdss_data_intf_desc.iInterface = id;
	}

	ep = usb_ep_autoconfig(gadget, &qdss_fs_data_desc);
	if (!ep) {
		pr_err("%s: ep_autoconfig error\n", __func__);
		return -EOPNOTSUPP;
	}
	qdss->port.data = ep;
	ep->driver_data = qdss;

	/* update hs/ss descriptors */
	qdss_hs_data_desc.bEndpointAddress =
		qdss_ss_data_desc.bEndpointAddress =
			qdss_fs_data_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, qdss_fs_data_only_desc,
			qdss_hs_data_only_desc, qdss_ss_data_only_desc,
			qdss_ss_data_only_desc);

	if (ret)
		goto clear_ep;

	return 0;

clear_ep:
	clear_eps(f);

	return -EOPNOTSUPP;
}


static void qdss_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_qdss  *qdss = func_to_qdss(f);

	qdss_disable(f);
	flush_workqueue(qdss->wq);

	/* Reset string ids */
	qdss_string_defs[QDSS_DATA_IDX].id = 0;

	clear_eps(f);
	usb_free_all_descriptors(f);
}

static void qdss_eps_disable(struct usb_function *f)
{
	struct f_qdss  *qdss = func_to_qdss(f);

	if (qdss->data_enabled) {
		usb_ep_disable(qdss->port.data);
		qdss->data_enabled = 0;
	}
}

static void usb_qdss_disconnect_work(struct work_struct *work)
{
	struct f_qdss *qdss = container_of(work, struct f_qdss, disconnect_w);

	/* Notify qdss to cancel all active transfers */
	if (qdss->ch.notify)
		qdss->ch.notify(qdss->ch.priv,
			USB_QDSS_DISCONNECT, NULL, NULL);
}

static void qdss_disable(struct usb_function *f)
{
	struct f_qdss	*qdss = func_to_qdss(f);
	unsigned long flags;

	spin_lock_irqsave(&qdss->lock, flags);

	if (!qdss->usb_connected) {
		spin_unlock_irqrestore(&qdss->lock, flags);
		return;
	}

	qdss->usb_connected = 0;
	/*cancell all active xfers*/
	spin_unlock_irqrestore(&qdss->lock, flags);
	qdss_eps_disable(f);
	queue_work(qdss->wq, &qdss->disconnect_w);
}

static void usb_qdss_connect_work(struct work_struct *work)
{
	struct f_qdss *qdss = container_of(work, struct f_qdss, connect_w);

	/* If cable is already removed, discard connect_work */
	if (qdss->usb_connected == 0) {
		cancel_work_sync(&qdss->disconnect_w);
		return;
	}

	if (qdss->opened && qdss->ch.notify)
		qdss->ch.notify(qdss->ch.priv, USB_QDSS_CONNECT,
						NULL, &qdss->ch);
}

static int qdss_set_alt(struct usb_function *f, unsigned int intf,
				unsigned int alt)
{
	struct f_qdss  *qdss = func_to_qdss(f);
	struct usb_gadget *gadget = f->config->cdev->gadget;
	int ret = 0;

	qdss->gadget = gadget;

	if (alt != 0)
		return -EINVAL;

	if (gadget->speed < USB_SPEED_HIGH) {
		pr_err("%s: qdss doesn't support USB full or low speed\n",
								__func__);
		return -EINVAL;
	}

	if (intf == qdss->data_iface_id && !qdss->data_enabled) {

		ret = config_ep_by_speed(gadget, f, qdss->port.data);
		if (ret) {
			pr_err("%s: failed config_ep_by_speed ret:%d\n",
							__func__, ret);
			return ret;
		}

		ret = usb_ep_enable(qdss->port.data);
		if (ret) {
			pr_err("%s: failed to enable ep ret:%d\n",
							__func__, ret);
			return ret;
		}

		qdss->port.data->driver_data = qdss;
		qdss->data_enabled = 1;
	}

	if (qdss->data_enabled)
		qdss->usb_connected = 1;

	if (qdss->usb_connected)
		queue_work(qdss->wq, &qdss->connect_w);

	return 0;
}

static struct f_qdss *alloc_usb_qdss(char *channel_name)
{
	struct f_qdss *qdss;
	int found = 0;
	struct usb_qdss_ch *ch;
	unsigned long flags;

	spin_lock_irqsave(&channel_lock, flags);
	list_for_each_entry(ch, &usb_qdss_ch_list, list) {
		if (!strcmp(channel_name, ch->name)) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&channel_lock, flags);

	if (found) {
		pr_err("%s: (%s) is already available.\n",
				__func__, channel_name);
		return ERR_PTR(-EEXIST);
	}

	qdss = kzalloc(sizeof(struct f_qdss), GFP_KERNEL);
	if (!qdss)
		return ERR_PTR(-ENOMEM);

	qdss->wq = create_singlethread_workqueue(channel_name);
	if (!qdss->wq) {
		kfree(qdss);
		return ERR_PTR(-ENOMEM);
	}

	spin_lock_irqsave(&channel_lock, flags);
	ch = &qdss->ch;
	ch->name = channel_name;

	list_add_tail(&ch->list, &usb_qdss_ch_list);
	spin_unlock_irqrestore(&channel_lock, flags);

	spin_lock_init(&qdss->lock);
	INIT_LIST_HEAD(&qdss->data_write_pool);
	INIT_LIST_HEAD(&qdss->queued_data_pool);
	INIT_LIST_HEAD(&qdss->dequeued_data_pool);
	INIT_WORK(&qdss->connect_w, usb_qdss_connect_work);
	INIT_WORK(&qdss->disconnect_w, usb_qdss_disconnect_work);
	mutex_init(&qdss->mutex);
	init_completion(&qdss->dequeue_done);

	return qdss;
}

int usb_qdss_write(struct usb_qdss_ch *ch, struct qdss_request *d_req)
{
	struct f_qdss *qdss = container_of(ch, struct f_qdss, ch);
	unsigned long flags;
	struct usb_request *req = NULL;
	struct qdss_req *qreq;

	if (!ch) {
		pr_err("%s: ch is NULL\n", __func__);
		return -EINVAL;
	}

	/* There is a possible scenario where the qdss_write &
	 * qdss_close can race if the close call is made in
	 * between a write request. This will result in all
	 * requests freed during the write operations leaving
	 * the driver with stale list.
	 */
	mutex_lock(&qdss->mutex);
	/* Possible race with qdss_close corrupting the
	 * data_write_pool.
	 */

	spin_lock_irqsave(&qdss->lock, flags);

	if (!qdss->opened || !qdss->usb_connected) {
		spin_unlock_irqrestore(&qdss->lock, flags);
		mutex_unlock(&qdss->mutex);
		return -EIO;
	}

	if (list_empty(&qdss->data_write_pool)) {
		pr_err("error: usb_qdss_data_write list is empty\n");
		spin_unlock_irqrestore(&qdss->lock, flags);
		mutex_unlock(&qdss->mutex);
		return -EAGAIN;
	}

	qreq = list_first_entry(&qdss->data_write_pool, struct qdss_req,
		list);
	list_move_tail(&qreq->list, &qdss->queued_data_pool);
	spin_unlock_irqrestore(&qdss->lock, flags);

	qreq->qdss_req = d_req;
	req = qreq->usb_req;
	req->buf = d_req->buf;
	req->length = d_req->length;
	req->sg = d_req->sg;
	req->num_sgs = d_req->num_sgs;

	if (usb_ep_queue(qdss->port.data, req, GFP_KERNEL)) {
		spin_lock_irqsave(&qdss->lock, flags);
		/* Remove from queued pool and add back to data pool.
		 * Protech the list operation inside spinlock to avoid
		 * the list getting corrupted.
		 */
		list_move_tail(&qreq->list, &qdss->data_write_pool);
		spin_unlock_irqrestore(&qdss->lock, flags);
		pr_err("qdss usb_ep_queue failed\n");
		mutex_unlock(&qdss->mutex);
		return -EIO;
	}

	mutex_unlock(&qdss->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(usb_qdss_write);

struct usb_qdss_ch *usb_qdss_open(const char *name, void *priv,
	void (*notify)(void *priv, unsigned int event,
		struct qdss_request *d_req, struct usb_qdss_ch *))
{
	struct usb_qdss_ch *ch = NULL;
	struct usb_qdss_ch *tmp_ch;
	struct f_qdss *qdss = NULL;
	unsigned long flags;

	if (!notify) {
		pr_err("%s: notification func is missing\n", __func__);
		return NULL;
	}
	/* Protech qdss instance from getting freed */
	spin_lock_irqsave(&channel_lock, flags);

	list_for_each_entry(tmp_ch, &usb_qdss_ch_list, list) {
		if (!strcmp(name, tmp_ch->name)) {
			ch = tmp_ch;
			qdss = container_of(ch, struct f_qdss, ch);
			break;
		}
	}

	spin_unlock_irqrestore(&channel_lock, flags);
	if (!ch || !qdss)
		return NULL;

	mutex_lock(&qdss->mutex);
	ch->priv = priv;
	ch->notify = notify;
	qdss->opened = true;
	reinit_completion(&qdss->dequeue_done);

	/* the case USB cabel was connected before qdss called qdss_open */
	if (qdss->usb_connected)
		queue_work(qdss->wq, &qdss->connect_w);

	mutex_unlock(&qdss->mutex);
	return ch;
}
EXPORT_SYMBOL_GPL(usb_qdss_open);

void usb_qdss_close(struct usb_qdss_ch *ch)
{
	struct f_qdss *qdss = container_of(ch, struct f_qdss, ch);
	unsigned long flags;
	struct qdss_req *qreq;
	bool do_wait;

	if (!ch) {
		pr_err("%s: ch is NULL\n", __func__);
		return;
	}

	mutex_lock(&qdss->mutex);
	if (!qdss->opened) {
		pr_err("%s: channel %s closed\n", __func__, ch->name);
		mutex_unlock(&qdss->mutex);
		return;
	}

	spin_lock_irqsave(&qdss->lock, flags);
	qdss->opened = false;
	/*
	 * Some UDCs like DWC3 stop the endpoint transfer upon dequeue
	 * of a request and retire all the previously *started* requests.
	 * This introduces a race between the below dequeue loop and
	 * retiring of all started requests. As soon as we drop the lock
	 * here before dequeue, the request gets retired and UDC thinks
	 * we are dequeuing a request that was not queued before. To
	 * avoid this problem, lets dequeue the requests in the reverse
	 * order.
	 */
	while (!list_empty(&qdss->queued_data_pool)) {
		qreq = list_last_entry(&qdss->queued_data_pool,
				struct qdss_req, list);
		list_move_tail(&qreq->list, &qdss->dequeued_data_pool);
		spin_unlock_irqrestore(&qdss->lock, flags);
		/* Perform dequeue operation outside spinlock */
		usb_ep_dequeue(qdss->port.data, qreq->usb_req);
		spin_lock_irqsave(&qdss->lock, flags);
	}

	/*
	 * It's possible that requests may be completed synchronously during
	 * usb_ep_dequeue() and would have already been moved back to
	 * data_write_pool.  So make sure to check that our dequeued_data_pool
	 * is empty. If not, wait for it to happen. The request completion
	 * handler would signal us when this list is empty and channel close
	 * is in progress.
	 */
	do_wait = !list_empty(&qdss->dequeued_data_pool);
	spin_unlock_irqrestore(&qdss->lock, flags);

	if (do_wait)
		wait_for_completion(&qdss->dequeue_done);

	WARN_ON(!list_empty(&qdss->dequeued_data_pool));

	qdss_free_reqs(qdss);
	ch->notify = NULL;
	mutex_unlock(&qdss->mutex);
}
EXPORT_SYMBOL_GPL(usb_qdss_close);

static void qdss_cleanup(void)
{
	struct f_qdss *qdss;
	struct list_head *act, *tmp;
	struct usb_qdss_ch *_ch;
	unsigned long flags;

	list_for_each_safe(act, tmp, &usb_qdss_ch_list) {
		_ch = list_entry(act, struct usb_qdss_ch, list);
		qdss = container_of(_ch, struct f_qdss, ch);
		destroy_workqueue(qdss->wq);
		/* Protect the channel with channel_lock to
		 * avoid races with alloc & open functions
		 */

		spin_lock_irqsave(&channel_lock, flags);
		if (!_ch->priv) {
			list_del(&_ch->list);
			kfree(qdss);
		}
		spin_unlock_irqrestore(&channel_lock, flags);
	}
}

static void qdss_free_func(struct usb_function *f)
{ }

static inline struct usb_qdss_opts *to_f_qdss_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct usb_qdss_opts,
			func_inst.group);
}


static void qdss_attr_release(struct config_item *item)
{
	struct usb_qdss_opts *opts = to_f_qdss_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations qdss_item_ops = {
	.release	= qdss_attr_release,
};

static struct config_item_type qdss_func_type = {
	.ct_item_ops	= &qdss_item_ops,
	.ct_owner	= THIS_MODULE,
};

static void usb_qdss_free_inst(struct usb_function_instance *fi)
{
	struct usb_qdss_opts *opts;

	opts = container_of(fi, struct usb_qdss_opts, func_inst);
	kfree(opts->usb_qdss);
	kfree(opts);
}

static int usb_qdss_set_inst_name(struct usb_function_instance *f,
				const char *name)
{
	struct usb_qdss_opts *opts =
		container_of(f, struct usb_qdss_opts, func_inst);
	char *ptr;
	size_t name_len;
	struct f_qdss *usb_qdss;

	/* get channel_name as expected input qdss.<channel_name> */
	name_len = strlen(name) + 1;
	if (name_len > 15)
		return -ENAMETOOLONG;

	/* get channel name */
	ptr = kstrndup(name, name_len, GFP_KERNEL);
	if (!ptr) {
		pr_err("error:%ld\n", PTR_ERR(ptr));
		return -ENOMEM;
	}

	opts->channel_name = ptr;

	usb_qdss = alloc_usb_qdss(opts->channel_name);
	if (IS_ERR(usb_qdss)) {
		pr_err("Failed to create usb_qdss port(%s)\n",
				opts->channel_name);
		return -ENOMEM;
	}

	opts->usb_qdss = usb_qdss;
	return 0;
}

static struct usb_function_instance *qdss_alloc_inst(void)
{
	struct usb_qdss_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	opts->func_inst.free_func_inst = usb_qdss_free_inst;
	opts->func_inst.set_inst_name = usb_qdss_set_inst_name;

	config_group_init_type_name(&opts->func_inst.group, "",
				    &qdss_func_type);
	return &opts->func_inst;
}

static struct usb_function *qdss_alloc(struct usb_function_instance *fi)
{
	struct usb_qdss_opts *opts = to_fi_usb_qdss_opts(fi);
	struct f_qdss *usb_qdss = opts->usb_qdss;

	usb_qdss->port.function.name = "usb_qdss";
	usb_qdss->port.function.strings = qdss_strings;
	usb_qdss->port.function.bind = qdss_bind;
	usb_qdss->port.function.unbind = qdss_unbind;
	usb_qdss->port.function.set_alt = qdss_set_alt;
	usb_qdss->port.function.disable = qdss_disable;
	usb_qdss->port.function.setup = NULL;
	usb_qdss->port.function.free_func = qdss_free_func;

	return &usb_qdss->port.function;
}

DECLARE_USB_FUNCTION(qdss, qdss_alloc_inst, qdss_alloc);
static int __init usb_qdss_init(void)
{
	int ret = 0;

	INIT_LIST_HEAD(&usb_qdss_ch_list);
	ret = usb_function_register(&qdssusb_func);
	if (ret)
		pr_err("%s: failed to register qdss %d\n", __func__, ret);

	return ret;
}

static void __exit usb_qdss_exit(void)
{
	usb_function_unregister(&qdssusb_func);
	qdss_cleanup();
}

module_init(usb_qdss_init);
module_exit(usb_qdss_exit);
MODULE_DESCRIPTION("USB QDSS Function Driver");
MODULE_LICENSE("GPL");
