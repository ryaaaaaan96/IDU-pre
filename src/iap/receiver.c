/* Legacy MV-IO IAPv3 wire adapter. All erases/decoding run on one Zephyr
 * thread; the Modbus worker only validates and enqueues one bounded job.
 * A packet ACK means accepted. The missing bitmap means persisted/read back.
 */
#include "idu_iap.h"
#include "idu_sgz.h"
#include "iap_public_key.h"
#include <transmit/modbus_adapter.h>
#include <dataprocess/params.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(idu_iap, LOG_LEVEL_INF);
enum { IAP_IDLE, IAP_LOGIN, IAP_TRANSFER, IAP_ANALYSIS, IAP_UPGRADING, IAP_SUCCESS, IAP_FAULT, IAP_JUMP };
enum { NO_CAUSE, VERSION, SPACE, CRC_ERROR, FLASH_ERROR, NO_APP, COMM_ERROR,
       SUBFILE, HEAP, OPEN_FILE, KEY_ERROR, UNKNOWN };
enum { JOB_NONE, JOB_LOGIN, JOB_PACKET, JOB_FINISH, JOB_EXIT, JOB_RECOVER, JOB_LOGIN_PACKET };
#define PACKET_MAX 1024
#define PACKETS_MAX (IDU_SGZ_MAX_SIZE / 16)
#define SECTOR 4096
#define TIMEOUT_MS 480000
BUILD_ASSERT(CONFIG_MODBUS_BUFFER_SIZE >= PACKET_MAX + 31,
             "IAPv3 requires room for a complete 1 KiB legacy packet");
static const struct flash_area *staging, *secondary, *primary;
static struct nvs_fs journal_fs;
/* Persist only upgrade result/expected hash, never unverified resume state. */
static struct journal { uint32_t magic, phase, size; uint8_t hash[32]; uint32_t crc; } journal;
static struct {
    bool ready, busy;
    uint8_t state, cause, job;
    int owner;
    uint16_t command, every, total, number, length, last, received;
    uint32_t address;
    int64_t deadline;
} receiver;
static uint8_t bitmap[PACKETS_MAX / 8], erased[IDU_SGZ_MAX_SIZE / SECTOR / 8];
static uint8_t packet[PACKET_MAX];
static uint16_t packet_length;
static struct idu_sgz_workspace workspace;
static struct flash_img_context image;
K_MUTEX_DEFINE(lock);
K_SEM_DEFINE(wake, 0, 1);
static struct k_thread thread;
K_THREAD_STACK_DEFINE(stack, 6144);

static bool bit_get(const uint8_t *map, unsigned n) { return map[n / 8] & BIT(n % 8); }
static void bit_set(uint8_t *map, unsigned n) { map[n / 8] |= BIT(n % 8); }
static void state_set(uint8_t state, uint8_t cause) { receiver.state = state; receiver.cause = cause; }
static int area_read(void *ctx, uint32_t off, void *buf, size_t len)
{ return flash_area_read(ctx, off, buf, len); }
static int image_write(void *ctx, const void *buf, size_t len)
{ return flash_img_buffered_write(ctx, buf, len, false); }

static int journal_save(uint32_t phase, uint32_t size, const uint8_t *hash)
{
    struct journal next = {.magic = 0x33415049, .phase = phase, .size = size};
    if (hash) { memcpy(next.hash, hash, 32); }
    uint8_t wire[48];
    sys_put_le32(next.magic, wire); sys_put_le32(phase, wire + 4); sys_put_le32(size, wire + 8);
    memcpy(wire + 12, next.hash, 32);
    next.crc = idu_sgz_crc32(0, wire, 44); sys_put_le32(next.crc, wire + 44);
    int err = nvs_write(&journal_fs, 1, wire, sizeof(wire));
    if (err < 0) { return err; }
    journal = next; return 0;
}

static bool may_upload(void)
{
    return boot_is_img_confirmed() && mcuboot_swap_type() == BOOT_SWAP_TYPE_NONE;
}

static int store_packet(void)
{
    uint32_t off = (uint32_t)(receiver.number - 1) * receiver.every;
    int err = idu_sgz_decrypt(packet, packet_length, off);
    if (err) { return err; }
    if (bit_get(bitmap, receiver.number - 1)) {
        err = flash_area_read(staging, off, workspace.plain, packet_length);
        return err ? err : memcmp(packet, workspace.plain, packet_length) ? -EBADMSG : 0;
    }
    for (unsigned page = off / SECTOR; page <= (off + packet_length - 1) / SECTOR; ++page) {
        if (!bit_get(erased, page)) {
            err = flash_area_erase(staging, page * SECTOR, SECTOR);
            if (err) { return err; }
            bit_set(erased, page);
        }
    }
    err = flash_area_write(staging, off, packet, packet_length);
    if (!err) { err = flash_area_read(staging, off, workspace.plain, packet_length); }
    if (!err && memcmp(packet, workspace.plain, packet_length)) { err = -EIO; }
    return err;
}

static int finish(void)
{
    uint32_t size = 0;
    uint8_t hash[32];
    bool frozen = false;
    if (!may_upload()) { return -EBUSY; }
    int err = flash_img_init_id(&image, secondary->fa_id);
    if (err) { return err; }
    err = idu_sgz_unpack(area_read, (void *)staging,
                         (receiver.total - 1u) * receiver.every + receiver.last,
                         image_write, &image, &workspace, &size);
    if (!err) { err = flash_img_buffered_write(&image, NULL, 0, true); }
    if (image.flash_area) { flash_area_close(image.flash_area); image.flash_area = NULL; }
    if (!err) { err = idu_image_verify(area_read, (void *)secondary, size,
                                      idu_iap_public_key, idu_iap_key_hash, hash); }
    if (!err) {
        k_mutex_lock(&lock, K_FOREVER); state_set(IAP_UPGRADING, NO_CAUSE); k_mutex_unlock(&lock);
    }
    /* Freeze parameter transactions only for the short commit/reboot window. */
    if (!err) { err = dp_params_prepare_reboot(); frozen = !err; }
    if (!err) { err = journal_save(2, size, hash); }
    if (!err) { err = boot_request_upgrade(BOOT_UPGRADE_TEST); }
    if (err) { if (frozen) { dp_params_cancel_reboot(); } return err; }
    k_mutex_lock(&lock, K_FOREVER);
    state_set(IAP_JUMP, NO_CAUSE);
    k_mutex_unlock(&lock);
    LOG_INF("Verified SGZ image staged (%u bytes); MCUboot test boot requested", size);
    k_sleep(K_MSEC(500)); /* Allow the host to read status / release RS485 DE. */
    sys_reboot(SYS_REBOOT_COLD);
    return 0;
}

static uint8_t cause_for(int err)
{
    if (err == -EACCES) { return KEY_ERROR; }
    if (err == -EBADMSG) { return CRC_ERROR; }
    if (err == -ENOTSUP) { return SUBFILE; }
    if (err == -EFBIG || err == -ENOSPC) { return SPACE; }
    if (err == -ENOEXEC) { return VERSION; }
    if (err == -ETIMEDOUT || err == -EBUSY) { return COMM_ERROR; }
    return FLASH_ERROR;
}

static void worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    for (;;) {
        k_sem_take(&wake, K_SECONDS(1));
        k_mutex_lock(&lock, K_FOREVER);
        uint8_t job = receiver.job;
        if (!job && (receiver.state == IAP_LOGIN || receiver.state == IAP_TRANSFER) &&
            k_uptime_get() >= receiver.deadline) { state_set(IAP_FAULT, COMM_ERROR); }
        k_mutex_unlock(&lock);
        if (!job) { continue; }
        int err = 0;
        if (job == JOB_LOGIN) { err = journal_save(1, 0, NULL); }
        else if (job == JOB_EXIT) { err = journal_save(0, 0, NULL); }
        else if (job == JOB_PACKET || job == JOB_LOGIN_PACKET) {
            if (job == JOB_LOGIN_PACKET) { err = journal_save(1, 0, NULL); }
            if (!err) { err = store_packet(); }
        }
        else if (job == JOB_FINISH) { err = finish(); }
        else if (job == JOB_RECOVER) {
            if (journal.phase == 2 && !boot_is_img_confirmed()) {
                continue; /* The normal 10 s health policy confirms first. */
            }
            uint8_t hash[32];
            err = journal.phase == 2 ? idu_image_verify(area_read, (void *)primary, journal.size,
                         idu_iap_public_key, idu_iap_key_hash, hash) : -ETIMEDOUT;
            if (!err && memcmp(hash, journal.hash, 32)) { err = -ENOEXEC; }
            /* Store terminal result so a later reset doesn't erase the result. */
            int saved = journal_save(err ? 4 : 3, 0, NULL);
            if (saved) { err = saved; }
        }
        k_mutex_lock(&lock, K_FOREVER);
        if (err) { state_set(IAP_FAULT, cause_for(err)); LOG_ERR("IAP job %u failed: %d", job, err); }
        else if (job == JOB_PACKET || job == JOB_LOGIN_PACKET) {
            if (!bit_get(bitmap, receiver.number - 1)) {
                bit_set(bitmap, receiver.number - 1); ++receiver.received;
            }
            if (receiver.number == receiver.total) { receiver.last = packet_length; }
            state_set(IAP_TRANSFER, NO_CAUSE);
        } else if (job == JOB_RECOVER) { state_set(IAP_SUCCESS, NO_CAUSE); }
        receiver.job = JOB_NONE; receiver.busy = false;
        k_mutex_unlock(&lock);
    }
}

int idu_iap_init(void)
{
    if (receiver.ready) { return -EALREADY; }
    int err = flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(iap_staging)), &staging);
    if (!err) { err = flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot1_partition)), &secondary); }
    if (!err) { err = flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition)), &primary); }
    const struct flash_area *area;
    if (!err) { err = flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(iap_journal)), &area); }
    if (err) { return err; }
    if (staging->fa_size != IDU_SGZ_MAX_SIZE || secondary->fa_size != 0x70000 ||
        area->fa_size != 0x4000 || flash_area_align(staging) > 16) { return -EINVAL; }
    journal_fs.flash_device = flash_area_get_device(area);
    journal_fs.offset = area->fa_off;
    journal_fs.sector_size = SECTOR;
    journal_fs.sector_count = area->fa_size / SECTOR;
    flash_area_close(area);
    err = nvs_mount(&journal_fs);
    if (err) { return err; }
    uint8_t wire[48] = {0};
    err = nvs_read(&journal_fs, 1, wire, sizeof(wire));
    journal.magic = sys_get_le32(wire); journal.phase = sys_get_le32(wire + 4);
    journal.size = sys_get_le32(wire + 8); memcpy(journal.hash, wire + 12, 32);
    journal.crc = sys_get_le32(wire + 44);
    if (err != -ENOENT && (err != sizeof(wire) || journal.magic != 0x33415049 || journal.phase > 4 ||
        journal.crc != idu_sgz_crc32(0, wire, 44))) {
        return err < 0 ? err : -EBADMSG;
    }
    receiver.owner = -1;
    if (err > 0 && (journal.phase == 1 || journal.phase == 2)) {
        receiver.busy = true; receiver.job = JOB_RECOVER;
        state_set(IAP_UPGRADING, NO_CAUSE);
    } else if (err > 0 && journal.phase == 3) { state_set(IAP_SUCCESS, NO_CAUSE); }
    else if (err > 0 && journal.phase == 4) { state_set(IAP_FAULT, COMM_ERROR); }
    receiver.ready = true;
    k_thread_create(&thread, stack, K_THREAD_STACK_SIZEOF(stack), worker,
                    NULL, NULL, NULL, 9, 0, K_NO_WAIT);
    k_thread_name_set(&thread, "iapv3");
    return 0;
}

bool idu_iap_ready(void) { return receiver.ready; }

static uint16_t missing(void)
{
    for (unsigned i = 0; i < receiver.total; ++i) { if (!bit_get(bitmap, i)) { return i + 1; } }
    return 0;
}
static void put32(uint8_t *dst, uint32_t n)
{ sys_put_be16(n, dst); sys_put_be16(n >> 16, dst + 2); }

static int read_regs(uint16_t address, uint16_t count, struct modbus_adu *tx)
{
    static const uint16_t starts[] = {20049,20051,20055,20057,20059,20061,20062,
        20063,20071,20083,20088,20089,20090,20091,20093,20094,20099,20100,20102,20103};
    bool valid = false;
    for (size_t i = 0; i < ARRAY_SIZE(starts); ++i) { valid |= address == starts[i]; }
    if (!valid || !count || address + (uint32_t)count > 20104) { return TR_MODBUS_SILENT; }
    uint8_t regs[110];
    memset(regs, 0xff, sizeof(regs));
#define REG(a) (regs + ((a) - 20049) * 2)
    put32(REG(20049), 0x03000000); put32(REG(20051), 0x03000000);
    put32(REG(20055), 3000);
    put32(REG(20057), receiver.every ? ((receiver.every + 17u) * 11000 / 9600) + 8 : 300);
    put32(REG(20059), TIMEOUT_MS);
    sys_put_be16(0, REG(20061)); sys_put_be16(3, REG(20062));
    memset(REG(20063), 0, 16);
    REG(20063)[15] = receiver.state;
    /* Legacy login/receive/analyse affect total status, not firmware[0]. */
    if (receiver.state >= IAP_UPGRADING) { REG(20063)[0] = receiver.state; }
    memset(REG(20071), 0, 16); REG(20071)[0] = REG(20071)[15] = receiver.cause;
    sys_put_be16(receiver.command, REG(20083));
    sys_put_be16(receiver.every, REG(20088)); sys_put_be16(receiver.total, REG(20089));
    sys_put_be16(receiver.number, REG(20090)); put32(REG(20091), receiver.address);
    sys_put_be16(receiver.length, REG(20093)); sys_put_be16(0, REG(20094));
    uint16_t lost = missing();
    sys_put_be16(lost, REG(20099));
    put32(REG(20100), lost ? (lost - 1u) * receiver.every : UINT32_MAX);
    sys_put_be16(lost ? (lost == receiver.total && receiver.last ? receiver.last : receiver.every) : 0,
                 REG(20102));
    sys_put_be16(receiver.total - receiver.received, REG(20103));
    memcpy(tx->data + 1, REG(address), count * 2);
    tx->data[0] = count * 2; tx->length = 1 + count * 2;
    if (address <= 20103 && address + count > 20103 && receiver.total && !lost &&
        receiver.state == IAP_TRANSFER && !receiver.busy) {
        receiver.busy = true; receiver.job = JOB_FINISH; state_set(IAP_ANALYSIS, NO_CAUSE);
        k_sem_give(&wake);
    }
#undef REG
    return 0;
}

static int write_regs(int iface, uint16_t address, const uint8_t *data, size_t bytes)
{
    if (receiver.busy || receiver.state == IAP_JUMP || receiver.state == IAP_ANALYSIS) { return -EBUSY; }
    if (receiver.owner >= 0 && receiver.owner != iface &&
        (receiver.state == IAP_LOGIN || receiver.state == IAP_TRANSFER)) { return -EBUSY; }
    uint16_t command = receiver.command;
    bool login = false, logout = false;
    if (address == 20083) {
        if (bytes < 2) { return -EINVAL; }
        command = sys_get_be16(data);
        login = command == 0xaa; logout = command == 0x55;
        if ((!login && !logout) || (logout && bytes != 2)) { return -EINVAL; }
        if (login && !may_upload()) { return -EBUSY; }
        data += 2; bytes -= 2; ++address;
    } else if (receiver.state != IAP_LOGIN && receiver.state != IAP_TRANSFER) { return -EACCES; }

    /* Validate the whole frame before committing metadata. A legacy master
     * may write command + reserved words + packet geometry in one FC16. */
    uint16_t every = login || logout ? 0 : receiver.every;
    uint16_t total = login || logout ? 0 : receiver.total;
    uint16_t number = login || logout ? 0 : receiver.number;
    uint16_t length = login || logout ? 0 : receiver.length;
    uint32_t offset = login || logout ? 0 : receiver.address;
    const uint8_t *payload = NULL;
    size_t remaining = bytes;
    for (uint16_t reg = address; remaining;) {
        if (reg == 20094) { payload = data; break; }
        if (remaining < 2) { return -EINVAL; }
        uint16_t value = sys_get_be16(data);
        if (login && reg >= 20084 && reg <= 20087) { /* reserved */ }
        else if (reg == 20088) { every = value; }
        else if (reg == 20089) { total = value; }
        else if (reg == 20090) { number = value; }
        else if (reg == 20091 && remaining >= 4) {
            offset = value | ((uint32_t)sys_get_be16(data + 2) << 16);
            data += 2; remaining -= 2; ++reg;
        } else if (reg == 20093) { length = value; }
        else { return -EINVAL; }
        data += 2; remaining -= 2; ++reg;
    }
    if ((every && (every > PACKET_MAX || every % 16)) || total > PACKETS_MAX ||
        (every && total && (uint32_t)every * total > IDU_SGZ_MAX_SIZE) ||
        (!login && !logout && (every != receiver.every || total != receiver.total) && receiver.received) ||
        (number && (!total || number > total))) { return -EINVAL; }
    /* In the old callback CUR_PACKLEN is meaningful only for the final packet;
     * other packets always use EVERY_PACKLEN, even when this field is zero. */
    uint16_t actual = number == total ? length : every;
    if (payload && (!every || !total || !number || !actual || actual > every || actual % 16 ||
        remaining != actual ||
        (!login && number == total && receiver.last && actual != receiver.last))) { return -EINVAL; }

    if (login || logout) {
        receiver.command = command; receiver.owner = logout ? -1 : iface;
        receiver.last = receiver.received = 0;
        memset(bitmap, 0, sizeof(bitmap)); memset(erased, 0, sizeof(erased));
        state_set(login ? IAP_LOGIN : IAP_IDLE, NO_CAUSE);
        receiver.job = login ? JOB_LOGIN : JOB_EXIT;
        receiver.busy = true;
    }
    receiver.every = every; receiver.total = total; receiver.number = number;
    receiver.length = length; receiver.address = offset;
    if (payload) {
        packet_length = actual; memcpy(packet, payload, actual);
        receiver.job = login ? JOB_LOGIN_PACKET : JOB_PACKET;
        receiver.busy = true;
    }
    receiver.deadline = k_uptime_get() + TIMEOUT_MS;
    if (receiver.busy) { k_sem_give(&wake); }
    return 0;
}

int idu_iap_route(int iface, const struct modbus_adu *rx, struct modbus_adu *tx)
{
    if (rx->length < 4 || rx->length > sizeof(rx->data)) { return TR_MODBUS_SILENT; }
    uint16_t address = sys_get_be16(rx->data), count = sys_get_be16(rx->data + 2);
    if (address < 20049 || address > 20104) { return -ENOENT; }
    if (!receiver.ready || rx->unit_id == 247 ||
        (rx->unit_id == 0 && rx->fc != 4 && rx->fc != 16)) { return TR_MODBUS_SILENT; }
    k_mutex_lock(&lock, K_FOREVER);
    int err = TR_MODBUS_SILENT;
    if (rx->fc == 3 || rx->fc == 4) {
        if (rx->length == 4 && count && count <= 55 &&
            (receiver.owner < 0 || receiver.owner == iface)) { err = read_regs(address, count, tx); }
    } else if (rx->fc == 6 || rx->fc == 16) {
        /* Legacy IAP ignores the 8-bit byte count and uses the full 16-bit
         * register count. Exact ADU size and packet bounds remain mandatory. */
        bool valid = rx->fc == 6 ? rx->length == 4 :
            count && count <= PACKET_MAX / 2 + 11 && rx->length == 5u + 2u * count;
        if (valid && !write_regs(iface, address, rx->data + (rx->fc == 6 ? 2 : 5),
                                 rx->fc == 6 ? 2 : 2u * count)) {
            memcpy(tx->data, rx->data, 4); tx->length = 4; err = 0;
        }
    }
    k_mutex_unlock(&lock);
    return rx->unit_id == 0 && rx->fc == 16 ? TR_MODBUS_SILENT : err;
}
