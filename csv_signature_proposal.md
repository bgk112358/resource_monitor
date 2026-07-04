# CSV 防伪签名方案 v2

> 最简方案：文件尾行 HMAC 签名，不增加数据列，不依赖 HSM。

---

## 原理

```
原始 CSV:                        签名后 CSV:
──────────                       ──────────
sec,cpu_percent                  sec,cpu_percent
1,2.0                            1,2.0
2,3.0                            2,3.0
3,2.0                            3,2.0
                                 # SIG:HMAC-SHA256:d4e5f6a7b8c9...
```

对 CSV 正文（不含签名行本身）计算 HMAC-SHA256，密钥为编译期预置的 32 字节随机字符串。

---

## 签名行格式

```
# SIG:HMAC-SHA256:<64个十六进制字符>
```

- 以 `#` 开头 → Excel/WPS 自动视为注释，不影响数据显示
- 程序验证时读取最后一行的 `# SIG:` 前缀并提取哈希

---

## 密钥管理

| 项目 | 方式 |
|------|------|
| **密钥生成** | `openssl rand -hex 32` → 64 字符随机串 |
| **密钥存储** | 编译期宏 `-DSIGN_KEY="abc123..."` 嵌入二进制 |
| **密钥更新** | 重新编译即可 |
| **安全性** | `strings app_profile \| grep SIGN_KEY` 可提取 → 建议 strip + 混淆 |

> 如需更强保护：密钥分段异或存储，运行时组装。但本方案定位是**防意外/非专业篡改**，非防逆向工程。

---

## 实现（`sign.c`）

```c
/**
 * sign.c — CSV 文件 HMAC-SHA256 签名与验证
 *
 * 签名: sign_file("cpu.csv") → 追加 "# SIG:HMAC-SHA256:xxxx..."
 * 验证: verify_file("cpu.csv") → 返回 0=通过, -1=失败
 *
 * 依赖: OpenSSL libcrypto (TBox 已有)
 */

#include <openssl/hmac.h>
#include <string.h>
#include <stdio.h>

#define SIGN_KEY  "0123456789abcdef0123456789abcdef"  /* 32 字节 hex */

/* ── 计算文件 HMAC-SHA256 ──────────────────────── */
static int compute_hmac(const char *path, unsigned char *out, unsigned int *out_len) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    HMAC_CTX *ctx = HMAC_CTX_new();
    HMAC_Init_ex(ctx, SIGN_KEY, strlen(SIGN_KEY), EVP_sha256(), NULL);

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        /* 跳过签名行 */
        char *p = buf;
        for (size_t i = 0; i < n; i++) {
            if (buf[i] == '\n' && i + 5 < n && strncmp(buf + i + 1, "# SIG:", 6) == 0) {
                HMAC_Update(ctx, buf, i + 1);  /* 到签名行前为止 */
                goto done;
            }
        }
        HMAC_Update(ctx, buf, n);
    }
done:
    HMAC_Final(ctx, out, out_len);
    HMAC_CTX_free(ctx);
    fclose(f);
    return 0;
}

/* ── 签名 CSV 文件 ──────────────────────────────── */
int sign_file(const char *path) {
    unsigned char hmac[EVP_MAX_MD_SIZE];
    unsigned int len;
    if (compute_hmac(path, hmac, &len) != 0) return -1;

    FILE *f = fopen(path, "a");
    if (!f) return -1;

    fprintf(f, "# SIG:HMAC-SHA256:");
    for (unsigned int i = 0; i < len; i++) fprintf(f, "%02x", hmac[i]);
    fprintf(f, "\n");
    fclose(f);
    return 0;
}

/* ── 验证 CSV 文件 ──────────────────────────────── */
int verify_file(const char *path) {
    /* 1. 从文件读取已有的签名 */
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024], last_sig[256] = "";
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "# SIG:HMAC-SHA256:", 18) == 0)
            strcpy(last_sig, line + 18);
    }
    fclose(f);
    if (last_sig[0] == '\0') return -2;  /* 无签名 */

    /* 2. 重算 HMAC */
    unsigned char hmac[EVP_MAX_MD_SIZE];
    unsigned int len;
    if (compute_hmac(path, hmac, &len) != 0) return -1;

    /* 3. 比较 */
    char computed[256];
    for (unsigned int i = 0; i < len; i++)
        sprintf(computed + i * 2, "%02x", hmac[i]);

    return (strncmp(computed, last_sig, 64) == 0) ? 0 : -3;
}
```

---

## 集成到 app_profile

### 改动点

| 文件 | 改动 | 行数 |
|------|------|:---:|
| `report.c` | CSV 写入完成后调用 `sign_file(cpu.csv)` 等 4 次 | +2 行 |
| **新增** `sign.c` | 签名 + 验证实现 | +80 行 |
| `CMakeLists.txt` | 链接 `-lcrypto`（已有 OpenSSL） | +1 行 |
| 编译选项 | `-DSIGN_KEY=\"xxx\"` | — |

### 签名时机

```
app_profile 写完全部 CSV → fclose(cpu/mem/thr/io) → sign_file(每个) → report.txt
```

---

## 验证工具 `csv_verify`

```bash
# 验证单个文件
./csv_verify /tmp/test_xxx/cpu.csv
# → ✅ cpu.csv: 签名有效

# 验证整个目录
./csv_verify /tmp/test_xxx/
# → ✅ cpu.csv: 有效
# → ✅ mem.csv: 有效
# → ❌ io.csv: 签名不匹配 — 数据可能被篡改!
```

---

## 对比

| | v1 方案 (A+B) | **v2 方案 (本方案)** |
|--|:---:|:---:|
| 增加数据列 | `_chain` 列 | **无** |
| 依赖 HSM | 可选 | **不需要** |
| 检测整文件篡改 | ✅ | ✅ |
| 定位到具体行 | ✅ | ❌ |
| 实现代码量 | ~200 行 | **~80 行** |
| Excel 兼容 | 多一列 | **完全兼容** |
