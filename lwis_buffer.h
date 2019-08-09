/*
 * Google LWIS DMA Buffer Utilities
 *
 *
 * Copyright (c) 2018 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef LWIS_BUFFER_H_
#define LWIS_BUFFER_H_

#include <linux/dma-buf.h>
#include <linux/dma-direction.h>
#include <linux/list.h>

#include "lwis_commands.h"

struct lwis_buffer {
	struct lwis_buffer_info info;
	enum dma_data_direction dma_direction;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *dma_buf_attachment;
	struct sg_table *sg_table;
	struct hlist_node node;
};

/*
 * lwis_buffer_alloc: Allocates a DMA buffer represented by alloc_info,
 * maps it into the device IO space and returns a file descriptor
 *
 * Assumes: lwisclient->lock is locked
 * Alloc: Yes
 * Returns: 0 on success
 */
int lwis_buffer_alloc(struct lwis_client *lwis_client,
		      struct lwis_alloc_buffer_info *alloc_info);

/*
 * lwis_buffer_enroll: Maps the DMA buffer represented by the file descriptor
 * passed in buffer->info.fd into IO space and adds the buffer object into
 * the table of this client's enrolled buffers
 *
 * Assumes: lwisclient->lock is locked
 * Alloc: Yes
 * Returns: 0 on success
 */
int lwis_buffer_enroll(struct lwis_client *lwis_client,
		       struct lwis_buffer *buffer);

/*
 * lwis_buffer_disenroll: Unmaps the buffer from IO space and removes the
 * object from the hash table
 *
 * Assumes: lwisclient->lock is locked
 * Alloc: Yes
 * Returns: 0 on success
 */
int lwis_buffer_disenroll(struct lwis_client *lwis_client,
			  struct lwis_buffer *buffer);

/*
 * lwis_client_enrolled_buffer_find: Finds the enrolled buffer based on
 * dma_vaddr passed, and returns it
 *
 * Assumes: lwisclient->lock is locked
 * Alloc: Yes
 * Returns: Pointer on success, NULL otherwise
 */
struct lwis_buffer *
lwis_client_enrolled_buffer_find(struct lwis_client *lwis_client,
				 dma_addr_t dma_vaddr);

/*
 * lwis_client_enrolled_buffers_clear: Frees all items in
 * lwisclient->enrolled_buffers and clears the hash table. Used for client
 * shutdown only.
 *
 * Assumes: lwisclient->lock is locked
 * Alloc: Free only
 * Returns: 0 on success
 */
int lwis_client_enrolled_buffers_clear(struct lwis_client *lwis_client);

#endif /* LWIS_BUFFER_H_ */