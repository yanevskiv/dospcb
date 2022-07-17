#include "semaphor.h"
#include "thread.h"
#include "event.h"
static L<KernelEv*> required_for_bcc_to_implement_the_template;

void Thread::wrapper(void)
{
    Thread *thread = (Thread*)(pcb_myself()->m_data);
    thread->run();
}

void dispatch(void)
{
    pcb_dispatch();
}

static void kernel_ev_signal(KernelEv *kev)
{
    kev->signal();
}

void IVTEntry::signalAll(void)
{
    IVTEntry *entry = (IVTEntry*)(ev_myself()->m_data);
    entry->m_attached->for_each(kernel_ev_signal);
}

IVTEntry *IVTEntry::m_table[256] = { 0 };