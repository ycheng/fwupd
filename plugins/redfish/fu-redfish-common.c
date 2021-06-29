/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-redfish-common.h"

gchar *
fu_redfish_common_buffer_to_ipv4 (const guint8 *buffer)
{
	GString *str = g_string_new (NULL);
	for (guint i = 0; i < 4; i++) {
		g_string_append_printf (str, "%u", buffer[i]);
		if (i != 3)
			g_string_append (str, ".");
	}
	return g_string_free (str, FALSE);
}

gchar *
fu_redfish_common_buffer_to_ipv6 (const guint8 *buffer)
{
	GString *str = g_string_new (NULL);
	for (guint i = 0; i < 16; i += 4) {
		g_string_append_printf (str, "%02x%02x%02x%02x",
					buffer[i+0], buffer[i+1],
					buffer[i+2], buffer[i+3]);
		if (i != 12)
			g_string_append (str, ":");
	}
	return g_string_free (str, FALSE);
}

gchar *
fu_redfish_common_buffer_to_mac (const guint8 *buffer)
{
	GString *str = g_string_new (NULL);
	for (guint i = 0; i < 6; i++) {
		g_string_append_printf (str, "%02X", buffer[i]);
		if (i != 5)
			g_string_append (str, ":");
	}
	return g_string_free (str, FALSE);
}

gchar *
fu_redfish_common_fix_version (const gchar *version)
{
	g_auto(GStrv) split = NULL;

	/* find the section preficed with "v" */
	split = g_strsplit (version, " ", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		if (g_str_has_prefix (split[i], "v")) {
			g_debug ("using %s for %s", split[i] + 1, version);
			return g_strdup (split[i] + 1);
		}
	}

	/* find the thing with dots */
	for (guint i = 0; split[i] != NULL; i++) {
		if (g_strstr_len (split[i], -1, ".")) {
			g_debug ("using %s for %s", split[i], version);
			return g_strdup (split[i]);
		}
	}

	/* we failed to do anything clever */
	return g_strdup (version);
}
