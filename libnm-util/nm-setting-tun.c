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

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <dbus/dbus-glib.h>

#include "nm-setting-tun.h"
#include "nm-dbus-glib-types.h"
#include "nm-setting-private.h"

/**
 * SECTION:nm-setting-tun
 * @short_description: Describes connection properties for TUN/TAP interfaces
 * @include: nm-setting-tun.h
 *
 * The #NMSettingTun object is a #NMSetting subclass that describes properties
 * necessary for connection to TUN interfaces.
 *
 * Deprecated: 1.2
 **/

static GQuark
nm_setting_tun_error_quark (void)
{
	static GQuark quark;

	if (G_UNLIKELY (!quark))
		quark = g_quark_from_static_string ("nm-setting-tun-error-quark");
	return quark;
}

G_DEFINE_TYPE_WITH_CODE (NMSettingTun, nm_setting_tun, NM_TYPE_SETTING,
                         _nm_register_setting (NM_SETTING_TUN_SETTING_NAME,
                                               g_define_type_id,
                                               1,
                                               nm_setting_tun_error_quark ()))
NM_SETTING_REGISTER_TYPE (NM_TYPE_SETTING_TUN)

static void
nm_setting_tun_init (NMSettingTun *setting)
{
}

static void
nm_setting_tun_class_init (NMSettingTunClass *setting_class)
{
}
