#ifndef _thread_h_
#define _thread_h_
#include <kernel.h>
const StackSize defaultStackSize = DSS;
const Time defaultTimeSlice = DTS;
void dispatch(void);
class Thread {
public:
    void start() { pcb_start(myPCB);}
    virtual ~Thread() { pcb_destroy(myPCB); }
    virtual void waitToComplete() { pcb_join(this->myPCB); }
    ID getId() { return myPCB->m_id; }
    static ID getRunningId() { return pcb_myself()->m_id; }
    static Thread *getThreadById(ID id) { return (Thread*)(pcb_find_by_id(id)->m_data); }
    
    static ID fork() { 
        PCB *pcb = pcb_fork(); 
        if (pcb) return pcb->m_id;
        return 0;
    }
    static void exit() {pcb_exit();}
    static void waitForForkChildren() {pcb_join_forked();}
protected:
    Thread(StackSize stackSize = defaultStackSize, Time timeSlice = defaultTimeSlice) {
        myPCB = pcb_create_ext(wrapper, this, stackSize, timeSlice);
    }
    virtual void run() {}
    static void wrapper(void);

private:
    PCB *myPCB;
};
#endif
