#ifndef _QUEUE_H_ 
#define _QUEUE_H_ 


struct queue_st* queue_new();
void queue_free(struct queue_st*);
void queue_enqueue(struct queue_st*, void* p);
int queue_tryenqueue(struct queue_st*, void* p);
void* queue_dequeue(struct queue_st*);
void* queue_trydequeue(struct queue_st*);


#endif  //_QUEUE_H_
