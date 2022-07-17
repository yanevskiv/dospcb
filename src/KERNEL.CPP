#include <kernel.h>
#include <schedule.h>
#include <l.h>
#include <dos.h>
#include <iostream.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define ATOMIC_START  \
   ++k_atomic;

#define ATOMIC_END \
    if (k_atomic - 1 == 0) {                        \
        if (k_ev_pool) {                            \
            k_ev_pool->for_each(k_bulk_ev_process); \
        }                                           \
        --k_atomic;                                 \
        if (k_expire) {                             \
            k_yield();                              \
        }                                           \
    } else {                                        \
        --k_atomic;                                 \
    }

#define ATOMIC(SECTION) \
    ATOMIC_START        \
    SECTION             \
    ATOMIC_END

#define NO_INTR_START \
    asm pushf;      \
    asm cli;

#define NO_INTR_END \
    asm popf;

#define NO_INTR(SECTION) \
    NO_INTR_START        \
    SECTION              \
    NO_INTR_END

/* Important PCBs */
static PCB *k_pcb_running = 0;
static PCB *k_pcb_kernel  = 0;
static PCB *k_pcb_main    = 0;
static PCB *k_pcb_idle    = 0;
static PCB *k_pcb_yield   = 0;

static void k_pcb_wrapper(void);
static void k_idle_wrapper(void);
static void k_main_wrapper(void);

/* Resource tracking stuff */
static L<PCB*>  *k_pcb_pool  = new L<PCB*>;
static L<SEM*>  *k_sem_pool  = new L<SEM*>;
static L<SPIN*> *k_spin_pool = new L<SPIN*>;
static L<EV*>   *k_ev_pool   = new L<EV*>;

static volatile ID k_pcb_id  = 0;
static volatile ID k_sem_id  = 0;
static volatile ID k_spin_id = 0;
static volatile ID k_ev_id   = 0;

/* User related stuff */
extern void tick();
extern int userMain(int, char **);

static void kernel_init(int, char **);
static void kernel_quit();

static int    k_argc = 0;
static char **k_argv = 0;
static int    k_ret  = 0;

static volatile word k_debug  = 0;

/* Context switching stuff */
static volatile word k_expire = 0;
static volatile word k_atomic = 0;

static ISR k_old_timer = 0;
static void interrupt k_timer(...);
static void interrupt k_yield(...);
static void interrupt k_fork(...);

static EV  *k_ev_running = 0;
static EV  *k_ev_timer   = 0;
static void k_ev_timer_wrapper(void);

/* for_each helper functions (sure wish C had lambdas) */
static void k_bulk_pcb_wakeup(PCB*);
static void k_bulk_pcb_rejoin(PCB*);
static void k_bulk_pcb_unblock(PCB*);
static void k_bulk_ev_undelay(EV*);
static void k_bulk_ev_process(EV*);

static void k_pcb_prune(PCB*);
static void k_sem_prune(SEM*);
static void k_spin_prune(SPIN*);

static int k_pcb_id_match (PCB*, void*);
static int k_sem_id_match (SEM*, void*);
static int k_spin_id_match(SPIN*, void*);
static int k_ev_id_match  (EV*, void*);

static int k_pcb_name_match (PCB*, void*);
static int k_sem_name_match (SEM*, void*);
static int k_spin_name_match(SPIN*, void*);
static int k_ev_name_match  (EV*, void*);

static void k_pcb_resched(PCB* pcb);

/* Keyboard stuff */
static L<char> *k_kbd_buf = new L<char> (64, 0);
static SEM     *k_kbd_sem  = 0;
static SPIN    *k_kbd_spin = 0; 
static ISR      k_old_kbd  = 0;

/* Schedule stuff */
static Sched    k_sched_flag = SCH_UNKNOWN;
static void   (*k_sched_put)(PCB*) = 0;
static PCB   *(*k_sched_get)(void) = 0;

/* Custom scheduling methods */
static void   (*k_custom_sched_put)(PCB*) = 0;
static PCB   *(*k_custom_sched_get)(void) = 0;

/* Kenel scheduling methods */
static L<PCB*> *k_kernel_sched_pool = new L<PCB*>;
static void     k_kernel_sched_put(PCB*);
static PCB     *k_kernel_sched_get(void);

void loop_delay(int cnt)
{
    for (volatile int j = 0; j < cnt; j++) {
        for (volatile int i = 0; i < 32767; i++) {
            /* nothing */
        }
    }
}

static void interrupt k_yield(...)
{
    static volatile reg t_ss;
    static volatile reg t_sp;
    static volatile reg t_bp;
    static volatile word t_lock = 0;
    static volatile word t_repeat = 0;
    /* Lock */
    asm {
        mov ax, 1
        xchg t_lock, ax
    }
    if (_AX == 1) {
        ++t_repeat;
        return;
    }
    for (t_repeat = 1; t_repeat > 0; --t_repeat) {
        /* Save context */
        asm {
            mov t_ss, ss
            mov t_sp, sp
            mov t_bp, bp
        }
        k_pcb_running->m_ss = t_ss;
        k_pcb_running->m_sp = t_sp;
        k_pcb_running->m_bp = t_bp;
        k_pcb_running->m_atomic = k_atomic;
        k_expire = 0;
        /* Reschedule */
        if (k_pcb_running->m_status == S_RUNNING) {
            k_pcb_running->m_status = S_READY;
            if (k_pcb_running != k_pcb_idle) {
                sched_put(k_pcb_running);
            }
        }
        /* Change thread */
        do {
            if (k_pcb_yield) {
                k_pcb_running = k_pcb_yield;
                k_pcb_yield = 0;
            } else {
                k_pcb_running = sched_get();
            }
        } while (k_pcb_running && k_pcb_running->m_status != S_READY);
        if (! k_pcb_running) {
            k_pcb_running = k_pcb_idle;
        }
        /* Restore context */
        k_pcb_running->m_status = S_RUNNING;
        k_pcb_running->m_counter = k_pcb_running->m_quantum;
        k_atomic = k_pcb_running->m_atomic;
        t_ss = k_pcb_running->m_ss;
        t_sp = k_pcb_running->m_sp;
        t_bp = k_pcb_running->m_bp;
        asm {
            mov ss, t_ss
            mov sp, t_sp
            mov bp, t_bp
        }
    }
    /* Unlock */
    t_lock = 0;
}

static void k_pcb_wrapper(void)
{
    k_pcb_running->m_run();
    ATOMIC(
        k_pcb_running->m_status = S_COMPLETE;
        k_pcb_running->m_joining->pop_each(k_bulk_pcb_rejoin);
        k_yield();
    )
}

static void k_main_wrapper(void)
{
    k_ret = userMain(k_argc, k_argv);
}

static void k_idle_wrapper(void)
{
    for ( ; ; ) {
        loop_delay(5);
    }
}

static void k_bulk_pcb_wakeup(PCB *pcb)
{
    if (pcb->m_status == S_SLEEPING || pcb->m_status == S_BLOCKED || pcb->m_status == S_JOINING) {
        if (pcb->m_counter > 0) {
            pcb->m_counter -= 1;
        }
        if (pcb->m_counter == 0) {
            if (pcb->m_waitsem) {
                pcb->m_waitsem->m_blocked->find_remove(pcb);
                pcb->m_waitsem = 0;
            } else if (pcb->m_waitpcb) {
                pcb->m_waitpcb->m_joining->find_remove(pcb);
                pcb->m_waitpcb = 0;
            }
        	printf("K");
            pcb->m_flag = F_TIMEOUT;
            pcb->m_status = S_READY;
            sched_put(pcb);
        }
    }
}

static void k_bulk_ev_undelay(EV *ev)
{
    if (ev->m_delay > 0) {
        ev->m_delay -= 1;
    }
}

static void k_ev_timer_wrapper(void)
{
    /* Decrement or expire currently running PCB */
    if (k_pcb_running->m_counter > 0) {
        k_pcb_running->m_counter -= 1;
        if (k_pcb_running->m_counter == 0) {
            k_expire = 1;
		}
    }
    /* Wake up sleeping threads and events */
    k_pcb_pool->for_each(k_bulk_pcb_wakeup);
    k_ev_pool->for_each(k_bulk_ev_undelay);
}

static void interrupt k_timer(...)
{
    /* Call old timer and user defined tick(). */
    if (k_old_timer) {
        k_old_timer();
    }
    tick();
    
    /* Signal timer event */
    ev_signal(k_ev_timer);
}

PCB *pcb_create_ext(void (*run)(void), void *data, StackSize sz, Time quantum)
{
    PCB *pcb = 0;
    ATOMIC_START
        pcb = new PCB;
        if (run) {
            /* Create stack */
            size_t cnt = sz / sizeof(word);
            if (sz > 0) {
                pcb->m_st = new word[cnt];
            } 
            pcb->m_sz = sz;
            pcb->m_st[cnt - 1] = DPSW;
            pcb->m_st[cnt - 2] = FP_SEG(k_pcb_wrapper);
            pcb->m_st[cnt - 3] = FP_OFF(k_pcb_wrapper);
            pcb->m_ss = FP_SEG(pcb->m_st + cnt - 12);
            pcb->m_sp = FP_OFF(pcb->m_st + cnt - 12);
            pcb->m_bp = pcb->m_sp;
        } else {
            /* Kernel pcb */
            volatile reg p_ss;
            volatile reg p_sp;
            volatile reg p_bp;
            asm {
                mov p_ss, ss
                mov p_sp, sp
                mov p_bp, bp
            }
            pcb->m_ss = p_ss;
            pcb->m_sp = p_sp;
            pcb->m_bp = p_bp;
        }
        /* Reconfigure */
        pcb->m_quantum = quantum;
        pcb->m_counter = quantum;
        pcb->m_status  = S_CREATED;
        pcb->m_flag    = F_NONE;
        pcb->m_id      = ++k_pcb_id;
        pcb->m_run     = run;
        pcb->m_data    = data;
        pcb->m_joining = new L<PCB*>;
        pcb->m_forked  = new L<PCB*>;
        pcb->m_waitpcb = 0;
        pcb->m_waitsem = 0;
        pcb->m_atomic  = 0;
        pcb->m_name    = "?";
        if (k_pcb_pool) {
            k_pcb_pool->push_head(pcb);
        }
    ATOMIC_END
    return pcb;
}

PCB *pcb_create(void (*run)(void))
{
    PCB *pcb = 0;
    ATOMIC(
        pcb = pcb_create_ext(run, 0, DSS, DTS);
    )
    return pcb;
}

void pcb_start(PCB *pcb)
{
    ATOMIC(
        if (pcb->m_status == S_CREATED) {
            pcb->m_status = S_READY;
            sched_put(pcb);
        }
    ) 
}

static PCB *k_fork_parent = 0;
static PCB *k_fork_child  = 0;
static void interrupt k_fork(...)
{
    static int k_fork_i = 0;
    static reg k_fork_ss;
    static reg k_fork_sp;
    static reg k_fork_bp;
    static word *k_fork_st;
    /* Copy stack */
    for (k_fork_i = 0; k_fork_i < k_fork_parent->m_sz / sizeof(word); k_fork_i++)
        k_fork_child->m_st[k_fork_i] = k_fork_parent->m_st[k_fork_i];
    /* Adjust regs */
    asm {
        mov k_fork_ss, ss
        mov k_fork_sp, sp
        mov k_fork_bp, bp
    }
    k_fork_st = (word*)(MK_FP(k_fork_ss, k_fork_sp)) - k_fork_parent->m_st + k_fork_child->m_st;
    k_fork_child->m_ss = FP_SEG(k_fork_st);
    k_fork_child->m_sp = FP_OFF(k_fork_st);
    k_fork_child->m_bp = k_fork_child->m_sp;
}

PCB *pcb_fork()
{
    PCB *pcb = 0;
    ATOMIC_START
        if (k_pcb_running->m_st) {
            /* Create child */
            pcb = pcb_create_ext(
                k_pcb_running->m_run,
                k_pcb_running->m_data,
                k_pcb_running->m_sz,
                k_pcb_running->m_quantum);
            /* Reconfigure */
            pcb->m_counter = k_pcb_running->m_counter;
            pcb->m_atomic  = k_atomic;
            pcb->m_flag    = F_FORKED;
            pcb->m_status  = S_READY;
            pcb->m_parent  = k_pcb_running;
            k_pcb_running->m_flag = F_PARENT;
            k_pcb_running->m_forked->push_head(pcb);
            sched_put(pcb);
            /* Adjust stack */
            k_fork_parent = k_pcb_running;
            k_fork_child = pcb;
            k_fork();
            k_yield();
            if (k_pcb_running->m_flag == F_FORKED) {
                pcb = 0;
            }
        }
    ATOMIC_END
    return pcb;
}

void pcb_join_forked()
{
    ATOMIC(
        k_pcb_running->m_forked->pop_each(pcb_join);
    )
}

static void k_bulk_pcb_rejoin(PCB* pcb)
{
    if (pcb->m_status == S_JOINING && pcb->m_waitpcb != 0) {
        pcb->m_status = S_READY;
        pcb->m_flag = F_JOINED;
        sched_put(pcb);
        pcb->m_waitpcb = 0;
    }
}

Flag pcb_join_for(PCB *pcb, Time count)
{
    Flag flag = F_FAILED;
    ATOMIC(
        if (pcb != k_pcb_idle && pcb != k_pcb_kernel && pcb->m_status != S_COMPLETE && pcb != k_pcb_running) {
            k_pcb_running->m_status = S_JOINING;
            k_pcb_running->m_waitpcb = pcb;
            k_pcb_running->m_counter = count;
            pcb->m_joining->push_head(k_pcb_running);
            k_yield();
            flag = k_pcb_running->m_flag;
        }
    )
    return flag;
}

void pcb_join(PCB *pcb)
{
    ATOMIC(
        pcb_join_for(pcb, FOREVER);
    )
}

PCB *pcb_myself()
{
    PCB *pcb = 0;
    ATOMIC(
        pcb = k_pcb_running;
    )
    return pcb;
}

void pcb_kill(PCB* pcb)
{
    ATOMIC (
        if (pcb->m_waitsem) {
            pcb->m_waitsem->m_blocked->find_remove(pcb);
            pcb->m_waitsem = 0;
        } else if (pcb->m_waitpcb) {
            pcb->m_waitpcb->m_joining->find_remove(pcb);
            pcb->m_waitpcb = 0;
        }
        pcb->m_status = S_COMPLETE;
        k_pcb_running->m_joining->pop_each(k_bulk_pcb_rejoin);
        k_yield();
    )
}

void pcb_exit()
{
    ATOMIC(
        k_pcb_running->m_status = S_COMPLETE;
        k_yield();
    )
}

void pcb_yield(PCB *pcb)
{
    ATOMIC(
        k_pcb_yield = pcb;
        k_yield();
    )
}

void pcb_dispatch()
{
    ATOMIC(
        k_yield();
    )
}

void pcb_sleep(Time count)
{
    ATOMIC(
        if (count > 0) {
            k_pcb_running->m_status = S_SLEEPING;
            k_pcb_running->m_counter = count;
        }
        k_yield();
    )
}

static int k_pcb_id_match(PCB *pcb, void *data)
{
    return pcb->m_id == *((ID*)(data));
}

static int k_pcb_name_match(PCB *pcb, void *data)
{
    return ! strcmp(pcb->m_name, (const char*)data);
}

PCB *pcb_find_by_id(ID id)
{
    PCB *pcb = 0;
    ATOMIC(
        pcb = k_pcb_pool->get(k_pcb_pool->find_by(k_pcb_id_match, (void*) &id));
    ) 
    return pcb;
}

PCB *pcb_find_by_name(const char *name)
{
    PCB *pcb = 0;
    ATOMIC(
        pcb = k_pcb_pool->get(k_pcb_pool->find_by(k_pcb_id_match, (void*) name));
    ) 
    return pcb;
}

void pcb_destroy(PCB *pcb)
{
    ATOMIC(
        pcb->m_joining->pop_each(k_bulk_pcb_rejoin);
        k_pcb_pool->find_remove(pcb);
        if (pcb->m_st)
            delete pcb->m_st;
        delete pcb->m_forked;
        delete pcb->m_joining;
        delete pcb;
    )
}

SEM *sem_create(int val)
{
    SEM *sem = 0;
    ATOMIC(
        sem = new SEM;
        sem->m_val = val;
        sem->m_blocked = new L<PCB*>;
        sem->m_id = ++k_sem_id;
        sem->m_name = "?";
        if (k_sem_pool) {
            k_sem_pool->push_head(sem);
        }
    )
    return sem;
}

void sem_signal(SEM *sem)
{
    ATOMIC(
        if (sem->m_val == 0) {
            if (sem->m_blocked->count() > 0) {
                PCB *pcb = sem->m_blocked->pop_tail();
                pcb->m_status = S_READY;
                pcb->m_flag = F_SIGNALED;
                sched_put(pcb);
                pcb->m_waitsem = 0;
            } else {
                sem->m_val += 1;
            }
        } else {
            sem->m_val += 1;
        }
    )
}

Flag sem_wait_for(SEM *sem, Time count)
{
    Flag flag = F_FAILED;
    ATOMIC(
        if (sem->m_val == 0) {
            k_pcb_running->m_status = S_BLOCKED;
            k_pcb_running->m_waitsem = sem;
            k_pcb_running->m_counter = count;
            sem->m_blocked->push_head(k_pcb_running);
            k_yield();
            flag = k_pcb_running->m_flag;
        } else if (sem->m_val > 0) {
            sem->m_val -= 1;
            flag = F_PASSED;
        }
    )
    return flag;
}

void sem_wait(SEM *sem)
{
    ATOMIC(
        sem_wait_for(sem, FOREVER);
    )
}

int sem_get_val(SEM *sem)
{
    int val = 0;
    ATOMIC(
        val = sem->m_val;
    ) 
    return val;
}

int sem_get_cnt(SEM* sem)
{
    int cnt = 0;
    ATOMIC(
        cnt = sem->m_blocked->count();
    )
    return cnt;
}

static void k_bulk_pcb_unblock(PCB* pcb)
{
    if (pcb->m_status == S_BLOCKED && pcb->m_waitsem != 0) {
        pcb->m_status = S_READY;
        pcb->m_flag = F_SIGNALED;
        sched_put(pcb);
        pcb->m_waitsem = 0;
    }
}

void sem_destroy(SEM *sem)
{
    ATOMIC(
        sem->m_blocked->pop_each(k_bulk_pcb_unblock);
        k_sem_pool->find_remove(sem);
        delete sem->m_blocked;
        delete sem;
    )
}

static int k_sem_id_match(SEM *sem, void *data)
{
    return sem->m_id == *((ID*)(data));
}

static int k_sem_name_match(SEM *sem, void *data)
{
    return ! strcmp(sem->m_name, (const char*)data);
}

SEM *sem_find_by_id(ID id)
{
    SEM *sem = 0;
    ATOMIC(
        sem = k_sem_pool->get(k_sem_pool->find_by(k_sem_id_match, (void*)&id));
    ) 
    return sem;
}

SEM *sem_find_by_name(const char *name)
{
    SEM *sem = 0;
    ATOMIC(
        sem = k_sem_pool->get(k_sem_pool->find_by(k_sem_name_match, (void*)name));
    ) 
    return sem;
}



SPIN *spin_create(int val) {
    SPIN *spin = 0;
    ATOMIC(
        spin = new SPIN;
        spin->m_lock = 0;
        spin->m_val = val;
        spin->m_id = ++k_spin_id;
        spin->m_name = "?";
        if (k_spin_pool) {
            k_spin_pool->push_head(spin);
        }
    )
    return spin;
}

Flag spin_wait_for(SPIN *spin, Time time)
{
    (void) spin;
    for ( ; ; ) {
        do {
            /* 
             * Lock spin mutex 
             *  do (
             *    atomic (
             *       ax = 1;
             *       swap spin->m_lock, ax             
             *    )
             * } while (ax == 1)
             */
            asm {
                les bx, dword ptr [bp + 6]
                mov ax, 1
                lock xchg ax, word ptr es:[bx]
            }
        } while (_AX == 1);
        if (spin->m_val > 0) {
            /* Unblock */
            spin->m_val -= 1;
            spin->m_lock = 0;
            return F_SIGNALED;
        }
        /* Unlock spin mutex */
        spin->m_lock = 0;
        /* Back off */
        if (time == 0) {
            pcb_yield(0);
        } else if (time > 0) {
            pcb_sleep(time);
        }
    }
}

void spin_wait(SPIN *spin)
{
    spin_wait_for(spin, 0);
}

void spin_signal(SPIN *spin)
{
    (void) spin;
    ATOMIC_START
    asm {
        les bx, dword ptr [bp + 6]
        lock inc word ptr es:[bx + 2]
    }
    ATOMIC_END
}

void spin_destroy(SPIN *spin)
{
    ATOMIC(
        k_spin_pool->find_remove(spin);
        delete spin;
    )
}

static int k_spin_id_match(SPIN *spin, void *data)
{
    return spin->m_id == *((ID*)(data));
}

static int k_spin_name_match(SPIN *spin, void *data)
{
    return ! strcmp(spin->m_name, (const char*)data);
}

SPIN *spin_find_by_id(ID id)
{
    SPIN *spin = 0;
    ATOMIC(
        spin = k_spin_pool->get(k_spin_pool->find_by(k_spin_id_match, (void*)&id));
    ) 
    return spin;
}

SPIN *spin_find_by_name(const char *name)
{
    SPIN *spin = 0;
    ATOMIC(
        spin = k_spin_pool->get(k_spin_pool->find_by(k_spin_name_match, (void*)name));
    ) 
    return spin;
}

static int k_ev_prio_cmp(EV *ev1, EV *ev2)
{
    return ev1->m_prio < ev2->m_prio;
}

EV *ev_create_ext(void (*run)(void), void *data, int prio)
{
    EV *ev = 0;
    ATOMIC(
        ev = new EV;
        ev->m_count = 0;
        ev->m_delay = 0;
        ev->m_data = data;
        ev->m_run = run;
        ev->m_prio = prio;
        ev->m_id = ++k_ev_id;
        ev->m_name = "?";
        if (k_ev_pool) {
            k_ev_pool->insert_by(ev, k_ev_prio_cmp);
        }
    )
    return ev;
}

EV *ev_create(void (*run)(void))
{
    return ev_create_ext(run, 0, 0);
}

void ev_signal(EV *ev)
{
    ATOMIC(
        ev->m_count++;
    )
}

void ev_repeat(EV *ev, int count)
{
    ATOMIC(
        ev->m_count = count;
    )
}

void ev_delay(EV *ev, Time count)
{
    ATOMIC(
        ev->m_delay = count;
    )
}

void ev_destroy(EV *ev)
{
    ATOMIC(
        k_ev_pool->find_remove(ev);
        delete ev;
    )
}

EV *ev_myself()
{
    EV *ev = 0;
    ATOMIC(
        ev = k_ev_running;
    )
    return ev;
}

static int k_ev_id_match(EV *ev, void *data)
{
    return ev->m_id == *((ID*)(data));
}

static int k_ev_name_match(EV *ev, void *data)
{
    return ! strcmp(ev->m_name, (const char*)data);
}

EV *ev_find_by_id(ID id)
{
    EV *ev = 0;
    ATOMIC(
        ev = k_ev_pool->get(k_ev_pool->find_by(k_ev_id_match, (void*)&id));
    ) 
    return ev;
}

EV *ev_find_by_name(const char *name)
{
    EV *ev = 0;
    ATOMIC(
        ev = k_ev_pool->get(k_ev_pool->find_by(k_ev_id_match, (void*)name));
    ) 
    return ev;
}

static void k_bulk_ev_process(EV *ev)
{
    if (ev->m_delay == 0) {
        k_ev_running = ev;
        int count = ev->m_count;
        ev->m_count -= count;
        while (count > 0) {
            ev->m_run();
            count -= 1;
        }
        k_ev_running = 0;
    }
}

void sched(Sched sch)
{
    ATOMIC(
        /* Save scheduling pool */
        L<PCB*> *pool = new L<PCB*>;
        for (PCB *pcb = sched_get(); pcb; pcb = sched_get()) {
            pool->push_head(pcb);
        }
        /* Change scheduling policy */
        k_sched_flag = sch;
        switch (sch) {
            case SCH_APPLICAT: {
                k_sched_put = Scheduler::put;
                k_sched_get = Scheduler::get;
            } break;
            case SCH_RANDOM:
            case SCH_FIFO: {
                k_sched_put = k_kernel_sched_put;
                k_sched_get = k_kernel_sched_get;
            } break;
            default: {
                k_sched_put = k_custom_sched_put;
                k_sched_get = k_custom_sched_get;
            };
        }
        /* Restore scheduling pool */
        pool->for_each(sched_put);
        delete pool;
    )
}

Sched sched_flag() 
{
    Sched flag = SCH_UNKNOWN;
    ATOMIC(
        flag = k_sched_flag;
    )
    return flag;
}

void sched_put(PCB *pcb)
{
    ATOMIC(
        k_sched_put(pcb);
    )
}

PCB *sched_get()
{
    PCB *pcb = 0;
    ATOMIC(
        pcb = k_sched_get();
    ) 
    return pcb;
}

static void k_kernel_sched_put(PCB *pcb)
{
    ATOMIC(
        switch (k_sched_flag) {
            default:
            case SCH_FIFO: 
            case SCH_RANDOM: {
                k_kernel_sched_pool->push_head(pcb);
            } break;
        }
    )
}

static PCB *k_kernel_sched_get()
{
    PCB *pcb = 0;
    ATOMIC(
        switch (k_sched_flag) {
            default:
            case SCH_FIFO: {
                pcb = k_kernel_sched_pool->pop_tail();
            } break;
            case SCH_RANDOM: {
                if (k_kernel_sched_pool->count() > 0) {
                    int at = rand() % k_kernel_sched_pool->count();
                    pcb = k_kernel_sched_pool->get(at);
                    k_kernel_sched_pool->remove(at);
                }
            } break;
        }
    )
    return pcb;
}

void sched_custom(void (*put)(PCB*), PCB *(*get)(void))
{
    ATOMIC(
        if (put && get) {
            k_custom_sched_put = put;
            k_custom_sched_get = get;
        }
    )
 
}

void sched_flush()
{
    ATOMIC(
        while (sched_get() != 0);
    )
}

static void k_pcb_resched(PCB* pcb)
{
    if (pcb->m_status == S_READY) {
        sched_put(pcb);
    }
}

void sched_reset()
{
    ATOMIC(
        sched_flush();
        k_pcb_pool->for_each(k_pcb_resched);
    ) 
}

void timer_start()
{
    NO_INTR(
        if (!k_old_timer) {
            k_old_timer = getvect(IVTNO_TIMER);
            setvect(IVTNO_TIMER, k_timer);
        }
    )
}

void timer_stop() 
{
    NO_INTR(
        if (k_old_timer) {
            setvect(IVTNO_TIMER, k_old_timer);
            k_old_timer = 0;
        }
    )
}

ISR isr_swap(IVTNo no, ISR isr)
{
    ISR old = 0;
    NO_INTR(
        old = getvect(no);
        setvect(no, isr);
    )
    return old;
}

void atomic(void)
{
    ATOMIC_START
}

void no_atomic(void)
{
    ATOMIC_END
}

int atomic_printf(const char *fmt, ...)
{
    int ret = 0;
    ATOMIC(
        va_list li;
        va_start(li, fmt);
        ret = vprintf(fmt, li);
        va_end(li);
    )
    return ret;
}

static volatile word k_kbd_lock = 0;

static void interrupt k_keybd(...) 
{
    static volatile char scan, st, ch;
    static volatile word wasShift = 0;
    static const char keymap[128] = { 
        0 , 27,'1','2','3','4','5','6','7','8','9','0','-','=', 8 , 9, 
        'q','w','e','r','t','y','u','i','o','p','[',']', '\n', 0 ,'a','s',
        'd','f','g','h','j','k','l',';',0,0,'`','\\','z','x','c','v','b',
        'n','m',',','.','/', 0 ,'*', 0 ,' '
    }, keymap_shift[128] = {
        0 , 27,'!','@','#','$','%','^','&','*','(',')','_','+', 8 , 9, 
        'Q','W','E','R','T','Y','U','I','O','P','{','}', '\n', 0 ,'A','S',
        'D','F','G','H','J','K','L',':',0,0,'`','\\','Z','X','C','V','B',
        'N','M','<','>','?', 0 ,'*', 0 ,' '    
    };
    
    ATOMIC_START
        do {
            st = inportb(0x64);
            if (st & 0x01) {
                scan = inportb(0x60);
                ch = wasShift ? keymap_shift[scan & 0x7f] : keymap[scan & 0x7f];
                if (ch == '`') {
                    wasShift = !(scan & 0x80);
                } else if (!(scan & 0x80)) {
                    k_kbd_buf->push_head(ch);
                    spin_signal(k_kbd_spin);
                }
            }
            asm {
                cli
                in  al,  61h
                or  al,  10000000b
                out 61h, al
                and al,  01111111b
                out 61h, al
                mov al,  20h
                out 20h, al
                sti
            };
        } while (st & 0x01);
    ATOMIC_END
}

void kbd_init(void)
{
    ATOMIC(
        if (k_old_kbd == 0) {
            k_old_kbd = isr_swap(IVTNO_KEYBD, k_keybd);
        }
    )
}

char kbd_getch(void)
{
    spin_wait(k_kbd_spin);
    return k_kbd_buf->pop_tail();
}

void kbd_flush(void)
{
    ATOMIC(
        while (k_kbd_lock > 0) {
            k_kbd_buf->pop_tail();
            asm {lock dec k_kbd_lock; }
        }
    )
}

void kbd_quit(void)
{
    ATOMIC(
        if (k_old_kbd) {
            isr_swap(IVTNO_KEYBD, k_old_kbd);
            k_old_kbd = 0;
        }
    )
}

int debug(const char *fmt, ...)
{
    int ret = 0;
    ATOMIC(
        if (k_debug) {
            va_list li;
            va_start(li, fmt);
            ret = vprintf(fmt, li);
            va_end(li);
        }
    )
    return ret;
}

void debug_start(void)
{
    k_debug = 1;
}

void debug_end(void)
{
    k_debug = 0;
}

static void kernel_init(int argc, char **argv)
{
    if (! k_pcb_running ) {        
        /* reset ids */
        k_pcb_id  = 0;
        k_sem_id  = 0;
        k_spin_id = 0;
        k_ev_id   = 0;
        
        /* set up pools */
        if (! k_pcb_pool)
            k_pcb_pool = new L<PCB*>;
        if (! k_sem_pool)
            k_sem_pool = new L<SEM*>;
        if (! k_spin_pool)
            k_ev_pool = new L<EV*>;
        if (! k_ev_pool)
            k_ev_pool = new L<EV*>;
        if (! k_kernel_sched_pool)
            k_kernel_sched_pool = new L<PCB*>;
        
        /* set up timer event */
        k_ev_timer = ev_create(k_ev_timer_wrapper);
        k_ev_timer->m_name = "k_ev_timer";
        k_ev_running = 0;
        
        /* set up scheduling policy */
        k_sched_flag = SCH_APPLICAT;
        k_sched_put = Scheduler::put;
        k_sched_get = Scheduler::get;
        
        /* set up kernel pcb (running) */
        k_pcb_kernel = pcb_create_ext(0, 0, 0, DTS);
        k_pcb_kernel->m_status = S_RUNNING;
        k_pcb_kernel->m_name = "k_pcb_kernel";
        k_pcb_running = k_pcb_kernel;
        k_expire = 0;
        k_atomic = 0;

        /* set up idle pcb */
        k_pcb_idle = pcb_create_ext(k_idle_wrapper, 0, DSS, DTS);
        k_pcb_idle->m_status = S_READY;
        k_pcb_idle->m_name = "k_pcb_idle";
        
        /* set up main pcb */
        k_ret  = 0;
        k_argc = argc;
        k_argv = argv;
        k_pcb_main = pcb_create_ext(k_main_wrapper, 0, DSS, DTS);
        k_pcb_main->m_status = S_READY;
        k_pcb_main->m_name = "k_pcb_main";
        sched_put(k_pcb_main);

        /* set up kbd interface */
        k_old_kbd = 0;
        k_kbd_spin = spin_create(0);
        k_kbd_sem = sem_create(0);
        
        /* start context switching */
        timer_start();
    }
}

static void k_pcb_prune(PCB *pcb)
{
    if (pcb->m_st)
        delete pcb->m_st;
    delete pcb->m_joining;
    delete pcb->m_forked;
    delete pcb;
}

static void k_sem_prune(SEM *sem)
{
    delete sem->m_blocked;
    delete sem;
}

static void k_spin_prune(SPIN *spin)
{
    delete spin;
}

static void k_ev_prune(EV *ev)
{
    delete ev;
}

static void kernel_quit(void)
{
    ATOMIC(
        timer_stop();
        
        k_pcb_running = 0;
        k_pcb_kernel  = 0;
        k_pcb_main    = 0;
        k_pcb_idle    = 0;
        
        k_expire  = 0;
        k_atomic  = 0;
        
        k_pcb_id  = 0;
        k_sem_id  = 0;
        k_spin_id = 0;
        k_ev_id   = 0;
        
        sched_flush();
        
        k_pcb_pool->pop_each(k_pcb_prune);
        k_sem_pool->pop_each(k_sem_prune);
        k_spin_pool->pop_each(k_spin_prune);
        k_ev_pool->pop_each(k_ev_prune);
        delete k_pcb_pool;
        delete k_sem_pool;
        delete k_spin_pool;
        delete k_ev_pool;
        k_pcb_pool = 0;
        k_sem_pool = 0;
        k_spin_pool = 0;
        k_ev_pool = 0;
        
        k_kbd_buf->clear();
    )
}

int main(int argc, char *argv[]){
    (void) argc;
    (void) argv;
#ifdef DEBUG
    debug_start();
#endif
    debug("Kernel starting...\n");
    kernel_init(argc, argv);
    pcb_join(k_pcb_main);
    kernel_quit();
    debug("Kernel complete.\n");
    return k_ret;
}
