#pragma once

#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>

//
//  Pool Tag for memory allocations
//
#define PDFG_POOL_TAG 'GdFP' // "PFdG"

//
//  Context definition
//
typedef struct _PDFG_STREAM_CONTEXT {
    BOOLEAN IsPdfFile;
} PDFG_STREAM_CONTEXT, * PPDFG_STREAM_CONTEXT;

//
//  Global driver data structure
//
typedef struct _PDFG_DATA {
    PFLT_FILTER FilterHandle;
} PDFG_DATA, * PPDFG_DATA;

extern PDFG_DATA Globals;

//
//  Function prototypes
//

DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

NTSTATUS
PdfGuardUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
);

NTSTATUS
PdfGuardInstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
);

FLT_PREOP_CALLBACK_STATUS
PdfGuardPreCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
);

FLT_POSTOP_CALLBACK_STATUS
PdfGuardPostCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
PdfGuardPreDirectoryControl(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
);

FLT_POSTOP_CALLBACK_STATUS
PdfGuardPostDirectoryControl(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
);

NTSTATUS
PdfGuardContextCleanup(
    _In_ PFLT_CONTEXT Context,
    _In_ FLT_CONTEXT_TYPE ContextType
);

BOOLEAN
IsTargetFilePdf(
    _In_ PFLT_CALLBACK_DATA Data,
    _Out_ PBOOLEAN IsDirectory
);