# 固定 Zephyr 基线的 GD32 PWM CAR 边界修正；只生成构建目录副本，不写 West 源文件。
function(gd32e505_patch_pwm upstream output)
  file(SHA256 "${upstream}" upstream_sha256)
  if(NOT upstream_sha256 STREQUAL
     "2aac5b6818d0835fc483c8fcab5113374d0ab4fbdbf4bf3ed90a4dd4f8aa6884")
    message(FATAL_ERROR "GD32 PWM baseline changed; review CAR fix before updating hash")
  endif()
  file(READ "${upstream}" pwm_source)
  # Zephyr 的 period_cycles 是周期计数；GD32 的实际周期为 CAR+1。
  string(REPLACE "TIMER_CAR(config->reg) = period_cycles;"
                 "TIMER_CAR(config->reg) = period_cycles - 1U;" pwm_source "${pwm_source}")
  # 16 位定时器允许 period_cycles=65536（CAR=65535）。
  string(REPLACE "if (!config->is_32bit && (period_cycles > UINT16_MAX)) {"
                 "if (!config->is_32bit && (period_cycles > (UINT16_MAX + 1U))) {"
                 pwm_source "${pwm_source}")
  # 互补输出只允许高级定时器 CH0–CH2；主/互补输出不共享同一实例。
  string(REPLACE "if (channel >= config->channels) {"
    "bool complementary = (flags & (1U << 8)) != 0U;\n\tif ((flags & ~(PWM_POLARITY_INVERTED | (1U << 8))) ||\n\t    (complementary && (!config->is_advanced || channel > 2U))) {\n\t\treturn -ENOTSUP;\n\t}\n\tuint32_t output_enable = BIT(4U * channel + (complementary ? 2U : 0U));\n\tuint32_t output_polarity = BIT(4U * channel + (complementary ? 3U : 1U));\n\tif (channel >= config->channels) {"
    pwm_source "${pwm_source}")
  string(REPLACE "TIMER_CHCTL2_CHXEN(channel)" "output_enable" pwm_source "${pwm_source}")
  string(REPLACE "TIMER_CHCTL2_CHXP(channel)" "output_polarity" pwm_source "${pwm_source}")
  # 互补端反相，改用 PWM0，保持 pulse_cycles 表示有效脉宽。
  string(REPLACE "(TIMER_OC_MODE_PWM1 | TIMER_OC_SHADOW_ENABLE)"
    "((complementary ? TIMER_OC_MODE_PWM0 : TIMER_OC_MODE_PWM1) | TIMER_OC_SHADOW_ENABLE)"
    pwm_source "${pwm_source}")
  file(CONFIGURE OUTPUT "${output}" CONTENT "${pwm_source}" @ONLY)
endfunction()
