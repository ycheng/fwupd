/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-redfish-common.h"
#include "fu-redfish-device.h"

struct _FuRedfishDevice {
	FuDevice		 parent_instance;
	FuRedfishBackend	*backend;
	JsonObject		*member;
};

G_DEFINE_TYPE (FuRedfishDevice, fu_redfish_device, FU_TYPE_DEVICE)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(curl_mime, curl_mime_free)

static gboolean
fu_redfish_device_probe (FuDevice *dev, GError **error)
{
	FuRedfishDevice *self = FU_REDFISH_DEVICE (dev);
	JsonObject *member = self->member;
	const gchar *guid = NULL;
	g_autofree gchar *guid_lower = NULL;

	/* required to POST later */
	if (!json_object_has_member (member, "@odata.id")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no @odata.id string");
		return FALSE;
	}
	fu_device_set_physical_id (dev, "Redfish-Inventory");
	fu_device_set_logical_id (dev, json_object_get_string_member (member, "@odata.id"));
	if (json_object_has_member (member, "Id"))
		fu_device_set_backend_id (dev, json_object_get_string_member (member, "Id"));

	if (json_object_has_member (member, "SoftwareId")) {
		guid = json_object_get_string_member (member, "SoftwareId");
	} else if (json_object_has_member (member, "Oem")) {
		JsonObject *oem = json_object_get_object_member (member, "Oem");
		if (oem != NULL && json_object_has_member (oem, "Hpe")) {
			JsonObject *hpe = json_object_get_object_member (oem, "Hpe");
			if (hpe != NULL && json_object_has_member (hpe, "DeviceClass"))
				guid = json_object_get_string_member (hpe, "DeviceClass");
		}
	}

	/* GUID is required */
	if (guid == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no GUID for device");
		return FALSE;
	}
	guid_lower = g_ascii_strdown (guid, -1);
	fu_device_add_guid (dev, guid_lower);

	/* device properties */
	if (json_object_has_member (member, "Manufacturer")) {
		const gchar *vendor = json_object_get_string_member (member, "Manufacturer");
		g_autofree gchar *vendor_upper = g_ascii_strup (vendor, -1);
		g_autofree gchar *vendor_id = g_strdup_printf ("REDFISH:%s", vendor_upper);
		fu_device_set_vendor (dev, vendor);
		fu_device_add_vendor_id (dev, vendor_id);
	}
	if (json_object_has_member (member, "Name"))
		fu_device_set_name (dev, json_object_get_string_member (member, "Name"));
	if (json_object_has_member (member, "Version")) {
		const gchar *ver = json_object_get_string_member (member, "Version");
		g_autofree gchar *tmp = fu_redfish_common_fix_version (ver);
		fu_device_set_version (dev, tmp);
		fu_device_set_version_format (dev, fu_common_version_guess_format (tmp));
	}
	if (json_object_has_member (member, "LowestSupportedVersion")) {
		const gchar *ver = json_object_get_string_member (member, "LowestSupportedVersion");
		g_autofree gchar *tmp = fu_redfish_common_fix_version (ver);
		fu_device_set_version_lowest (dev, tmp);
	}
	if (json_object_has_member (member, "Description"))
		fu_device_set_description (dev, json_object_get_string_member (member, "Description"));
	if (json_object_has_member (member, "Updateable")) {
		if (json_object_get_boolean_member (member, "Updateable"))
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	} else {
		/* assume the device is updatable */
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	}

	/* success */
	return TRUE;
}

static GString *
fu_redfish_device_get_parameters (FuRedfishDevice *self)
{
	g_autoptr(GString) str = g_string_new (NULL);
	g_autoptr(JsonBuilder) builder = json_builder_new ();
	g_autoptr(JsonGenerator) json_generator = json_generator_new ();
	g_autoptr(JsonNode) json_root = NULL;

	/* create header */
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "Targets");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, fu_device_get_logical_id (FU_DEVICE (self)));
	json_builder_end_array (builder);
	json_builder_set_member_name (builder, "@Redfish.OperationApplyTime");
	json_builder_add_string_value (builder, "Immediate");
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	json_generator_to_gstring (json_generator, str);
	return g_steal_pointer (&str);
}

static size_t
fu_redfish_device_header_callback_cb (char *buffer, size_t size, size_t nitems, void *userdata)
{
	gchar **location = (gchar **) userdata;
	if (*location == NULL) {
		if (g_str_has_prefix (buffer, "Location: ")) {
			gchar *tmp = g_strdup (buffer + 10);
			g_strdelimit (tmp, "\n\r", '\0');
			*location = tmp;
		}
	}
	return nitems * size;
}

static gboolean
fu_redfish_device_poll_task_manager (FuRedfishDevice *self, const gchar *uri, GError **error)
{
	for (guint i = 0; i < 2400; i++) {
		JsonObject *json_obj;
		CURL *curl;
		const gchar *state_tmp;
		g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new (self->backend);

		/* create URI and poll */
		curl = fu_redfish_request_get_curl (request);
		curl_easy_setopt (curl, CURLOPT_CURLU, NULL);
		if (curl_easy_setopt (curl, CURLOPT_URL, uri) != CURLE_OK) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "failed to set URI %s", uri);
			return FALSE;
		}
		if (!fu_redfish_request_perform (request,
						 NULL,
						 FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON,
						 error))
			return FALSE;
		json_obj = fu_redfish_request_get_json_object (request);
		if (!json_object_has_member (json_obj, "TaskState")) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "no TaskState for task manager");
			return FALSE;
		}
		state_tmp = json_object_get_string_member (json_obj, "TaskState");
		g_debug ("TaskState now %s", state_tmp);
		if (g_strcmp0 (state_tmp, "Completed") == 0)
			return TRUE;

		/* sleep and wait for hardware */
		g_usleep (G_USEC_PER_SEC);
	}

	/* success */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_INVALID_FILE,
		     "failed to poll %s for success after 2400 seconds",
		     uri);
	return FALSE;
}

static gboolean
fu_redfish_device_write_firmware (FuDevice *device,
				  FuFirmware *firmware,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuRedfishDevice *self = FU_REDFISH_DEVICE (device);
	CURL *curl;
	curl_mimepart *part;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *location = NULL;
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new (self->backend);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GString) params = NULL;
	g_autoptr(curl_mime) mime = NULL;

	/* get default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* Get the update version */
	filename = g_strdup_printf ("%s.bin", fu_device_get_name (device));

	/* create the multipart request */
	curl = fu_redfish_request_get_curl (request);
	mime = curl_mime_init (curl);
	curl_easy_setopt (curl, CURLOPT_MIMEPOST, mime);
	curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, fu_redfish_device_header_callback_cb);
	curl_easy_setopt (curl, CURLOPT_HEADERDATA, &location);

	params = fu_redfish_device_get_parameters (FU_REDFISH_DEVICE (device));
	part = curl_mime_addpart (mime);
	curl_mime_name (part, "UpdateParameters");
	curl_mime_type (part, "application/json");
	curl_mime_data (part, params->str, CURL_ZERO_TERMINATED);

	part = curl_mime_addpart (mime);
	curl_mime_name (part, "UpdateFile");
	curl_mime_type (part, "application/octet-stream");
	curl_mime_filedata (part, filename);
	curl_mime_data (part, g_bytes_get_data (fw, NULL), g_bytes_get_size (fw));

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_redfish_request_perform (request,
					 fu_redfish_backend_get_push_uri_path (self->backend),
					 FU_REDFISH_REQUEST_PERFORM_FLAG_NONE,
					 error))
		return FALSE;
	if (fu_redfish_request_get_status_code (request) != 202) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to upload %s: %li",
			     filename,
			     fu_redfish_request_get_status_code (request));
		return FALSE;
	}
	if (location == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "no task monitor returned for %s",
			     fu_redfish_backend_get_push_uri_path (self->backend));
		return FALSE;
	}

	/* poll the task monitor for progress */
	return fu_redfish_device_poll_task_manager (self, location, error);
}

static void
fu_redfish_device_init (FuRedfishDevice *self)
{
	fu_device_set_summary (FU_DEVICE (self), "Redfish device");
	fu_device_add_protocol (FU_DEVICE (self), "org.dmtf.redfish");
}

static void
fu_redfish_device_finalize (GObject *object)
{
	FuRedfishDevice *self = FU_REDFISH_DEVICE (object);
	g_object_unref (self->backend);
	json_object_unref (self->member);
	G_OBJECT_CLASS (fu_redfish_device_parent_class)->finalize (object);
}

static void
fu_redfish_device_class_init (FuRedfishDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_redfish_device_finalize;
	klass_device->probe = fu_redfish_device_probe;
	klass_device->write_firmware = fu_redfish_device_write_firmware;
}

FuDevice *
fu_redfish_device_new_from_object (FuRedfishBackend *backend, JsonObject *member)
{
	g_autoptr(FuRedfishDevice) self = g_object_new (FU_TYPE_REDFISH_DEVICE, NULL);
	self->backend = g_object_ref (backend);
	self->member = json_object_ref (member);
	return FU_DEVICE (g_steal_pointer (&self));
}
