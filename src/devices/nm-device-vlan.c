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
 * Copyright 2011 - 2012 Red Hat, Inc.
 */

#include "config.h"

#include <sys/socket.h>

#include "nm-default.h"
#include "nm-device-vlan.h"
#include "nm-manager.h"
#include "nm-utils.h"
#include "NetworkManagerUtils.h"
#include "nm-device-private.h"
#include "nm-enum-types.h"
#include "nm-connection-provider.h"
#include "nm-activation-request.h"
#include "nm-ip4-config.h"
#include "nm-platform.h"
#include "nm-device-factory.h"
#include "nm-manager.h"
#include "nm-core-internal.h"
#include "nmp-object.h"

#include "nmdbus-device-vlan.h"

#include "nm-device-logging.h"
_LOG_DECLARE_SELF(NMDeviceVlan);

G_DEFINE_TYPE (NMDeviceVlan, nm_device_vlan, NM_TYPE_DEVICE)

#define NM_DEVICE_VLAN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE_VLAN, NMDeviceVlanPrivate))

typedef struct {
	NMDevice *parent;
	guint parent_state_id;
	guint parent_hwaddr_id;
	int vlan_id;
} NMDeviceVlanPrivate;

enum {
	PROP_0,
	PROP_PARENT,
	PROP_VLAN_ID,

	LAST_PROP
};

/******************************************************************/

static void
parent_state_changed (NMDevice *parent,
                      NMDeviceState new_state,
                      NMDeviceState old_state,
                      NMDeviceStateReason reason,
                      gpointer user_data)
{
	NMDeviceVlan *self = NM_DEVICE_VLAN (user_data);

	/* We'll react to our own carrier state notifications. Ignore the parent's. */
	if (reason == NM_DEVICE_STATE_REASON_CARRIER)
		return;

	nm_device_set_unmanaged (NM_DEVICE (self), NM_UNMANAGED_PARENT, !nm_device_get_managed (parent), reason);
}

static void
parent_hwaddr_changed (NMDevice *parent,
                       GParamSpec *pspec,
                       gpointer user_data)
{
	NMDeviceVlan *self = NM_DEVICE_VLAN (user_data);
	NMConnection *connection;
	NMSettingWired *s_wired;
	const char *cloned_mac = NULL;

	/* Never touch assumed devices */
	if (nm_device_uses_assumed_connection (self))
		return;

	connection = nm_device_get_applied_connection (self);
	if (!connection)
		return;

	/* Update the VLAN MAC only if configuration does not specify one */
	s_wired = nm_connection_get_setting_wired (connection);
	if (s_wired)
		cloned_mac = nm_setting_wired_get_cloned_mac_address (s_wired);

	if (!cloned_mac) {
		_LOGD (LOGD_VLAN, "parent hardware address changed");
		nm_device_set_hw_addr (self, nm_device_get_hw_address (parent),
		                       "set", LOGD_VLAN);
	}
}

static void
nm_device_vlan_set_parent (NMDeviceVlan *self, NMDevice *parent)
{
	NMDeviceVlanPrivate *priv = NM_DEVICE_VLAN_GET_PRIVATE (self);
	NMDevice *device = NM_DEVICE (self);

	if (parent == priv->parent)
		return;

	nm_clear_g_signal_handler (priv->parent, &priv->parent_state_id);
	nm_clear_g_signal_handler (priv->parent, &priv->parent_hwaddr_id);
	g_clear_object (&priv->parent);

	if (parent) {
		priv->parent = g_object_ref (parent);
		priv->parent_state_id = g_signal_connect (priv->parent,
		                                          "state-changed",
		                                          G_CALLBACK (parent_state_changed),
		                                          device);

		priv->parent_hwaddr_id = g_signal_connect (priv->parent, "notify::" NM_DEVICE_HW_ADDRESS,
		                                           G_CALLBACK (parent_hwaddr_changed), device);

		/* Set parent-dependent unmanaged flag */
		nm_device_set_unmanaged (device,
		                         NM_UNMANAGED_PARENT,
		                         !nm_device_get_managed (parent),
		                         NM_DEVICE_STATE_REASON_PARENT_MANAGED_CHANGED);
	}

	/* Recheck availability now that the parent has changed */
	nm_device_queue_recheck_available (self,
	                                   NM_DEVICE_STATE_REASON_PARENT_CHANGED,
	                                   NM_DEVICE_STATE_REASON_PARENT_CHANGED);
	g_object_notify (G_OBJECT (device), NM_DEVICE_VLAN_PARENT);
}

static void
setup (NMDevice *device, NMPlatformLink *plink)
{
	NMDeviceVlan *self = NM_DEVICE_VLAN (device);
	NMDeviceVlanPrivate *priv = NM_DEVICE_VLAN_GET_PRIVATE (self);

	NM_DEVICE_CLASS (nm_device_vlan_parent_class)->setup (device, plink);

	_LOGI (LOGD_HW | LOGD_VLAN, "VLAN ID %d with parent %s",
	       priv->vlan_id, nm_device_get_iface (priv->parent));
}

static gboolean
realize (NMDevice *device,
         NMPlatformLink *plink,
         GError **error)
{
	NMDeviceVlanPrivate *priv = NM_DEVICE_VLAN_GET_PRIVATE (device);
	NMDevice *parent;
	const NMPlatformLnkVlan *plnk;

	g_return_val_if_fail (plink, FALSE);

	g_assert (plink->type == NM_LINK_TYPE_VLAN);

	plnk = nm_platform_link_get_lnk_vlan (NM_PLATFORM_GET, plink->ifindex, NULL);
	if (!plnk) {
		g_set_error (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_FAILED,
		             "(%s): failed to get VLAN properties", plink->name);
		return FALSE;
	}

	if (plnk->id < 0) {
		g_set_error (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_FAILED,
		             "(%s): VLAN ID invalid", plink->name);
		return FALSE;
	}

	if (plink->parent != NM_PLATFORM_LINK_OTHER_NETNS) {
		parent = nm_manager_get_device_by_ifindex (nm_manager_get (), plink->parent);
		if (!parent) {
			nm_log_dbg (LOGD_HW, "(%s): VLAN parent interface unknown", plink->name);
			g_set_error (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_FAILED,
		                     "(%s): VLAN parent interface unknown", plink->name);
			return FALSE;
		}
	} else
		parent = NULL;

	g_warn_if_fail (priv->parent == NULL);
	nm_device_vlan_set_parent (NM_DEVICE_VLAN (device), parent);
	priv->vlan_id = plnk->id;

	return TRUE;
}

static gboolean
create_and_realize (NMDevice *device,
                    NMConnection *connection,
                    NMDevice *parent,
                    NMPlatformLink *out_plink,
                    GError **error)
{
	NMDeviceVlanPrivate *priv = NM_DEVICE_VLAN_GET_PRIVATE (device);
	const char *iface = nm_device_get_iface (device);
	NMSettingVlan *s_vlan;
	int parent_ifindex, vlan_id;
	NMPlatformError plerr;

	g_assert (out_plink);

	s_vlan = nm_connection_get_setting_vlan (connection);
	g_assert (s_vlan);

	if (!nm_device_supports_vlans (parent)) {
		g_set_error (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_FAILED,
		             "no support for VLANs on interface %s of type %s",
		             nm_device_get_iface (parent),
		             nm_device_get_type_desc (parent));
		return FALSE;
	}

	parent_ifindex = nm_device_get_ifindex (parent);
	g_warn_if_fail (parent_ifindex > 0);

	vlan_id = nm_setting_vlan_get_id (s_vlan);

	plerr = nm_platform_vlan_add (NM_PLATFORM_GET,
	                              iface,
	                              parent_ifindex,
	                              vlan_id,
	                              nm_setting_vlan_get_flags (s_vlan),
	                              out_plink);
	if (plerr != NM_PLATFORM_ERROR_SUCCESS && plerr != NM_PLATFORM_ERROR_EXISTS) {
		g_set_error (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_CREATION_FAILED,
		             "Failed to create VLAN interface '%s' for '%s': %s",
		             iface,
		             nm_connection_get_id (connection),
		             nm_platform_error_to_string (plerr));
		return FALSE;
	}

	g_warn_if_fail (priv->parent == NULL);
	nm_device_vlan_set_parent (NM_DEVICE_VLAN (device), parent);
	priv->vlan_id = vlan_id;

	return TRUE;
}

/******************************************************************/

static NMDeviceCapabilities
get_generic_capabilities (NMDevice *dev)
{
	/* We assume VLAN interfaces always support carrier detect */
	return NM_DEVICE_CAP_CARRIER_DETECT | NM_DEVICE_CAP_IS_SOFTWARE;
}

static gboolean
bring_up (NMDevice *dev, gboolean *no_firmware)
{
	gboolean success = FALSE;
	guint i = 20;

	while (i-- > 0 && !success) {
		success = NM_DEVICE_CLASS (nm_device_vlan_parent_class)->bring_up (dev, no_firmware);
		g_usleep (50);
	}

	return success;
}

/******************************************************************/

static gboolean
is_available (NMDevice *device, NMDeviceCheckDevAvailableFlags flags)
{
	if (!NM_DEVICE_VLAN_GET_PRIVATE (device)->parent)
		return FALSE;

	return NM_DEVICE_CLASS (nm_device_vlan_parent_class)->is_available (device, flags);
}

static gboolean
component_added (NMDevice *device, GObject *component)
{
	NMDeviceVlan *self = NM_DEVICE_VLAN (device);
	NMDeviceVlanPrivate *priv = NM_DEVICE_VLAN_GET_PRIVATE (self);
	NMDevice *added_device;
	const NMPlatformLink *plink;
	const NMPlatformLnkVlan *plnk;

	if (priv->parent)
		return FALSE;

	if (!NM_IS_DEVICE (component))
		return FALSE;
	added_device = NM_DEVICE (component);

	plnk = nm_platform_link_get_lnk_vlan (NM_PLATFORM_GET, nm_device_get_ifindex (device), &plink);
	if (!plnk) {
		_LOGW (LOGD_VLAN, "failed to get VLAN interface info while checking added component.");
		return FALSE;
	}

	if (   plink->parent <= 0
	    || nm_device_get_ifindex (added_device) != plink->parent)
		return FALSE;

	nm_device_vlan_set_parent (self, added_device);

	/* Don't claim parent exclusively */
	return FALSE;
}

/******************************************************************/

static gboolean
match_parent (NMDeviceVlan *self, const char *parent)
{
	NMDeviceVlanPrivate *priv = NM_DEVICE_VLAN_GET_PRIVATE (self);

	g_return_val_if_fail (parent != NULL, FALSE);

	if (!priv->parent)
		return FALSE;

	if (nm_utils_is_uuid (parent)) {
		NMActRequest *parent_req;
		NMConnection *parent_connection;

		/* If the parent is a UUID, the connection matches if our parent
		 * device has that connection activated.
		 */

		parent_req = nm_device_get_act_request (priv->parent);
		if (!parent_req)
			return FALSE;

		parent_connection = nm_active_connection_get_applied_connection (NM_ACTIVE_CONNECTION (parent_req));
		if (!parent_connection)
			return FALSE;

		if (g_strcmp0 (parent, nm_connection_get_uuid (parent_connection)) != 0)
			return FALSE;
	} else {
		/* interface name */
		if (g_strcmp0 (parent, nm_device_get_ip_iface (priv->parent)) != 0)
			return FALSE;
	}

	return TRUE;
}

static gboolean
match_hwaddr (NMDevice *device, NMConnection *connection, gboolean fail_if_no_hwaddr)
{
	  NMSettingWired *s_wired;
	  const char *setting_mac;
	  const char *device_mac;

	  s_wired = nm_connection_get_setting_wired (connection);
	  if (!s_wired)
		  return !fail_if_no_hwaddr;

	  setting_mac = nm_setting_wired_get_mac_address (s_wired);
	  if (!setting_mac)
		  return !fail_if_no_hwaddr;

	  device_mac = nm_device_get_hw_address (device);

	  return nm_utils_hwaddr_matches (setting_mac, -1, device_mac, -1);
}

static gboolean
check_connection_compatible (NMDevice *device, NMConnection *connection)
{
	NMDeviceVlanPrivate *priv = NM_DEVICE_VLAN_GET_PRIVATE (device);
	NMSettingVlan *s_vlan;
	const char *parent, *iface = NULL;

	if (!NM_DEVICE_CLASS (nm_device_vlan_parent_class)->check_connection_compatible (device, connection))
		return FALSE;

	s_vlan = nm_connection_get_setting_vlan (connection);
	if (!s_vlan)
		return FALSE;

	if (nm_setting_vlan_get_id (s_vlan) != priv->vlan_id)
		return FALSE;

	/* Check parent interface; could be an interface name or a UUID */
	parent = nm_setting_vlan_get_parent (s_vlan);
	if (parent) {
		if (!match_parent (NM_DEVICE_VLAN (device), parent))
			return FALSE;
	} else {
		/* Parent could be a MAC address in an NMSettingWired */
		if (!match_hwaddr (device, connection, TRUE))
			return FALSE;
	}

	/* Ensure the interface name matches.  If not specified we assume a match
	 * since both the parent interface and the VLAN ID matched by the time we
	 * get here.
	 */
	iface = nm_connection_get_interface_name (connection);
	if (iface) {
		if (g_strcmp0 (nm_device_get_ip_iface (device), iface) != 0)
			return FALSE;
	}

	return TRUE;
}

static gboolean
complete_connection (NMDevice *device,
                     NMConnection *connection,
                     const char *specific_object,
                     const GSList *existing_connections,
                     GError **error)
{
	NMSettingVlan *s_vlan;

	nm_utils_complete_generic (connection,
	                           NM_SETTING_VLAN_SETTING_NAME,
	                           existing_connections,
	                           NULL,
	                           _("VLAN connection"),
	                           NULL,
	                           TRUE);

	s_vlan = nm_connection_get_setting_vlan (connection);
	if (!s_vlan) {
		g_set_error_literal (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INVALID_CONNECTION,
		                     "A 'vlan' setting is required.");
		return FALSE;
	}

	/* If there's no VLAN interface, no parent, and no hardware address in the
	 * settings, then there's not enough information to complete the setting.
	 */
	if (   !nm_setting_vlan_get_parent (s_vlan)
	    && !match_hwaddr (device, connection, TRUE)) {
		g_set_error_literal (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INVALID_CONNECTION,
		                     "The 'vlan' setting had no interface name, parent, or hardware address.");
		return FALSE;
	}

	return TRUE;
}

static void
update_connection (NMDevice *device, NMConnection *connection)
{
	NMDeviceVlan *self = NM_DEVICE_VLAN (device);
	NMDeviceVlanPrivate *priv = NM_DEVICE_VLAN_GET_PRIVATE (device);
	NMSettingVlan *s_vlan = nm_connection_get_setting_vlan (connection);
	int ifindex = nm_device_get_ifindex (device);
	NMDevice *parent;
	const char *setting_parent, *new_parent;
	const NMPlatformLink *plink;
	const NMPObject *polnk;
	nm_auto_nmpobj NMPObject *polnk_ref = NULL;

	if (!s_vlan) {
		s_vlan = (NMSettingVlan *) nm_setting_vlan_new ();
		nm_connection_add_setting (connection, (NMSetting *) s_vlan);
	}

	polnk = nm_platform_link_get_lnk (NM_PLATFORM_GET, ifindex, NM_LINK_TYPE_VLAN, &plink);
	if (!polnk) {
		_LOGW (LOGD_VLAN, "failed to get VLAN interface info while updating connection.");
		return;
	}
	polnk_ref = nmp_object_ref ((NMPObject *) polnk);

	if (priv->vlan_id != polnk->lnk_vlan.id) {
		priv->vlan_id = polnk->lnk_vlan.id;
		g_object_notify (G_OBJECT (device), NM_DEVICE_VLAN_ID);
	}

	if (polnk->lnk_vlan.id != nm_setting_vlan_get_id (s_vlan))
		g_object_set (s_vlan, NM_SETTING_VLAN_ID, priv->vlan_id, NULL);

	if (plink->parent != NM_PLATFORM_LINK_OTHER_NETNS)
		parent = nm_manager_get_device_by_ifindex (nm_manager_get (), plink->parent);
	else
		parent = NULL;
	nm_device_vlan_set_parent (NM_DEVICE_VLAN (device), parent);

	/* Update parent in the connection; default to parent's interface name */
	if (parent) {
		new_parent = nm_device_get_iface (parent);
		setting_parent = nm_setting_vlan_get_parent (s_vlan);
		if (setting_parent && nm_utils_is_uuid (setting_parent)) {
			NMConnection *parent_connection;

			/* Don't change a parent specified by UUID if it's still valid */
			parent_connection = nm_connection_provider_get_connection_by_uuid (nm_connection_provider_get (), setting_parent);
			if (parent_connection && nm_device_check_connection_compatible (parent, parent_connection))
				new_parent = NULL;
		}
		if (new_parent)
			g_object_set (s_vlan, NM_SETTING_VLAN_PARENT, new_parent, NULL);
	} else
		g_object_set (s_vlan, NM_SETTING_VLAN_PARENT, NULL, NULL);

	if (polnk->lnk_vlan.flags != nm_setting_vlan_get_flags (s_vlan))
		g_object_set (s_vlan, NM_SETTING_VLAN_FLAGS, (NMVlanFlags) polnk->lnk_vlan.flags, NULL);

	_nm_setting_vlan_set_priorities (s_vlan, NM_VLAN_INGRESS_MAP,
	                                 polnk->_lnk_vlan.ingress_qos_map,
	                                 polnk->_lnk_vlan.n_ingress_qos_map);
	_nm_setting_vlan_set_priorities (s_vlan, NM_VLAN_EGRESS_MAP,
	                                 polnk->_lnk_vlan.egress_qos_map,
	                                 polnk->_lnk_vlan.n_egress_qos_map);
}

static NMActStageReturn
act_stage1_prepare (NMDevice *dev, NMDeviceStateReason *reason)
{
	NMActRequest *req;
	NMConnection *connection;
	NMSettingVlan *s_vlan;
	NMSettingWired *s_wired;
	const char *cloned_mac;
	NMActStageReturn ret;

	g_return_val_if_fail (reason != NULL, NM_ACT_STAGE_RETURN_FAILURE);

	ret = NM_DEVICE_CLASS (nm_device_vlan_parent_class)->act_stage1_prepare (dev, reason);
	if (ret != NM_ACT_STAGE_RETURN_SUCCESS)
		return ret;

	req = nm_device_get_act_request (dev);
	g_return_val_if_fail (req != NULL, NM_ACT_STAGE_RETURN_FAILURE);

	connection = nm_act_request_get_applied_connection (req);
	g_return_val_if_fail (connection != NULL, NM_ACT_STAGE_RETURN_FAILURE);

	s_wired = nm_connection_get_setting_wired (connection);
	if (s_wired) {
		/* Set device MAC address if the connection wants to change it */
		cloned_mac = nm_setting_wired_get_cloned_mac_address (s_wired);
		if (cloned_mac)
			nm_device_set_hw_addr (dev, cloned_mac, "set", LOGD_VLAN);
	}

	s_vlan = nm_connection_get_setting_vlan (connection);
	if (s_vlan) {
		gs_free NMVlanQosMapping *ingress_map = NULL;
		gs_free NMVlanQosMapping *egress_map = NULL;
		guint n_ingress_map = 0, n_egress_map = 0;

		_nm_setting_vlan_get_priorities (s_vlan,
		                                 NM_VLAN_INGRESS_MAP,
		                                 &ingress_map,
		                                 &n_ingress_map);
		_nm_setting_vlan_get_priorities (s_vlan,
		                                 NM_VLAN_EGRESS_MAP,
		                                 &egress_map,
		                                 &n_egress_map);

		nm_platform_link_vlan_change (NM_PLATFORM_GET,
		                              nm_device_get_ifindex (dev),
		                              NM_VLAN_FLAGS_ALL,
		                              nm_setting_vlan_get_flags (s_vlan),
		                              TRUE,
		                              ingress_map,
		                              n_ingress_map,
		                              TRUE,
		                              egress_map,
		                              n_egress_map);
	}

	return ret;
}

static void
ip4_config_pre_commit (NMDevice *device, NMIP4Config *config)
{
	NMConnection *connection;
	NMSettingWired *s_wired;
	guint32 mtu;

	connection = nm_device_get_applied_connection (device);
	g_assert (connection);

	s_wired = nm_connection_get_setting_wired (connection);
	if (s_wired) {
		mtu = nm_setting_wired_get_mtu (s_wired);
		if (mtu)
			nm_ip4_config_set_mtu (config, mtu, NM_IP_CONFIG_SOURCE_USER);
	}
}

static void
deactivate (NMDevice *device)
{
	/* Reset MAC address back to initial address */
	if (nm_device_get_initial_hw_address (device))
		nm_device_set_hw_addr (device, nm_device_get_initial_hw_address (device), "reset", LOGD_VLAN);
}

/******************************************************************/

static void
nm_device_vlan_init (NMDeviceVlan * self)
{
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMDeviceVlanPrivate *priv = NM_DEVICE_VLAN_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_PARENT:
		nm_utils_g_value_set_object_path (value, priv->parent);
		break;
	case PROP_VLAN_ID:
		g_value_set_uint (value, priv->vlan_id);
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
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
dispose (GObject *object)
{
	nm_device_vlan_set_parent (NM_DEVICE_VLAN (object), NULL);

	G_OBJECT_CLASS (nm_device_vlan_parent_class)->dispose (object);
}

static void
nm_device_vlan_class_init (NMDeviceVlanClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMDeviceClass *parent_class = NM_DEVICE_CLASS (klass);

	parent_class->connection_type = NM_SETTING_VLAN_SETTING_NAME;

	g_type_class_add_private (object_class, sizeof (NMDeviceVlanPrivate));

	/* virtual methods */
	object_class->get_property = get_property;
	object_class->set_property = set_property;
	object_class->dispose = dispose;

	parent_class->create_and_realize = create_and_realize;
	parent_class->realize = realize;
	parent_class->setup = setup;
	parent_class->get_generic_capabilities = get_generic_capabilities;
	parent_class->bring_up = bring_up;
	parent_class->act_stage1_prepare = act_stage1_prepare;
	parent_class->ip4_config_pre_commit = ip4_config_pre_commit;
	parent_class->deactivate = deactivate;
	parent_class->is_available = is_available;
	parent_class->component_added = component_added;

	parent_class->check_connection_compatible = check_connection_compatible;
	parent_class->complete_connection = complete_connection;
	parent_class->update_connection = update_connection;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_PARENT,
		 g_param_spec_string (NM_DEVICE_VLAN_PARENT, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));
	g_object_class_install_property
		(object_class, PROP_VLAN_ID,
		 g_param_spec_uint (NM_DEVICE_VLAN_ID, "", "",
		                    0, 4095, 0,
		                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	nm_exported_object_class_add_interface (NM_EXPORTED_OBJECT_CLASS (klass),
	                                        NMDBUS_TYPE_DEVICE_VLAN_SKELETON,
	                                        NULL);
}

/*************************************************************/

#define NM_TYPE_VLAN_FACTORY (nm_vlan_factory_get_type ())
#define NM_VLAN_FACTORY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_VLAN_FACTORY, NMVlanFactory))

static NMDevice *
create_device (NMDeviceFactory *factory,
               const char *iface,
               NMPlatformLink *plink,
               NMConnection *connection,
               gboolean *out_ignore)
{
	return (NMDevice *) g_object_new (NM_TYPE_DEVICE_VLAN,
	                                  NM_DEVICE_IFACE, iface,
	                                  NM_DEVICE_DRIVER, "8021q",
	                                  NM_DEVICE_TYPE_DESC, "VLAN",
	                                  NM_DEVICE_DEVICE_TYPE, NM_DEVICE_TYPE_VLAN,
	                                  NULL);
}

static const char *
get_connection_parent (NMDeviceFactory *factory, NMConnection *connection)
{
	NMSettingVlan *s_vlan;
	NMSettingWired *s_wired;
	const char *parent = NULL;

	g_return_val_if_fail (nm_connection_is_type (connection, NM_SETTING_VLAN_SETTING_NAME), NULL);

	s_vlan = nm_connection_get_setting_vlan (connection);
	g_assert (s_vlan);

	parent = nm_setting_vlan_get_parent (s_vlan);
	if (parent)
		return parent;

	/* Try the hardware address from the VLAN connection's hardware setting */
	s_wired = nm_connection_get_setting_wired (connection);
	if (s_wired)
		return nm_setting_wired_get_mac_address (s_wired);

	return NULL;
}

static char *
get_virtual_iface_name (NMDeviceFactory *factory,
                        NMConnection *connection,
                        const char *parent_iface)
{
	const char *ifname;
	NMSettingVlan *s_vlan;

	g_return_val_if_fail (nm_connection_is_type (connection, NM_SETTING_VLAN_SETTING_NAME), NULL);

	s_vlan = nm_connection_get_setting_vlan (connection);
	g_assert (s_vlan);

	if (!parent_iface)
		return NULL;

	ifname = nm_connection_get_interface_name (connection);
	if (ifname)
		return g_strdup (ifname);

	/* If the connection doesn't specify the interface name for the VLAN
	 * device, we create one for it using the VLAN ID and the parent
	 * interface's name.
	 */
	return nm_utils_new_vlan_name (parent_iface, nm_setting_vlan_get_id (s_vlan));
}

NM_DEVICE_FACTORY_DEFINE_INTERNAL (VLAN, Vlan, vlan,
	NM_DEVICE_FACTORY_DECLARE_LINK_TYPES    (NM_LINK_TYPE_VLAN)
	NM_DEVICE_FACTORY_DECLARE_SETTING_TYPES (NM_SETTING_VLAN_SETTING_NAME),
	factory_iface->create_device = create_device;
	factory_iface->get_connection_parent = get_connection_parent;
	factory_iface->get_virtual_iface_name = get_virtual_iface_name;
	)

