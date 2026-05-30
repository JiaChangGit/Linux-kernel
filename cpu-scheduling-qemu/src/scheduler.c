/*
 * CPU Scheduling Algorithm Simulator
 *
 * 支援演算法：
 *   - FCFS：First-Come First-Served
 *   - SJF：Shortest Job First，非搶佔式
 *   - SRTF：Shortest Remaining Time First，搶佔式 SJF
 *   - Priority Scheduling：非搶佔式，數字越小優先權越高
 *   - Round Robin：以 time quantum 控制每輪可執行時間
 *
 * 輸入：
 *   scheduler <algorithm> [time_quantum] < workload.txt
 *
 * 輸出：
 *   - 每個行程的 Start / Finish / Waiting / Turnaround
 *   - 平均 AWT / ATT / ART
 *   - BENCHMARK line，供 scripts/04_benchmark.sh 解析
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 固定容量讓資料結構容易閱讀；輸入超過上限時會明確報錯。 */
#define MAX_PROC 64
#define MAX_GANTT_SLOT (MAX_PROC * 200)
#define MAX_QUEUE_SLOT (MAX_PROC * 200)

typedef struct {
    int pid;
    int arrival;
    int burst;        /* 原始 CPU burst time，不會被演算法改掉。 */
    int remaining;    /* 搶佔式演算法用來記錄還需要多少 CPU 時間。 */
    int priority;     /* 數字越小代表優先權越高。 */

    /* 以下欄位在排程完成後填入。 */
    int start;
    int finish;
    int waiting;
    int turnaround;
    int response;
    int responded;    /* 保留欄位；目前主要以 start == -1 判斷是否首次執行。 */
} Process;

typedef struct {
    int pid;
    int start;
    int end;
} GanttSlot;

/* 這個模擬器一次只跑一個 workload，因此使用全域狀態簡化函式介面。 */
Process proc[MAX_PROC];
GanttSlot gantt[MAX_GANTT_SLOT];
int n_proc = 0;
int n_gantt = 0;

static void die_input(const char *message)
{
    fprintf(stderr, "Input error: %s\n", message);
    exit(1);
}

static void die_runtime(const char *message)
{
    fprintf(stderr, "Runtime error: %s\n", message);
    exit(1);
}

static int compare_int(int left, int right)
{
    return (left > right) - (left < right);
}

/*
 * 將 CPU 執行區間加入 Gantt Chart。
 * 如果同一個 PID 連續執行，直接延長上一段，避免 SRTF 每 tick 都印一格。
 */
void gantt_push(int pid, int t_start, int t_end)
{
    if (t_start >= t_end) {
        return;
    }

    if (n_gantt > 0 && gantt[n_gantt - 1].pid == pid) {
        gantt[n_gantt - 1].end = t_end;
        return;
    }

    if (n_gantt >= MAX_GANTT_SLOT) {
        die_runtime("Gantt chart slot capacity exceeded");
    }

    gantt[n_gantt].pid = pid;
    gantt[n_gantt].start = t_start;
    gantt[n_gantt].end = t_end;
    n_gantt++;
}

void compute_stats(void)
{
    for (int i = 0; i < n_proc; i++) {
        proc[i].turnaround = proc[i].finish - proc[i].arrival;
        proc[i].waiting = proc[i].turnaround - proc[i].burst;
        proc[i].response = proc[i].start - proc[i].arrival;
    }
}

void print_results(const char *algo_label)
{
    double sum_wt = 0, sum_tat = 0, sum_rt = 0;

    printf("\n=== %s ===\n", algo_label);
    printf("%-6s %-8s %-7s %-8s %-10s %-8s %-8s\n",
           "PID", "Arrival", "Burst", "Start", "Finish", "Wait", "TAT");
    printf("%-6s %-8s %-7s %-8s %-10s %-8s %-8s\n",
           "---", "-------", "-----", "-----", "------", "----", "---");

    for (int i = 0; i < n_proc; i++) {
        printf("%-6d %-8d %-7d %-8d %-10d %-8d %-8d\n",
               proc[i].pid,
               proc[i].arrival,
               proc[i].burst,
               proc[i].start,
               proc[i].finish,
               proc[i].waiting,
               proc[i].turnaround);
        sum_wt += proc[i].waiting;
        sum_tat += proc[i].turnaround;
        sum_rt += proc[i].response;
    }

    printf("\nAvg Waiting Time    : %.2f\n", sum_wt / n_proc);
    printf("Avg Turnaround Time : %.2f\n", sum_tat / n_proc);
    printf("Avg Response Time   : %.2f\n", sum_rt / n_proc);

    /*
     * 給 benchmark script 解析的固定格式。
     * 人類可讀表格可以調整，但這行若改格式，scripts/04_benchmark.sh 也要同步更新。
     */
    printf("BENCHMARK %s AWT=%.4f ATT=%.4f ART=%.4f\n",
           algo_label,
           sum_wt / n_proc,
           sum_tat / n_proc,
           sum_rt / n_proc);
}

void print_gantt(void)
{
    printf("\nGantt Chart:\n|");
    for (int i = 0; i < n_gantt; i++) {
        printf(" P%-2d |", gantt[i].pid);
    }

    printf("\n0");
    for (int i = 0; i < n_gantt; i++) {
        printf("     %d", gantt[i].end);
    }
    printf("\n");
}

/* qsort comparator：先比到達時間，再用 PID 讓排序結果穩定。 */
int cmp_arrival(const void *a, const void *b)
{
    const Process *pa = (const Process *)a;
    const Process *pb = (const Process *)b;

    int by_arrival = compare_int(pa->arrival, pb->arrival);
    if (by_arrival != 0) {
        return by_arrival;
    }
    return compare_int(pa->pid, pb->pid);
}

/*
 * 這兩個 comparator 保留為演算法比較用。
 * 目前 SJF/Priority 不能直接全域排序，因為它們每輪只能從已到達的 ready set 挑選。
 */
int cmp_burst(const void *a, const void *b)
{
    const Process *pa = (const Process *)a;
    const Process *pb = (const Process *)b;

    int by_burst = compare_int(pa->burst, pb->burst);
    if (by_burst != 0) {
        return by_burst;
    }
    int by_arrival = compare_int(pa->arrival, pb->arrival);
    if (by_arrival != 0) {
        return by_arrival;
    }
    return compare_int(pa->pid, pb->pid);
}

int cmp_priority(const void *a, const void *b)
{
    const Process *pa = (const Process *)a;
    const Process *pb = (const Process *)b;

    int by_priority = compare_int(pa->priority, pb->priority);
    if (by_priority != 0) {
        return by_priority;
    }
    int by_arrival = compare_int(pa->arrival, pb->arrival);
    if (by_arrival != 0) {
        return by_arrival;
    }
    return compare_int(pa->pid, pb->pid);
}

void sched_fcfs(void)
{
    qsort(proc, n_proc, sizeof(Process), cmp_arrival);

    int clock = 0;
    for (int i = 0; i < n_proc; i++) {
        if (clock < proc[i].arrival) {
            clock = proc[i].arrival;
        }
        proc[i].start = clock;
        proc[i].finish = clock + proc[i].burst;
        gantt_push(proc[i].pid, proc[i].start, proc[i].finish);
        clock = proc[i].finish;
    }

    compute_stats();
    print_results("FCFS");
    print_gantt();
}

void sched_sjf(void)
{
    int done[MAX_PROC] = {0};
    int clock = 0, completed = 0;

    while (completed < n_proc) {
        int sel = -1;

        /* 每輪只從已到達且尚未完成的行程中挑 burst 最短者。 */
        for (int i = 0; i < n_proc; i++) {
            if (!done[i] && proc[i].arrival <= clock) {
                if (sel == -1 ||
                    proc[i].burst < proc[sel].burst ||
                    (proc[i].burst == proc[sel].burst &&
                     (proc[i].arrival < proc[sel].arrival ||
                      (proc[i].arrival == proc[sel].arrival && proc[i].pid < proc[sel].pid)))) {
                    sel = i;
                }
            }
        }

        if (sel == -1) {
            int next = INT_MAX;
            for (int i = 0; i < n_proc; i++) {
                if (!done[i] && proc[i].arrival < next) {
                    next = proc[i].arrival;
                }
            }
            clock = next;
            continue;
        }

        proc[sel].start = clock;
        proc[sel].finish = clock + proc[sel].burst;
        gantt_push(proc[sel].pid, proc[sel].start, proc[sel].finish);
        clock = proc[sel].finish;
        done[sel] = 1;
        completed++;
    }

    compute_stats();
    print_results("SJF_NonPreemptive");
    print_gantt();
}

void sched_srtf(void)
{
    for (int i = 0; i < n_proc; i++) {
        proc[i].remaining = proc[i].burst;
        proc[i].start = -1;
    }

    int clock = 0, completed = 0;

    while (completed < n_proc) {
        int sel = -1;

        /* SRTF 是搶佔式：每個 tick 都重新找剩餘時間最短的 ready process。 */
        for (int i = 0; i < n_proc; i++) {
            if (proc[i].remaining > 0 && proc[i].arrival <= clock) {
                if (sel == -1 ||
                    proc[i].remaining < proc[sel].remaining ||
                    (proc[i].remaining == proc[sel].remaining &&
                     (proc[i].arrival < proc[sel].arrival ||
                      (proc[i].arrival == proc[sel].arrival && proc[i].pid < proc[sel].pid)))) {
                    sel = i;
                }
            }
        }

        if (sel == -1) {
            clock++;
            continue;
        }

        if (proc[sel].start == -1) {
            proc[sel].start = clock;
        }

        gantt_push(proc[sel].pid, clock, clock + 1);
        proc[sel].remaining--;
        clock++;

        if (proc[sel].remaining == 0) {
            proc[sel].finish = clock;
            completed++;
        }
    }

    compute_stats();
    print_results("SRTF_Preemptive");
    print_gantt();
}

void sched_priority(void)
{
    int done[MAX_PROC] = {0};
    int clock = 0, completed = 0;

    while (completed < n_proc) {
        int sel = -1;

        /* 數字越小代表優先權越高；同優先權時先看 arrival，再看 PID。 */
        for (int i = 0; i < n_proc; i++) {
            if (!done[i] && proc[i].arrival <= clock) {
                if (sel == -1 ||
                    proc[i].priority < proc[sel].priority ||
                    (proc[i].priority == proc[sel].priority &&
                     (proc[i].arrival < proc[sel].arrival ||
                      (proc[i].arrival == proc[sel].arrival && proc[i].pid < proc[sel].pid)))) {
                    sel = i;
                }
            }
        }

        if (sel == -1) {
            int next = INT_MAX;
            for (int i = 0; i < n_proc; i++) {
                if (!done[i] && proc[i].arrival < next) {
                    next = proc[i].arrival;
                }
            }
            clock = next;
            continue;
        }

        proc[sel].start = clock;
        proc[sel].finish = clock + proc[sel].burst;
        gantt_push(proc[sel].pid, proc[sel].start, proc[sel].finish);
        clock = proc[sel].finish;
        done[sel] = 1;
        completed++;
    }

    compute_stats();
    print_results("Priority_NonPreemptive");
    print_gantt();
}

static void rr_enqueue(int queue[], int *q_tail, int idx)
{
    if (*q_tail >= MAX_QUEUE_SLOT) {
        die_runtime("Round Robin queue capacity exceeded");
    }
    queue[(*q_tail)++] = idx;
}

void sched_rr(int quantum)
{
    int remaining[MAX_PROC];
    int admitted[MAX_PROC];
    int queue[MAX_QUEUE_SLOT];
    int q_head = 0, q_tail = 0;
    int clock = 0, completed = 0;

    qsort(proc, n_proc, sizeof(Process), cmp_arrival);

    for (int i = 0; i < n_proc; i++) {
        remaining[i] = proc[i].burst;
        proc[i].start = -1;
        admitted[i] = 0;
    }

    /* 先把時間 0 已到達的行程放進 RR ready queue。 */
    for (int i = 0; i < n_proc; i++) {
        if (proc[i].arrival == 0) {
            rr_enqueue(queue, &q_tail, i);
            admitted[i] = 1;
        }
    }

    while (completed < n_proc) {
        if (q_head == q_tail) {
            int next = INT_MAX;

            for (int i = 0; i < n_proc; i++) {
                if (!admitted[i] && proc[i].arrival < next) {
                    next = proc[i].arrival;
                }
            }
            if (next == INT_MAX) {
                die_runtime("Round Robin queue became empty before all processes completed");
            }

            clock = next;
            for (int i = 0; i < n_proc; i++) {
                if (!admitted[i] && proc[i].arrival <= clock) {
                    rr_enqueue(queue, &q_tail, i);
                    admitted[i] = 1;
                }
            }
            continue;
        }

        int idx = queue[q_head++];

        if (proc[idx].start == -1) {
            proc[idx].start = clock;
        }

        int run = (remaining[idx] < quantum) ? remaining[idx] : quantum;
        gantt_push(proc[idx].pid, clock, clock + run);
        clock += run;
        remaining[idx] -= run;

        /*
         * 新到達的行程先入隊，再把目前尚未完成的行程排回隊尾。
         * 這讓較晚到達的行程不用等目前行程連續排回去後才第一次執行。
         */
        for (int i = 0; i < n_proc; i++) {
            if (!admitted[i] && proc[i].arrival <= clock) {
                rr_enqueue(queue, &q_tail, i);
                admitted[i] = 1;
            }
        }

        if (remaining[idx] == 0) {
            proc[idx].finish = clock;
            completed++;
        } else {
            rr_enqueue(queue, &q_tail, idx);
        }
    }

    compute_stats();

    char label[32];
    snprintf(label, sizeof(label), "RoundRobin_Q%d", quantum);
    print_results(label);
    print_gantt();
}

/*
 * Workload 格式：
 *   <n_proc>
 *   <pid> <arrival> <burst> <priority>
 */
void load_processes(void)
{
    if (scanf("%d", &n_proc) != 1) {
        die_input("failed to read process count");
    }

    if (n_proc < 1 || n_proc > MAX_PROC) {
        fprintf(stderr, "Input error: process count must be between 1 and %d\n", MAX_PROC);
        exit(1);
    }

    for (int i = 0; i < n_proc; i++) {
        if (scanf("%d %d %d %d",
                  &proc[i].pid,
                  &proc[i].arrival,
                  &proc[i].burst,
                  &proc[i].priority) != 4) {
            fprintf(stderr, "Input error: failed to read process row %d\n", i + 1);
            exit(1);
        }

        if (proc[i].arrival < 0) {
            fprintf(stderr, "Input error: process %d has negative arrival time\n", proc[i].pid);
            exit(1);
        }
        if (proc[i].burst <= 0) {
            fprintf(stderr, "Input error: process %d must have positive burst time\n", proc[i].pid);
            exit(1);
        }

        proc[i].remaining = proc[i].burst;
        proc[i].start = -1;
        proc[i].responded = 0;
    }
}

static int parse_quantum(const char *text)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);

    if (text[0] == '\0' || end == text || *end != '\0' ||
        errno == ERANGE || value <= 0 || value > INT_MAX) {
        fprintf(stderr, "Invalid time quantum: %s\n", text);
        exit(1);
    }

    return (int)value;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr,
                "Usage: %s <algorithm> [time_quantum]\n"
                "  algorithm: fcfs | sjf | srtf | priority | rr\n",
                argv[0]);
        return 1;
    }

    load_processes();

    const char *algo = argv[1];

    if (strcmp(algo, "fcfs") == 0) {
        sched_fcfs();
    } else if (strcmp(algo, "sjf") == 0) {
        sched_sjf();
    } else if (strcmp(algo, "srtf") == 0) {
        sched_srtf();
    } else if (strcmp(algo, "priority") == 0) {
        sched_priority();
    } else if (strcmp(algo, "rr") == 0) {
        int q = (argc >= 3) ? parse_quantum(argv[2]) : 2;
        sched_rr(q);
    } else {
        fprintf(stderr, "Unknown algorithm: %s\n", algo);
        return 1;
    }

    return 0;
}
