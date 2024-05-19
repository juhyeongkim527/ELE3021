#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>

int shared_resource = 0;

#define NUM_ITERS 100000
#define NUM_THREADS 10000

typedef atomic_int semarphore;
// typedef int semarphore;

semarphore s = 1;

void lock(semarphore *s);
void unlock(semarphore *s);

void lock(semarphore *s)
{
    while(*s <= 0);
    (*s)--;
}

void unlock(semarphore *s)
{
    (*s)++;
}

void* thread_func(void* arg) {
    int tid = *(int*)arg;
    
    lock(&s);
    
        for(int i = 0; i < NUM_ITERS; i++)    shared_resource++;
    
    unlock(&s);
    
    pthread_exit(NULL);
}

int main() {
    pthread_t threads[NUM_THREADS];
    int tids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        tids[i] = i;
        pthread_create(&threads[i], NULL, thread_func, &tids[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("shared: %d\n", shared_resource);
    
    return 0;
}