
#include "cpuminer-config.h"
#include "miner.h"

#include <gmp.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <sph_sha2.h>
#include <sph_keccak.h>
#include <sph_haval.h>
#include <sph_tiger.h>
#include <sph_whirlpool.h>
#include <sph_ripemd.h>

static void mpz_set_uint256(mpz_t r, uint8_t *u)
{
    mpz_import(r, 32 / sizeof(unsigned long), -1, sizeof(unsigned long), -1, 0, u);
}

static void mpz_get_uint256(mpz_t r, uint8_t *u)
{
    u=0;
    mpz_export(u, 0, -1, sizeof(unsigned long), -1, 0, r);
}

static void mpz_set_uint512(mpz_t r, uint8_t *u)
{
    mpz_import(r, 64 / sizeof(unsigned long), -1, sizeof(unsigned long), -1, 0, u);
}

static void set_one_if_zero(uint8_t *hash512) {
    for (int i = 0; i < 32; i++) {
        if (hash512[i] != 0) {
            return;
        }
    }
    hash512[0] = 1;
}

static void m7hash_old(const char *finalhash, const unsigned char *input, int len)
{
    sph_sha256_context       ctx_sha256;
    sph_sha512_context       ctx_sha512;
    sph_keccak512_context    ctx_keccak;
    sph_whirlpool_context    ctx_whirlpool;
    sph_haval256_5_context   ctx_haval;
    sph_tiger_context        ctx_tiger;
    sph_ripemd160_context    ctx_ripemd;

    unsigned char hash[7][64];
    memset(hash, 0, 7 * 64);

    const void* ptr = input;
    size_t sz = len;

    sph_sha256_init(&ctx_sha256);
    // ZSHA256;
    sph_sha256 (&ctx_sha256, ptr, sz);
    sph_sha256_close(&ctx_sha256, (void*)(hash[0]));

    sph_sha512_init(&ctx_sha512);
    // ZSHA512;
    sph_sha512 (&ctx_sha512, ptr, sz);
    sph_sha512_close(&ctx_sha512, (void*)(hash[1]));
    
    sph_keccak512_init(&ctx_keccak);
    // ZKECCAK;
    sph_keccak512 (&ctx_keccak, ptr, sz);
    sph_keccak512_close(&ctx_keccak, (void*)(hash[2]));

    sph_whirlpool_init(&ctx_whirlpool);
    // ZWHIRLPOOL;
    sph_whirlpool (&ctx_whirlpool, ptr, sz);
    sph_whirlpool_close(&ctx_whirlpool, (void*)(hash[3]));
    
    sph_haval256_5_init(&ctx_haval);
    // ZHAVAL;
    sph_haval256_5 (&ctx_haval, ptr, sz);
    sph_haval256_5_close(&ctx_haval, (void*)(hash[4]));

    sph_tiger_init(&ctx_tiger);
    // ZTIGER;
    sph_tiger (&ctx_tiger, ptr, sz);
    sph_tiger_close(&ctx_tiger, (void*)(hash[5]));

#if 1
    sph_ripemd160_init(&ctx_ripemd);
    // ZRIPEMD;
    sph_ripemd160 (&ctx_ripemd, ptr, sz);
    sph_ripemd160_close(&ctx_ripemd, (void*)(hash[6]));

    //printf("%s\n", hash[6].GetHex().c_str());
#endif
    mpz_t bns[7];

    //Take care of zeros and load gmp
    for(int i=0; i < 7; i++){
        set_one_if_zero(hash[i]);
        mpz_init(bns[i]);
        mpz_set_uint512(bns[i],hash[i]);
    }
 
    mpz_t product;
    mpz_init(product);
    mpz_set_ui(product,1);
    for(int i=0; i < 7; i++){
        mpz_mul(product,product,bns[i]);
    }

    int bytes = mpz_sizeinbase(product, 256);
    char *data = (char*)malloc(bytes);
    mpz_export((void *)data, NULL, -1, 1, 0, 0, product);

    //Free the memory
    for(int i=0; i < 7; i++){
        mpz_clear(bns[i]);
    }
    mpz_clear(product);

    sph_sha256_init(&ctx_sha256);
    // ZSHA256;
    sph_sha256 (&ctx_sha256, data,bytes);
    sph_sha256_close(&ctx_sha256, (void*)(finalhash));

    free(data);
}

static bool fulltest_m7hash(const uint32_t *hash32, const uint32_t *target32)
{
    int i;
    bool rc = true;

    const unsigned char *hash = (const unsigned char *)hash32;
    const unsigned char *target = (const unsigned char *)target32;
    for (i = 31; i >= 0; i--) {
        if (hash[i] != target[i]) {
            rc = hash[i] < target[i];
            break;
        }
    }

    return rc;
}

int scanhash_m7hash_old(int thr_id, uint32_t *pdata, const uint32_t *ptarget,
    uint64_t max_nonce, unsigned long *hashes_done)
{
    uint32_t data[32] __attribute__((aligned(128)));
    uint32_t hash[8] __attribute__((aligned(32)));
    uint32_t n = pdata[29] - 1;
    const uint32_t first_nonce = pdata[29];
    char data_str[245], hash_str[65], target_str[65];
    
    memcpy(data, pdata, 122);

    do {
        data[29] = ++n;

        m7hash_old((const char *)hash, (const unsigned char *)data, 122);

        int rc = fulltest_m7hash(hash, ptarget);
        if (rc) {
            if (opt_debug) {
                bin2hex(hash_str, (unsigned char *)hash, 32);
                bin2hex(target_str, (unsigned char *)ptarget, 32);
                bin2hex(data_str, (unsigned char *)data, 122);
                applog(LOG_DEBUG, "DEBUG: [%d thread] Found share!\ndata   %s\nhash   %s\ntarget %s", thr_id, 
                    data_str,
                    hash_str,
                    target_str);
            }

            pdata[29] = data[29];
            *hashes_done = n - first_nonce + 1;
            return 1;
        }
    } while (n < max_nonce && !work_restart[thr_id].restart);

    *hashes_done = n - first_nonce + 1;
    pdata[29] = n;

    return 0;
}

int scanhash_m7hash(int thr_id, uint32_t *pdata, const uint32_t *ptarget,
    uint64_t max_nonce, unsigned long *hashes_done)
{
    uint32_t data[32] __attribute__((aligned(128)));
    uint32_t *data_p64 = data + 16;
    uint32_t hash[8] __attribute__((aligned(32)));
    uint8_t bhash[7][64] __attribute__((aligned(32)));
    uint32_t hashtest[8] __attribute__((aligned(32)));
    uint32_t n = pdata[29] - 1;
    const uint32_t first_nonce = pdata[29];
    char data_str[245], hash_str[65], target_str[65];
    uint8_t *bdata = 0;
    mpz_t bns[7];
    mpz_t product;
    int rc = 0;

    for(int i=0; i < 7; i++){
        mpz_init(bns[i]);
    }
    mpz_init(product);

    memcpy(data, pdata, 122);

    sph_sha256_context       ctx_final_sha256;

    sph_sha256_context       ctx_sha256;
    sph_sha512_context       ctx_sha512;
    sph_keccak512_context    ctx_keccak;
    sph_whirlpool_context    ctx_whirlpool;
    sph_haval256_5_context   ctx_haval;
    sph_tiger_context        ctx_tiger;
    sph_ripemd160_context    ctx_ripemd;

    sph_sha256_init(&ctx_sha256);
    sph_sha256 (&ctx_sha256, data, 64);
    
    sph_sha512_init(&ctx_sha512);
    sph_sha512 (&ctx_sha512, data, 64);
    
    sph_keccak512_init(&ctx_keccak);
    sph_keccak512 (&ctx_keccak, data, 64);

    sph_whirlpool_init(&ctx_whirlpool);
    sph_whirlpool (&ctx_whirlpool, data, 64);
    
    sph_haval256_5_init(&ctx_haval);
    sph_haval256_5 (&ctx_haval, data, 64);

    sph_tiger_init(&ctx_tiger);
    sph_tiger (&ctx_tiger, data, 64);

    sph_ripemd160_init(&ctx_ripemd);
    sph_ripemd160 (&ctx_ripemd, data, 64);

    sph_sha256_context       ctx2_sha256;
    sph_sha512_context       ctx2_sha512;
    sph_keccak512_context    ctx2_keccak;
    sph_whirlpool_context    ctx2_whirlpool;
    sph_haval256_5_context   ctx2_haval;
    sph_tiger_context        ctx2_tiger;
    sph_ripemd160_context    ctx2_ripemd;

    do {
        data[29] = ++n;

        memset(bhash, 0, 7 * 64);

        ctx2_sha256 = ctx_sha256;
        sph_sha256 (&ctx2_sha256, data_p64, 58);
        sph_sha256_close(&ctx2_sha256, (void*)(bhash[0]));

        ctx2_sha512 = ctx_sha512;
        sph_sha512 (&ctx2_sha512, data_p64, 58);
        sph_sha512_close(&ctx2_sha512, (void*)(bhash[1]));
        
        ctx2_keccak = ctx_keccak;
        sph_keccak512 (&ctx2_keccak, data_p64, 58);
        sph_keccak512_close(&ctx2_keccak, (void*)(bhash[2]));

        ctx2_whirlpool = ctx_whirlpool;
        sph_whirlpool (&ctx2_whirlpool, data_p64, 58);
        sph_whirlpool_close(&ctx2_whirlpool, (void*)(bhash[3]));
        
        ctx2_haval = ctx_haval;
        sph_haval256_5 (&ctx2_haval, data_p64, 58);
        sph_haval256_5_close(&ctx2_haval, (void*)(bhash[4]));

        ctx2_tiger = ctx_tiger;
        sph_tiger (&ctx2_tiger, data_p64, 58);
        sph_tiger_close(&ctx2_tiger, (void*)(bhash[5]));

        ctx2_ripemd = ctx_ripemd;
        sph_ripemd160 (&ctx2_ripemd, data_p64, 58);
        sph_ripemd160_close(&ctx2_ripemd, (void*)(bhash[6]));

        for(int i=0; i < 7; i++){
            set_one_if_zero(bhash[i]);
            mpz_set_uint512(bns[i],bhash[i]);
        }
        
        mpz_set_ui(product,1);
        for(int i=0; i < 7; i++){
            mpz_mul(product,product,bns[i]);
        }

        int bytes = mpz_sizeinbase(product, 256);
        bdata = (uint8_t *)realloc(bdata, bytes);
        mpz_export((void *)bdata, NULL, -1, 1, 0, 0, product);

        sph_sha256_init(&ctx_final_sha256);
        sph_sha256 (&ctx_final_sha256, bdata, bytes);
        sph_sha256_close(&ctx_final_sha256, (void*)(hash));

        rc = fulltest_m7hash(hash, ptarget);
        if (rc) {
            if (opt_debug) {
                bin2hex(hash_str, (unsigned char *)hash, 32);
                bin2hex(target_str, (unsigned char *)ptarget, 32);
                bin2hex(data_str, (unsigned char *)data, 122);
                applog(LOG_DEBUG, "DEBUG: [%d thread] Found share!\ndata   %s\nhash   %s\ntarget %s", thr_id, 
                    data_str,
                    hash_str,
                    target_str);
            }

            pdata[29] = data[29];

            goto out;
        }
    } while (n < max_nonce && !work_restart[thr_id].restart);

    pdata[29] = n;

out:
    for(int i=0; i < 7; i++){
        mpz_clear(bns[i]);
    }
    mpz_clear(product);

    *hashes_done = n - first_nonce + 1;
    free(bdata);
    return rc;
}



