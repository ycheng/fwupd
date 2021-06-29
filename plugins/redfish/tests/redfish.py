#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import json

from flask import Flask, Response, request, g

app = Flask(__name__)

HARDCODED_USERNAME = "username2"
HARDCODED_PASSWORD = "password2"

app._percentage: int = 0


def _failure(msg: str, status=400):
    response = json.dumps({"msg": msg}, indent=4, separators=(",", ": "))
    return Response(response=response, status=401, mimetype="application/json")


@app.route("/redfish/v1/")
def index():

    # check password from the config file
    try:
        if (
            request.authorization["username"] != HARDCODED_USERNAME
            or request.authorization["password"] != HARDCODED_PASSWORD
        ):
            return _failure("unauthorised", status=401)
    except (KeyError, TypeError):
        return _failure("invalid")

    res = {
        "@odata.id": "/redfish/v1/",
        "RedfishVersion": "1.6.0",
        "UUID": "92384634-2938-2342-8820-489239905423",
        "UpdateService": {"@odata.id": "/redfish/v1/UpdateService"},
    }
    response = json.dumps(res, indent=4, separators=(",", ": "))
    return Response(response=response, status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService")
def update_service():

    res = {
        "@odata.id": "/redfish/v1/UpdateService",
        "@odata.type": "#UpdateService.v1_8_0.UpdateService",
        "FirmwareInventory": {
            "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory"
        },
        "HttpPushUri": "/FWUpdate",
        "HttpPushUriOptions": {
            "HttpPushUriApplyTime": {
                "ApplyTime": "Immediate",
            }
        },
        "HttpPushUriOptionsBusy": False,
        "ServiceEnabled": True,
    }
    response = json.dumps(res, indent=4, separators=(",", ": "))
    return Response(response=response, status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService/FirmwareInventory")
def firmware_inventory():

    res = {
        "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory",
        "@odata.type": "#SoftwareInventoryCollection.SoftwareInventoryCollection",
        "Members": [
            {"@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/BMC"},
            {"@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/BIOS"},
        ],
        "Members@odata.count": 2,
    }
    response = json.dumps(res, indent=4, separators=(",", ": "))
    return Response(response=response, status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService/FirmwareInventory/BMC")
def firmware_inventory_bmc():

    res = {
        "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/BMC",
        "@odata.type": "#SoftwareInventory.v1_2_3.SoftwareInventory",
        "Id": "BMC",
        "LowestSupportedVersion": "1.30.367a12-rev1",
        "Manufacturer": "Contoso",
        "Name": "Contoso BMC Firmware",
        "RelatedItem": [{"@odata.id": "/redfish/v1/Managers/BMC"}],
        "ReleaseDate": "2017-08-22T12:00:00",
        "SoftwareId": "1624A9DF-5E13-47FC-874A-DF3AFF143089",
        "UefiDevicePaths": ["BMC(0x1,0x0ABCDEF)"],
        "Updateable": True,
        "Version": "1.45.455b66-rev4",
    }
    response = json.dumps(res, indent=4, separators=(",", ": "))
    return Response(response=response, status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService/FirmwareInventory/BIOS")
def firmware_inventory_bios():

    res = {
        "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/BIOS",
        "@odata.type": "#SoftwareInventory.v1_2_3.SoftwareInventory",
        "Id": "BIOS",
        "LowestSupportedVersion": "P79 v1.10",
        "Manufacturer": "Contoso",
        "Name": "Contoso BIOS Firmware",
        "RelatedItem": [{"@odata.id": "/redfish/v1/Systems/437XR1138R2"}],
        "ReleaseDate": "2017-12-06T12:00:00",
        "SoftwareId": "FEE82A67-6CE2-4625-9F44-237AD2402C28",
        "Updateable": True,
        "Version": "P79 v1.45",
    }
    response = json.dumps(res, indent=4, separators=(",", ": "))
    return Response(response=response, status=200, mimetype="application/json")


@app.route("/redfish/v1/TaskService/Tasks/545")
def task_status():

    res = {
        "@odata.id": "/redfish/v1/TaskService/Tasks/545",
        "@odata.type": "#Task.v1_4_3.Task",
        "Id": "545",
        "Messages": [
            {
                "Message": "The property SKU is a read only property and cannot be assigned a value",
                "Severity": "Warning",
            }
        ],
        "Name": "Task 545",
        "TaskState": "Completed",
        "TaskStatus": "OK",
    }
    if app._percentage < 100:
        res["TaskState"] = "In Progress"
        app._percentage += 25
    response = json.dumps(res, indent=4, separators=(",", ": "))
    return Response(response=response, status=200, mimetype="application/json")


@app.route("/FWUpdate", methods=["POST"])
def fwupdate():

    # reset counter
    app._percentage = 0

    data = json.loads(request.form["UpdateParameters"])
    if data["@Redfish.OperationApplyTime"] != "Immediate":
        return _failure("apply invalid")
    if data["Targets"][0] != "/redfish/v1/UpdateService/FirmwareInventory/BMC":
        return _failure("id invalid")
    fileitem = request.files["UpdateFile"]
    if fileitem.filename != "BMC Firmware.bin":
        return _failure("filename invalid")
    if fileitem.read().decode() != "hello":
        return _failure("data invalid")
    res = {
        "Version": "P79 v1.45",
    }
    # Location set to the URI of a task monitor.
    response = json.dumps(res, indent=4, separators=(",", ": "))
    return Response(
        response=response,
        status=202,
        mimetype="application/json",
        headers={"Location": "http://localhost:4661/redfish/v1/TaskService/Tasks/545"},
    )


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=4661)
