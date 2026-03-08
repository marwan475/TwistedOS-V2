
#include <stdint.h>
#include <uefi.hpp>

typedef struct {
  void *MemoryMap;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GopMode;
} KernelParameters;

extern "C" {

void EFIAPI kernel_main(KernelParameters KernelArgs) {
  UINT32 *FrameBuffer = (UINT32 *)KernelArgs.GopMode.FrameBufferBase;
  UINT32 stride = KernelArgs.GopMode.Info->PixelsPerScanLine;
  UINT32 xres = KernelArgs.GopMode.Info->HorizontalResolution;
  UINT32 yres = KernelArgs.GopMode.Info->VerticalResolution;

  for (UINT32 y = 0; y < yres; y++) {
    for (UINT32 x = 0; x < xres; x++) {
      FrameBuffer[y * stride + x] = 0x00FF00FF; // Purple
    }
  }

  while (1)
    __asm__ __volatile__("hlt");
}
}