/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <windows.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <regstr.h>
#include <combaseapi.h>

#include <libusbip\hkey.h>
#include <libusbip\hdevinfo.h>
#include <libusbip\format_message.h>

#include <libusbip\src\strconv.h>
#include <libusbip\src\file_ver.h>

#include <CLI11\CLI11.hpp>

#include <initguid.h>
#include <devpkey.h>

/*
 * See: devcon utility
 * https://github.com/microsoft/Windows-driver-samples/tree/master/setup/devcon
 */

namespace
{

using namespace usbip;

struct devnode_install_args
{
        std::wstring infpath;
        std::wstring hwid;
};

struct devnode_remove_args
{
        std::wstring hwid;
        std::wstring class_guid;
        std::wstring enumerator;
        bool dry_run;
};

struct classfilter_args
{
        std::string_view level;
        std::wstring class_guid;
        std::wstring driver_name;
};

auto &opt_upper = "upper";

using command_f = std::function<bool()>;

auto pack(command_f cmd) 
{
        return [cmd = std::move(cmd)] 
        {
                if (!cmd()) {
                        exit(EXIT_FAILURE); // throw CLI::RuntimeError(EXIT_FAILURE);
                }
        };
}

void errmsg(_In_ LPCSTR api, _In_ LPCWSTR str = L"", _In_ DWORD err = GetLastError())
{
        auto msg = wformat_message(err);
        fwprintf(stderr, L"%S(%s) error %#lx %s\n", api, str, err, msg.c_str());
}

auto get_version(_In_ const wchar_t *program)
{
        win::FileVersion fv(program);
        auto ver = fv.GetFileVersion();
        return wchar_to_utf8(ver); // CLI::narrow(ver)
}

auto split_multi_sz(_In_opt_ PCWSTR str, _In_ std::wstring_view exclude, _Inout_ bool &excluded)
{
        std::vector<std::wstring> v;

        while (str && *str) {
                std::wstring_view s(str);
                if (s == exclude) {
                        excluded = true;
                } else {
                        v.emplace_back(s);
                }
                str += s.size() + 1; // skip L'\0'
        }

        return v;
}

/*
 * @return REG_MULTI_SZ 
 */
auto make_hwid(_In_ std::wstring hwid)
{
        hwid += L'\0'; // first string
        hwid += L'\0'; // end of the list
        return hwid;
}

auto read_multi_z(_In_ HKEY key, _In_ LPCWSTR val_name, _Out_ std::vector<WCHAR> &val)
{
        for (auto val_sz = DWORD(val.size()); ; ) {
                switch (auto err = RegGetValue(key, nullptr, val_name, RRF_RT_REG_MULTI_SZ, nullptr, 
                                               reinterpret_cast<BYTE*>(val.data()), &val_sz)) {
                case ERROR_FILE_NOT_FOUND: // val_name
                        val.clear();
                        [[fallthrough]];
                case ERROR_SUCCESS:
                        return true;
                case ERROR_MORE_DATA:
                        val.resize(val_sz);
                        break;
                default:
                        errmsg("RegGetValue", val_name, err);
                        return false;
                }
        }
}

void prompt_reboot()
{
        switch (auto ret = SetupPromptReboot(nullptr, nullptr, false)) {
        case SPFILEQ_REBOOT_IN_PROGRESS:
                wprintf(L"Rebooting...\n");
                break;
        case SPFILEQ_REBOOT_RECOMMENDED:
                wprintf(L"Reboot is recommended\n");
                break;
        default:
                assert(ret == -1);
                errmsg("SetupPromptReboot");
        }
}

using device_visitor_f = std::function<bool(HDEVINFO di, SP_DEVINFO_DATA &dd)>;

DWORD enum_device_info(_In_ HDEVINFO di, _In_ const device_visitor_f &func)
{
        SP_DEVINFO_DATA	dd{ .cbSize = sizeof(dd) };

        for (DWORD i = 0; ; ++i) {
                if (SetupDiEnumDeviceInfo(di, i, &dd)) {
                        if (func(di, dd)) {
                                return ERROR_SUCCESS;
                        }
                } else if (auto err = GetLastError(); err == ERROR_NO_MORE_ITEMS) {
                        return ERROR_SUCCESS;
                } else {
                        errmsg("SetupDiEnumDeviceInfo", L"", err);
                        return err;
                }
        }
}

auto get_device_property(
        _In_ HDEVINFO di, _In_ SP_DEVINFO_DATA &dd, 
        _In_ const DEVPROPKEY &key, _In_ DWORD maxsize_hint,
        _Out_ DEVPROPTYPE &type)
{
        for (std::vector<BYTE> v(maxsize_hint); ; ) {
                if (DWORD actual{}; // bytes
                    SetupDiGetDeviceProperty(di, &dd, &key, &type, v.data(), DWORD(v.size()), &actual, 0)) {
                        v.resize(actual);
                        return v;
                } else if (auto err = GetLastError(); err == ERROR_INSUFFICIENT_BUFFER) {
                        v.resize(actual);
                } else {
                        errmsg("SetupDiGetDeviceProperty", L"", err);
                        v.clear();
                        return v;
                }
        }
}

inline auto as_wstring_view(_In_ std::vector<BYTE> &v) noexcept
{
        assert(!(v.size() % sizeof(wchar_t)));
        return std::wstring_view(reinterpret_cast<wchar_t*>(v.data()), v.size()/sizeof(wchar_t));
}

/*
 * @param infpath must be an absolute path
 * @see devcon, cmd/cmd_remove
 * @see devcon hwids ROOT\USBIP_WIN2\*
 */
auto install_devnode_and_driver(_In_ devnode_install_args &r)
{
        GUID ClassGUID;
        WCHAR ClassName[MAX_CLASS_NAME_LEN];
        if (!SetupDiGetINFClass(r.infpath.c_str(), &ClassGUID, ClassName, ARRAYSIZE(ClassName), 0)) {
                errmsg("SetupDiGetINFClass", r.infpath.c_str());
                return false;
        }

        hdevinfo dev_list(SetupDiCreateDeviceInfoList(&ClassGUID, nullptr));
        if (!dev_list) {
                errmsg("SetupDiCreateDeviceInfoList");
                return false;
        }

        SP_DEVINFO_DATA dev_data{ .cbSize = sizeof(dev_data) };
        if (!SetupDiCreateDeviceInfo(dev_list.get(), ClassName, &ClassGUID, nullptr, 0, DICD_GENERATE_ID, &dev_data)) {
                errmsg("SetupDiCreateDeviceInfo");
                return false;
        }

        auto id = make_hwid(r.hwid);
        auto id_sz = DWORD(id.length()*sizeof(id[0]));

        if (!SetupDiSetDeviceRegistryProperty(dev_list.get(), &dev_data, SPDRP_HARDWAREID, 
                                              reinterpret_cast<const BYTE*>(id.data()), id_sz)) {
                errmsg("SetupDiSetDeviceRegistryProperty");
                return false;
        }

        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, dev_list.get(), &dev_data)) {
                errmsg("SetupDiCallClassInstaller");
                return false;
        }

        SP_DEVINSTALL_PARAMS params{ .cbSize = sizeof(params) };
        if (!SetupDiGetDeviceInstallParams(dev_list.get(), &dev_data, &params)) {
                errmsg("SetupDiGetDeviceInstallParams");
                return false;
        }
        bool reboot_required = params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART);

        // the same as "pnputil /add-driver usbip2_vhci.inf /install"

        BOOL RebootRequired{};
        if (!UpdateDriverForPlugAndPlayDevices(nullptr, r.hwid.c_str(), r.infpath.c_str(), INSTALLFLAG_FORCE, &RebootRequired)) {
                errmsg("UpdateDriverForPlugAndPlayDevices");
                return false;
        }

        if (reboot_required || RebootRequired) {
                prompt_reboot();
        }

        return true;
}

auto uninstall_device(
        _In_ HDEVINFO di, _In_  SP_DEVINFO_DATA &dd, _In_ const devnode_remove_args &r, _Inout_ bool &reboot)
{
        DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
        auto prop = get_device_property(di, dd, DEVPKEY_Device_HardwareIds, REGSTR_VAL_MAX_HCID_LEN, type);
        if (prop.empty()) {
                return false;
        }
        assert(type == DEVPROP_TYPE_STRING_LIST);
        
        if (as_wstring_view(prop) != r.hwid) {
                //
        } else if (r.dry_run) {
                prop = get_device_property(di, dd, DEVPKEY_Device_InstanceId, MAX_DEVICE_ID_LEN, type); 
                if (!prop.empty()) {
                        assert(type == DEVPROP_TYPE_STRING);
                        auto id = as_wstring_view(prop);
                        wprintf(L"%s\n", id.data());
                }
        } else if (BOOL NeedReboot{}; !DiUninstallDevice(nullptr, di, &dd, 0, &NeedReboot)) {
                errmsg("DiUninstallDevice");
        } else if (NeedReboot) {
                reboot = true;
        }

        return false;
};

/*
 * pnputil /remove-device /deviceid <HWID>
 * a) /remove-device is available since Windows 10 version 2004
 * b) /deviceid flag is available since Windows 11 version 21H2
 *
 * @see devcon, cmd/cmd_remove
 * @see devcon hwids ROOT\USBIP_WIN2\*
 */
auto remove_devnode(_In_ devnode_remove_args &r)
{
        auto clsid = r.class_guid.c_str();

        CLSID ClassGUID{};
        if (auto ret = CLSIDFromString(clsid, &ClassGUID); FAILED(ret)) {
                errmsg("CLSIDFromString", clsid, ret);
                return false;
        }

        auto enumerator = r.enumerator.empty() ? nullptr : r.enumerator.c_str();

        hdevinfo di(SetupDiGetClassDevs(&ClassGUID, enumerator, nullptr, DIGCF_PRESENT));
        if (!di) {
                errmsg("SetupDiGetClassDevs", clsid);
                return false;
        }

        r.hwid = make_hwid(std::move(r.hwid)); // DEVPKEY_Device_HardwareIds is DEVPROP_TYPE_STRING_LIST

        bool reboot{};
        auto f = [&r, &reboot] (auto di, auto &dd) { return uninstall_device(di, dd, r, reboot); };

        enum_device_info(di.get(), f);
        if (reboot) {
                prompt_reboot();
        }

        return true;
}

/*
 * devcon classfilter usb upper ; query
 * devcon classfilter usb upper !usbip2_filter ; remove
 * @see devcon, cmdClassFilter
 */
auto classfilter(_In_ classfilter_args &r, _In_ bool add)
{
        auto clsid = r.class_guid.c_str();

        CLSID ClassGUID;
        if (auto ret = CLSIDFromString(clsid, &ClassGUID); FAILED(ret)) {
                errmsg("CLSIDFromString", clsid, ret);
                return false;
        }

        HKey key(SetupDiOpenClassRegKeyEx(&ClassGUID, KEY_QUERY_VALUE | KEY_SET_VALUE, DIOCR_INSTALLER, nullptr, nullptr));
        if (!key) {
                errmsg("SetupDiOpenClassRegKeyEx", clsid);
                return false;
        }

        auto val_name = r.level == opt_upper ? REGSTR_VAL_UPPERFILTERS : REGSTR_VAL_LOWERFILTERS;
        std::vector<WCHAR> val(4096);
        if (!read_multi_z(key.get(), val_name, val)) {
                return false;
        }

        auto modified = add;
        auto filters = split_multi_sz(val.empty() ? nullptr : val.data(), r.driver_name, modified);
        if (add) {
                filters.emplace_back(r.driver_name);
        }

        if (!modified) {
                return true;
        }

        if (auto str = make_multi_sz(filters); 
            auto err = RegSetValueEx(key.get(), val_name, 0, REG_MULTI_SZ,
                                     reinterpret_cast<const BYTE*>(str.data()), 
                                     DWORD(str.length()*sizeof(str[0])))) {
                errmsg("RegSetValueEx", val_name, err);
                return false;
        }

        return true;
}

void add_devnode_install_cmd(_In_ CLI::App &app)
{
        static devnode_install_args r;
        auto cmd = app.add_subcommand("install", "Install a device node and its driver");

        cmd->add_option("infpath", r.infpath, "Path to driver .inf file")
                ->check(CLI::ExistingFile)
                ->required();

        cmd->add_option("hwid", r.hwid, "Hardware Id of the device")->required();

        auto f = [&r = r] { return install_devnode_and_driver(r); };
        cmd->callback(pack(std::move(f)));
}

void add_devnode_remove_cmd(_In_ CLI::App &app)
{
        static devnode_remove_args r;
        auto cmd = app.add_subcommand("remove", "Uninstall a device and remove its device nodes");

        cmd->add_option("hwid", r.hwid, "Hardware Id of the device")->required();
        cmd->add_option("ClassGuid", r.class_guid, "A GUID for a device setup class")->required();
        cmd->add_option("enumerator", r.enumerator, "An identifier of a Plug and Play enumerator");

        cmd->add_flag("-n,--dry-run", r.dry_run, 
                      "Print InstanceId of devices that will be removed instead of removing them");

        auto f = [&r = r] { return remove_devnode(r); };
        cmd->callback(pack(std::move(f)));
}

void add_devnode_cmds(_In_ CLI::App &app)
{
        add_devnode_install_cmd(app);
        add_devnode_remove_cmd(app);
}

void add_classfilter_cmds(_In_ CLI::App &app)
{
        static classfilter_args r;
        auto &cmd_add = "add";

        for (auto action: {cmd_add, "remove"}) {

                auto cmd = app.add_subcommand(action, std::string(action) + " class filter driver");

                cmd->add_option("Level", r.level)
                        ->check(CLI::IsMember({opt_upper, "lower"}))
                        ->required();

                cmd->add_option("ClassGuid", r.class_guid, "A GUID for a device setup class")->required();
                cmd->add_option("DriverName", r.driver_name, "Filter driver name")->required();

                auto f = [&r = r, add = action == cmd_add] { return classfilter(r, add); };
                cmd->callback(pack(std::move(f)));
        }
}

} // namespace


int wmain(_In_ int argc, _Inout_ wchar_t* argv[])
{
        CLI::App app("usbip2 drivers installation utility");
        app.set_version_flag("-V,--version", get_version(*argv));

        auto &devnode = L"devnode";
        auto &classfilter = L"classfilter";

        if (auto program = std::filesystem::path(*argv).stem(); program == devnode) {
                add_devnode_cmds(app);
        } else if (program == classfilter) {
                add_classfilter_cmds(app);
        } else {
                fwprintf(stderr, L"Program name must be '%s' or '%s', not '%s'\n", 
                                   devnode, classfilter, program.c_str());

                return EXIT_FAILURE;
        }

        app.require_subcommand(1);
        CLI11_PARSE(app, argc, argv);
}
