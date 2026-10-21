#include <vport/vport.h>
#include <zephyr/ztest.h>
#include <zephyr/irq_offload.h>
#include <errno.h>
#include <string.h>

static const char owner, foreign;
static int send_error;
static uint16_t sent_port;
static struct vport_source sent_source;
static uint8_t copied[32];
static size_t copied_size;
static int receive(void *user, const struct vport_source *source, const struct vport_frame *frame)
{
    ARG_UNUSED(user);
    sent_source = *source; copied_size = frame->size;
    memcpy(copied, frame->data, frame->size);
    return 0;
}
static int send(void *user, uint16_t port, const struct vport_source *source,
                 const struct vport_frame *frame, k_timeout_t timeout)
{
    ARG_UNUSED(timeout); sent_port = port;
    receive(user, source, frame);
    return send_error;
}
static const struct vport_io io = {.send = send, .mtu = sizeof(copied),
    .destination = {.link = 8, .peer = 0x123456789abcdefULL}};
static const struct vport_binding binding = {.port = 5, .io = &io, .receive = receive};
static const uint8_t data[] = {1, 2, 3};
static const struct vport_frame frame = {.data = data, .size = sizeof(data)};
static const struct vport_source source = {.link = 9, .peer = 77};
static void *setup(void)
{
    zassert_equal(vport_input(&owner, 5, &source, &frame), -ENOENT);
    zassert_ok(vport_register(&owner, &binding));
    return NULL;
}
ZTEST(vport, test_registration_and_owner_isolation)
{
    zassert_ok(vport_register(&owner, &binding));
    zassert_equal(vport_register(&foreign, &binding), -EEXIST);
    struct vport_binding replacement = binding;
    zassert_equal(vport_register(&owner, &replacement), -EEXIST);
    zassert_equal(vport_input(&foreign, 5, &source, &frame), -ENOENT);
    zassert_equal(vport_output(&foreign, 5, NULL, &frame, K_NO_WAIT), -ENOENT);
    zassert_equal(vport_register(NULL, &binding), -EINVAL);
}
ZTEST(vport, test_complete_message_and_source)
{
    zassert_ok(vport_input(&owner, 5, &source, &frame));
    zassert_equal(copied_size, sizeof(data)); zassert_mem_equal(copied, data, sizeof(data));
    zassert_equal(sent_source.link, 9); zassert_equal(sent_source.peer, 77);
    zassert_ok(vport_output(&owner, 5, NULL, &frame, K_MSEC(10)));
    zassert_equal(sent_port, 5); zassert_equal(sent_source.peer, io.destination.peer);
    zassert_ok(vport_output(&owner, 5, &source, &frame, K_MSEC(10)));
    zassert_equal(sent_source.link, 9); zassert_equal(sent_source.peer, 77);
}
ZTEST(vport, test_errors_and_payload_boundaries)
{
    struct vport_frame bad = frame; bad.size = 33;
    zassert_equal(vport_input(&owner, 5, &source, &bad), -EMSGSIZE);
    zassert_equal(vport_output(&owner, 5, NULL, &bad, K_NO_WAIT), -EMSGSIZE);
    bad.data = NULL; bad.size = 1;
    zassert_equal(vport_input(&owner, 5, &source, &bad), -EINVAL);
    send_error = -EIO;
    zassert_equal(vport_output(&owner, 5, NULL, &frame, K_NO_WAIT), -EIO);
    send_error = 1;
    zassert_equal(vport_output(&owner, 5, NULL, &frame, K_NO_WAIT), -EIO);
    send_error = 0;
    zassert_ok(vport_output(&owner, 5, NULL, &frame, K_NO_WAIT));
}
static void interrupt(const void *unused)
{
    ARG_UNUSED(unused);
    zassert_equal(vport_register(&owner, &binding), -EWOULDBLOCK);
    zassert_equal(vport_output(&owner, 5, NULL, &frame, K_NO_WAIT), -EWOULDBLOCK);
    zassert_ok(vport_input(&owner, 5, &source, &frame));
}
ZTEST(vport, test_interrupt_contract) { irq_offload(interrupt, NULL); }
ZTEST_SUITE(vport, NULL, setup, NULL, NULL, NULL);
