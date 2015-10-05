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

#include "nm-setting-macvlan.h"
#include "nm-utils.h"
#include "nm-setting-connection.h"
#include "nm-setting-private.h"
#include "nm-setting-wired.h"
#include "nm-connection-private.h"

/**
 * SECTION:nm-setting-macvlan
 * @short_description: Describes connection properties for MAC-VLAN interfaces
 *
 * The #NMSettingMacvlan object is a #NMSetting subclass that describes properties
 * necessary for connection to MACVLAN interfaces.
 **/

G_DEFINE_TYPE_WITH_CODE (NMSettingMacvlan, nm_setting_macvlan, NM_TYPE_SETTING,
                         _nm_register_setting (MACVLAN, 1))
NM_SETTING_REGISTER_TYPE (NM_TYPE_SETTING_MACVLAN)

#define NM_SETTING_MACVLAN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_SETTING_MACVLAN, NMSettingMacvlanPrivate))

typedef struct {
	char *parent;
	NMSettingMacvlanMode mode;
	gboolean is_macvtap;
} NMSettingMacvlanPrivate;

enum {
	PROP_0,
	PROP_PARENT,
	PROP_MODE,
	PROP_IS_MACVTAP,
	LAST_PROP
};

/**
 * nm_setting_macvlan_new:
 *
 * Creates a new #NMSettingMacvlan object with default values.
 *
 * Returns: (transfer full): the new empty #NMSettingMacvlan object
 *
 * Since: 1.2
 **/
NMSetting *
nm_setting_macvlan_new (void)
{
	return (NMSetting *) g_object_new (NM_TYPE_SETTING_MACVLAN, NULL);
}

/**
 * nm_setting_macvlan_get_parent:
 * @setting: the #NMSettingMacvlan
 *
 * Returns: the #NMSettingMacvlan:parent property of the setting
 *
 * Since: 1.2
 **/
const char *
nm_setting_macvlan_get_parent (NMSettingMacvlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_MACVLAN (setting), NULL);
	return NM_SETTING_MACVLAN_GET_PRIVATE (setting)->parent;
}

/**
 * nm_setting_macvlan_get_mode:
 * @setting: the #NMSettingMacvlan
 *
 * Returns: the #NMSettingMacvlan:mode property of the setting
 *
 * Since: 1.2
 **/
NMSettingMacvlanMode
nm_setting_macvlan_get_mode (NMSettingMacvlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_MACVLAN (setting), NM_SETTING_MACVLAN_MODE_DEFAULT);
	return NM_SETTING_MACVLAN_GET_PRIVATE (setting)->mode;
}

/**
 * nm_setting_macvlan_get_is_macvtap:
 * @setting: the #NMSettingMacvlan
 *
 * Returns: the #NMSettingMacvlan:is_macvtap property of the setting
 *
 * Since: 1.2
 **/
gboolean
nm_setting_macvlan_get_is_macvtap (NMSettingMacvlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_MACVLAN (setting), FALSE);
	return NM_SETTING_MACVLAN_GET_PRIVATE (setting)->is_macvtap;
}

/*********************************************************************/

static void
nm_setting_macvlan_init (NMSettingMacvlan *setting)
{
}

static gboolean
verify (NMSetting *setting, NMConnection *connection, GError **error)
{
	NMSettingMacvlanPrivate *priv = NM_SETTING_MACVLAN_GET_PRIVATE (setting);
	NMSettingConnection *s_con;
	NMSettingWired *s_wired;

	if (connection) {
		s_con = nm_connection_get_setting_connection (connection);
		s_wired = nm_connection_get_setting_wired (connection);
	} else {
		s_con = NULL;
		s_wired = NULL;
	}

	if (priv->parent) {
		if (nm_utils_is_uuid (priv->parent)) {
			/* If we have an NMSettingConnection:master with slave-type="macvlan",
			 * then it must be the same UUID.
			 */
			if (s_con) {
				const char *master = NULL, *slave_type = NULL;

				slave_type = nm_setting_connection_get_slave_type (s_con);
				if (!g_strcmp0 (slave_type, NM_SETTING_MACVLAN_SETTING_NAME))
					master = nm_setting_connection_get_master (s_con);

				if (master && g_strcmp0 (priv->parent, master) != 0) {
					g_set_error (error,
					             NM_CONNECTION_ERROR,
					             NM_CONNECTION_ERROR_INVALID_PROPERTY,
					             _("'%s' value doesn't match '%s=%s'"),
					             priv->parent, NM_SETTING_CONNECTION_MASTER, master);
					g_prefix_error (error, "%s.%s: ", NM_SETTING_MACVLAN_SETTING_NAME, NM_SETTING_MACVLAN_PARENT);
					return FALSE;
				}
			}
		} else if (!nm_utils_iface_valid_name (priv->parent)) {
			/* parent must be either a UUID or an interface name */
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s' is neither an UUID nor an interface name"),
			             priv->parent);
			g_prefix_error (error, "%s.%s: ", NM_SETTING_MACVLAN_SETTING_NAME, NM_SETTING_MACVLAN_PARENT);
			return FALSE;
		}
	} else {
		/* If parent is NULL, the parent must be specified via
		 * NMSettingWired:mac-address.
		 */
		if (   connection
		    && (!s_wired || !nm_setting_wired_get_mac_address (s_wired))) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_MISSING_PROPERTY,
			             _("property is not specified and neither is '%s:%s'"),
			             NM_SETTING_WIRED_SETTING_NAME, NM_SETTING_WIRED_MAC_ADDRESS);
			g_prefix_error (error, "%s.%s: ", NM_SETTING_MACVLAN_SETTING_NAME, NM_SETTING_MACVLAN_PARENT);
			return FALSE;
		}
	}

	return TRUE;
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMSettingMacvlan *setting = NM_SETTING_MACVLAN (object);
	NMSettingMacvlanPrivate *priv = NM_SETTING_MACVLAN_GET_PRIVATE (setting);

	switch (prop_id) {
	case PROP_PARENT:
		g_free (priv->parent);
		priv->parent = g_value_dup_string (value);
		break;
	case PROP_MODE:
		priv->mode = g_value_get_uint (value);
		break;
	case PROP_IS_MACVTAP:
		priv->is_macvtap = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMSettingMacvlan *setting = NM_SETTING_MACVLAN (object);
	NMSettingMacvlanPrivate *priv = NM_SETTING_MACVLAN_GET_PRIVATE (setting);

	switch (prop_id) {
	case PROP_PARENT:
		g_value_set_string (value, priv->parent);
		break;
	case PROP_MODE:
		g_value_set_uint (value, priv->mode);
		break;
	case PROP_IS_MACVTAP:
		g_value_set_boolean (value, priv->is_macvtap);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
finalize (GObject *object)
{
	NMSettingMacvlan *setting = NM_SETTING_MACVLAN (object);
	NMSettingMacvlanPrivate *priv = NM_SETTING_MACVLAN_GET_PRIVATE (setting);

	g_free (priv->parent);

	G_OBJECT_CLASS (nm_setting_macvlan_parent_class)->finalize (object);
}

static void
nm_setting_macvlan_class_init (NMSettingMacvlanClass *setting_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (setting_class);
	NMSettingClass *parent_class = NM_SETTING_CLASS (setting_class);

	g_type_class_add_private (setting_class, sizeof (NMSettingMacvlanPrivate));

	/* virtual methods */
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize     = finalize;
	parent_class->verify       = verify;

	/* Properties */

	/**
	 * NMSettingMacvlan:parent:
	 *
	 * If given, specifies the parent interface name or parent connection UUID
	 * from which this MAC-VLAN interface should be created.  If this property is
	 * not specified, the connection must contain an #NMSettingWired setting
	 * with a #NMSettingWired:mac-address property.
	 *
	 * Since: 1.2
	 **/
	/* ---ifcfg-rh---
	 * property: parent
	 * variable: DEVICE or PHYSDEV
	 * description: Parent interface of the MAC-VLAN.
	 * ---end---
	 */
	g_object_class_install_property
		(object_class, PROP_PARENT,
		 g_param_spec_string (NM_SETTING_MACVLAN_PARENT, "", "",
		                      NULL,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      NM_SETTING_PARAM_INFERRABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingMacvlan:mode:
	 *
	 * The MAC-VLAN mode, which specifies the communication mechanism between multiple
	 * MAC-VLANs on the same lower device.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_MODE,
		 g_param_spec_uint (NM_SETTING_MACVLAN_MODE, "", "",
		                    0, G_MAXUINT, 0,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    NM_SETTING_PARAM_INFERRABLE |
		                    G_PARAM_STATIC_STRINGS));
	/**
	 * NMSettingMacvlan:is_macvtap:
	 *
	 * If %TRUE the device will be a macvtap, otherwise a macvlan.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_IS_MACVTAP,
		 g_param_spec_boolean (NM_SETTING_MACVLAN_IS_MACVTAP, "", "",
		                       FALSE,
		                       G_PARAM_READWRITE |
		                       G_PARAM_CONSTRUCT |
		                       NM_SETTING_PARAM_INFERRABLE |
		                       G_PARAM_STATIC_STRINGS));
}
