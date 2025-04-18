// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MHI Endpoint Network driver
 *
 * Based on drivers/net/mhi_net.c
 *
 * Copyright (c) 2023, Linaro Ltd.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/if_arp.h>
#include <linux/mhi_ep.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/u64_stats_sync.h>

#define MHI_NET_MIN_MTU		ETH_MIN_MTU
#define MHI_NET_MAX_MTU		0xffff

struct mhi_ep_net_stats {
	u64_stats_t rx_packets;
	u64_stats_t rx_bytes;
	u64_stats_t rx_errors;
	u64_stats_t tx_packets;
	u64_stats_t tx_bytes;
	u64_stats_t tx_errors;
	u64_stats_t tx_dropped;
	struct u64_stats_sync tx_syncp;
	struct u64_stats_sync rx_syncp;
};

struct mhi_ep_net_dev {
	struct mhi_ep_device *mdev;
	struct net_device *ndev;
	struct mhi_ep_net_stats stats;
	struct workqueue_struct *xmit_wq;
	struct work_struct xmit_work;
	struct sk_buff_head tx_buffers;
	spinlock_t tx_lock; /* Lock for protecting tx_buffers */
	u32 mru;
};

static void mhi_ep_net_dev_process_queue_packets(struct work_struct *work)
{
	struct mhi_ep_net_dev *mhi_ep_netdev = container_of(work,
			struct mhi_ep_net_dev, xmit_work);
	struct mhi_ep_device *mdev = mhi_ep_netdev->mdev;
	struct sk_buff_head q;
	struct sk_buff *skb;
	int ret;

	if (mhi_ep_queue_is_empty(mdev, DMA_FROM_DEVICE)) {
		netif_stop_queue(mhi_ep_netdev->ndev);
		return;
	}

	__skb_queue_head_init(&q);

	spin_lock_bh(&mhi_ep_netdev->tx_lock);
	skb_queue_splice_init(&mhi_ep_netdev->tx_buffers, &q);
	spin_unlock_bh(&mhi_ep_netdev->tx_lock);

	while ((skb = __skb_dequeue(&q))) {
		ret = mhi_ep_queue_skb(mdev, skb);
		if (ret) {
			kfree_skb(skb);
			goto exit_drop;
		}

		u64_stats_update_begin(&mhi_ep_netdev->stats.tx_syncp);
		u64_stats_inc(&mhi_ep_netdev->stats.tx_packets);
		u64_stats_add(&mhi_ep_netdev->stats.tx_bytes, skb->len);
		u64_stats_update_end(&mhi_ep_netdev->stats.tx_syncp);

		/* Check if queue is empty */
		if (mhi_ep_queue_is_empty(mdev, DMA_FROM_DEVICE)) {
			netif_stop_queue(mhi_ep_netdev->ndev);
			break;
		}

		consume_skb(skb);
		cond_resched();
	}

	return;

exit_drop:
	u64_stats_update_begin(&mhi_ep_netdev->stats.tx_syncp);
	u64_stats_inc(&mhi_ep_netdev->stats.tx_dropped);
	u64_stats_update_end(&mhi_ep_netdev->stats.tx_syncp);
}

static int mhi_ndo_open(struct net_device *ndev)
{
	netif_carrier_on(ndev);
	netif_start_queue(ndev);

	return 0;
}

static int mhi_ndo_stop(struct net_device *ndev)
{
	netif_stop_queue(ndev);
	netif_carrier_off(ndev);

	return 0;
}

static netdev_tx_t mhi_ndo_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct mhi_ep_net_dev *mhi_ep_netdev = netdev_priv(ndev);

	spin_lock(&mhi_ep_netdev->tx_lock);
	skb_queue_tail(&mhi_ep_netdev->tx_buffers, skb);
	spin_unlock(&mhi_ep_netdev->tx_lock);

	queue_work(mhi_ep_netdev->xmit_wq, &mhi_ep_netdev->xmit_work);

	return NETDEV_TX_OK;
}

static void mhi_ndo_get_stats64(struct net_device *ndev,
				struct rtnl_link_stats64 *stats)
{
	struct mhi_ep_net_dev *mhi_ep_netdev = netdev_priv(ndev);
	unsigned int start;

	do {
		start = u64_stats_fetch_begin(&mhi_ep_netdev->stats.rx_syncp);
		stats->rx_packets = u64_stats_read(&mhi_ep_netdev->stats.rx_packets);
		stats->rx_bytes = u64_stats_read(&mhi_ep_netdev->stats.rx_bytes);
		stats->rx_errors = u64_stats_read(&mhi_ep_netdev->stats.rx_errors);
	} while (u64_stats_fetch_retry(&mhi_ep_netdev->stats.rx_syncp, start));

	do {
		start = u64_stats_fetch_begin(&mhi_ep_netdev->stats.tx_syncp);
		stats->tx_packets = u64_stats_read(&mhi_ep_netdev->stats.tx_packets);
		stats->tx_bytes = u64_stats_read(&mhi_ep_netdev->stats.tx_bytes);
		stats->tx_errors = u64_stats_read(&mhi_ep_netdev->stats.tx_errors);
		stats->tx_dropped = u64_stats_read(&mhi_ep_netdev->stats.tx_dropped);
	} while (u64_stats_fetch_retry(&mhi_ep_netdev->stats.tx_syncp, start));
}

static const struct net_device_ops mhi_ep_netdev_ops = {
	.ndo_open               = mhi_ndo_open,
	.ndo_stop               = mhi_ndo_stop,
	.ndo_start_xmit         = mhi_ndo_xmit,
	.ndo_get_stats64	= mhi_ndo_get_stats64,
};

static void mhi_ep_net_setup(struct net_device *ndev)
{
	ndev->header_ops = NULL;  /* No header */
	ndev->type = ARPHRD_RAWIP;
	ndev->hard_header_len = 0;
	ndev->addr_len = 0;
	ndev->flags = IFF_POINTOPOINT | IFF_NOARP;
	ndev->netdev_ops = &mhi_ep_netdev_ops;
	ndev->mtu = MHI_EP_DEFAULT_MTU;
	ndev->min_mtu = MHI_NET_MIN_MTU;
	ndev->max_mtu = MHI_NET_MAX_MTU;
	ndev->tx_queue_len = 1000;
}

static void mhi_ep_net_ul_callback(struct mhi_ep_device *mhi_dev,
				   struct mhi_result *mhi_res)
{
	struct mhi_ep_net_dev *mhi_ep_netdev = dev_get_drvdata(&mhi_dev->dev);
	struct net_device *ndev = mhi_ep_netdev->ndev;
	struct sk_buff *skb;
	size_t size;

	size = mhi_ep_netdev->mru ? mhi_ep_netdev->mru : READ_ONCE(ndev->mtu);

	skb = netdev_alloc_skb(ndev, size);
	if (unlikely(!skb)) {
		u64_stats_update_begin(&mhi_ep_netdev->stats.rx_syncp);
		u64_stats_inc(&mhi_ep_netdev->stats.rx_errors);
		u64_stats_update_end(&mhi_ep_netdev->stats.rx_syncp);
		return;
	}

	skb_copy_to_linear_data(skb, mhi_res->buf_addr, mhi_res->bytes_xferd);
	skb->len = mhi_res->bytes_xferd;
	skb->dev = mhi_ep_netdev->ndev;

	if (unlikely(mhi_res->transaction_status)) {
		switch (mhi_res->transaction_status) {
		case -ENOTCONN:
			/* MHI layer stopping/resetting the UL channel */
			dev_kfree_skb_any(skb);
			return;
		default:
			/* Unknown error, simply drop */
			dev_kfree_skb_any(skb);
			u64_stats_update_begin(&mhi_ep_netdev->stats.rx_syncp);
			u64_stats_inc(&mhi_ep_netdev->stats.rx_errors);
			u64_stats_update_end(&mhi_ep_netdev->stats.rx_syncp);
		}
	} else {
		skb_put(skb, mhi_res->bytes_xferd);

		switch (skb->data[0] & 0xf0) {
		case 0x40:
			skb->protocol = htons(ETH_P_IP);
			break;
		case 0x60:
			skb->protocol = htons(ETH_P_IPV6);
			break;
		default:
			skb->protocol = htons(ETH_P_MAP);
			break;
		}

		u64_stats_update_begin(&mhi_ep_netdev->stats.rx_syncp);
		u64_stats_inc(&mhi_ep_netdev->stats.rx_packets);
		u64_stats_add(&mhi_ep_netdev->stats.rx_bytes, skb->len);
		u64_stats_update_end(&mhi_ep_netdev->stats.rx_syncp);
		netif_rx(skb);
	}
}

static void mhi_ep_net_dl_callback(struct mhi_ep_device *mhi_dev,
				   struct mhi_result *mhi_res)
{
	struct mhi_ep_net_dev *mhi_ep_netdev = dev_get_drvdata(&mhi_dev->dev);

	if (unlikely(mhi_res->transaction_status == -ENOTCONN))
		return;

	/* Since we got enough buffers to queue, wake the queue if stopped */
	if (netif_queue_stopped(mhi_ep_netdev->ndev)) {
		netif_wake_queue(mhi_ep_netdev->ndev);
		queue_work(mhi_ep_netdev->xmit_wq, &mhi_ep_netdev->xmit_work);
	}
}

static int mhi_ep_net_newlink(struct mhi_ep_device *mhi_dev, struct net_device *ndev)
{
	struct mhi_ep_net_dev *mhi_ep_netdev;
	int ret;

	mhi_ep_netdev = netdev_priv(ndev);

	dev_set_drvdata(&mhi_dev->dev, mhi_ep_netdev);
	mhi_ep_netdev->ndev = ndev;
	mhi_ep_netdev->mdev = mhi_dev;
	mhi_ep_netdev->mru = mhi_dev->mhi_cntrl->mru;

	skb_queue_head_init(&mhi_ep_netdev->tx_buffers);
	spin_lock_init(&mhi_ep_netdev->tx_lock);

	u64_stats_init(&mhi_ep_netdev->stats.rx_syncp);
	u64_stats_init(&mhi_ep_netdev->stats.tx_syncp);

	mhi_ep_netdev->xmit_wq = alloc_workqueue("mhi_ep_net_xmit_wq", 0, WQ_HIGHPRI);
	INIT_WORK(&mhi_ep_netdev->xmit_work, mhi_ep_net_dev_process_queue_packets);

	ret = register_netdev(ndev);
	if (ret) {
		destroy_workqueue(mhi_ep_netdev->xmit_wq);
		return ret;
	}

	return 0;
}

static void mhi_ep_net_dellink(struct mhi_ep_device *mhi_dev, struct net_device *ndev)
{
	struct mhi_ep_net_dev *mhi_ep_netdev = netdev_priv(ndev);

	destroy_workqueue(mhi_ep_netdev->xmit_wq);
	unregister_netdev(ndev);
	free_netdev(ndev);
	dev_set_drvdata(&mhi_dev->dev, NULL);
}

static int mhi_ep_net_probe(struct mhi_ep_device *mhi_dev, const struct mhi_device_id *id)
{
	struct net_device *ndev;
	int ret;

	ndev = alloc_netdev(sizeof(struct mhi_ep_net_dev), (const char *)id->driver_data,
			    NET_NAME_PREDICTABLE, mhi_ep_net_setup);
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &mhi_dev->dev);

	ret = mhi_ep_net_newlink(mhi_dev, ndev);
	if (ret) {
		free_netdev(ndev);
		return ret;
	}

	return 0;
}

static void mhi_ep_net_remove(struct mhi_ep_device *mhi_dev)
{
	struct mhi_ep_net_dev *mhi_ep_netdev = dev_get_drvdata(&mhi_dev->dev);

	mhi_ep_net_dellink(mhi_dev, mhi_ep_netdev->ndev);
}

static const struct mhi_device_id mhi_ep_net_id_table[] = {
	/* Software data PATH (from endpoint CPU) */
	{ .chan = "IP_SW0", .driver_data = (kernel_ulong_t)"mhi_swip%d" },
	{}
};
MODULE_DEVICE_TABLE(mhi, mhi_ep_net_id_table);

static struct mhi_ep_driver mhi_ep_net_driver = {
	.probe = mhi_ep_net_probe,
	.remove = mhi_ep_net_remove,
	.dl_xfer_cb = mhi_ep_net_dl_callback,
	.ul_xfer_cb = mhi_ep_net_ul_callback,
	.id_table = mhi_ep_net_id_table,
	.driver = {
		.name = "mhi_ep_net",
		.owner = THIS_MODULE,
	},
};

module_mhi_ep_driver(mhi_ep_net_driver);

MODULE_DESCRIPTION("MHI Endpoint Network driver");
MODULE_LICENSE("GPL");
