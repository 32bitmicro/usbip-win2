﻿/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "frame.h"

#include <wx/log.h>

#include <libusbip/win_handle.h>

#include <thread>
#include <mutex>

namespace usbip
{
	struct device_location;
}

class DeviceStateEvent;
wxDECLARE_EVENT(EVT_DEVICE_STATE, DeviceStateEvent);

class MainFrame : public Frame
{
public:
	MainFrame(_In_ usbip::Handle read);
	~MainFrame();

private:
	enum { COL_BUSID, COL_PORT, COL_SPEED, COL_VID, COL_PID, COL_STATE }; // m_treeListCtrl
	wxLogWindow *m_log = new wxLogWindow(this, _("Log records"), false);

	usbip::Handle m_read;
	std::mutex m_read_close_mtx;

	std::thread m_read_thread{ &MainFrame::read_loop, this };

	void on_close(wxCloseEvent &event) override; 
	void on_exit(wxCommandEvent &event) override;
	void on_list(wxCommandEvent &event) override;
	void on_attach(wxCommandEvent &event) override;
	void on_detach(wxCommandEvent &event) override;
	void on_refresh(wxCommandEvent &event) override;
	void on_log_level(wxCommandEvent &event) override;

	void on_log_show_update_ui(wxUpdateUIEvent &event) override;
	void on_log_show(wxCommandEvent &event) override;
	
	void on_device_state(_In_ DeviceStateEvent &event);
	void set_log_level();

	void read_loop();
	void break_read_loop();

	wxTreeListItem find_server(_In_ const usbip::device_location &loc, _In_ bool append);
};
