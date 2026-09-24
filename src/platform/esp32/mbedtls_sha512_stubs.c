// Weak stubs for mbedtls_sha512_* to satisfy libmbedcrypto.a (esp_sha.c.o)
// when mbedTLS is built without software SHA-512 in toolchain libs.

#include "sdkconfig.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32)

#include <stddef.h>

struct mbedtls_sha512_context;

__attribute__((weak, used)) void mbedtls_sha512_init(struct mbedtls_sha512_context *ctx)
{
    (void)ctx;
}

__attribute__((weak, used)) void mbedtls_sha512_free(struct mbedtls_sha512_context *ctx)
{
    (void)ctx;
}

__attribute__((weak, used)) int mbedtls_sha512_starts(struct mbedtls_sha512_context *ctx, int is384)
{
    (void)ctx;
    (void)is384;
    return -1;
}

__attribute__((weak, used)) int mbedtls_sha512_update(struct mbedtls_sha512_context *ctx, const unsigned char *input, size_t ilen)
{
    (void)ctx;
    (void)input;
    (void)ilen;
    return -1;
}

__attribute__((weak, used)) int mbedtls_sha512_finish(struct mbedtls_sha512_context *ctx, unsigned char *output)
{
    (void)ctx;
    (void)output;
    return -1;
}

#endif
