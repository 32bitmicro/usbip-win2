/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "request.h"

/*
 * UDECXUSBDEVICE never receives USB_REQUEST_SET_CONFIGURATION and USB_REQUEST_SET_INTERFACE 
 * in URB_FUNCTION_CONTROL_TRANSFER because UDE handles them itself. Therefore these requests 
 * are passed as fake/invalid USB_REQUEST_SET_FIRMWARE_STATUS.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::filter::pack_request_select(
        _Out_ _URB_CONTROL_TRANSFER_EX &r, _In_ void *TransferBuffer, _In_ bool cfg_or_intf)
{
        r.Hdr.Length = sizeof(r);
        r.Hdr.Function = URB_FUNCTION_CONTROL_TRANSFER_EX;

        r.TransferBuffer = TransferBuffer;
        r.TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | USBD_TRANSFER_DIRECTION_OUT;
        r.Timeout = impl::const_part | ULONG(cfg_or_intf);

        get_setup_packet(r) = impl::setup_select;
        NT_ASSERT(is_request_select(r));
}