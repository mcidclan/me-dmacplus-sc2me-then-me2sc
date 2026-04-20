// m-c/d 2025
#include "main.h"

PSP_MODULE_INFO("sc2me", 0, 1, 1);
PSP_HEAP_SIZE_KB(-1024);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_VFPU | PSP_THREAD_ATTR_USER);

static volatile u32* mem = nullptr;
#define meCounter         (mem[0])
#define meExit            (mem[1])

__attribute__((noinline, aligned(4)))
static void meLoop() {
  do {
    meDCacheWritebackInvalidAll();
  } while(!mem);
  
  do {
    meCounter++;
  } while(meExit == 0);
  meExit = 2;
  meHalt();
}

extern char __start__me_section;
extern char __stop__me_section;
__attribute__((section("_me_section")))
void meHandler() {
  hw(0xbc100050) = 0x0f;       // 0b11111, enable me and AW buses
  hw(0xbc100004) = 0xffffffff; // enable NMI interrupts in the global hardware context
  hw(0xbc100040) = 0x02;       // allow 64MB ram
  asm("sync");
  
  asm volatile(
    "li          $k0, 0x30000000\n"
    "mtc0        $k0, $12\n"
    "sync\n"
    "la          $k0, %0\n"
    "li          $k1, 0x80000000\n"
    "or          $k0, $k0, $k1\n"
    "jr          $k0\n"
    "nop\n"
    :
    : "i" (meLoop)
    : "k0"
  );
}

static int initMe() {
  #define me_section_size (&__stop__me_section - &__start__me_section)
  memcpy((void *)ME_HANDLER_BASE, (void*)&__start__me_section, me_section_size);
  meDCacheWritebackInvalidAll();
  // reset me
  hw(0xbc10004c) = 0x04;
  hw(0xbc10004c) = 0x0;
  asm volatile("sync");
  return 0;
}

static DMADescriptor* lli0 = nullptr;
static DMADescriptor* lli1 = nullptr;

static int sc2Me() {
  cleanChannels();
  dmacplusLLIFromSc(lli0);
  waitChannels();
  return 0;
}

static int me2Sc() {
  cleanChannels();
  dmacplusLLIFromMe(lli1);
  waitChannels();
  return 0;
}

void exitSample(const char* const str) {
  pspDebugScreenInit();
  pspDebugScreenClear();
  pspDebugScreenSetXY(0, 1);
  pspDebugScreenPrintf(str);
  sceKernelDelayThread(100000);
  sceKernelExitGame();
}

int main() {
  scePowerSetClockFrequency(333, 333, 166);

  if (pspSdkLoadStartModule("./kcall.prx", PSP_MEMORY_PARTITION_KERNEL) < 0){
    exitSample("Can't load the PRX, exiting...");
    return 0;
  }

  mem = meGetUncached32(4);
  kcall(&initMe);
  
  sceDisplaySetFrameBuf((void*)(UNCACHED_USER_MASK |
    GE_EDRAM_BASE), 512, 3, PSP_DISPLAY_SETBUF_NEXTFRAME);
  pspDebugScreenInitEx(0, PSP_DISPLAY_PIXEL_FORMAT_8888, 0);
  pspDebugScreenSetOffset(0);

  
  u8* ram = (u8*)memalign(16, 0x40000);
  lli0 = dmacplusInitLLIs(dmaControlSc2Me, (u32)ram, ME_EDRAM_BASE, 0x40000);
  lli1 = dmacplusInitLLIs(dmaControlMe2Sc, ME_EDRAM_BASE, GE_EDRAM_BASE, 0x40000);

  u32 offset = 0;
  u32 color = 0xffffffff;
  do {
    hw((u32)ram + offset) = color;
    offset += 4;
    if (!((0xff & offset) / 2)) {
      color = 0xff000000 | (randInRange(0x7f) << 16) |
        (randInRange(0x7f) << 8) | randInRange(0xff);
    }
  } while(offset < 0x40000);
  sceKernelDcacheWritebackInvalidateAll();

  
  SceCtrlData ctl;
  do {
    kcall(&sc2Me);
    kcall(&me2Sc);
    sceCtrlPeekBufferPositive(&ctl, 1);
    pspDebugScreenSetXY(0, 17);
    pspDebugScreenPrintf("%x          ", meCounter);
    sceDisplayWaitVblankStart();
  } while(!(ctl.Buttons & PSP_CTRL_HOME));
  
  meExit = 1;
  do {
    asm volatile("sync");
  } while(meExit < 2);
  
  free(ram);
  free(lli0);
  free(lli1);
  meGetUncached32(0);
  exitSample("Exiting...");
  return 0;
}
