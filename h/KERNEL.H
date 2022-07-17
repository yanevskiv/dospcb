#ifndef _kernel_h_
#define _kernel_h_
#define DSS         4096
#define DTS         2
#define DPSW        0x0200
#define FOREVER     -1
#define IVTNO_TIMER 0x8
#define IVTNO_KEYBD 0x9
#define KBD_ESCAPE    27
#define KBD_BACKSPACE 8
#include <L.H>

struct PCB;
struct SEM;
struct SPIN;
struct EV;

/*
 * Useful definitions 
 */
typedef unsigned int reg;
typedef unsigned int word;
typedef unsigned long StackSize;
typedef int ID;
typedef int Time;
typedef unsigned char IVTNo;
typedef void interrupt (*ISR)(...);

/*
 * Flags and Statuses
 */
enum Status {
    S_CREATED,
    S_READY,
    S_RUNNING,
    S_SLEEPING,
    S_BLOCKED,
    S_JOINING,
    S_WAITING,
    S_COMPLETE
};

enum Flag {
    F_NONE,
    F_TIMEOUT,
    F_JOINED,
    F_SIGNALED,
    F_FAILED,
    F_PASSED,
    F_FORKED,
    F_PARENT
};

/*
 * PCB related stuff (Thread)
 */
struct PCB {
    reg       m_ss;
    reg       m_sp;
    reg       m_bp;
    StackSize m_sz;
    word     *m_st;
    word      m_atomic;
    void    (*m_run)(void);
    void     *m_data;
    Time      m_quantum;
    Time      m_counter;
    Status    m_status;
    Flag      m_flag;
    L<PCB*>  *m_joining;
    L<PCB*>  *m_forked;
    PCB      *m_parent;
    PCB      *m_waitpcb;
    SEM      *m_waitsem;
    ID        m_id;
    char     *m_name;
};
PCB *pcb_create_ext  (void (*)(void), void*, StackSize, Time);
PCB *pcb_create      (void (*)(void));
void pcb_start       (PCB*);
PCB* pcb_fork        (void);
void pcb_join_forked (void);
Flag pcb_join_for    (PCB*, Time);
void pcb_join        (PCB*);
void pcb_kill        (PCB*);
void pcb_exit        (void);
void pcb_yield       (PCB*);
void pcb_dispatch    (void);
void pcb_sleep       (Time);
void pcb_destroy     (PCB*);
PCB* pcb_myself      (void);
PCB *pcb_find_by_id  (ID);
PCB *pcb_find_by_name(const char*);

/*
 * SEM related stuff (Semaphore (FIFO))
 */
struct SEM {
    int      m_val;
    L<PCB*> *m_blocked;
    ID       m_id;
    char    *m_name;
};
SEM *sem_create      (int);
Flag sem_wait_for    (SEM*, Time);
void sem_wait        (SEM*);
void sem_signal      (SEM*);
int  sem_get_val     (SEM*);
int  sem_get_cnt     (SEM*);
void sem_destroy     (SEM*);
SEM *sem_find_by_id  (ID);
SEM *sem_find_by_name(const char*);

/* 
 * SPIN related stuff (Spinning semaphore)
 */
struct SPIN {
    int   m_lock;
    int   m_val;
    ID    m_id;
    char *m_name;
};
SPIN *spin_create      (int);
Flag  spin_wait_for    (SPIN*, Time);
void  spin_wait        (SPIN*);
void  spin_signal      (SPIN*);
void  spin_destroy     (SPIN*);
SPIN *spin_find_by_id  (ID);
SPIN *spin_find_by_name(const char*);

/* 
 * EV related stuff (Event) 
 */
struct EV {
    int    m_count;
    int    m_delay;
    void  *m_data;
    void (*m_run)(void);
    int    m_prio;
    ID     m_id;
    char  *m_name;
};
EV  *ev_create_ext  (void(*)(void), void*, int);
EV  *ev_create      (void(*)(void));
void ev_signal      (EV*);
void ev_repeat      (EV*, int);
void ev_delay       (EV*, Time);
void ev_destroy     (EV*);
EV  *ev_myself      (void);
EV  *ev_find_by_id  (ID);
EV  *ev_find_by_name(const char*);

/* 
 * Sched relating stuff (Scheduling)
 */
enum Sched {
    SCH_UNKNOWN = 0,
    SCH_APPLICAT,
    SCH_RANDOM,
    SCH_FIFO
};
void  sched       (Sched);
Sched sched_flag  (void);
void  sched_put   (PCB*);
PCB*  sched_get   (void);
void  sched_flush (void);
void  sched_reset (void);
void  sched_custom(void (*)(PCB*), PCB *(*)(void));

/* Interrupt stuff */
void timer_start(void);
void timer_stop (void);
ISR  isr_swap   (IVTNo, ISR);

void kbd_init(void);
char kbd_getch(void);
void kbd_flush(void);
void kbd_quit(void);

/* Fine grain stuff */
void loop_delay(int cnt);
void atomic();
void no_atomic();

/* Debuging stuff */
int  atomic_printf(const char*, ...);
int  debug(const char*, ...);
void debug_start(void);
void debug_end(void);

#endif
