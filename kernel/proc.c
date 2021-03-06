/*
Copyright (c) 2016, James Sullivan <sullivan.james.f@gmail.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote
      products derived from this software without specific prior
      written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER>
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <mm/pmm.h>
#include <mm/vma.h>
#include <sched/scheduler.h>
#include <sys/kprintf.h>
#include <sys/panic.h>
#include <sys/proc.h>
#include <sys/string.h>
#include <sys/timer.h>

static proc_t init_proc;
proc_t *init_procp;

proc_t **proc_table;
pid_t    pid_max = 65535; /* Default value */

mem_cache_t *proc_alloc_cache;

extern size_t STACK_TOP;

int
set_pidmax(pid_t p)
{
        proc_t **tmp;
        if (p < pid_max)
                return 1; /* Don't let the table shrink */
        tmp = krealloc(proc_table, (1 + p) * sizeof(void *), M_KERNEL);
        if (!tmp)
                return 1;
        memset(proc_table + pid_max + 1, 0, (1 + p - pid_max)
                                            * sizeof(void *));
        proc_table = tmp;
        pid_max    = p;
        return 0;
}

static void
assign_pid(proc_t *proc, pid_t pid)
{
        bug_on(pid < 0 || pid >= pid_max, "Assigning invalid PID");
        proc_table[pid] = proc;
        if (proc)
                proc->id.pid = pid;
}

static void
unassign_pid(pid_t pid)
{
        proc_table[pid] = NULL;
}

static pid_t
next_pid(void)
{
        static pid_t last_pid = 0;
        pid_t ret;
        // TODO pid randomization?
        for (ret = last_pid+1; ret != last_pid; ret++)
        {
                if (ret > pid_max)
                        ret = 1;
                if (proc_table[ret] == NULL) {
                        last_pid = ret;
                        return ret;
                }
        }
        return 0; /* Out of PIDs */
}

static int
make_child(proc_t *par, proc_t *p, fork_req_t req)
{
        p->id.ppid = par->id.pid;
        list_add(&par->control.children, &p->control.pr_list);
        if (pmm_copy_kern(p->control.pmm,
                          (const pmm_t *)par->control.pmm)) {
                kprintf(0, "Failed to copy kernel page tables\n");
                return 1;
        }
        if (req & FORK_FLAGS_COPYUSER) {
                if (pmm_copy_user(p->control.pmm,
                                  (const pmm_t *)par->control.pmm)) {
                        kprintf(0, "Failed to copy user page tables\n");
                        return 1;
                }
        }
        return 0;
}


static void
proc_system_init_table(void)
{
        proc_table = kmalloc((1 + pid_max) * sizeof(void *), M_KERNEL);
        panic_on(!proc_table, "Failed to allocate process table.");
        memset(proc_table, 0, (1 + pid_max) * (sizeof(void *)));
}

static int
proc_init(proc_t *p)
{
        if (!p)
                return 0;
        p->id = PROC_ID_INIT;
        assign_pid(p, next_pid());
        p->state = PROC_STATE_INIT;
        p->control = PROC_CONTROL_INIT(p->control);
        p->control.pmm = pmm_create();
        if (!p->control.pmm)
               goto cleanup_pid;
        vmmap_init(&p->state.vmmap, p->control.pmm);
        return 0;

cleanup_pid:
        unassign_pid(p->id.pid);
        p->id.pid = 0;
cleanup_pmm:
        pmm_destroy(p->control.pmm);
        p->control.pmm = NULL;
        return 1;
}

static void
proc_deinit(proc_t *p)
{
        if (!p)
                return;
        if (p->id.pid > 0)
                unassign_pid(p->id.pid);
        if (p->control.pmm)
                pmm_destroy(p->control.pmm);
        vmmap_deinit(&p->state.vmmap);
}

static void
proc_dtor(void *p, __attribute__((unused)) size_t sz)
{
        proc_t *proc = (proc_t *)p;
        if (!p)
                return;
        proc_deinit(proc);
}

static void
proc_system_init_initproc_early(void)
{
        init_procp = &init_proc;
        init_proc = PROC_INIT(init_proc);
        init_proc.id.pid = 1;
        init_proc.control.pmm = &init_pmm;
        get_regs(&init_proc.state.regs);
}

static void
proc_system_init_initproc(void)
{
        assign_pid(init_procp, 1);
        proc_set_current(init_procp);
        vmmap_init(&init_proc.state.vmmap, init_proc.control.pmm);
}

static void
proc_system_init_alloc(void)
{
        proc_alloc_cache = mem_cache_create("pcb_cache", sizeof(proc_t),
                                             sizeof(proc_t),
                                             0, NULL, proc_dtor);
        panic_on(!proc_alloc_cache, "Failed to create pcb_cache");
}

void
proc_system_early_init(void)
{
        proc_system_init_initproc_early();
}

void
proc_system_init(void)
{
        proc_system_init_alloc();
        proc_system_init_table();
        proc_system_init_initproc();
}


static proc_t *
_alloc_process(void)
{
        proc_t *p;
        p = mem_cache_alloc(proc_alloc_cache, M_KERNEL | M_ZERO);
        if (p || proc_init(p)) {
                goto free_proc;
        }
        return p;
free_proc:
        mem_cache_free(proc_alloc_cache, p);
        return NULL;
}

proc_t *
find_process(pid_t pid)
{
        if (pid <= 0 || pid > pid_max)
                return NULL;
        return proc_table[pid];
}

proc_t *
copy_process(proc_t *par, fork_req_t req)
{
        if (!par)
                return NULL;

        proc_t *p = _alloc_process();
        if (!p) {
                kprintf(0, "Failed to allocate process\n");
                return NULL;
        } else if (make_child(par, p, req)) {
                free_process(p);
                p = NULL;
        }
        return p;
}

void
free_process(proc_t *p)
{
        if (!p)
                return;
        bug_on(p->state.sched_state != PROC_STATE_TERMINATED,
               "Process was freed while in use.");
        mem_cache_free(proc_alloc_cache, p);
}

__test void
proc_test(void)
{
        proc_t *p1, *p2;
        p1 = proc_current();
        bug_on(p1->id.pid != 0, "Current is not pid 0\n");
        p2 = find_process(0);
        bug_on(p1 != p2, "find_process misidentified pid 0\n");
        p2 = copy_process(p1, 0);
        bug_on(!p2, "copy_process failed\n");
        p2->state.sched_state = PROC_STATE_TERMINATED;
        free_process(p2);
        kprintf(0, "proc_test passed\n");

}
