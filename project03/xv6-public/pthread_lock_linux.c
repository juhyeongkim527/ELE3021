#include <stdio.h>
#include <pthread.h>

int shared_resource = 0;

#define NUM_ITERS 10
#define NUM_THREADS 10

void lock();
void unlock();

void lock()
{
}

void unlock()
{
}

void* thread_func(void* arg) {
    int tid = *(int*)arg;
    
    lock();
    
        for(int i = 0; i < NUM_ITERS; i++)    shared_resource++;
    
    unlock();
    
    pthread_exit(NULL);
}

int main() {
    pthread_t threads[n];
    int tids[n];
    
    for (int i = 0; i < n; i++) {
        tids[i] = i;
        pthread_create(&threads[i], NULL, thread_func, &tids[i]);
    }
    
    for (int i = 0; i < n; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("shared: %d\n", shared_resource);
    
    return 0;
}