/**
 * sign.h — CSV 文件 HMAC-SHA256 签名
 */
#ifndef SIGN_H
#define SIGN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 对文件追加 HMAC 签名行 "# SIG:HMAC-SHA256:xxxx..."
 * 签名覆盖文件正文内容（不含已有签名行）。
 * 返回 0 成功, -1 失败。
 */
int sign_file(const char *path);

/**
 * 验证文件的签名是否匹配。
 * 返回 0=签名有效, -1=IO错误, -2=无签名, -3=签名不匹配。
 */
int verify_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* SIGN_H */
