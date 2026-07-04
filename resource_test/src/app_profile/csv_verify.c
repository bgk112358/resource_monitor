/**
 * csv_verify.c — CSV 签名验证工具
 *
 * 验证 app_profile 生成的 CSV 文件的 HMAC 签名。
 *
 * 用法:
 *   ./csv_verify <file_or_dir>
 *   ./csv_verify /tmp/test_xxx/cpu.csv      # 验证单个文件
 *   ./csv_verify /tmp/test_xxx/              # 验证目录下所有 CSV
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "sign.h"

static int verify_and_print(const char *path) {
    int ret = verify_file(path);
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    switch (ret) {
    case 0:  printf("  ✅ %-20s 签名有效\n", name); break;
    case -2: printf("  ⚠  %-20s 无签名 (旧版本生成)\n", name); break;
    case -3: printf("  ❌ %-20s 签名不匹配 — 数据可能被篡改!\n", name); break;
    default: printf("  ❌ %-20s 读取失败\n", name); break;
    }
    return ret;
}

static int is_csv(const char *name) {
    const char *ext = strrchr(name, '.');
    return ext && strcmp(ext, ".csv") == 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <file_or_dir>\n", argv[0]);
        fprintf(stderr, "示例: %s /tmp/test/cpu.csv\n", argv[0]);
        fprintf(stderr, "      %s /tmp/test/\n", argv[0]);
        return 1;
    }

    struct stat st;
    if (stat(argv[1], &st) != 0) {
        fprintf(stderr, "❌ 路径不存在: %s\n", argv[1]);
        return 1;
    }

    int fail_count = 0;
    int total = 0;

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(argv[1]);
        if (!d) { perror("opendir"); return 1; }

        printf("📋 验证目录: %s\n", argv[1]);
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (is_csv(de->d_name)) {
                char path[1024];
                snprintf(path, sizeof(path), "%s/%s", argv[1], de->d_name);
                int r = verify_and_print(path);
                if (r != 0 && r != -2) fail_count++;
                total++;
            }
        }
        closedir(d);
    } else {
        printf("📋 验证文件: %s\n", argv[1]);
        int r = verify_and_print(argv[1]);
        if (r != 0 && r != -2) fail_count++;
        total++;
    }

    printf("\n");
    if (fail_count == 0)
        printf("✅ 全部通过 (%d/%d)\n", total - fail_count, total);
    else
        printf("❌ %d/%d 文件验证失败\n", fail_count, total);

    return fail_count > 0 ? 1 : 0;
}
