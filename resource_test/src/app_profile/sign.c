/**
 * sign.c — CSV 文件 HMAC-SHA256 签名与验证
 *
 * 依赖: OpenSSL libcrypto (链接 -lcrypto)
 * 密钥: 编译期宏 SIGN_KEY, 默认 32 字节 hex 字符串
 */
#include "sign.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#ifndef SIGN_KEY
#define SIGN_KEY "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#endif

/* ── 计算文件 HMAC-SHA256 (跳过已有签名行) ──────── */
static int compute_hmac(const char *path, unsigned char *out, unsigned int *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    /* 先读整个文件到内存 (CSV 文件 ≤ 几 KB) */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 0) { fclose(f); return -1; }
    rewind(f);

    char *content = (char *)malloc((size_t)fsize + 1);
    if (!content) { fclose(f); return -1; }
    size_t nread = fread(content, 1, (size_t)fsize, f);
    fclose(f);
    content[nread] = '\0';

    /* 截断: 找到第一个 "# SIG:" 行, 只签名前面的内容 */
    char *sig_start = strstr(content, "\n# SIG:HMAC-SHA256:");
    size_t signable_len = sig_start ? (size_t)(sig_start - content) + 1 : nread;

    HMAC(EVP_sha256(),
         SIGN_KEY, strlen(SIGN_KEY),
         (unsigned char *)content, signable_len,
         out, out_len);

    free(content);
    return 0;
}

/* ── 签名 ──────────────────────────────────────── */
int sign_file(const char *path) {
    /* 如果文件已签名，先移除旧签名行 */
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 0) { fclose(f); return -1; }
    rewind(f);

    char *content = (char *)malloc((size_t)fsize + 1);
    if (!content) { fclose(f); return -1; }
    fread(content, 1, (size_t)fsize, f);
    content[fsize] = '\0';
    fclose(f);

    /* 移除已有签名行 */
    char *sig = strstr(content, "\n# SIG:HMAC-SHA256:");
    if (sig) *sig = '\0';

    /* 写回 (不加签名, 先写干净版) */
    f = fopen(path, "w");
    if (!f) { free(content); return -1; }
    fputs(content, f);
    fclose(f);
    free(content);

    /* 计算 HMAC */
    unsigned char hmac[EVP_MAX_MD_SIZE];
    unsigned int hmac_len;
    if (compute_hmac(path, hmac, &hmac_len) != 0) return -1;

    /* 追加签名行 */
    f = fopen(path, "a");
    if (!f) return -1;
    fprintf(f, "# SIG:HMAC-SHA256:");
    for (unsigned int i = 0; i < hmac_len; i++)
        fprintf(f, "%02x", hmac[i]);
    fprintf(f, "\n");
    fclose(f);
    return 0;
}

/* ── 验证 ──────────────────────────────────────── */
int verify_file(const char *path) {
    /* 读取签名行 */
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024], last_sig[256] = "";
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "# SIG:HMAC-SHA256:", 18) == 0) {
            strcpy(last_sig, line + 18);
            last_sig[strcspn(last_sig, "\r\n")] = '\0';
        }
    }
    fclose(f);
    if (last_sig[0] == '\0') return -2;

    /* 重算 */
    unsigned char hmac[EVP_MAX_MD_SIZE];
    unsigned int hmac_len;
    if (compute_hmac(path, hmac, &hmac_len) != 0) return -1;

    char computed[256];
    for (unsigned int i = 0; i < hmac_len; i++)
        sprintf(computed + i * 2, "%02x", hmac[i]);

    return (strcmp(computed, last_sig) == 0) ? 0 : -3;
}
