/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-redfish-backend.h"

#define FU_TYPE_REDFISH_DEVICE (fu_redfish_device_get_type ())
G_DECLARE_FINAL_TYPE (FuRedfishDevice, fu_redfish_device, FU, REDFISH_DEVICE, FuDevice)

FuDevice	*fu_redfish_device_new_from_object	(FuRedfishBackend	*backend,
							 JsonObject		*member);
