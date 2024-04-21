#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// below are the ADT about struct proc_queue : init, is_empty, is_full, ...
// init queue
void 
init(struct proc_queue* q, int tq)
{
    q->front = 0;
    q->rear = 0;
    q->time_quantum = tq;
}

// if queue is empty, then return 1
int 
is_empty(struct proc_queue* q)
{
    return (q->front == q->rear);
}

// if queue is full, then return 1 
int
is_full(struct proc_queue* q)
{
    return (q->front == ((q->rear + 1) % (MAXQUEUESIZE)));
}

// enqueue process in queue 
void
enqueue(struct proc_queue* q, struct proc* p)
{
    if(is_full(q)){
        cprintf("queue is full !\nfail to enqueue !\n");
        return;
    }
    q->proc_list[q->rear] = p;
    q->rear = (q->rear+1) % MAXQUEUESIZE;
}

// dequeue process in queue 
void
dequeue(struct proc_queue* q)
{
    q->front = (q->front+1) % MAXQUEUESIZE;
}

// return front process that queue has
struct proc*
front(struct proc_queue* q)
{
    return q->proc_list[q->front];
}

// search specific process in queue
// fail : return -1
// success : return specific process by index that proc_list has
int
search(struct proc_queue* q, struct proc* p)
{
    if(is_empty(q)){
        return -1;
    }
    for(int idx = q->front; idx != q->rear; idx = (idx + 1) % MAXQUEUESIZE){
        if(q->proc_list[idx] == p)
        return idx;
    }
    return -1;
}

// remove specific process by index that proc_list has
void 
remove(struct proc_queue* q, int idx)
{
    for(; idx != q->rear; idx = (idx+1) % MAXQUEUESIZE){
        q->proc_list[idx] = q->proc_list[(idx + 1) % MAXQUEUESIZE];
    } 
    q->rear = (q->rear-1 + MAXQUEUESIZE) % MAXQUEUESIZE;
}

// return current queue size
int
size(struct proc_queue *q)
{
    return ((q->rear - q->front + MAXQUEUESIZE) % MAXQUEUESIZE);
}