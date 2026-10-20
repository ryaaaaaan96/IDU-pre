#ifndef TEST_GD32_ADC_H
#define TEST_GD32_ADC_H
#include <stdint.h>
#define ADC0 0x40012400U
#define ADC_CTL1_RSTCLB 8U
#define ADC_CTL1_CLB 4U
#define ADC_SAMPLETIME_1POINT5 0
#define ADC_SAMPLETIME_7POINT5 1
#define ADC_SAMPLETIME_13POINT5 2
#define ADC_SAMPLETIME_28POINT5 3
#define ADC_SAMPLETIME_41POINT5 4
#define ADC_SAMPLETIME_55POINT5 5
#define ADC_SAMPLETIME_71POINT5 6
#define ADC_SAMPLETIME_239POINT5 7
#define ADC_RESOLUTION_12B 0
#define ADC_RESOLUTION_10B 1
#define ADC_RESOLUTION_8B 2
#define ADC_RESOLUTION_6B 3
#define ADC_MODE_FREE 0
#define ADC_CONTINUOUS_MODE 1
#define ADC_SCAN_MODE 2
#define DISABLE 0
#define ENABLE 1
#define ADC_DATAALIGN_RIGHT 0
#define ADC_REGULAR_CHANNEL 0
#define ADC0_1_2_EXTTRIG_REGULAR_NONE 7
#define ADC_FLAG_EOC 2
uint32_t *test_ctl1(void);
#define ADC_CTL1(reg) (*test_ctl1())
void adc_deinit(uint32_t reg);
void adc_mode_config(uint32_t mode);
void adc_special_function_config(uint32_t reg, uint32_t function, uint32_t enable);
void adc_data_alignment_config(uint32_t reg, uint32_t align);
void adc_channel_length_config(uint32_t reg, uint32_t group, uint32_t length);
void adc_external_trigger_source_config(uint32_t reg, uint32_t group, uint32_t source);
void adc_external_trigger_config(uint32_t reg, uint32_t group, uint32_t enable);
void adc_enable(uint32_t reg);
void adc_disable(uint32_t reg);
void adc_resolution_config(uint32_t reg, uint32_t resolution);
void adc_regular_channel_config(uint32_t reg, uint32_t rank, uint32_t channel, uint32_t sample_time);
void adc_flag_clear(uint32_t reg, uint32_t flag);
void adc_software_trigger_enable(uint32_t reg, uint32_t group);
int adc_flag_get(uint32_t reg, uint32_t flag);
uint16_t adc_regular_data_read(uint32_t reg);
#endif
