/*
 * Sistemas Operacionais

 * 04/09/2024

/* Include from ps */

#include <minix/config.h>
#include <minix/com.h>
#include <minix/sysinfo.h>
#include <minix/endpoint.h>
#include <limits.h>

#include <timers.h>
#include <sys/types.h>

#include <minix/const.h>
#include <minix/type.h>
#include <minix/ipc.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <minix/com.h>
#include <fcntl.h>
#include <a.out.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdio.h>
#include <ttyent.h>

#include <machine/archtypes.h>
#include "kernel/const.h"
#include "kernel/type.h"
#include "kernel/proc.h"

#include "pm/mproc.h"
#include "pm/const.h"
#include "vfs/fproc.h"
#include "vfs/const.h"
#include "mfs/const.h"

#include <stdio.h>
#include <time.h>

/* Macro to convert memory offsets to rounded kilo-units */
#define off_to_k(off) ((unsigned)(((off) + 512) / 1024))

/* Number of tasks and processes and addresses of the main process tables. */
int nr_tasks, nr_procs;

/* Process tables of the kernel, MM, and FS. */
struct proc *ps_proc;
struct mproc *ps_mproc;
struct fproc *ps_fproc;

#define KMEM_PATH "/dev/kmem" /* opened for kernel proc table */
#define MEM_PATH "/dev/mem" /* opened for pm/fs + user processes */

#define TTY_MAJ 4 /* major device of console */

int kmemfd, memfd; /* file descriptors of [k]mem */

struct pstat { /* structure filled by pstat() */
    dev_t ps_dev; /* major / minor of controlling tty */
    uid_t ps_ruid; /* real uid */
    uid_t ps_euid; /* effective uid */
    pid_t ps_pid; /* process id */
    pid_t ps_ppid; /* parent process id */
    int ps_pgrp; /* process group id */
    int ps_flags; /* kernel flags */
    int ps_mflags; /* mm flags */
    int ps_ftask; /* fs suspend task */
    int ps_blocked_on; /* what is the process blocked on */
    char ps_state; /* process state */
    vir_bytes ps_tsize; /* text size (in bytes) */
    vir_bytes ps_dsize; /* data size (in bytes) */
    vir_bytes ps_ssize; /* stack size (in bytes) */
    phys_bytes ps_vtext; /* virtual text offset */
    phys_bytes ps_vdata; /* virtual data offset */
    phys_bytes ps_vstack; /* virtual stack offset */
    phys_bytes ps_text; /* physical text offset */
    phys_bytes ps_data; /* physical data offset */
    phys_bytes ps_stack; /* physical stack offset */
    int ps_recv; /* process number to receive from */
    time_t ps_utime; /* accumulated user time */
    time_t ps_stime; /* accumulated system time */
    char *ps_args; /* concatenated argument string */
    vir_bytes ps_procargs; /* initial stack frame from MM */
};

/* ****************************************** */

/* Return canonical task name of task p_nr; overwritten on each call (yucch) */
PRIVATE char *taskname(int p_nr)
{
    int n;
    n = _ENDPOINT_P(p_nr) + nr_tasks;
    if (n < 0 || n >= nr_tasks + nr_procs) {
        return "OUTOFRANGE";
    }
    return ps_proc[n].p_name;
}

void imprimeProcessos(void)
{
    int i;
    struct pstat buf;
    struct pstat buf2;
    int uid = getuid(); /* real uid of caller */
    struct kinfo kinfo;
    u32_t system_hz;

    if (getsysinfo_up(PM_PROC_NR, SIU_SYSTEMHZ, sizeof(system_hz), &system_hz) < 0)
        exit(1);

    /* Open memory devices and get PS info from the kernel */
    if ((kmemfd = open(KMEM_PATH, O_RDONLY)) == -1) err(KMEM_PATH);
    if ((memfd = open(MEM_PATH, O_RDONLY)) == -1) err(MEM_PATH);

    getsysinfo(PM_PROC_NR, SI_KINFO, &kinfo);

    nr_tasks = kinfo.nr_tasks;
    nr_procs = kinfo.nr_procs;

    /* Allocate memory for process tables */
    ps_proc = (struct proc *)malloc((nr_tasks + nr_procs) * sizeof(ps_proc[0]));
    ps_mproc = (struct mproc *)malloc(nr_procs * sizeof(ps_mproc[0]));
    ps_fproc = (struct fproc *)malloc(nr_procs * sizeof(ps_fproc[0]));

    getsysinfo(PM_PROC_NR, SI_KPROC_TAB, ps_proc);
    getsysinfo(PM_PROC_NR, SI_PROC_TAB, ps_mproc);
    getsysinfo(VFS_PROC_NR, SI_PROC_TAB, ps_fproc);

    /* CICLO QUE IMPRIME OS DADOS PARA O EP */
    /* CICLO QUE IMPRIME OS DADOS PARA O EP */
    printf("\nPID CPU_Time Sys_Time Sons_Time P_Stack P_Data P_BSS P_Text Name\n");
    for (i = 0; i < nr_procs; i++) {
        if (pstat(i, &buf, FALSE) != -1) {
            char buff1[100];
            char buff2[100];
            char buff3[100];
            double tiempo;
            time_t seconds;

            strftime(buff1, 100, "%M:%S", localtime(&buf.ps_utime));
            strftime(buff2, 100, "%M:%S", localtime(&buf.ps_stime));

            pstat(buf.ps_ppid, &buf2, FALSE);
            tiempo = difftime(buf.ps_stime, buf2.ps_stime);

            seconds = (time_t)((int)tiempo);
            strftime(buff3, 100, "%M:%S", localtime(&seconds));

            printf(" %4d %s %s %s 0x%lx 0x%lx 0x%lx 0x%lx %s\n",
                   buf.ps_pid,
                   buff1,
                   buff2,
                   buff3,
                   buf.ps_stack,
                   buf.ps_data,
                   buf.ps_data - buf.ps_dsize,
                   buf.ps_text,
                   taskname(i));
        }
    }
    free(ps_proc);
    free(ps_fproc);
    free(ps_mproc);
}

int pstat(int p_nr, struct pstat *bufp, int endpoints)
{
    int p_ki = p_nr + nr_tasks; /* kernel proc index */

    if (p_nr < -nr_tasks || p_nr >= nr_procs) {
        fprintf(stderr, "pstat: %d out of range\n", p_nr);
        return -1;
    }

    if (isemptyp(&ps_proc[p_ki])
        && !(ps_mproc[p_nr].mp_flags & IN_USE)) {
        return -1;
    }

    bufp->ps_flags = ps_proc[p_ki].p_rts_flags;
    bufp->ps_dev = ps_fproc[p_nr].fp_tty;
    bufp->ps_ftask = ps_fproc[p_nr].fp_task;
    bufp->ps_blocked_on = ps_fproc[p_nr].fp_blocked_on;

    if (p_nr >= 0) {
        bufp->ps_ruid = ps_mproc[p_nr].mp_realuid;
        bufp->ps_euid = ps_mproc[p_nr].mp_effuid;
        if (endpoints) bufp->ps_pid = ps_proc[p_ki].p_endpoint;
        else bufp->ps_pid = ps_mproc[p_nr].mp_pid;
        bufp->ps_ppid = ps_mproc[ps_mproc[p_nr].mp_parent].mp_pid;
        /* Assume no parent when the parent and the child share the same pid.
         * This is what PM currently assumes.
         */
        if (bufp->ps_ppid == bufp->ps_pid) {
            bufp->ps_ppid = NO_PID;
        }
        bufp->ps_pgrp = ps_mproc[p_nr].mp_procgrp;
        bufp->ps_mflags = ps_mproc[p_nr].mp_flags;
    } else {
        if (endpoints) bufp->ps_pid = ps_proc[p_ki].p_endpoint;
        else bufp->ps_pid = NO_PID;
        bufp->ps_ppid = NO_PID;
        bufp->ps_ruid = bufp->ps_euid = 0;
        bufp->ps_pgrp = 0;
        bufp->ps_mflags = 0;
    }

    bufp->ps_tsize = (size_t)ps_proc[p_ki].p_memmap[T].mem_len << CLICK_SHIFT;
    bufp->ps_dsize = (size_t)ps_proc[p_ki].p_memmap[D].mem_len << CLICK_SHIFT;
    bufp->ps_ssize = (size_t)ps_proc[p_ki].p_memmap[S].mem_len << CLICK_SHIFT;
    bufp->ps_vtext = (off_t)ps_proc[p_ki].p_memmap[T].mem_vir << CLICK_SHIFT;
    bufp->ps_vdata = (off_t)ps_proc[p_ki].p_memmap[D].mem_vir << CLICK_SHIFT;
bufp->ps_vstack = (off_t)ps_proc[p_ki].p_memmap[S].mem_vir << CLICK_SHIFT;
bufp->ps_text = (off_t)ps_proc[p_ki].p_memmap[T].mem_phys << CLICK_SHIFT;
bufp->ps_data = (off_t)ps_proc[p_ki].p_memmap[D].mem_phys << CLICK_SHIFT;
bufp->ps_stack = (off_t)ps_proc[p_ki].p_memmap[S].mem_phys << CLICK_SHIFT;

bufp->ps_utime = ps_proc[p_ki].p_user_time;
bufp->ps_stime = ps_proc[p_ki].p_sys_time;

return 0;

}
