# DOS PCB
Multithreading library for MS-DOS inspired by POSIX threads. It was created for a school project but it's pretty cool in its own right.

# Compilation and Running
It is necessary to have the full Boralnd C++ environment located in the bc31 directory. 
The following error is an indication that this was not done properly.
```
Illegal command: make
```

To compile the program, simply drag the `c.bat` file into [DOSBox](https://www.dosbox.com/).

To run the program, drag the `r.bat` file into DOSBox or simply type in 'r' and press enter.

# Example
```c
#include <stdio.h>
#include <kernel.h>
PCB *pa = NULL, *pb = NULL;
SEM *sa = NULL, *sb = NULL;
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

