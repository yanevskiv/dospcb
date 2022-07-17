#ifndef _event_h_
#define _event_h_
#include <kernel.h>
#include <l.h>

class IVTEntry;
class KernelEv;
class Event;

class IVTEntry {
public:
    static IVTEntry *get(IVTNo no) { return m_table[no]; }
    IVTEntry(IVTNo no, ISR isr) {
        m_no        = no;
        m_table[no] = this;
        m_event     = ev_create(IVTEntry::signalAll);
        m_old       = isr_swap(no, isr);
        m_new       = isr;
        m_attached  = new L<KernelEv*>;
        
        m_event->m_data = (void*) this;
    }
    ~IVTEntry() {
        delete m_attached;
        ev_destroy(m_event);
        isr_swap(m_no, m_old); 
        m_table[m_no] = 0;
    }
    void attach(KernelEv *kev) { m_attached->push_head(kev); }
    void detach(KernelEv *kev) { m_attached->find_remove(kev); }
    EV *event() const { return m_event; }
    
    ISR m_old;
    ISR m_new;
private:
    static void signalAll(void);    
    friend class KernelEv;
    static IVTEntry *m_table[];

    L<KernelEv*> *m_attached;
    
    EV   *m_event;
    IVTNo m_no;
};

class KernelEv {
public:
    KernelEv(IVTNo no) {
        m_pcb = pcb_myself();
        //m_sem = sem_create(0);
        m_spin = spin_create(0);
        m_entry = IVTEntry::get(no);
        if (m_entry)
            m_entry->attach(this);
    }
    ~KernelEv() {
        if (m_entry)
            m_entry->detach(this);
        spin_destroy(m_spin);
        //sem_destroy(m_sem);
    }
    void signal() {
        spin_signal(m_spin);
        //sem_signal(m_sem);
    }
    void wait() {
        if (pcb_myself() == m_pcb) 
            spin_wait(m_spin);
            //sem_wait(m_sem);
    }
private:
    friend class IVTEntry;
    // spin is more responsive than semaphore
    PCB      *m_pcb;
    SPIN     *m_spin;
    //SEM      *m_sem;
    IVTEntry *m_entry;    
};

class Event {
public:
    Event(IVTNo no) : myImpl(new KernelEv(no)) {}
    ~Event() { delete myImpl;}
    void wait() {myImpl->wait();}
protected:
    friend class KernelEv;
    void signal() {myImpl->signal();}
private:
    KernelEv *myImpl;
};

#define PREPAREENTRY(__NO, __CALL_OLD)              \
void interrupt __PREPARED_ISR##__NO(...) {          \
    if (__CALL_OLD && IVTEntry::get(__NO)->m_old)   \
        IVTEntry::get(__NO)->m_old();               \
    ev_signal(IVTEntry::get(__NO)->event());        \
}                                                   \
IVTEntry __PREPARED_IVT_ENTRY##__NO(                \
    __NO, __PREPARED_ISR##__NO                      \
);                                            

#endif
