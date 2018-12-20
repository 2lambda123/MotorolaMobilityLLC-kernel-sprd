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

#define MUSB_DMA_PAUSE	0x1000
#define MUSB_DMA_FRAG_WAIT	0x1004
#define MUSB_DMA_INTR_RAW_STATUS	0x1008
#define MUSB_DMA_INTR_MASK_STATUS	0x100C
#define MUSB_DMA_REQ_STATUS	0x1010
#define MUSB_DMA_EN_STATUS	0x1014
#define MUSB_DMA_DEBUG_STATUS	0x1018

#define MUSB_DMA_CHN_PAUSE(n)		(0x1c00 + (n - 1) * 0x20)
#define MUSB_DMA_CHN_CFG(n)		(0x1c04 + (n - 1) * 0x20)
#define MUSB_DMA_CHN_INTR(n)		(0x1c08 + (n - 1) * 0x20)
#define MUSB_DMA_CHN_ADDR(n)		(0x1c0c + (n - 1) * 0x20)
#define MUSB_DMA_CHN_LEN(n)		(0x1c10 + (n - 1) * 0x20)
#define MUSB_DMA_CHN_LLIST_PTR(n)	(0x1c14 + (n - 1) * 0x20)
#define MUSB_DMA_CHN_ADDR_H(n)		(0x1c18 + (n - 1) * 0x20)
#define MUSB_DMA_CHN_REQ(n)		(0x1c1c + (n - 1) * 0x20)

/*MUSB I2S control*/
#define MUSB_AUDIO_IIS_CTL0		0x1404
#define BIT_RTX_MD(x)			(((x) & 0x3) << 6)
#define BIT_NG_TX			BIT(1)
#define BIT_NG_RX			BIT(0)

#define MUSB_AUDIO_IIS_CLKM		0x1420
#define BIT_IIS_CLKM(x)		((x) & GENMASK(21, 0))

#define MUSB_AUDIO_IIS_CLKN		0x1424
#define BIT_IIS_CLKN(x)		((x) & GENMASK(21, 0))

#define MUSB_AUDIO_IIS_DMA_INS		0x1428
#define BIT_TX_FIFO_DEPTH(x)		(((x) & 0x3) << 30)
#define BIT_RX_FIFO_DEPTH(x)		(((x) & 0x3) << 28)
#define BIT_TX_ST_MO			BIT(23)
#define BIT_RX_ST_MO			BIT(22)
#define BIT_TX_LEFT_FIRST		BIT(21)
#define BIT_RX_LEFT_FIRST		BIT(20)
#define BIT_TX_SAMPLE_RATE(x)		(((x) & GENMASK(8, 0)) << 9)
#define BIT_RX_SAMPLE_RATE(x)		((x) & GENMASK(8, 0))

#define MUSB_AUDIO_IIS_DMA_CHN		0x142c
#define BIT_CHN_AUDIO_EN(x)		BIT(x)

#define MUSB_AUDIO_IIS_EN		0x1430
#define BIT_TX_EMPTY_INT_MSK		BIT(13)
#define BIT_RX_FULL_INT_MSK		BIT(12)
#define BIT_TX_EMPTY_INT_CLR		BIT(11)
#define BIT_RX_FULL_INT_CLR		BIT(10)
#define BIT_TX_EMPTY_INT_EN		BIT(9)
#define BIT_RX_FULL_INT_EN		BIT(8)
#define BIT_IIS_SAMPLE_DEPTH		BIT(7)
#define BIT_UNALIGN_OUT_EN		BIT(6)
#define BIT_UNALIGN_IN_EN		BIT(5)
#define BIT_IIS_HALT			BIT(4)
#define BIT_IIS_START			BIT(3)
#define BIT_EXT_IIS_MODE		BIT(2)
#define BIT_IIS_TO_TXF_EN		BIT(1)
#define BIT_IIS_FROM_RXF_EN		BIT(0)

/*
 * Usb audio clk_i2s is same as clk_utmi. clk_utmi is 30M when
 * configured 16bit data width and 60M when configured 8bit.
 */
#define MUSB_IIS_CLKN	30000

#define musb_read_dma_addr(mbase, bchannel)	\
	musb_readl(mbase,	\
		   MUSB_DMA_CHN_ADDR(bchannel))

#define musb_write_dma_addr(mbase, bchannel, addr) \
	musb_writel(mbase, \
		    MUSB_DMA_CHN_ADDR(bchannel), \
		    addr)

#define CHN_EN	BIT(0)
#define CHN_LLIST_INT_EN	BIT(2)
#define CHN_START_INT_EN	BIT(3)
#define CHN_USBRX_INT_EN	BIT(4)
#define CHN_CLEAR_INT_EN	BIT(5)

#define CHN_LLIST_INT_MASK_STATUS	BIT(18)
#define CHN_START_INT_MASK_STATUS	BIT(19)
#define CHN_USBRX_INT_MASK_STATUS	BIT(20)
#define CHN_CLEAR_INT_MASK_STATUS	BIT(21)

#define CHN_CLR	BIT(15)
#define CHN_CLR_STATUS		BIT(31)

#define CHN_FRAG_INT_CLR	BIT(24)
#define CHN_BLK_INT_CLR	BIT(25)
#define CHN_LLIST_INT_CLR	BIT(26)
#define CHN_START_INT_CLR	BIT(27)
#define CHN_USBRX_LAST_INT_CLR	BIT(28)

#define LISTNODE_NUM	2048
#define LISTNODE_MASK	(LISTNODE_NUM - 1)

#define MUSB_DMA_CHANNELS	31

enum {
	IIS_WIDTH_16BIT,
	IIS_WIDTH_24BIT,
	IIS_WIDTH_MAX,
};

#ifdef CONFIG_64BIT
#define ADDR_FLAG GENMASK(63, 28)
#else
#define ADDR_FLAG GENMASK(31, 28)
#endif /* CONFIG_64BIT */


struct sprd_musb_dma_controller;

struct linklist_node_s {
	u32	addr;
	u16	frag_len;
	u16	blk_len;
	u32	list_end :1;
	u32	sp :1;
	u32	ioc :1;
	u32	reserved:5;
	u32	data_addr :4;
	u32	pad :20;
#if IS_ENABLED(CONFIG_USB_SPRD_DMA_V3)
	u32	reserved1;
#endif
};

struct sprd_musb_dma_channel {
	struct dma_channel	channel;
	struct sprd_musb_dma_controller	*controller;
	struct linklist_node_s	*dma_linklist;
	dma_addr_t list_dma_addr;
	struct list_head	req_queued;
	u32	free_slot;
	u32	busy_slot;
	u32	node_num;
	u16	max_packet_sz;
	u8	channel_num;
	u8	transmit;
	u8	ep_num;
};

struct sprd_musb_dma_controller {
	struct dma_controller	controller;
	struct sprd_musb_dma_channel	channel[MUSB_DMA_CHANNELS];
	void	*private_data;
	void __iomem	*base;
	u32	used_channels;
	wait_queue_head_t	wait;
};

irqreturn_t sprd_dma_interrupt(struct musb *musb, u32 int_hsdma);
struct dma_controller *sprd_musb_dma_controller_create(struct musb *musb,
							void __iomem *base);
void sprd_musb_dma_controller_destroy(struct dma_controller *c);
