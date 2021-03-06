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

#include <machine/arch_cpu.h>
#include <machine/cpu.h>
#include <mm/vma.h>
#include <sys/base.h>
#include <sys/string.h>
#include <sys/panic.h>
#include <sys/proc.h>

static cpu_t *cpus;
static unsigned int cpus_count;

// XXX only necessary in lieu of other percpu variables to actually
// make the section non-empty. Safe to remove.
PERCPU_DEFINE(int, foo);

cpu_t cpu_primary;
char cpu_primary_kstack[KSTACK_SIZE];

static void cpu_init_cpu(cpu_t *cpu, unsigned int id);

void
cpu_init_early(void)
{
        cpu_primary.id = 0;
        cpu_primary.kstack = (vaddr_t)cpu_primary_kstack + KSTACK_SIZE;
        cpu_primary.proc = init_procp;
        cpu_primary._percpu = (void *)PERCPU_START;
        cpu_primary.self = &cpu_primary;
        arch_cpu_setup(&cpu_primary);
}

void
cpu_init(unsigned int num_cpus)
{
        cpus_count = num_cpus;
        cpus = kmalloc(sizeof(cpu_t) * (num_cpus - 1), M_KERNEL);
        panic_on(!cpus, "Failed to allocate cpu block");
        for (unsigned int i = 1; i < num_cpus; i++)
        {
                cpu_init_cpu(cpus + i, i);
        }
}

cpu_t *
cpu_get(unsigned int id)
{
        bug_on(id >= cpus_count, "ID exceeds cpu block");
        return cpus + id;
}

static void
cpu_init_percpu(cpu_t *cpu)
{
        bug_on(!cpu, "NULL cpu");
        cpu->_percpu = kmalloc(PERCPU_SZ, M_KERNEL | M_ZERO);
        panic_on(!cpu->_percpu, "Failed to allocate percpu region");
}

static void
cpu_init_cpu(cpu_t *cpu, unsigned int id)
{
        cpu->id = id;
        cpu->kstack = (vaddr_t)kmalloc(KSTACK_SIZE, M_KERNEL);
        panic_on(!cpu->kstack, "Failed to allocate kernel stack for CPU");
        cpu->proc = NULL;
        cpu_init_percpu(cpu);
}
