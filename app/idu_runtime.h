#ifndef IDU_RUNTIME_H
#define IDU_RUNTIME_H

#include "idu_boot.h"

enum idu_runtime_state { IDU_RUNTIME_NEW, IDU_RUNTIME_RUNNING, IDU_RUNTIME_STOPPED };

/* Zero-initialize once. Failed startup/cycles are terminal until reset. */
struct idu_runtime {
    enum idu_runtime_state state;
#ifdef CONFIG_BOOTLOADER_MCUBOOT
    struct idu_boot_health boot;
#endif
};

int idu_runtime_init(struct idu_runtime *runtime);
/* Runs one application cycle; scheduling/sleep belong to the caller. */
int idu_runtime_step(struct idu_runtime *runtime);

#endif
