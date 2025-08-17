#include "PdfGuard.h"

//
// Global data structure
//
PDFG_DATA Globals;

//
// Assign text sections for each routine.
//
#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, PdfGuardUnload)
#pragma alloc_text(PAGE, PdfGuardInstanceSetup)
#endif

//
// Callback registration structure
//
const FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE,
      0,
      PdfGuardPreCreate,
      PdfGuardPostCreate },

    { IRP_MJ_DIRECTORY_CONTROL,
      0,
      PdfGuardPreDirectoryControl,
      PdfGuardPostDirectoryControl },

    { IRP_MJ_OPERATION_END }
};

//
// Context registration structure
//
const FLT_CONTEXT_REGISTRATION ContextRegistration[] = {
    { FLT_STREAM_CONTEXT,
      0,
      PdfGuardContextCleanup,
      sizeof(PDFG_STREAM_CONTEXT),
      PDFG_POOL_TAG },

    { FLT_CONTEXT_END }
};

//
// Filter registration structure
//
const FLT_REGISTRATION FilterRegistration = {
    sizeof(FLT_REGISTRATION),           //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags
    ContextRegistration,                //  Context Registration
    Callbacks,                          //  Operation Callbacks
    PdfGuardUnload,                     //  FilterUnload
    PdfGuardInstanceSetup,              //  InstanceSetup
    NULL,                               //  InstanceQueryTeardown
    NULL,                               //  InstanceTeardownStart
    NULL,                               //  InstanceTeardownComplete
};


//
// Main driver entry point
//
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);

    //
    // Register with the filter manager.
    //
    status = FltRegisterFilter(
        DriverObject,
        &FilterRegistration,
        &Globals.FilterHandle
    );

    if (NT_SUCCESS(status)) {
        //
        // Start filtering I/O.
        //
        status = FltStartFiltering(Globals.FilterHandle);

        if (!NT_SUCCESS(status)) {
            FltUnregisterFilter(Globals.FilterHandle);
        }
    }

    return status;
}

//
// Driver unload routine
//
NTSTATUS
PdfGuardUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(Flags);
    PAGED_CODE();

    FltUnregisterFilter(Globals.FilterHandle);
    return STATUS_SUCCESS;
}

//
// Instance setup: Attach only to USB removable drives.
//
NTSTATUS
PdfGuardInstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_OBJECT devObject = NULL;
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeDeviceType);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

    try {
        // We only want to attach to USB storage devices.
        status = FltGetDiskDeviceObject(FltObjects->Volume, &devObject);
        if (!NT_SUCCESS(status)) {
            // This can happen for network volumes, just ignore them.
            status = STATUS_FLT_DO_NOT_ATTACH;
            leave;
        }

        if (!(devObject->Characteristics & FILE_REMOVABLE_MEDIA)) {
            status = STATUS_FLT_DO_NOT_ATTACH;
            leave;
        }

        // A more robust check for USB devices
        // Note: This requires linking against Wdmsec.lib and including Wdm.h
        // For simplicity, we stick to FILE_REMOVABLE_MEDIA, but a production
        // driver would do this:
        /*
        ULONG busType;
        status = FltGetDeviceProperty(devObject, DevicePropertyBusType, sizeof(busType), &busType, &retLen);
        if (NT_SUCCESS(status) && busType != BusTypeUsb) {
             status = STATUS_FLT_DO_NOT_ATTACH;
        }
        */

    }
    finally {
        if (devObject) {
            ObDereferenceObject(devObject);
        }
    }

    return status;
}

//
// Pre-Create: Main gatekeeper for file access.
//
FLT_PREOP_CALLBACK_STATUS
PdfGuardPreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
    BOOLEAN isDirectory = FALSE;
    UNREFERENCED_PARAMETER(FltObjects);

    // Set CompletionContext to NULL for all paths.
    *CompletionContext = NULL;

    // We only care about file system requests.
    if (FLT_IS_FS_FILTER_OPERATION(Data)) {
        // If the file is a PDF, allow it and ask for a post-create callback
        // to set our stream context.
        if (IsTargetFilePdf(Data, &isDirectory)) {
            // It's a PDF or a directory, allow it.
            // For PDFs, we want to set a context in post-create.
            if (!isDirectory) {
                *CompletionContext = (PVOID)1; // Signal to post-create
            }
            return FLT_PREOP_SUCCESS_WITH_CALLBACK;
        }
        else {
            // Not a PDF, and not a directory. Block it.
            Data->IoStatus.Status = STATUS_ACCESS_DENIED;
            Data->IoStatus.Information = 0;
            return FLT_PREOP_COMPLETE;
        }
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

//
// Post-Create: Set the stream context for allowed PDF files.
//
FLT_POSTOP_CALLBACK_STATUS
PdfGuardPostCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
)
{
    PPDFG_STREAM_CONTEXT streamContext = NULL;
    NTSTATUS status;
    UNREFERENCED_PARAMETER(Flags);

    // We only act if pre-create signaled us and the create was successful.
    if (CompletionContext != (PVOID)1 || !NT_SUCCESS(Data->IoStatus.Status)) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    // Allocate and set the stream context.
    status = FltAllocateContext(
        Globals.FilterHandle,
        FLT_STREAM_CONTEXT,
        sizeof(PDFG_STREAM_CONTEXT),
        PagedPool,
        &streamContext
    );

    if (NT_SUCCESS(status)) {
        streamContext->IsPdfFile = TRUE;

        (void)FltSetStreamContext(
            FltObjects->Instance,
            FltObjects->FileObject,
            FLT_SET_CONTEXT_KEEP_IF_EXISTS,
            streamContext,
            NULL
        );

        FltReleaseContext(streamContext);
    }

    return FLT_POSTOP_FINISHED_PROCESSING;
}

//
// Pre-DirectoryControl: Request post-op callback to see results.
//
FLT_PREOP_CALLBACK_STATUS
PdfGuardPreDirectoryControl(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    // We need to see the results of the directory query.
    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

//
// Post-DirectoryControl: Filter non-PDF files from the listing.
//
FLT_POSTOP_CALLBACK_STATUS
PdfGuardPostDirectoryControl(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);

    // We only care about successful queries.
    if (!NT_SUCCESS(Data->IoStatus.Status) || Data->IoStatus.Status == STATUS_NO_MORE_FILES) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    // Only filter for IRP_MN_QUERY_DIRECTORY
    if (Data->Iopb->MinorFunction != IRP_MN_QUERY_DIRECTORY) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    // This logic is complex and depends on the specific FILE_INFORMATION_CLASS.
    // Here we provide a simplified example for FILE_NAMES_INFORMATION.
    // A production driver MUST handle all relevant classes (FILE_BOTH_DIR_INFORMATION, etc.)
    if (Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileInformationClass == FileNamesInformation) {
        PFILE_NAMES_INFORMATION currentEntry = (PFILE_NAMES_INFORMATION)Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer;
        PFILE_NAMES_INFORMATION prevEntry = NULL;
        UNICODE_STRING fileName;

        while (TRUE) {
            fileName.Buffer = currentEntry->FileName;
            fileName.Length = (USHORT)currentEntry->FileNameLength;
            fileName.MaximumLength = (USHORT)currentEntry->FileNameLength;

            // Check if the file name ends with .pdf (case-insensitive)
            // Note: A robust implementation would handle files like ".pdf" correctly.
            if (fileName.Length > 8 &&
                RtlCompareUnicodeString(&fileName, L".pdf", TRUE) == 0) {
                // It's a PDF, keep it. Move to the next entry.
                prevEntry = currentEntry;
            }
            else {
                // Not a PDF. Remove it from the buffer.
                if (currentEntry->NextEntryOffset == 0) {
                    // This is the last entry.
                    if (prevEntry) {
                        prevEntry->NextEntryOffset = 0;
                    }
                    else {
                        // This was the *only* entry, so the buffer is now empty.
                        Data->IoStatus.Status = STATUS_NO_MORE_FILES;
                    }
                    break;
                }

                // Not the last entry. Shift subsequent entries up.
                ULONG offset = currentEntry->NextEntryOffset;
                PVOID nextEntry = (PBYTE)currentEntry + offset;
                ULONG bytesToMove = (ULONG)((PBYTE)Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer + Data->IoStatus.Information) - (ULONG)nextEntry;

                RtlMoveMemory(currentEntry, nextEntry, bytesToMove);

                // We stay on the "current" entry (which now contains new data)
                // and re-evaluate it in the next loop iteration.
                // The prevEntry remains the same.
                continue;
            }

            if (currentEntry->NextEntryOffset == 0) {
                break;
            }
            currentEntry = (PFILE_NAMES_INFORMATION)((PBYTE)currentEntry + currentEntry->NextEntryOffset);
        }
    }

    return FLT_POSTOP_FINISHED_PROCESSING;
}


//
// Context cleanup callback
//
NTSTATUS
PdfGuardContextCleanup(
    _In_ PFLT_CONTEXT Context,
    _In_ FLT_CONTEXT_TYPE ContextType
)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ContextType);
    return STATUS_SUCCESS;
}

//
// Utility function to check if a file is a PDF.
//
BOOLEAN
IsTargetFilePdf(
    _In_ PFLT_CALLBACK_DATA Data,
    _Out_ PBOOLEAN IsDirectory
)
{
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    NTSTATUS status;
    BOOLEAN isPdf = FALSE;
    UNICODE_STRING pdfExtension = RTL_CONSTANT_STRING(L".pdf");

    *IsDirectory = FALSE;

    // Check if this is a directory open. Directories are always allowed.
    if (Data->Iopb->Parameters.Create.Options & FILE_DIRECTORY_FILE) {
        *IsDirectory = TRUE;
        return TRUE;
    }

    // Get the filename.
    status = FltGetFileNameInformation(
        Data,
        FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT,
        &nameInfo
    );

    if (!NT_SUCCESS(status)) {
        // If we can't get the name, we cautiously allow it to avoid blocking system ops.
        return TRUE;
    }

    // Parse the filename information.
    FltParseFileNameInformation(nameInfo);

    // Check for our extension.
    if (RtlSuffixUnicodeString(&nameInfo->Extension, &pdfExtension, TRUE)) {
        isPdf = TRUE;
    }

    FltReleaseFileNameInformation(nameInfo);
    return isPdf;
}
