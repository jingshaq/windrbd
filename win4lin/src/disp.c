﻿/*
        Copyright(C) 2017-2018, Johannes Thoma <johannes@johannesthoma.com>
        Copyright(C) 2017-2018, LINBIT HA-Solutions GmbH  <office@linbit.com>
	Copyright(C) 2007-2016, ManTechnology Co., LTD.
	Copyright(C) 2007-2016, wdrbd@mantech.co.kr

	Windows DRBD is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2, or (at your option)
	any later version.

	Windows DRBD is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Windows DRBD; see the file COPYING. If not, write to
	the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <wdm.h>
#include <wdmsec.h>
#include <ntstrsafe.h>
#include <ntddk.h>
#include "drbd_windows.h"
#include "windrbd_device.h"
#include "drbd_wingenl.h"	
#include "disp.h"
#include "windrbd_ioctl.h"

#include "drbd_int.h"
#include "drbd_wrappers.h"

	/* TODO: find some headers where this fits. */
void drbd_cleanup(void);
void idr_shutdown(void);

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD mvolUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif

NTSTATUS
DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
	NTSTATUS            		status;
	PDEVICE_OBJECT      		deviceObject;
	PROOT_EXTENSION			RootExtension = NULL;
	UNICODE_STRING      		nameUnicode, linkUnicode;
	ULONG				i;
	static volatile LONG      IsEngineStart = FALSE;
	int ret;

	/* Init windrbd primitives (spinlocks, ...) before doing anything
	 * else .. needed for printk.
	 */
	init_windrbd();

	/* Then, initialize the printk subsystem (ring buffer). Logging
	 * can be seen only later when booting is finished (depending on
	 * OS), on most OSes this is when the first printk on behalf of
	 * a drbdadm command happens.
	 */
	initialize_syslog_printk();

	printk(KERN_INFO "Windrbd Driver Loading (compiled " __DATE__ " " __TIME__ ") ...\n");

	initRegistry(RegistryPath);

	RtlInitUnicodeString(&nameUnicode, L"\\Device\\" WINDRBD_ROOT_DEVICE_NAME);
	status = IoCreateDeviceSecure(DriverObject, sizeof(ROOT_EXTENSION),
			 &nameUnicode, FILE_DEVICE_UNKNOWN,
			FILE_DEVICE_SECURE_OPEN, FALSE,
			&SDDL_DEVOBJ_SYS_ALL_ADM_ALL, NULL, &deviceObject);
	if (!NT_SUCCESS(status))
	{
		WDRBD_ERROR("Can't create root, err=%x\n", status);
		return status;
	}

	RtlInitUnicodeString(&linkUnicode, L"\\DosDevices\\" WINDRBD_ROOT_DEVICE_NAME);
	status = IoCreateSymbolicLink(&linkUnicode, &nameUnicode);
	if (!NT_SUCCESS(status))
	{
		WDRBD_ERROR("cannot create symbolic link, err=%x\n", status);
		IoDeleteDevice(deviceObject);
		return status;
	}

	mvolDriverObject = DriverObject;
	mvolRootDeviceObject = deviceObject;

	windrbd_set_major_functions(DriverObject);
	DriverObject->DriverUnload = mvolUnload;

	RootExtension = deviceObject->DeviceExtension;

	downup_rwlock_init(&transport_classes_lock); //init spinlock for transport 
	mutex_init(&notification_mutex);
		/* TODO: this is unneccessary */
	KeInitializeSpinLock(&transport_classes_lock);

	dtt_initialize();

/* TODO: if be place an if 0 from here until end of this
 * function, sc stop windrbd does not blue screen. Somewhere
 * there are resources we are not freeing */

#if 0
	system_wq = alloc_ordered_workqueue("system workqueue", 0);
	if (system_wq == NULL) {
		pr_err("Could not allocate system work queue\n");
		return STATUS_NO_MEMORY;
	}
#endif

		/* if we enable this BSOD on unload */
	ret = drbd_init();
	if (ret != 0) {
		printk(KERN_ERR "cannot init drbd, error is %d", ret);
		IoDeleteDevice(deviceObject);

		return STATUS_TIMEOUT;
	}

	printk(KERN_INFO "Windrbd Driver loaded.\n");

#if 0
	if (FALSE == InterlockedCompareExchange(&IsEngineStart, TRUE, FALSE))
	{
		HANDLE		hNetLinkThread = NULL;
		NTSTATUS	Status = STATUS_UNSUCCESSFUL;

        // Init WSK and StartNetLinkServer
		Status = PsCreateSystemThread(&hNetLinkThread, THREAD_ALL_ACCESS, NULL, NULL, NULL, init_wsk_and_netlink, NULL);
		if (!NT_SUCCESS(Status))
		{
			WDRBD_ERROR("PsCreateSystemThread failed with status 0x%08X\n", Status);
			return Status;
		}

		Status = ObReferenceObjectByHandle(hNetLinkThread, THREAD_ALL_ACCESS, NULL, KernelMode, &g_NetlinkServerThread, NULL);
		ZwClose(hNetLinkThread);

		if (!NT_SUCCESS(Status))
		{
			WDRBD_ERROR("ObReferenceObjectByHandle() failed with status 0x%08X\n", Status);
			return Status;
		}
    }

	windrbd_init_usermode_helper();
#endif

    return STATUS_SUCCESS;
}

void mvolUnload(IN PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	UNICODE_STRING linkUnicode;
	NTSTATUS status;

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "Unloading windrbd driver.\n");

	drbd_cleanup();

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "DRBD cleaned up.\n");

		/* TODO: a lot. clean up everything. Right now,
		 * Windows BSODs (rightfully, I think) after
		 * doing this. There are some threads running,
		 * which may cause the BSOD.
		 */

	RtlInitUnicodeString(&linkUnicode, L"\\DosDevices\\" WINDRBD_ROOT_DEVICE_NAME);
	status = IoDeleteSymbolicLink(&linkUnicode);
	if (!NT_SUCCESS(status))
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "Cannot delete root device link, status is %x.\n", status);

        IoDeleteDevice(mvolRootDeviceObject);

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "Root device deleted.\n");

	idr_shutdown();
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "IDR layer shut down.\n");
}

