# HAL 时钟查询和早期启动必须使用相同的板级振荡器。
dt_nodelabel(gd32_hxtal NODELABEL hxtal)
dt_prop(gd32_hxtal_hz PATH ${gd32_hxtal} PROPERTY clock-frequency REQUIRED)
# E505 使用 CL 外设布局；XD 会将 PLLMF[4] 编码到错误的位。
zephyr_compile_definitions(GD32E50X_CL HXTAL_VALUE=${gd32_hxtal_hz})
# Zephyr 4.4 的 FMC v1 源码定义 GD32_FMC_V1_WORK_ALIGNED，却检查
# FLASH_GD32_FMC_WORK_ALIGNED；E505 要求按字编程，并使用 PGAERR。
zephyr_compile_definitions(FLASH_GD32_FMC_WORK_ALIGNED)
add_subdirectory(${ZEPHYR_BASE}/soc/gd/gd32/common ${CMAKE_CURRENT_BINARY_DIR}/common)
zephyr_include_directories(${ZEPHYR_BASE}/soc/gd/gd32/gd32e50x)
zephyr_sources(soc.c)
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")
