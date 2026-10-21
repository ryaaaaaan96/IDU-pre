#include <dataprocess/point_parameters.h>
#include <dataprocess/params.h>
#include <platform_callback.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

static const uint8_t array_default[] = {1, 2, 3, 4, 5, 6};
static const DM_SIG_CFG_S signals[] = {
    {.sig_id = 1, .sig_size = 2, .data_type = DATA_TYPE_U16,
     .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_RANGE_CHECK,
     .data = {.def_value.u16 = 10, .max_value.u16 = 100}},
    {.sig_id = 2, .sig_size = 2, .data_type = DATA_TYPE_U16,
     .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_NO_RESTORE | FLAG_RANGE_CHECK,
     .data = {.def_value.u16 = 11, .max_value.u16 = 100}},
    {.sig_id = 3, .sig_size = 2, .data_type = DATA_TYPE_U16,
     .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE},
    {.sig_id = 4, .sig_size = 4, .data_type = DATA_TYPE_S32, .sig_flag = FLAG_RANGE_CHECK,
     .data = {.def_value.s32 = -10, .min_value.s32 = -100, .max_value.s32 = 100}},
    {.sig_id = 5, .sig_size = 8, .data_type = DATA_TYPE_U16ARRAY},
    {.sig_id = 6, .sig_size = 6, .data_type = DATA_TYPE_U8ARRAY,
     .data.def_value.pst = (void *)array_default},
    {.sig_id = 7, .sig_size = 3, .data_type = DATA_TYPE_U8ARRAY,
     .data.def_value.str = {1, 2, 3}},
    {.sig_id = 8, .sig_size = 2, .data_type = DATA_TYPE_U16, .sig_flag = FLAG_RANGE_CHECK},
    {.sig_id = 9, .sig_size = 2, .data_type = DATA_TYPE_U16,
     .data = {.min_value.u16 = 10, .max_value.u16 = 20}},
};
static const DEV_TABLE_S devices[] = {
    {.dev_id = 1, .ptl_tbl_cfg = {{PRO_DATA_PROCESS, ARRAY_SIZE(signals), signals}}},
    {.dev_id = 2, .ptl_tbl_cfg = {{PRO_DATA_PROCESS, ARRAY_SIZE(signals), signals}}},
};
static const DEV_TABLE_S *get_devices(void) { return devices; }
static uint16_t get_count(void) { return ARRAY_SIZE(devices); }
static const struct flash_area *storage;

static int country_override(uint16_t country, uint16_t dev, uint16_t sig,
                             DM_SIG_DATA_S *data, bool *restore)
{
    ARG_UNUSED(dev);
    if (country == 2) { return 7; } /* Positive status is not a successful override. */
    if (country == 3) { return -EIO; }
    if (country != 1) { return -ENOTSUP; }
    if (sig == 1) {
        data->def_value.u16 = 33; data->max_value.u16 = 50;
        *restore = true;
        return 0;
    }
    /* A declined override must not leak partial modifications. */
    memset(data, 0xaa, sizeof(*data)); *restore = false;
    return -ENOENT;
}
static int legacy_default(uint16_t dev, uint16_t sig, DM_SIG_DATA_S *data, bool *recover)
{
    ARG_UNUSED(dev); ARG_UNUSED(sig); ARG_UNUSED(data); ARG_UNUSED(recover);
    return 0;
}
static uint16_t selected_default(void)
{ return IS_ENABLED(CONFIG_TEST_COUNTRY_OVERRIDE) ? 33 : 10; }
static uint16_t value(uint16_t sig)
{
    struct dp_point_state state;
    zassert_ok(dp_read((struct dp_key){1, sig}, &state));
    return state.value.words[0];
}
static void write_word(uint16_t sig, uint16_t word)
{
    uint8_t bytes[2]; sys_put_le16(word, bytes);
    zassert_ok(dp_set_sig_value(1, sig, sizeof(bytes), bytes));
}
static void *setup(void)
{
    pltcb_reg_cfg_get_dev_tbl(get_devices); pltcb_reg_cfg_get_dev_num(get_count);
    pltcb_reg_para_range_def_cb(legacy_default);
    zassert_equal(dp_register_parameter_country(DP_COUNTRY_COMMON, NULL), -EINVAL);
    pltcb_reg_para_range_def_cb(NULL);
    zassert_equal(dp_register_parameter_country(1, NULL), -EINVAL);
    zassert_ok(dp_register_parameter_country(2, country_override));
    zassert_equal(dp_init(), -1); /* Invalid provider status cannot initialize DP. */
    zassert_ok(dp_register_parameter_country(IS_ENABLED(CONFIG_TEST_COUNTRY_OVERRIDE) ? 1 : 0,
                                             country_override));
    zassert_ok(dp_init());
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition)), &storage));
    zassert_ok(flash_area_erase(storage, 0, storage->fa_size));
    zassert_ok(dp_params_init(dp_config(), storage->fa_id));
    return NULL;
}
static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    zassert_ok(flash_area_erase(storage, 0, storage->fa_size));
    zassert_ok(dp_params_reload());
}

ZTEST(point_parameters, test_country_lookup_is_read_only_and_sparse)
{
    DM_SIG_DATA_S data; bool restore;
    zassert_ok(dp_point_parameters_get(0, 1, 1, &data, &restore));
    zassert_equal(data.def_value.u16, 10); zassert_true(restore);
    zassert_ok(dp_point_parameters_get(1, 1, 1, &data, &restore));
    zassert_equal(data.def_value.u16, 33); zassert_equal(data.max_value.u16, 50);
    zassert_ok(dp_point_parameters_get(1, 2, 1, &data, &restore));
    zassert_equal(data.def_value.u16, 33); /* Shared table, distinct device identity. */
    zassert_ok(dp_point_parameters_get(1, 1, 2, &data, &restore));
    zassert_equal(data.def_value.u16, 11); zassert_false(restore);
    zassert_equal(value(1), selected_default());
    zassert_equal(dp_register_parameter_country(1, country_override), -EBUSY);
}
ZTEST(point_parameters, test_unknown_country_and_invalid_output_are_atomic)
{
    DM_SIG_DATA_S original = {.def_value.u32 = 42}, data = original;
    bool restore = false;
    zassert_equal(dp_point_parameters_get(99, 1, 1, &data, &restore), -ENOTSUP);
    zassert_mem_equal(&data, &original, sizeof(data)); zassert_false(restore);
    zassert_equal(dp_point_parameters_get(2, 1, 1, &data, &restore), -EINVAL);
    zassert_mem_equal(&data, &original, sizeof(data)); zassert_false(restore);
    zassert_equal(dp_point_parameters_get(3, 1, 1, &data, &restore), -EIO);
    zassert_mem_equal(&data, &original, sizeof(data)); zassert_false(restore);
    zassert_equal(dp_point_parameters_get(0, 257, 1, &data, &restore), -ENOENT);
    zassert_equal(dp_point_parameters_get(0, 1, 99, &data, &restore), -ENOENT);
}
ZTEST(point_parameters, test_zero_arrays_and_signed_limits)
{
    uint8_t bytes[8], zero[8] = {0};
    zassert_equal(dp_get_sig_value(1, 5, sizeof(bytes), bytes), sizeof(bytes));
    zassert_mem_equal(bytes, zero, sizeof(bytes));
    zassert_equal(dp_get_sig_value(1, 6, 6, bytes), 6);
    zassert_mem_equal(bytes, array_default, 6);
    zassert_equal(dp_get_sig_value(1, 7, 3, bytes), 3);
    zassert_mem_equal(bytes, array_default, 3);
    sys_put_le32((uint32_t)-101, bytes);
    zassert_equal(dp_set_sig_value(1, 4, 4, bytes), -ERANGE);
    sys_put_le32((uint32_t)-100, bytes);
    zassert_ok(dp_set_sig_value(1, 4, 4, bytes));
    write_word(3, UINT16_MAX); /* Omitted bounds retain the full type range. */
}
ZTEST(point_parameters, test_restore_policy_and_nvs_priority)
{
    write_word(1, 20); write_word(2, 21); write_word(3, 22);
    zassert_ok(dp_params_reload());
    zassert_equal(value(1), 20); zassert_equal(value(2), 21); zassert_equal(value(3), 22);
    zassert_ok(dp_params_restore(1));
    zassert_equal(value(1), selected_default());
    zassert_equal(value(2), 21); /* Explicit opt-out remains saved and writable. */
    zassert_equal(value(3), 0); /* Omitted policy retains legacy restore behavior. */
    zassert_ok(dp_params_reload()); zassert_equal(value(2), 21);
}
ZTEST(point_parameters, test_range_flag_supports_zero_only_and_can_be_disabled)
{
    uint8_t bytes[2];
    sys_put_le16(1, bytes);
    zassert_equal(dp_set_sig_value(1, 8, 2, bytes), -ERANGE);
    sys_put_le16(0, bytes); zassert_ok(dp_set_sig_value(1, 8, 2, bytes));
    write_word(9, UINT16_MAX); /* Nonzero configured bounds are inactive without the flag. */
    sys_put_le16(75, bytes);
    zassert_equal(dp_set_sig_value(1, 1, 2, bytes),
                  IS_ENABLED(CONFIG_TEST_COUNTRY_OVERRIDE) ? -ERANGE : 0);
}
ZTEST(point_parameters, test_runtime_overrides_do_not_change_flash_configuration)
{
    DM_SIG_DATA_S changed = {.def_value.s32 = -5, .min_value.s32 = -50, .max_value.s32 = 50};
    zassert_ok(dp_set_para_def_range(1, 4, &changed));
    DM_SIG_DATA_S data; bool restore;
    zassert_ok(dp_point_parameters_get(0, 1, 4, &data, &restore));
    zassert_equal(data.def_value.s32, -10); zassert_equal(data.min_value.s32, -100);
    zassert_ok(dp_get_para_def_range(1, 4, &data));
    zassert_equal(data.def_value.s32, -5); zassert_equal(data.min_value.s32, -50);
    zassert_equal(signals[3].data.min_value.s32, -100);
    zassert_ok(dp_point_parameters_get(0, 1, 4, &data, &restore));
    zassert_ok(dp_set_para_def_range(1, 4, &data));
}
ZTEST_SUITE(point_parameters, NULL, setup, before, NULL, NULL);
