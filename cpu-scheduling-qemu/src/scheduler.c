/*
 * scheduler.c — CPU Scheduling Algorithm Simulator
 *
 * Implements: FCFS, SJF (Non-preemptive), SRTF (Preemptive SJF),
 *             Priority Scheduling (Non-preemptive), Round Robin
 *
 * Output: per-process stats + aggregate metrics for benchmark comparison.
 * Usage:  scheduler <algorithm> [time_quantum]
 *         algorithms: fcfs | sjf | srtf | priority | rr
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

/* ── Data structures ──────────────────────────────────────────────── */

#define MAX_PROC 64

typedef struct {
    int  pid;
    int  arrival;
    int  burst;        /* original burst time */
    int  remaining;    /* used by preemptive algorithms */
    int  priority;     /* lower number = higher priority */
    /* filled in after scheduling */
    int  start;
    int  finish;
    int  waiting;
    int  turnaround;
    int  response;     /* time from arrival to first CPU access */
    int  responded;    /* flag: 0 = not yet started */
} Process;

typedef struct {
    int pid;
    int start;
    int end;
} GanttSlot;

/* ── Globals ──────────────────────────────────────────────────────── */

Process  proc[MAX_PROC];
GanttSlot gantt[MAX_PROC * 200];   /* generous; RR can produce many slots */
int      n_proc   = 0;
int      n_gantt  = 0;

/* ── Helpers ──────────────────────────────────────────────────────── */

/* Append a Gantt chart entry, merging if same PID continues */
void gantt_push(int pid, int t_start, int t_end)
{
    if (n_gantt > 0 && gantt[n_gantt-1].pid == pid) {
        gantt[n_gantt-1].end = t_end;
    } else {
        gantt[n_gantt].pid   = pid;
        gantt[n_gantt].start = t_start;
        gantt[n_gantt].end   = t_end;
        n_gantt++;
    }
}

void compute_stats(void)
{
    for (int i = 0; i < n_proc; i++) {
        proc[i].turnaround = proc[i].finish - proc[i].arrival;
        proc[i].waiting    = proc[i].turnaround - proc[i].burst;
        proc[i].response   = proc[i].start - proc[i].arrival;
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
        sum_wt  += proc[i].waiting;
        sum_tat += proc[i].turnaround;
        sum_rt  += proc[i].response;
    }

    printf("\nAvg Waiting Time    : %.2f\n", sum_wt  / n_proc);
    printf("Avg Turnaround Time : %.2f\n",  sum_tat / n_proc);
    printf("Avg Response Time   : %.2f\n",  sum_rt  / n_proc);

    /* Machine-readable line for benchmark aggregation */
    printf("BENCHMARK %s AWT=%.4f ATT=%.4f ART=%.4f\n",
           algo_label,
           sum_wt  / n_proc,
           sum_tat / n_proc,
           sum_rt  / n_proc);
}

void print_gantt(void)
{
    printf("\nGantt Chart:\n|");
    for (int i = 0; i < n_gantt; i++)
        printf(" P%-2d |", gantt[i].pid);
    printf("\n0");
    for (int i = 0; i < n_gantt; i++)
        printf("     %d", gantt[i].end);
    printf("\n");
}

/* ── Comparison functions for qsort ──────────────────────────────── */

int cmp_arrival(const void *a, const void *b)
{
    const Process *pa = (const Process *)a;
    const Process *pb = (const Process *)b;
    if (pa->arrival != pb->arrival) return pa->arrival - pb->arrival;
    return pa->pid - pb->pid;
}

int cmp_burst(const void *a, const void *b)
{
    const Process *pa = (const Process *)a;
    const Process *pb = (const Process *)b;
    if (pa->burst != pb->burst) return pa->burst - pb->burst;
    return pa->arrival - pb->arrival;
}

int cmp_priority(const void *a, const void *b)
{
    const Process *pa = (const Process *)a;
    const Process *pb = (const Process *)b;
    if (pa->priority != pb->priority) return pa->priority - pb->priority;
    return pa->arrival - pb->arrival;
}

/* ── FCFS ────────────────────────────────────────────────────────── */

void sched_fcfs(void)
{
    qsort(proc, n_proc, sizeof(Process), cmp_arrival);

    int clock = 0;
    for (int i = 0; i < n_proc; i++) {
        if (clock < proc[i].arrival) clock = proc[i].arrival;
        proc[i].start  = clock;
        proc[i].finish = clock + proc[i].burst;
        gantt_push(proc[i].pid, proc[i].start, proc[i].finish);
        clock = proc[i].finish;
    }
    compute_stats();
    print_results("FCFS");
    print_gantt();
}

/* ── SJF Non-preemptive ──────────────────────────────────────────── */

void sched_sjf(void)
{
    int done[MAX_PROC] = {0};
    int clock = 0, completed = 0;

    while (completed < n_proc) {
        /* Find shortest job that has arrived and is not done */
        int sel = -1;
        for (int i = 0; i < n_proc; i++) {
            if (!done[i] && proc[i].arrival <= clock) {
                if (sel == -1 ||
                    proc[i].burst < proc[sel].burst ||
                    (proc[i].burst == proc[sel].burst && proc[i].arrival < proc[sel].arrival))
                    sel = i;
            }
        }
        if (sel == -1) {
            /* No process ready — advance to next arrival */
            int next = INT_MAX;
            for (int i = 0; i < n_proc; i++)
                if (!done[i] && proc[i].arrival < next)
                    next = proc[i].arrival;
            clock = next;
            continue;
        }
        proc[sel].start  = clock;
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

/* ── SRTF (Preemptive SJF) ───────────────────────────────────────── */

void sched_srtf(void)
{
    /* Reset remaining times */
    for (int i = 0; i < n_proc; i++) {
        proc[i].remaining = proc[i].burst;
        proc[i].start     = -1;
    }

    int clock = 0, completed = 0;

    while (completed < n_proc) {
        /* Find process with shortest remaining time that has arrived */
        int sel = -1;
        for (int i = 0; i < n_proc; i++) {
            if (proc[i].remaining > 0 && proc[i].arrival <= clock) {
                if (sel == -1 ||
                    proc[i].remaining < proc[sel].remaining ||
                    (proc[i].remaining == proc[sel].remaining && proc[i].arrival < proc[sel].arrival))
                    sel = i;
            }
        }
        if (sel == -1) {
            clock++;
            continue;
        }
        if (proc[sel].start == -1) proc[sel].start = clock;

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

/* ── Priority Scheduling (Non-preemptive) ───────────────────────── */

void sched_priority(void)
{
    int done[MAX_PROC] = {0};
    int clock = 0, completed = 0;

    while (completed < n_proc) {
        int sel = -1;
        for (int i = 0; i < n_proc; i++) {
            if (!done[i] && proc[i].arrival <= clock) {
                if (sel == -1 ||
                    proc[i].priority < proc[sel].priority ||
                    (proc[i].priority == proc[sel].priority && proc[i].arrival < proc[sel].arrival))
                    sel = i;
            }
        }
        if (sel == -1) {
            int next = INT_MAX;
            for (int i = 0; i < n_proc; i++)
                if (!done[i] && proc[i].arrival < next)
                    next = proc[i].arrival;
            clock = next;
            continue;
        }
        proc[sel].start  = clock;
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

/* ── Round Robin ─────────────────────────────────────────────────── */

void sched_rr(int quantum)
{
    int remaining[MAX_PROC];
    int started[MAX_PROC];
    int in_queue[MAX_PROC];
    int queue[MAX_PROC * 200];
    int q_head = 0, q_tail = 0;
    int clock = 0, completed = 0;

    /* Sort by arrival first */
    qsort(proc, n_proc, sizeof(Process), cmp_arrival);

    for (int i = 0; i < n_proc; i++) {
        remaining[i] = proc[i].burst;
        proc[i].start = -1;
        started[i]   = 0;
        in_queue[i]  = 0;
    }

    /* Enqueue all processes that arrive at time 0 */
    for (int i = 0; i < n_proc; i++) {
        if (proc[i].arrival == 0) {
            queue[q_tail++] = i;
            in_queue[i] = 1;
        }
    }

    while (completed < n_proc) {
        if (q_head == q_tail) {
            /* Queue empty — advance clock to next arrival */
            int next = INT_MAX;
            for (int i = 0; i < n_proc; i++)
                if (!started[i] && proc[i].arrival < next)
                    next = proc[i].arrival;
            clock = next;
            for (int i = 0; i < n_proc; i++)
                if (!in_queue[i] && !started[i] && proc[i].arrival <= clock) {
                    queue[q_tail++] = i;
                    in_queue[i] = 1;
                }
            continue;
        }

        int idx = queue[q_head++];

        if (proc[idx].start == -1) proc[idx].start = clock;

        int run = (remaining[idx] < quantum) ? remaining[idx] : quantum;
        gantt_push(proc[idx].pid, clock, clock + run);
        clock       += run;
        remaining[idx] -= run;
        started[idx] = 1;

        /* Enqueue newly arrived processes before re-enqueuing current */
        for (int i = 0; i < n_proc; i++) {
            if (!in_queue[i] && !started[i] && proc[i].arrival <= clock) {
                queue[q_tail++] = i;
                in_queue[i] = 1;
            }
        }

        if (remaining[idx] == 0) {
            proc[idx].finish = clock;
            completed++;
        } else {
            /* Re-enqueue */
            queue[q_tail++] = idx;
        }
    }
    compute_stats();

    char label[32];
    snprintf(label, sizeof(label), "RoundRobin_Q%d", quantum);
    print_results(label);
    print_gantt();
}

/* ── Process input loader ────────────────────────────────────────── */

/*
 * Input format (stdin or file piped in):
 *   <n>
 *   <pid> <arrival> <burst> <priority>
 *   ...
 */
void load_processes(void)
{
    if (scanf("%d", &n_proc) != 1) { fprintf(stderr, "Input error\n"); exit(1); }
    for (int i = 0; i < n_proc; i++) {
        if (scanf("%d %d %d %d",
              &proc[i].pid,
              &proc[i].arrival,
              &proc[i].burst,
              &proc[i].priority) != 4) { fprintf(stderr, "Input error on process %d\n", i); exit(1); }
        proc[i].remaining = proc[i].burst;
        proc[i].start     = -1;
        proc[i].responded = 0;
    }
}

/* ── Entry point ─────────────────────────────────────────────────── */

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
        int q = (argc >= 3) ? atoi(argv[2]) : 2;
        sched_rr(q);
    } else {
        fprintf(stderr, "Unknown algorithm: %s\n", algo);
        return 1;
    }

    return 0;
}
