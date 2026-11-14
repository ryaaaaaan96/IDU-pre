# 固定 Zephyr 基线的定时器时钟修正；仅生成构建目录副本，不写 West 源文件。
function(gd32e505_patch_clock upstream output)
  file(SHA256 "${upstream}" upstream_sha256)
  if(NOT upstream_sha256 STREQUAL
     "0237b214206ac0b5961b64ef6f950beeee8b874dc2e328fa2257356705dc58af")
    message(FATAL_ERROR "GD32 clock baseline changed; review timer prescaler fix before updating hash")
  endif()
  file(READ "${upstream}" clock_source)
  # APB 字段 0..3 均为不分频，4..7 才是 /2、/4、/8、/16。
  string(REPLACE "if (psc != 1U)" "if (psc >= 4U)" clock_source "${clock_source}")
  file(CONFIGURE OUTPUT "${output}" CONTENT "${clock_source}" @ONLY)
endfunction()
