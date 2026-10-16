#include "idu_sgz.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned seed = 505;
static unsigned random_byte(void) { seed = seed * 1664525u + 1013904223u; return seed >> 24; }
static uint8_t input[2304], output[2048];
static size_t input_len;
static int read_buf(void *ctx, uint32_t off, void *dst, size_t len)
{
    (void)ctx;
    if (off > input_len || len > input_len - off) { return -1; }
    memcpy(dst, input + off, len); return 0;
}
static int write_buf(void *ctx, const void *src, size_t len)
{ (void)ctx; (void)src; return len > sizeof(output) ? -1 : 0; }
int main(void)
{
    struct idu_sgz_workspace ws;
    for (unsigned iteration = 0; iteration < 30000; ++iteration) {
        input_len = (random_byte() * 256 + random_byte()) % sizeof(input);
        for (size_t i = 0; i < input_len; ++i) { input[i] = random_byte(); }
        if (input_len >= 9 && iteration % 2) {
            input[0] = 0x4f;
            input[1] = input_len; input[2] = input_len >> 8; input[3] = input[4] = 0;
            input[5] = random_byte(); input[6] = random_byte() % 8; input[7] = input[8] = 0;
        }
        int n = idu_qlz_decode(input, input_len, output, sizeof(output));
        assert(n < 0 || (size_t)n <= sizeof(output));
        uint32_t size;
        (void)idu_sgz_unpack(read_buf, NULL, input_len, write_buf, NULL, &ws, &size);
    }
    assert(idu_sgz_crc32(0, "123456789", 9) == 0xcbf43926);
    puts("IAP codec: 30000 malformed inputs passed ASan/UBSan");
    return 0;
}
