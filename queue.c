#define _GNU_SOURCE 1
#include "queue.h"

#include <stdlib.h>
#include <pthread.h>
#include <assert.h>


struct queueitem_st {
    struct queueitem_st* n;
    void* p;
};

struct queue_st {
    struct queueitem_st* h;
    struct queueitem_st* r;
    pthread_mutex_t lock;
    pthread_cond_t wait;
};

struct queue_st*
queue_new() {
    struct queue_st* q = malloc(sizeof(*q));
    q->h = NULL;
    q->r = NULL;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->wait, NULL);

    assert(q->h && q->r || !q->h && !q->r);
    return q;
}

void
queue_free(struct queue_st* q) {
    assert(q->h && q->r || !q->h && !q->r);

    pthread_cond_destroy(&q->wait);
    pthread_mutex_destroy(&q->lock);
    struct queueitem_st* h = q->h;
    free(q);
    assert(0);// check free
}

int
queue_tryenqueue(struct queue_st* q, void* p) {
    if (pthread_mutex_trylock(&q->lock))
        return -1;
    assert(q->h && q->r || !q->h && !q->r);
    
    struct queueitem_st* i = malloc(sizeof(*i));
    i->n = NULL;
    i->p = p;

    if (!q->h)
        q->h = i;
    if (q->r)
        q->r->n = i;
    q->r = i;
    
    assert(q->h && q->r || !q->h && !q->r);
    pthread_mutex_unlock(&q->lock);
    pthread_cond_signal(&q->wait);
    return 0;
}

void
queue_enqueue(struct queue_st* q, void* p) {
    pthread_mutex_lock(&q->lock);
    assert(q->h && q->r || !q->h && !q->r);

    struct queueitem_st* i = malloc(sizeof(*i));
    i->n = NULL;
    i->p = p;

    if (!q->h)
        q->h = i;
    if (q->r)
        q->r->n = i;
    q->r = i;
    
    assert(q->h && q->r || !q->h && !q->r);
    pthread_mutex_unlock(&q->lock);
    pthread_cond_signal(&q->wait);
}

void*
queue_trydequeue(struct queue_st* q) {
    if (pthread_mutex_trylock(&q->lock))
        return NULL;

    struct queueitem_st* i = NULL;
    if (q->h) {
        i = q->h;
        q->h = i->n;
    }

    if (i && !i->n) {
        q->r = NULL;
    }
    
    pthread_mutex_unlock(&q->lock);
    void* p = NULL;
    if (i) {
        p = i->p;
        free(i);
    }
    return p;
}

void* 
queue_dequeue(struct queue_st* q) {
    pthread_mutex_lock(&q->lock);
    assert(q->h && q->r || !q->h && !q->r);

    while (!q->h) {
        int err = pthread_cond_wait(&q->wait, &q->lock);
        assert(err == 0);
    }
    
    struct queueitem_st* i = NULL;
    if (q->h) {
        i = q->h;
        q->h = i->n;
    }

    if (i && !i->n) {
        q->r = NULL;
    }
    
    assert(q->h && q->r || !q->h && !q->r);
    pthread_mutex_unlock(&q->lock);
    void* p = NULL;
    if (i) {
        p = i->p;
        free(i);
    }
    return p;
}

#include <stdio.h>
#include <unistd.h>

static void*
_test_worker(void* arg) {
    struct queue_st* q = arg;

    while (1)
        puts(queue_dequeue(q));

}


void
test_queue() {
    struct queue_st* q = queue_new();
    pthread_t thr;
    pthread_create(&thr, NULL, _test_worker, q);
    sleep(1);
    queue_enqueue(q, "1");
    queue_enqueue(q, "2");
    queue_enqueue(q, "3");
    queue_enqueue(q, "4");
    getchar();
    queue_free(q);
}
