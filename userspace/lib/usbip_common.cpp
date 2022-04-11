/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include <windows.h>

#include "usbip_common.h"
#include "names.h"
#include "usbip_util.h"

#include "usbip_proto.h"

int usbip_use_stderr;
int usbip_use_debug;
const char* usbip_progname;

struct speed_string 
{
	usb_device_speed val;
	char *speed;
	char *desc;
};

static const speed_string speed_strings[] = 
{
	{ USB_SPEED_UNKNOWN, "unknown", "Unknown Speed"},
	{ USB_SPEED_LOW,  "1.5", "Low Speed(1.5Mbps)"  },
	{ USB_SPEED_FULL, "12",  "Full Speed(12Mbps)" },
	{ USB_SPEED_HIGH, "480", "High Speed(480Mbps)" },
	{ USB_SPEED_WIRELESS, "53.3-480", "Wireless" },
	{ USB_SPEED_SUPER, "5000", "Super Speed(5000Mbps)" },
	{ USB_SPEED_SUPER_PLUS, "10000", "Super Speed Plus(10 Gbit/s)" },
	{}
};

struct portst_string 
{
        usbip_device_status status;
	char *desc;
};

static portst_string portst_strings[] = 
{
	{ SDEV_ST_AVAILABLE,	"Device Available" },
	{ SDEV_ST_USED,		"Device in Use" },
	{ SDEV_ST_ERROR,	"Device Error"},
	{ VDEV_ST_NULL,		"Port Available"},
	{ VDEV_ST_NOTASSIGNED,	"Port Initializing"},
	{ VDEV_ST_USED,		"Port in Use"},
	{ VDEV_ST_ERROR,	"Port Error"},
	{}
};

const char *usbip_status_string(usbip_device_status status)
{
        for (auto &i: portst_strings) {
                if (i.status == status) {
                        return i.desc;
                }
        }

	return "Unknown Status";
}

const char *usbip_speed_string(usb_device_speed speed)
{
        for (auto &i : speed_strings) {
                if (i.val == speed) {
                        return i.desc;
                }
        }

	return "Unknown Speed";
}


#define DBG_UDEV_INTEGER(name)\
	dbg("%-20s = %x", #name, (int) udev->name)

#define DBG_UINF_INTEGER(name)\
	dbg("%-20s = %x", #name, (int) uinf->name)

void dump_usb_interface(usbip_usb_interface *uinf)
{
	char buff[100];

	usbip_names_get_class(buff, sizeof(buff),
			uinf->bInterfaceClass,
			uinf->bInterfaceSubClass,
			uinf->bInterfaceProtocol);
	
        dbg("%-20s = %s", "Interface(C/SC/P)", buff);
}

void dump_usb_device(usbip_usb_device *udev)
{
	char buff[100];

	dbg("%-20s = %s", "path",  udev->path);
	dbg("%-20s = %s", "busid", udev->busid);

	usbip_names_get_class(buff, sizeof(buff),
			udev->bDeviceClass,
			udev->bDeviceSubClass,
			udev->bDeviceProtocol);
	
        dbg("%-20s = %s", "Device(C/SC/P)", buff);

	DBG_UDEV_INTEGER(bcdDevice);

	usbip_names_get_product(buff, sizeof(buff),
			udev->idVendor,
			udev->idProduct);
	dbg("%-20s = %s", "Vendor/Product", buff);

	DBG_UDEV_INTEGER(bNumConfigurations);
	DBG_UDEV_INTEGER(bNumInterfaces);

	dbg("%-20s = %s", "speed", usbip_speed_string(static_cast<usb_device_speed>(udev->speed)));

	DBG_UDEV_INTEGER(busnum);
	DBG_UDEV_INTEGER(devnum);
}

int usbip_names_init()
{
	char	*fpath_db, *fpath_mod;
	int	ret;

	fpath_mod = get_module_dir();
	asprintf(&fpath_db, "%s\\usb.ids", fpath_mod);
	free(fpath_mod);

	ret = names_init(fpath_db);
	free(fpath_db);

	return ret;
}

void usbip_names_free()
{
	names_free();
}

void usbip_names_get_product(char *buff, size_t size, uint16_t vendor, uint16_t product)
{
	const char *prod, *vend;

	prod = names_product(vendor, product);
	if (!prod)
		prod = "unknown product";


	vend = names_vendor(vendor);
	if (!vend)
		vend = "unknown vendor";

	snprintf(buff, size, "%s : %s (%04x:%04x)", vend, prod, vendor, product);
}

void usbip_names_get_class(char *buff, size_t size, uint8_t class_, uint8_t subclass, uint8_t protocol)
{
	const char *c, *s, *p;

	if (class_ == 0 && subclass == 0 && protocol == 0) {
		snprintf(buff, size, "(Defined at Interface level) (%02x/%02x/%02x)", class_, subclass, protocol);
		return;
	}

	p = names_protocol(class_, subclass, protocol);
	if (!p)
		p = "unknown protocol";

	s = names_subclass(class_, subclass);
	if (!s)
		s = "unknown subclass";

	c = names_class(class_);
	if (!c)
		c = "unknown class";

	snprintf(buff, size, "%s / %s / %s (%02x/%02x/%02x)", c, s, p, class_, subclass, protocol);
}