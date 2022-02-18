/*
* Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
*               2005-2007 Takahiro Hirofuchi
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "start_xfer.h"
#include "usbip_xfer/usbip_xfer.h"
#include "usbip_network.h"

static BOOL write_data(HANDLE hInWrite, const void *data, DWORD len)
{
	DWORD nwritten = 0;
	BOOL res = WriteFile(hInWrite, data, len, &nwritten, NULL);

	if (res && nwritten == len) {
		return TRUE;
	}

	dbg("failed to write handle value, len %d", len);
	return FALSE;
}

static BOOL create_pipe(HANDLE *phRead, HANDLE *phWrite)
{
	SECURITY_ATTRIBUTES saAttr = {
		.nLength = sizeof(saAttr),
		.bInheritHandle = TRUE
	};

	if (CreatePipe(phRead, phWrite, &saAttr, 0)) {
		return TRUE;
	}

	dbg("failed to create stdin pipe: 0x%lx", GetLastError());
	return FALSE;
}

int start_xfer(HANDLE hdev, SOCKET sockfd, bool client)
{
	int ret = ERR_GENERAL;

	HANDLE hRead;
	HANDLE hWrite;
	if (!create_pipe(&hRead, &hWrite)) {
		return ERR_GENERAL;
	}

	char CommandLine[MAX_PATH];
	strcpy_s(CommandLine, sizeof(CommandLine), usbip_xfer_binary());

	STARTUPINFO si = {
		.cb = sizeof(si),
		.hStdInput = hRead,
		.dwFlags = STARTF_USESTDHANDLES
	};

	PROCESS_INFORMATION pi = {0};

	BOOL res = CreateProcess(usbip_xfer_binary(), CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
	if (!res) {
		DWORD err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND) {
			ret = ERR_NOTEXIST;
		}
		dbg("failed to create process: 0x%lx", err);
		goto out;
	}

	struct usbip_xfer_args args = {
		.hdev = INVALID_HANDLE_VALUE,
		.client = client
	};

	res = DuplicateHandle(GetCurrentProcess(), hdev, pi.hProcess, &args.hdev, 0, FALSE, DUPLICATE_SAME_ACCESS);
	if (!res) {
		dbg("failed to dup hdev: 0x%lx", GetLastError());
		goto out_proc;
	}

	res = WSADuplicateSocketW(sockfd, pi.dwProcessId, &args.info);
	if (res) {
		dbg("failed to dup sockfd: %#x", WSAGetLastError());
		goto out_proc;
	}

	if (write_data(hWrite, &args, sizeof(args))) {
		ret = 0;
	}

out_proc:
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
out:
	CloseHandle(hRead);
	CloseHandle(hWrite);

	return ret;
}