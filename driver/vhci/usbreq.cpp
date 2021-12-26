#include "usbreq.h"
#include "trace.h"
#include "usbreq.tmh"

#include "vhci.h"
#include "usbip_proto.h"
#include "vhci_read.h"
#include "vhci_irp.h"

#include <ntstrsafe.h>

const char *urb_req_str(char *buf, size_t len, const struct urb_req *urbr)
{
	if (!urbr) {
		return "urb_req{null}";
	}

	NTSTATUS st = RtlStringCbPrintfA(buf, len, "urb_req{irp %#p, seq_num %lu, seq_num_unlink %lu}", 
							urbr->irp, urbr->seq_num, urbr->seq_num_unlink);

	return st != STATUS_INVALID_PARAMETER ? buf : "dbg_urbr invalid parameter";
}

struct urb_req *find_sent_urbr(vpdo_dev_t *vpdo, unsigned long seqnum)
{
	struct urb_req *result = nullptr;

	KIRQL oldirql;
	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	for (LIST_ENTRY *le = vpdo->head_urbr_sent.Flink; le != &vpdo->head_urbr_sent; le = le->Flink) {
		struct urb_req *urbr = CONTAINING_RECORD(le, struct urb_req, list_state);
		if (urbr->seq_num == seqnum) {
			RemoveEntryListInit(&urbr->list_all);
			RemoveEntryListInit(&urbr->list_state);
			result = urbr;
			break;
		}
	}

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
	return result;
}

struct urb_req *find_pending_urbr(vpdo_dev_t *vpdo)
{
	if (IsListEmpty(&vpdo->head_urbr_pending)) {
		return nullptr;
	}

	struct urb_req *urbr = CONTAINING_RECORD(vpdo->head_urbr_pending.Flink, struct urb_req, list_state);

	urbr->seq_num = ++vpdo->seq_num;
	RemoveEntryListInit(&urbr->list_state);

	return urbr;
}

static void submit_urbr_unlink(vpdo_dev_t *vpdo, unsigned long seq_num_unlink)
{
	struct urb_req *urbr_unlink = create_urbr(vpdo, nullptr, seq_num_unlink);
	if (urbr_unlink) {
		NTSTATUS status = submit_urbr(vpdo, urbr_unlink);
		if (NT_ERROR(status)) {
			char buf[URB_REQ_STR_BUFSZ];
			TraceError(FLAG_GENERAL, "failed to submit unlink urb %s", urb_req_str(buf, sizeof(buf), urbr_unlink));
			free_urbr(urbr_unlink);
		}
	}
}

static void remove_cancelled_urbr(pvpdo_dev_t vpdo, struct urb_req *urbr)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	RemoveEntryListInit(&urbr->list_state);
	RemoveEntryListInit(&urbr->list_all);
	if (vpdo->urbr_sent_partial == urbr) {
		vpdo->urbr_sent_partial = nullptr;
		vpdo->len_sent_partial = 0;
	}

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	submit_urbr_unlink(vpdo, urbr->seq_num);

	char buf[URB_REQ_STR_BUFSZ];
	TraceInfo(FLAG_GENERAL, "cancelled urb destroyed %s", urb_req_str(buf, sizeof(buf), urbr));
	
	free_urbr(urbr);
}

static void cancel_urbr(PDEVICE_OBJECT devobj, PIRP irp)
{
	UNREFERENCED_PARAMETER(devobj);

	pvpdo_dev_t	vpdo;
	struct urb_req	*urbr;

	vpdo = (pvpdo_dev_t)irp->Tail.Overlay.DriverContext[0];
	urbr = (struct urb_req *)irp->Tail.Overlay.DriverContext[1];

	vpdo = (pvpdo_dev_t)devobj->DeviceExtension;
	
	{
		char buf[URB_REQ_STR_BUFSZ];
		TraceInfo(FLAG_GENERAL, "irp will be cancelled %s", urb_req_str(buf, sizeof(buf), urbr));
	}

	IoReleaseCancelSpinLock(irp->CancelIrql);

	remove_cancelled_urbr(vpdo, urbr);

	irp->IoStatus.Information = 0;
	irp_done(irp, STATUS_CANCELLED);
}

struct urb_req *create_urbr(vpdo_dev_t *vpdo, IRP *irp, unsigned long seq_num_unlink)
{
	auto urbr = (urb_req*)ExAllocateFromNPagedLookasideList(&g_lookaside);
	if (!urbr) {
		TraceError(FLAG_GENERAL, "out of memory");
		return nullptr;
	}

	RtlZeroMemory(urbr, sizeof(*urbr));

	urbr->vpdo = vpdo;
	urbr->irp = irp;

	if (irp) {
		irp->Tail.Overlay.DriverContext[0] = vpdo;
		irp->Tail.Overlay.DriverContext[1] = urbr;
	}

	urbr->seq_num_unlink = seq_num_unlink;

	InitializeListHead(&urbr->list_all);
	InitializeListHead(&urbr->list_state);

	return urbr;
}

void free_urbr(struct urb_req *urbr)
{
	NT_ASSERT(IsListEmpty(&urbr->list_all));
	NT_ASSERT(IsListEmpty(&urbr->list_state)); // FAIL

	ExFreeToNPagedLookasideList(&g_lookaside, urbr);
}

bool is_port_urbr(IRP *irp, USBD_PIPE_HANDLE handle)
{
	if (!(irp && handle)) {
		return false;
	}

	auto urb = (URB*)URB_FROM_IRP(irp);
	if (!urb) {
		return false;
	}

	USBD_PIPE_HANDLE hPipe = 0;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_CONTROL_TRANSFER:
		hPipe = urb->UrbControlTransfer.PipeHandle; // nullptr if (TransferFlags & USBD_DEFAULT_PIPE_TRANSFER)
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		hPipe = urb->UrbControlTransferEx.PipeHandle; // nullptr if (TransferFlags & USBD_DEFAULT_PIPE_TRANSFER)
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		hPipe = urb->UrbBulkOrInterruptTransfer.PipeHandle;
		NT_ASSERT(hPipe);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		hPipe = urb->UrbIsochronousTransfer.PipeHandle;
		NT_ASSERT(hPipe);
		break;
	}

	return hPipe == handle;
}

NTSTATUS submit_urbr(vpdo_dev_t *vpdo, struct urb_req *urbr)
{
	KIRQL	oldirql;
	KIRQL	oldirql_cancel;
	PIRP	read_irp;
	NTSTATUS status = STATUS_PENDING;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (vpdo->urbr_sent_partial || !vpdo->pending_read_irp) {
		
		if (urbr->irp) {
			IoAcquireCancelSpinLock(&oldirql_cancel);
			IoSetCancelRoutine(urbr->irp, cancel_urbr);
			IoReleaseCancelSpinLock(oldirql_cancel);

			IoMarkIrpPending(urbr->irp);
		}

		InsertTailList(&vpdo->head_urbr_pending, &urbr->list_state);
		InsertTailList(&vpdo->head_urbr, &urbr->list_all);
		
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		TraceVerbose(FLAG_GENERAL, "STATUS_PENDING");
		return STATUS_PENDING;
	}

	IoAcquireCancelSpinLock(&oldirql_cancel);
	bool valid_irp = IoSetCancelRoutine(vpdo->pending_read_irp, nullptr);
	IoReleaseCancelSpinLock(oldirql_cancel);

	if (!valid_irp) {
		TraceVerbose(FLAG_GENERAL, "Read irp was cancelled");
		status = STATUS_INVALID_PARAMETER;
		vpdo->pending_read_irp = nullptr;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
		return status;
	}

	read_irp = vpdo->pending_read_irp;
	vpdo->urbr_sent_partial = urbr;

	urbr->seq_num = ++vpdo->seq_num;

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	status = store_urbr(read_irp, urbr);

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (status == STATUS_SUCCESS) {
		if (urbr->irp) {
			IoAcquireCancelSpinLock(&oldirql_cancel);
			IoSetCancelRoutine(urbr->irp, cancel_urbr);
			IoReleaseCancelSpinLock(oldirql_cancel);
			IoMarkIrpPending(urbr->irp);
		}

		if (!vpdo->len_sent_partial) {
			vpdo->urbr_sent_partial = nullptr;
			InsertTailList(&vpdo->head_urbr_sent, &urbr->list_state);
		}

		InsertTailList(&vpdo->head_urbr, &urbr->list_all);

		vpdo->pending_read_irp = nullptr;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		irp_done_success(read_irp);
		status = STATUS_PENDING;
	} else {
		vpdo->urbr_sent_partial = nullptr;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		status = STATUS_INVALID_PARAMETER;
	}

	TraceVerbose(FLAG_GENERAL, "%!STATUS!", status);
	return status;
}
