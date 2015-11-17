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

#include "nm-setting-vxlan.h"
#include "nm-utils.h"
#include "nm-setting-connection.h"
#include "nm-setting-private.h"
#include "nm-setting-wired.h"
#include "nm-connection-private.h"

/**
 * SECTION:nm-setting-vxlan
 * @short_description: Describes connection properties for VXLAN interfaces
 *
 * The #NMSettingVxlan object is a #NMSetting subclass that describes properties
 * necessary for connection to VXLAN interfaces.
 **/

G_DEFINE_TYPE_WITH_CODE (NMSettingVxlan, nm_setting_vxlan, NM_TYPE_SETTING,
                         _nm_register_setting (VXLAN, 1))
NM_SETTING_REGISTER_TYPE (NM_TYPE_SETTING_VXLAN)

#define NM_SETTING_VXLAN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_SETTING_VXLAN, NMSettingVxlanPrivate))

typedef struct {
	char *parent;
	guint id;
	char *group;
	char *local;
	guint src_port_min;
	guint src_port_max;
	guint dst_port;
	guint tos;
	guint ttl;
	guint ageing;
	guint limit;
	gboolean learning;
	gboolean proxy;
	gboolean rsc;
	gboolean l2miss;
	gboolean l3miss;
} NMSettingVxlanPrivate;

enum {
	PROP_0,
	PROP_PARENT,
	PROP_ID,
	PROP_GROUP,
	PROP_LOCAL,
	PROP_SRC_PORT_MIN,
	PROP_SRC_PORT_MAX,
	PROP_DST_PORT,
	PROP_TOS,
	PROP_TTL,
	PROP_AGEING,
	PROP_LIMIT,
	PROP_LEARNING,
	PROP_PROXY,
	PROP_RSC,
	PROP_L2MISS,
	PROP_L3MISS,

	LAST_PROP
};

#define DST_PORT_DEFAULT   8472

/**
 * nm_setting_vxlan_new:
 *
 * Creates a new #NMSettingVxlan object with default values.
 *
 * Returns: (transfer full): the new empty #NMSettingVxlan object
 *
 * Since: 1.2
 **/
NMSetting *
nm_setting_vxlan_new (void)
{
	return (NMSetting *) g_object_new (NM_TYPE_SETTING_VXLAN, NULL);
}

/**
 * nm_setting_vxlan_get_parent:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:parent property of the setting
 *
 * Since: 1.2
 **/
const char *
nm_setting_vxlan_get_parent (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), NULL);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->parent;
}

/**
 * nm_setting_vxlan_get_id:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:id property of the setting
 *
 * Since: 1.2
 **/
guint
nm_setting_vxlan_get_id (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), 0);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->id;
}

/**
 * nm_setting_vxlan_get_local:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:local property of the setting
 *
 * Since: 1.2
 **/
const char *
nm_setting_vxlan_get_local (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), NULL);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->local;
}

/**
 * nm_setting_vxlan_get_src_port_min:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:src-port-min property of the setting
 *
 * Since: 1.2
 **/
guint
nm_setting_vxlan_get_src_port_min (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), 0);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->src_port_min;
}

/**
 * nm_setting_vxlan_get_src_port_max:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:src-port-max property of the setting
 *
 * Since: 1.2
 **/
guint
nm_setting_vxlan_get_src_port_max (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), 0);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->src_port_max;
}

/**
 * nm_setting_vxlan_get_dst_port:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:dst-port property of the setting
 *
 * Since: 1.2
 **/
guint
nm_setting_vxlan_get_dst_port (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), DST_PORT_DEFAULT);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->dst_port;
}

/**
 * nm_setting_vxlan_get_group:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:group property of the setting
 *
 * Since: 1.2
 **/
const char *
nm_setting_vxlan_get_group (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), NULL);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->group;
}

/**
 * nm_setting_vxlan_get_arp_proxy:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:arp-proxy property of the setting
 *
 * Since: 1.2
 **/
gboolean
nm_setting_vxlan_get_proxy (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), FALSE);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->proxy;
}

/**
 * nm_setting_vxlan_get_ageing:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:ageing property of the setting
 *
 * Since: 1.2
 **/
guint
nm_setting_vxlan_get_ageing (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), 0);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->ageing;
}

/**
 * nm_setting_vxlan_get_limit:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:limit property of the setting
 *
 * Since: 1.2
 **/
guint
nm_setting_vxlan_get_limit (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), 0);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->limit;
}

/**
 * nm_setting_vxlan_get_tos:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:tos property of the setting
 *
 * Since: 1.2
 **/
guint
nm_setting_vxlan_get_tos (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), 0);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->tos;
}

/**
 * nm_setting_vxlan_get_ttl:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:ttl property of the setting
 *
 * Since: 1.2
 **/
guint
nm_setting_vxlan_get_ttl (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), 0);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->ttl;
}

/**
 * nm_setting_vxlan_get_learning:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:learning property of the setting
 *
 * Since: 1.2
 **/
gboolean
nm_setting_vxlan_get_learning (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), FALSE);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->learning;
}

/**
 * nm_setting_vxlan_get_rsc:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:rsc property of the setting
 *
 * Since: 1.2
 **/
gboolean
nm_setting_vxlan_get_rsc (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), FALSE);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->rsc;
}

/**
 * nm_setting_vxlan_get_l2miss:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:l2miss property of the setting
 *
 * Since: 1.2
 **/
gboolean
nm_setting_vxlan_get_l2miss (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), FALSE);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->l2miss;
}

/**
 * nm_setting_vxlan_get_l3miss:
 * @setting: the #NMSettingVxlan
 *
 * Returns: the #NMSettingVxlan:l3miss property of the setting
 *
 * Since: 1.2
 **/
gboolean
nm_setting_vxlan_get_l3miss (NMSettingVxlan *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_VXLAN (setting), FALSE);
	return NM_SETTING_VXLAN_GET_PRIVATE (setting)->l3miss;
}

/*********************************************************************/

static void
nm_setting_vxlan_init (NMSettingVxlan *setting)
{
}

static gboolean
verify (NMSetting *setting, NMConnection *connection, GError **error)
{
	NMSettingVxlanPrivate *priv = NM_SETTING_VXLAN_GET_PRIVATE (setting);
	NMSettingConnection *s_con;
	NMSettingWired *s_wired;
	int family = AF_UNSPEC;

	if (connection) {
		s_con = nm_connection_get_setting_connection (connection);
		s_wired = nm_connection_get_setting_wired (connection);
	} else {
		s_con = NULL;
		s_wired = NULL;
	}

	if (!priv->group) {
		g_set_error_literal (error,
		                     NM_CONNECTION_ERROR,
		                     NM_CONNECTION_ERROR_MISSING_PROPERTY,
		                     _("property is missing"));
		g_prefix_error (error, "%s.%s: ",
		                NM_SETTING_VXLAN_SETTING_NAME,
		                NM_SETTING_VXLAN_GROUP);
		return FALSE;
	}

	if (nm_utils_ipaddr_valid (AF_INET, priv->group))
		family = AF_INET;
	else if (nm_utils_ipaddr_valid (AF_INET6, priv->group))
		family = AF_INET6;
	else {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s' is not a valid IP address"),
		             priv->group);
		g_prefix_error (error, "%s.%s: ",
		                NM_SETTING_VXLAN_SETTING_NAME,
		                NM_SETTING_VXLAN_GROUP);
		return FALSE;
	}

	if (priv->local) {
		if (!nm_utils_ipaddr_valid (family, priv->local)) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s' is not a valid IP%c address"),
			             priv->local, family == AF_INET ? '4' : '6');
			g_prefix_error (error, "%s.%s: ",
			                NM_SETTING_VXLAN_SETTING_NAME,
			                NM_SETTING_VXLAN_LOCAL);
			return FALSE;
		}
	}

	if (priv->parent) {
		if (nm_utils_is_uuid (priv->parent)) {
			/* If we have an NMSettingConnection:master with slave-type="vxlan",
			 * then it must be the same UUID.
			 */
			if (s_con) {
				const char *master = NULL, *slave_type = NULL;

				slave_type = nm_setting_connection_get_slave_type (s_con);
				if (!g_strcmp0 (slave_type, NM_SETTING_VXLAN_SETTING_NAME))
					master = nm_setting_connection_get_master (s_con);

				if (master && g_strcmp0 (priv->parent, master) != 0) {
					g_set_error (error,
					             NM_CONNECTION_ERROR,
					             NM_CONNECTION_ERROR_INVALID_PROPERTY,
					             _("'%s' value doesn't match '%s=%s'"),
					             priv->parent, NM_SETTING_CONNECTION_MASTER, master);
					g_prefix_error (error, "%s.%s: ", NM_SETTING_VXLAN_SETTING_NAME,
					                NM_SETTING_VXLAN_PARENT);
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
			g_prefix_error (error, "%s.%s: ", NM_SETTING_VXLAN_SETTING_NAME,
			                NM_SETTING_VXLAN_PARENT);
			return FALSE;
		}
	}

	/* In order to assign a name to the interface we need 'connection.interface-name'
	 * to be set or a parent device from which the name can be derived appending the
	 * vxlan id.
	 */
	if (   (!s_con || !nm_setting_connection_get_interface_name (s_con))
	    && (!s_wired || !nm_setting_wired_get_mac_address (s_wired))
	    && !priv->parent) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_MISSING_PROPERTY,
		             _("the property is required when no interface name is specified"));
		g_prefix_error (error, "%s.%s: ", NM_SETTING_VXLAN_SETTING_NAME, NM_SETTING_VXLAN_PARENT);
		return FALSE;
	}

	if (   (priv->src_port_min || priv->src_port_max)
	    && (priv->src_port_min > priv->src_port_max)) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("%d is greater than local port max %d"),
		             priv->src_port_min,
		             priv->src_port_max);
		g_prefix_error (error, "%s.%s: ",
		                NM_SETTING_VXLAN_SETTING_NAME,
		                NM_SETTING_VXLAN_SRC_PORT_MIN);
		return FALSE;
	}

	return TRUE;
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMSettingVxlan *setting = NM_SETTING_VXLAN (object);
	NMSettingVxlanPrivate *priv = NM_SETTING_VXLAN_GET_PRIVATE (setting);

	switch (prop_id) {
	case PROP_PARENT:
		g_free (priv->parent);
		priv->parent = g_value_dup_string (value);
		break;
	case PROP_ID:
		priv->id = g_value_get_uint (value);
		break;
	case PROP_LOCAL:
		g_free (priv->local);
		priv->local = g_value_dup_string (value);
		break;
	case PROP_SRC_PORT_MIN:
		priv->src_port_min = g_value_get_uint (value);
		break;
	case PROP_SRC_PORT_MAX:
		priv->src_port_max = g_value_get_uint (value);
		break;
	case PROP_DST_PORT:
		priv->dst_port = g_value_get_uint (value);
		break;
	case PROP_GROUP:
		g_free (priv->group);
		priv->group = g_value_dup_string (value);
		break;
	case PROP_TOS:
		priv->tos = g_value_get_uint (value);
		break;
	case PROP_AGEING:
		priv->ageing = g_value_get_uint (value);
		break;
	case PROP_LIMIT:
		priv->limit = g_value_get_uint (value);
		break;
	case PROP_PROXY:
		priv->proxy = g_value_get_boolean (value);
		break;
	case PROP_TTL:
		priv->ttl = g_value_get_uint (value);
		break;
	case PROP_LEARNING:
		priv->learning = g_value_get_boolean (value);
		break;
	case PROP_RSC:
		priv->rsc = g_value_get_boolean (value);
		break;
	case PROP_L2MISS:
		priv->l2miss = g_value_get_boolean (value);
		break;
	case PROP_L3MISS:
		priv->l3miss = g_value_get_boolean (value);
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
	NMSettingVxlan *setting = NM_SETTING_VXLAN (object);
	NMSettingVxlanPrivate *priv = NM_SETTING_VXLAN_GET_PRIVATE (setting);

	switch (prop_id) {
	case PROP_PARENT:
		g_value_set_string (value, priv->parent);
		break;
	case PROP_ID:
		g_value_set_uint (value, priv->id);
		break;
	case PROP_LOCAL:
		g_value_set_string (value, priv->local);
		break;
	case PROP_SRC_PORT_MIN:
		g_value_set_uint (value, priv->src_port_min);
		break;
	case PROP_SRC_PORT_MAX:
		g_value_set_uint (value, priv->src_port_max);
		break;
	case PROP_DST_PORT:
		g_value_set_uint (value, priv->dst_port);
		break;
	case PROP_GROUP:
		g_value_set_string (value, priv->group);
		break;
	case PROP_TOS:
		g_value_set_uint (value, priv->tos);
		break;
	case PROP_AGEING:
		g_value_set_uint (value, priv->ageing);
		break;
	case PROP_LIMIT:
		g_value_set_uint (value, priv->limit);
		break;
	case PROP_PROXY:
		g_value_set_boolean (value, priv->proxy);
		break;
	case PROP_TTL:
		g_value_set_uint (value, priv->ttl);
		break;
	case PROP_LEARNING:
		g_value_set_boolean (value, priv->learning);
		break;
	case PROP_RSC:
		g_value_set_boolean (value, priv->rsc);
		break;
	case PROP_L2MISS:
		g_value_set_boolean (value, priv->l2miss);
		break;
	case PROP_L3MISS:
		g_value_set_boolean (value, priv->l3miss);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
finalize (GObject *object)
{
	NMSettingVxlan *setting = NM_SETTING_VXLAN (object);
	NMSettingVxlanPrivate *priv = NM_SETTING_VXLAN_GET_PRIVATE (setting);

	g_free (priv->parent);
	g_free (priv->local);
	g_free (priv->group);

	G_OBJECT_CLASS (nm_setting_vxlan_parent_class)->finalize (object);
}

static void
nm_setting_vxlan_class_init (NMSettingVxlanClass *setting_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (setting_class);
	NMSettingClass *parent_class = NM_SETTING_CLASS (setting_class);

	g_type_class_add_private (setting_class, sizeof (NMSettingVxlanPrivate));

	/* virtual methods */
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize     = finalize;
	parent_class->verify       = verify;

	/* Properties */

	/**
	 * NMSettingVxlan:parent:
	 *
	 * If given, specifies the parent interface name or parent connection UUID.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_PARENT,
		 g_param_spec_string (NM_SETTING_VXLAN_PARENT, "", "",
		                      NULL,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      NM_SETTING_PARAM_INFERRABLE |
		                      G_PARAM_STATIC_STRINGS));
	/**
	 * NMSettingVxlan:id:
	 *
	 * Specifies the VXLAN Network Identifer (or VXLAN Segment Identifier) to use.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_ID,
		 g_param_spec_uint (NM_SETTING_VXLAN_ID, "", "",
		                    0, (1 << 24) - 1, 0,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      NM_SETTING_PARAM_INFERRABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:local:
	 *
	 * If given, specifies the source IP address to use in outgoing packets.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_LOCAL,
		 g_param_spec_string (NM_SETTING_VXLAN_LOCAL, "", "",
		                      NULL,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      NM_SETTING_PARAM_INFERRABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:group:
	 *
	 * Specifies the unicast destination IP address to use in outgoing
	 * packets when the destination link layer address is not known in the VXLAN
	 * device forwarding database or the multicast IP address to join.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_GROUP,
		 g_param_spec_string (NM_SETTING_VXLAN_GROUP, "", "",
		                      NULL,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      NM_SETTING_PARAM_INFERRABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:src-port-min:
	 *
	 * Specifies the minimum UDP source port to communicate to the
	 * remote VXLAN tunnel endpoint.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_SRC_PORT_MIN,
		 g_param_spec_uint (NM_SETTING_VXLAN_SRC_PORT_MIN, "", "",
		                    0, G_MAXUINT16, 0,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    NM_SETTING_PARAM_INFERRABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:src-port-max:
	 *
	 * Specifies the maximum UDP source port to communicate to the
	 * remote VXLAN tunnel endpoint.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_SRC_PORT_MAX,
		 g_param_spec_uint (NM_SETTING_VXLAN_SRC_PORT_MAX, "", "",
		                    0, G_MAXUINT16, 0,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    NM_SETTING_PARAM_INFERRABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:dst-port:
	 *
	 * Specifies the UDP destination port to communicate to the
	 * remote VXLAN tunnel endpoint.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_DST_PORT,
		 g_param_spec_uint (NM_SETTING_VXLAN_DST_PORT, "", "",
		                    0, G_MAXUINT16, DST_PORT_DEFAULT,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    NM_SETTING_PARAM_INFERRABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:ageing:
	 *
	 * Specifies the lifetime in seconds of FDB entries learnt by the kernel.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_AGEING,
		 g_param_spec_uint (NM_SETTING_VXLAN_AGEING, "", "",
		                    0, G_MAXINT32, 300,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    NM_SETTING_PARAM_INFERRABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:limit:
	 *
	 * Specifies the maximum number of FDB entries. A value of zero
	 * means that the kernel will store unlimited entries.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_AGEING,
		 g_param_spec_uint (NM_SETTING_VXLAN_LIMIT, "", "",
		                    0, G_MAXINT32, 0,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    NM_SETTING_PARAM_INFERRABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:tos:
	 *
	 * Specifies the TOS value to use in outgoing packets.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_TOS,
		 g_param_spec_uint (NM_SETTING_VXLAN_TOS, "", "",
		                    0, 255, 0,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    NM_SETTING_PARAM_INFERRABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:ttl:
	 *
	 * Specifies the time-to-live value to use in outgoing packets.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_TTL,
		 g_param_spec_uint (NM_SETTING_VXLAN_TTL, "", "",
		                    0, 255, 0,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      NM_SETTING_PARAM_INFERRABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:proxy:
	 *
	 * Specifies if ARP proxy is turned on.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_PROXY,
		 g_param_spec_boolean (NM_SETTING_VXLAN_PROXY, "", "",
		                       FALSE,
		                       G_PARAM_READWRITE |
		                       G_PARAM_CONSTRUCT |
		                       NM_SETTING_PARAM_INFERRABLE |
		                       G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:learning:
	 *
	 * Specifies if unknown source link layer addresses and IP addresses are
	 * entered into the VXLAN device forwarding database.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_LEARNING,
		 g_param_spec_boolean (NM_SETTING_VXLAN_LEARNING, "", "",
		                       TRUE,
		                       G_PARAM_READWRITE |
		                       G_PARAM_CONSTRUCT |
		                       NM_SETTING_PARAM_INFERRABLE |
		                       G_PARAM_STATIC_STRINGS));
	/**
	 * NMSettingVxlan:rsc:
	 *
	 * Specifies if route short circuit is turned on.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_RSC,
		 g_param_spec_boolean (NM_SETTING_VXLAN_RSC, "", "",
		                       FALSE,
		                       G_PARAM_READWRITE |
		                       G_PARAM_CONSTRUCT |
		                       NM_SETTING_PARAM_INFERRABLE |
		                       G_PARAM_STATIC_STRINGS));
	/**
	 * NMSettingVxlan:l2miss:
	 *
	 * Specifies if netlink LL ADDR miss notifications are generated.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_L2MISS,
		 g_param_spec_boolean (NM_SETTING_VXLAN_L2MISS, "", "",
		                       FALSE,
		                       G_PARAM_READWRITE |
		                       G_PARAM_CONSTRUCT |
		                       NM_SETTING_PARAM_INFERRABLE |
		                       G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingVxlan:l3miss:
	 *
	 * Specifies if netlink IP ADDR miss notifications are generated.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_L3MISS,
		 g_param_spec_boolean (NM_SETTING_VXLAN_L3MISS, "", "",
		                       FALSE,
		                       G_PARAM_READWRITE |
		                       G_PARAM_CONSTRUCT |
		                       NM_SETTING_PARAM_INFERRABLE |
		                       G_PARAM_STATIC_STRINGS));
}
