/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2021 Unisoc Technologies Co., Ltd.
 * <https://www.unisoc.com>
 *
 * Abstract: cfg80211 header.
 */

#ifndef __CFG80211_H_5G__
#define __CFG80211_H_5G__

static struct ieee80211_channel sprd_5ghz_channels[] = {
	CHAN5G(34, 0), CHAN5G(36, 0),
	CHAN5G(40, 0), CHAN5G(44, 0),
	CHAN5G(48, 0), CHAN5G(52, 0),
	CHAN5G(56, 0), CHAN5G(60, 0),
	CHAN5G(64, 0), CHAN5G(100, 0),
	CHAN5G(104, 0), CHAN5G(108, 0),
	CHAN5G(112, 0), CHAN5G(116, 0),
	CHAN5G(120, 0), CHAN5G(124, 0),
	CHAN5G(128, 0), CHAN5G(132, 0),
	CHAN5G(136, 0), CHAN5G(140, 0),
	CHAN5G(144, 0), CHAN5G(149, 0),
	CHAN5G(153, 0), CHAN5G(157, 0),
	CHAN5G(161, 0), CHAN5G(165, 0),
};

#endif
