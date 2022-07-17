#ifndef _concur_h_
#define _concur_h_
#include <kernel.h>

struct BAR;
struct RW;
struct SJN;

void concur_start();
void concur_quit();

struct BAR {
    int m_count;
    int m_capacity;
    SEM *m_mutex;
    SEM *m_enter;
    SEM *m_leave;
};
BAR *bar_create (int);
void bar_wait   (BAR*);
void bar_destroy(BAR*);

struct RW {
    int m_nr;
    int m_nw;
    int m_dr;
    int m_dw;
    SEM *m_mutex;
    SEM *m_rwait;
    SEM *m_wwait;
};
RW  *rw_create     (void);
void rw_start_read (RW*);
void rw_stop_read  (RW*);
void rw_start_write(RW*);
void rw_stop_write (RW*);
void rw_destroy    (RW*);

template <class T> struct BUF {
private:
    L<T> *m_buf;
    SEM *m_full;
    SEM *m_empty;
    SEM *m_mutex;
public:
    BUF(int cap) {
        m_buf = new L<T>();
        m_full = sem_create(cap);
        m_empty = sem_create(0);
        m_mutex = sem_create(1);
    }
    ~BUF() { delete m_buf; }
    void push_tail(T t) {
        sem_wait(m_full);
        sem_wait(m_mutex);
        m_buf->push_tail(t);
        sem_signal(m_mutex);
        sem_signal(m_empty);
    }
    void push_head(T t) {
        sem_wait(m_full);
        sem_wait(m_mutex);
        m_buf->push_head(t);
        sem_signal(m_mutex);
        sem_signal(m_empty);
    }
    T pop_tail() {
        sem_wait(m_empty);
        sem_wait(m_mutex);
        T t = m_buf->pop_tail();
        sem_signal(m_mutex);
        sem_signal(m_full);
        return t;
    }
    T pop_head() {
        sem_wait(m_empty);
        sem_wait(m_mutex);
        T t = m_buf->pop_head();
        sem_signal(m_mutex);
        sem_signal(m_full);
        return t;
    }
    int count() {
        sem_wait(m_mutex);
        int cnt = m_buf->count();
        sem_signal(m_mutex);
        return cnt;
    }
};

#endif
