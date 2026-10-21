#include <dataprocess/params.h>
#include <platform_callback.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

/* Each phase starts a new process against the same simulated flash. Changes to
 * order, defaults, bounds and device count model actual firmware replacements. */
static struct dp_point_cfg points[4];
static struct dp_equip_cfg equipment;
static const struct flash_area *area;
static struct nvs_fs fs;
static unsigned seeds;
static const char *phase;

static void add(uint16_t dev, uint16_t sig, uint16_t initial)
{
    points[equipment.point_count++] = (struct dp_point_cfg){
        .key = {dev, sig}, .source = DP_SOURCE_PARAMETER, .type = DP_U16, .width = 1,
        .maximum = UINT16_MAX, .initial.words = {initial},
        .param_flags = DP_PARAM_WRITE | DP_PARAM_PERSIST,
    };
}
static uint16_t get(uint16_t dev, uint16_t sig)
{
    struct dp_point_state state;
    zassert_ok(dp_params_read((struct dp_key){dev, sig}, &state));
    return state.value.words[0];
}
static void set(uint16_t dev, uint16_t sig, uint16_t value)
{
    const struct dp_point_update update = {
        .key = {dev, sig}, .width = 1, .value.words = {value},
    };
    zassert_ok(dp_params_set_values(&update, 1));
}
static uint32_t checksum(void)
{
    uint32_t crc = 0;
    uint8_t block[64];
    for (size_t off = 0; off < area->fa_size; off += sizeof(block)) {
        zassert_ok(flash_area_read(area, off, block, sizeof(block)));
        crc = crc32_ieee_update(crc, block, sizeof(block));
    }
    return crc;
}
static uint16_t bucket(uint16_t dev, uint16_t sig)
{
    uint8_t bytes[4]; sys_put_le16(dev, bytes); sys_put_le16(sig, bytes + 2);
    return crc32_ieee(bytes, sizeof(bytes)) % 1024;
}
static uint16_t collision(void)
{
    for (uint32_t sig = 102; sig <= UINT16_MAX; ++sig) {
        if (bucket(10, sig) == bucket(10, 100)) { return sig; }
    }
    zassert_unreachable(); return 0;
}
static int seed(const struct dp_point_cfg *p, struct dp_value *value)
{
    zassert_equal(p->key.dev_id, 11); zassert_equal(p->key.sig_id, 100);
    value->words[0] = 333; ++seeds; return 0;
}

static int reject_seed(const struct dp_point_cfg *p, struct dp_value *value)
{
    ARG_UNUSED(p); ARG_UNUSED(value); return -EPROTO;
}

ZTEST(parameter_records, test_firmware_transition)
{
    phase = getenv("IDU_RECORD_PHASE");
    if (!phase) { phase = "write"; }
    zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(storage_partition)), &area));
    struct flash_pages_info page;
    zassert_ok(flash_get_page_info_by_offs(flash_area_get_device(area), area->fa_off, &page));
    fs = (struct nvs_fs){.flash_device = flash_area_get_device(area), .offset = area->fa_off,
        .sector_size = page.size, .sector_count = area->fa_size / page.size};
    zassert_ok(nvs_mount(&fs));
    equipment.points = points;
    if (!strcmp(phase, "write")) {
        add(10, 100, 10); add(10, 101, 20);
    } else if (!strcmp(phase, "remove")) {
        add(11, 100, 999); add(10, 100, 999);
    } else {
        add(10, 101, 999); add(11, 100, 999); add(10, 100, 999);
        if (!strcmp(phase, "add")) { points[1].seed_missing = seed; }
        if (!strcmp(phase, "collision") || !strcmp(phase, "verify")) {
            add(10, collision(), 0);
        }
    }
    if (!strcmp(phase, "seed_error")) {
        add(12, 100, 0); points[3].seed_missing = reject_seed;
    }
    if (!strcmp(phase, "type_change")) { points[2].type = DP_S16; points[2].maximum = INT16_MAX; }
    if (!strcmp(phase, "width_change")) { points[2].type = DP_WORDS; points[2].width = 2; }
    if (!strcmp(phase, "range_change")) { points[2].maximum = 500; points[2].initial.words[0] = 10; }
    if (!strcmp(phase, "obsolete")) {
        zassert_ok(nvs_clear(&fs)); zassert_ok(nvs_mount(&fs));
        uint8_t obsolete[16] = {0};
        zassert_equal(nvs_write(&fs, 1, obsolete, sizeof(obsolete)), sizeof(obsolete));
    }
    uint32_t before = checksum();
    zassert_ok(dp_init_config(&equipment));
    int err = dp_params_init(&equipment, area->fa_id);
    if (!strcmp(phase, "seed_error")) {
        zassert_equal(err, -EPROTO); zassert_equal(checksum(), before); return;
    }
    if (strstr(phase, "_change") || !strcmp(phase, "obsolete")) {
        if (!strcmp(phase, "range_change")) {
            zassert_equal(err, -ERANGE); zassert_equal(checksum(), before);
            zassert_equal(dp_params_flush(), -EROFS); return;
        }
        zassert_ok(err);
        struct dp_param_status status; dp_params_get_status(&status);
        zassert_true(status.flags & DP_PARAM_STORAGE_RESET);
        zassert_equal(get(10, 100), 999); zassert_equal(get(10, 101), 999);
        zassert_equal(get(11, 100), 999);
        before = checksum(); zassert_ok(dp_params_reload());
        dp_params_get_status(&status); zassert_false(status.flags & DP_PARAM_STORAGE_RESET);
        zassert_equal(checksum(), before); return;
    }
    zassert_ok(err);
    if (!strcmp(phase, "write")) {
        set(10, 100, 111); set(10, 101, 222);
    } else if (!strcmp(phase, "add")) {
        zassert_equal(get(10, 100), 111); zassert_equal(get(10, 101), 222);
        zassert_equal(get(11, 100), 333); zassert_equal(seeds, 1);
        zassert_ok(dp_params_reload()); zassert_equal(seeds, 1);
        set(10, 100, 444); set(11, 100, 555);
    } else {
        zassert_equal(get(11, 100), 555);
        zassert_equal(get(10, 100), !strcmp(phase, "verify") ? 888 : 444);
        if (strcmp(phase, "remove")) { zassert_equal(get(10, 101), 222); }
        if (!strcmp(phase, "collision")) {
            set(10, 100, 888); set(10, collision(), 777);
        } else if (!strcmp(phase, "verify")) { zassert_equal(get(10, collision()), 777); }
    }
    before = checksum();
    zassert_ok(dp_params_flush()); zassert_ok(dp_params_flush());
    zassert_equal(checksum(), before, "unchanged flush must not write flash");
    flash_area_close(area);
}
ZTEST_SUITE(parameter_records, NULL, NULL, NULL, NULL, NULL);
