/*
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/ipv6.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/if_arp.h>
#include <asm/byteorder.h>
#include <linux/tty.h>
#include <linux/platform_device.h>
#include <uapi/linux/sched/types.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/of_device.h>
#include <linux/sipa.h>
#include "sipa_priv.h"
#include "sipa_hal.h"

#define SIPA_RECV_BUF_LEN     1600
#define SIPA_RECV_RSVD_LEN     64

#define SIPA_RECV_EVT (SIPA_HAL_INTR_BIT | \
			SIPA_HAL_TX_FIFO_THRESHOLD_SW | SIPA_HAL_DELAY_TIMER)

int put_recv_array_node(struct sipa_skb_array *p,
						struct sk_buff *skb, dma_addr_t dma_addr)
{
	u32 pos;

	if ((p->wp - p->rp) < p->depth) {
		pos = p->wp & (p->depth -1);
		p->array[pos].skb = skb;
		p->array[pos].dma_addr = dma_addr;
		p->wp++;
		return 0;
	} else {
		return -1;
	}
}

int get_recv_array_node(struct sipa_skb_array *p,
						struct sk_buff **skb, dma_addr_t *dma_addr)
{
	u32 pos;

	if (p->rp != p->wp) {
		pos = p->rp & (p->depth -1);
		*skb = p->array[pos].skb;
		*dma_addr = p->array[pos].dma_addr;
		p->rp++;
		return 0;
	} else {
		return  -1;
	}
}

struct sk_buff *alloc_recv_skb(u32 req_len, u8 rsvd)
{
	struct sk_buff *skb;

	skb = __dev_alloc_skb(req_len + rsvd, GFP_KERNEL | GFP_DMA);
	if (!skb) {
		pr_err("failed to alloc skb!\n");
		return NULL;
	}

	/* save skb ptr to skb->data */
	skb_reserve(skb, rsvd);

	return skb;
}

void fill_free_fifo(struct sipa_skb_receiver *receiver, u32 cnt)
{
	struct sk_buff *skb;
	u32 fail_cnt = 0;
	int i;
	u32 success_cnt = 0;
	struct sipa_hal_fifo_item item;
	dma_addr_t dma_addr;

	for (i = 0; i < cnt; i++) {
		skb = alloc_recv_skb(SIPA_RECV_BUF_LEN, receiver->rsvd);
		if (skb) {
			unsigned long flags;

			dma_addr = dma_map_single(receiver->ctx->pdev,
									  skb_put(skb, SIPA_RECV_BUF_LEN + receiver->rsvd),
									  SIPA_RECV_BUF_LEN + receiver->rsvd,
									  DMA_FROM_DEVICE);

			spin_lock_irqsave(&receiver->lock, flags);

			put_recv_array_node(&receiver->recv_array,
								skb, dma_addr);

			item.addr = dma_addr;
			item.len = skb->len - SIPA_DEF_OFFSET;
			item.offset = SIPA_DEF_OFFSET;
			item.dst = receiver->ep->recv_fifo.dst_id;
			item.src = receiver->ep->recv_fifo.src_id;
			item.intr = 0;
			item.netid = 0;
			item.err_code = 0;
			sipa_hal_put_rx_fifo_item(receiver->ctx->hdl,
									  receiver->ep->recv_fifo.idx,
									  &item);
			spin_unlock_irqrestore(&receiver->lock, flags);
			success_cnt++;
		} else {
			fail_cnt++;
		}
	}
}

void sipa_receiver_notify_cb(void *priv, enum sipa_hal_evt_type evt,
							 unsigned long data)
{
	struct sipa_skb_receiver *receiver = (struct sipa_skb_receiver *)priv;

	if (evt & SIPA_RECV_EVT)
		wake_up(&receiver->recv_waitq);
}

static void trigger_nics_recv(struct sipa_skb_receiver *receiver)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&receiver->lock, flags);
	for (i = 0; i < receiver->nic_cnt; i++)
		sipa_nic_try_notify_recv(receiver->nic_array[i]);
	spin_unlock_irqrestore(&receiver->lock, flags);
}

static int dispath_to_nic(struct sipa_skb_receiver *receiver,
						  struct sipa_hal_fifo_item *item,
						  struct sk_buff *skb)
{
	u32 i;
	unsigned long flags;
	struct sipa_nic *nic;
	struct sipa_nic *dst_nic = NULL;

	spin_lock_irqsave(&receiver->lock, flags);

	for (i = 0; i < receiver->nic_cnt; i++) {
		nic = receiver->nic_array[i];
		if (nic->src_mask | item->src) {
			if ((nic->netid == -1) ||
				(nic->netid == item->netid)) {
				dst_nic = nic;
				break;
			}
		}
	}

	spin_unlock_irqrestore(&receiver->lock, flags);

	if (dst_nic) {
		sipa_nic_push_skb(dst_nic, skb);
	} else {
		pr_err("dispath_to_nic src:0x%x, netid:%d no nic matched\n",
			   item->src, item->netid);
		dev_kfree_skb_any(skb);
	}

	return 0;
}

static int do_recv(struct sipa_skb_receiver *receiver)
{
	int ret;
	dma_addr_t addr;
	struct sk_buff *recv_skb = NULL;
	struct sipa_hal_fifo_item item;

	ret = sipa_hal_get_tx_fifo_item(receiver->ctx->hdl,
									receiver->ep->recv_fifo.idx,
									&item);
	if (ret)
		return ret;

	ret = get_recv_array_node(&receiver->recv_array, &recv_skb, &addr);
	if (ret) {
		pr_err("do_recv recv addr:0x%llx, butrecv_array is empty\n",
			   item.addr);
	} else if (addr != item.addr) {
		pr_err("do_recv recv addr:0x%llx, but recv_array addr:0x%llx not equal\n",
			   item.addr, addr);
	}

	recv_skb->data_len = item.len;
	dispath_to_nic(receiver, &item, recv_skb);

	return 0;
}

static int recv_thread(void *data)
{
	struct sipa_skb_receiver *receiver = (struct sipa_skb_receiver *)data;
	struct sched_param param = {.sched_priority = 90};

	/*set the thread as a real time thread, and its priority is 90*/
	sched_setscheduler(current, SCHED_RR, &param);

	while (!kthread_should_stop()) {
		u32 recv_cnt = 0;
#if 1
		pr_info("fifo(%d) empty status is %d\n",
				receiver->ep->recv_fifo.idx,
				sipa_hal_is_tx_fifo_empty(receiver->ctx->hdl,
										  receiver->ep->recv_fifo.idx));
#endif
		wait_event_interruptible(receiver->recv_waitq,
								 !sipa_hal_is_tx_fifo_empty(receiver->ctx->hdl,
										 receiver->ep->recv_fifo.idx));

		while (!do_recv(receiver))
			recv_cnt++;

		if (recv_cnt)
			fill_free_fifo(receiver, recv_cnt);

		trigger_nics_recv(receiver);
	}

	return 0;
}

void sipa_receiver_init(struct sipa_skb_receiver *receiver, u32 rsvd)
{
	u32 depth;
	struct sipa_comm_fifo_params attr;

	attr.tx_intr_delay_us = 0;
	attr.tx_intr_threshold = 32;
	attr.flowctrl_in_tx_full = false;
	attr.flow_ctrl_cfg = flow_ctrl_rx_empty;
	attr.flow_ctrl_irq_mode = enter_exit_flow_ctrl;
	attr.rx_enter_flowctrl_watermark = receiver->ep
									   ->recv_fifo.rx_fifo.fifo_depth / 4;
	attr.rx_leave_flowctrl_watermark = receiver->ep
									   ->recv_fifo.rx_fifo.fifo_depth / 2;
	attr.tx_enter_flowctrl_watermark = 0;
	attr.tx_leave_flowctrl_watermark = 0;

	pr_info("ep_id = %d fifo_id = %d rx_fifo depth = 0x%x\n",
			receiver->ep->id,
			receiver->ep->recv_fifo.idx,
			receiver->ep->recv_fifo.rx_fifo.fifo_depth);
	pr_info("recv status is %d\n",
			receiver->ep->recv_fifo.is_receiver);
	sipa_open_common_fifo(receiver->ctx->hdl,
						  receiver->ep->recv_fifo.idx,
						  &attr,
						  sipa_receiver_notify_cb, receiver);

	/* reserve space for dma flushing cache issue */
	receiver->rsvd = rsvd;

	depth = receiver->ep->recv_fifo.tx_fifo.fifo_depth;

	fill_free_fifo(receiver, depth);
}

void sipa_receiver_add_nic(struct sipa_skb_receiver *receiver,
						   struct sipa_nic *nic)
{
	unsigned long flags;

	spin_lock_irqsave(&receiver->lock, flags);
	if (receiver->nic_cnt < SIPA_NIC_MAX)
		receiver->nic_array[receiver->nic_cnt++] = nic;
	spin_unlock_irqrestore(&receiver->lock, flags);
}
EXPORT_SYMBOL(sipa_receiver_add_nic);


int create_recv_array(struct sipa_skb_array *p, u32 depth)
{
	p->array = kzalloc(sizeof(struct sipa_skb_dma_addr_pair) * depth,
					   GFP_KERNEL);
	if (!p->array)
		return -ENOMEM;
	p->rp = 0;
	p->wp = 0;
	p->depth = depth;

	return 0;
}

void destroy_recv_array(struct sipa_skb_array *p)
{
	if (p->array)
		kfree(p->array);

	p->array = NULL;
	p->rp = 0;
	p->wp = 0;
	p->depth = 0;
}

int create_sipa_skb_receiver(struct sipa_context *ipa,
							 struct sipa_endpoint *ep,
							 struct sipa_skb_receiver **receiver_pp)
{
	int ret;
	struct sipa_skb_receiver *receiver = NULL;

	pr_info("%s ep->id = %d start\n", __func__, ep->id);
	receiver = kzalloc(sizeof(struct sipa_skb_receiver), GFP_KERNEL);
	if (!receiver) {
		pr_err("create_sipa_sipa_receiver: kzalloc err.\n");
		return -ENOMEM;
	}

	receiver->ctx = ipa;
	receiver->ep = ep;
	receiver->rsvd = SIPA_RECV_RSVD_LEN;

	ret = create_recv_array(&receiver->recv_array,
							receiver->ep->recv_fifo.rx_fifo.fifo_depth);
	if (ret) {
		pr_err("create_sipa_sipa_receiver: recv_array kzalloc err.\n");
		kfree(receiver);
		return -ENOMEM;
	}

	spin_lock_init(&receiver->lock);
	init_waitqueue_head(&receiver->recv_waitq);

	sipa_receiver_init(receiver, SIPA_RECV_RSVD_LEN);
	/* create sender thread */
	receiver->thread = kthread_create(recv_thread, receiver,
									  "sipa-recv-%d", ep->id);
	if (IS_ERR(receiver->thread)) {
		pr_err("Failed to create kthread: ipa-recv-%d\n",
			   ep->id);
		ret = PTR_ERR(receiver->thread);
		return ret;
	}
	wake_up_process(receiver->thread);

	*receiver_pp = receiver;
	return 0;
}
EXPORT_SYMBOL(create_sipa_skb_receiver);

void destroy_sipa_skb_receiver(struct sipa_skb_receiver *receiver)
{
	if (receiver->recv_array.array)
		destroy_recv_array(&receiver->recv_array);

	kfree(receiver);
}
EXPORT_SYMBOL(destroy_sipa_skb_receiver);
