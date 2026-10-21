#include <vport/vport.h>
#include <errno.h>
#include <string.h>
#include <zephyr/irq_offload.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

struct event {
    struct vport_source source;
    size_t size;
    uint32_t id;
    uint8_t flags;
    uint8_t data[CONFIG_VPORT_MAX_PAYLOAD];
};
struct fake_link { uint16_t id; int error; unsigned calls; };
static struct fake_link backends[] = {{.id=10}, {.id=11}, {.id=20}, {.id=21}, {.id=30}};
K_MSGQ_DEFINE(received, sizeof(struct event), 16, 8);
K_MSGQ_DEFINE(sent, sizeof(struct event), 16, 8);
K_SEM_DEFINE(rx_entered, 0, 1);
K_SEM_DEFINE(rx_release, 0, 1);
K_SEM_DEFINE(tx_entered, 0, 1);
K_SEM_DEFINE(tx_release, 0, 1);
static atomic_t hold_rx, hold_tx;

static struct event make_event(const struct vport_frame *frame)
{
    struct event event = {.size=frame->size, .id=frame->id, .flags=frame->flags};
    if (frame->size) { memcpy(event.data, frame->data, frame->size); }
    return event;
}

static void receive(const struct vport_source *source, const struct vport_frame *frame, void *user)
{
    zassert_equal((uintptr_t)user, source->port);
    zassert_false(k_is_in_isr());
    if (atomic_cas(&hold_rx, 1, 0)) {
        k_sem_give(&rx_entered);
        zassert_ok(k_sem_take(&rx_release, K_SECONDS(1)));
    }
    struct event event = make_event(frame);
    event.source = *source;
    zassert_ok(k_msgq_put(&received, &event, K_NO_WAIT));
}

static int send(void *context, uint16_t port, uint64_t peer,
                 const struct vport_frame *frame, k_timeout_t timeout)
{
    ARG_UNUSED(timeout);
    struct fake_link *link = context;
    ++link->calls;
    if (link->id == 10 && atomic_cas(&hold_tx, 1, 0)) {
        k_sem_give(&tx_entered);
        zassert_ok(k_sem_take(&tx_release, K_SECONDS(1)));
    }
    if (link->error) { return link->error; }
    struct event event = make_event(frame);
    event.source.port = port;
    event.source.link = link->id;
    event.source.peer = peer;
    return k_msgq_put(&sent, &event, K_NO_WAIT);
}

static const struct vport_backend_ops ops = {.send=send};
static const struct vport_link_config physical[] = {
    {.id=10, .type=VPORT_CAN, .mtu=64, .ops=&ops, .context=&backends[0]},
    {.id=11, .type=VPORT_CAN, .mtu=64, .ops=&ops, .context=&backends[1]},
    {.id=20, .type=VPORT_STREAM, .mtu=16, .ops=&ops, .context=&backends[2]},
    {.id=21, .type=VPORT_STREAM, .mtu=16, .ops=&ops, .context=&backends[3]},
    {.id=30, .type=VPORT_DATAGRAM, .mtu=128, .ops=&ops, .context=&backends[4]},
};
static const struct vport_port_config ports[] = {
    {.id=100, .type=VPORT_CAN, .receive=receive, .user=(void *)100},
    {.id=101, .type=VPORT_CAN, .receive=receive, .user=(void *)101},
    {.id=102, .type=VPORT_CAN, .receive=receive, .user=(void *)102},
    {.id=200, .type=VPORT_STREAM, .receive=receive, .user=(void *)200},
    {.id=300, .type=VPORT_DATAGRAM, .receive=receive, .user=(void *)300},
    {.id=301, .type=VPORT_DATAGRAM, .receive=receive, .user=(void *)301},
    {.id=302, .type=VPORT_DATAGRAM, .receive=receive, .user=(void *)302},
};
static const struct vport_route routes[] = {
    {.port=100, .link=10, .directions=VPORT_RX | VPORT_TX,
     .rx_id=0x180, .rx_mask=0x7f0, .rx_flags_mask=VPORT_CAN_EXTENDED},
    {.port=101, .link=10, .directions=VPORT_RX | VPORT_TX,
     .rx_id=0x280, .rx_mask=0x7f0, .rx_flags_mask=VPORT_CAN_EXTENDED},
    {.port=102, .link=10, .directions=VPORT_RX | VPORT_TX,
     .rx_id=0x180, .rx_mask=0x1fffffff, .rx_flags=VPORT_CAN_EXTENDED,
     .rx_flags_mask=VPORT_CAN_EXTENDED},
    {.port=100, .link=11, .directions=VPORT_RX | VPORT_TX,
     .rx_id=0x180, .rx_mask=0x7f0, .rx_flags_mask=VPORT_CAN_EXTENDED},
    {.port=200, .link=20, .directions=VPORT_RX | VPORT_TX},
    {.port=200, .link=21, .directions=VPORT_RX | VPORT_TX},
    {.port=300, .link=30, .directions=VPORT_RX | VPORT_TX,
     .match_peer=true, .peer=0x100000001ULL},
    {.port=301, .link=30, .directions=VPORT_RX | VPORT_TX,
     .match_peer=true, .peer=0x200000001ULL},
    {.port=302, .link=30, .directions=VPORT_RX, .match_peer=true, .peer=3},
};
static const struct vport_config config = {
    .links=physical, .link_count=ARRAY_SIZE(physical),
    .ports=ports, .port_count=ARRAY_SIZE(ports),
    .routes=routes, .route_count=ARRAY_SIZE(routes),
};
static const uint8_t payload[] = {1, 2, 3, 4};
static const struct vport_frame can_frame = {.id=0x180, .data=payload, .size=sizeof(payload)};
static const struct vport_frame stream_frame = {.data=payload, .size=sizeof(payload)};

static struct event pop(struct k_msgq *queue)
{
    struct event event;
    zassert_ok(k_msgq_get(queue, &event, K_SECONDS(1)));
    return event;
}

static void wait_idle(void)
{
    for (unsigned n = 0; n < 100; ++n) {
        bool idle = true;
        for (size_t i = 0; i < ARRAY_SIZE(physical); ++i) {
            struct vport_stats stats;
            zassert_ok(vport_get_stats(physical[i].id, &stats));
            idle &= stats.rx_delivered == stats.rx_queued;
        }
        if (idle) { return; }
        k_msleep(1);
    }
    zassert_unreachable("RX worker did not drain");
}

static void *setup(void)
{
    zassert_equal(vport_rx_submit(10, 0, &can_frame), -EACCES);
    zassert_equal(vport_send(100, 10, 0, &can_frame, K_NO_WAIT), -EACCES);
    struct vport_config bad = config;
    bad.route_count = 0;
    zassert_equal(vport_init(&bad), -EINVAL);
    zassert_ok(vport_init(&config));
    zassert_equal(vport_init(&config), -EALREADY);
    return NULL;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    wait_idle();
    k_msgq_purge(&received);
    k_msgq_purge(&sent);
}

ZTEST(vport, test_can_filtering_and_extended_id)
{
    zassert_ok(vport_rx_submit(10, 0, &can_frame));
    struct event event = pop(&received);
    zassert_equal(event.source.port, 100);
    struct vport_frame frame = can_frame;
    frame.id = 0x28f;
    zassert_ok(vport_rx_submit(10, 0, &frame));
    event = pop(&received);
    zassert_equal(event.source.port, 101);
    frame.id = 0x180; frame.flags = VPORT_CAN_EXTENDED;
    zassert_ok(vport_rx_submit(10, 0, &frame));
    event = pop(&received);
    zassert_equal(event.source.port, 102);
    zassert_equal(event.flags, VPORT_CAN_EXTENDED);
    struct vport_stats old, now;
    zassert_ok(vport_get_stats(10, &old));
    frame.id = 0x500;
    zassert_equal(vport_rx_submit(10, 0, &frame), -ENOENT);
    zassert_ok(vport_get_stats(10, &now));
    zassert_equal(now.rx_unrouted, old.rx_unrouted + 1);
}

ZTEST(vport, test_multiple_links_reply_to_origin)
{
    zassert_ok(vport_rx_submit(11, 42, &can_frame));
    struct event event = pop(&received);
    zassert_equal(event.source.port, 100);
    zassert_equal(event.source.link, 11);
    struct vport_frame reply = can_frame;
    reply.id = 0x580; /* 响应 CAN ID 由协议指定，不复用接收 ID。 */
    zassert_ok(vport_reply(&event.source, &reply, K_MSEC(20)));
    event = pop(&sent);
    zassert_equal(event.source.port, 100);
    zassert_equal(event.source.link, 11);
    zassert_equal(event.source.peer, 42);
    zassert_equal(event.id, 0x580);
    zassert_equal(k_msgq_num_used_get(&sent), 0);
    zassert_equal(vport_send(100, 20, 0, &stream_frame, K_NO_WAIT), -EACCES);
}

ZTEST(vport, test_shared_link_callback_receives_each_virtual_port)
{
    const uint16_t ids[] = {100, 101, 102};
    for (size_t i = 0; i < ARRAY_SIZE(ids); ++i) {
        zassert_ok(vport_send(ids[i], 10, 0, &can_frame, K_MSEC(20)));
        struct event event = pop(&sent);
        zassert_equal(event.source.port, ids[i]);
        zassert_equal(event.source.link, 10);
        zassert_equal(event.id, can_frame.id);
        zassert_mem_equal(event.data, payload, sizeof(payload));
    }
}

ZTEST(vport, test_stream_sources_are_not_concatenated)
{
    struct vport_frame frame = {.data=(const uint8_t *)"A", .size=1};
    zassert_ok(vport_rx_submit(20, 9, &frame));
    frame.data = (const uint8_t *)"B";
    zassert_ok(vport_rx_submit(21, 9, &frame));
    frame.data = (const uint8_t *)"C";
    zassert_ok(vport_rx_submit(20, 9, &frame));
    struct event a = pop(&received), b = pop(&received), c = pop(&received);
    zassert_equal(a.source.port, 200);
    zassert_equal(b.source.port, 200);
    zassert_equal(a.source.link, 20);
    zassert_equal(b.source.link, 21);
    zassert_equal(c.source.link, 20);
    zassert_equal(c.source.sequence, a.source.sequence + 1);
    zassert_equal(a.size, 1); zassert_equal(b.size, 1); zassert_equal(c.size, 1);
    zassert_equal(a.data[0], 'A'); zassert_equal(b.data[0], 'B'); zassert_equal(c.data[0], 'C');
}

ZTEST(vport, test_datagram_peer_and_direction)
{
    uint64_t peer = 0x200000001ULL;
    zassert_ok(vport_rx_submit(30, peer, &stream_frame));
    struct event event = pop(&received);
    zassert_equal(event.source.port, 301);
    zassert_ok(vport_reply(&event.source, &stream_frame, K_NO_WAIT));
    event = pop(&sent);
    zassert_equal(event.source.port, 301);
    zassert_equal(event.source.peer, peer);
    zassert_equal(vport_send(300, 30, peer, &stream_frame, K_NO_WAIT), -EACCES);
    zassert_equal(vport_rx_submit(30, 99, &stream_frame), -ENOENT);
    zassert_ok(vport_rx_submit(30, 3, &stream_frame));
    event = pop(&received);
    zassert_equal(vport_reply(&event.source, &stream_frame, K_NO_WAIT), -EACCES);
}

ZTEST(vport, test_rx_copy_backpressure_and_gap)
{
    struct vport_stats old, now;
    zassert_ok(vport_get_stats(20, &old));
    atomic_set(&hold_rx, 1);
    uint8_t bytes[] = {10, 11};
    struct vport_frame frame = {.data=bytes, .size=sizeof(bytes)};
    zassert_ok(vport_rx_submit(20, 0, &frame));
    zassert_ok(k_sem_take(&rx_entered, K_SECONDS(1)));
    for (int i = 1; i < CONFIG_VPORT_RX_DEPTH; ++i) {
        bytes[0] = 10 + i;
        zassert_ok(vport_rx_submit(20, 0, &frame));
    }
    zassert_equal(vport_rx_submit(20, 0, &frame), -EAGAIN);
    memset(bytes, 0xff, sizeof(bytes));
    k_sem_give(&rx_release);
    uint32_t sequence = 0;
    for (int i = 0; i < CONFIG_VPORT_RX_DEPTH; ++i) {
        struct event event = pop(&received);
        zassert_equal(event.data[0], 10 + i);
        zassert_equal(event.data[1], 11);
        if (i) { zassert_equal(event.source.sequence, sequence + 1); }
        sequence = event.source.sequence;
    }
    wait_idle();
    zassert_ok(vport_rx_submit(20, 0, &frame));
    struct event event = pop(&received);
    zassert_equal(event.source.sequence, sequence + 2);
    wait_idle();
    zassert_ok(vport_get_stats(20, &now));
    zassert_equal(now.rx_dropped, old.rx_dropped + 1);
}

ZTEST(vport, test_stream_loss_and_oversize_report_gap)
{
    zassert_ok(vport_rx_submit(20, 9, &stream_frame));
    struct event before_gap = pop(&received);
    zassert_ok(vport_rx_gap(20, 9));
    zassert_ok(vport_rx_submit(20, 9, &stream_frame));
    struct event after_gap = pop(&received);
    zassert_equal(after_gap.source.sequence, before_gap.source.sequence + 2);
    uint8_t data[17] = {0};
    const struct vport_frame oversized = {.data=data, .size=sizeof(data)};
    zassert_equal(vport_rx_submit(20, 9, &oversized), -EMSGSIZE);
    zassert_ok(vport_rx_submit(20, 9, &stream_frame));
    zassert_equal(pop(&received).source.sequence, after_gap.source.sequence + 2);
    zassert_equal(vport_rx_gap(99, 0), -ENOENT);
}

static int irq_rx_result, irq_send_result, irq_claim_result, irq_init_result;
static void irq_receive(const void *unused)
{
    ARG_UNUSED(unused);
    irq_rx_result = vport_rx_submit(10, 0, &can_frame);
    irq_send_result = vport_send(100, 10, 0, &can_frame, K_NO_WAIT);
    irq_claim_result = vport_claim(100, 10, 0, K_NO_WAIT);
    irq_init_result = vport_init(&config);
}

ZTEST(vport, test_isr_ingress_defers_callback)
{
    irq_offload(irq_receive, NULL);
    zassert_ok(irq_rx_result);
    zassert_equal(irq_send_result, -EWOULDBLOCK);
    zassert_equal(irq_claim_result, -EWOULDBLOCK);
    zassert_equal(irq_init_result, -EWOULDBLOCK);
    struct event event = pop(&received);
    zassert_equal(event.source.port, 100);
}

K_THREAD_STACK_DEFINE(sender_stack, 2048);
static struct k_thread sender_thread;
static int send_result;
static void sender(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    send_result = vport_send(101, 10, 0, &can_frame, K_MSEC(20));
}

ZTEST(vport, test_physical_lock_shared_by_virtual_ports)
{
    zassert_ok(vport_claim(100, 10, 0, K_NO_WAIT));
    /* 同线程可在事务锁内发送；其他逻辑端口不能绕过同一物理链路的锁。 */
    zassert_ok(vport_send(100, 10, 0, &can_frame, K_NO_WAIT));
    (void)pop(&sent);
    unsigned calls = backends[0].calls;
    k_thread_create(&sender_thread, sender_stack, K_THREAD_STACK_SIZEOF(sender_stack),
                    sender, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    zassert_ok(k_thread_join(&sender_thread, K_SECONDS(1)));
    zassert_equal(send_result, -EAGAIN);
    zassert_equal(backends[0].calls, calls);
    zassert_ok(vport_send(100, 11, 0, &can_frame, K_NO_WAIT));
    zassert_equal(pop(&sent).source.link, 11);
    /* 接收响应不领取发送锁。 */
    zassert_ok(vport_rx_submit(10, 0, &can_frame));
    zassert_equal(pop(&received).source.port, 100);
    zassert_ok(vport_release(10));
    zassert_ok(vport_send(101, 10, 0, &can_frame, K_NO_WAIT));
    (void)pop(&sent);
    zassert_not_equal(vport_release(10), 0);
}

ZTEST(vport, test_send_holds_link_until_backend_returns)
{
    atomic_set(&hold_tx, 1);
    k_thread_create(&sender_thread, sender_stack, K_THREAD_STACK_SIZEOF(sender_stack),
                    sender, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    zassert_ok(k_sem_take(&tx_entered, K_SECONDS(1)));
    zassert_equal(vport_send(100, 10, 0, &can_frame, K_NO_WAIT), -EBUSY);
    zassert_ok(vport_send(100, 11, 0, &can_frame, K_NO_WAIT));
    zassert_equal(pop(&sent).source.link, 11);
    k_sem_give(&tx_release);
    zassert_ok(k_thread_join(&sender_thread, K_SECONDS(1)));
    zassert_ok(send_result);
    zassert_equal(pop(&sent).source.link, 10);
}

ZTEST(vport, test_backend_failure_is_not_retried)
{
    struct vport_stats old, now;
    zassert_ok(vport_get_stats(11, &old));
    unsigned calls = backends[1].calls;
    backends[1].error = -ENETDOWN;
    zassert_equal(vport_send(100, 11, 0, &can_frame, K_NO_WAIT), -ENETDOWN);
    backends[1].error = 0;
    zassert_equal(backends[1].calls, calls + 1);
    zassert_equal(k_msgq_num_used_get(&sent), 0);
    zassert_ok(vport_get_stats(11, &now));
    zassert_equal(now.tx_errors, old.tx_errors + 1);
    zassert_equal(now.tx_completed, old.tx_completed);
    zassert_ok(vport_send(100, 11, 0, &can_frame, K_NO_WAIT));
    (void)pop(&sent);
}

ZTEST(vport, test_bounds_can_fd_and_invalid_inputs)
{
    uint8_t data[129] = {0};
    struct vport_frame frame = {.data=data, .size=17};
    zassert_equal(vport_rx_submit(20, 0, &frame), -EMSGSIZE);
    zassert_equal(vport_send(200, 20, 0, &frame, K_NO_WAIT), -EMSGSIZE);
    frame.size = 129;
    zassert_equal(vport_rx_submit(30, 0x100000001ULL, &frame), -EMSGSIZE);
    frame.size = 9; frame.id = 0x180;
    zassert_equal(vport_rx_submit(10, 0, &frame), -EMSGSIZE);
    frame.flags = VPORT_CAN_FD;
    zassert_equal(vport_rx_submit(10, 0, &frame), -EMSGSIZE);
    frame.size = 12; frame.flags |= VPORT_CAN_BRS;
    zassert_ok(vport_rx_submit(10, 0, &frame));
    zassert_equal(pop(&received).size, 12);
    frame.flags = VPORT_CAN_BRS;
    zassert_equal(vport_rx_submit(10, 0, &frame), -EINVAL);
    frame = can_frame; frame.id = 0x800;
    zassert_equal(vport_rx_submit(10, 0, &frame), -EINVAL);
    zassert_equal(vport_rx_submit(20, 0, &can_frame), -EINVAL);
    zassert_equal(vport_rx_submit(10, 0, NULL), -EINVAL);
    zassert_equal(vport_rx_submit(99, 0, &can_frame), -ENOENT);
    zassert_equal(vport_reply(NULL, &can_frame, K_NO_WAIT), -EINVAL);
    zassert_equal(vport_send(999, 10, 0, &can_frame, K_NO_WAIT), -ENOENT);
    zassert_equal(vport_get_stats(10, NULL), -EINVAL);
    frame = (struct vport_frame){0};
    zassert_ok(vport_rx_submit(30, 0x100000001ULL, &frame));
    zassert_equal(pop(&received).size, 0);
}

ZTEST(vport, test_registration_rejects_ambiguous_filters)
{
    struct vport_route copy[ARRAY_SIZE(routes)];
    memcpy(copy, routes, sizeof(copy));
    struct vport_config cfg = config;
    cfg.routes = copy;
    copy[1].rx_id = 0x180;
    zassert_equal(vport_validate(&cfg), -EADDRINUSE);
    memcpy(copy, routes, sizeof(copy));
    copy[2].rx_flags_mask = 0; copy[2].rx_flags = 0;
    zassert_equal(vport_validate(&cfg), -EADDRINUSE);
    memcpy(copy, routes, sizeof(copy));
    copy[7].match_peer = false;
    zassert_equal(vport_validate(&cfg), -EADDRINUSE);
    memcpy(copy, routes, sizeof(copy));
    copy[0].rx_id = 0x181; /* 匹配值不能包含掩码外的位。 */
    zassert_equal(vport_validate(&cfg), -EINVAL);
}

ZTEST(vport, test_registration_rejects_missing_incompatible_or_duplicate_links)
{
    struct vport_link_config copy[ARRAY_SIZE(physical)];
    memcpy(copy, physical, sizeof(copy));
    struct vport_config cfg = config;
    cfg.links = copy;
    copy[1].id = 10;
    zassert_equal(vport_validate(&cfg), -EEXIST);
    copy[1] = physical[1]; copy[0].type = VPORT_STREAM;
    zassert_equal(vport_validate(&cfg), -EPROTOTYPE);
    copy[0] = physical[0]; copy[0].id = 99;
    zassert_equal(vport_validate(&cfg), -ENOENT);
    copy[0] = physical[0]; copy[0].mtu = CONFIG_VPORT_MAX_PAYLOAD + 1;
    zassert_equal(vport_validate(&cfg), -EINVAL);
    cfg = config; cfg.link_count = CONFIG_VPORT_MAX_LINKS + 1;
    zassert_equal(vport_validate(&cfg), -E2BIG);
    zassert_equal(vport_validate(NULL), -EINVAL);
    zassert_ok(vport_validate(&config));
}

ZTEST_SUITE(vport, NULL, setup, before, NULL, NULL);
