/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* nm-platform.c - Handle runtime kernel networking configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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

#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "nm-default.h"
#include "nm-lldp-listener.h"

#include "nm-test-utils.h"

static int
create_tap (char *name, int *out_ifindex, guint8 *out_mac)
{
	struct ifreq ifr = { };
	int fd, tmp = -1;

	fd = open ("/dev/net/tun", O_RDWR);
	if (fd < 0)
		return -1;

	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strncpy (ifr.ifr_name, name, IFNAMSIZ);
	if (ioctl (fd, TUNSETIFF, &ifr) < 0)
		goto out_err;

	*out_ifindex = if_nametoindex (name);

	/* Bring the interface up */
	tmp = socket (AF_INET, SOCK_DGRAM, 0);
	ifr.ifr_flags |= IFF_UP;
	if (ioctl (tmp, SIOCSIFFLAGS, &ifr) < 0)
		goto out_err;

	/* Get its MAC address */
	if (ioctl (tmp, SIOCGIFHWADDR, &ifr) < 0)
		goto out_err;

	memcpy (out_mac, ifr.ifr_addr.sa_data, ETH_ALEN);
	close (tmp);

	return fd;

out_err:
	if (fd >= 0)
		close (fd);
	if (tmp >= 0)
		close (tmp);
	return -1;
}

typedef struct {
	int num_called;
} TestInfo;

static gboolean
loop_quit (gpointer user_data)
{
	g_main_loop_quit ((GMainLoop *) user_data);
	return G_SOURCE_REMOVE;
}

static void
lldp_neighbors_changed (NMLldpListener *lldp_listener, GParamSpec *pspec,
                        gpointer user_data)
{
	TestInfo *info = user_data;

	info->num_called++;
}

static GVariant *
get_lldp_neighbor_attribute (GVariant *neighbors,
                             const char *chassis, const char *port,
                             const char *name)
{
	GVariantIter iter, attrs_iter;
	GVariant *variant, *attr_variant;
	const char *attr_name;
	GVariant *chassis_v, *port_v, *attr_v;

	g_return_val_if_fail (g_variant_is_of_type (neighbors,
	                                            G_VARIANT_TYPE ("aa{sv}")),
	                      NULL);
	g_variant_iter_init (&iter, neighbors);

	while (g_variant_iter_next (&iter, "@a{sv}", &variant)) {
		g_variant_iter_init (&attrs_iter, variant);
		chassis_v = NULL;
		port_v = NULL;
		attr_v = NULL;

		while (g_variant_iter_next (&attrs_iter, "{&sv}", &attr_name, &attr_variant)) {
			if (!g_strcmp0 (attr_name, NM_LLDP_ATTR_CHASSIS_ID))
				chassis_v = attr_variant;
			else if (!g_strcmp0 (attr_name, NM_LLDP_ATTR_PORT_ID))
				port_v = attr_variant;
			else if (!g_strcmp0 (attr_name, name))
				attr_v = attr_variant;
		}

		if (   g_variant_is_of_type (chassis_v, G_VARIANT_TYPE_STRING)
		    && g_variant_is_of_type (port_v, G_VARIANT_TYPE_STRING)
		    && !g_strcmp0 (chassis, g_variant_get_string (chassis_v, NULL))
		    && !g_strcmp0 (port, g_variant_get_string (port_v, NULL))) {

			g_variant_ref (attr_v);
			g_variant_unref (variant);

			return attr_v;
		}
		g_variant_unref (variant);
	}

	/* neighbor not found */
	return NULL;
}

static void
test_receive_frame (void)
{
	NMLldpListener *listener;
	int fd, ifindex = -1;
	guint8 mac[ETH_ALEN];
	GMainLoop *loop;
	TestInfo info = { };
	GVariant *neighbors, *attr;
	uint8_t frame[] = {
		/* Ethernet header */
		0x01, 0x80, 0xc2, 0x00, 0x00, 0x03,     /* Destination MAC*/
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06,     /* Source MAC */
		0x88, 0xcc,                             /* Ethertype */
		/* LLDP mandatory TLVs */
		0x02, 0x07, 0x04, 0x00, 0x01, 0x02,     /* Chassis: MAC, 00:01:02:03:04:05 */
		0x03, 0x04, 0x05,
		0x04, 0x04, 0x05, 0x31, 0x2f, 0x33,     /* Port: interface name, "1/3" */
		0x06, 0x02, 0x00, 0x78,                 /* TTL: 120 seconds*/
		/* LLDP optional TLVs */
		0x08, 0x04, 0x50, 0x6f, 0x72, 0x74,     /* Port Description: "Port" */
		0x0a, 0x03, 0x53, 0x59, 0x53,           /* System Name: "SYS" */
		0x0c, 0x04, 0x66, 0x6f, 0x6f, 0x00,     /* System Description: "foo" (NULL-terminated) */
		0x00, 0x00                              /* End Of LLDPDU */
	};

	fd = create_tap ("nm-test-tap", &ifindex, mac);
	g_assert_cmpint (fd, >=, 0);
	g_assert_cmpint (ifindex, >=, 0);

	listener = nm_lldp_listener_new ();
	g_assert (listener != NULL);
	g_assert (nm_lldp_listener_start (listener, ifindex, "nm-test-tap", mac, ETH_ALEN, NULL));

	g_signal_connect (listener, "notify::" NM_LLDP_LISTENER_NEIGHBORS,
	                  (GCallback) lldp_neighbors_changed, &info);
	loop = g_main_loop_new (NULL, FALSE);
	g_timeout_add_seconds (1, loop_quit, loop);

	g_assert (write (fd, frame, sizeof (frame)) == sizeof (frame));
	g_assert (write (fd, frame, sizeof (frame)) == sizeof (frame));

	g_main_loop_run (loop);

	g_assert_cmpint (info.num_called, ==, 1);
	neighbors = nm_lldp_listener_get_neighbors (listener);
	g_assert (neighbors != NULL);

	/* Check port description */
	attr = get_lldp_neighbor_attribute (neighbors, "00:01:02:03:04:05", "1/3",
	                                    NM_LLDP_ATTR_PORT_DESCRIPTION);
	g_assert (attr != NULL);
	g_assert (g_variant_is_of_type (attr, G_VARIANT_TYPE_STRING));
	g_assert_cmpstr (g_variant_get_string (attr, NULL), ==, "Port");

	/* Check system name */
	attr = get_lldp_neighbor_attribute (neighbors, "00:01:02:03:04:05", "1/3",
	                                    NM_LLDP_ATTR_SYSTEM_NAME);
	g_assert (attr != NULL);
	g_assert (g_variant_is_of_type (attr, G_VARIANT_TYPE_STRING));
	g_assert_cmpstr (g_variant_get_string (attr, NULL), ==, "SYS");

	/* Check destination */
	attr = get_lldp_neighbor_attribute (neighbors, "00:01:02:03:04:05", "1/3",
	                                    NM_LLDP_ATTR_DESTINATION);
	g_assert (attr != NULL);
	g_assert (g_variant_is_of_type (attr, G_VARIANT_TYPE_STRING));
	g_assert_cmpstr (g_variant_get_string (attr, NULL), ==,
	                 NM_LLDP_DEST_NEAREST_NON_TPMR_BRIDGE);

	g_clear_pointer (&loop, g_main_loop_unref);
}

NMTST_DEFINE ();

int
main (int argc, char **argv)
{
	nmtst_init_assert_logging (&argc, &argv, "INFO", "DEFAULT");

	if (geteuid () != 0 || getegid () != 0) {
#ifdef REQUIRE_ROOT_TESTS
		g_print ("Fail test: requires root privileges (%s)\n", argv[0]);
		return EXIT_FAILURE;
#else
		g_print ("Skipping test: requires root privileges (%s)\n", argv[0]);
		return g_test_run ();
#endif
	}

	g_test_add_func ("/lldp/receive_frame", test_receive_frame);

	return g_test_run ();
}
