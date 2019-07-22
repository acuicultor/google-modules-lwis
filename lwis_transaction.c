/*
 * Google LWIS Transaction Processor
 *
 * Copyright (c) 2019 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-transact: " fmt

#include "lwis_transaction.h"

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "lwis_event.h"
#include "lwis_util.h"

static struct lwis_transaction_event_list *
event_list_find(struct lwis_client *client, int64_t event_id)
{
	struct lwis_transaction_event_list *list;
	hash_for_each_possible(client->transaction_list, list, node, event_id)
	{
		if (list->event_id == event_id) {
			return list;
		}
	}
	return NULL;
}

static struct lwis_transaction_event_list *
event_list_create(struct lwis_client *client, int64_t event_id)
{
	struct lwis_transaction_event_list *event_list =
		kzalloc(sizeof(struct lwis_transaction_event_list), GFP_KERNEL);
	if (!event_list) {
		pr_err("Cannot allocate new event list\n");
		return NULL;
	}
	event_list->event_id = event_id;
	INIT_LIST_HEAD(&event_list->list);
	hash_add(client->transaction_list, &event_list->node, event_id);
	return event_list;
}

static struct lwis_transaction_event_list *
event_list_find_or_create(struct lwis_client *client, int64_t event_id)
{
	struct lwis_transaction_event_list *list =
		event_list_find(client, event_id);
	return (list == NULL) ? event_list_create(client, event_id) : list;
}

static int process_io_entries(struct lwis_client *client,
			      struct lwis_transaction *transaction,
			      struct list_head *list_node,
			      struct list_head *pending_events)
{
	int i;
	int ret = 0;
	struct lwis_io_entry *entry;
	int read_idx;
	struct lwis_device *lwis_dev = client->lwis_dev;
	struct lwis_transaction_info *info = &transaction->info;
	struct lwis_transaction_response_header *resp = transaction->resp;
	size_t resp_size;
	struct lwis_io_result *resp_buf;
	uint64_t value = 0;

	resp_size = sizeof(struct lwis_transaction_response_header) +
		    resp->num_entries * sizeof(struct lwis_io_result);
	resp_buf = (struct lwis_io_result
			    *)((uint8_t *)resp +
			       sizeof(struct lwis_transaction_response_header));

	for (i = 0, read_idx = 0; i < info->num_io_entries; ++i) {
		entry = &info->io_entries[i];
		if (entry->type == LWIS_IO_ENTRY_WRITE) {
			ret = lwis_device_single_register_write(
				lwis_dev, false, entry->bid, entry->offset,
				entry->val);
			if (ret) {
				resp->error_code = ret;
				goto event_push;
			}
		} else if (entry->type == LWIS_IO_ENTRY_READ) {
			resp_buf[read_idx].bid = entry->bid;
			resp_buf[read_idx].offset = entry->offset;
			ret = lwis_device_single_register_read(
				lwis_dev, false, entry->bid, entry->offset,
				&resp_buf[read_idx].value);
			read_idx++;
			if (ret) {
				resp->error_code = ret;
				goto event_push;
			}
		} else if (entry->type == LWIS_IO_ENTRY_MODIFY) {
			ret = lwis_device_single_register_read(
				lwis_dev, false, entry->bid, entry->offset,
				&value);
			if (ret) {
				resp->error_code = ret;
				goto event_push;
			}
			value &= ~entry->val_mask;
			value |= entry->val_mask & entry->val;
			ret = lwis_device_single_register_write(
				lwis_dev, false, entry->bid, entry->offset,
				value);
			if (ret) {
				resp->error_code = ret;
				goto event_push;
			}
		}
	}

event_push:
	lwis_pending_event_push(pending_events,
				resp->error_code ? info->emit_error_event_id
						 : info->emit_success_event_id,
				(void *)resp, resp_size);
	list_del(list_node);
	kfree(resp);
	kfree(info->io_entries);
	kfree(transaction);
	return ret;
}

static void transaction_work_func(struct work_struct *work)
{
	unsigned long flags;
	struct lwis_transaction *transaction;
	struct list_head *it_tran, *it_tran_tmp;
	struct lwis_client *client =
		container_of(work, struct lwis_client, transaction_work);
	struct list_head pending_events;

	INIT_LIST_HEAD(&pending_events);

	spin_lock_irqsave(&client->transaction_lock, flags);

	list_for_each_safe(it_tran, it_tran_tmp,
			   &client->transaction_process_queue)
	{
		transaction = list_entry(it_tran, struct lwis_transaction,
					 process_queue_node);
		process_io_entries(client, transaction,
				   &transaction->process_queue_node,
				   &pending_events);
	}

	spin_unlock_irqrestore(&client->transaction_lock, flags);

	lwis_pending_events_emit(client->lwis_dev, &pending_events);
}

int lwis_transaction_init(struct lwis_client *client)
{
	spin_lock_init(&client->transaction_lock);
	INIT_LIST_HEAD(&client->transaction_process_queue);
	client->transaction_wq = create_workqueue("lwistran");
	INIT_WORK(&client->transaction_work, transaction_work_func);
	client->transaction_counter = 0;
	hash_init(client->transaction_list);
	return 0;
}

int lwis_transaction_cleanup(struct lwis_client *client)
{
	// To be implemented.
	return 0;
}

int lwis_transaction_submit(struct lwis_client *client,
			    struct lwis_transaction *transaction)
{
	struct lwis_transaction_event_list *event_list;
	struct lwis_transaction_info *info = &transaction->info;
	struct lwis_io_entry *entry;
	int i;
	size_t resp_size;
	int read_entries = 0;
	unsigned long flags;

	BUG_ON(!client);
	BUG_ON(!transaction);

	info->id = client->transaction_counter;

	for (i = 0; i < info->num_io_entries; ++i) {
		entry = &info->io_entries[i];
		if (entry->type == LWIS_IO_ENTRY_READ) {
			read_entries++;
		}
	}

	// Event response payload consists of header, and address and
	// offset pairs.
	resp_size = sizeof(struct lwis_transaction_response_header) +
		    read_entries * sizeof(struct lwis_io_result);
	transaction->resp = kzalloc(resp_size, GFP_KERNEL);
	if (!transaction->resp) {
		pr_err("Cannot allocate transaction response\n");
		return -ENOMEM;
	}
	transaction->resp->id = info->id;
	transaction->resp->error_code = 0;
	transaction->resp->num_entries = read_entries;

	spin_lock_irqsave(&client->transaction_lock, flags);
	if (info->trigger_event_id == LWIS_EVENT_ID_NONE) {
		/* Immediate trigger. */
		list_add_tail(&transaction->process_queue_node,
			      &client->transaction_process_queue);
		spin_unlock_irqrestore(&client->transaction_lock, flags);
		queue_work(client->transaction_wq, &client->transaction_work);
	} else {
		/* Trigger by event. */
		event_list = event_list_find_or_create(client,
						       info->trigger_event_id);
		if (!event_list) {
			pr_err("Cannot create transaction event list\n");
			kfree(transaction->resp);
			spin_unlock_irqrestore(&client->transaction_lock,
					       flags);
			return -EINVAL;
		}
		list_add_tail(&transaction->event_list_node, &event_list->list);
		spin_unlock_irqrestore(&client->transaction_lock, flags);
	}

	client->transaction_counter++;
	return 0;
}

static void process_transaction(struct lwis_client *client,
				struct lwis_transaction *transaction,
				uint64_t current_event_counter,
				struct list_head *pending_events)
{
	uint64_t trigger_counter = transaction->info.trigger_event_counter;

	if (trigger_counter == LWIS_EVENT_COUNTER_ON_NEXT_OCCURRENCE ||
	    trigger_counter == current_event_counter) {
		if (transaction->info.run_in_event_context) {
			process_io_entries(client, transaction,
					   &transaction->event_list_node,
					   pending_events);

		} else {
			list_add_tail(&transaction->process_queue_node,
				      &client->transaction_process_queue);
			list_del(&transaction->event_list_node);
		}

	} else if (trigger_counter < current_event_counter) {
		// TODO: Cancels the transaction.
		list_del(&transaction->event_list_node);
		kfree(transaction);
	}
}

int lwis_transaction_event_trigger(struct lwis_client *client, int64_t event_id,
				   uint64_t event_counter,
				   struct list_head *pending_events)
{
	unsigned long flags;
	struct lwis_transaction_event_list *event_list;
	struct list_head *it_tran, *it_tran_tmp;
	struct lwis_transaction *transaction;

	/* Find event list that matches the trigger event ID. */
	spin_lock_irqsave(&client->transaction_lock, flags);
	event_list = event_list_find(client, event_id);
	/* No event found, just return. */
	if (event_list == NULL || list_empty(&event_list->list)) {
		spin_unlock_irqrestore(&client->transaction_lock, flags);
		return 0;
	}

	/* Go through all transactions under the chosen event list. */
	list_for_each_safe(it_tran, it_tran_tmp, &event_list->list)
	{
		transaction = list_entry(it_tran, struct lwis_transaction,
					 event_list_node);
		process_transaction(client, transaction, event_counter,
				    pending_events);
	}

	/* Schedule deferred transactions */
	if (!list_empty(&client->transaction_process_queue)) {
		queue_work(client->transaction_wq, &client->transaction_work);
	}

	spin_unlock_irqrestore(&client->transaction_lock, flags);

	return 0;
}
