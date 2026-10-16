/* Verification uses no RNG. Fail closed if a future caller attempts signing
 * through this verification-only TinyCrypt instance. */
#include <stdint.h>
int default_CSPRNG(uint8_t *dest, unsigned int size)
{
    (void)dest; (void)size;
    return 0;
}
