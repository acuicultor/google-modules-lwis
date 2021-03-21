/*
 * Google LWIS Top Level Device Driver
 *
 * Copyright (c) 2018 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef LWIS_DEVICE_TOP_H_
#define LWIS_DEVICE_TOP_H_

#include "lwis_device.h"

#define SCRATCH_MEMORY_SIZE 16

/*
 *  struct lwis_top_device
 *  "Derived" lwis_device struct, with added top device related elements.
 */
struct lwis_top_device {
	struct lwis_device base_dev;
	/* For testing purposes, scratch memory is used as register space in
	 * top device.
	 */
	uint8_t scratch_mem[SCRATCH_MEMORY_SIZE];
	/* Hash table for event subscriber */
	DECLARE_HASHTABLE(event_subscriber, EVENT_HASH_BITS);

	/* Subscription tasklet */
	struct tasklet_struct subscribe_tasklet;
	struct list_head emitted_event_list_tasklet;
};

int lwis_top_device_deinit(void);
#endif /* LWIS_DEVICE_TOP_H_ */
