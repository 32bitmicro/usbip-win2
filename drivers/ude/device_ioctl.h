/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv/wdf_cpp.h>

#include <usb.h>
#include <wdfusb.h>
#include <UdeCx.h>

namespace usbip::device
{

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void send_cmd_unlink(_In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS set_configuration(
        _In_ UDECXUSBDEVICE device, _In_opt_ WDFREQUEST request, _In_ UCHAR ConfigurationValue);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS set_interface(
        _In_ UDECXUSBDEVICE device, _In_opt_ WDFREQUEST request, 
        _In_ UCHAR InterfaceNumber, _In_ UCHAR AlternateSetting);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS clear_endpoint_stall(_In_ UDECXUSBENDPOINT endpoint, _In_opt_ WDFREQUEST request);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS reset_port(_In_ UDECXUSBDEVICE device, _In_opt_ WDFREQUEST request);

_Function_class_(EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI internal_control(
        _In_ WDFQUEUE queue,
        _In_ WDFREQUEST request,
        _In_ size_t OutputBufferLength,
        _In_ size_t InputBufferLength,
        _In_ ULONG IoControlCode);

} // namespace usbip::device