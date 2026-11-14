/* 旧版 MV-IO IAPv3 通信适配。
 * 擦除和解码全部在一个 Zephyr 线程执行；
 * Modbus 任务只校验并入队一个有界作业。报文 ACK 表示已接收，
 * 缺失位图表示已持久化或读回。
 */
#include <iap/iap.h>
#include <iap/sgz.h>
#include <zephyr/modbus/modbus.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(iap, LOG_LEVEL_INF);

typedef enum {
    IAP_IDLE,
    IAP_LOGIN,
    IAP_TRANSFER,
    IAP_ANALYSIS,
    IAP_UPGRADING,
    IAP_SUCCESS,
    IAP_FAULT,
    IAP_JUMP
} iap_state_t;

typedef enum {
    NO_CAUSE,
    VERSION,
    SPACE,
    CRC_ERROR,
    FLASH_ERROR,
    NO_APP,
    COMM_ERROR,
    SUBFILE,
    HEAP,
    OPEN_FILE,
    KEY_ERROR,
    UNKNOWN
} iap_cause_t;

typedef enum {
    JOB_NONE,
    JOB_LOGIN,
    JOB_PACKET,
    JOB_FINISH,
    JOB_EXIT,
    JOB_RECOVER,
    JOB_LOGIN_PACKET
} iap_job_t;

#define PACKET_MAX 1024
#define PACKETS_MAX (CONFIG_IAP_MAX_CONTAINER_SIZE / 16)
#define SECTOR 4096
#define TIMEOUT_MS 480000
BUILD_ASSERT(CONFIG_MODBUS_BUFFER_SIZE >= PACKET_MAX + 31,
    "IAPv3 requires room for a complete 1 KiB legacy packet");
static const struct flash_area *staging, *secondary, *primary;
static struct nvs_fs journal_fs;
static struct iap_config config;

/* 仅持久化升级结果及预期哈希，不保存未验证的续传状态。 */
static struct journal {
    uint32_t magic, phase, size;
    uint8_t hash[32];
    uint32_t crc;
} journal;

static struct {
    bool ready, busy;
    uint8_t state, cause, job;
    int owner;
    uint16_t command, every, total, number, length, last, received;
    uint32_t address;
    int64_t deadline;
} receiver;

struct receiver_buffers {
    struct flash_img_context image;
    struct iap_sgz_workspace workspace;
    uint8_t packet[PACKET_MAX];
    uint8_t bitmap[DIV_ROUND_UP(PACKETS_MAX, 8)];
    uint8_t erased[DIV_ROUND_UP(DIV_ROUND_UP(CONFIG_IAP_MAX_CONTAINER_SIZE, SECTOR), 8)];
};
static struct receiver_buffers *buffers;
static uint16_t packet_length;
K_MUTEX_DEFINE(lock);
K_SEM_DEFINE(wake, 0, 1);

typedef enum {
    STOP_REQUESTED = BIT(0),
    COMMIT_ADMITTED = BIT(1),
    REBOOT_ADMITTED = BIT(2)
} iap_stop_flags_t;

static atomic_t lifecycle;

void iap_stop(void)
{
    atomic_or(&lifecycle, STOP_REQUESTED);
    k_sem_give(&wake);
}

static bool stopped(void)
{
    return (atomic_get(&lifecycle) & STOP_REQUESTED) != 0;
}
static struct k_thread thread;
K_THREAD_STACK_DEFINE(stack, 6144);

static bool bit_get(const uint8_t *map, unsigned n)
{
    return map[n / 8] & BIT(n % 8);
}

static void bit_set(uint8_t *map, unsigned n)
{
    map[n / 8] |= BIT(n % 8);
}

static void state_set(uint8_t state, uint8_t cause)
{
    receiver.state = state;
    receiver.cause = cause;
}

static int area_read(void *ctx, uint32_t off, void *buf, size_t len)
{
    return flash_area_read(ctx, off, buf, len);
}

static int image_write(void *ctx, const void *buf, size_t len)
{
    return stopped() ? -ECANCELED : flash_img_buffered_write(ctx, buf, len, false);
}

static int journal_save(uint32_t phase, uint32_t size, const uint8_t *hash)
{
    struct journal next = {.magic = 0x33415049, .phase = phase, .size = size};
    if (hash) {
        memcpy(next.hash, hash, 32);
    }
    uint8_t wire[48];
    sys_put_le32(next.magic, wire);
    sys_put_le32(phase, wire + 4);
    sys_put_le32(size, wire + 8);
    memcpy(wire + 12, next.hash, 32);
    next.crc = iap_sgz_crc32(0, wire, 44);
    sys_put_le32(next.crc, wire + 44);
    int err = nvs_write(&journal_fs, 1, wire, sizeof(wire));
    if (err < 0) {
        return err;
    }
    journal = next;
    return 0;
}

static bool may_upload(void)
{
    return boot_is_img_confirmed() && mcuboot_swap_type() == BOOT_SWAP_TYPE_NONE;
}

static int store_packet(void)
{
    uint32_t off = (uint32_t)(receiver.number - 1) * receiver.every;
    int err = iap_sgz_decrypt(buffers->packet, packet_length, off, config.aes_key);
    if (err) {
        return err;
    }
    if (bit_get(buffers->bitmap, receiver.number - 1)) {
        err = flash_area_read(staging, off, buffers->workspace.plain, packet_length);
        return err                                                                ? err
               : memcmp(buffers->packet, buffers->workspace.plain, packet_length) ? -EBADMSG
                                                                                  : 0;
    }
    for (unsigned page = off / SECTOR; page <= (off + packet_length - 1) / SECTOR; ++page) {
        if (!bit_get(buffers->erased, page)) {
            err = flash_area_erase(staging, page * SECTOR, SECTOR);
            if (err) {
                return err;
            }
            bit_set(buffers->erased, page);
        }
    }
    err = flash_area_write(staging, off, buffers->packet, packet_length);
    if (!err) {
        err = flash_area_read(staging, off, buffers->workspace.plain, packet_length);
    }
    if (!err && memcmp(buffers->packet, buffers->workspace.plain, packet_length)) {
        err = -EIO;
    }
    return err;
}

static int finish(void)
{
    uint32_t size = 0;
    uint8_t hash[32];
    bool frozen = false;
    if (stopped()) {
        return -ECANCELED;
    }
    if (!may_upload()) {
        return -EBUSY;
    }
    int err = flash_img_init_id(&buffers->image, secondary->fa_id);
    if (err) {
        return err;
    }
    err = iap_sgz_unpack(area_read,
        (void *)staging,
        (receiver.total - 1u) * receiver.every + receiver.last,
        config.image.max_size,
        image_write,
        &buffers->image,
        &buffers->workspace,
        &size);
    if (!err) {
        err = flash_img_buffered_write(&buffers->image, NULL, 0, true);
    }
    if (buffers->image.flash_area) {
        flash_area_close(buffers->image.flash_area);
        buffers->image.flash_area = NULL;
    }
    if (!err) {
        err = iap_image_verify(area_read, (void *)secondary, size, &config.image, hash);
    }
    if (!err) {
        k_mutex_lock(&lock, K_FOREVER);
        state_set(IAP_UPGRADING, NO_CAUSE);
        k_mutex_unlock(&lock);
    }
    /* 仅在短暂的提交和复位窗口冻结参数事务。 */
    if (!err && stopped()) {
        err = -ECANCELED;
    }
    if (!err && config.prepare_reboot) {
        err = config.prepare_reboot(config.user);
        frozen = !err;
    }
    /* 唯一的原子准入点：在此之前停止请求优先，
     * 此后日志和 MCUboot 标记沿现有持久化完成路径执行。 */
    if (!err && !atomic_cas(&lifecycle, 0, COMMIT_ADMITTED)) {
        err = -ECANCELED;
    }
    if (!err) {
        err = journal_save(2, size, hash);
    }
    if (!err) {
        err = boot_request_upgrade(BOOT_UPGRADE_TEST);
    }
    if (err) {
        atomic_and(&lifecycle, ~COMMIT_ADMITTED);
        if (frozen) {
            config.cancel_reboot(config.user);
        }
        return err;
    }
    k_mutex_lock(&lock, K_FOREVER);
    state_set(IAP_JUMP, NO_CAUSE);
    k_mutex_unlock(&lock);
    LOG_INF("Verified SGZ image staged (%u bytes); MCUboot test boot requested", size);
    k_sleep(K_MSEC(500)); /* 允许主机读取状态，并给 RS485 DE 留出释放时间。 */
    if (atomic_cas(&lifecycle, COMMIT_ADMITTED, REBOOT_ADMITTED)) {
        sys_reboot(SYS_REBOOT_COLD);
    }
    return 0;
}

static uint8_t cause_for(int err)
{
    if (err == -ENOMEM) {
        return HEAP;
    }
    if (err == -EACCES) {
        return KEY_ERROR;
    }
    if (err == -EBADMSG) {
        return CRC_ERROR;
    }
    if (err == -ENOTSUP) {
        return SUBFILE;
    }
    if (err == -EFBIG || err == -ENOSPC) {
        return SPACE;
    }
    if (err == -ENOEXEC) {
        return VERSION;
    }
    if (err == -ETIMEDOUT || err == -EBUSY) {
        return COMM_ERROR;
    }
    return FLASH_ERROR;
}

static void worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (;;) {
        k_sem_take(&wake, K_SECONDS(1));
        k_mutex_lock(&lock, K_FOREVER);
        if (stopped()) {
            receiver.job = JOB_NONE;
            receiver.busy = false;
            k_mutex_unlock(&lock);
            return;
        }
        uint8_t job = receiver.job;
        if (!job && (receiver.state == IAP_LOGIN || receiver.state == IAP_TRANSFER) &&
            k_uptime_get() >= receiver.deadline) {
            state_set(IAP_FAULT, COMM_ERROR);
        }
        k_mutex_unlock(&lock);
        if (!job) {
            continue;
        }
        int err = 0;
        if (job == JOB_LOGIN) {
            err = journal_save(1, 0, NULL);
        } else if (job == JOB_EXIT) {
            err = journal_save(0, 0, NULL);
        } else if (job == JOB_PACKET || job == JOB_LOGIN_PACKET) {
            if (job == JOB_LOGIN_PACKET) {
                err = journal_save(1, 0, NULL);
            }
            if (!err) {
                err = store_packet();
            }
        } else if (job == JOB_FINISH) {
            err = finish();
        } else if (job == JOB_RECOVER) {
            if (journal.phase == 2 && !boot_is_img_confirmed()) {
                continue; /* 必须先由应用健康策略确认镜像。 */
            }
            uint8_t hash[32];
            err = journal.phase == 2
                      ? iap_image_verify(
                            area_read, (void *)primary, journal.size, &config.image, hash)
                      : -ETIMEDOUT;
            if (!err && memcmp(hash, journal.hash, 32)) {
                err = -ENOEXEC;
            }
            /* 保存最终结果，避免后续复位丢失结果。 */
            int saved = journal_save(err ? 4 : 3, 0, NULL);
            if (saved) {
                err = saved;
            }
        }
        k_mutex_lock(&lock, K_FOREVER);
        if (err) {
            state_set(IAP_FAULT, cause_for(err));
            LOG_ERR("IAP job %u failed: %d", job, err);
        } else if (job == JOB_PACKET || job == JOB_LOGIN_PACKET) {
            if (!bit_get(buffers->bitmap, receiver.number - 1)) {
                bit_set(buffers->bitmap, receiver.number - 1);
                ++receiver.received;
            }
            if (receiver.number == receiver.total) {
                receiver.last = packet_length;
            }
            state_set(IAP_TRANSFER, NO_CAUSE);
        } else if (job == JOB_RECOVER) {
            state_set(IAP_SUCCESS, NO_CAUSE);
        }
        receiver.job = JOB_NONE;
        receiver.busy = false;
        k_mutex_unlock(&lock);
    }
}

static void close_areas(void)
{
    if (staging) {
        flash_area_close(staging);
        staging = NULL;
    }
    if (secondary) {
        flash_area_close(secondary);
        secondary = NULL;
    }
    if (primary) {
        flash_area_close(primary);
        primary = NULL;
    }
}

/* 暂存区和 NVS 使用 4 KiB 单元，允许硬件擦除页更小。 */
static int check_erase_area(const struct flash_area *area)
{
    if (!flash_area_device_is_ready(area) || !area->fa_size || area->fa_size % SECTOR ||
        area->fa_off % SECTOR || flash_area_align(area) > 16) {
        return -EINVAL;
    }
    for (off_t off = area->fa_off; off < area->fa_off + area->fa_size;) {
        struct flash_pages_info info;
        int err = flash_get_page_info_by_offs(flash_area_get_device(area), off, &info);
        if (err) {
            return err;
        }
        if (!info.size || info.size > SECTOR || SECTOR % info.size || info.start_offset != off) {
            return -EINVAL;
        }
        off += info.size;
    }
    return 0;
}

static bool overlap(const struct flash_area *a, const struct flash_area *b)
{
    return flash_area_get_device(a) == flash_area_get_device(b) &&
           a->fa_off < b->fa_off + b->fa_size && b->fa_off < a->fa_off + a->fa_size;
}

int iap_init_config(const struct iap_config *cfg)
{
    if (stopped()) {
        return -ECANCELED;
    }
    if (receiver.ready) {
        return -EALREADY;
    }
    if (!cfg || !cfg->aes_key || !cfg->image.public_key || !cfg->image.key_hash ||
        cfg->image.max_size < 544 || cfg->image.load_address > UINT32_MAX - cfg->image.max_size ||
        cfg->image.sram_start >= cfg->image.sram_end ||
        (!!cfg->prepare_reboot != !!cfg->cancel_reboot)) {
        return -EINVAL;
    }
    int err = flash_area_open(cfg->staging_area_id, &staging);
    if (!err) {
        err = flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot1_partition)), &secondary);
    }
    if (!err) {
        err = flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition)), &primary);
    }
    const struct flash_area *area = NULL;
    if (!err) {
        err = flash_area_open(cfg->journal_area_id, &area);
    }
    if (!err) {
        err = check_erase_area(staging);
    }
    if (!err) {
        err = check_erase_area(area);
    }
    if (!err &&
        (staging->fa_size > CONFIG_IAP_MAX_CONTAINER_SIZE ||
            cfg->image.max_size > secondary->fa_size || cfg->image.max_size > primary->fa_size ||
            area->fa_size / SECTOR < 2 || area->fa_size / SECTOR > UINT16_MAX ||
            overlap(staging, area) || overlap(staging, primary) || overlap(staging, secondary) ||
            overlap(area, primary) || overlap(area, secondary) || overlap(primary, secondary))) {
        err = -EINVAL;
    }
    if (err) {
        if (area) {
            flash_area_close(area);
        }
        close_areas();
        return err;
    }
    memset(&journal_fs, 0, sizeof(journal_fs));
    journal_fs.flash_device = flash_area_get_device(area);
    journal_fs.offset = area->fa_off;
    journal_fs.sector_size = SECTOR;
    journal_fs.sector_count = area->fa_size / SECTOR;
    flash_area_close(area);
    err = nvs_mount(&journal_fs);
    if (err) {
        close_areas();
        return err;
    }
    uint8_t wire[48] = {0};
    err = nvs_read(&journal_fs, 1, wire, sizeof(wire));
    journal.magic = sys_get_le32(wire);
    journal.phase = sys_get_le32(wire + 4);
    journal.size = sys_get_le32(wire + 8);
    memcpy(journal.hash, wire + 12, 32);
    journal.crc = sys_get_le32(wire + 44);
    if (err != -ENOENT && (err != sizeof(wire) || journal.magic != 0x33415049 ||
                              journal.phase > 4 || journal.crc != iap_sgz_crc32(0, wire, 44))) {
        close_areas();
        return err < 0 ? err : -EBADMSG;
    }
    buffers = k_malloc(sizeof(*buffers));
    if (!buffers) {
        close_areas();
        return -ENOMEM;
    }
    memset(buffers, 0, sizeof(*buffers));
    config = *cfg;
    receiver.owner = -1;
    if (err > 0 && (journal.phase == 1 || journal.phase == 2)) {
        receiver.busy = true;
        receiver.job = JOB_RECOVER;
        state_set(IAP_UPGRADING, NO_CAUSE);
    } else if (err > 0 && journal.phase == 3) {
        state_set(IAP_SUCCESS, NO_CAUSE);
    } else if (err > 0 && journal.phase == 4) {
        state_set(IAP_FAULT, COMM_ERROR);
    }
    receiver.ready = true;
    k_thread_create(
        &thread, stack, K_THREAD_STACK_SIZEOF(stack), worker, NULL, NULL, NULL, 9, 0, K_NO_WAIT);
    k_thread_name_set(&thread, "iapv3");
    return 0;
}

bool iap_ready(void)
{
    return receiver.ready;
}

static uint16_t missing(void)
{
    for (unsigned i = 0; i < receiver.total; ++i) {
        if (!bit_get(buffers->bitmap, i)) {
            return i + 1;
        }
    }
    return 0;
}

static void put32(uint8_t *dst, uint32_t n)
{
    sys_put_be16(n, dst);
    sys_put_be16(n >> 16, dst + 2);
}

static int read_regs(uint16_t address, uint16_t count, struct modbus_adu *tx)
{
    static const uint16_t starts[] = {20049,
        20051,
        20055,
        20057,
        20059,
        20061,
        20062,
        20063,
        20071,
        20083,
        20088,
        20089,
        20090,
        20091,
        20093,
        20094,
        20099,
        20100,
        20102,
        20103};
    bool valid = false;
    for (size_t i = 0; i < ARRAY_SIZE(starts); ++i) {
        valid |= address == starts[i];
    }
    if (!valid || !count || address + (uint32_t)count > 20104) {
        return IAP_ROUTE_SILENT;
    }
    uint8_t regs[110];
    memset(regs, 0xff, sizeof(regs));
#define REG(a) (regs + ((a) - 20049) * 2)
    put32(REG(20049), 0x03000000);
    put32(REG(20051), 0x03000000);
    put32(REG(20055), 3000);
    put32(REG(20057), receiver.every ? ((receiver.every + 17u) * 11000 / 9600) + 8 : 300);
    put32(REG(20059), TIMEOUT_MS);
    sys_put_be16(0, REG(20061));
    sys_put_be16(3, REG(20062));
    memset(REG(20063), 0, 16);
    REG(20063)[15] = receiver.state;
    /* 旧版登录、接收和分析影响总状态，不影响 firmware[0]。 */
    if (receiver.state >= IAP_UPGRADING) {
        REG(20063)[0] = receiver.state;
    }
    memset(REG(20071), 0, 16);
    REG(20071)[0] = REG(20071)[15] = receiver.cause;
    sys_put_be16(receiver.command, REG(20083));
    sys_put_be16(receiver.every, REG(20088));
    sys_put_be16(receiver.total, REG(20089));
    sys_put_be16(receiver.number, REG(20090));
    put32(REG(20091), receiver.address);
    sys_put_be16(receiver.length, REG(20093));
    sys_put_be16(0, REG(20094));
    uint16_t lost = missing();
    sys_put_be16(lost, REG(20099));
    put32(REG(20100), lost ? (lost - 1u) * receiver.every : UINT32_MAX);
    sys_put_be16(
        lost ? (lost == receiver.total && receiver.last ? receiver.last : receiver.every) : 0,
        REG(20102));
    sys_put_be16(receiver.total - receiver.received, REG(20103));
    memcpy(tx->data + 1, REG(address), count * 2);
    tx->data[0] = count * 2;
    tx->length = 1 + count * 2;
    if (address <= 20103 && address + count > 20103 && receiver.total && !lost &&
        receiver.state == IAP_TRANSFER && !receiver.busy) {
        receiver.busy = true;
        receiver.job = JOB_FINISH;
        state_set(IAP_ANALYSIS, NO_CAUSE);
        k_sem_give(&wake);
    }
#undef REG
    return 0;
}

static int write_regs(int iface, uint16_t address, const uint8_t *data, size_t bytes)
{
    if (receiver.busy || receiver.state == IAP_JUMP || receiver.state == IAP_ANALYSIS) {
        return -EBUSY;
    }
    if (receiver.owner >= 0 && receiver.owner != iface &&
        (receiver.state == IAP_LOGIN || receiver.state == IAP_TRANSFER)) {
        return -EBUSY;
    }
    uint16_t command = receiver.command;
    bool login = false, logout = false;
    if (address == 20083) {
        if (bytes < 2) {
            return -EINVAL;
        }
        command = sys_get_be16(data);
        login = command == 0xaa;
        logout = command == 0x55;
        if ((!login && !logout) || (logout && bytes != 2)) {
            return -EINVAL;
        }
        if (login && !may_upload()) {
            return -EBUSY;
        }
        data += 2;
        bytes -= 2;
        ++address;
    } else if (receiver.state != IAP_LOGIN && receiver.state != IAP_TRANSFER) {
        return -EACCES;
    }

    /* 提交元数据前先校验完整报文；
     * 旧版主机可能在一笔 FC16 中同时写入命令、保留字和分包参数。 */
    uint16_t every = login || logout ? 0 : receiver.every;
    uint16_t total = login || logout ? 0 : receiver.total;
    uint16_t number = login || logout ? 0 : receiver.number;
    uint16_t length = login || logout ? 0 : receiver.length;
    uint32_t offset = login || logout ? 0 : receiver.address;
    const uint8_t *payload = NULL;
    size_t remaining = bytes;
    for (uint16_t reg = address; remaining;) {
        if (reg == 20094) {
            payload = data;
            break;
        }
        if (remaining < 2) {
            return -EINVAL;
        }
        uint16_t value = sys_get_be16(data);
        if (login && reg >= 20084 && reg <= 20087) { /* 保留字段。 */
        } else if (reg == 20088) {
            every = value;
        } else if (reg == 20089) {
            total = value;
        } else if (reg == 20090) {
            number = value;
        } else if (reg == 20091 && remaining >= 4) {
            offset = value | ((uint32_t)sys_get_be16(data + 2) << 16);
            data += 2;
            remaining -= 2;
            ++reg;
        } else if (reg == 20093) {
            length = value;
        } else {
            return -EINVAL;
        }
        data += 2;
        remaining -= 2;
        ++reg;
    }
    if ((every && (every > PACKET_MAX || every % 16)) || total > PACKETS_MAX ||
        (every && total && (uint32_t)every * total > staging->fa_size) ||
        (!login && !logout && (every != receiver.every || total != receiver.total) &&
            receiver.received) ||
        (number && (!total || number > total))) {
        return -EINVAL;
    }
    /* 旧回调中的 CUR_PACKLEN 仅对最后一包有效；
     * 其他包始终使用 EVERY_PACKLEN，即使该字段为零。 */
    uint16_t actual = number == total ? length : every;
    if (payload && (!every || !total || !number || !actual || actual > every || actual % 16 ||
                       remaining != actual ||
                       (!login && number == total && receiver.last && actual != receiver.last))) {
        return -EINVAL;
    }

    if (login || logout) {
        receiver.command = command;
        receiver.owner = logout ? -1 : iface;
        receiver.last = receiver.received = 0;
        memset(buffers->bitmap, 0, sizeof(buffers->bitmap));
        memset(buffers->erased, 0, sizeof(buffers->erased));
        state_set(login ? IAP_LOGIN : IAP_IDLE, NO_CAUSE);
        receiver.job = login ? JOB_LOGIN : JOB_EXIT;
        receiver.busy = true;
    }
    receiver.every = every;
    receiver.total = total;
    receiver.number = number;
    receiver.length = length;
    receiver.address = offset;
    if (payload) {
        packet_length = actual;
        memcpy(buffers->packet, payload, actual);
        receiver.job = login ? JOB_LOGIN_PACKET : JOB_PACKET;
        receiver.busy = true;
    }
    receiver.deadline = k_uptime_get() + TIMEOUT_MS;
    if (receiver.busy) {
        k_sem_give(&wake);
    }
    return 0;
}

int iap_route(int iface, const struct modbus_adu *rx, struct modbus_adu *tx)
{
    if (stopped()) {
        return IAP_ROUTE_SILENT;
    }
    if (!rx || !tx || iface < 0) {
        return -EINVAL;
    }
    if (rx->length < 2 || rx->length > sizeof(rx->data)) {
        return -ENOENT;
    }
    uint16_t address = sys_get_be16(rx->data);
    /* 协议能力和版本归升级模块，转发模块无需了解 IAP 寄存器。 */
    if (address == 20003 || address == 31996 || address == 31997) {
        if (rx->length != 4 || (rx->fc != 3 && rx->fc != 4) || rx->unit_id == 247 ||
            (!rx->unit_id && rx->fc != 4)) {
            return IAP_ROUTE_SILENT;
        }
        uint16_t count = sys_get_be16(rx->data + 2);
        if (!count || count > (address == 31996 ? 2 : 1)) {
            return IAP_ROUTE_SILENT;
        }
        tx->data[0] = count * 2;
        sys_put_be16(!receiver.ready    ? 0
                     : address == 20003 ? 4
                     : address == 31996 ? 0x1200
                                        : 0,
            tx->data + 1);
        if (count == 2) {
            sys_put_be16(0, tx->data + 3);
        }
        tx->length = 1 + count * 2;
        return 0;
    }
    if (address < 20049 || address > 20104) {
        return -ENOENT;
    }
    if (rx->length < 4) {
        return IAP_ROUTE_SILENT;
    }
    uint16_t count = sys_get_be16(rx->data + 2);
    if (!receiver.ready || rx->unit_id == 247 ||
        (rx->unit_id == 0 && rx->fc != 4 && rx->fc != 16)) {
        return IAP_ROUTE_SILENT;
    }
    k_mutex_lock(&lock, K_FOREVER);
    int err = IAP_ROUTE_SILENT;
    if (stopped()) {
        k_mutex_unlock(&lock);
        return IAP_ROUTE_SILENT;
    }
    if (rx->fc == 3 || rx->fc == 4) {
        if (rx->length == 4 && count && count <= 55 &&
            (receiver.owner < 0 || receiver.owner == iface)) {
            err = read_regs(address, count, tx);
        }
    } else if (rx->fc == 6 || rx->fc == 16) {
        /* 旧版 IAP 忽略 8 位字节计数，使用完整的 16 位寄存器计数；
         * 仍须严格检查 ADU 大小和分包边界。 */
        bool valid = rx->fc == 6
                         ? rx->length == 4
                         : count && count <= PACKET_MAX / 2 + 11 && rx->length == 5u + 2u * count;
        if (valid &&
            !write_regs(
                iface, address, rx->data + (rx->fc == 6 ? 2 : 5), rx->fc == 6 ? 2 : 2u * count)) {
            memcpy(tx->data, rx->data, 4);
            tx->length = 4;
            err = 0;
        }
    }
    k_mutex_unlock(&lock);
    return rx->unit_id == 0 && rx->fc == 16 ? IAP_ROUTE_SILENT : err;
}
