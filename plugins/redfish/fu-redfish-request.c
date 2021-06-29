/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-redfish-request.h"

struct _FuRedfishRequest {
	GObject			 parent_instance;
	CURL			*curl;
	CURLU			*uri;
	GByteArray		*buf;
	glong			 status_code;
	JsonParser		*json_parser;
	JsonObject		*json_obj;
};

G_DEFINE_TYPE (FuRedfishRequest, fu_redfish_request, G_TYPE_OBJECT)

typedef gchar curlptr;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(curlptr, curl_free)

JsonObject *
fu_redfish_request_get_json_object (FuRedfishRequest *self)
{
	g_return_val_if_fail (FU_IS_REDFISH_REQUEST (self), NULL);
	return self->json_obj;
}

CURL *
fu_redfish_request_get_curl (FuRedfishRequest *self)
{
	g_return_val_if_fail (FU_IS_REDFISH_REQUEST (self), NULL);
	return self->curl;
}

CURLU *
fu_redfish_request_get_uri (FuRedfishRequest *self)
{
	g_return_val_if_fail (FU_IS_REDFISH_REQUEST (self), NULL);
	return self->uri;
}

glong
fu_redfish_request_get_status_code (FuRedfishRequest *self)
{
	g_return_val_if_fail (FU_IS_REDFISH_REQUEST (self), G_MAXLONG);
	return self->status_code;
}

gboolean
fu_redfish_request_perform (FuRedfishRequest *self,
			    const gchar *path,
			    FuRedfishRequestPerformFlags flags,
			    GError **error)
{
	CURLcode res;
	g_autoptr(curlptr) uri_str = NULL;

	g_return_val_if_fail (FU_IS_REDFISH_REQUEST (self), FALSE);
	g_return_val_if_fail (self->status_code == 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* do request */
	if (path != NULL)
		curl_url_set (self->uri, CURLUPART_PATH, path, 0);
	curl_url_get (self->uri, CURLUPART_URL, &uri_str, 0);
	res = curl_easy_perform (self->curl);
	curl_easy_getinfo (self->curl, CURLINFO_RESPONSE_CODE, &self->status_code);
	if (g_getenv ("FWUPD_REDFISH_VERBOSE") != NULL) {
		g_autofree gchar *str = NULL;
		str = g_strndup ((const gchar *) self->buf->data,
				 self->buf->len);
		g_debug ("%s: %s [%li]", uri_str, str, self->status_code);
	}

	/* check result */
	if (res != CURLE_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to request %s: %s",
			     uri_str, curl_easy_strerror (res));
		return FALSE;
	}

	/* load JSON */
	if (flags & FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON) {
		JsonNode *json_root;
		if (!json_parser_load_from_data (self->json_parser,
						 (const gchar *) self->buf->data,
						 (gssize) self->buf->len,
						 error)) {
			g_prefix_error (error,
					"failed to parse node for %s: ",
					uri_str);
			return FALSE;
		}
		json_root = json_parser_get_root (self->json_parser);
		if (json_root == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no JSON root node for %s",
				     uri_str);
			return FALSE;
		}
		self->json_obj = json_node_get_object (json_root);
		if (self->json_obj == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no JSON object for %s",
				     uri_str);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static size_t
fu_redfish_request_write_cb (char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GByteArray *buf = (GByteArray *) userdata;
	gsize realsize = size * nmemb;
	g_byte_array_append (buf, (const guint8 *) ptr, realsize);
	return realsize;
}

static void
fu_redfish_request_init (FuRedfishRequest *self)
{
	self->curl = curl_easy_init ();
	self->uri = curl_url ();
	self->buf = g_byte_array_new ();
	self->json_parser = json_parser_new ();
	curl_easy_setopt (self->curl, CURLOPT_WRITEFUNCTION, fu_redfish_request_write_cb);
	curl_easy_setopt (self->curl, CURLOPT_WRITEDATA, self->buf);
}

static void
fu_redfish_request_finalize (GObject *object)
{
	FuRedfishRequest *self = FU_REDFISH_REQUEST (object);
	g_object_unref (self->json_parser);
	g_byte_array_unref (self->buf);
	curl_easy_cleanup (self->curl);
	curl_url_cleanup (self->uri);
	G_OBJECT_CLASS (fu_redfish_request_parent_class)->finalize (object);
}

static void
fu_redfish_request_class_init (FuRedfishRequestClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_redfish_request_finalize;
}
