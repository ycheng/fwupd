/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include <fwupdplugin.h>

#include "fu-redfish-backend.h"
#include "fu-redfish-common.h"
#include "fu-redfish-device.h"
#include "fu-redfish-request.h"
#include "fu-redfish-smbios.h"

struct _FuRedfishBackend
{
	FuBackend		 parent_instance;
	gchar			*hostname;
	gchar			*username;
	gchar			*password;
	guint			 port;
	gchar			*update_uri_path;
	gchar			*push_uri_path;
	gboolean		 use_https;
	gboolean		 cacheck;
};

G_DEFINE_TYPE (FuRedfishBackend, fu_redfish_backend, FU_TYPE_BACKEND)


FuRedfishRequest *
fu_redfish_backend_request_new (FuRedfishBackend *self)
 {
	FuRedfishRequest *request = g_object_new (FU_TYPE_REDFISH_REQUEST, NULL);
	CURL *curl;
	CURLU *uri;
	g_autofree gchar *user_agent = NULL;
	g_autofree gchar *port = g_strdup_printf ("%u", self->port);

	/* set up defaults */
	curl = fu_redfish_request_get_curl (request);
	uri = fu_redfish_request_get_uri (request);
	curl_url_set (uri, CURLUPART_SCHEME, self->use_https ? "https" : "http", 0);
	curl_url_set (uri, CURLUPART_HOST, self->hostname, 0);
	curl_url_set (uri, CURLUPART_PORT, port, 0);
	curl_easy_setopt (curl, CURLOPT_CURLU, uri);

	/* since DSP0266 makes Basic Authorization a requirement,
	* it is safe to use Basic Auth for all implementations */
	curl_easy_setopt (curl, CURLOPT_HTTPAUTH, (glong) CURLAUTH_BASIC);
	curl_easy_setopt (curl, CURLOPT_USERNAME, self->username);
	curl_easy_setopt (curl, CURLOPT_PASSWORD, self->password);

	/* setup networking */
	user_agent = g_strdup_printf ("%s/%s", PACKAGE_NAME, PACKAGE_VERSION);
	curl_easy_setopt (curl, CURLOPT_USERAGENT , user_agent);
	curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 60L);
	if (!self->cacheck)
		curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 0L);

	/* success */
	return request;
 }

static gboolean
fu_redfish_backend_coldplug_member (FuRedfishBackend *self,
				    JsonObject *member,
				    GError **error)
{
	g_autoptr(FuDevice) dev = fu_redfish_device_new_from_object (self, member);
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;
	fu_backend_device_added (FU_BACKEND (self), dev);
	return TRUE;
}

static gboolean
fu_redfish_backend_coldplug_collection (FuRedfishBackend *self,
					JsonObject *collection,
					GError **error)
{
	JsonArray *members = json_object_get_array_member (collection, "Members");
	for (guint i = 0; i < json_array_get_length (members); i++) {
		JsonObject *json_obj;
		JsonObject *member_id;
		const gchar *member_uri;
		g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new (self);

		member_id = json_array_get_object_element (members, i);
		member_uri = json_object_get_string_member (member_id, "@odata.id");
		if (member_uri == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_FOUND,
					     "no @odata.id string");
			return FALSE;
		}

		/* create the device for the member */
		if (!fu_redfish_request_perform (request,
						 member_uri,
						 FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
						 error))
			  return FALSE;
		json_obj = fu_redfish_request_get_json_object (request);
		if (!fu_redfish_backend_coldplug_member (self, json_obj, error))
			  return FALSE;
	}
	return TRUE;
}

static gboolean
fu_redfish_backend_coldplug_inventory (FuRedfishBackend *self,
				       JsonObject *inventory,
				       GError **error)
{
	JsonObject *json_obj;
	const gchar *collection_uri;
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new (self);

	if (inventory == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no inventory object");
		return FALSE;
	}

	collection_uri = json_object_get_string_member (inventory, "@odata.id");
	if (collection_uri == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no @odata.id string");
		return FALSE;
	}

	if (!fu_redfish_request_perform (request,
					 collection_uri,
					 FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					 error))
		 return FALSE;
	json_obj = fu_redfish_request_get_json_object (request);
	return fu_redfish_backend_coldplug_collection (self, json_obj, error);
}

static gboolean
fu_redfish_backend_coldplug (FuBackend *backend, GError **error)
{
	FuRedfishBackend *self = FU_REDFISH_BACKEND (backend);
	JsonObject *json_obj;
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new (self);

	/* nothing set */
	if (self->update_uri_path == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no update_uri_path");
		return FALSE;
	}

	/* get the update service */
	if (!fu_redfish_request_perform (request,
					 self->update_uri_path,
					 FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					 error))
		 return FALSE;
	json_obj = fu_redfish_request_get_json_object (request);
	if (!json_object_get_boolean_member (json_obj, "ServiceEnabled")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "service is not enabled");
		return FALSE;
	}
	if (!json_object_has_member (json_obj, "HttpPushUri")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "HttpPushUri is not available");
		return FALSE;
	}
	self->push_uri_path = g_strdup (json_object_get_string_member (json_obj, "HttpPushUri"));
	if (self->push_uri_path == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "HttpPushUri is invalid");
		return FALSE;
	}
	if (json_object_has_member (json_obj, "FirmwareInventory")) {
		JsonObject *tmp = json_object_get_object_member (json_obj, "FirmwareInventory");
		return fu_redfish_backend_coldplug_inventory (self, tmp, error);
	}
	if (json_object_has_member (json_obj, "SoftwareInventory")) {
		JsonObject *tmp = json_object_get_object_member (json_obj, "SoftwareInventory");
		return fu_redfish_backend_coldplug_inventory (self, tmp, error);
	}
	return TRUE;
}

static gboolean
fu_redfish_backend_setup (FuBackend *backend, GError **error)
{
	FuRedfishBackend *self = FU_REDFISH_BACKEND (backend);
	JsonObject *json_obj;
	JsonObject *json_update_service = NULL;
	const gchar *data_id;
	const gchar *version = NULL;
	const gchar *uuid = NULL;
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new (self);

	/* sanity check */
	if (self->port == 0 || self->port > G_MAXUINT16) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid port specified: 0x%x",
			     self->port);
		return FALSE;
	}

	/* try to connect */
	if (!fu_redfish_request_perform (request,
					 "/redfish/v1/",
					 FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
					 error))
		return FALSE;
	json_obj = fu_redfish_request_get_json_object (request);
	if (json_object_has_member (json_obj, "ServiceVersion")) {
		version = json_object_get_string_member (json_obj, "ServiceVersion");
	} else if (json_object_has_member (json_obj, "RedfishVersion")) {
		version = json_object_get_string_member (json_obj, "RedfishVersion");
	}
	if (json_object_has_member (json_obj, "UUID"))
		uuid = json_object_get_string_member (json_obj, "UUID");
	g_debug ("Version: %s", version);
	g_debug ("UUID: %s", uuid);

	if (json_object_has_member (json_obj, "UpdateService"))
		json_update_service = json_object_get_object_member (json_obj, "UpdateService");
	if (json_update_service == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no UpdateService object");
		return FALSE;
	}
	data_id = json_object_get_string_member (json_update_service, "@odata.id");
	if (data_id == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no @odata.id string");
		return FALSE;
	}
	self->update_uri_path = g_strdup (data_id);
	return TRUE;
}

void
fu_redfish_backend_set_hostname (FuRedfishBackend *self, const gchar *hostname)
{
	g_free (self->hostname);
	self->hostname = g_strdup (hostname);
}

void
fu_redfish_backend_set_port (FuRedfishBackend *self, guint port)
{
	self->port = port;
}

void
fu_redfish_backend_set_https (FuRedfishBackend *self, gboolean use_https)
{
	self->use_https = use_https;
}

void
fu_redfish_backend_set_cacheck (FuRedfishBackend *self, gboolean cacheck)
{
	self->cacheck = cacheck;
}

void
fu_redfish_backend_set_username (FuRedfishBackend *self, const gchar *username)
{
	g_free (self->username);
	self->username = g_strdup (username);
}

void
fu_redfish_backend_set_password (FuRedfishBackend *self, const gchar *password)
{
	g_free (self->password);
	self->password = g_strdup (password);
}

const gchar *
fu_redfish_backend_get_push_uri_path (FuRedfishBackend *self)
{
	return self->push_uri_path;
}

static void
fu_redfish_backend_finalize (GObject *object)
{
	FuRedfishBackend *self = FU_REDFISH_BACKEND (object);
	g_free (self->update_uri_path);
	g_free (self->push_uri_path);
	g_free (self->hostname);
	g_free (self->username);
	g_free (self->password);
	G_OBJECT_CLASS (fu_redfish_backend_parent_class)->finalize (object);
}

static void
fu_redfish_backend_class_init (FuRedfishBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuBackendClass *klass_backend = FU_BACKEND_CLASS (klass);
	klass_backend->coldplug = fu_redfish_backend_coldplug;
	klass_backend->setup = fu_redfish_backend_setup;
	object_class->finalize = fu_redfish_backend_finalize;
}

static void
fu_redfish_backend_init (FuRedfishBackend *self)
{
}

FuRedfishBackend *
fu_redfish_backend_new (FuContext *ctx)
{
	return FU_REDFISH_BACKEND (g_object_new (FU_REDFISH_TYPE_BACKEND,
						 "name", "redfish",
						 "context", ctx,
						 NULL));
}
