/*
 * Google LWIS Misc Utility Functions and Wrappers
 *
 * Copyright (c) 2018 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef LWIS_UTIL_H_
#define LWIS_UTIL_H_

#include <linux/kernel.h>

#include "lwis_commands.h"

/* Forward declaration for lwis_device. This is needed for the function
 * prototypes below that take a pointer to lwis_device */
struct lwis_device;

/*
 * lwis_device_single_register_write: A utility function that allows you to
 * write a single register for a given bid, offset and value on any device
 * that supports register writes.
 *
 * non_blocking: Specifies whether blocking is allowed (i.e. should be set to
 * true when called with IRQs disabled, or from an ISR)
 *
 * Returns: 0 on success
 * -EAGAIN if non_blocking is true and the operation would need to block
 * -ENXIO if register offset is out of range allowed for bid
 * Other errors are possible
 */
int lwis_device_single_register_write(struct lwis_device *lwis_dev,
				      bool non_blocking, int bid,
				      uint64_t offset, uint64_t value,
				      int access_size);

/*
 * lwis_device_single_register_read: A utility function that allows you to
 * read a single register for a given bid, offset and value on any device
 * that supports register reads.
 *
 * non_blocking: Specifies whether blocking is allowed (i.e. should be set to
 * true when called with IRQs disabled, or from an ISR)
 *
 * Returns: 0 on success
 * -EAGAIN if non_blocking is true and the operation would need to block
 * -ENXIO if register offset is out of range allowed for bid
 * Other errors are possible
 */
int lwis_device_single_register_read(struct lwis_device *lwis_dev,
				     bool non_blocking, int bid,
				     uint64_t offset, uint64_t *value,
				     int access_size);

const char *lwis_device_type_to_string(enum lwis_device_types type);

#endif // LWIS_UTIL_H_
