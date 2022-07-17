#include <stdio.h>
#include <kernel.h>
PCB *pa = NULL, *pb = NULL;
SEM *sa = NULL, *sb = NULL;

/*
 * Timer event
 */
void tick() {
	printf("T");
}

void a(void) 
{
	for ( ; ; ) {
		sem_wait(sa);
		printf("a");
		sem_signal(sb);
	}
}

void b(void)
{
	for ( ; ; ) {
		sem_wait(sb);
		printf("b");
		sem_signal(sa);
	}
}

int userMain(int argc, char *argv[]) {
	(void) argc;
	(void) argv;
	sa = sem_create(0);
	sb = sem_create(1);
	pb = pcb_create(b);
	pa = pcb_create(a);
	pcb_start(pa);
	pcb_start(pb);
	pcb_join(pa);
	pcb_join(pb);
	return 0;
}