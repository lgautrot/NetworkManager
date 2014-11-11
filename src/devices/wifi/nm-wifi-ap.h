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
 * Copyright (C) 2004 - 2011 Red Hat, Inc.
 * Copyright (C) 2006 - 2008 Novell, Inc.
 */

#ifndef __NETWORKMANAGER_ACCESS_POINT_H__
#define __NETWORKMANAGER_ACCESS_POINT_H__

#include <glib.h>
#include <glib-object.h>
#include "nm-dbus-interface.h"
#include "nm-connection.h"

#define NM_TYPE_AP            (nm_ap_get_type ())
#define NM_AP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_AP, NMAccessPoint))
#define NM_AP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_AP, NMAccessPointClass))
#define NM_IS_AP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_AP))
#define NM_IS_AP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_AP))
#define NM_AP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_AP, NMAccessPointClass))

#define NM_AP_FLAGS "flags"
#define NM_AP_WPA_FLAGS "wpa-flags"
#define NM_AP_RSN_FLAGS "rsn-flags"
#define NM_AP_SSID "ssid"
#define NM_AP_FREQUENCY "frequency"
#define NM_AP_HW_ADDRESS "hw-address"
#define NM_AP_MODE "mode"
#define NM_AP_MAX_BITRATE "max-bitrate"
#define NM_AP_STRENGTH "strength"
#define NM_AP_LAST_SEEN "last-seen"

typedef struct {
	GObject parent;
} NMAccessPoint;

typedef struct {
	GObjectClass parent;

} NMAccessPointClass;

GType nm_ap_get_type (void);

NMAccessPoint * nm_ap_new_from_properties (const char *supplicant_path,
                                           GVariant *properties);
NMAccessPoint * nm_ap_new_fake_from_connection (NMConnection *connection);
void            nm_ap_export_to_dbus    (NMAccessPoint *ap);

const char *nm_ap_get_dbus_path (NMAccessPoint *ap);

const char *nm_ap_get_supplicant_path (NMAccessPoint *ap);
void        nm_ap_set_supplicant_path (NMAccessPoint *ap,
                                       const char *path);

const GByteArray *nm_ap_get_ssid (const NMAccessPoint * ap);
void              nm_ap_set_ssid (NMAccessPoint * ap, const guint8 * ssid, gsize len);

NM80211ApFlags nm_ap_get_flags (NMAccessPoint *ap);
void           nm_ap_set_flags (NMAccessPoint *ap, NM80211ApFlags flags);

NM80211ApSecurityFlags nm_ap_get_wpa_flags (NMAccessPoint *ap);
void                   nm_ap_set_wpa_flags (NMAccessPoint *ap, NM80211ApSecurityFlags flags);

NM80211ApSecurityFlags nm_ap_get_rsn_flags (NMAccessPoint *ap);
void                   nm_ap_set_rsn_flags (NMAccessPoint *ap, NM80211ApSecurityFlags flags);

const char *nm_ap_get_address (const NMAccessPoint *ap);
void        nm_ap_set_address (NMAccessPoint *ap, const char *addr);

NM80211Mode nm_ap_get_mode (NMAccessPoint *ap);
void        nm_ap_set_mode (NMAccessPoint *ap, const NM80211Mode mode);

gboolean nm_ap_is_hotspot (NMAccessPoint *ap);

gint8 nm_ap_get_strength (NMAccessPoint *ap);
void  nm_ap_set_strength (NMAccessPoint *ap, gint8 strength);

guint32 nm_ap_get_freq (NMAccessPoint *ap);
void    nm_ap_set_freq (NMAccessPoint *ap, guint32 freq);

guint32 nm_ap_get_max_bitrate (NMAccessPoint *ap);
void    nm_ap_set_max_bitrate (NMAccessPoint *ap, guint32 bitrate);

gboolean nm_ap_get_fake (const NMAccessPoint *ap);
void     nm_ap_set_fake (NMAccessPoint *ap, gboolean fake);

gboolean nm_ap_get_broadcast (NMAccessPoint *ap);
void     nm_ap_set_broadcast (NMAccessPoint *ap, gboolean broadcast);

guint32	nm_ap_get_last_seen	(const NMAccessPoint *ap);
void	nm_ap_set_last_seen	(NMAccessPoint *ap, guint32 last_seen);
void	nm_ap_update_last_seen	(NMAccessPoint *ap);

gboolean nm_ap_check_compatible (NMAccessPoint *self,
                                 NMConnection *connection);

gboolean nm_ap_complete_connection (NMAccessPoint *self,
                                    NMConnection *connection,
                                    gboolean lock_bssid,
                                    GError **error);

NMAccessPoint *     nm_ap_match_in_list (NMAccessPoint *find_ap,
                                         GSList *ap_list,
                                         gboolean strict_match);

void                nm_ap_dump (NMAccessPoint *ap, const char *prefix);

#endif /* __NETWORKMANAGER_ACCESS_POINT_H__ */
