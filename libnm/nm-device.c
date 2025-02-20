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
 * Copyright 2007 - 2008 Novell, Inc.
 * Copyright 2007 - 2012 Red Hat, Inc.
 */

#include "config.h"

#include <string.h>

#include <gudev/gudev.h>

#include "nm-default.h"
#include "nm-dbus-interface.h"
#include "nm-active-connection.h"
#include "nm-device-ethernet.h"
#include "nm-device-adsl.h"
#include "nm-device-wifi.h"
#include "nm-device-modem.h"
#include "nm-device-bt.h"
#include "nm-device-olpc-mesh.h"
#include "nm-device-wimax.h"
#include "nm-device-infiniband.h"
#include "nm-device-bond.h"
#include "nm-device-team.h"
#include "nm-device-bridge.h"
#include "nm-device-vlan.h"
#include "nm-device-generic.h"
#include "nm-device.h"
#include "nm-device-private.h"
#include "nm-dhcp4-config.h"
#include "nm-dhcp6-config.h"
#include "nm-ip4-config.h"
#include "nm-ip6-config.h"
#include "nm-object-private.h"
#include "nm-object-cache.h"
#include "nm-remote-connection.h"
#include "nm-core-internal.h"
#include "nm-utils.h"
#include "nm-dbus-helpers.h"
#include "nm-setting-connection.h"
#include "nm-macros-internal.h"

#include "nmdbus-device.h"

static GType _nm_device_decide_type (GVariant *value);
gboolean connection_compatible (NMDevice *device, NMConnection *connection, GError **error);
static NMLldpNeighbor *nm_lldp_neighbor_dup (NMLldpNeighbor *neighbor);

G_DEFINE_TYPE_WITH_CODE (NMDevice, nm_device, NM_TYPE_OBJECT,
                         _nm_object_register_type_func (g_define_type_id,
                                                        _nm_device_decide_type,
                                                        NM_DBUS_INTERFACE_DEVICE,
                                                        "DeviceType");
                         )

#define NM_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE, NMDevicePrivate))

typedef struct {
	NMDBusDevice *proxy;

	char *iface;
	char *ip_iface;
	NMDeviceType device_type;
	char *udi;
	char *driver;
	char *driver_version;
	char *firmware_version;
	char *type_description;
	NMMetered metered;
	NMDeviceCapabilities capabilities;
	gboolean managed;
	gboolean firmware_missing;
	gboolean nm_plugin_missing;
	gboolean autoconnect;
	NMIPConfig *ip4_config;
	NMDhcpConfig *dhcp4_config;
	NMIPConfig *ip6_config;
	NMDhcpConfig *dhcp6_config;
	NMDeviceState state;
	NMDeviceState last_seen_state;
	NMDeviceStateReason reason;

	NMActiveConnection *active_connection;
	GPtrArray *available_connections;

	GUdevClient *client;
	char *product, *short_product;
	char *vendor, *short_vendor;
	char *description, *bus_name;

	char *physical_port_id;
	guint32 mtu;
	GPtrArray *lldp_neighbors;
} NMDevicePrivate;

enum {
	PROP_0,
	PROP_INTERFACE,
	PROP_UDI,
	PROP_DRIVER,
	PROP_DRIVER_VERSION,
	PROP_FIRMWARE_VERSION,
	PROP_CAPABILITIES,
	PROP_MANAGED,
	PROP_AUTOCONNECT,
	PROP_FIRMWARE_MISSING,
	PROP_NM_PLUGIN_MISSING,
	PROP_IP4_CONFIG,
	PROP_DHCP4_CONFIG,
	PROP_IP6_CONFIG,
	PROP_STATE,
	PROP_STATE_REASON,
	PROP_PRODUCT,
	PROP_VENDOR,
	PROP_DHCP6_CONFIG,
	PROP_IP_INTERFACE,
	PROP_DEVICE_TYPE,
	PROP_ACTIVE_CONNECTION,
	PROP_AVAILABLE_CONNECTIONS,
	PROP_PHYSICAL_PORT_ID,
	PROP_MTU,
	PROP_METERED,
	PROP_LLDP_NEIGHBORS,

	LAST_PROP
};

enum {
	STATE_CHANGED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _NMLldpNeighbor {
	guint refcount;
	GHashTable *attrs;
};

G_DEFINE_BOXED_TYPE (NMLldpNeighbor, nm_lldp_neighbor, nm_lldp_neighbor_dup, nm_lldp_neighbor_unref)

static void
nm_device_init (NMDevice *device)
{
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (device);

	priv->state = NM_DEVICE_STATE_UNKNOWN;
	priv->reason = NM_DEVICE_STATE_REASON_NONE;
	priv->lldp_neighbors = g_ptr_array_new ();
}

static gboolean
demarshal_state_reason (NMObject *object, GParamSpec *pspec, GVariant *value, gpointer field)
{
	guint32 *reason_field = field;

	g_variant_get (value, "(uu)", NULL, reason_field);
	_nm_object_queue_notify (object, NM_DEVICE_STATE_REASON);
	return TRUE;
}

static gboolean
demarshal_lldp_neighbors (NMObject *object, GParamSpec *pspec, GVariant *value, gpointer field)
{
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (object);
	GVariantIter iter, attrs_iter;
	GVariant *variant, *attr_variant;
	const char *attr_name;

	g_return_val_if_fail (g_variant_is_of_type (value, G_VARIANT_TYPE ("aa{sv}")), FALSE);

	g_ptr_array_unref (priv->lldp_neighbors);
	priv->lldp_neighbors = g_ptr_array_new_with_free_func ((GDestroyNotify) nm_lldp_neighbor_unref);
	g_variant_iter_init (&iter, value);

	while (g_variant_iter_next (&iter, "@a{sv}", &variant)) {
		NMLldpNeighbor *neigh;

		neigh = nm_lldp_neighbor_new ();
		g_variant_iter_init (&attrs_iter, variant);

		while (g_variant_iter_next (&attrs_iter, "{&sv}", &attr_name, &attr_variant))
			g_hash_table_insert (neigh->attrs, g_strdup (attr_name), attr_variant);

		g_variant_unref (variant);
		g_ptr_array_add (priv->lldp_neighbors, neigh);
	}

	_nm_object_queue_notify (object, NM_DEVICE_LLDP_NEIGHBORS);

	return TRUE;
}

static void
device_state_changed (NMDBusDevice *proxy,
                      guint new_state,
                      guint old_state,
                      guint reason,
                      gpointer user_data);

static void
init_dbus (NMObject *object)
{
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (object);
	const NMPropertiesInfo property_info[] = {
		{ NM_DEVICE_UDI,               &priv->udi },
		{ NM_DEVICE_INTERFACE,         &priv->iface },
		{ NM_DEVICE_IP_INTERFACE,      &priv->ip_iface },
		{ NM_DEVICE_DRIVER,            &priv->driver },
		{ NM_DEVICE_DRIVER_VERSION,    &priv->driver_version },
		{ NM_DEVICE_FIRMWARE_VERSION,  &priv->firmware_version },
		{ NM_DEVICE_CAPABILITIES,      &priv->capabilities },
		{ NM_DEVICE_MANAGED,           &priv->managed },
		{ NM_DEVICE_AUTOCONNECT,       &priv->autoconnect },
		{ NM_DEVICE_FIRMWARE_MISSING,  &priv->firmware_missing },
		{ NM_DEVICE_NM_PLUGIN_MISSING, &priv->nm_plugin_missing },
		{ NM_DEVICE_IP4_CONFIG,        &priv->ip4_config, NULL, NM_TYPE_IP4_CONFIG },
		{ NM_DEVICE_DHCP4_CONFIG,      &priv->dhcp4_config, NULL, NM_TYPE_DHCP4_CONFIG },
		{ NM_DEVICE_IP6_CONFIG,        &priv->ip6_config, NULL, NM_TYPE_IP6_CONFIG },
		{ NM_DEVICE_DHCP6_CONFIG,      &priv->dhcp6_config, NULL, NM_TYPE_DHCP6_CONFIG },
		{ NM_DEVICE_STATE,             &priv->state },
		{ NM_DEVICE_STATE_REASON,      &priv->reason, demarshal_state_reason },
		{ NM_DEVICE_ACTIVE_CONNECTION, &priv->active_connection, NULL, NM_TYPE_ACTIVE_CONNECTION },
		{ NM_DEVICE_AVAILABLE_CONNECTIONS, &priv->available_connections, NULL, NM_TYPE_REMOTE_CONNECTION },
		{ NM_DEVICE_PHYSICAL_PORT_ID,  &priv->physical_port_id },
		{ NM_DEVICE_MTU,               &priv->mtu },
		{ NM_DEVICE_METERED,           &priv->metered },
		{ NM_DEVICE_LLDP_NEIGHBORS,    &priv->lldp_neighbors, demarshal_lldp_neighbors },

		/* Properties that exist in D-Bus but that we don't track */
		{ "ip4-address", NULL },
		{ "device-type", NULL },

		{ NULL },
	};

	NM_OBJECT_CLASS (nm_device_parent_class)->init_dbus (object);

	priv->proxy = NMDBUS_DEVICE (_nm_object_get_proxy (object, NM_DBUS_INTERFACE_DEVICE));
	_nm_object_register_properties (object,
	                                NM_DBUS_INTERFACE_DEVICE,
	                                property_info);

	g_signal_connect (priv->proxy, "state-changed",
	                  G_CALLBACK (device_state_changed), object);
}

typedef struct {
	NMDeviceState old_state;
	NMDeviceState new_state;
	NMDeviceStateReason reason;
} StateChangeData;

static void
device_state_change_reloaded (GObject *object,
                              GAsyncResult *result,
                              gpointer user_data)
{
	NMDevice *self = NM_DEVICE (object);
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (self);
	StateChangeData *data = user_data;
	NMDeviceState old_state = data->old_state;
	NMDeviceState new_state = data->new_state;
	NMDeviceStateReason reason = data->reason;

	g_slice_free (StateChangeData, data);

	_nm_object_reload_properties_finish (NM_OBJECT (object), result, NULL);

	/* If the device changes state several times in rapid succession, then we'll
	 * queue several reload_properties() calls, and there's no guarantee that
	 * they'll finish in the right order. In that case, only emit the signal
	 * for the last one.
	 */
	if (priv->last_seen_state != new_state)
		return;

	/* Ensure that nm_device_get_state() will return the right value even if
	 * we haven't processed the corresponding PropertiesChanged yet.
	 */
	priv->state = new_state;

	g_signal_emit (self, signals[STATE_CHANGED], 0,
	               new_state, old_state, reason);
}

static void
device_state_changed (NMDBusDevice *proxy,
                      guint new_state,
                      guint old_state,
                      guint reason,
                      gpointer user_data)
{
	NMDevice *self = NM_DEVICE (user_data);
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (self);
	StateChangeData *data;

	if (old_state == new_state)
		return;

	/* Our object-valued properties (eg, ip4_config) will still
	 * have their old values at this point, because NMObject is
	 * in the process of asynchronously reading the new values.
	 * Wait for that to finish before emitting the signal.
	 */
	priv->last_seen_state = new_state;

	data = g_slice_new (StateChangeData);
	data->old_state = old_state;
	data->new_state = new_state;
	data->reason = reason;
	_nm_object_reload_properties_async (NM_OBJECT (user_data),
	                                    NULL,
	                                    device_state_change_reloaded,
	                                    data);
}

static GType
_nm_device_gtype_from_dtype (NMDeviceType dtype)
{
	switch (dtype) {
	case NM_DEVICE_TYPE_ETHERNET:
		return NM_TYPE_DEVICE_ETHERNET;
	case NM_DEVICE_TYPE_WIFI:
		return NM_TYPE_DEVICE_WIFI;
	case NM_DEVICE_TYPE_MODEM:
		return NM_TYPE_DEVICE_MODEM;
	case NM_DEVICE_TYPE_BT:
		return NM_TYPE_DEVICE_BT;
	case NM_DEVICE_TYPE_ADSL:
		return NM_TYPE_DEVICE_ADSL;
	case NM_DEVICE_TYPE_OLPC_MESH:
		return NM_TYPE_DEVICE_OLPC_MESH;
	case NM_DEVICE_TYPE_WIMAX:
		return NM_TYPE_DEVICE_WIMAX;
	case NM_DEVICE_TYPE_INFINIBAND:
		return NM_TYPE_DEVICE_INFINIBAND;
	case NM_DEVICE_TYPE_BOND:
		return NM_TYPE_DEVICE_BOND;
	case NM_DEVICE_TYPE_TEAM:
		return NM_TYPE_DEVICE_TEAM;
	case NM_DEVICE_TYPE_BRIDGE:
		return NM_TYPE_DEVICE_BRIDGE;
	case NM_DEVICE_TYPE_VLAN:
		return NM_TYPE_DEVICE_VLAN;
	case NM_DEVICE_TYPE_GENERIC:
		return NM_TYPE_DEVICE_GENERIC;
	default:
		g_warning ("Unknown device type %d", dtype);
		return G_TYPE_INVALID;
	}
}

static void
constructed (GObject *object)
{
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (object);

	G_OBJECT_CLASS (nm_device_parent_class)->constructed (object);

	/* Catch failure of subclasses to call _nm_device_set_device_type() */
	g_warn_if_fail (priv->device_type != NM_DEVICE_TYPE_UNKNOWN);
	/* Catch a subclass setting the wrong type */
	g_warn_if_fail (G_OBJECT_TYPE (object) == _nm_device_gtype_from_dtype (priv->device_type));
}

static void
dispose (GObject *object)
{
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (object);

	g_clear_object (&priv->ip4_config);
	g_clear_object (&priv->dhcp4_config);
	g_clear_object (&priv->ip6_config);
	g_clear_object (&priv->dhcp6_config);
	g_clear_object (&priv->client);
	g_clear_object (&priv->active_connection);

	g_clear_pointer (&priv->available_connections, g_ptr_array_unref);
	g_clear_pointer (&priv->lldp_neighbors, g_ptr_array_unref);

	G_OBJECT_CLASS (nm_device_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (object);

	g_free (priv->iface);
	g_free (priv->ip_iface);
	g_free (priv->udi);
	g_free (priv->driver);
	g_free (priv->driver_version);
	g_free (priv->firmware_version);
	g_free (priv->product);
	g_free (priv->short_product);
	g_free (priv->vendor);
	g_free (priv->short_vendor);
	g_free (priv->description);
	g_free (priv->bus_name);
	g_free (priv->type_description);
	g_free (priv->physical_port_id);

	G_OBJECT_CLASS (nm_device_parent_class)->finalize (object);
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
	NMDevice *device = NM_DEVICE (object);

	switch (prop_id) {
	case PROP_DEVICE_TYPE:
		g_value_set_enum (value, nm_device_get_device_type (device));
		break;
	case PROP_UDI:
		g_value_set_string (value, nm_device_get_udi (device));
		break;
	case PROP_INTERFACE:
		g_value_set_string (value, nm_device_get_iface (device));
		break;
	case PROP_IP_INTERFACE:
		g_value_set_string (value, nm_device_get_ip_iface (device));
		break;
	case PROP_DRIVER:
		g_value_set_string (value, nm_device_get_driver (device));
		break;
	case PROP_DRIVER_VERSION:
		g_value_set_string (value, nm_device_get_driver_version (device));
		break;
	case PROP_FIRMWARE_VERSION:
		g_value_set_string (value, nm_device_get_firmware_version (device));
		break;
	case PROP_CAPABILITIES:
		g_value_set_flags (value, nm_device_get_capabilities (device));
		break;
	case PROP_MANAGED:
		g_value_set_boolean (value, nm_device_get_managed (device));
		break;
	case PROP_AUTOCONNECT:
		g_value_set_boolean (value, nm_device_get_autoconnect (device));
		break;
	case PROP_FIRMWARE_MISSING:
		g_value_set_boolean (value, nm_device_get_firmware_missing (device));
		break;
	case PROP_NM_PLUGIN_MISSING:
		g_value_set_boolean (value, nm_device_get_nm_plugin_missing (device));
		break;
	case PROP_IP4_CONFIG:
		g_value_set_object (value, nm_device_get_ip4_config (device));
		break;
	case PROP_DHCP4_CONFIG:
		g_value_set_object (value, nm_device_get_dhcp4_config (device));
		break;
	case PROP_IP6_CONFIG:
		g_value_set_object (value, nm_device_get_ip6_config (device));
		break;
	case PROP_DHCP6_CONFIG:
		g_value_set_object (value, nm_device_get_dhcp6_config (device));
		break;
	case PROP_STATE:
		g_value_set_enum (value, nm_device_get_state (device));
		break;
	case PROP_STATE_REASON:
		g_value_set_uint (value, nm_device_get_state_reason (device));
		break;
	case PROP_ACTIVE_CONNECTION:
		g_value_set_object (value, nm_device_get_active_connection (device));
		break;
	case PROP_AVAILABLE_CONNECTIONS:
		g_value_take_boxed (value, _nm_utils_copy_object_array (nm_device_get_available_connections (device)));
		break;
	case PROP_PRODUCT:
		g_value_set_string (value, nm_device_get_product (device));
		break;
	case PROP_VENDOR:
		g_value_set_string (value, nm_device_get_vendor (device));
		break;
	case PROP_PHYSICAL_PORT_ID:
		g_value_set_string (value, nm_device_get_physical_port_id (device));
		break;
	case PROP_MTU:
		g_value_set_uint (value, nm_device_get_mtu (device));
		break;
	case PROP_METERED:
		g_value_set_uint (value, nm_device_get_metered (device));
		break;
	case PROP_LLDP_NEIGHBORS:
		g_value_set_boxed (value, nm_device_get_lldp_neighbors (device));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
	NMDevice *self = NM_DEVICE (object);
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (self);
	gboolean b;

	switch (prop_id) {
	case PROP_DEVICE_TYPE:
		/* Construct only */
		priv->device_type = g_value_get_enum (value);
		break;
	case PROP_MANAGED:
		b = g_value_get_boolean (value);
		if (priv->managed != b)
			nm_device_set_managed (NM_DEVICE (object), b);
		break;
	case PROP_AUTOCONNECT:
		b = g_value_get_boolean (value);
		if (priv->autoconnect != b)
			nm_device_set_autoconnect (NM_DEVICE (object), b);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_device_class_init (NMDeviceClass *device_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (device_class);
	NMObjectClass *nm_object_class = NM_OBJECT_CLASS (device_class);

	g_type_class_add_private (device_class, sizeof (NMDevicePrivate));

	_nm_object_class_add_interface (nm_object_class, NM_DBUS_INTERFACE_DEVICE);
	_nm_dbus_register_proxy_type (NM_DBUS_INTERFACE_DEVICE, NMDBUS_TYPE_DEVICE_PROXY);

	/* virtual methods */
	object_class->constructed = constructed;
	object_class->get_property = get_property;
	object_class->set_property = set_property;
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	nm_object_class->init_dbus = init_dbus;

	device_class->connection_compatible = connection_compatible;

	/* properties */

	/**
	 * NMDevice:interface:
	 *
	 * The interface of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_INTERFACE,
		 g_param_spec_string (NM_DEVICE_INTERFACE, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:ip-interface:
	 *
	 * The IP interface of the device which should be used for all IP-related
	 * operations like addressing and routing.
	 **/
	g_object_class_install_property
		(object_class, PROP_IP_INTERFACE,
		 g_param_spec_string (NM_DEVICE_IP_INTERFACE, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:device-type:
	 *
	 * The numeric type of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_DEVICE_TYPE,
		 g_param_spec_enum (NM_DEVICE_DEVICE_TYPE, "", "",
		                    NM_TYPE_DEVICE_TYPE,
		                    NM_DEVICE_TYPE_UNKNOWN,
		                    G_PARAM_READABLE |
		                    G_PARAM_STATIC_STRINGS));
	/**
	 * NMDevice:udi:
	 *
	 * An operating-system specific device hardware identifier; this is not
	 * unique to a specific hardware device across reboots or hotplugs.  It
	 * is an opaque string which for some device types (Bluetooth, Modem)
	 * contains an identifier provided by the underlying hardware service daemon
	 * such as Bluez or ModemManager, and clients can use this property to
	 * request more information about the device from those services.
	 **/
	g_object_class_install_property
		(object_class, PROP_UDI,
		 g_param_spec_string (NM_DEVICE_UDI, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:driver:
	 *
	 * The driver of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_DRIVER,
		 g_param_spec_string (NM_DEVICE_DRIVER, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:driver-version:
	 *
	 * The version of the device driver.
	 **/
	g_object_class_install_property
		(object_class, PROP_DRIVER_VERSION,
		 g_param_spec_string (NM_DEVICE_DRIVER_VERSION, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:firmware-version:
	 *
	 * The firmware version of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_FIRMWARE_VERSION,
		 g_param_spec_string (NM_DEVICE_FIRMWARE_VERSION, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:capabilities:
	 *
	 * The capabilities of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_CAPABILITIES,
		 g_param_spec_flags (NM_DEVICE_CAPABILITIES, "", "",
		                     NM_TYPE_DEVICE_CAPABILITIES,
		                     NM_DEVICE_CAP_NONE,
		                     G_PARAM_READABLE |
		                     G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:managed:
	 *
	 * Whether the device is managed by NetworkManager.
	 **/
	g_object_class_install_property
		(object_class, PROP_MANAGED,
		 g_param_spec_boolean (NM_DEVICE_MANAGED, "", "",
		                       FALSE,
		                       G_PARAM_READABLE |
		                       G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:autoconnect:
	 *
	 * Whether the device can auto-activate a connection.
	 **/
	g_object_class_install_property
		(object_class, PROP_AUTOCONNECT,
		 g_param_spec_boolean (NM_DEVICE_AUTOCONNECT, "", "",
		                       TRUE,
		                       G_PARAM_READWRITE |
		                       G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:firmware-missing:
	 *
	 * When %TRUE indicates the device is likely missing firmware required
	 * for its operation.
	 **/
	g_object_class_install_property
		(object_class, PROP_FIRMWARE_MISSING,
		 g_param_spec_boolean (NM_DEVICE_FIRMWARE_MISSING, "", "",
		                       FALSE,
		                       G_PARAM_READABLE |
		                       G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:nm-plugin-missing:
	 *
	 * When %TRUE indicates that the NetworkManager plugin for the device
	 * is not installed.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_NM_PLUGIN_MISSING,
		 g_param_spec_boolean (NM_DEVICE_NM_PLUGIN_MISSING, "", "",
		                       FALSE,
		                       G_PARAM_READABLE |
		                       G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:ip4-config:
	 *
	 * The #NMIP4Config of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_IP4_CONFIG,
		 g_param_spec_object (NM_DEVICE_IP4_CONFIG, "", "",
		                      NM_TYPE_IP_CONFIG,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:dhcp4-config:
	 *
	 * The IPv4 #NMDhcpConfig of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_DHCP4_CONFIG,
		 g_param_spec_object (NM_DEVICE_DHCP4_CONFIG, "", "",
		                      NM_TYPE_DHCP_CONFIG,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:ip6-config:
	 *
	 * The IPv6 #NMIPConfig of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_IP6_CONFIG,
		 g_param_spec_object (NM_DEVICE_IP6_CONFIG, "", "",
		                      NM_TYPE_IP_CONFIG,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:dhcp6-config:
	 *
	 * The IPv6 #NMDhcpConfig of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_DHCP6_CONFIG,
		 g_param_spec_object (NM_DEVICE_DHCP6_CONFIG, "", "",
		                      NM_TYPE_DHCP_CONFIG,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:state:
	 *
	 * The state of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_STATE,
		 g_param_spec_enum (NM_DEVICE_STATE, "", "",
		                    NM_TYPE_DEVICE_STATE,
		                    NM_DEVICE_STATE_UNKNOWN,
		                    G_PARAM_READABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:state-reason:
	 *
	 * The reason for the device state.
	 **/
	g_object_class_install_property
		(object_class, PROP_STATE_REASON,
		 g_param_spec_uint (NM_DEVICE_STATE_REASON, "", "",
		                    0, G_MAXUINT32, 0,
		                    G_PARAM_READABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:active-connection:
	 *
	 * The #NMActiveConnection object that "owns" this device during activation.
	 **/
	g_object_class_install_property
		(object_class, PROP_ACTIVE_CONNECTION,
		 g_param_spec_object (NM_DEVICE_ACTIVE_CONNECTION, "", "",
		                      NM_TYPE_ACTIVE_CONNECTION,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:available-connections:
	 *
	 * The available connections of the device
	 *
	 * Element-type: NMRemoteConnection
	 **/
	g_object_class_install_property
		(object_class, PROP_AVAILABLE_CONNECTIONS,
		 g_param_spec_boxed (NM_DEVICE_AVAILABLE_CONNECTIONS, "", "",
		                     G_TYPE_PTR_ARRAY,
		                     G_PARAM_READABLE |
		                     G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:vendor:
	 *
	 * The vendor string of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_VENDOR,
		 g_param_spec_string (NM_DEVICE_VENDOR, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:product:
	 *
	 * The product string of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_PRODUCT,
		 g_param_spec_string (NM_DEVICE_PRODUCT, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:physical-port-id:
	 *
	 * The physical port ID of the device. (See
	 * nm_device_get_physical_port_id().)
	 **/
	g_object_class_install_property
		(object_class, PROP_PHYSICAL_PORT_ID,
		 g_param_spec_string (NM_DEVICE_PHYSICAL_PORT_ID, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:mtu:
	 *
	 * The MTU of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_MTU,
		 g_param_spec_uint (NM_DEVICE_MTU, "", "",
		                    0, G_MAXUINT32, 1500,
		                    G_PARAM_READABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:metered:
	 *
	 * Whether the device is metered.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_METERED,
		 g_param_spec_uint (NM_DEVICE_METERED, "", "",
		                    0, G_MAXUINT32, NM_METERED_UNKNOWN,
		                    G_PARAM_READABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMDevice:lldp-neighbors:
	 *
	 * The LLDP neighbors.
	 **/
	g_object_class_install_property
	    (object_class, PROP_LLDP_NEIGHBORS,
	     g_param_spec_boxed (NM_DEVICE_LLDP_NEIGHBORS, "", "",
	                         G_TYPE_PTR_ARRAY,
	                         G_PARAM_READABLE |
	                         G_PARAM_STATIC_STRINGS));

	/* signals */

	/**
	 * NMDevice::state-changed:
	 * @device: the device object that received the signal
	 * @new_state: the new state of the device
	 * @old_state: the previous state of the device
	 * @reason: the reason describing the state change
	 *
	 * Notifies the state change of a #NMDevice.
	 **/
	signals[STATE_CHANGED] =
		g_signal_new ("state-changed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (NMDeviceClass, state_changed),
		              NULL, NULL, NULL,
		              G_TYPE_NONE, 3,
		              G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
}

/**
 * _nm_device_set_device_type:
 * @device: the device
 * @dtype: the NM device type
 *
 * Sets the NM device type if it wasn't set during construction.  INTERNAL
 * ONLY METHOD.
 **/
void
_nm_device_set_device_type (NMDevice *device, NMDeviceType dtype)
{
	NMDevicePrivate *priv;

	g_return_if_fail (device != NULL);
	g_return_if_fail (dtype != NM_DEVICE_TYPE_UNKNOWN);

	priv = NM_DEVICE_GET_PRIVATE (device);
	if (priv->device_type == NM_DEVICE_TYPE_UNKNOWN)
		priv->device_type = dtype;
	else
		g_warn_if_fail (dtype == priv->device_type);
}

static GType
_nm_device_decide_type (GVariant *value)
{
	return _nm_device_gtype_from_dtype (g_variant_get_uint32 (value));
}

/**
 * nm_device_get_iface:
 * @device: a #NMDevice
 *
 * Gets the interface name of the #NMDevice.
 *
 * Returns: the interface of the device. This is the internal string used by the
 * device, and must not be modified.
 **/
const char *
nm_device_get_iface (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->iface;
}

/**
 * nm_device_get_ip_iface:
 * @device: a #NMDevice
 *
 * Gets the IP interface name of the #NMDevice over which IP traffic flows
 * when the device is in the ACTIVATED state.
 *
 * Returns: the IP traffic interface of the device. This is the internal string
 * used by the device, and must not be modified.
 **/
const char *
nm_device_get_ip_iface (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->ip_iface;
}

/**
 * nm_device_get_device_type:
 * @device: a #NMDevice
 *
 * Returns the numeric type of the #NMDevice, ie Ethernet, Wi-Fi, etc.
 *
 * Returns: the device type
 **/
NMDeviceType
nm_device_get_device_type (NMDevice *self)
{
	g_return_val_if_fail (NM_IS_DEVICE (self), NM_DEVICE_TYPE_UNKNOWN);

	return NM_DEVICE_GET_PRIVATE (self)->device_type;
}

/**
 * nm_device_get_udi:
 * @device: a #NMDevice
 *
 * Gets the Unique Device Identifier of the #NMDevice.
 *
 * Returns: the Unique Device Identifier of the device.  This identifier may be
 * used to gather more information about the device from various operating
 * system services like udev or sysfs.
 **/
const char *
nm_device_get_udi (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->udi;
}

/**
 * nm_device_get_driver:
 * @device: a #NMDevice
 *
 * Gets the driver of the #NMDevice.
 *
 * Returns: the driver of the device. This is the internal string used by the
 * device, and must not be modified.
 **/
const char *
nm_device_get_driver (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->driver;
}

/**
 * nm_device_get_driver_version:
 * @device: a #NMDevice
 *
 * Gets the driver version of the #NMDevice.
 *
 * Returns: the version of the device driver. This is the internal string used by the
 * device, and must not be modified.
 **/
const char *
nm_device_get_driver_version (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->driver_version;
}

/**
 * nm_device_get_firmware_version:
 * @device: a #NMDevice
 *
 * Gets the firmware version of the #NMDevice.
 *
 * Returns: the firmware version of the device. This is the internal string used by the
 * device, and must not be modified.
 **/
const char *
nm_device_get_firmware_version (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->firmware_version;
}

/**
 * nm_device_get_type_description:
 * @device: a #NMDevice
 *
 * Gets a (non-localized) description of the type of device that
 * @device is.
 *
 * Returns: the type description of the device. This is the internal
 * string used by the device, and must not be modified.
 **/
const char *
nm_device_get_type_description (NMDevice *device)
{
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (device);
	const char *desc, *typename;

	/* BEWARE: this function should return the same value
	 * as nm_device_get_type_description() in nm-core. */

	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	if (priv->type_description)
		return priv->type_description;

	if (NM_DEVICE_GET_CLASS (device)->get_type_description) {
		desc = NM_DEVICE_GET_CLASS (device)->get_type_description (device);
		if (desc)
			return desc;
	}

	typename = G_OBJECT_TYPE_NAME (device);
	if (g_str_has_prefix (typename, "NMDevice"))
		typename += 8;
	priv->type_description = g_ascii_strdown (typename, -1);

	return priv->type_description;
}

/**
 * nm_device_get_hw_address:
 * @device: a #NMDevice
 *
 * Gets the current a hardware address (MAC) for the @device.
 *
 * Returns: the current MAC of the device, or %NULL.
 * This is the internal string used by the device, and must not be modified.
 **/
const char *
nm_device_get_hw_address (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	if (NM_DEVICE_GET_CLASS (device)->get_hw_address)
		return NM_DEVICE_GET_CLASS (device)->get_hw_address (device);

	return NULL;
}

/**
 * nm_device_get_capabilities:
 * @device: a #NMDevice
 *
 * Gets the device' capabilities.
 *
 * Returns: the capabilities
 **/
NMDeviceCapabilities
nm_device_get_capabilities (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), 0);

	return NM_DEVICE_GET_PRIVATE (device)->capabilities;
}

/**
 * nm_device_get_managed:
 * @device: a #NMDevice
 *
 * Whether the #NMDevice is managed by NetworkManager.
 *
 * Returns: %TRUE if the device is managed by NetworkManager
 **/
gboolean
nm_device_get_managed (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), 0);

	return NM_DEVICE_GET_PRIVATE (device)->managed;
}

/**
 * nm_device_set_managed:
 * @device: a #NMDevice
 * @managed: %TRUE to make the device managed by NetworkManager.
 *
 * Enables or disables management of  #NMDevice by NetworkManager.
 *
 * Since: 1.2
 **/
void
nm_device_set_managed (NMDevice *device, gboolean managed)
{
	g_return_if_fail (NM_IS_DEVICE (device));

	managed = !!managed;

	NM_DEVICE_GET_PRIVATE (device)->managed = managed;

	_nm_object_set_property (NM_OBJECT (device),
	                         NM_DBUS_INTERFACE_DEVICE,
	                         "Managed",
	                         "b", managed);
}

/**
 * nm_device_get_autoconnect:
 * @device: a #NMDevice
 *
 * Whether the #NMDevice can be autoconnected.
 *
 * Returns: %TRUE if the device is allowed to be autoconnected
 **/
gboolean
nm_device_get_autoconnect (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), FALSE);

	return NM_DEVICE_GET_PRIVATE (device)->autoconnect;
}

/**
 * nm_device_set_autoconnect:
 * @device: a #NMDevice
 * @autoconnect: %TRUE to enable autoconnecting
 *
 * Enables or disables automatic activation of the #NMDevice.
 **/
void
nm_device_set_autoconnect (NMDevice *device, gboolean autoconnect)
{
	g_return_if_fail (NM_IS_DEVICE (device));

	NM_DEVICE_GET_PRIVATE (device)->autoconnect = autoconnect;

	_nm_object_set_property (NM_OBJECT (device),
	                         NM_DBUS_INTERFACE_DEVICE,
	                         "Autoconnect",
	                         "b", autoconnect);
}

/**
 * nm_device_get_firmware_missing:
 * @device: a #NMDevice
 *
 * Indicates that firmware required for the device's operation is likely
 * to be missing.
 *
 * Returns: %TRUE if firmware required for the device's operation is likely
 * to be missing.
 **/
gboolean
nm_device_get_firmware_missing (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), 0);

	return NM_DEVICE_GET_PRIVATE (device)->firmware_missing;
}

/**
 * nm_device_get_nm_plugin_missing:
 * @device: a #NMDevice
 *
 * Indicates that the NetworkManager plugin for the device is not installed.
 *
 * Returns: %TRUE if the device plugin not installed.
 *
 * Since: 1.2
 **/
gboolean
nm_device_get_nm_plugin_missing (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), FALSE);

	return NM_DEVICE_GET_PRIVATE (device)->nm_plugin_missing;
}

/**
 * nm_device_get_ip4_config:
 * @device: a #NMDevice
 *
 * Gets the current IPv4 #NMIPConfig associated with the #NMDevice.
 *
 * You can alternatively use nm_active_connection_get_ip4_config(), which also
 * works with VPN connections.
 *
 * Returns: (transfer none): the IPv4 #NMIPConfig, or %NULL if the device is not
 * activated.
 **/
NMIPConfig *
nm_device_get_ip4_config (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->ip4_config;
}

/**
 * nm_device_get_dhcp4_config:
 * @device: a #NMDevice
 *
 * Gets the current IPv4 #NMDhcpConfig associated with the #NMDevice.
 *
 * You can alternatively use nm_active_connection_get_dhcp4_config(), which also
 * works with VPN connections.
 *
 * Returns: (transfer none): the IPv4 #NMDhcpConfig, or %NULL if the device is
 * not activated or not using DHCP.
 **/
NMDhcpConfig *
nm_device_get_dhcp4_config (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->dhcp4_config;
}

/**
 * nm_device_get_ip6_config:
 * @device: a #NMDevice
 *
 * Gets the current IPv6 #NMIPConfig associated with the #NMDevice.
 *
 * You can alternatively use nm_active_connection_get_ip6_config(), which also
 * works with VPN connections.
 *
 * Returns: (transfer none): the IPv6 #NMIPConfig or %NULL if the device is not activated.
 **/
NMIPConfig *
nm_device_get_ip6_config (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->ip6_config;
}

/**
 * nm_device_get_dhcp6_config:
 * @device: a #NMDevice
 *
 * Gets the current IPv6 #NMDhcpConfig associated with the #NMDevice.
 *
 * You can alternatively use nm_active_connection_get_dhcp6_config(), which also
 * works with VPN connections.
 *
 * Returns: (transfer none): the IPv6 #NMDhcpConfig, or %NULL if the device is
 * not activated or not using DHCPv6.
 **/
NMDhcpConfig *
nm_device_get_dhcp6_config (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->dhcp6_config;
}

/**
 * nm_device_get_state:
 * @device: a #NMDevice
 *
 * Gets the current #NMDevice state.
 *
 * Returns: the current device state
 **/
NMDeviceState
nm_device_get_state (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NM_DEVICE_STATE_UNKNOWN);

	return NM_DEVICE_GET_PRIVATE (device)->state;
}

/**
 * nm_device_get_state_reason:
 * @device: a #NMDevice
 *
 * Gets the reason for entering the current #NMDevice state.
 *
 * Returns: the reason for entering the current device state
 **/
NMDeviceStateReason
nm_device_get_state_reason (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NM_DEVICE_STATE_REASON_UNKNOWN);

	return NM_DEVICE_GET_PRIVATE (device)->reason;
}

/**
 * nm_device_get_active_connection:
 * @device: a #NMDevice
 *
 * Gets the #NMActiveConnection object which owns this device during activation.
 *
 * Returns: (transfer none): the #NMActiveConnection or %NULL if the device is
 * not part of an active connection
 **/
NMActiveConnection *
nm_device_get_active_connection (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->active_connection;
}

/**
 * nm_device_get_available_connections:
 * @device: a #NMDevice
 *
 * Gets the #NMRemoteConnections currently known to the daemon that could
 * be activated on @device.
 *
 * Returns: (element-type NMRemoteConnection): the #GPtrArray
 * containing #NMRemoteConnections. This is the internal copy used by
 * the connection, and must not be modified.
 **/
const GPtrArray *
nm_device_get_available_connections (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	return NM_DEVICE_GET_PRIVATE (device)->available_connections;
}

static inline guint8
hex2byte (const char *hex)
{
	int a, b;
	a = g_ascii_xdigit_value (*hex++);
	if (a < 0)
		return -1;
	b = g_ascii_xdigit_value (*hex++);
	if (b < 0)
		return -1;
	return (a << 4) | b;
}

static char *
get_decoded_property (GUdevDevice *device, const char *property)
{
	const char *orig, *p;
	char *unescaped, *n;
	guint len;

	p = orig = g_udev_device_get_property (device, property);
	if (!orig)
		return NULL;

	len = strlen (orig);
	n = unescaped = g_malloc0 (len + 1);
	while (*p) {
		if ((len >= 4) && (*p == '\\') && (*(p+1) == 'x')) {
			*n++ = (char) hex2byte (p + 2);
			p += 4;
			len -= 4;
		} else {
			*n++ = *p++;
			len--;
		}
	}

	return unescaped;
}

static gboolean
ensure_udev_client (NMDevice *device)
{
	static const char *const subsys[3] = { "net", "tty", NULL };
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (device);

	if (!priv->client)
		priv->client = g_udev_client_new (subsys);

	return priv->client != NULL;
}

static char *
_get_udev_property (NMDevice *device,
                    const char *enc_prop,  /* ID_XXX_ENC */
                    const char *db_prop)   /* ID_XXX_FROM_DATABASE */
{
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (device);
	GUdevDevice *udev_device = NULL, *tmpdev, *olddev;
	const char *ifname;
	guint32 count = 0;
	char *enc_value = NULL, *db_value = NULL;

	if (!ensure_udev_client (device))
		return NULL;

	ifname = nm_device_get_iface (device);
	if (!ifname)
		return NULL;

	udev_device = g_udev_client_query_by_subsystem_and_name (priv->client, "net", ifname);
	if (!udev_device)
		udev_device = g_udev_client_query_by_subsystem_and_name (priv->client, "tty", ifname);
	if (!udev_device)
		return NULL;

	/* Walk up the chain of the device and its parents a few steps to grab
	 * vendor and device ID information off it.
	 */

	/* Ref the device again because we have to unref it each iteration,
	 * as g_udev_device_get_parent() returns a ref-ed object.
	 */
	tmpdev = g_object_ref (udev_device);
	while ((count++ < 3) && tmpdev && !enc_value) {
		if (!enc_value)
			enc_value = get_decoded_property (tmpdev, enc_prop);
		if (!db_value)
			db_value = g_strdup (g_udev_device_get_property (tmpdev, db_prop));

		olddev = tmpdev;
		tmpdev = g_udev_device_get_parent (tmpdev);
		g_object_unref (olddev);
	}

	/* Unref the last device if we found what we needed before running out
	 * of parents.
	 */
	if (tmpdev)
		g_object_unref (tmpdev);

	/* Balance the initial g_udev_client_query_by_subsystem_and_name() */
	g_object_unref (udev_device);

	/* Prefer the encoded value which comes directly from the device
	 * over the hwdata database value.
	 */
	if (enc_value) {
		g_free (db_value);
		return enc_value;
	}

	return db_value;
}

/**
 * nm_device_get_product:
 * @device: a #NMDevice
 *
 * Gets the product string of the #NMDevice.
 *
 * Returns: the product name of the device. This is the internal string used by the
 * device, and must not be modified.
 **/
const char *
nm_device_get_product (NMDevice *device)
{
	NMDevicePrivate *priv;

	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	priv = NM_DEVICE_GET_PRIVATE (device);
	if (!priv->product) {
		priv->product = _get_udev_property (device, "ID_MODEL_ENC", "ID_MODEL_FROM_DATABASE");
		if (!priv->product) {
			/* Sometimes ID_PRODUCT_FROM_DATABASE is used? */
			priv->product = _get_udev_property (device, "ID_MODEL_ENC", "ID_PRODUCT_FROM_DATABASE");
		}
		_nm_object_queue_notify (NM_OBJECT (device), NM_DEVICE_PRODUCT);
	}
	return priv->product;
}

/**
 * nm_device_get_vendor:
 * @device: a #NMDevice
 *
 * Gets the vendor string of the #NMDevice.
 *
 * Returns: the vendor name of the device. This is the internal string used by the
 * device, and must not be modified.
 **/
const char *
nm_device_get_vendor (NMDevice *device)
{
	NMDevicePrivate *priv;

	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	priv = NM_DEVICE_GET_PRIVATE (device);
	if (!priv->vendor) {
		priv->vendor = _get_udev_property (device, "ID_VENDOR_ENC", "ID_VENDOR_FROM_DATABASE");
		_nm_object_queue_notify (NM_OBJECT (device), NM_DEVICE_VENDOR);
	}
	return priv->vendor;
}

static const char * const ignored_words[] = {
	"Semiconductor",
	"Components",
	"Corporation",
	"Communications",
	"Company",
	"Corp.",
	"Corp",
	"Co.",
	"Inc.",
	"Inc",
	"Incorporated",
	"Ltd.",
	"Limited.",
	"Intel?",
	"chipset",
	"adapter",
	"[hex]",
	"NDIS",
	"Module",
	NULL
};

static const char * const ignored_phrases[] = {
	"Multiprotocol MAC/baseband processor",
	"Wireless LAN Controller",
	"Wireless LAN Adapter",
	"Wireless Adapter",
	"Network Connection",
	"Wireless Cardbus Adapter",
	"Wireless CardBus Adapter",
	"54 Mbps Wireless PC Card",
	"Wireless PC Card",
	"Wireless PC",
	"PC Card with XJACK(r) Antenna",
	"Wireless cardbus",
	"Wireless LAN PC Card",
	"Technology Group Ltd.",
	"Communication S.p.A.",
	"Business Mobile Networks BV",
	"Mobile Broadband Minicard Composite Device",
	"Mobile Communications AB",
	"(PC-Suite Mode)",
	NULL
};

static char *
fixup_desc_string (const char *desc)
{
	char *p, *temp;
	char **words, **item;
	GString *str;
	int i;

	if (!desc)
		return NULL;

	p = temp = g_strdup (desc);
	while (*p) {
		if (*p == '_' || *p == ',')
			*p = ' ';
		p++;
	}

	/* Attempt to shorten ID by ignoring certain phrases */
	for (i = 0; ignored_phrases[i]; i++) {
		p = strstr (temp, ignored_phrases[i]);
		if (p) {
			guint32 ignored_len = strlen (ignored_phrases[i]);

			memmove (p, p + ignored_len, strlen (p + ignored_len) + 1); /* +1 for the \0 */
		}
	}

	/* Attempt to shorten ID by ignoring certain individual words */
	words = g_strsplit (temp, " ", 0);
	str = g_string_new_len (NULL, strlen (temp));
	g_free (temp);

	for (item = words; *item; item++) {
		gboolean ignore = FALSE;

		if (**item == '\0')
			continue;

		for (i = 0; ignored_words[i]; i++) {
			if (!strcmp (*item, ignored_words[i])) {
				ignore = TRUE;
				break;
			}
		}

		if (!ignore) {
			if (str->len)
				g_string_append_c (str, ' ');
			g_string_append (str, *item);
		}
	}
	g_strfreev (words);

	temp = str->str;
	g_string_free (str, FALSE);

	return temp;
}

static void
get_description (NMDevice *device)
{
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (device);
	const char *dev_product;
	const char *dev_vendor;
	char *pdown;
	char *vdown;
	GString *str;

	dev_product = nm_device_get_product (device);
	priv->short_product = fixup_desc_string (dev_product);

	dev_vendor = nm_device_get_vendor (device);
	priv->short_vendor = fixup_desc_string (dev_vendor);

	if (!dev_product || !dev_vendor) {
		priv->description = g_strdup (nm_device_get_iface (device));
		return;
	}

	str = g_string_new_len (NULL, strlen (priv->short_vendor) + strlen (priv->short_product) + 1);

	/* Another quick hack; if all of the fixed up vendor string
	 * is found in product, ignore the vendor.
	 */
	pdown = g_ascii_strdown (priv->short_product, -1);
	vdown = g_ascii_strdown (priv->short_vendor, -1);
	if (!strstr (pdown, vdown)) {
		g_string_append (str, priv->short_vendor);
		g_string_append_c (str, ' ');
	}
	g_free (pdown);
	g_free (vdown);

	g_string_append (str, priv->short_product);

	priv->description = g_string_free (str, FALSE);
}

static const char *
get_short_vendor (NMDevice *device)
{
	NMDevicePrivate *priv;

	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	priv = NM_DEVICE_GET_PRIVATE (device);

	if (!priv->description)
		get_description (device);

	return priv->short_vendor;
}

/**
 * nm_device_get_description:
 * @device: an #NMDevice
 *
 * Gets a description of @device, based on its vendor and product names.
 *
 * Returns: a description of @device. If either the vendor or the
 *   product name is unknown, this returns the interface name.
 */
const char *
nm_device_get_description (NMDevice *device)
{
	NMDevicePrivate *priv;

	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	priv = NM_DEVICE_GET_PRIVATE (device);

	if (!priv->description)
		get_description (device);

	return priv->description;
}

static const char *
get_type_name (NMDevice *device)
{
	switch (nm_device_get_device_type (device)) {
	case NM_DEVICE_TYPE_ETHERNET:
		return _("Ethernet");
	case NM_DEVICE_TYPE_WIFI:
		return _("Wi-Fi");
	case NM_DEVICE_TYPE_BT:
		return _("Bluetooth");
	case NM_DEVICE_TYPE_OLPC_MESH:
		return _("OLPC Mesh");
	case NM_DEVICE_TYPE_WIMAX:
		return _("WiMAX");
	case NM_DEVICE_TYPE_MODEM:
		return _("Mobile Broadband");
	case NM_DEVICE_TYPE_INFINIBAND:
		return _("InfiniBand");
	case NM_DEVICE_TYPE_BOND:
		return _("Bond");
	case NM_DEVICE_TYPE_TEAM:
		return _("Team");
	case NM_DEVICE_TYPE_BRIDGE:
		return _("Bridge");
	case NM_DEVICE_TYPE_VLAN:
		return _("VLAN");
	case NM_DEVICE_TYPE_ADSL:
		return _("ADSL");
	default:
		return _("Unknown");
	}
}

static char *
get_device_type_name_with_iface (NMDevice *device)
{
	const char *type_name = get_type_name (device);

	switch (nm_device_get_device_type (device)) {
	case NM_DEVICE_TYPE_BOND:
	case NM_DEVICE_TYPE_TEAM:
	case NM_DEVICE_TYPE_BRIDGE:
	case NM_DEVICE_TYPE_VLAN:
		return g_strdup_printf ("%s (%s)", type_name, nm_device_get_iface (device));
	default:
		return g_strdup (type_name);
	}
}

static char *
get_device_generic_type_name_with_iface (NMDevice *device)
{
	switch (nm_device_get_device_type (device)) {
	case NM_DEVICE_TYPE_ETHERNET:
	case NM_DEVICE_TYPE_INFINIBAND:
		return g_strdup (_("Wired"));
	default:
		return get_device_type_name_with_iface (device);
	}
}

static const char *
get_bus_name (NMDevice *device)
{
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (device);
	GUdevDevice *udevice;
	const char *ifname, *bus;

	if (priv->bus_name)
		goto out;

	if (!ensure_udev_client (device))
		return NULL;

	ifname = nm_device_get_iface (device);
	if (!ifname)
		return NULL;

	udevice = g_udev_client_query_by_subsystem_and_name (priv->client, "net", ifname);
	if (!udevice)
		udevice = g_udev_client_query_by_subsystem_and_name (priv->client, "tty", ifname);
	if (!udevice)
		return NULL;

	bus = g_udev_device_get_property (udevice, "ID_BUS");
	if (!g_strcmp0 (bus, "pci"))
		priv->bus_name = g_strdup (_("PCI"));
	else if (!g_strcmp0 (bus, "usb"))
		priv->bus_name = g_strdup (_("USB"));
	else {
		/* Use "" instead of NULL so we can tell later that we've
		 * already tried.
		 */
		priv->bus_name = g_strdup ("");
	}

out:
	if (*priv->bus_name)
		return priv->bus_name;
	else
		return NULL;
}

static gboolean
find_duplicates (char     **names,
                 gboolean  *duplicates,
                 int        num_devices)
{
	int i, j;
	gboolean found_any = FALSE;

	memset (duplicates, 0, num_devices * sizeof (gboolean));
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i])
			continue;
		for (j = i + 1; j < num_devices; j++) {
			if (duplicates[j])
				continue;
			if (!strcmp (names[i], names[j]))
				duplicates[i] = duplicates[j] = found_any = TRUE;
		}
	}

	return found_any;
}

/**
 * nm_device_disambiguate_names:
 * @devices: (array length=num_devices): an array of #NMDevice
 * @num_devices: length of @devices
 *
 * Generates a list of short-ish unique presentation names for the
 * devices in @devices.
 *
 * Returns: (transfer full) (array zero-terminated=1): the device names
 */
char **
nm_device_disambiguate_names (NMDevice **devices,
                              int        num_devices)
{
	char **names;
	gboolean *duplicates;
	int i;

	names = g_new (char *, num_devices + 1);
	duplicates = g_new (gboolean, num_devices);

	/* Generic device name */
	for (i = 0; i < num_devices; i++)
		names[i] = get_device_generic_type_name_with_iface (devices[i]);
	if (!find_duplicates (names, duplicates, num_devices))
		goto done;

	/* Try specific names (eg, "Ethernet" and "InfiniBand" rather
	 * than "Wired")
	 */
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i]) {
			g_free (names[i]);
			names[i] = get_device_type_name_with_iface (devices[i]);
		}
	}
	if (!find_duplicates (names, duplicates, num_devices))
		goto done;

	/* Try prefixing bus name (eg, "PCI Ethernet" vs "USB Ethernet") */
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i]) {
			const char *bus = get_bus_name (devices[i]);
			char *name;

			if (!bus)
				continue;

			g_free (names[i]);
			name = get_device_type_name_with_iface (devices[i]);
			/* Translators: the first %s is a bus name (eg, "USB") or
			 * product name, the second is a device type (eg,
			 * "Ethernet"). You can change this to something like
			 * "%2$s (%1$s)" if there's no grammatical way to combine
			 * the strings otherwise.
			 */
			names[i] = g_strdup_printf (C_("long device name", "%s %s"),
			                            bus, name);
			g_free (name);
		}
	}
	if (!find_duplicates (names, duplicates, num_devices))
		goto done;

	/* Try prefixing vendor name */
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i]) {
			const char *vendor = get_short_vendor (devices[i]);
			char *name;

			if (!vendor)
				continue;

			g_free (names[i]);
			name = get_device_type_name_with_iface (devices[i]);
			names[i] = g_strdup_printf (C_("long device name", "%s %s"),
			                            vendor,
			                            get_type_name (devices[i]));
			g_free (name);
		}
	}
	if (!find_duplicates (names, duplicates, num_devices))
		goto done;

	/* If dealing with Bluetooth devices, try to distinguish them by
	 * device name.
	 */
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i] && NM_IS_DEVICE_BT (devices[i])) {
			const char *devname = nm_device_bt_get_name (NM_DEVICE_BT (devices[i]));

			if (!devname)
				continue;

			g_free (names[i]);
			names[i] = g_strdup_printf ("%s (%s)",
			                            get_device_type_name_with_iface (devices[i]),
			                            devname);
		}
	}
	if (!find_duplicates (names, duplicates, num_devices))
		goto done;

	/* We have multiple identical network cards, so we have to differentiate
	 * them by interface name.
	 */
	for (i = 0; i < num_devices; i++) {
		if (duplicates[i]) {
			const char *interface = nm_device_get_iface (devices[i]);

			if (!interface)
				continue;

			g_free (names[i]);
			names[i] = g_strdup_printf ("%s (%s)",
			                            get_type_name (devices[i]),
			                            interface);
		}
	}

done:
	g_free (duplicates);
	names[num_devices] = NULL;
	return names;
}

/**
 * nm_device_get_physical_port_id:
 * @device: a #NMDevice
 *
 * Gets the physical port ID of the #NMDevice. If non-%NULL, this is
 * an opaque string that can be used to recognize when
 * seemingly-unrelated #NMDevices are actually just different virtual
 * ports on a single physical port. (Eg, NPAR / SR-IOV.)
 *
 * Returns: the physical port ID of the device, or %NULL if the port
 *   ID is unknown. This is the internal string used by the device and
 *   must not be modified.
 **/
const char *
nm_device_get_physical_port_id (NMDevice *device)
{
	NMDevicePrivate *priv;

	g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

	priv = NM_DEVICE_GET_PRIVATE (device);

	if (priv->physical_port_id && *priv->physical_port_id)
		return priv->physical_port_id;
	else
		return NULL;
}

/**
 * nm_device_get_mtu:
 * @device: a #NMDevice
 *
 * Gets the  MTU of the #NMDevice.
 *
 * Returns: the MTU of the device.
 **/
guint32
nm_device_get_mtu (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), 0);

	return NM_DEVICE_GET_PRIVATE (device)->mtu;
}

/**
 * nm_device_get_metered:
 * @device: a #NMDevice
 *
 * Gets the metered setting of a #NMDevice.
 *
 * Returns: the metered setting.
 *
 * Since: 1.2
 **/
NMMetered
nm_device_get_metered (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), NM_METERED_UNKNOWN);

	return NM_DEVICE_GET_PRIVATE (device)->metered;
}

NM_BACKPORT_SYMBOL (libnm_1_0_6, NMMetered, nm_device_get_metered, (NMDevice *device), (device));

/**
 * nm_device_get_lldp_neighbors:
 * @device: a #NMDevice
 *
 * Gets the list of neighbors discovered through LLDP.
 *
 * Returns: (element-type NMLldpNeighbor) (transfer none): the #GPtrArray
 * containing #NMLldpNeighbor<!-- -->s. This is the internal copy used by the
 * device and must not be modified. The library never modifies the returned
 * array and thus it is safe for callers to reference and keep using it.
 *
 * Since: 1.2
 **/
GPtrArray *
nm_device_get_lldp_neighbors (NMDevice *device)
{
       g_return_val_if_fail (NM_IS_DEVICE (device), NULL);

       return NM_DEVICE_GET_PRIVATE (device)->lldp_neighbors;
}

/**
 * nm_device_is_software:
 * @device: a #NMDevice
 *
 * Whether the device is a software device.
 *
 * Returns: %TRUE if @device is a software device, %FALSE if it is a hardware device.
 **/
gboolean
nm_device_is_software (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), FALSE);

	return !!(NM_DEVICE_GET_PRIVATE (device)->capabilities & NM_DEVICE_CAP_IS_SOFTWARE);
}

/**
 * nm_device_disconnect:
 * @device: a #NMDevice
 * @cancellable: a #GCancellable, or %NULL
 * @error: location for a #GError, or %NULL
 *
 * Disconnects the device if currently connected, and prevents the device from
 * automatically connecting to networks until the next manual network connection
 * request.
 *
 * Returns: %TRUE on success, %FALSE on error, in which case @error will be set.
 **/
gboolean
nm_device_disconnect (NMDevice *device,
                      GCancellable *cancellable,
                      GError **error)
{
	gboolean ret;

	g_return_val_if_fail (NM_IS_DEVICE (device), FALSE);

	ret = nmdbus_device_call_disconnect_sync (NM_DEVICE_GET_PRIVATE (device)->proxy,
	                                          cancellable, error);
	if (error && *error)
		g_dbus_error_strip_remote_error (*error);
	return ret;
}

static void
device_disconnect_cb (GObject *proxy,
                      GAsyncResult *result,
                      gpointer user_data)
{
	GSimpleAsyncResult *simple = user_data;
	GError *error = NULL;

	if (nmdbus_device_call_disconnect_finish (NMDBUS_DEVICE (proxy), result, &error))
		g_simple_async_result_set_op_res_gboolean (simple, TRUE);
	else {
		g_dbus_error_strip_remote_error (error);
		g_simple_async_result_take_error (simple, error);
	}

	g_simple_async_result_complete (simple);
	g_object_unref (simple);
}

/**
 * nm_device_disconnect_async:
 * @device: a #NMDevice
 * @cancellable: a #GCancellable, or %NULL
 * @callback: callback to be called when the disconnect operation completes
 * @user_data: caller-specific data passed to @callback
 *
 * Asynchronously begins disconnecting the device if currently connected, and
 * prevents the device from automatically connecting to networks until the next
 * manual network connection request.
 **/
void
nm_device_disconnect_async (NMDevice *device,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (NM_IS_DEVICE (device));

	simple = g_simple_async_result_new (G_OBJECT (device), callback, user_data,
	                                    nm_device_disconnect_async);

	nmdbus_device_call_disconnect (NM_DEVICE_GET_PRIVATE (device)->proxy,
	                               cancellable,
	                               device_disconnect_cb, simple);
}

/**
 * nm_device_disconnect_finish:
 * @device: a #NMDevice
 * @result: the result passed to the #GAsyncReadyCallback
 * @error: location for a #GError, or %NULL
 *
 * Gets the result of a call to nm_device_disconnect_async().
 *
 * Returns: %TRUE on success, %FALSE on error, in which case @error
 * will be set.
 **/
gboolean
nm_device_disconnect_finish (NMDevice *device,
                             GAsyncResult *result,
                             GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (device), nm_device_disconnect_async), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;
	else
		return g_simple_async_result_get_op_res_gboolean (simple);
}

/**
 * nm_device_delete:
 * @device: a #NMDevice
 * @cancellable: a #GCancellable, or %NULL
 * @error: location for a #GError, or %NULL
 *
 * Deletes the software device. Hardware devices can't be deleted.
 *
 * Returns: %TRUE on success, %FALSE on error, in which case @error
 * will be set.
 **/
gboolean
nm_device_delete (NMDevice *device,
                  GCancellable *cancellable,
                  GError **error)
{
	gboolean ret;

	g_return_val_if_fail (NM_IS_DEVICE (device), FALSE);

	ret = nmdbus_device_call_delete_sync (NM_DEVICE_GET_PRIVATE (device)->proxy,
	                                      cancellable, error);
	if (error && *error)
		g_dbus_error_strip_remote_error (*error);
	return ret;
}

static void
device_delete_cb (GObject *proxy,
                  GAsyncResult *result,
                  gpointer user_data)
{
	GSimpleAsyncResult *simple = user_data;
	GError *error = NULL;

	if (nmdbus_device_call_delete_finish (NMDBUS_DEVICE (proxy), result, &error))
		g_simple_async_result_set_op_res_gboolean (simple, TRUE);
	else {
		g_dbus_error_strip_remote_error (error);
		g_simple_async_result_take_error (simple, error);
	}

	g_simple_async_result_complete (simple);
	g_object_unref (simple);
}

/**
 * nm_device_delete_async:
 * @device: a #NMDevice
 * @cancellable: a #GCancellable, or %NULL
 * @callback: callback to be called when delete operation completes
 * @user_data: caller-specific data passed to @callback
 *
 * Asynchronously begins deleteing the software device. Hardware devices can't
 * be deleted.
 **/
void
nm_device_delete_async (NMDevice *device,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (NM_IS_DEVICE (device));

	simple = g_simple_async_result_new (G_OBJECT (device), callback, user_data,
	                                    nm_device_delete_async);

	nmdbus_device_call_delete (NM_DEVICE_GET_PRIVATE (device)->proxy,
	                           cancellable,
	                           device_delete_cb, simple);
}

/**
 * nm_device_delete_finish:
 * @device: a #NMDevice
 * @result: the result passed to the #GAsyncReadyCallback
 * @error: location for a #GError, or %NULL
 *
 * Gets the result of a call to nm_device_delete_async().
 *
 * Returns: %TRUE on success, %FALSE on error, in which case @error
 * will be set.
 **/
gboolean
nm_device_delete_finish (NMDevice *device,
                         GAsyncResult *result,
                         GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (device), nm_device_delete_async), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;
	else
		return g_simple_async_result_get_op_res_gboolean (simple);
}

/**
 * nm_device_connection_valid:
 * @device: an #NMDevice to validate @connection against
 * @connection: an #NMConnection to validate against @device
 *
 * Validates a given connection for a given #NMDevice object and returns
 * whether the connection may be activated with the device. For example if
 * @device is a Wi-Fi device that supports only WEP encryption, the connection
 * will only be valid if it is a Wi-Fi connection which describes a WEP or open
 * network, and will not be valid if it describes a WPA network, or if it is
 * an Ethernet, Bluetooth, WWAN, etc connection that is incompatible with the
 * device.
 *
 * Returns: %TRUE if the connection may be activated with this device, %FALSE
 * if is incompatible with the device's capabilities and characteristics.
 **/
gboolean
nm_device_connection_valid (NMDevice *device, NMConnection *connection)
{
	return nm_device_connection_compatible (device, connection, NULL);
}

gboolean
connection_compatible (NMDevice *device, NMConnection *connection, GError **error)
{
	const char *config_iface, *device_iface;
	GError *local = NULL;

	if (!nm_connection_verify (connection, &local)) {
		g_set_error (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INVALID_CONNECTION,
		             _("The connection was not valid: %s"), local->message);
		g_error_free (local);
		return FALSE;
	}

	config_iface = nm_connection_get_interface_name (connection);
	device_iface = nm_device_get_iface (device);
	if (config_iface && g_strcmp0 (config_iface, device_iface) != 0) {
		g_set_error (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INCOMPATIBLE_CONNECTION,
		             _("The interface names of the device and the connection didn't match."));
		return FALSE;
	}

	return TRUE;
}

/**
 * nm_device_connection_compatible:
 * @device: an #NMDevice to validate @connection against
 * @connection: an #NMConnection to validate against @device
 * @error: return location for a #GError, or %NULL
 *
 * Validates a given connection for a given #NMDevice object and returns
 * whether the connection may be activated with the device. For example if
 * @device is a Wi-Fi device that supports only WEP encryption, the connection
 * will only be valid if it is a Wi-Fi connection which describes a WEP or open
 * network, and will not be valid if it describes a WPA network, or if it is
 * an Ethernet, Bluetooth, WWAN, etc connection that is incompatible with the
 * device.
 *
 * This function does the same as nm_device_connection_valid(), i.e. checking
 * compatibility of the given device and connection. But, in addition, it sets
 * GError when FALSE is returned.
 *
 * Returns: %TRUE if the connection may be activated with this device, %FALSE
 * if is incompatible with the device's capabilities and characteristics.
 **/
gboolean
nm_device_connection_compatible (NMDevice *device, NMConnection *connection, GError **error)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return NM_DEVICE_GET_CLASS (device)->connection_compatible (device, connection, error);
}

/**
 * nm_device_filter_connections:
 * @device: an #NMDevice to filter connections for
 * @connections: (element-type NMConnection): an array of #NMConnections to filter
 *
 * Filters a given array of connections for a given #NMDevice object and returns
 * connections which may be activated with the device. For example if @device
 * is a Wi-Fi device that supports only WEP encryption, the returned array will
 * contain any Wi-Fi connections in @connections that allow connection to
 * unencrypted or WEP-enabled SSIDs.  The returned array will not contain
 * Ethernet, Bluetooth, Wi-Fi WPA connections, or any other connection that is
 * incompatible with the device. To get the full list of connections see
 * nm_client_get_connections().
 *
 * Returns: (transfer container) (element-type NMConnection): an array of
 * #NMConnections that could be activated with the given @device.  The array
 * should be freed with g_ptr_array_unref() when it is no longer required.
 **/
GPtrArray *
nm_device_filter_connections (NMDevice *device, const GPtrArray *connections)
{
	GPtrArray *filtered;
	int i;

	filtered = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; i < connections->len; i++) {
		NMConnection *candidate = connections->pdata[i];

		/* Connection applies to this device */
		if (nm_device_connection_valid (device, candidate))
			g_ptr_array_add (filtered, g_object_ref (candidate));
	}

	return filtered;
}

/**
 * nm_device_get_setting_type:
 * @device: an #NMDevice
 *
 * Gets the (primary) #NMSetting subtype associated with connections
 * that can be used on @device.
 *
 * Returns: @device's associated #NMSetting type
 */
GType
nm_device_get_setting_type (NMDevice *device)
{
	g_return_val_if_fail (NM_IS_DEVICE (device), G_TYPE_INVALID);
	g_return_val_if_fail (NM_DEVICE_GET_CLASS (device)->get_setting_type != NULL, G_TYPE_INVALID);

	return NM_DEVICE_GET_CLASS (device)->get_setting_type (device);
}

/**
 * nm_lldp_neighbor_new:
 *
 * Creates a new #NMLldpNeighbor object.
 *
 * Returns: (transfer full): the new #NMLldpNeighbor object.
 *
 * Since: 1.2
 **/
NMLldpNeighbor *
nm_lldp_neighbor_new (void)
{
	NMLldpNeighbor *neigh;

	neigh = g_new0 (NMLldpNeighbor, 1);
	neigh->refcount = 1;
	neigh->attrs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                                      (GDestroyNotify) g_variant_unref);

	return neigh;
}

static NMLldpNeighbor *
nm_lldp_neighbor_dup (NMLldpNeighbor *neighbor)
{
	NMLldpNeighbor *copy;

	copy = nm_lldp_neighbor_new ();
	copy->attrs = g_hash_table_ref (neighbor->attrs);

	return copy;
}

/**
 * nm_lldp_neighbor_ref:
 * @neighbor: the #NMLldpNeighbor
 *
 * Increases the reference count of the object.
 *
 * Since: 1.2
 **/
void
nm_lldp_neighbor_ref (NMLldpNeighbor *neighbor)
{
	g_return_if_fail (neighbor);
	g_return_if_fail (neighbor->refcount > 0);

	neighbor->refcount++;
}

/**
 * nm_lldp_neighbor_unref:
 * @neighbor: the #NMLldpNeighbor
 *
 * Decreases the reference count of the object.  If the reference count
 * reaches zero, the object will be destroyed.
 *
 * Since: 1.2
 **/
void
nm_lldp_neighbor_unref (NMLldpNeighbor *neighbor)
{
	g_return_if_fail (neighbor);
	g_return_if_fail (neighbor->refcount > 0);

	if (--neighbor->refcount == 0) {
		g_return_if_fail (neighbor->attrs);
		g_hash_table_unref (neighbor->attrs);
		g_free (neighbor);
	}
}

/**
 * nm_lldp_neighbor_get_attr_names:
 * @neighbor: the #NMLldpNeighbor
 *
 * Gets an array of attribute names available for @neighbor.
 *
 * Returns: (transfer full): a %NULL-terminated array of attribute names.
 *
 * Since: 1.2
 **/
char **
nm_lldp_neighbor_get_attr_names (NMLldpNeighbor *neighbor)
{
	GHashTableIter iter;
	const char *key;
	GPtrArray *names;

	g_return_val_if_fail (neighbor, NULL);
	g_return_val_if_fail (neighbor->attrs, NULL);

	names = g_ptr_array_new ();

	g_hash_table_iter_init (&iter, neighbor->attrs);
	while (g_hash_table_iter_next (&iter, (gpointer *) &key, NULL))
		g_ptr_array_add (names, g_strdup (key));

	g_ptr_array_add (names, NULL);

	return (char **) g_ptr_array_free (names, FALSE);
}

/**
 * nm_lldp_neighbor_get_attr_string_value:
 * @neighbor: the #NMLldpNeighbor
 * @name: the attribute name
 * @out_value: (out) (allow-none) (transfer none): on return, the attribute value
 *
 * Gets the string value of attribute with name @name on @neighbor
 *
 * Returns: %TRUE if a string attribute with name @name was found, %FALSE otherwise
 *
 * Since: 1.2
 **/
gboolean
nm_lldp_neighbor_get_attr_string_value (NMLldpNeighbor *neighbor, char *name,
                                        const char **out_value)
{
	GVariant *variant;

	g_return_val_if_fail (neighbor, FALSE);
	g_return_val_if_fail (name && name[0], FALSE);

	variant = g_hash_table_lookup (neighbor->attrs, name);
	if (variant && g_variant_is_of_type (variant, G_VARIANT_TYPE_STRING)) {
		if (out_value)
			*out_value = g_variant_get_string (variant, NULL);
		return TRUE;
	} else
		return FALSE;
}

/**
 * nm_lldp_neighbor_get_attr_uint_value:
 * @neighbor: the #NMLldpNeighbor
 * @name: the attribute name
 * @out_value: (out) (allow-none): on return, the attribute value
 *
 * Gets the uint value of attribute with name @name on @neighbor
 *
 * Returns: %TRUE if a uint attribute with name @name was found, %FALSE otherwise
 *
 * Since: 1.2
 **/
gboolean
nm_lldp_neighbor_get_attr_uint_value (NMLldpNeighbor *neighbor, char *name,
                                      guint *out_value)
{
	GVariant *variant;

	g_return_val_if_fail (neighbor, FALSE);
	g_return_val_if_fail (name && name[0], FALSE);

	variant = g_hash_table_lookup (neighbor->attrs, name);
	if (variant && g_variant_is_of_type (variant, G_VARIANT_TYPE_UINT32)) {
		if (out_value)
			*out_value = g_variant_get_uint32 (variant);
		return TRUE;
	} else
		return FALSE;
}

/**
 * nm_lldp_neighbor_get_attr_type:
 * @neighbor: the #NMLldpNeighbor
 * @name: the attribute name
 *
 * Get the type of an attribute.
 *
 * Returns: the #GVariantType of the attribute with name @name
 *
 * Since: 1.2
 **/
const GVariantType *
nm_lldp_neighbor_get_attr_type (NMLldpNeighbor *neighbor, char *name)
{
	GVariant *variant;

	g_return_val_if_fail (neighbor, NULL);
	g_return_val_if_fail (name && name[0], NULL);

	variant = g_hash_table_lookup (neighbor->attrs, name);
	if (variant)
		return g_variant_get_type (variant);
	else
		return NULL;

}
