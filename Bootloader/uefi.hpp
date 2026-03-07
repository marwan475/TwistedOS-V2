// https://uefi.org/sites/default/files/resources/UEFI_Spec_2_10_Aug29.pdf

#pragma once

#include <stddef.h>
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

typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;

typedef enum
{
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiUnacceptedMemoryType,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID                                                          \
    {0x9042a9de, 0x23dc, 0x4a38, 0x96, 0xfb, {0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}}

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID                                                       \
    {0x0964e5b22, 0x6459, 0x11d2, 0x8e, 0x39, {0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}

#define EFI_LOADED_IMAGE_PROTOCOL_GUID                                                             \
    {0x5B1B31A1, 0x9562, 0x11d2, 0x8E, 0x3F, {0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}

#define EFI_FILE_INFO_ID                                                                           \
    {0x09576e92, 0x6d3f, 0x11d2, 0x8e, 0x39, {0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}

#define EFI_FILE_SYSTEM_INFO_ID                                                                    \
    {0x09576e93, 0x6d3f, 0x11d2, 0x8e, 0x39, {0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}

// Status Codes
#define EFI_SUCCESS 0ULL

#define TOP_BIT 0x8000000000000000
#define DECODE_ERROR(x) (TOP_BIT | (x))
#define EFI_ERROR(x) ((INTN) ((UINTN) (x)) < 0)

#define EFI_UNSUPPORTED DECODE_ERROR(3)
#define EFI_BUFFER_TOO_SMALL DECODE_ERROR(5)
#define EFI_DEVICE_ERROR DECODE_ERROR(7)
#define EFI_NOT_FOUND DECODE_ERROR(14)
#define EFI_CRC_ERROR DECODE_ERROR(27)

// EFI_GRAPHICS_OUTPUT_PROTOCOL
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;

// EFI_PIXEL_BITMASK
typedef struct
{
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

// EFI_GRAPHICS_PIXEL_FORMAT
typedef enum
{
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

// EFI_GRAPHICS_OUTPUT_MODE_INFORMATION
typedef struct
{
    UINT32                    Version;
    UINT32                    HorizontalResolution;
    UINT32                    VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK         PixelInformation;
    UINT32                    PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

// EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE: UEFI spec 2.10 section 12.9.2.1
typedef EFI_STATUS(EFIAPI* EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
        IN EFI_GRAPHICS_OUTPUT_PROTOCOL* This, IN UINT32 ModeNumber, OUT UINTN* SizeOfInfo,
        OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION** Info);

// EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE: UEFI spec 2.10 section 12.9.2.2
typedef EFI_STATUS(EFIAPI* EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
        IN EFI_GRAPHICS_OUTPUT_PROTOCOL* This, IN UINT32 ModeNumber);

// EFI_GRAPHICS_OUTPUT_BLT_PIXEL
typedef struct
{
    UINT8 Blue;
    UINT8 Green;
    UINT8 Red;
    UINT8 Reserved;
} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;

// EFI_GRAPHICS_OUTPUT_BLT_OPERATION
typedef enum
{
    EfiBltVideoFill,
    EfiBltVideoToBltBuffer,
    EfiBltBufferToVideo,
    EfiBltVideoToVideo,
    EfiGraphicsOutputBltOperationMax
} EFI_GRAPHICS_OUTPUT_BLT_OPERATION;

// EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT: UEFI spec 2.10 section 12.9.2.3
typedef EFI_STATUS(EFIAPI* EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT)(
        IN EFI_GRAPHICS_OUTPUT_PROTOCOL*                This,
        IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL* BltBuffer OPTIONAL,
        IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation, IN UINTN SourceX, IN UINTN SourceY,
        IN UINTN DestinationX, IN UINTN DestinationY, IN UINTN Width, IN UINTN Height,
        IN UINTN Delta OPTIONAL);

// EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE
typedef struct
{
    UINT32                                MaxMode;
    UINT32                                Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN                                 SizeOfInfo;
    EFI_PHYSICAL_ADDRESS                  FrameBufferBase;
    UINTN                                 FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

// EFI_GRAPHICS_OUTPUT_PROTOCOL: UEFI spec 2.10 section 12.9.2
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE   SetMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT        Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE*      Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

// EFI_LOCATE_PROTOCOL: UEFI spec 2.10 section 7.3.16
typedef EFI_STATUS(EFIAPI* EFI_LOCATE_PROTOCOL)(IN EFI_GUID*          Protocol,
                                                IN VOID* Registration OPTIONAL,
                                                OUT VOID**            Interface);

typedef EFI_STATUS(EFIAPI* EFI_OPEN_PROTOCOL)(IN EFI_HANDLE Handle, IN EFI_GUID* Protocol,
                                              OUT VOID** Interface OPTIONAL,
                                              IN EFI_HANDLE        AgentHandle,
                                              IN EFI_HANDLE ControllerHandle, IN UINT32 Attributes);

// EFI_CLOSE_PROTOCOL: UEFI Spec 2.10 section 7.3.10
typedef EFI_STATUS(EFIAPI* EFI_CLOSE_PROTOCOL)(IN EFI_HANDLE Handle, IN EFI_GUID* Protocol,
                                               IN EFI_HANDLE AgentHandle,
                                               IN EFI_HANDLE ControllerHandle);

#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 0x00000001
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL 0x00000002
#define EFI_OPEN_PROTOCOL_TEST_PROTOCOL 0x00000004
#define EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER 0x00000008
#define EFI_OPEN_PROTOCOL_BY_DRIVER 0x00000010
#define EFI_OPEN_PROTOCOL_EXCLUSIVE 0x00000020

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

// EFI_TIME
typedef struct
{
    UINT16 Year;   // 1900 - 9999
    UINT8  Month;  // 1 - 12
    UINT8  Day;    // 1 - 31
    UINT8  Hour;   // 0 - 23
    UINT8  Minute; // 0 - 59
    UINT8  Second; // 0 - 59
    UINT8  Pad1;
    UINT32 Nanosecond; // 0 - 999,999,999
    INT16  TimeZone;   // --1440 to 1440 or 2047
    UINT8  Daylight;
    UINT8  Pad2;
} EFI_TIME;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

// EFI_FILE_OPEN: UEFI Spec 2.10 section 13.5.2
typedef EFI_STATUS(EFIAPI* EFI_FILE_OPEN)(IN EFI_FILE_PROTOCOL*   This,
                                          OUT EFI_FILE_PROTOCOL** NewHandle, IN CHAR16* FileName,
                                          IN UINT64 OpenMode, IN UINT64 Attributes);

// Open Modes
#define EFI_FILE_MODE_READ 0x0000000000000001
#define EFI_FILE_MODE_WRITE 0x0000000000000002
#define EFI_FILE_MODE_CREATE 0x8000000000000000

// File Attributes
#define EFI_FILE_READ_ONLY 0x0000000000000001
#define EFI_FILE_HIDDEN 0x0000000000000002
#define EFI_FILE_SYSTEM 0x0000000000000004
#define EFI_FILE_RESERVED 0x0000000000000008
#define EFI_FILE_DIRECTORY 0x0000000000000010
#define EFI_FILE_ARCHIVE 0x0000000000000020
#define EFI_FILE_VALID_ATTR 0x0000000000000037

// EFI_FILE_CLOSE: UEFI Spec 2.10 section 13.5.3
typedef EFI_STATUS(EFIAPI* EFI_FILE_CLOSE)(IN EFI_FILE_PROTOCOL* This);

// EFI_FILE_DELETE: UEFI Spec 2.10 section 13.5.4
typedef EFI_STATUS(EFIAPI* EFI_FILE_DELETE)(IN EFI_FILE_PROTOCOL* This);

// EFI_FILE_READ: UEFI Spec 2.10 section 13.5.5
typedef EFI_STATUS(EFIAPI* EFI_FILE_READ)(IN EFI_FILE_PROTOCOL* This, IN OUT UINTN* BufferSize,
                                          OUT VOID* Buffer);

// EFI_FILE_WRITE: UEFI Spec 2.10 section 13.5.6
typedef EFI_STATUS(EFIAPI* EFI_FILE_WRITE)(IN EFI_FILE_PROTOCOL* This, IN OUT UINTN* BufferSize,
                                           IN VOID* Buffer);

// EFI_FILE_SET_POSITION: UEFI Spec 2.10 section 13.5.11
typedef EFI_STATUS(EFIAPI* EFI_FILE_SET_POSITION)(IN EFI_FILE_PROTOCOL* This, IN UINT64 Position);

// EFI_FILE_GET_POSITION: UEFI Spec 2.10 section 13.5.12
typedef EFI_STATUS(EFIAPI* EFI_FILE_GET_POSITION)(IN EFI_FILE_PROTOCOL* This, OUT UINT64* Position);

// EFI_FILE_GET_INFO: UEFI Spec 2.10 section 13.5.13
typedef EFI_STATUS(EFIAPI* EFI_FILE_GET_INFO)(IN EFI_FILE_PROTOCOL* This,
                                              IN EFI_GUID*          InformationType,
                                              IN OUT UINTN* BufferSize, OUT VOID* Buffer);

// EFI_FILE_SET_INFO: UEFI Spec 2.10 section 13.5.14
typedef EFI_STATUS(EFIAPI* EFI_FILE_SET_INFO)(IN EFI_FILE_PROTOCOL* This,
                                              IN EFI_GUID* InformationType, IN UINTN BufferSize,
                                              IN VOID* Buffer);

// EFI_FILE_FLUSH: UEFI Spec 2.10 section 13.5.15
typedef EFI_STATUS(EFIAPI* EFI_FILE_FLUSH)(IN EFI_FILE_PROTOCOL* This);

// EFI_FILE_INFO: UEFI Spec 2.10 section 13.5.16
typedef struct
{
    UINT64   Size;
    UINT64   FileSize;
    UINT64   PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64   Attribute;
    // CHAR16   FileName [];
    CHAR16 FileName[256]; // Maybe TODO: change to dynamically allocate memory for these?
} EFI_FILE_INFO;

// File Attribute Bits
#define EFI_FILE_READ_ONLY 0x0000000000000001
#define EFI_FILE_HIDDEN 0x0000000000000002
#define EFI_FILE_SYSTEM 0x0000000000000004
#define EFI_FILE_RESERVED 0x0000000000000008
#define EFI_FILE_DIRECTORY 0x0000000000000010
#define EFI_FILE_ARCHIVE 0x0000000000000020
#define EFI_FILE_VALID_ATTR 0x0000000000000037

#define EFI_FILE_PROTOCOL_REVISION 0x00010000
#define EFI_FILE_PROTOCOL_REVISION2 0x00020000
#define EFI_FILE_PROTOCOL_LATEST_REVISION EFI_FILE_PROTOCOL_REVISION2
typedef struct EFI_FILE_PROTOCOL
{
    UINT64                Revision;
    EFI_FILE_OPEN         Open;
    EFI_FILE_CLOSE        Close;
    EFI_FILE_DELETE       Delete;
    EFI_FILE_READ         Read;
    EFI_FILE_WRITE        Write;
    EFI_FILE_GET_POSITION GetPosition;
    EFI_FILE_SET_POSITION SetPosition;
    EFI_FILE_GET_INFO     GetInfo;
    EFI_FILE_SET_INFO     SetInfo;
    EFI_FILE_FLUSH        Flush;

    // EFI_FILE_OPEN_EX  OpenEx;  // Added for revision 2
    void* OpenEx;
    // EFI_FILE_READ_EX  ReadEx;  // Added for revision 2
    void* ReadEx;
    // EFI_FILE_WRITE_EX WriteEx; // Added for revision 2
    void* WriteEx;
    // EFI_FILE_FLUSH_EX FlushEx; // Added for revision 2
    void* FlushEx;
} EFI_FILE_PROTOCOL;

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION 0x00010000

typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(
        IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* This, OUT EFI_FILE_PROTOCOL** Root);

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
{
    UINT64                                      Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

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

    EFI_OPEN_PROTOCOL  OpenProtocol;
    EFI_CLOSE_PROTOCOL CloseProtocol;
    void*              OpenProtocolInformation;

    void*               ProtocolsPerHandle;
    void*               LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    void*               InstallMultipleProtocolInterfaces;
    void*               UninstallMultipleProtocolInterfaces;

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

#define EFI_LOADED_IMAGE_PROTOCOL_REVISION 0x1000

typedef struct
{
    UINT32            Revision;
    EFI_HANDLE        ParentHandle;
    EFI_SYSTEM_TABLE* SystemTable;

    // Source location of the image
    EFI_HANDLE DeviceHandle;
    // EFI_DEVICE_PATH_PROTOCOL *FilePath;
    void* FilePath;
    VOID* Reserved;

    // Image’s load options
    UINT32 LoadOptionsSize;
    VOID*  LoadOptions;

    // Location where image was loaded
    VOID*           ImageBase;
    UINT64          ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    // EFI_IMAGE_UNLOAD Unload;
    void* Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef EFI_STATUS(EFIAPI* EFI_IMAGE_ENTRY_POINT)(IN EFI_HANDLE        ImageHandle,
                                                  IN EFI_SYSTEM_TABLE* SystemTable);
