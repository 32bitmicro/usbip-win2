#pragma once

#include "pageable.h"
#include "dev.h"

extern const ULONG WskEvents[2];

PAGEABLE void destroy_device(vdev_t *vdev);
PAGEABLE NTSTATUS pnp_remove_device(vdev_t *vdev, IRP *irp);


