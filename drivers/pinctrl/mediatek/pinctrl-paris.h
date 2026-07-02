/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Ingo Reitz <9l@9lo.re>
 *
 * Based on Linux pinctrl-paris:
 *   Copyright (C) 2018 MediaTek Inc.
 *   Author: Sean Wang <sean.wang@mediatek.com>
 *     Zhiyong Tao <zhiyong.tao@mediatek.com>
 *     Hongzhou.Yang <hongzhou.yang@mediatek.com>
 */
#ifndef __PINCTRL_PARIS_H
#define __PINCTRL_PARIS_H

#define MTK_RANGE(_a)		{ .range = (_a), .nranges = ARRAY_SIZE(_a), }

// KEPT FOR COMPAT!
#define MTK_EINT_FUNCTION(_eintmux, _eintnum)				\
	{							\
		.eint_m = _eintmux,					\
		.eint_n = _eintnum,					\
	}

#define MTK_FUNCTION(_val, _name)				\
	{							\
		.muxval = _val,					\
		.name = _name,					\
	}

#define MTK_PIN(_number, _name, _eint, _drv_n, ...) {	\
		.number = _number,			\
		.name = _name,				\
		.eint = _eint,				\
		.drv_n = _drv_n,			\
		.funcs = (struct mtk_func_desc[]){	\
			__VA_ARGS__, { } },				\
	}

#define MTK_EINT_PIN(_number, _instance, _index, _debounce) {	\
		.number = _number,				\
		.instance = _instance,				\
		.index = _index,				\
		.debounce = _debounce,				\
	}

#define PINCTRL_PIN_GROUP(_name_, id)							\
	{										\
		.grp = PINCTRL_PINGROUP(_name_,id##_pins, ARRAY_SIZE(id##_pins)),	\
		.data = id##_funcs,							\
	}

int mtk_paris_pinctrl_probe(struct device *dev);

#endif /* __PINCTRL_PARIS_H */
