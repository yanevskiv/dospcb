#include <concur.h>
#define ATOMIC(SECTION)\
    atomic();\
    SECTION\
    no_atomic();\


BAR *bar_create(int count)
{
    BAR *bar = 0;
    ATOMIC(
        bar = new BAR;
        bar->m_capacity = count;
        bar->m_count = 0;
        bar->m_enter = sem_create(1);
        bar->m_leave = sem_create(0);
    )
    return bar;
}

void bar_wait(BAR *bar)
{
    sem_wait(bar->m_enter);
    bar->m_count += 1;
    if (bar->m_count == bar->m_capacity) {
        sem_signal(bar->m_leave);
    } else {
        sem_signal(bar->m_enter);
    }
    sem_wait(bar->m_leave);
    bar->m_count -= 1;
    if (bar->m_count == 0) {
        sem_signal(bar->m_enter);
    } else {
        sem_signal(bar->m_leave);
    }
}

void bar_destroy(BAR *bar)
{
    ATOMIC(
        sem_destroy(bar->m_enter);
        sem_destroy(bar->m_leave);
        delete bar;
    )
}

RW *rw_create(void)
{
    RW *rw = 0;
    ATOMIC(
        rw = new RW;
        rw->m_nr = 0;
        rw->m_nw = 0;
        rw->m_dr = 0;
        rw->m_dw = 0;
        rw->m_mutex = sem_create(1);
        rw->m_rwait = sem_create(0);
        rw->m_wwait = sem_create(0);
    )
    return rw;
}

static void rw_signal(RW *rw)
{
    if ((rw->m_nr == 0 && rw->m_nw == 0) && rw->m_dw > 0) {
        rw->m_dw -= 1;
        sem_signal(rw->m_wwait);
    } else if (rw->m_nw == 0 && rw->m_dr > 0) {
        rw->m_dr -= 1;
        sem_signal(rw->m_rwait);
    } else {
        sem_signal(rw->m_mutex);
    }
}

void rw_start_read(RW *rw)
{
    sem_wait(rw->m_mutex);
    if (rw->m_nw == 0) {
        rw->m_dr += 1;
        sem_signal(rw->m_mutex);
        sem_wait(rw->m_rwait);
    }
    rw->m_nr += 1;
    rw_signal(rw);
}

void rw_stop_read(RW *rw)
{
    sem_wait(rw->m_mutex);
    rw->m_nr -= 1;
    rw_signal(rw);
}

void rw_start_write(RW *rw)
{
    sem_wait(rw->m_mutex);
    if (rw->m_nw != 0 || rw->m_nr != 0) {
        rw->m_dw += 1;
        sem_signal(rw->m_mutex);
        sem_wait(rw->m_wwait);
    }
    rw->m_nw += 1;
    rw_signal(rw);
}

void rw_stop_write(RW *rw)
{
    sem_wait(rw->m_mutex);
    rw->m_nw -= 1;
    rw_signal(rw);
}

void rw_destroy(RW *rw)
{
    ATOMIC(
        sem_destroy(rw->m_mutex);
        sem_destroy(rw->m_rwait);
        sem_destroy(rw->m_wwait);
    )
}