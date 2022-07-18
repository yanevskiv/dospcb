# DOS PCB
This is a library inspired by POSIX threads which brings multithreading to MS-DOS.
It was created for a school project but it's pretty interesting in its own right.

#Example
```
#include <stdio.h>
#include <kernel.h>
PCB *pa, *pb;
SEM *sa, *sb;
void tick() {
}

void a()
{
    for ( ; ; ) {
        sem_wait(sa);
        printf("a");
        sem_signal(sb);
    }
}

void b()
{
    for ( ; ; ) {
        sem_wait(sb);
        printf("b");
        sem_signal(sa);
    }
}

int main() {
    pa = pcb_create(a);
    pb = pcb_create(b);
    sa = sem_create(1);
    sb = sem_create(0);
    pcb_start(pa);
    pcb_start(pb);
    pcb_join(pa);
    pcb_join(pb);
    return 0;
}
```

