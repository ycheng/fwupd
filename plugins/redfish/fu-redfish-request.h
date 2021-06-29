/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <curl/curl.h>

#ifndef HAVE_LIBCURL_7_62_0
#error libcurl >= 7.62.0 required
#endif

#define FU_TYPE_REDFISH_REQUEST (fu_redfish_request_get_type ())
G_DECLARE_FINAL_TYPE (FuRedfishRequest, fu_redfish_request, FU, REDFISH_REQUEST, GObject)

typedef enum {
	FU_REDFISH_REQUEST_PERFORM_FLAG_NONE,
	FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
} FuRedfishRequestPerformFlags;

gboolean	 fu_redfish_request_perform		(FuRedfishRequest	*self,
							 const gchar		*path,
							 FuRedfishRequestPerformFlags flags,
							 GError			**error);
JsonObject	*fu_redfish_request_get_json_object	(FuRedfishRequest	*self);
CURL		*fu_redfish_request_get_curl		(FuRedfishRequest	*self);
CURLU		*fu_redfish_request_get_uri		(FuRedfishRequest	*self);
glong		 fu_redfish_request_get_status_code	(FuRedfishRequest	*self);
