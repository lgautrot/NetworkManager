/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright 2015 Red Hat, Inc.
 */

#ifndef NM_SETTING_TUN_H
#define NM_SETTING_TUN_H

#include "nm-setting.h"

G_BEGIN_DECLS

#define NM_TYPE_SETTING_TUN            (nm_setting_tun_get_type ())
#define NM_SETTING_TUN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_SETTING_TUN, NMSettingTun))
#define NM_SETTING_TUN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_SETTING_TUNCONFIG, NMSettingTunClass))
#define NM_IS_SETTING_TUN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_SETTING_TUN))
#define NM_IS_SETTING_TUN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_SETTING_TUN))
#define NM_SETTING_TUN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_SETTING_TUN, NMSettingTunClass))

#define NM_SETTING_TUN_SETTING_NAME "tun"

typedef struct {
	NMSetting parent;
} NMSettingTun;

typedef struct {
	NMSettingClass parent;

	/* Padding for future expansion */
	void (*_reserved1) (void);
	void (*_reserved2) (void);
	void (*_reserved3) (void);
	void (*_reserved4) (void);
} NMSettingTunClass;

NM_DEPRECATED_IN_1_2
GType nm_setting_tun_get_type (void);

G_END_DECLS

#endif /* NM_SETTING_TUN_H */
