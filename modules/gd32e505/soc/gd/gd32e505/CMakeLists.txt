# HAL clock queries and early startup must use the same board oscillator.
dt_nodelabel(gd32_hxtal NODELABEL hxtal)
dt_prop(gd32_hxtal_hz PATH ${gd32_hxtal} PROPERTY clock-frequency REQUIRED)
# E505 uses the CL peripheral layout; XD encodes PLLMF[4] at the wrong bit.
zephyr_compile_definitions(GD32E50X_CL HXTAL_VALUE=${gd32_hxtal_hz})
# Zephyr 4.4's FMC v1 source defines GD32_FMC_V1_WORK_ALIGNED but tests
# FLASH_GD32_FMC_WORK_ALIGNED. E505 requires word programming and PGAERR.
zephyr_compile_definitions(FLASH_GD32_FMC_WORK_ALIGNED)
add_subdirectory(${ZEPHYR_BASE}/soc/gd/gd32/common ${CMAKE_CURRENT_BINARY_DIR}/common)
zephyr_include_directories(${ZEPHYR_BASE}/soc/gd/gd32/gd32e50x)
zephyr_sources(soc.c)
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")
