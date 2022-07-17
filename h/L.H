#ifndef _l_h_
#define _l_h_
#define L_CAP 64
#define L_OFF(base, off) \
    (((base) + (off)) % L_CAP + L_CAP * ((((base) + (off)) % L_CAP) < 0))
#define L_FACTOR 1.5

template <class T> struct L {
    T *m_data;
    int m_head;
    int m_tail;
    int m_count;
    int m_cap;
    float m_factor;
    int m_growable;
    L(int cap = L_CAP, int grw = 1) : 
        m_head(0),
        m_tail(0),
        m_count(0),
        m_cap(cap),
        m_data(new T[cap]),
        m_factor(L_FACTOR),
        m_growable(grw) {} 
    ~L() {delete m_data;}
    int grow(void);
    int head_off(int off) {return L_OFF(m_head, off); }
    int tail_off(int off) {return L_OFF(m_tail, off); }
    void push_tail(T t) {
        if (! grow()) return;
        m_data[tail_off(-1)] = t;
        m_tail = tail_off(-1);
        m_count += 1;
    }
    void push_head(T t) {
        if (! grow()) return;
        m_data[m_head] = t;
        m_head = head_off(+1);
        m_count += 1;
    }
    T pop_tail() {
        if (m_count == 0) return 0;
        T t = m_data[m_tail];
        m_tail = tail_off(+1);
        m_count -= 1;
        return t;
    }
    T pop_head() {
        if (m_count == 0) return 0;
        T t = m_data[head_off(-1)];
        m_head = head_off(-1);
        m_count -= 1;
        return t;
    }
    int find(T t);
    int find_by(int (*)(T, void*), void *);
    void insert(T t, int at);
    void insert_by(T t, int (*)(T, T));
    void remove(int at);
    void find_remove(T t) {
        remove(find(t));
    }
    void find_by_remove(int (*crit)(T, void*), void *data) {
        remove(find_by(crit, data));
    }     
    T get(int at) { 
        return (at >= 0 && at < m_count) ? m_data[tail_off(at)] : 0;
    }
    int count() const { 
        return m_count; 
    }
    void for_each(void (*func)(T));
    void pop_each(void (*func)(T)) {
        for_each(func);
        clear();
    }
    void clear() {
        m_head = 0;
        m_tail = 0;
        m_count = 0;
    }
    L<T> *subset(int (*crit)(T, void*), void *data);
};

template <class T> int L<T>::grow(void)
{
    if (m_count == m_cap) {
        if (! m_growable)
            return 0;
        int new_cap = (int)(m_factor * m_cap);
        T *new_data = new T[new_cap];
        if (! new_data)
            return 0;
        for (int i = 0; i < m_count; i++)
            new_data[i] = m_data[tail_off(i)];
        m_tail = 0;
        m_head = m_count;
        m_cap = new_cap;
        delete m_data;
        m_data = new_data;
    }
    return 1;
}

template <class T> void L<T>::for_each(void (*func)(T)) 
{
    for (int i = 0; i < m_count; i++) {
        func(get(i));
    }
}

template <class T> void L<T>::remove(int at) 
{
    if (m_count > 0 && at >= 0 && at < m_count) {
        if (at >= m_count / 2) {
            for (int i = at; i < m_count - 1; i++)
                m_data[L_OFF(m_tail, i)] = m_data[L_OFF(m_tail, i + 1)]; 
            m_head = head_off(-1);
        } else {
            for (int i = at; i > 0; i--)
                m_data[L_OFF(m_tail, i)] = m_data[L_OFF(m_tail, i - 1)];
            m_tail = tail_off(+1);
        }
        m_count -= 1;
    } 
}

template <class T> int L<T>::find(T t)
{
    for (int i = 0; i < m_count ; i++)
        if (m_data[tail_off(i)] == t) 
            return i;
    return -1;
} 

template <class T> int L<T>::find_by(int (*crit)(T, void*), void *data)
{
    for (int i = 0; i < m_count ; i++)
        if (crit(m_data[tail_off(i)], data)) 
            return i;
    return -1;
} 

template <class T> void L<T>::insert(T t, int at)
{
    if (at >= 0 && at <= m_count) {
        if (! grow()) return;
        if (at >= m_count / 2) {
            for (int i = m_count; i > at; i--)
                m_data[tail_off(i)] = m_data[tail_off(i - 1)];
            m_head = head_off(+1);
        } else {
            for (int i = 0; i <= at; i++)
                m_data[tail_off(i - 1)] = m_data[tail_off(i)];
            m_tail = tail_off(-1);
        }
        m_data[L_OFF(m_tail, at)] = t;
        m_count += 1;
    }
}

template <class T> void L<T>::insert_by(T t, int (*compare)(T, T))
{
    if (m_count == 0) {
        push_head(t);
    } else {
        int at = 0;
        while (at < m_count && ! compare(t, get(at)))
            at++;
        insert(t, at);
    }
}

template <class T> L<T> *L<T>::subset(int (*crit)(T, void*), void *data)
{
    L<T> *l = new L;
    for (int i = 0; i < m_count; i++) {
        if (crit(m_data[tail_off(i)], data)) {
            l->push_head(m_data[tail_off(i)]);
        }
    }
    return l;
}

#endif