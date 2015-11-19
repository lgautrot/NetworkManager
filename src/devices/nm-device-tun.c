/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright 2013 - 2015 Red Hat, Inc.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "nm-device-tun.h"
#include "nm-device-private.h"
#include "nm-default.h"
#include "nm-platform.h"
#include "nm-device-factory.h"
#include "nm-setting-tun.h"
#include "nm-core-internal.h"

#include "nmdbus-device-tun.h"

#include "nm-device-logging.h"
_LOG_DECLARE_SELF(NMDeviceTun);

G_DEFINE_TYPE (NMDeviceTun, nm_device_tun, NM_TYPE_DEVICE)

#define NM_DEVICE_TUN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE_TUN, NMDeviceTunPrivate))

typedef struct {
	NMPlatformTunProperties props;
	const char *mode;
} NMDeviceTunPrivate;

enum {
	PROP_0,
	PROP_OWNER,
	PROP_GROUP,
	PROP_FLAGS,
	PROP_MODE,
	PROP_NO_PI,
	PROP_VNET_HDR,
	PROP_MULTI_QUEUE,

	LAST_PROP
};

static void
reload_tun_properties (NMDeviceTun *self)
{
	NMDeviceTunPrivate *priv = NM_DEVICE_TUN_GET_PRIVATE (self);
	GObject *object = G_OBJECT (self);
	NMPlatformTunProperties props;

	if (!nm_platform_tun_get_properties (NM_PLATFORM_GET, nm_device_get_ifindex (NM_DEVICE (self)), &props)) {
		_LOGD (LOGD_HW, "could not read tun properties");
		return;
	}

	g_object_freeze_notify (object);

	if (priv->props.owner != props.owner)
		g_object_notify (object, NM_DEVICE_TUN_OWNER);
	if (priv->props.group != props.group)
		g_object_notify (object, NM_DEVICE_TUN_GROUP);
	if (priv->props.no_pi != props.no_pi)
		g_object_notify (object, NM_DEVICE_TUN_NO_PI);
	if (priv->props.vnet_hdr != props.vnet_hdr)
		g_object_notify (object, NM_DEVICE_TUN_VNET_HDR);
	if (priv->props.multi_queue != props.multi_queue)
		g_object_notify (object, NM_DEVICE_TUN_MULTI_QUEUE);

	priv->mode = props.mode;
	memcpy (&priv->props, &props, sizeof (NMPlatformTunProperties));

	g_object_thaw_notify (object);
}

static void
link_changed (NMDevice *device, NMPlatformLink *info)
{
	NM_DEVICE_CLASS (nm_device_tun_parent_class)->link_changed (device, info);

	reload_tun_properties (NM_DEVICE_TUN (device));
}

static gboolean
complete_connection (NMDevice *device,
                     NMConnection *connection,
                     const char *specific_object,
                     const GSList *existing_connections,
                     GError **error)
{
	NMSettingTun *s_tun;

	nm_utils_complete_generic (connection,
	                           NM_SETTING_TUN_SETTING_NAME,
	                           existing_connections,
	                           NULL,
	                           _("TUN connection"),
	                           NULL,
	                           TRUE);

	s_tun = nm_connection_get_setting_tun (connection);
	if (!s_tun) {
		g_set_error_literal (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INVALID_CONNECTION,
		                     "A 'tun' setting is required.");
		return FALSE;
	}

	return TRUE;
}

static int
tun_mode_from_string (const char *string)
{
	if (!g_strcmp0 (string, "tap"))
		return NM_SETTING_TUN_MODE_TAP;
	else
		return NM_SETTING_TUN_MODE_TUN;
}

static void
update_connection (NMDevice *device, NMConnection *connection)
{
	NMDeviceTun *self = NM_DEVICE_TUN (device);
	NMSettingTun *s_tun = nm_connection_get_setting_tun (connection);
	NMPlatformTunProperties props;
	NMSettingTunMode mode;
	gint64 user, group;
	char *str;

	if (!s_tun) {
		s_tun = (NMSettingTun *) nm_setting_tun_new ();
		nm_connection_add_setting (connection, (NMSetting *) s_tun);
	}

	if (!nm_platform_tun_get_properties (NM_PLATFORM_GET, nm_device_get_ifindex (device), &props)) {
		_LOGW (LOGD_HW, "failed to get TUN interface info while updating connection.");
		return;
	}

	mode = tun_mode_from_string (props.mode);

	if (mode != nm_setting_tun_get_mode (s_tun))
		g_object_set (G_OBJECT (s_tun), NM_SETTING_TUN_MODE, mode, NULL);

	user = _nm_utils_ascii_str_to_int64 (nm_setting_tun_get_owner (s_tun), 10, 0, G_MAXINT32, -1);
	group = _nm_utils_ascii_str_to_int64 (nm_setting_tun_get_group (s_tun), 10, 0, G_MAXINT32, -1);

	if (props.owner != user) {
		str = props.owner >= 0 ? g_strdup_printf ("%" G_GINT32_FORMAT, (gint32) props.owner) : NULL;
		g_object_set (G_OBJECT (s_tun), NM_SETTING_TUN_OWNER, str, NULL);
		g_free (str);
	}

	if (props.group != group) {
		str = props.group >= 0 ? g_strdup_printf ("%" G_GINT32_FORMAT, (gint32) props.group) : NULL;
		g_object_set (G_OBJECT (s_tun), NM_SETTING_TUN_GROUP, str, NULL);
		g_free (str);
	}

	if ((!props.no_pi) != nm_setting_tun_get_pi (s_tun))
		g_object_set (G_OBJECT (s_tun), NM_SETTING_TUN_PI, !props.no_pi, NULL);
	if (props.vnet_hdr != nm_setting_tun_get_vnet_hdr (s_tun))
		g_object_set (G_OBJECT (s_tun), NM_SETTING_TUN_VNET_HDR, props.vnet_hdr, NULL);
	if (props.multi_queue != nm_setting_tun_get_multi_queue (s_tun))
		g_object_set (G_OBJECT (s_tun), NM_SETTING_TUN_MULTI_QUEUE, props.multi_queue, NULL);
}

static gboolean
create_and_realize (NMDevice *device,
                    NMConnection *connection,
                    NMDevice *parent,
                    NMPlatformLink *out_plink,
                    GError **error)
{
	const char *iface = nm_device_get_iface (device);
	NMPlatformError plerr;
	NMSettingTun *s_tun;
	gint64 user, group;

	s_tun = nm_connection_get_setting_tun (connection);
	g_assert (s_tun);
	g_assert (out_plink);

	user = _nm_utils_ascii_str_to_int64 (nm_setting_tun_get_owner (s_tun), 10, 0, G_MAXINT32, -1);
	group = _nm_utils_ascii_str_to_int64 (nm_setting_tun_get_group (s_tun), 10, 0, G_MAXINT32, -1);

	plerr = nm_platform_tun_add (NM_PLATFORM_GET, iface,
	                             nm_setting_tun_get_mode (s_tun) == NM_SETTING_TUN_MODE_TAP,
	                             user, group,
	                             nm_setting_tun_get_pi (s_tun),
	                             nm_setting_tun_get_vnet_hdr (s_tun),
	                             nm_setting_tun_get_multi_queue (s_tun),
	                             out_plink);
	if (plerr != NM_PLATFORM_ERROR_SUCCESS && plerr != NM_PLATFORM_ERROR_EXISTS) {
		g_set_error (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_CREATION_FAILED,
		             "Failed to create TUN/TAP interface '%s' for '%s': %s",
		             iface,
		             nm_connection_get_id (connection),
		             nm_platform_error_to_string (plerr));
		return FALSE;
	}

	return TRUE;
}

static gboolean
realize (NMDevice *device, NMPlatformLink *plink, GError **error)
{
	reload_tun_properties (NM_DEVICE_TUN (device));
	return TRUE;
}

static gboolean
check_connection_compatible (NMDevice *device, NMConnection *connection)
{
	NMDeviceTun *self = NM_DEVICE_TUN (device);
	NMDeviceTunPrivate *priv = NM_DEVICE_TUN_GET_PRIVATE (self);
	NMSettingTunMode mode;
	NMSettingTun *s_tun;
	gint64 user, group;

	reload_tun_properties (self);

	if (!NM_DEVICE_CLASS (nm_device_tun_parent_class)->check_connection_compatible (device, connection))
		return FALSE;

	s_tun = nm_connection_get_setting_tun (connection);
	if (!s_tun)
		return FALSE;

	mode = tun_mode_from_string (priv->mode);
	if (mode != nm_setting_tun_get_mode (s_tun))
		return FALSE;

	user = _nm_utils_ascii_str_to_int64 (nm_setting_tun_get_owner (s_tun), 10, 0, G_MAXINT32, -1);
	group = _nm_utils_ascii_str_to_int64 (nm_setting_tun_get_group (s_tun), 10, 0, G_MAXINT32, -1);

	if (user != priv->props.owner)
		return FALSE;
	if (group != priv->props.group)
		return FALSE;
	if (nm_setting_tun_get_pi (s_tun) == priv->props.no_pi)
		return FALSE;
	if (nm_setting_tun_get_vnet_hdr (s_tun) != priv->props.vnet_hdr)
		return FALSE;
	if (nm_setting_tun_get_multi_queue (s_tun) != priv->props.multi_queue)
		return FALSE;

	return TRUE;
}

/**************************************************************/

static void
nm_device_tun_init (NMDeviceTun *self)
{
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMDeviceTun *self = NM_DEVICE_TUN (object);
	NMDeviceTunPrivate *priv = NM_DEVICE_TUN_GET_PRIVATE (self);

	switch (prop_id) {
	case PROP_OWNER:
		g_value_set_int64 (value, priv->props.owner);
		break;
	case PROP_GROUP:
		g_value_set_int64 (value, priv->props.group);
		break;
	case PROP_MODE:
		g_value_set_string (value, priv->mode);
		break;
	case PROP_NO_PI:
		g_value_set_boolean (value, priv->props.no_pi);
		break;
	case PROP_VNET_HDR:
		g_value_set_boolean (value, priv->props.vnet_hdr);
		break;
	case PROP_MULTI_QUEUE:
		g_value_set_boolean (value, priv->props.multi_queue);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMDeviceTun *self = NM_DEVICE_TUN (object);
	NMDeviceTunPrivate *priv = NM_DEVICE_TUN_GET_PRIVATE (self);
	const char *str;

	switch (prop_id) {
	case PROP_MODE:
		/* construct-only */
		str = g_value_get_string (value);

		/* mode is G_PARAM_STATIC_STRINGS */
		if (g_strcmp0 (str, "tun") == 0)
			priv->mode = "tun";
		else if (g_strcmp0 (str, "tap") == 0)
			priv->mode = "tap";
		else
			g_return_if_fail (FALSE);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_device_tun_class_init (NMDeviceTunClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMDeviceClass *device_class = NM_DEVICE_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NMDeviceTunPrivate));

	object_class->get_property = get_property;
	object_class->set_property = set_property;

	device_class->connection_type = NM_SETTING_TUN_SETTING_NAME;

	device_class->link_changed = link_changed;
	device_class->complete_connection = complete_connection;
	device_class->check_connection_compatible = check_connection_compatible;
	device_class->create_and_realize = create_and_realize;
	device_class->realize = realize;
	device_class->update_connection = update_connection;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_OWNER,
		 g_param_spec_int64 (NM_DEVICE_TUN_OWNER, "", "",
		                     -1, G_MAXUINT32, -1,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(object_class, PROP_GROUP,
		 g_param_spec_int64 (NM_DEVICE_TUN_GROUP, "", "",
		                     -1, G_MAXUINT32, -1,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(object_class, PROP_MODE,
		 g_param_spec_string (NM_DEVICE_TUN_MODE, "", "",
		                      "tun",
		                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(object_class, PROP_NO_PI,
		 g_param_spec_boolean (NM_DEVICE_TUN_NO_PI, "", "",
		                       FALSE,
		                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(object_class, PROP_VNET_HDR,
		 g_param_spec_boolean (NM_DEVICE_TUN_VNET_HDR, "", "",
		                       FALSE,
		                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(object_class, PROP_MULTI_QUEUE,
		 g_param_spec_boolean (NM_DEVICE_TUN_MULTI_QUEUE, "", "",
		                       FALSE,
		                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	nm_exported_object_class_add_interface (NM_EXPORTED_OBJECT_CLASS (klass),
	                                        NMDBUS_TYPE_DEVICE_TUN_SKELETON,
	                                        NULL);
}


/*************************************************************/

#define NM_TYPE_TUN_FACTORY (nm_tun_factory_get_type ())
#define NM_TUN_FACTORY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_TUN_FACTORY, NMTunFactory))

static NMDevice *
create_device (NMDeviceFactory *factory,
               const char *iface,
               NMPlatformLink *plink,
               NMConnection *connection,
               gboolean *out_ignore)
{
	return (NMDevice *) g_object_new (NM_TYPE_DEVICE_TUN,
	                                  NM_DEVICE_IFACE, iface,
	                                  NM_DEVICE_TYPE_DESC, "Tun",
	                                  NM_DEVICE_DEVICE_TYPE, NM_DEVICE_TYPE_TUN,
	                                  NULL);
}

NM_DEVICE_FACTORY_DEFINE_INTERNAL (TUN, Tun, tun,
	NM_DEVICE_FACTORY_DECLARE_LINK_TYPES (NM_LINK_TYPE_TUN, NM_LINK_TYPE_TAP)
	NM_DEVICE_FACTORY_DECLARE_SETTING_TYPES (NM_SETTING_TUN_SETTING_NAME),
	factory_iface->create_device = create_device;
	)

