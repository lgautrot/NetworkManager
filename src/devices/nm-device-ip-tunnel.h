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
 * Copyright 2015 Red Hat, Inc.
 */

#ifndef __NETWORKMANAGER_DEVICE_IP_TUNNEL_H__
#define __NETWORKMANAGER_DEVICE_IP_TUNNEL_H__

#include "nm-device-generic.h"

G_BEGIN_DECLS

#define NM_TYPE_DEVICE_IP_TUNNEL            (nm_device_ip_tunnel_get_type ())
#define NM_DEVICE_IP_TUNNEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_DEVICE_IP_TUNNEL, NMDeviceIPTunnel))
#define NM_DEVICE_IP_TUNNEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  NM_TYPE_DEVICE_IP_TUNNEL, NMDeviceIPTunnelClass))
#define NM_IS_DEVICE_IP_TUNNEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_DEVICE_IP_TUNNEL))
#define NM_IS_DEVICE_IP_TUNNEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  NM_TYPE_DEVICE_IP_TUNNEL))
#define NM_DEVICE_IP_TUNNEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  NM_TYPE_DEVICE_IP_TUNNEL, NMDeviceIPTunnelClass))

#define NM_DEVICE_IP_TUNNEL_PARENT             "parent"
#define NM_DEVICE_IP_TUNNEL_LOCAL              "local"
#define NM_DEVICE_IP_TUNNEL_REMOTE             "remote"
#define NM_DEVICE_IP_TUNNEL_TTL                "ttl"
#define NM_DEVICE_IP_TUNNEL_TOS                "tos"
#define NM_DEVICE_IP_TUNNEL_PATH_MTU_DISCOVERY "path-mtu-discovery"

typedef struct {
	NMDevice parent;
} NMDeviceIPTunnel;

typedef struct {
	NMDeviceClass parent;

	void        (*update_properties) (NMDeviceIPTunnel *self);

} NMDeviceIPTunnelClass;

GType nm_device_ip_tunnel_get_type (void);

/* Private API - to be used only by NMDeviceIPTunnel subclasses */
void nm_device_ip_tunnel_set_parent (NMDeviceIPTunnel *self, int parent);
void nm_device_ip_tunnel_set_local (NMDeviceIPTunnel *self, const char *local);
void nm_device_ip_tunnel_set_remote (NMDeviceIPTunnel *self, const char *remote);
void nm_device_ip_tunnel_set_tos (NMDeviceIPTunnel *self, guint8 tos);
void nm_device_ip_tunnel_set_ttl (NMDeviceIPTunnel *self, guint8 ttl);
void nm_device_ip_tunnel_set_path_mtu_discovery (NMDeviceIPTunnel *self, gboolean pmtud);

G_END_DECLS

#endif	/* NM_DEVICE_IP_TUNNEL_H */
