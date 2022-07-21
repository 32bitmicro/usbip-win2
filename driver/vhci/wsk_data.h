﻿/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <ntdef.h>

struct vpdo_dev_t;
struct _WSK_DATA_INDICATION;

size_t wsk_data_size(_In_ const vpdo_dev_t &vpdo);

void wsk_data_append(_Inout_ vpdo_dev_t &vpdo, _In_ _WSK_DATA_INDICATION *DataIndication, _In_ size_t BytesIndicated);
size_t wsk_data_release(_Inout_ vpdo_dev_t &vpdo, _In_ size_t length);

NTSTATUS wsk_data_copy(_In_ const vpdo_dev_t &vpdo, _Out_ void *dest, _In_ size_t offset, _In_ size_t length);
