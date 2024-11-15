// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#include "bcm-voter.h"
#include "icc-common.h"
#include "icc-rpmh.h"

/* QNOC QoS */
#define QOSGEN_MAINCTL_LO(p, qp)	(0x8 + (p->offsets[qp]))
#define QOS_SLV_URG_MSG_EN_SHFT		3
#define QOS_DFLT_PRIO_MASK		0x7
#define QOS_DFLT_PRIO_SHFT		4
#define QOS_DISABLE_SHIFT		24

/**
 * qcom_icc_set_qos - initialize static QoS configurations
 * @node: qcom icc node to operate on
 */
static void qcom_icc_set_qos(struct qcom_icc_node *node)
{
	struct qcom_icc_qosbox *qos = node->qosbox;
	int port;

	if (!node->regmap)
		return;

	if (!qos)
		return;

	for (port = 0; port < qos->num_ports; port++) {
		regmap_update_bits(node->regmap, QOSGEN_MAINCTL_LO(qos, port),
				   BIT(QOS_DISABLE_SHIFT),
				   qos->prio_fwd_disable << QOS_DISABLE_SHIFT);

		regmap_update_bits(node->regmap, QOSGEN_MAINCTL_LO(qos, port),
				   QOS_DFLT_PRIO_MASK << QOS_DFLT_PRIO_SHFT,
				   qos->prio << QOS_DFLT_PRIO_SHFT);

		regmap_update_bits(node->regmap, QOSGEN_MAINCTL_LO(qos, port),
				   BIT(QOS_SLV_URG_MSG_EN_SHFT),
				   qos->urg_fwd << QOS_SLV_URG_MSG_EN_SHFT);
	}
}

/**
 * qcom_icc_pre_aggregate - cleans up stale values from prior icc_set
 * @node: icc node to operate on
 */
void qcom_icc_pre_aggregate(struct icc_node *node)
{
	size_t i;
	struct qcom_icc_node *qn;
	struct qcom_icc_provider *qp;

	qn = node->data;
	qp = to_qcom_provider(node->provider);

	for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++) {
		qn->sum_avg[i] = 0;
		qn->max_peak[i] = 0;
		qn->perf_mode[i] = false;
	}

	for (i = 0; i < qn->num_bcms; i++)
		qcom_icc_bcm_voter_add(qp->voter, qn->bcms[i]);
}
EXPORT_SYMBOL_GPL(qcom_icc_pre_aggregate);

/**
 * qcom_icc_aggregate - aggregate bw for buckets indicated by tag
 * @node: node to aggregate
 * @tag: tag to indicate which buckets to aggregate
 * @avg_bw: new bw to sum aggregate
 * @peak_bw: new bw to max aggregate
 * @agg_avg: existing aggregate avg bw val
 * @agg_peak: existing aggregate peak bw val
 */
int qcom_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
		       u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	size_t i;
	struct qcom_icc_node *qn;

	qn = node->data;

	if (!tag)
		tag = QCOM_ICC_TAG_ALWAYS;

	for (i = 0; i < QCOM_ICC_NUM_BUCKETS; i++) {
		if (tag & BIT(i)) {
			qn->sum_avg[i] += avg_bw;
			qn->max_peak[i] = max_t(u32, qn->max_peak[i], peak_bw);
			if (tag & QCOM_ICC_TAG_PERF_MODE && (avg_bw || peak_bw))
				qn->perf_mode[i] = true;
		}

		if (node->init_avg || node->init_peak) {
			qn->sum_avg[i] = max_t(u64, qn->sum_avg[i], node->init_avg);
			qn->max_peak[i] = max_t(u64, qn->max_peak[i], node->init_peak);
		}
	}

	*agg_avg += avg_bw;
	*agg_peak = max_t(u32, *agg_peak, peak_bw);

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_icc_aggregate);

/**
 * qcom_icc_set - set the constraints based on path
 * @src: source node for the path to set constraints on
 * @dst: destination node for the path to set constraints on
 *
 * Return: 0 on success, or an error code otherwise
 */
int qcom_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct qcom_icc_provider *qp;
	struct icc_node *node;

	if (!src)
		node = dst;
	else
		node = src;

	qp = to_qcom_provider(node->provider);

	qcom_icc_bcm_voter_commit(qp->voter);

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_icc_set);

/**
 * qcom_icc_bcm_init - populates bcm aux data and connect qnodes
 * @bcm: bcm to be initialized
 * @dev: associated provider device
 *
 * Return: 0 on success, or an error code otherwise
 */
int qcom_icc_bcm_init(struct qcom_icc_bcm *bcm, struct device *dev)
{
	struct qcom_icc_node *qn;
	const struct bcm_db *data;
	size_t data_count;
	int i;

	/* BCM is already initialised*/
	if (bcm->addr)
		return 0;

	bcm->addr = cmd_db_read_addr(bcm->name);
	if (!bcm->addr) {
		dev_err(dev, "%s could not find RPMh address\n",
			bcm->name);
		return -EINVAL;
	}

	data = cmd_db_read_aux_data(bcm->name, &data_count);
	if (IS_ERR(data)) {
		dev_err(dev, "%s command db read error (%ld)\n",
			bcm->name, PTR_ERR(data));
		return PTR_ERR(data);
	}
	if (!data_count) {
		dev_err(dev, "%s command db missing or partial aux data\n",
			bcm->name);
		return -EINVAL;
	}

	bcm->aux_data.unit = le32_to_cpu(data->unit);
	bcm->aux_data.width = le16_to_cpu(data->width);
	bcm->aux_data.vcd = data->vcd;
	bcm->aux_data.reserved = data->reserved;
	INIT_LIST_HEAD(&bcm->list);
	INIT_LIST_HEAD(&bcm->ws_list);

	if (!bcm->vote_scale)
		bcm->vote_scale = 1000;

	/* Link Qnodes to their respective BCMs */
	for (i = 0; i < bcm->num_nodes; i++) {
		qn = bcm->nodes[i];
		qn->bcms[qn->num_bcms] = bcm;
		qn->num_bcms++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_icc_bcm_init);

static bool bcm_needs_qos_proxy(struct qcom_icc_bcm *bcm)
{
	int i;

	for (i = 0; i < bcm->num_nodes; i++)
		if (bcm->nodes[i]->qosbox)
			return true;

	return false;
}

static int enable_qos_deps(struct qcom_icc_provider *qp)
{
	struct qcom_icc_bcm *bcm;
	bool keepalive;
	int ret, i;

	for (i = 0; i < qp->num_bcms; i++) {
		bcm = qp->bcms[i];
		if (bcm_needs_qos_proxy(bcm)) {
			keepalive = bcm->keepalive;
			bcm->keepalive = true;

			qcom_icc_bcm_voter_add(qp->voter, bcm);
			ret = qcom_icc_bcm_voter_commit(qp->voter);

			bcm->keepalive = keepalive;

			if (ret) {
				dev_err(qp->dev, "failed to vote BW to %s for QoS\n",
					bcm->name);
				return ret;
			}
		}
	}

	ret = clk_bulk_prepare_enable(qp->num_clks, qp->clks);
	if (ret) {
		dev_err(qp->dev, "failed to enable clocks for QoS\n");
		return ret;
	}

	return 0;
}

static void disable_qos_deps(struct qcom_icc_provider *qp)
{
	struct qcom_icc_bcm *bcm;
	int i;

	clk_bulk_disable_unprepare(qp->num_clks, qp->clks);

	for (i = 0; i < qp->num_bcms; i++) {
		bcm = qp->bcms[i];
		if (bcm_needs_qos_proxy(bcm)) {
			qcom_icc_bcm_voter_add(qp->voter, bcm);
			qcom_icc_bcm_voter_commit(qp->voter);
		}
	}
}

int qcom_icc_rpmh_configure_qos(struct qcom_icc_provider *qp)
{
	struct qcom_icc_node *qnode;
	size_t i;
	int ret;

	ret = enable_qos_deps(qp);
	if (ret)
		return ret;

	for (i = 0; i < qp->num_nodes; i++) {
		qnode = qp->nodes[i];
		if (!qnode)
			continue;

		if (qnode->qosbox)
			qcom_icc_set_qos(qnode);
	}

	disable_qos_deps(qp);

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_icc_rpmh_configure_qos);

static struct regmap *qcom_icc_rpmh_map(struct platform_device *pdev,
					const struct qcom_icc_desc *desc)
{
	void __iomem *base;
	struct resource *res;
	struct device *dev = &pdev->dev;

	if (!desc->config)
		return NULL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return NULL;

	base = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(base))
		return ERR_CAST(base);

	return devm_regmap_init_mmio(dev, base, desc->config);
}

int qcom_icc_rpmh_probe(struct platform_device *pdev)
{
	const struct qcom_icc_desc *desc;
	struct device *dev = &pdev->dev;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct qcom_icc_node * const *qnodes, *qn;
	struct qcom_icc_provider *qp;
	struct icc_node *node;
	size_t num_nodes, i, j;
	int ret;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kzalloc(dev, struct_size(data, nodes, num_nodes), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->num_nodes = num_nodes;

	provider = &qp->provider;
	provider->dev = dev;
	provider->set = qcom_icc_set;
	provider->pre_aggregate = qcom_icc_pre_aggregate;
	provider->aggregate = qcom_icc_aggregate;
	provider->xlate_extended = qcom_icc_xlate_extended;
	provider->data = data;

	icc_provider_init(provider);

	qp->dev = dev;
	qp->bcms = desc->bcms;
	qp->nodes = desc->nodes;
	qp->num_bcms = desc->num_bcms;
	qp->num_nodes = desc->num_nodes;

	qp->voter = of_bcm_voter_get(qp->dev, NULL);
	if (IS_ERR(qp->voter))
		return PTR_ERR(qp->voter);

	qp->regmap = qcom_icc_rpmh_map(pdev, desc);
	if (IS_ERR(qp->regmap))
		return PTR_ERR(qp->regmap);

	qp->num_clks = devm_clk_bulk_get_all(qp->dev, &qp->clks);
	if (qp->num_clks < 0)
		return qp->num_clks;

	for (i = 0; i < qp->num_bcms; i++)
		qcom_icc_bcm_init(qp->bcms[i], dev);

	for (i = 0; i < num_nodes; i++) {
		qn = qnodes[i];
		if (!qn)
			continue;

		qn->regmap = dev_get_regmap(qp->dev, NULL);

		node = icc_node_create(qn->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err_remove_nodes;
		}

		node->name = qn->name;
		node->data = qn;
		icc_node_add(node, provider);

		for (j = 0; j < qn->num_links; j++)
			icc_link_create(node, qn->links[j]);

		data->nodes[i] = node;
	}

	ret = qcom_icc_rpmh_configure_qos(qp);
	if (ret)
		goto err_remove_nodes;

	ret = icc_provider_register(provider);
	if (ret)
		goto err_remove_nodes;

	platform_set_drvdata(pdev, qp);

	/* Populate child NoC devices if any */
	if (of_get_child_count(dev->of_node) > 0) {
		ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
		if (ret)
			goto err_deregister_provider;
	}

	return 0;

err_deregister_provider:
	icc_provider_deregister(provider);
err_remove_nodes:
	clk_bulk_put_all(qp->num_clks, qp->clks);
	icc_nodes_remove(provider);

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_icc_rpmh_probe);

void qcom_icc_rpmh_remove(struct platform_device *pdev)
{
	struct qcom_icc_provider *qp = platform_get_drvdata(pdev);

	icc_provider_deregister(&qp->provider);
	clk_bulk_put_all(qp->num_clks, qp->clks);
	icc_nodes_remove(&qp->provider);
}
EXPORT_SYMBOL_GPL(qcom_icc_rpmh_remove);

MODULE_LICENSE("GPL v2");
