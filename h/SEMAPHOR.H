#ifndef _semaphor_h_
#define _semaphor_h_
#include <kernel.h>
#include <iostream.h>
class KernelSem;
class Semaphore;

class KernelSem {
public:
    KernelSem(int init = 1) : m_sem(0) {m_sem = sem_create(init);}
    virtual ~KernelSem() { sem_destroy(m_sem); }
    virtual int wait(Time maxTimeToWait) { 
        return (sem_wait_for(m_sem, maxTimeToWait == 0 ? FOREVER : maxTimeToWait) == F_SIGNALED);
    }
    virtual void signal() {sem_signal(m_sem);}
    int val() const {return sem_get_val(m_sem);}
private:
    SEM *m_sem;
};

class Semaphore {
public:
    Semaphore(int init = 1) : myImpl(new KernelSem(init)) {}
    virtual ~Semaphore() {delete myImpl;}
    virtual int wait(Time maxTimeToWait) {return myImpl->wait(maxTimeToWait);}
    virtual void signal() {myImpl->signal();}
    int val() const {return myImpl->val();}
private:
    KernelSem *myImpl;
    friend KernelSem;
};

#endif
