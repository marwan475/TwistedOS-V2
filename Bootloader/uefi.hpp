// https://uefi.org/sites/default/files/resources/UEFI_Spec_2_10_Aug29.pdf

#pragma once

#include <stdint.h>

#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define EFIAPI __attribute__((ms_abi))

typedef uint8_t  BOOLEAN;
typedef int64_t  INTN;
typedef uint64_t UINTN;
typedef int8_t   INT8;
typedef uint8_t  UINT8;
typedef int16_t  INT16;
typedef uint16_t UINT16;
typedef int32_t  INT32;
typedef uint32_t UINT32;
typedef int64_t  INT64;
typedef uint64_t UINT64;
typedef char     CHAR8;
typedef char16_t CHAR16;
typedef void     VOID;

typedef struct
{
    UINT32 TimeLow;
    UINT16 TimeMid;
    UINT16 TimeHighAndVersion;
    UINT8  CLockSeqHighAndReserved;
    UINT8  ClockSeqLow;
    UINT8  Node[6];
} __attribute__((packed)) EFI_GUID;

typedef UINTN  EFI_STATUS;
typedef void*  EFI_HANDLE;
typedef void*  EFI_EVENT;
typedef UINT64 EFI_LBA;
typedef UINTN  EFI_TPL;

// Status Codes
#define EFI_SUCCESS 0ULL

// Text Input
typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef EFI_STATUS(EFIAPI* EFI_INPUT_RESET)(IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL* This,
                                            IN BOOLEAN ExtendedVerification);

typedef struct
{
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef EFI_STATUS(EFIAPI* EFI_INPUT_READ_KEY)(IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL* This,
                                               OUT EFI_INPUT_KEY*                 Key);

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL
{
    EFI_INPUT_RESET    Reset;
    EFI_INPUT_READ_KEY ReadKeyStroke;
    EFI_EVENT          WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

// Text Output
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS(EFIAPI* EFI_TEXT_RESET)(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
                                           IN BOOLEAN ExtendedVerification);

typedef EFI_STATUS(EFIAPI* EFI_TEXT_STRING)(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
                                            IN CHAR16*                          String);

typedef EFI_STATUS(EFIAPI* EFI_TEXT_QUERY_MODE)(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
                                                IN UINTN ModeNumber, OUT UINTN* Columns,
                                                OUT UINTN* Rows);

typedef EFI_STATUS(EFIAPI* EFI_TEXT_SET_MODE)(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
                                              IN UINTN                            ModeNumber);

typedef EFI_STATUS(EFIAPI* EFI_TEXT_SET_ATTRIBUTE)(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
                                                   IN UINTN                            Attribute);

typedef EFI_STATUS(EFIAPI* EFI_TEXT_CLEAR_SCREEN)(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This);

typedef EFI_STATUS(EFIAPI* EFI_TEXT_SET_CURSOR_POSITION)(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
                                                         IN UINTN Column, IN UINTN Row);

#define EFI_BLACK 0x00
#define EFI_BLUE 0x01
#define EFI_GREEN 0x02
#define EFI_CYAN 0x03
#define EFI_RED 0x04
#define EFI_MAGENTA 0x05
#define EFI_BROWN 0x06
#define EFI_LIGHTGRAY 0x07
#define EFI_BRIGHT 0x08
#define EFI_DARKGRAY 0x08
#define EFI_LIGHTBLUE 0x09
#define EFI_LIGHTGREEN 0x0A
#define EFI_LIGHTCYAN 0x0B
#define EFI_LIGHTRED 0x0C
#define EFI_LIGHTMAGENTA 0x0D
#define EFI_YELLOW 0x0E
#define EFI_WHITE 0x0F

#define EFI_BACKGROUND_BLACK 0x00
#define EFI_BACKGROUND_BLUE 0x10
#define EFI_BACKGROUND_GREEN 0x20
#define EFI_BACKGROUND_CYAN 0x30
#define EFI_BACKGROUND_RED 0x40
#define EFI_BACKGROUND_MAGENTA 0x50
#define EFI_BACKGROUND_BROWN 0x60
#define EFI_BACKGROUND_LIGHTGRAY 0x70

#define EFI_TEXT_ATTR(Foreground, Background) ((Foreground) | ((Background) << 4))

typedef struct
{
    INT32   MaxMode;
    INT32   Mode;
    INT32   Attribute;
    INT32   CursorColumn;
    INT32   CursorRow;
    BOOLEAN CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL
{
    EFI_TEXT_RESET               Reset;
    EFI_TEXT_STRING              OutputString;
    void*                        TestString;
    EFI_TEXT_QUERY_MODE          QueryMode;
    EFI_TEXT_SET_MODE            SetMode;
    EFI_TEXT_SET_ATTRIBUTE       SetAttribute;
    EFI_TEXT_CLEAR_SCREEN        ClearScreen;
    EFI_TEXT_SET_CURSOR_POSITION SetCursorPosition;
    void*                        EnableCursor;
    SIMPLE_TEXT_OUTPUT_MODE*     Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct
{
} EFI_RUNTIME_SERVICES;

typedef struct
{
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 REserved;
} EFI_TABLE_HEADER;

typedef EFI_STATUS(EFIAPI* EFI_WAIT_FOR_EVENT)(IN UINTN NumberOfEvents, IN EFI_EVENT* Event,
                                               OUT UINTN* Index);

typedef struct
{
    EFI_TABLE_HEADER Hdr;

    void* RaiseTPL;
    void* RestoreTPL;

    void* AllocatePages;
    void* FreePages;
    void* GetMemoryMap;
    void* AllocatePool;
    void* FreePool;

    void*              CreateEvent;
    void*              SetTimer;
    EFI_WAIT_FOR_EVENT WaitForEvent;
    void*              SignalEvent;
    void*              CloseEvent;
    void*              CheckEvent;

    void* InstallProtocolInterface;
    void* ReinstallProtocolInterface;
    void* UninstallProtocolInterface;
    void* HandleProtocol;
    VOID* Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;

    void* LoadImage;
    void* StartImage;
    void* Exit;
    void* UnloadImage;
    void* ExitBootServices;

    void* GetNextMonotonicCount;
    void* Stall;
    void* SetWatchdogTimer;

    void* ConnectController;
    void* DisconnectController;

    void* OpenProtocol;
    void* CloseProtocol;
    void* OpenProtocolInformation;

    void* ProtocolsPerHandle;
    void* LocateHandleBuffer;
    void* LocateProtocol;
    void* InstallMultipleProtocolInterfaces;
    void* UninstallMultipleProtocolInterfaces;

    void* CalculateCrc32;

    void* CopyMem;
    void* SetMem;
    void* CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct
{
} EFI_CONFIGURATION_TABLE;

// System Table
typedef struct
{
    EFI_TABLE_HEADER                 Hdr;
    CHAR16*                          FirmwareVendor;
    UINT32                           FirmwareRevision;
    EFI_HANDLE                       ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL*  ConIn;
    EFI_HANDLE                       ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE                       StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;
    EFI_RUNTIME_SERVICES*            RuntimeServices;
    EFI_BOOT_SERVICES*               BootServices;
    UINTN                            NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE*         ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef EFI_STATUS(EFIAPI* EFI_IMAGE_ENTRY_POINT)(IN EFI_HANDLE        ImageHandle,
                                                  IN EFI_SYSTEM_TABLE* SystemTable);
