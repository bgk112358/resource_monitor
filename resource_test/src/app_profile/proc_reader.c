/**
 * proc_reader.c — 进程基本信息读取
 *
 * 模块职责: PID 验证、进程名获取、子进程计数。
 * 接口: proc_validate / proc_get_name / proc_count_children
 */
#include "sampler.h"

volatile int g_running = 1;

/* 信号处理 */
void sig_handler(int sig) { (void)sig; g_running = 0; }

int proc_validate(pid_t pid) {
    /* stat /proc/<pid> 不依赖信号权限, 避免 EPERM 误判为"进程不存在" */
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "/proc/%d", pid);
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    /* /proc 未挂载时回退 kill */
    return (kill(pid, 0) == 0) ? 0 : -1;
}

int proc_get_name(pid_t pid, char *name, size_t size) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(name, size, f)) { fclose(f); return -1; }
    name[strcspn(name, "\n")] = 0;
    fclose(f);
    return 0;
}

int proc_count_children(pid_t pid) {
    char path[MAX_PATH], line[MAX_LINE];
    snprintf(path, sizeof(path), "/proc/%d/task/%d/children", pid, pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    fclose(f);
    if (line[0] == '\0' || line[0] == '\n') return 0;
    int n = 0;
    for (char *p = line; *p; p++) if (*p == ' ') n++;
    return n + 1;
}
