/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2021 Unisoc Technologies Co., Ltd.
 * <https://www.unisoc.com>
 */

#ifndef __SDIO_H__
#define __SDIO_H__

#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <misc/wcn_bus.h>

#include "common/hif.h"

#define SDIO_RX_CMD_PORT	22
#define SDIO_RX_PKT_LOG_PORT	23
/*use port 24 because fifo_len = 8*/
#define SDIO_RX_DATA_PORT	24
#define SDIO_TX_CMD_PORT	8
/*use port 10 because fifo_len = 8*/
#define SDIO_TX_DATA_PORT	10

#define MSDU_DSCR_RSVD		5

#define DEL_LUT_INDEX		0
#define ADD_LUT_INDEX		1
#define UPD_LUT_INDEX		2
#define SPRD_TX_MSG_CMD_NUM	128
#define SPRD_TX_QOS_POOL_SIZE	20000
#define SPRD_TX_DATA_START_NUM	(SPRD_TX_QOS_POOL_SIZE - 3)
#define SPRD_RX_MSG_NUM		20000

/* tx len less than cp len 4 byte as sdiom 4 bytes align */
/* set MAX CMD length to 1600 on firmware side*/
#define SPRD_MAX_CMD_TXLEN	1596
#define SPRD_MAX_CMD_RXLEN	1092
#define SPRD_MAX_DATA_TXLEN	1672
#define SPRD_MAX_DATA_RXLEN	1676

#define SAVE_ADDR(data, buf, len)	memcpy((data - len), &buf, len)
#define RESTORE_ADDR(buf, data, len)	memcpy(&buf, (data - len), len)
#define CLEAR_ADDR(data, len)		memset((data - len), 0x0, len)
#define HIGHER_DDR_PRIORITY	0xAA

#define SDIO_HEX_DUMP_BUF_SIZE	255

struct sc2355_sdiohal_puh {
	unsigned int pad:6;
	unsigned int check_sum:1;
	unsigned int len:16;
	unsigned int eof:1;
	unsigned int subtype:4;
	unsigned int type:4;
};/* 32bits public header */

unsigned short sc2355_get_data_csum(void *entry, void *data);
int sc2355_tx_cmd_pop_list(int channel, struct mbuf_t *head,
			   struct mbuf_t *tail, int num);
int sc2355_tx_data_pop_list(int channel, struct mbuf_t *head,
			    struct mbuf_t *tail, int num);
int sc2355_tx_cmd(struct sprd_hif *hif, unsigned char *data, int len);
int sc2355_tx_addr_trans(struct sprd_hif *hif, unsigned char *data, int len);
int sc2355_hif_tx_list(struct sprd_hif *hif,
		       struct list_head *tx_list,
		       struct list_head *tx_list_head,
		       int tx_count, int ac_index, u8 coex_bt_on);
int sc2355_hif_fill_msdu_dscr(struct sprd_vif *vif,
			      struct sk_buff *skb, u8 type, u8 offset);
void *sc2355_get_rx_data(struct sprd_hif *hif, void *pos, void **data,
			 void **tran_data, int *len, int offset);
void sc2355_free_rx_data(struct sprd_hif *hif,
			 int chn, void *head, void *tail, int num);

void sc2355_hex_dump(unsigned char *name,
		     unsigned char *data, unsigned short len);
struct sprd_peer_entry
*sc2355_find_peer_entry_using_lut_index(struct sprd_hif *hif,
					unsigned char sta_lut_index);
void sc2355_event_sta_lut(struct sprd_vif *vif, u8 *data, u16 len);
struct sprd_peer_entry
*sc2355_find_peer_entry_using_addr(struct sprd_vif *vif, u8 *addr);
void sc2355_tx_addba(struct sprd_hif *hif,
		     struct sprd_peer_entry *peer_entry, unsigned char tid);
void sc2355_tx_delba(struct sprd_hif *hif,
		     struct sprd_peer_entry *peer_entry, unsigned int ac_index);
void sc2355_tx_send_addba(struct sprd_vif *vif, void *data, int len);
void sc2355_tx_send_delba(struct sprd_vif *vif, void *data, int len);
unsigned char sc2355_find_lut_index(struct sprd_hif *hif, struct sprd_vif *vif);
int sc2355_dis_flush_txlist(struct sprd_hif *hif, u8 lut_index);
void sc2355_handle_pop_list(void *data);
int sc2355_add_topop_list(int chn, struct mbuf_t *head,
			  struct mbuf_t *tail, int num);
void sc2355_set_coex_bt_on_off(u8 action);
int sc2355_tx_data_pop_list(int channel, struct mbuf_t *head,
			    struct mbuf_t *tail, int num);
int sc2355_push_link(struct sprd_hif *hif, int chn,
		     struct mbuf_t *head, struct mbuf_t *tail, int num,
		     int (*pop)(int, struct mbuf_t *, struct mbuf_t *, int));
enum sprd_hif_type get_hwintf_type(void);
void sc2355_tx_addr_trans_free(struct sprd_hif *hif);
void sc2355_count_rx_tp(struct sprd_hif *hif, int len);
void sc2355_add_to_free_list(struct sprd_priv *priv,
			     struct list_head *tx_list_head, int tx_count);
struct sprd_hif *sc2355_get_hif(void);

#define sc2355_tx_free_pcie_data_num(hif, data)			do {} while (0)
#define sc2355_tx_free_pcie_data(priv, data)			do {} while (0)
#define sc2355_tx_addr_trans_pcie(hif, data, len, send_now)	do {} while (0)

#endif /* __SDIO_H__ */
