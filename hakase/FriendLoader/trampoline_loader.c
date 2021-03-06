#include "trampoline_loader.h"
#include "deploy.h"
#include "common/_memory.h"
#include "cpu_hotplug.h"
#include <asm/realmode.h>
#include <linux/slab.h>

static uint8_t jmp_bin[] = {0xeb, 0x1e, 0x66, 0x90}; // jmp 0x20; xchg %ax, &ax
static const size_t trampoline_buf_align = 0x1000;
static const size_t trampoline_buf_size = 0x4000;

int trampoline_region_alloc(struct trampoline_region *region) {
  phys_addr_t tpaddr;
  uint32_t __iomem *io_addr;

  // search signature
  for (tpaddr = 0x1000; tpaddr < 0x100000; tpaddr += trampoline_buf_align) {
    if (tpaddr >= 0xA0000 && tpaddr <= 0xBF000) {
      // this area is reserved. (ref. Multiprocessor Specification)
      continue;
    }
    io_addr = ioremap(tpaddr, 0x1000);
    if (io_addr == 0) {
      continue;
    }
    if (io_addr[kMemoryMapSignature / sizeof(*io_addr)] == FRIEND_LOADER_TRAMPOLINE_SIGNATURE) {
      // found
      region->paddr = tpaddr;
      iounmap(io_addr);
      return 0;
    }
    iounmap(io_addr);
  }
  return -1;
}

int trampoline_region_init(struct trampoline_region *region,
                           phys_addr_t phys_addr_start,
                           phys_addr_t phys_addr_end) {
  extern uint8_t _binary_boot_trampoline_bin_start[];
  extern uint8_t _binary_boot_trampoline_bin_end[];
  extern uint8_t _binary_boot_trampoline_bin_size[];
  size_t binary_boot_trampoline_bin_size =
      (size_t)_binary_boot_trampoline_bin_size;
  uint8_t *buf;
  const size_t kRegionSize = binary_boot_trampoline_bin_size + kMemoryMapTrampolineBinLoadPoint;

  {
    uint8_t __iomem *vaddr = ioremap(region->paddr, 0x1000);
    uint64_t __iomem *vaddr64 = (uint64_t __iomem *)vaddr;

    memcpy(vaddr, jmp_bin, sizeof(jmp_bin) / sizeof(jmp_bin[0]));

    if (0x1000 - kMemoryMapTrampolineBinLoadPoint < binary_boot_trampoline_bin_size) {
      return -1;
    }

    if (_binary_boot_trampoline_bin_start + binary_boot_trampoline_bin_size !=
        _binary_boot_trampoline_bin_end) {
      // invalid state
      return -1;
    }

    // copy trampoline binary to trampoline region + 8 byte
    memcpy(vaddr + kMemoryMapTrampolineBinLoadPoint / (sizeof(*vaddr)),
           _binary_boot_trampoline_bin_start, binary_boot_trampoline_bin_size);

    // check address
    if ((phys_addr_start & (2 * 1024 * 1024 - 1)) != 0) {
      // should be aligned to 2MB boundary
      // because of using 2MB huge page
      return -1;
    }

    // initialize trampoline header
    vaddr64[kMemoryMapRegionOffset / sizeof(*vaddr64)] = region->paddr;
    vaddr64[kMemoryMapPhysAddrStart / sizeof(*vaddr64)] = phys_addr_start;
    vaddr64[kMemoryMapId / sizeof(*vaddr64)] = 0;  // will be initialized by trampoline_region_set_id()

    buf = (uint8_t *)kmalloc(kRegionSize, GFP_KERNEL);
    memcpy_fromio(buf, vaddr, kRegionSize);
    
    iounmap(vaddr);
  }

  // make copy of trampoline region at deploy area
  if (deploy((const char *)buf, kRegionSize, region->paddr) < 0) {
    pr_err("friend_loader: deploy failed\n");
    return -1;
  }
  if (deploy((const char *)buf, kRegionSize, 0) < 0) {
    pr_err("friend_loader: deploy failed\n");
    return -1;
  }

  if (deploy_zero(kMemoryMapPml4t, kMemoryMapStack + 0x1000 * get_cpu_num() - kMemoryMapPml4t) < 0) {
    pr_err("friend_loader: deploy failed\n");
    return -1;
  }

  return 0;
}

int trampoline_region_set_id(struct trampoline_region *region, int cpuid, int apicid) {
  int32_t buf[2];
  buf[0] = apicid;
  buf[1] = cpuid;
  return deploy((const char *)buf, sizeof(buf), kMemoryMapId);
}

