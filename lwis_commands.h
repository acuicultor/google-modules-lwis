/* BEGIN-INTERNAL */
/*
 * Google LWIS IOCTL Commands and Data Structures
 *
 * Copyright (c) 2018 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
/* END-INTERNAL */
#ifndef LWIS_COMMANDS_H_
#define LWIS_COMMANDS_H_

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#endif /* __KERNEL__ */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 *  IOCTL Types and Data Structures.
 */

/*
 * lwis_device_types
 * top  : top level device that overlooks all the LWIS devices. Will be used to
 *        list the information of the other LWIS devices in the system.
 * i2c  : for controlling i2c devices
 * ioreg: for controlling mapped register I/O devices
 */
enum lwis_device_types {
	DEVICE_TYPE_UNKNOWN = -1,
	DEVICE_TYPE_TOP = 0,
	DEVICE_TYPE_I2C,
	DEVICE_TYPE_IOREG,
	DEVICE_TYPE_SLC,
	NUM_DEVICE_TYPES
};

/* Device tree strings have a maximum length of 31, according to specs.
   Adding 1 byte for the null character. */
#define LWIS_MAX_NAME_STRING_LEN 32
/* Maximum clock number defined in device tree. */
#define LWIS_MAX_CLOCK_NUM 20

struct lwis_clk_setting {
	// clock name defined in device tree.
	char name[LWIS_MAX_NAME_STRING_LEN];
	// clock index stored in lwis_dev->clocks
	int32_t clk_index;
	// clock rate
	uint32_t frequency;
};

struct lwis_device_info {
	int id;
	enum lwis_device_types type;
	char name[LWIS_MAX_NAME_STRING_LEN];
	struct lwis_clk_setting clks[LWIS_MAX_CLOCK_NUM];
	int32_t num_clks;
};

enum lwis_dma_alloc_flags {
	// Allocates a cached buffer.
	LWIS_DMA_BUFFER_CACHED = 1UL << 0,
	// Allocates a buffer which is not initialized to 0 to avoid
	// initialization overhead.
	LWIS_DMA_BUFFER_UNINITIALIZED = 1UL << 1,
	// Allocates a buffer which is stored in contiguous memory.
	LWIS_DMA_BUFFER_CONTIGUOUS = 1UL << 2,
	// Allocates a buffer represent system cache reservation.
	LWIS_DMA_SYSTEM_CACHE_RESERVATION = 1UL << 3,
	// Allocates a secure buffer.
	LWIS_DMA_BUFFER_SECURE = 1UL << 4,
};

struct lwis_alloc_buffer_info {
	// IOCTL input for BUFFER_ALLOC
	size_t size;
	uint32_t flags; // lwis_dma_alloc_flags
	// IOCTL output for BUFFER_ALLOC
	int dma_fd;
};

struct lwis_buffer_info {
	// IOCTL input for BUFFER_ENROLL
	int fd;
	bool dma_read;
	bool dma_write;
	// IOCTL output for BUFFER_ENROLL
	uint64_t dma_vaddr;
};

enum lwis_io_entry_types {
	LWIS_IO_ENTRY_READ,
	LWIS_IO_ENTRY_READ_BATCH,
	LWIS_IO_ENTRY_WRITE,
	LWIS_IO_ENTRY_WRITE_BATCH,
	LWIS_IO_ENTRY_MODIFY,
	LWIS_IO_ENTRY_BIAS,
	LWIS_IO_ENTRY_POLL
};

// For io_entry read and write types.
struct lwis_io_entry_rw {
	int bid;
	uint64_t offset;
	uint64_t val;
};

struct lwis_io_entry_rw_batch {
	int bid;
	uint64_t offset;
	size_t size_in_bytes;
	uint8_t *buf;
};

// For io_entry modify types.
struct lwis_io_entry_modify {
	int bid;
	uint64_t offset;
	uint64_t val;
	uint64_t val_mask;
};

struct lwis_io_entry_set_bias {
	uint64_t bias;
};

// For io_entry poll type.
struct lwis_io_entry_poll {
	int bid;
	uint64_t offset;
	uint64_t val;
	uint64_t mask;
	uint64_t timeout_ms;
};

struct lwis_io_entry {
	int type;
	union {
		struct lwis_io_entry_rw rw;
		struct lwis_io_entry_rw_batch rw_batch;
		struct lwis_io_entry_modify mod;
		struct lwis_io_entry_set_bias set_bias;
		struct lwis_io_entry_poll poll;
	};
};

struct lwis_io_entries {
	uint32_t num_io_entries;
	struct lwis_io_entry *io_entries;
};

struct lwis_echo {
	size_t size;
	char *msg;
};

/* The first 4096 event IDs are reserved for generic events shared by all
 * devices.
 *
 * The rest are specific to device specializations
 */
// Event NONE and INVALID are intended to be sharing the same ID.
#define LWIS_EVENT_ID_NONE 0
#define LWIS_EVENT_ID_INVALID 0
#define LWIS_EVENT_ID_HEARTBEAT 1
#define LWIS_EVENT_ID_CLIENT_CLEANUP 2
// ...
#define LWIS_EVENT_ID_START_OF_SPECIALIZED_RANGE 4096

// Event flags used for transaction events.
#define LWIS_TRANSACTION_EVENT_FLAG (1ULL << 63)
#define LWIS_TRANSACTION_FAILURE_EVENT_FLAG (1ULL << 62)

struct lwis_event_info {
	// IOCTL Inputs
	size_t payload_buffer_size;
	void *payload_buffer;
	// IOCTL Outputs
	int64_t event_id;
	int64_t event_counter;
	int64_t timestamp_ns;
	size_t payload_size;
};

#define LWIS_EVENT_CONTROL_FLAG_IRQ_ENABLE (1ULL << 0)
#define LWIS_EVENT_CONTROL_FLAG_QUEUE_ENABLE (1ULL << 1)

struct lwis_event_control {
	// IOCTL Inputs
	int64_t event_id;
	// IOCTL Outputs
	uint64_t flags;
};

// Invalid ID for Transaction id and Periodic IO id
#define LWIS_ID_INVALID (-1LL)
#define LWIS_EVENT_COUNTER_ON_NEXT_OCCURRENCE (-1LL)
struct lwis_transaction_info {
	// Input
	int trigger_device_id;
	int64_t trigger_event_id;
	int64_t trigger_event_counter;
	size_t num_io_entries;
	struct lwis_io_entry *io_entries;
	bool run_in_event_context;
	int64_t emit_success_event_id;
	int64_t emit_error_event_id;
	// Output
	int64_t id;
	// Only will be set if trigger_event_id is specified.
	// Otherwise, the value is -1.
	int64_t current_trigger_event_counter;
};

// Actual size of this struct depends on num_entries
struct lwis_transaction_response_header {
	int64_t id;
	int error_code;
	int completion_index;
	size_t num_entries;
	size_t results_size_bytes;
};

struct lwis_io_result {
	int bid;
	uint64_t offset;
	size_t num_value_bytes;
	uint8_t values[];
};

struct lwis_event_subscribe {
	int trigger_device_id;
	int64_t trigger_event_id;
};

struct lwis_periodic_io_info {
	// Input
	int batch_size;
	int64_t period_ms;
	size_t num_io_entries;
	struct lwis_io_entry *io_entries;
	int64_t emit_success_event_id;
	int64_t emit_error_event_id;
	// Output
	int64_t id;
};

// Header of a periodic_io response as a payload of lwis_event_info
// Actual size of this struct depends on batch_size and num_entries_per_period
struct lwis_periodic_io_response_header {
	int64_t id;
	int error_code;
	int batch_size;
	size_t num_entries_per_period;
	size_t results_size_bytes;
};

struct lwis_periodic_io_result {
	int64_t timestamp_ns;
	struct lwis_io_result io_result;
};

struct lwis_dpm_clk_settings {
	struct lwis_clk_setting *settings;
	size_t num_settings;
};

/*
 *  IOCTL Commands
 */

#define LWIS_IOC_TYPE 'L'

#define LWIS_GET_DEVICE_INFO _IOWR(LWIS_IOC_TYPE, 1, struct lwis_device_info)
#define LWIS_BUFFER_ENROLL _IOWR(LWIS_IOC_TYPE, 2, struct lwis_buffer_info)
#define LWIS_BUFFER_DISENROLL _IOWR(LWIS_IOC_TYPE, 3, uint64_t)
#define LWIS_DEVICE_ENABLE _IO(LWIS_IOC_TYPE, 6)
#define LWIS_DEVICE_DISABLE _IO(LWIS_IOC_TYPE, 7)
#define LWIS_BUFFER_ALLOC _IOWR(LWIS_IOC_TYPE, 8, struct lwis_alloc_buffer_info)
#define LWIS_BUFFER_FREE _IOWR(LWIS_IOC_TYPE, 9, int)
#define LWIS_TIME_QUERY _IOWR(LWIS_IOC_TYPE, 10, int64_t)
#define LWIS_REG_IO _IOWR(LWIS_IOC_TYPE, 11, struct lwis_io_entries)
#define LWIS_ECHO _IOWR(LWIS_IOC_TYPE, 12, struct lwis_echo)

#define LWIS_EVENT_CONTROL_GET                                                 \
	_IOWR(LWIS_IOC_TYPE, 20, struct lwis_event_control)
#define LWIS_EVENT_CONTROL_SET                                                 \
	_IOW(LWIS_IOC_TYPE, 21, struct lwis_event_control)
#define LWIS_EVENT_DEQUEUE _IOWR(LWIS_IOC_TYPE, 22, struct lwis_event_info)
#define LWIS_EVENT_SUBSCRIBE                                                   \
	_IOW(LWIS_IOC_TYPE, 23, struct lwis_event_subscribe)
#define LWIS_EVENT_UNSUBSCRIBE _IOW(LWIS_IOC_TYPE, 24, int64_t)

#define LWIS_TRANSACTION_SUBMIT                                                \
	_IOWR(LWIS_IOC_TYPE, 30, struct lwis_transaction_info)
#define LWIS_TRANSACTION_CANCEL _IOWR(LWIS_IOC_TYPE, 31, int64_t)
#define LWIS_TRANSACTION_REPLACE                                               \
	_IOWR(LWIS_IOC_TYPE, 32, struct lwis_transaction_info)

#define LWIS_PERIODIC_IO_SUBMIT                                                \
	_IOWR(LWIS_IOC_TYPE, 40, struct lwis_periodic_io_info)
#define LWIS_PERIODIC_IO_CANCEL _IOWR(LWIS_IOC_TYPE, 41, int64_t)

#define LWIS_DPM_CLK_UPDATE                                                    \
	_IOW(LWIS_IOC_TYPE, 50, struct lwis_dpm_clk_settings)

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* LWIS_COMMANDS_H_ */
