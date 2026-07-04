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
    int quiet = 0;
    const char *target = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else {
            target = argv[i];
        }
    }

    if (!target) {
        fprintf(stderr, "用法: %s [-q] <file_or_dir>\n", argv[0]);
        fprintf(stderr, "  -q  安静模式, 只返回退出码:\n");
        fprintf(stderr, "       0=签名有效  1=签名无效  2=无签名  3=IO错误\n");
        return 3;
    }

    struct stat st;
    if (stat(target, &st) != 0) {
        if (!quiet) fprintf(stderr, "❌ 路径不存在: %s\n", target);
        return 3;
    }

    /* 安静模式: 验证单个文件, 返回退出码 */
    if (quiet && !S_ISDIR(st.st_mode)) {
        int r = verify_file(target);
        /* 映射 verify_file 返回值到退出码:
           0 (valid)  → 0
           -2 (无签名) → 2
           -3 (无效)  → 1
           -1 (IO错)  → 3 */
        if (r == 0)  return 0;
        if (r == -2) return 2;
        if (r == -3) return 1;
        return 3;
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
