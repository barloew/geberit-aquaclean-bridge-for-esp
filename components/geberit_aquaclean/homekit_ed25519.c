/*
 * Ed25519 key pair and signing for HomeKit when the libsodium in the build leaves them out.
 *
 * ESPHome's libsodium (used for the API encryption) is built without crypto_sign_ed25519_keypair() and
 * crypto_sign_ed25519_detached(); it still has the ed25519 core (ref10) and SHA-512, which these two need.
 * Same algorithm as libsodium 1.0.21 (crypto_sign/ed25519/ref10/keypair.c and sign.c, ISC license,
 * Copyright (c) 2013-2025 Frank Denis). Weak: a full libsodium's own functions take precedence.
 * Lives in this component (not in homekit_sdk/) so it always comes along with the component.
 */
#include "esphome/core/defines.h"

#ifdef USE_AQUACLEAN_HOMEKIT
#include <stdint.h>
#include <string.h>

#include <sodium.h>

/* libsodium internals (private/ed25519_ref10.h); the point is only passed by pointer, so an opaque
 * buffer large enough for ge25519_p3 (4 field elements, at most 4 x 40 bytes) is enough */
typedef struct {
    uint64_t opaque[24];
} hap_ge25519_p3;

extern void ge25519_scalarmult_base(hap_ge25519_p3 *h, const unsigned char *a);
extern void ge25519_p3_tobytes(unsigned char *s, const hap_ge25519_p3 *h);
extern void sc25519_reduce(unsigned char s[64]);
extern void sc25519_muladd(unsigned char s[32], const unsigned char a[32], const unsigned char b[32],
                           const unsigned char c[32]);

/* Also used by libsodium's own verify (open.c); only here in case sign.c is left out as well */
__attribute__((weak)) void _crypto_sign_ed25519_ref10_hinit(crypto_hash_sha512_state *hs, int prehashed)
{
    static const unsigned char DOM2PREFIX[32 + 2] = {
        'S', 'i', 'g', 'E', 'd', '2', '5', '5', '1', '9', ' ', 'n', 'o', ' ', 'E', 'd', '2',
        '5', '5', '1', '9', ' ', 'c', 'o', 'l', 'l', 'i', 's', 'i', 'o', 'n', 's', 1, 0};

    crypto_hash_sha512_init(hs);
    if (prehashed) {
        crypto_hash_sha512_update(hs, DOM2PREFIX, sizeof DOM2PREFIX);
    }
}

__attribute__((weak)) int crypto_sign_ed25519_keypair(unsigned char *pk, unsigned char *sk)
{
    unsigned char seed[32];
    hap_ge25519_p3 A;

    randombytes_buf(seed, sizeof seed);
    crypto_hash_sha512(sk, seed, 32);
    sk[0] &= 248;
    sk[31] &= 127;
    sk[31] |= 64;
    ge25519_scalarmult_base(&A, sk);
    ge25519_p3_tobytes(pk, &A);
    memmove(sk, seed, 32);
    memmove(sk + 32, pk, 32);
    sodium_memzero(seed, sizeof seed);
    return 0;
}

__attribute__((weak)) int crypto_sign_ed25519_detached(unsigned char *sig, unsigned long long *siglen_p,
                                                       const unsigned char *m, unsigned long long mlen,
                                                       const unsigned char *sk)
{
    crypto_hash_sha512_state hs;
    unsigned char az[64];
    unsigned char nonce[64];
    unsigned char hram[64];
    hap_ge25519_p3 R;

    crypto_hash_sha512_init(&hs);
    crypto_hash_sha512(az, sk, 32);
    crypto_hash_sha512_update(&hs, az + 32, 32);
    crypto_hash_sha512_update(&hs, m, mlen);
    crypto_hash_sha512_final(&hs, nonce);

    memmove(sig + 32, sk + 32, 32);

    sc25519_reduce(nonce);
    ge25519_scalarmult_base(&R, nonce);
    ge25519_p3_tobytes(sig, &R);

    crypto_hash_sha512_init(&hs);
    crypto_hash_sha512_update(&hs, sig, 64);
    crypto_hash_sha512_update(&hs, m, mlen);
    crypto_hash_sha512_final(&hs, hram);

    sc25519_reduce(hram);
    az[0] &= 248;
    az[31] &= 127;
    az[31] |= 64;
    sc25519_muladd(sig + 32, hram, az, nonce);

    sodium_memzero(az, sizeof az);
    sodium_memzero(nonce, sizeof nonce);
    if (siglen_p != NULL) {
        *siglen_p = 64U;
    }
    return 0;
}

#endif  // USE_AQUACLEAN_HOMEKIT
