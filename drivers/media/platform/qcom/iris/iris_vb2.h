/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_VB2_H_
#define _IRIS_VB2_H_

#include <media/videobuf2-v4l2.h>

int iris_vb2_queue_setup(struct vb2_queue *q,
			 unsigned int *num_buffers, unsigned int *num_planes,
			 unsigned int sizes[], struct device *alloc_devs[]);
int iris_vb2_start_streaming(struct vb2_queue *q, unsigned int count);
void iris_vb2_stop_streaming(struct vb2_queue *q);
void iris_vb2_buf_queue(struct vb2_buffer *vb2);

/* vb2_mem_ops */
void *iris_vb2_alloc(struct vb2_buffer *vb, struct device *dev, unsigned long size);
void *iris_vb2_attach_dmabuf(struct vb2_buffer *vb, struct device *dev, struct dma_buf *dbuf,
			     unsigned long size);
void iris_vb2_put(void *buf_priv);
int iris_vb2_mmap(void *buf_priv, struct vm_area_struct *vma);
void iris_vb2_detach_dmabuf(void *buf_priv);
int iris_vb2_map_dmabuf(void *buf_priv);
void iris_vb2_unmap_dmabuf(void *buf_priv);

#endif
