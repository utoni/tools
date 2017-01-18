// AES Implementation by X-N2O
// Started:  15:41:35 - 18 Nov 2009
// Finished: 20:03:59 - 21 Nov 2009
// Logarithm, S-Box, and RCON tables are not hardcoded
// Instead they are generated when the program starts
// All of the code below is based from the AES specification
// You can find it at http://csrc.nist.gov/publications/fips/fips197/fips-197.pdf
// You may use this code as you wish, but do not remove this comment
// This is only a proof of concept, and should not be considered as the most efficient implementation

#include <unistd.h> 
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>

 
#define AES_RPOL    0x011b // reduction polynomial (x^8 + x^4 + x^3 + x + 1)
#define AES_GEN     0x03   // gf(2^8) generator  (x + 1)
#define AES_SBOX_CC 0x63   // S-Box C constant
 
#define KEY_128     (128/8)
#define KEY_192     (192/8)
#define KEY_256     (256/8)

#define aes_mul(a, b) ((a)&&(b)?g_aes_ilogt[(g_aes_logt[(a)]+g_aes_logt[(b)])%0xff]:0)
#define aes_inv(a)    ((a)?g_aes_ilogt[0xff-g_aes_logt[(a)]]:0)
 
unsigned char g_aes_logt[256], g_aes_ilogt[256];
unsigned char g_aes_sbox[256], g_aes_isbox[256];
 
typedef struct {
    unsigned char state[4][4];
    int kcol;
    size_t rounds;
    unsigned long keysched[0];
} aes_ctx_t;
 
void aes_init();
aes_ctx_t *aes_alloc_ctx(unsigned char *key, size_t keyLen);
inline unsigned long aes_subword(unsigned long w);
inline unsigned long aes_rotword(unsigned long w);
void aes_keyexpansion(aes_ctx_t *ctx);
 
inline unsigned char aes_mul_manual(unsigned char a, unsigned char b); // use aes_mul instead
 
void aes_subbytes(aes_ctx_t *ctx);
void aes_shiftrows(aes_ctx_t *ctx);
void aes_mixcolumns(aes_ctx_t *ctx);
void aes_addroundkey(aes_ctx_t *ctx, int round);
void aes_encrypt(aes_ctx_t *ctx, unsigned char input[16], unsigned char output[16]);
 
void aes_invsubbytes(aes_ctx_t *ctx);
void aes_invshiftrows(aes_ctx_t *ctx);
void aes_invmixcolumns(aes_ctx_t *ctx);
void aes_decrypt(aes_ctx_t *ctx, unsigned char input[16], unsigned char output[16]);
 
void aes_free_ctx(aes_ctx_t *ctx);


char* aes_crypt_s(aes_ctx_t* ctx, char* input, size_t siz, size_t* newsiz, bool doEncrypt)
{
    size_t bsiz;
    if (doEncrypt) {
        bsiz = siz + (16 - siz%16);
    } else {
        bsiz = siz;
    }
    char* output = calloc(1, bsiz+1);
    unsigned char inbuf[16];
    unsigned char outbuf[16];

    size_t i = 0;
    for (i = 0; i < bsiz; i=i+16) {
        size_t maxsiz;
        if (doEncrypt && bsiz-i <= 16) {
            maxsiz = siz%16;
        } else maxsiz = 16;
        memset(&inbuf[0], '\0', 16);
        memset(&outbuf[0], '\0', 16);
        memcpy( (void*)&inbuf[0], (void*)(input+i), maxsiz);
        if (doEncrypt) {
            aes_encrypt(ctx, inbuf, outbuf);
        } else {
            aes_decrypt(ctx, inbuf, outbuf);
        }
        memcpy( (void*)(output+i), (void*)&outbuf[0], 16);
    }
    if (newsiz)
        *newsiz = bsiz;
    return output;
}

/*
#ifdef __i386__
static uint64_t __rnd(void)
{
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}
#else
static uint64_t __rnd(void)
{
    return 0;
}
#endif

static void __pseudoRandom(unsigned char* buf, size_t siz)
{
    time_t seed = time(NULL) + __rnd();

    for (size_t i = 0; i < siz; ++i) {
        buf[i] = (unsigned char)((seed * time(NULL)) % 256);
        seed++;
    }
}

void aes_randomkey(unsigned char* keyout, size_t keyLen)
{
    __pseudoRandom(keyout, keyLen);
}
*/

void init_aes()
{
    int i;
    unsigned char gen;

    memset(&g_aes_logt[0], '\0', sizeof(g_aes_logt));
    memset(&g_aes_ilogt[0], '\0', sizeof(g_aes_ilogt));
    memset(&g_aes_sbox[0], '\0', sizeof(g_aes_sbox));
    memset(&g_aes_isbox[0], '\0', sizeof(g_aes_isbox));
 
    // build logarithm table and it's inverse
    gen = 1;
    for(i = 0; i < 0xff; i++) {
        g_aes_logt[gen]  = i;
        g_aes_ilogt[i]   = gen;
        gen = aes_mul_manual(gen, AES_GEN);
    }
 
    // build S-Box and it's inverse
    for(i = 0; i <= 0xff; i++) {
        char bi;
        unsigned char inv = aes_inv(i);
 
        g_aes_sbox[i] = 0;
        for(bi = 0; bi < 8; bi++) {
            // based on transformation 5.1
            // could also be done with a loop based on the matrix
            g_aes_sbox[i] |= ((inv & (1<<bi)?1:0)
                        ^ (inv & (1 << ((bi+4) & 7))?1:0)
                        ^ (inv & (1 << ((bi+5) & 7))?1:0)
                        ^ (inv & (1 << ((bi+6) & 7))?1:0)
                        ^ (inv & (1 << ((bi+7) & 7))?1:0)
                        ^ (AES_SBOX_CC & (1 << bi)?1:0)
            ) << bi;
        }
        g_aes_isbox[g_aes_sbox[i]] = i;
    }
    // warning: quickhack
    g_aes_sbox[1] = 0x7c;
    g_aes_isbox[0x7c] = 1;
    g_aes_isbox[0x63] = 0;
}
 
aes_ctx_t *aes_alloc_ctx(unsigned char *key, size_t keyLen)
{
    aes_ctx_t *ctx;
    size_t rounds;
    size_t ks_size;
 
    switch(keyLen) {
        case 16: // 128-bit key
            rounds = 10;
            break;
 
        case 24: // 192-bit key
            rounds = 12;
            break;
 
        case 32: // 256-bit key
            rounds = 14;
            break;
 
        default:
            return NULL;
    }
 
    ks_size = 4*(rounds+1)*sizeof(unsigned long);
    ctx = calloc(1, sizeof(aes_ctx_t)+ks_size);
    if(ctx) {
        ctx->rounds = rounds;
        ctx->kcol = keyLen/4;
        memcpy(ctx->keysched, key, keyLen);
        ctx->keysched[43] = 0;
        aes_keyexpansion(ctx);
    }
 
    return ctx;
}
 
inline unsigned long aes_subword(unsigned long w)
{
    return g_aes_sbox[w & 0x000000ff] |
        (g_aes_sbox[(w & 0x0000ff00) >> 8] << 8) |
        (g_aes_sbox[(w & 0x00ff0000) >> 16] << 16) |
        (g_aes_sbox[(w & 0xff000000) >> 24] << 24);
}
 
inline unsigned long aes_rotword(unsigned long w)
{
    // May seem a bit different from the spec
    // It was changed because unsigned long is represented with little-endian convention on x86
    // Should not depend on architecture, but this is only a POC
    return ((w & 0x000000ff) << 24) |
        ((w & 0x0000ff00) >> 8) |
        ((w & 0x00ff0000) >> 8) |
        ((w & 0xff000000) >> 8);
}
 
void aes_keyexpansion(aes_ctx_t *ctx)
{
    unsigned long temp;
    unsigned long rcon;
    register int i;
 
    rcon = 0x00000001;
    for(i = ctx->kcol; i < (4*(ctx->rounds+1)); i++) {
        temp = ctx->keysched[i-1];
        if(!(i%ctx->kcol)) {
            temp = aes_subword(aes_rotword(temp)) ^ rcon;
            rcon = aes_mul(rcon, 2);
        } else if(ctx->kcol > 6 && i%ctx->kcol == 4)
            temp = aes_subword(temp);
        ctx->keysched[i] = ctx->keysched[i-ctx->kcol] ^ temp;
    }
}
 
inline unsigned char aes_mul_manual(unsigned char a, unsigned char b)
{
    register unsigned short ac;
    register unsigned char ret;
 
    ac = a;
    ret = 0;
    while(b) {
        if(b & 0x01)
            ret ^= ac;
        ac <<= 1;
        b >>= 1;
        if(ac & 0x0100)
            ac ^= AES_RPOL;
    }
 
    return ret;
}
 
void aes_subbytes(aes_ctx_t *ctx)
{
    int i;
 
    for(i = 0; i < 16; i++) {
        int x, y;
 
        x = i & 0x03;
        y = i >> 2;
        ctx->state[x][y] = g_aes_sbox[ctx->state[x][y]];
    }
}
 
void aes_shiftrows(aes_ctx_t *ctx)
{
    unsigned char nstate[4][4];
    int i;
 
    for(i = 0; i < 16; i++) {
        int x, y;
 
        x = i & 0x03;
        y = i >> 2;
        nstate[x][y] = ctx->state[x][(y+x) & 0x03];
    }
 
    memcpy(ctx->state, nstate, sizeof(ctx->state));
}
 
void aes_mixcolumns(aes_ctx_t *ctx)
{
    unsigned char nstate[4][4];
    int i;
     
    for(i = 0; i < 4; i++) {
        nstate[0][i] = aes_mul(0x02, ctx->state[0][i]) ^
                aes_mul(0x03, ctx->state[1][i]) ^
                ctx->state[2][i] ^
                ctx->state[3][i];
        nstate[1][i] = ctx->state[0][i] ^
                aes_mul(0x02, ctx->state[1][i]) ^
                aes_mul(0x03, ctx->state[2][i]) ^
                ctx->state[3][i];
        nstate[2][i] = ctx->state[0][i] ^
                ctx->state[1][i] ^
                aes_mul(0x02, ctx->state[2][i]) ^
                aes_mul(0x03, ctx->state[3][i]);
        nstate[3][i] = aes_mul(0x03, ctx->state[0][i]) ^
                ctx->state[1][i] ^
                ctx->state[2][i] ^
                aes_mul(0x02, ctx->state[3][i]);
    }
 
    memcpy(ctx->state, nstate, sizeof(ctx->state));
}
 
void aes_addroundkey(aes_ctx_t *ctx, int round)
{
    int i;
 
    for(i = 0; i < 16; i++) {
        int x, y;
 
        x = i & 0x03;
        y = i >> 2;
        ctx->state[x][y] = ctx->state[x][y] ^
            ((ctx->keysched[round*4+y] & (0xff << (x*8))) >> (x*8));
    }
}
 
void aes_encrypt(aes_ctx_t *ctx, unsigned char input[16], unsigned char output[16])
{
    int i;
 
    // copy input to state
    for(i = 0; i < 16; i++)
        ctx->state[i & 0x03][i >> 2] = input[i];
 
    aes_addroundkey(ctx, 0);
 
    for(i = 1; i < ctx->rounds; i++) {
        aes_subbytes(ctx);
        aes_shiftrows(ctx);
        aes_mixcolumns(ctx);
        aes_addroundkey(ctx, i);
    }
 
    aes_subbytes(ctx);
    aes_shiftrows(ctx);
    aes_addroundkey(ctx, ctx->rounds);
 
    // copy state to output
    for(i = 0; i < 16; i++)
        output[i] = ctx->state[i & 0x03][i >> 2];
}
 
void aes_invshiftrows(aes_ctx_t *ctx)
{
    unsigned char nstate[4][4];
    int i;
 
    for(i = 0; i < 16; i++) {
        int x, y;
 
        x = i & 0x03;
        y = i >> 2;
        nstate[x][(y+x) & 0x03] = ctx->state[x][y];
    }
 
    memcpy(ctx->state, nstate, sizeof(ctx->state));
}
 
void aes_invsubbytes(aes_ctx_t *ctx)
{
    int i;
 
    for(i = 0; i < 16; i++) {
        int x, y;
 
        x = i & 0x03;
        y = i >> 2;
        ctx->state[x][y] = g_aes_isbox[ctx->state[x][y]];
    }
}
 
void aes_invmixcolumns(aes_ctx_t *ctx)
{
    unsigned char nstate[4][4];
    int i;

    memset(&nstate[0][0], '\0', sizeof(unsigned char)*16);
    for(i = 0; i < 4; i++) {
        nstate[0][i] = aes_mul(0x0e, ctx->state[0][i]) ^
                aes_mul(0x0b, ctx->state[1][i]) ^
                aes_mul(0x0d, ctx->state[2][i]) ^
                aes_mul(0x09, ctx->state[3][i]);
        nstate[1][i] = aes_mul(0x09, ctx->state[0][i]) ^
                aes_mul(0x0e, ctx->state[1][i]) ^
                aes_mul(0x0b, ctx->state[2][i]) ^
                aes_mul(0x0d, ctx->state[3][i]);
        nstate[2][i] = aes_mul(0x0d, ctx->state[0][i]) ^
                aes_mul(0x09, ctx->state[1][i]) ^
                aes_mul(0x0e, ctx->state[2][i]) ^
                aes_mul(0x0b, ctx->state[3][i]);
        nstate[3][i] = aes_mul(0x0b, ctx->state[0][i]) ^
                aes_mul(0x0d, ctx->state[1][i]) ^
                aes_mul(0x09, ctx->state[2][i]) ^
                aes_mul(0x0e, ctx->state[3][i]);
    }
 
    memcpy(ctx->state, nstate, sizeof(ctx->state));
}
 
void aes_decrypt(aes_ctx_t *ctx, unsigned char input[16], unsigned char output[16])
{
    int i;
 
    // copy input to state
    for(i = 0; i < 16; i++)
        ctx->state[i & 0x03][i >> 2] = input[i];
 
    aes_addroundkey(ctx, ctx->rounds);
    for(i = ctx->rounds-1; i >= 1; i--) {
        aes_invshiftrows(ctx);
        aes_invsubbytes(ctx);
        aes_addroundkey(ctx, i);
        aes_invmixcolumns(ctx);
    }
 
    aes_invshiftrows(ctx);
    aes_invsubbytes(ctx);
    aes_addroundkey(ctx, 0);
 
    // copy state to output
    for(i = 0; i < 16; i++)
        output[i] = ctx->state[i & 0x03][i >> 2];
}
 
void aes_free_ctx(aes_ctx_t *ctx)
{
    free(ctx);
}


static void print_usage_and_exit(char* arg0)
{
    fprintf(stderr, "usage %s [options]\n\n%s", (arg0 != NULL ? arg0 : ""),
        "where [options] can be:\n"
        "\t-s\tkeysize (128/192/256)\n"
        "\t-k\tkey with keysize length\n"
        "\t-m\tmessage to (en|de)crypt\n"
        "\t-e\tencrypt\n"
        "\t-d\tdecrypt\n"
        "\t-c\tC-Str (in|out)put\n"
        "\t-q\tquiet mode - print only (en|de)crypted chars\n"
        );
    exit(EXIT_FAILURE);
}

#define PRINT_BYTES(bPtr, siz, offset, doCStr) { int _bPtr_idx; if (doCStr) printf("\""); for (_bPtr_idx = offset; _bPtr_idx < offset+siz; _bPtr_idx++) { printf("%s%02X%s", (doCStr ? "\\x" : ""), (unsigned char)bPtr[_bPtr_idx], (doCStr ? "" : " ")); } if (doCStr) printf("\""); printf("\n"); }
int main(int argc, char *argv[])
{
    bool doEncrypt = false;
    bool doDecrypt = false;
    bool doCStrOutput = false;
    bool quiet = false;
    int opt;
    int keysiz = KEY_256;
    char *key = NULL;
    char *msg = NULL;

    if (argc == 0)
        exit(1);
    if (argc == 1)
        print_usage_and_exit(argv[0]);

    while ((opt = getopt(argc, argv, "s:k:m:edcq")) != -1 ) {
        switch (opt) {
        case 's': {
            unsigned long int ksiz = strtoul(optarg, NULL, 10);
            if (errno == EINVAL || errno == ERANGE) {
                fprintf(stderr, "%s: keysiz(`-s`) invalid input (numbers only)\n", argv[0]);
                return 1;
            } else {
                switch (ksiz) {
                case 128: keysiz = KEY_128; break;
                case 192: keysiz = KEY_192; break;
                case 256: keysiz = KEY_256; break;
                default: fprintf(stderr, "%s: keysiz(`-s`) invalid number (valid numbers: 128/192/256)\n", argv[0]); return 1;
                }
            }
            break;
        }
        case 'k':
            key = strdup(optarg);
            if (strlen(key) != keysiz) {
                 fprintf(stderr, "%s: key(`-k`) does not match keysiz(`-s`) %d\n", argv[0], keysiz);
            }
            break;
        case 'm':
            msg = strdup(optarg);
            break;
        case 'e':
            doEncrypt = true;
            break;
        case 'd':
            doDecrypt = true;
            break;
        case 'c':
            doCStrOutput = true;
            break;
        case 'q':
            quiet = true;
            break;
        }
    }

    if (!key || !msg) {
        fprintf(stderr, "%s: missing key or message\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (!doEncrypt && !doDecrypt) {
        doEncrypt = true;
        doDecrypt = true;
    }

    aes_ctx_t *ctx;

    init_aes();
    ctx = aes_alloc_ctx((unsigned char*)key, keysiz);
    if(!ctx) {
        perror("aes_alloc_ctx");
        return EXIT_FAILURE;
    }

    size_t cipher_siz = strlen(msg);
    char *cipher_msg = msg;
    if (doEncrypt) {
        if (!quiet) printf("Encrypted[HEX]..: ");
        cipher_msg = aes_crypt_s(ctx, msg, strlen(msg), &cipher_siz, true);
        if (!cipher_msg || cipher_siz == 0) {
            fprintf(stderr, "%s: aes encryption failed\n", argv[0]);
            return EXIT_FAILURE;
        }
        PRINT_BYTES(cipher_msg, cipher_siz, 0, doCStrOutput);
    }

    size_t plain_siz = 0;
    char *plain_msg = cipher_msg;
    if (doDecrypt) {
        if (!quiet) printf("Decrypted[HEX]..: ");
        plain_msg = aes_crypt_s(ctx, cipher_msg, cipher_siz, &plain_siz, false);
        if (!plain_msg || plain_siz == 0) {
            fprintf(stderr, "%s: aes decryption failed\n", argv[0]);
            return EXIT_FAILURE;
        }
        PRINT_BYTES(plain_msg, plain_siz, 0, doCStrOutput);
    }

    if (doEncrypt && doDecrypt) {
        if (strlen(msg) != strlen(plain_msg) || strcmp(msg, plain_msg) != 0) {
            fprintf(stderr, "%s: message differs from original - (en|de)cryption may failed\n", argv[0]);
            return EXIT_FAILURE;
        }
        if (!quiet) printf("Decrypted[ASCII]: ");
        puts(plain_msg);
    }

    free(key);
    free(msg);
    if (doEncrypt)
        free(cipher_msg);
    if (doDecrypt)
        free(plain_msg);
    aes_free_ctx(ctx);
    return EXIT_SUCCESS;
}

