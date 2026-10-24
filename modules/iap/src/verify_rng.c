/* 验签不使用随机数。
 * 若未来调用方尝试通过这个仅供验签的 TinyCrypt 实例签名，
 * 则直接失败。 */
#include <stdint.h>
int default_CSPRNG(uint8_t *dest, unsigned int size)
{
    (void)dest; (void)size;
    return 0;
}
