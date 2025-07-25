// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Bluetooth supports for Qualcomm Atheros chips
 *
 *  Copyright (c) 2015 The Linux Foundation. All rights reserved.
 */
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btqca.h"

#define VERSION "0.1"

int qca_read_soc_version(struct hci_dev *hdev, struct qca_btsoc_version *ver,
			 enum qca_btsoc_type soc_type)
{
	struct sk_buff *skb;
	struct edl_event_hdr *edl;
	char cmd;
	int err = 0;
	u8 event_type = HCI_EV_VENDOR;
	u8 rlen = sizeof(*edl) + sizeof(*ver);
	u8 rtype = EDL_APP_VER_RES_EVT;

	bt_dev_dbg(hdev, "QCA Version Request");

	/* Unlike other SoC's sending version command response as payload to
	 * VSE event. WCN3991 sends version command response as a payload to
	 * command complete event.
	 */
	if (soc_type >= QCA_WCN3991) {
		event_type = 0;
		rlen += 1;
		rtype = EDL_PATCH_VER_REQ_CMD;
	}

	cmd = EDL_PATCH_VER_REQ_CMD;
	skb = __hci_cmd_sync_ev(hdev, EDL_PATCH_CMD_OPCODE, EDL_PATCH_CMD_LEN,
				&cmd, event_type, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Reading QCA version information failed (%d)",
			   err);
		return err;
	}

	if (skb->len != rlen) {
		bt_dev_err(hdev, "QCA Version size mismatch len %d", skb->len);
		err = -EILSEQ;
		goto out;
	}

	edl = (struct edl_event_hdr *)(skb->data);

	if (edl->cresp != EDL_CMD_REQ_RES_EVT ||
	    edl->rtype != rtype) {
		bt_dev_err(hdev, "QCA Wrong packet received %d %d", edl->cresp,
			   edl->rtype);
		err = -EIO;
		goto out;
	}

	if (soc_type >= QCA_WCN3991)
		memcpy(ver, edl->data + 1, sizeof(*ver));
	else
		memcpy(ver, &edl->data, sizeof(*ver));

	bt_dev_info(hdev, "QCA Product ID   :0x%08x",
		    le32_to_cpu(ver->product_id));
	bt_dev_info(hdev, "QCA SOC Version  :0x%08x",
		    le32_to_cpu(ver->soc_id));
	bt_dev_info(hdev, "QCA ROM Version  :0x%08x",
		    le16_to_cpu(ver->rom_ver));
	bt_dev_info(hdev, "QCA Patch Version:0x%08x",
		    le16_to_cpu(ver->patch_ver));

	if (ver->soc_id == 0 || ver->rom_ver == 0)
		err = -EILSEQ;

out:
	kfree_skb(skb);
	if (err)
		bt_dev_err(hdev, "QCA Failed to get version (%d)", err);

	return err;
}
EXPORT_SYMBOL_GPL(qca_read_soc_version);

static int qca_read_fw_build_info(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct edl_event_hdr *edl;
	char *build_label;
	char cmd;
	int build_lbl_len, err = 0;

	bt_dev_dbg(hdev, "QCA read fw build info");

	cmd = EDL_GET_BUILD_INFO_CMD;
	skb = __hci_cmd_sync_ev(hdev, EDL_PATCH_CMD_OPCODE, EDL_PATCH_CMD_LEN,
				&cmd, 0, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Reading QCA fw build info failed (%d)",
			   err);
		return err;
	}

	if (skb->len < sizeof(*edl)) {
		err = -EILSEQ;
		goto out;
	}

	edl = (struct edl_event_hdr *)(skb->data);

	if (edl->cresp != EDL_CMD_REQ_RES_EVT ||
	    edl->rtype != EDL_GET_BUILD_INFO_CMD) {
		bt_dev_err(hdev, "QCA Wrong packet received %d %d", edl->cresp,
			   edl->rtype);
		err = -EIO;
		goto out;
	}

	if (skb->len < sizeof(*edl) + 1) {
		err = -EILSEQ;
		goto out;
	}

	build_lbl_len = edl->data[0];

	if (skb->len < sizeof(*edl) + 1 + build_lbl_len) {
		err = -EILSEQ;
		goto out;
	}

	build_label = kstrndup(&edl->data[1], build_lbl_len, GFP_KERNEL);
	if (!build_label) {
		err = -ENOMEM;
		goto out;
	}

	hci_set_fw_info(hdev, "%s", build_label);

	kfree(build_label);
out:
	kfree_skb(skb);
	return err;
}

static int qca_send_patch_config_cmd(struct hci_dev *hdev)
{
	const u8 cmd[] = { EDL_PATCH_CONFIG_CMD, 0x01, 0, 0, 0 };
	struct sk_buff *skb;
	struct edl_event_hdr *edl;
	int err;

	bt_dev_dbg(hdev, "QCA Patch config");

	skb = __hci_cmd_sync_ev(hdev, EDL_PATCH_CMD_OPCODE, sizeof(cmd),
				cmd, 0, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Sending QCA Patch config failed (%d)", err);
		return err;
	}

	if (skb->len != 2) {
		bt_dev_err(hdev, "QCA Patch config cmd size mismatch len %d", skb->len);
		err = -EILSEQ;
		goto out;
	}

	edl = (struct edl_event_hdr *)(skb->data);

	if (edl->cresp != EDL_PATCH_CONFIG_RES_EVT || edl->rtype != EDL_PATCH_CONFIG_CMD) {
		bt_dev_err(hdev, "QCA Wrong packet received %d %d", edl->cresp,
			   edl->rtype);
		err = -EIO;
		goto out;
	}

	err = 0;

out:
	kfree_skb(skb);
	return err;
}

static int qca_send_reset(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	int err;

	bt_dev_dbg(hdev, "QCA HCI_RESET");

	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA Reset failed (%d)", err);
		return err;
	}

	kfree_skb(skb);

	return 0;
}

static int qca_read_fw_board_id(struct hci_dev *hdev, u16 *bid)
{
	u8 cmd;
	struct sk_buff *skb;
	struct edl_event_hdr *edl;
	int err = 0;

	cmd = EDL_GET_BID_REQ_CMD;
	skb = __hci_cmd_sync_ev(hdev, EDL_PATCH_CMD_OPCODE, EDL_PATCH_CMD_LEN,
				&cmd, 0, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Reading QCA board ID failed (%d)", err);
		return err;
	}

	edl = skb_pull_data(skb, sizeof(*edl));
	if (!edl) {
		bt_dev_err(hdev, "QCA read board ID with no header");
		err = -EILSEQ;
		goto out;
	}

	if (edl->cresp != EDL_CMD_REQ_RES_EVT ||
	    edl->rtype != EDL_GET_BID_REQ_CMD) {
		bt_dev_err(hdev, "QCA Wrong packet: %d %d", edl->cresp, edl->rtype);
		err = -EIO;
		goto out;
	}

	if (skb->len < 3) {
		err = -EILSEQ;
		goto out;
	}

	*bid = (edl->data[1] << 8) + edl->data[2];
	bt_dev_dbg(hdev, "%s: bid = %x", __func__, *bid);

out:
	kfree_skb(skb);
	return err;
}

int qca_send_pre_shutdown_cmd(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	int err;

	bt_dev_dbg(hdev, "QCA pre shutdown cmd");

	skb = __hci_cmd_sync_ev(hdev, QCA_PRE_SHUTDOWN_CMD, 0,
				NULL, HCI_EV_CMD_COMPLETE, HCI_INIT_TIMEOUT);

	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA preshutdown_cmd failed (%d)", err);
		return err;
	}

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(qca_send_pre_shutdown_cmd);

static bool qca_filename_has_extension(const char *filename)
{
	const char *suffix = strrchr(filename, '.');

	/* File extensions require a dot, but not as the first or last character */
	if (!suffix || suffix == filename || *(suffix + 1) == '\0')
		return 0;

	/* Avoid matching directories with names that look like files with extensions */
	return !strchr(suffix, '/');
}

static bool qca_get_alt_nvm_file(char *filename, size_t max_size)
{
	char fwname[64];
	const char *suffix;

	/* nvm file name has an extension, replace with .bin */
	if (qca_filename_has_extension(filename)) {
		suffix = strrchr(filename, '.');
		strscpy(fwname, filename, suffix - filename + 1);
		snprintf(fwname + (suffix - filename),
		       sizeof(fwname) - (suffix - filename), ".bin");
		/* If nvm file is already the default one, return false to skip the retry. */
		if (strcmp(fwname, filename) == 0)
			return false;

		snprintf(filename, max_size, "%s", fwname);
		return true;
	}
	return false;
}

static int qca_tlv_check_data(struct hci_dev *hdev,
			       struct qca_fw_config *config,
			       u8 *fw_data, size_t fw_size,
			       enum qca_btsoc_type soc_type)
{
	const u8 *data;
	u32 type_len;
	u16 tag_id, tag_len;
	int idx, length;
	struct tlv_type_hdr *tlv;
	struct tlv_type_patch *tlv_patch;
	struct tlv_type_nvm *tlv_nvm;
	uint8_t nvm_baud_rate = config->user_baud_rate;
	u8 type;

	config->dnld_mode = QCA_SKIP_EVT_NONE;
	config->dnld_type = QCA_SKIP_EVT_NONE;

	switch (config->type) {
	case ELF_TYPE_PATCH:
		if (fw_size < 7)
			return -EINVAL;

		config->dnld_mode = QCA_SKIP_EVT_VSE_CC;
		config->dnld_type = QCA_SKIP_EVT_VSE_CC;

		bt_dev_dbg(hdev, "File Class        : 0x%x", fw_data[4]);
		bt_dev_dbg(hdev, "Data Encoding     : 0x%x", fw_data[5]);
		bt_dev_dbg(hdev, "File version      : 0x%x", fw_data[6]);
		break;
	case TLV_TYPE_PATCH:
		if (fw_size < sizeof(struct tlv_type_hdr) + sizeof(struct tlv_type_patch))
			return -EINVAL;

		tlv = (struct tlv_type_hdr *)fw_data;
		type_len = le32_to_cpu(tlv->type_len);
		tlv_patch = (struct tlv_type_patch *)tlv->data;

		/* For Rome version 1.1 to 3.1, all segment commands
		 * are acked by a vendor specific event (VSE).
		 * For Rome >= 3.2, the download mode field indicates
		 * if VSE is skipped by the controller.
		 * In case VSE is skipped, only the last segment is acked.
		 */
		config->dnld_mode = tlv_patch->download_mode;
		config->dnld_type = config->dnld_mode;

		BT_DBG("TLV Type\t\t : 0x%x", type_len & 0x000000ff);
		BT_DBG("Total Length           : %d bytes",
		       le32_to_cpu(tlv_patch->total_size));
		BT_DBG("Patch Data Length      : %d bytes",
		       le32_to_cpu(tlv_patch->data_length));
		BT_DBG("Signing Format Version : 0x%x",
		       tlv_patch->format_version);
		BT_DBG("Signature Algorithm    : 0x%x",
		       tlv_patch->signature);
		BT_DBG("Download mode          : 0x%x",
		       tlv_patch->download_mode);
		BT_DBG("Reserved               : 0x%x",
		       tlv_patch->reserved1);
		BT_DBG("Product ID             : 0x%04x",
		       le16_to_cpu(tlv_patch->product_id));
		BT_DBG("Rom Build Version      : 0x%04x",
		       le16_to_cpu(tlv_patch->rom_build));
		BT_DBG("Patch Version          : 0x%04x",
		       le16_to_cpu(tlv_patch->patch_version));
		BT_DBG("Reserved               : 0x%x",
		       le16_to_cpu(tlv_patch->reserved2));
		BT_DBG("Patch Entry Address    : 0x%x",
		       le32_to_cpu(tlv_patch->entry));
		break;

	case TLV_TYPE_NVM:
		if (fw_size < sizeof(struct tlv_type_hdr))
			return -EINVAL;

		tlv = (struct tlv_type_hdr *)fw_data;

		type_len = le32_to_cpu(tlv->type_len);
		length = type_len >> 8;
		type = type_len & 0xff;

		/* Some NVM files have more than one set of tags, only parse
		 * the first set when it has type 2 for now. When there is
		 * more than one set there is an enclosing header of type 4.
		 */
		if (type == 4) {
			if (fw_size < 2 * sizeof(struct tlv_type_hdr))
				return -EINVAL;

			tlv++;

			type_len = le32_to_cpu(tlv->type_len);
			length = type_len >> 8;
			type = type_len & 0xff;
		}

		BT_DBG("TLV Type\t\t : 0x%x", type);
		BT_DBG("Length\t\t : %d bytes", length);

		if (type != 2)
			break;

		if (fw_size < length + (tlv->data - fw_data))
			return -EINVAL;

		idx = 0;
		data = tlv->data;
		while (idx < length - sizeof(struct tlv_type_nvm)) {
			tlv_nvm = (struct tlv_type_nvm *)(data + idx);

			tag_id = le16_to_cpu(tlv_nvm->tag_id);
			tag_len = le16_to_cpu(tlv_nvm->tag_len);

			if (length < idx + sizeof(struct tlv_type_nvm) + tag_len)
				return -EINVAL;

			/* Update NVM tags as needed */
			switch (tag_id) {
			case EDL_TAG_ID_BD_ADDR:
				if (tag_len != sizeof(bdaddr_t))
					return -EINVAL;

				memcpy(&config->bdaddr, tlv_nvm->data, sizeof(bdaddr_t));

				break;

			case EDL_TAG_ID_HCI:
				if (tag_len < 3)
					return -EINVAL;

				/* HCI transport layer parameters
				 * enabling software inband sleep
				 * onto controller side.
				 */
				tlv_nvm->data[0] |= 0x80;

				/* UART Baud Rate */
				if (soc_type >= QCA_WCN3991)
					tlv_nvm->data[1] = nvm_baud_rate;
				else
					tlv_nvm->data[2] = nvm_baud_rate;

				break;

			case EDL_TAG_ID_DEEP_SLEEP:
				if (tag_len < 1)
					return -EINVAL;

				/* Sleep enable mask
				 * enabling deep sleep feature on controller.
				 */
				tlv_nvm->data[0] |= 0x01;

				break;
			}

			idx += sizeof(struct tlv_type_nvm) + tag_len;
		}
		break;

	default:
		BT_ERR("Unknown TLV type %d", config->type);
		return -EINVAL;
	}

	return 0;
}

static int qca_tlv_send_segment(struct hci_dev *hdev, int seg_size,
				const u8 *data, enum qca_tlv_dnld_mode mode,
				enum qca_btsoc_type soc_type)
{
	struct sk_buff *skb;
	struct edl_event_hdr *edl;
	struct tlv_seg_resp *tlv_resp;
	u8 cmd[MAX_SIZE_PER_TLV_SEGMENT + 2];
	int err = 0;
	u8 event_type = HCI_EV_VENDOR;
	u8 rlen = (sizeof(*edl) + sizeof(*tlv_resp));
	u8 rtype = EDL_TVL_DNLD_RES_EVT;

	cmd[0] = EDL_PATCH_TLV_REQ_CMD;
	cmd[1] = seg_size;
	memcpy(cmd + 2, data, seg_size);

	if (mode == QCA_SKIP_EVT_VSE_CC || mode == QCA_SKIP_EVT_VSE)
		return __hci_cmd_send(hdev, EDL_PATCH_CMD_OPCODE, seg_size + 2,
				      cmd);

	/* Unlike other SoC's sending version command response as payload to
	 * VSE event. WCN3991 sends version command response as a payload to
	 * command complete event.
	 */
	if (soc_type >= QCA_WCN3991) {
		event_type = 0;
		rlen = sizeof(*edl);
		rtype = EDL_PATCH_TLV_REQ_CMD;
	}

	skb = __hci_cmd_sync_ev(hdev, EDL_PATCH_CMD_OPCODE, seg_size + 2, cmd,
				event_type, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA Failed to send TLV segment (%d)", err);
		return err;
	}

	if (skb->len != rlen) {
		bt_dev_err(hdev, "QCA TLV response size mismatch");
		err = -EILSEQ;
		goto out;
	}

	edl = (struct edl_event_hdr *)(skb->data);

	if (edl->cresp != EDL_CMD_REQ_RES_EVT || edl->rtype != rtype) {
		bt_dev_err(hdev, "QCA TLV with error stat 0x%x rtype 0x%x",
			   edl->cresp, edl->rtype);
		err = -EIO;
	}

	if (soc_type >= QCA_WCN3991)
		goto out;

	tlv_resp = (struct tlv_seg_resp *)(edl->data);
	if (tlv_resp->result) {
		bt_dev_err(hdev, "QCA TLV with error stat 0x%x rtype 0x%x (0x%x)",
			   edl->cresp, edl->rtype, tlv_resp->result);
	}

out:
	kfree_skb(skb);

	return err;
}

static int qca_inject_cmd_complete_event(struct hci_dev *hdev)
{
	struct hci_event_hdr *hdr;
	struct hci_ev_cmd_complete *evt;
	struct sk_buff *skb;

	skb = bt_skb_alloc(sizeof(*hdr) + sizeof(*evt) + 1, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = skb_put(skb, sizeof(*hdr));
	hdr->evt = HCI_EV_CMD_COMPLETE;
	hdr->plen = sizeof(*evt) + 1;

	evt = skb_put(skb, sizeof(*evt));
	evt->ncmd = 1;
	evt->opcode = cpu_to_le16(QCA_HCI_CC_OPCODE);

	skb_put_u8(skb, QCA_HCI_CC_SUCCESS);

	hci_skb_pkt_type(skb) = HCI_EVENT_PKT;

	return hci_recv_frame(hdev, skb);
}

static int qca_download_firmware(struct hci_dev *hdev,
				 struct qca_fw_config *config,
				 enum qca_btsoc_type soc_type,
				 u8 rom_ver)
{
	const struct firmware *fw;
	u8 *data;
	const u8 *segment;
	int ret, size, remain, i = 0;

	bt_dev_info(hdev, "QCA Downloading %s", config->fwname);

	ret = request_firmware(&fw, config->fwname, &hdev->dev);
	if (ret) {
		/* For WCN6750, if mbn file is not present then check for
		 * tlv file.
		 */
		if (soc_type == QCA_WCN6750 && config->type == ELF_TYPE_PATCH) {
			bt_dev_dbg(hdev, "QCA Failed to request file: %s (%d)",
				   config->fwname, ret);
			config->type = TLV_TYPE_PATCH;
			snprintf(config->fwname, sizeof(config->fwname),
				 "qca/msbtfw%02x.tlv", rom_ver);
			bt_dev_info(hdev, "QCA Downloading %s", config->fwname);
			ret = request_firmware(&fw, config->fwname, &hdev->dev);
			if (ret) {
				bt_dev_err(hdev, "QCA Failed to request file: %s (%d)",
					   config->fwname, ret);
				return ret;
			}
		}
		/* If the board-specific file is missing, try loading the default
		 * one, unless that was attempted already.
		 */
		else if (config->type == TLV_TYPE_NVM &&
			 qca_get_alt_nvm_file(config->fwname, sizeof(config->fwname))) {
			bt_dev_info(hdev, "QCA Downloading %s", config->fwname);
			ret = request_firmware(&fw, config->fwname, &hdev->dev);
			if (ret) {
				bt_dev_err(hdev, "QCA Failed to request file: %s (%d)",
					   config->fwname, ret);
				return ret;
			}
		} else {
			bt_dev_err(hdev, "QCA Failed to request file: %s (%d)",
				   config->fwname, ret);
			return ret;
		}
	}

	size = fw->size;
	data = vmalloc(fw->size);
	if (!data) {
		bt_dev_err(hdev, "QCA Failed to allocate memory for file: %s",
			   config->fwname);
		release_firmware(fw);
		return -ENOMEM;
	}

	memcpy(data, fw->data, size);
	release_firmware(fw);

	ret = qca_tlv_check_data(hdev, config, data, size, soc_type);
	if (ret)
		goto out;

	segment = data;
	remain = size;
	while (remain > 0) {
		int segsize = min(MAX_SIZE_PER_TLV_SEGMENT, remain);

		bt_dev_dbg(hdev, "Send segment %d, size %d", i++, segsize);

		remain -= segsize;
		/* The last segment is always acked regardless download mode */
		if (!remain || segsize < MAX_SIZE_PER_TLV_SEGMENT)
			config->dnld_mode = QCA_SKIP_EVT_NONE;

		ret = qca_tlv_send_segment(hdev, segsize, segment,
					   config->dnld_mode, soc_type);
		if (ret)
			goto out;

		segment += segsize;
	}

	/* Latest qualcomm chipsets are not sending a command complete event
	 * for every fw packet sent. They only respond with a vendor specific
	 * event for the last packet. This optimization in the chip will
	 * decrease the BT in initialization time. Here we will inject a command
	 * complete event to avoid a command timeout error message.
	 */
	if (config->dnld_type == QCA_SKIP_EVT_VSE_CC ||
	    config->dnld_type == QCA_SKIP_EVT_VSE)
		ret = qca_inject_cmd_complete_event(hdev);

out:
	vfree(data);

	return ret;
}

static int qca_disable_soc_logging(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	u8 cmd[2];
	int err;

	cmd[0] = QCA_DISABLE_LOGGING_SUB_OP;
	cmd[1] = 0x00;
	skb = __hci_cmd_sync_ev(hdev, QCA_DISABLE_LOGGING, sizeof(cmd), cmd,
				HCI_EV_CMD_COMPLETE, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA Failed to disable soc logging(%d)", err);
		return err;
	}

	kfree_skb(skb);

	return 0;
}

int qca_set_bdaddr_rome(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	u8 cmd[9];
	int err;

	cmd[0] = EDL_NVM_ACCESS_SET_REQ_CMD;
	cmd[1] = 0x02; 			/* TAG ID */
	cmd[2] = sizeof(bdaddr_t);	/* size */
	memcpy(cmd + 3, bdaddr, sizeof(bdaddr_t));
	skb = __hci_cmd_sync_ev(hdev, EDL_NVM_ACCESS_OPCODE, sizeof(cmd), cmd,
				HCI_EV_VENDOR, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA Change address command failed (%d)", err);
		return err;
	}

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(qca_set_bdaddr_rome);

static int qca_check_bdaddr(struct hci_dev *hdev, const struct qca_fw_config *config)
{
	struct hci_rp_read_bd_addr *bda;
	struct sk_buff *skb;
	int err;

	if (bacmp(&hdev->public_addr, BDADDR_ANY))
		return 0;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_BD_ADDR, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Failed to read device address (%d)", err);
		return err;
	}

	if (skb->len != sizeof(*bda)) {
		bt_dev_err(hdev, "Device address length mismatch");
		kfree_skb(skb);
		return -EIO;
	}

	bda = (struct hci_rp_read_bd_addr *)skb->data;
	if (!bacmp(&bda->bdaddr, &config->bdaddr))
		set_bit(HCI_QUIRK_USE_BDADDR_PROPERTY, &hdev->quirks);

	kfree_skb(skb);

	return 0;
}

static void qca_get_nvm_name_by_board(char *fwname, size_t max_size,
		const char *stem, enum qca_btsoc_type soc_type,
		struct qca_btsoc_version ver, u8 rom_ver, u16 bid)
{
	const char *variant;
	const char *prefix;

	/* Set the default value to variant and prefix */
	variant = "";
	prefix = "b";

	if (soc_type == QCA_QCA2066)
		prefix = "";

	if (soc_type == QCA_WCN6855 || soc_type == QCA_QCA2066) {
		/* If the chip is manufactured by GlobalFoundries */
		if ((le32_to_cpu(ver.soc_id) & QCA_HSP_GF_SOC_MASK) == QCA_HSP_GF_SOC_ID)
			variant = "g";
	}

	if (rom_ver != 0) {
		if (bid == 0x0 || bid == 0xffff)
			snprintf(fwname, max_size, "qca/%s%02x%s.bin", stem, rom_ver, variant);
		else
			snprintf(fwname, max_size, "qca/%s%02x%s.%s%02x", stem, rom_ver,
						variant, prefix, bid);
	} else {
		if (bid == 0x0 || bid == 0xffff)
			snprintf(fwname, max_size, "qca/%s%s.bin", stem, variant);
		else
			snprintf(fwname, max_size, "qca/%s%s.%s%02x", stem, variant, prefix, bid);
	}
}

int qca_uart_setup(struct hci_dev *hdev, uint8_t baudrate,
		   enum qca_btsoc_type soc_type, struct qca_btsoc_version ver,
		   const char *firmware_name, const char *rampatch_name)
{
	struct qca_fw_config config = {};
	const char *variant = "";
	int err;
	u8 rom_ver = 0;
	u32 soc_ver;
	u16 boardid = 0;

	bt_dev_dbg(hdev, "QCA setup on UART");

	soc_ver = get_soc_ver(ver.soc_id, ver.rom_ver);

	bt_dev_info(hdev, "QCA controller version 0x%08x", soc_ver);

	config.user_baud_rate = baudrate;

	/* Firmware files to download are based on ROM version.
	 * ROM version is derived from last two bytes of soc_ver.
	 */
	if (soc_type == QCA_WCN3988)
		rom_ver = ((soc_ver & 0x00000f00) >> 0x05) | (soc_ver & 0x0000000f);
	else
		rom_ver = ((soc_ver & 0x00000f00) >> 0x04) | (soc_ver & 0x0000000f);

	if (soc_type == QCA_WCN6750) {
		qca_send_patch_config_cmd(hdev);
	}

	/* Download rampatch file */
	config.type = TLV_TYPE_PATCH;
	if (rampatch_name) {
		snprintf(config.fwname, sizeof(config.fwname), "qca/%s", rampatch_name);
	} else {
		switch (soc_type) {
		case QCA_WCN3990:
		case QCA_WCN3991:
		case QCA_WCN3998:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/crbtfw%02x.tlv", rom_ver);
			break;
		case QCA_WCN3988:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/apbtfw%02x.tlv", rom_ver);
			break;
		case QCA_QCA2066:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/hpbtfw%02x.tlv", rom_ver);
			break;
		case QCA_QCA6390:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/htbtfw%02x.tlv", rom_ver);
			break;
		case QCA_WCN6750:
			/* Choose mbn file by default.If mbn file is not found
			 * then choose tlv file
			 */
			config.type = ELF_TYPE_PATCH;
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/msbtfw%02x.mbn", rom_ver);
			break;
		case QCA_WCN6855:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/hpbtfw%02x.tlv", rom_ver);
			break;
		case QCA_WCN7850:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/hmtbtfw%02x.tlv", rom_ver);
			break;
		default:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/rampatch_%08x.bin", soc_ver);
		}
	}

	err = qca_download_firmware(hdev, &config, soc_type, rom_ver);
	if (err < 0) {
		bt_dev_err(hdev, "QCA Failed to download patch (%d)", err);
		return err;
	}

	/* Give the controller some time to get ready to receive the NVM */
	msleep(10);

	if (soc_type == QCA_QCA2066 || soc_type == QCA_WCN7850)
		qca_read_fw_board_id(hdev, &boardid);

	/* Download NVM configuration */
	config.type = TLV_TYPE_NVM;
	if (firmware_name) {
		/* The firmware name has an extension, use it directly */
		if (qca_filename_has_extension(firmware_name)) {
			snprintf(config.fwname, sizeof(config.fwname), "qca/%s", firmware_name);
		} else {
			qca_read_fw_board_id(hdev, &boardid);
			qca_get_nvm_name_by_board(config.fwname, sizeof(config.fwname),
				 firmware_name, soc_type, ver, 0, boardid);
		}
	} else {
		switch (soc_type) {
		case QCA_WCN3990:
		case QCA_WCN3991:
		case QCA_WCN3998:
			if (le32_to_cpu(ver.soc_id) == QCA_WCN3991_SOC_ID)
				variant = "u";

			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/crnv%02x%s.bin", rom_ver, variant);
			break;
		case QCA_WCN3988:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/apnv%02x.bin", rom_ver);
			break;
		case QCA_QCA2066:
			qca_get_nvm_name_by_board(config.fwname,
				sizeof(config.fwname), "hpnv", soc_type, ver,
				rom_ver, boardid);
			break;
		case QCA_QCA6390:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/htnv%02x.bin", rom_ver);
			break;
		case QCA_WCN6750:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/msnv%02x.bin", rom_ver);
			break;
		case QCA_WCN6855:
			qca_read_fw_board_id(hdev, &boardid);
			qca_get_nvm_name_by_board(config.fwname, sizeof(config.fwname),
						  "hpnv", soc_type, ver, rom_ver, boardid);
			break;
		case QCA_WCN7850:
			qca_get_nvm_name_by_board(config.fwname, sizeof(config.fwname),
				 "hmtnv", soc_type, ver, rom_ver, boardid);
			break;
		default:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/nvm_%08x.bin", soc_ver);
		}
	}

	err = qca_download_firmware(hdev, &config, soc_type, rom_ver);
	if (err < 0) {
		bt_dev_err(hdev, "QCA Failed to download NVM (%d)", err);
		return err;
	}

	switch (soc_type) {
	case QCA_WCN3991:
	case QCA_QCA2066:
	case QCA_QCA6390:
	case QCA_WCN6750:
	case QCA_WCN6855:
	case QCA_WCN7850:
		err = qca_disable_soc_logging(hdev);
		if (err < 0)
			return err;
		break;
	default:
		break;
	}

	/* WCN399x and WCN6750 supports the Microsoft vendor extension with 0xFD70 as the
	 * VsMsftOpCode.
	 */
	switch (soc_type) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
	case QCA_WCN6750:
		hci_set_msft_opcode(hdev, 0xFD70);
		break;
	default:
		break;
	}

	/* Perform HCI reset */
	err = qca_send_reset(hdev);
	if (err < 0) {
		bt_dev_err(hdev, "QCA Failed to run HCI_RESET (%d)", err);
		return err;
	}

	switch (soc_type) {
	case QCA_WCN3991:
	case QCA_WCN6750:
	case QCA_WCN6855:
	case QCA_WCN7850:
		/* get fw build info */
		err = qca_read_fw_build_info(hdev);
		if (err < 0)
			return err;
		break;
	default:
		break;
	}

	err = qca_check_bdaddr(hdev, &config);
	if (err)
		return err;

	bt_dev_info(hdev, "QCA setup on UART is completed");

	return 0;
}
EXPORT_SYMBOL_GPL(qca_uart_setup);

int qca_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	bdaddr_t bdaddr_swapped;
	struct sk_buff *skb;
	int err;

	baswap(&bdaddr_swapped, bdaddr);

	skb = __hci_cmd_sync_ev(hdev, EDL_WRITE_BD_ADDR_OPCODE, 6,
				&bdaddr_swapped, HCI_EV_VENDOR,
				HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA Change address cmd failed (%d)", err);
		return err;
	}

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(qca_set_bdaddr);


MODULE_AUTHOR("Ben Young Tae Kim <ytkim@qca.qualcomm.com>");
MODULE_DESCRIPTION("Bluetooth support for Qualcomm Atheros family ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
