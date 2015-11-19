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
 * Copyright (C) 2015 Red Hat, Inc.
 */

#include "config.h"

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "nm-default.h"
#include "nm-arping-manager.h"
#include "NetworkManagerUtils.h"
#include "nm-utils.h"

typedef enum {
	STATE_INIT,
	STATE_PROBING,
	STATE_PROBE_DONE,
	STATE_ANNOUNCING,
} State;

typedef struct {
	const char   *iface;
	State         state;
	GHashTable   *addresses;
	guint         completed;
	guint         timer;
	guint         round2_id;
} NMArpingManagerPrivate;

typedef struct {
	in_addr_t address;
	gboolean announce;
	GPid pid;
	guint watch;
	gboolean duplicated;
	NMArpingManager *manager;
} AddressInfo;

enum {
	DAD_TERMINATED,
	LAST_SIGNAL,
};
static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (NMArpingManager, nm_arping_manager, G_TYPE_OBJECT)

#define NM_ARPING_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_ARPING_MANAGER, NMArpingManagerPrivate))

gboolean
nm_arping_manager_add_address (NMArpingManager *self, in_addr_t address, gboolean announce)
{
	NMArpingManagerPrivate *priv;
	AddressInfo *info;

	g_return_val_if_fail (NM_IS_ARPING_MANAGER (self), FALSE);
	priv = NM_ARPING_MANAGER_GET_PRIVATE (self);
	g_return_val_if_fail (priv->state == STATE_INIT, FALSE);

	if (g_hash_table_lookup (priv->addresses, GUINT_TO_POINTER (address))) {
		nm_log_dbg (LOGD_IP4, "arping: address already exists");
		return FALSE;
	}

	info = g_new0 (AddressInfo, 1);
	info->address = address;
	info->announce = announce;
	info->manager = g_object_ref (self);

	g_hash_table_insert (priv->addresses, GUINT_TO_POINTER (address), info);

	return TRUE;
}

static void
arping_watch_cb (GPid pid, gint status, gpointer user_data)
{
	AddressInfo *info = user_data;
	NMArpingManager *self = info->manager;
	NMArpingManagerPrivate *priv = NM_ARPING_MANAGER_GET_PRIVATE (self);
	const char *addr;

	info->pid = 0;
	info->watch = 0;
	addr = nm_utils_inet4_ntop (info->address, NULL);

	if (WIFEXITED (status)) {
		if (WEXITSTATUS (status) != 0) {
			nm_log_warn (LOGD_IP4, "arping: %s already used in %s network", addr, priv->iface);
			info->duplicated = TRUE;
		} else
			nm_log_dbg (LOGD_IP4, "arping: DAD succeeded for %s", addr);
	} else {
		nm_log_warn (LOGD_IP4, "arping: stopped unexpectedly with status %d for %s",
		             status, addr);
	}

	if (++priv->completed == g_hash_table_size (priv->addresses)) {
		priv->state = STATE_PROBE_DONE;
		nm_clear_g_source (&priv->timer);
		g_signal_emit (self, signals[DAD_TERMINATED], 0);
	}
}

static gboolean
arping_timeout_cb (gpointer user_data)
{
	NMArpingManager *self = user_data;
	NMArpingManagerPrivate *priv = NM_ARPING_MANAGER_GET_PRIVATE (self);
	GHashTableIter iter;
	AddressInfo *info;

	priv->timer = 0;

	g_hash_table_iter_init (&iter, priv->addresses);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &info)) {
		nm_log_dbg (LOGD_IP4, "arping: DAD timed out for %s", nm_utils_inet4_ntop (info->address, NULL));
		if (info->pid) {
			nm_utils_kill_child_async (info->pid, SIGTERM, LOGD_IP4, "arping", 1000, NULL, NULL);
			info->pid = 0;
		}
		nm_clear_g_source (&info->watch);
	}

	priv->state = STATE_PROBE_DONE;
	g_signal_emit (self, signals[DAD_TERMINATED], 0);

	return G_SOURCE_REMOVE;
}

gboolean
nm_arping_manager_start (NMArpingManager *self, guint timeout, GError **error)
{
	const char *argv[] = { NULL, "-D", "-q", "-I", NULL, "-c", NULL, "-w", NULL, NULL, NULL };
	NMArpingManagerPrivate *priv;
	GHashTableIter iter;
	AddressInfo *info;
	gs_free char *timeout_str = NULL;

	g_return_val_if_fail (NM_IS_ARPING_MANAGER (self), FALSE);
	g_return_val_if_fail (!error || !*error, FALSE);
	g_return_val_if_fail (timeout, FALSE);

	priv = NM_ARPING_MANAGER_GET_PRIVATE (self);
	g_return_val_if_fail (priv->state == STATE_INIT, FALSE);

	priv->completed = 0;
	argv[0] = nm_utils_find_helper ("arping", NULL, NULL);
	if (!argv[0]) {
		g_set_error_literal (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_FAILED,
		                     "arping could not be found");
		return FALSE;
	}

	timeout_str = g_strdup_printf ("%u", timeout + 1);
	argv[4] = priv->iface;
	argv[6] = timeout_str;
	argv[8] = timeout_str;

	g_hash_table_iter_init (&iter, priv->addresses);

	while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &info)) {
 		gs_free char *tmp_str = NULL;
		gboolean success;

		argv[9] = nm_utils_inet4_ntop (info->address, NULL);
 		nm_log_dbg (LOGD_DEVICE | LOGD_IP4,
 		            "arping: run %s", (tmp_str = g_strjoinv (" ", (char **) argv)));

 		success = g_spawn_async (NULL, (char **) argv, NULL,
		                         G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_DO_NOT_REAP_CHILD,
		                         NULL, NULL, &info->pid, NULL);

		info->watch = g_child_watch_add (info->pid, arping_watch_cb, info);
	}

	priv->timer = g_timeout_add_seconds (timeout, arping_timeout_cb, self);
	priv->state = STATE_PROBING;

	return TRUE;
}

void
nm_arping_manager_reset (NMArpingManager *self)
{
	NMArpingManagerPrivate *priv;

	g_return_if_fail (NM_IS_ARPING_MANAGER (self));
	priv = NM_ARPING_MANAGER_GET_PRIVATE (self);

	nm_clear_g_source (&priv->timer);
	nm_clear_g_source (&priv->round2_id);
	g_hash_table_remove_all (priv->addresses);
	priv->state = STATE_INIT;
}

gboolean
nm_arping_manager_check_address (NMArpingManager *self, in_addr_t address)
{
	NMArpingManagerPrivate *priv;
	AddressInfo *info;

	g_return_val_if_fail (NM_IS_ARPING_MANAGER (self), FALSE);

	priv = NM_ARPING_MANAGER_GET_PRIVATE (self);

	info = g_hash_table_lookup (priv->addresses, GUINT_TO_POINTER (address));
	g_return_val_if_fail (info, FALSE);

	return !info->duplicated;
}

static void
send_arps (NMArpingManager *self, const char *mode_arg)
{
	NMArpingManagerPrivate *priv = NM_ARPING_MANAGER_GET_PRIVATE (self);
	const char *argv[] = { NULL, mode_arg, "-q", "-I", priv->iface, "-c", "1", NULL, NULL };
	int ip_arg = G_N_ELEMENTS (argv) - 2;
	GError *error = NULL;
	GHashTableIter iter;
	AddressInfo *info;

	argv[0] = nm_utils_find_helper ("arping", NULL, NULL);
	if (!argv[0]) {
		nm_log_warn (LOGD_DEVICE | LOGD_IP4, "arping could not be found; no ARPs will be sent");
		return;
	}

	g_hash_table_iter_init (&iter, priv->addresses);

	while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &info)) {
		gs_free char *tmp_str = NULL;
		gboolean success;

		if (!info->announce || info->duplicated)
			continue;

		argv[ip_arg] = nm_utils_inet4_ntop (info->address, NULL);
		nm_log_dbg (LOGD_DEVICE | LOGD_IP4,
		            "arping: run %s", (tmp_str = g_strjoinv (" ", (char **) argv)));

		success = g_spawn_async (NULL, (char **) argv, NULL,
		                         G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
		                         NULL, NULL, NULL, &error);
		if (!success) {
			nm_log_warn (LOGD_DEVICE | LOGD_IP4,
			             "arping: could not send ARP for local address %s: %s",
			             argv[ip_arg], error->message);
			g_clear_error (&error);
		}
	}
}

static gboolean
arp_announce_round2 (gpointer self)
{
	NMArpingManagerPrivate *priv = NM_ARPING_MANAGER_GET_PRIVATE (self);

	priv->round2_id = 0;
	send_arps (self, "-U");
	g_hash_table_remove_all (priv->addresses);
	priv->state = STATE_INIT;

	return G_SOURCE_REMOVE;
}

gboolean
nm_arping_manager_announce_addresses (NMArpingManager *self)
{
	NMArpingManagerPrivate *priv = NM_ARPING_MANAGER_GET_PRIVATE (self);

	g_return_val_if_fail (   priv->state == STATE_INIT
	                      || priv->state == STATE_PROBE_DONE, FALSE);

	send_arps (self, "-A");
	nm_clear_g_source (&priv->round2_id);
	priv->round2_id = g_timeout_add_seconds (2, arp_announce_round2, self);
	priv->state = STATE_ANNOUNCING;

	return TRUE;
}

static void
destroy_address_info (gpointer data)
{
	AddressInfo *info = (AddressInfo *) data;

	if (info->pid) {
		nm_utils_kill_child_async (info->pid, SIGTERM, LOGD_IP4, "arping",
		                           1000, NULL, NULL);
	}

	nm_clear_g_source (&info->watch);
	g_object_unref (info->manager);
	g_free (info);
}

static void
dispose (GObject *object)
{
	NMArpingManager *self = NM_ARPING_MANAGER (object);
	NMArpingManagerPrivate *priv = NM_ARPING_MANAGER_GET_PRIVATE (self);

	nm_clear_g_source (&priv->timer);
	nm_clear_g_source (&priv->round2_id);
	g_clear_pointer (&priv->iface, g_free);
	g_hash_table_destroy (priv->addresses);

	G_OBJECT_CLASS (nm_arping_manager_parent_class)->dispose (object);
}

static void
nm_arping_manager_init (NMArpingManager *self)
{
	NMArpingManagerPrivate *priv = NM_ARPING_MANAGER_GET_PRIVATE (self);

	priv->addresses = g_hash_table_new_full (g_direct_hash, g_direct_equal,
	                                         NULL, destroy_address_info);
	priv->state = STATE_INIT;
}

NMArpingManager *
nm_arping_manager_new (const char *interface)
{
	NMArpingManager *self;
	NMArpingManagerPrivate *priv;

	self = g_object_new (NM_TYPE_ARPING_MANAGER, NULL);
	priv = NM_ARPING_MANAGER_GET_PRIVATE (self);
	priv->iface = g_strdup (interface);

	return self;
}

static void
nm_arping_manager_class_init (NMArpingManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NMArpingManagerPrivate));

	object_class->dispose = dispose;

	signals[DAD_TERMINATED] =
		g_signal_new (NM_ARPING_MANAGER_DAD_TERMINATED,
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

