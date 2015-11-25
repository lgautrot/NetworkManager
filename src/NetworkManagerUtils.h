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
 * Copyright 2004 - 2014 Red Hat, Inc.
 * Copyright 2005 - 2008 Novell, Inc.
 */

#ifndef __NETWORKMANAGER_UTILS_H__
#define __NETWORKMANAGER_UTILS_H__

#include <stdio.h>
#include <arpa/inet.h>

#include "nm-default.h"
#include "nm-connection.h"

/*****************************************************************************/

#define NM_DEFINE_SINGLETON_INSTANCE(TYPE) \
static TYPE *singleton_instance

#define NM_DEFINE_SINGLETON_REGISTER(TYPE) \
NM_DEFINE_SINGLETON_INSTANCE (TYPE); \
static void \
_singleton_instance_weak_ref_cb (gpointer data, \
                                 GObject *where_the_object_was) \
{ \
	nm_log_dbg (LOGD_CORE, "disposing %s singleton (%p)", G_STRINGIFY (TYPE), singleton_instance); \
	singleton_instance = NULL; \
} \
static inline void \
nm_singleton_instance_register (void) \
{ \
	g_object_weak_ref (G_OBJECT (singleton_instance), _singleton_instance_weak_ref_cb, NULL); \
	_nm_singleton_instance_register_destruction (G_OBJECT (singleton_instance)); \
}

void _nm_singleton_instance_register_destruction (GObject *instance);

/* By default, the getter will assert that the singleton will be created only once. You can
 * change this by redefining NM_DEFINE_SINGLETON_ALLOW_MULTIPLE. */
#ifndef NM_DEFINE_SINGLETON_ALLOW_MULTIPLE
#define NM_DEFINE_SINGLETON_ALLOW_MULTIPLE     FALSE
#endif

#define NM_DEFINE_SINGLETON_GETTER(TYPE, GETTER, GTYPE, ...) \
NM_DEFINE_SINGLETON_INSTANCE (TYPE); \
NM_DEFINE_SINGLETON_REGISTER (TYPE); \
TYPE * \
GETTER (void) \
{ \
	if (G_UNLIKELY (!singleton_instance)) { \
		static char _already_created = FALSE; \
\
		g_assert (!_already_created || (NM_DEFINE_SINGLETON_ALLOW_MULTIPLE)); \
		_already_created = TRUE;\
		singleton_instance = (g_object_new (GTYPE, ##__VA_ARGS__, NULL)); \
		g_assert (singleton_instance); \
		nm_singleton_instance_register (); \
		nm_log_dbg (LOGD_CORE, "create %s singleton (%p)", G_STRINGIFY (TYPE), singleton_instance); \
	} \
	return singleton_instance; \
}

/* attach @instance to the data or @owner. @owner owns a reference
 * to @instance thus the lifetime of @instance is at least as long
 * as that of @owner. Use this when @owner depends on @instance. */
#define NM_UTILS_KEEP_ALIVE(owner, instance, unique_token) \
    G_STMT_START { \
         g_object_set_data_full (G_OBJECT (owner), \
                                 ".nm-utils-keep-alive-" unique_token "", \
                                 g_object_ref (instance), \
                                 g_object_unref); \
    } G_STMT_END

/*****************************************************************************/

/**
 * NMUtilsError:
 * @NM_UTILS_ERROR_UNKNOWN: unknown or unclassified error
 * @NM_UTILS_ERROR_CANCELLED_DISPOSING: when disposing an object that has
 *   pending aynchronous operations, the operation is cancelled with this
 *   error reason. Depending on the usage, this might indicate a bug because
 *   usually the target object should stay alive as long as there are pending
 *   operations.
 */
typedef enum {
	NM_UTILS_ERROR_UNKNOWN = 0,                 /*< nick=Unknown >*/
	NM_UTILS_ERROR_CANCELLED_DISPOSING,         /*< nick=CancelledDisposing >*/
} NMUtilsError;

#define NM_UTILS_ERROR (nm_utils_error_quark ())
GQuark nm_utils_error_quark (void);

void nm_utils_error_set_cancelled (GError **error,
                                   gboolean is_disposing,
                                   const char *instance_name);
gboolean nm_utils_error_is_cancelled (GError *error,
                                      gboolean consider_is_disposing);

/*****************************************************************************/

gint nm_utils_ascii_str_to_bool (const char *str,
                                 gint default_value);

/*****************************************************************************/

gboolean nm_ethernet_address_is_valid (gconstpointer addr, gssize len);

in_addr_t nm_utils_ip4_address_clear_host_address (in_addr_t addr, guint8 plen);
const struct in6_addr *nm_utils_ip6_address_clear_host_address (struct in6_addr *dst, const struct in6_addr *src, guint8 plen);

/**
 * nm_utils_ip6_route_metric_normalize:
 * @metric: the route metric
 *
 * For IPv6 route, kernel treats the value 0 as IP6_RT_PRIO_USER (1024).
 * Thus, when comparing metric (values), we want to treat zero as NM_PLATFORM_ROUTE_METRIC_DEFAULT_IP6.
 *
 * Returns: @metric, if @metric is not zero, otherwise 1024.
 */
static inline guint32
nm_utils_ip6_route_metric_normalize (guint32 metric)
{
	return metric ? metric : 1024 /*NM_PLATFORM_ROUTE_METRIC_DEFAULT_IP6*/;
}

int nm_spawn_process (const char *args, GError **error);

int nm_utils_modprobe (GError **error, gboolean suppress_error_loggin, const char *arg1, ...) G_GNUC_NULL_TERMINATED;

/**
 * str_if_set:
 * @str: input string that will be returned if @str is not %NULL
 * @fallback: if @str is %NULL, return @fallback instead
 *
 * This utility function is useful when printing a string to avoid passing
 * %NULL. E.g. printf ("%s", str_if_set (get_string(), "(none)"));
 *
 * Returns: either @str or @fallback, depending on whether @str is %NULL.
 */
static inline const char *
str_if_set (const char *str, const char *fallback)
{
	return str ? str : fallback;
}

guint64 nm_utils_get_start_time_for_pid (pid_t pid, char *out_state, pid_t *out_ppid);

void nm_utils_kill_process_sync (pid_t pid, guint64 start_time, int sig, guint64 log_domain,
                                 const char *log_name, guint32 wait_before_kill_msec,
                                 guint32 sleep_duration_msec, guint32 max_wait_msec);

typedef void (*NMUtilsKillChildAsyncCb) (pid_t pid, gboolean success, int child_status, void *user_data);
void nm_utils_kill_child_async (pid_t pid, int sig, guint64 log_domain, const char *log_name,
                                guint32 wait_before_kill_msec,
                                NMUtilsKillChildAsyncCb callback, void *user_data);
gboolean nm_utils_kill_child_sync (pid_t pid, int sig, guint64 log_domain, const char *log_name,
                                   int *child_status, guint32 wait_before_kill_msec,
                                   guint32 sleep_duration_msec);

const char *nm_utils_find_helper (const char *progname,
                                  const char *try_first,
                                  GError **error);

typedef enum {
	NM_MATCH_SPEC_NO_MATCH  = 0,
	NM_MATCH_SPEC_MATCH     = 1,
	NM_MATCH_SPEC_NEG_MATCH = 2,
} NMMatchSpecMatchType;

NMMatchSpecMatchType nm_match_spec_device_type (const GSList *specs, const char *device_type);
NMMatchSpecMatchType nm_match_spec_hwaddr (const GSList *specs, const char *hwaddr);
NMMatchSpecMatchType nm_match_spec_s390_subchannels (const GSList *specs, const char *subchannels);
NMMatchSpecMatchType nm_match_spec_interface_name (const GSList *specs, const char *interface_name);
NMMatchSpecMatchType nm_match_spec_match_config (const GSList *specs, guint nm_version, const char *env);
GSList *nm_match_spec_split (const char *value);
char *nm_match_spec_join (GSList *specs);

extern char _nm_utils_to_string_buffer[2096];

void     nm_utils_to_string_buffer_init (char **buf, gsize *len);
gboolean nm_utils_to_string_buffer_init_null (gconstpointer obj, char **buf, gsize *len);

typedef struct {
	unsigned flag;
	const char *name;
} NMUtilsFlags2StrDesc;

#define NM_UTILS_FLAGS2STR(f, n) { .flag = f, .name = ""n, }

const char *nm_utils_flags2str (const NMUtilsFlags2StrDesc *descs,
                                gsize n_descs,
                                unsigned flags,
                                char *buf,
                                gsize len);

void nm_utils_strbuf_append (char **buf, gsize *len, const char *format, ...) __attribute__((__format__ (__printf__, 3, 4)));
void nm_utils_strbuf_append_c (char **buf, gsize *len, char c);
void nm_utils_strbuf_append_str (char **buf, gsize *len, const char *str);

const char *nm_utils_get_shared_wifi_permission (NMConnection *connection);

const char *nm_utils_get_ip_config_method (NMConnection *connection,
                                           GType         ip_setting_type);

void nm_utils_complete_generic (NMConnection *connection,
                                const char *ctype,
                                const GSList *existing,
                                const char *preferred_id,
                                const char *fallback_id_prefix,
                                const char *ifname_prefix,
                                gboolean default_enable_ipv6);

char *nm_utils_new_vlan_name (const char *parent_iface, guint32 vlan_id);

GPtrArray *nm_utils_read_resolv_conf_nameservers (const char *rc_contents);
GPtrArray *nm_utils_read_resolv_conf_dns_options (const char *rc_contents);

typedef gboolean (NMUtilsMatchFilterFunc) (NMConnection *connection, gpointer user_data);

NMConnection *nm_utils_match_connection (GSList *connections,
                                         NMConnection *original,
                                         gboolean device_has_carrier,
                                         NMUtilsMatchFilterFunc match_filter_func,
                                         gpointer match_filter_data);

int nm_utils_cmp_connection_by_autoconnect_priority (NMConnection **a, NMConnection **b);

void nm_utils_log_connection_diff (NMConnection *connection, NMConnection *diff_base, guint32 level, guint64 domain, const char *name, const char *prefix);

#define NM_UTILS_NS_PER_SECOND  ((gint64) 1000000000)
gint64 nm_utils_get_monotonic_timestamp_ns (void);
gint64 nm_utils_get_monotonic_timestamp_us (void);
gint64 nm_utils_get_monotonic_timestamp_ms (void);
gint32 nm_utils_get_monotonic_timestamp_s (void);
gint64 nm_utils_monotonic_timestamp_as_boottime (gint64 timestamp, gint64 timestamp_ticks_per_ns);

gboolean    nm_utils_is_valid_path_component (const char *name);
const char *ASSERT_VALID_PATH_COMPONENT (const char *name);
const char *nm_utils_ip6_property_path (const char *ifname, const char *property);
const char *nm_utils_ip4_property_path (const char *ifname, const char *property);

gboolean nm_utils_is_specific_hostname (const char *name);

/* IPv6 Interface Identifer helpers */

/**
 * NMUtilsIPv6IfaceId:
 * @id: convenience member for validity checking; never use directly
 * @id_u8: the 64-bit Interface Identifier
 *
 * Holds a 64-bit IPv6 Interface Identifier.  The IID is a sequence of bytes
 * and should not normally be treated as a %guint64, but this is done for
 * convenience of validity checking and initialization.
 */
struct _NMUtilsIPv6IfaceId {
	union {
		guint64 id;
		guint8  id_u8[8];
	};
};

#define NM_UTILS_IPV6_IFACE_ID_INIT { { .id = 0 } }

gboolean nm_utils_get_ipv6_interface_identifier (NMLinkType link_type,
                                                 const guint8 *hwaddr,
                                                 guint len,
                                                 guint dev_id,
                                                 NMUtilsIPv6IfaceId *out_iid);

void nm_utils_ipv6_addr_set_interface_identfier (struct in6_addr *addr,
                                                 const NMUtilsIPv6IfaceId iid);

gboolean nm_utils_ipv6_addr_set_stable_privacy (struct in6_addr *addr,
                                                const char *ifname,
                                                const char *uuid,
                                                guint dad_counter,
                                                GError **error);

void nm_utils_ipv6_interface_identfier_get_from_addr (NMUtilsIPv6IfaceId *iid,
                                                      const struct in6_addr *addr);

void nm_utils_array_remove_at_indexes (GArray *array, const guint *indexes_to_delete, gsize len);

void nm_utils_setpgid (gpointer unused);

typedef enum {
	NM_UTILS_TEST_NONE                              = 0,

	/* Internal flag, marking that either nm_utils_get_testing() or _nm_utils_set_testing() was called. */
	_NM_UTILS_TEST_INITIALIZED                      = (1LL << 0),

	/* Indicate that test mode is enabled in general. Explicitly calling _nm_utils_set_testing() will always set this flag. */
	_NM_UTILS_TEST_GENERAL                          = (1LL << 1),

	/* Don't check the owner of keyfiles during testing. */
	NM_UTILS_TEST_NO_KEYFILE_OWNER_CHECK            = (1LL << 2),

	_NM_UTILS_TEST_LAST,
	NM_UTILS_TEST_ALL                               = (((_NM_UTILS_TEST_LAST - 1) << 1) - 1) & ~(_NM_UTILS_TEST_INITIALIZED),
} NMUtilsTestFlags;

gboolean nm_utils_get_testing_initialized (void);
NMUtilsTestFlags nm_utils_get_testing (void);
void _nm_utils_set_testing (NMUtilsTestFlags flags);

void nm_utils_g_value_set_object_path (GValue *value, gpointer object);
void nm_utils_g_value_set_strv (GValue *value, GPtrArray *strings);

/**
 * NMUtilsObjectFunc:
 * @object: the object to filter on
 * @user_data: data passed to the function from the caller
 *
 * Returns: %TRUE if the object should be used, %FALSE if not
 */
typedef gboolean (*NMUtilsObjectFunc) (GObject *object, gpointer user_data);

void nm_utils_g_value_set_object_path_array (GValue *value,
                                             GSList *objects,
                                             NMUtilsObjectFunc filter_func,
                                             gpointer user_data);

#endif /* __NETWORKMANAGER_UTILS_H__ */
