#include <stdbool.h>

#define MAX_SIZE 100

typedef struct
{
    void *items[MAX_SIZE];
    int front;
    int rear;
} Queue;

void initialize_queue(Queue *q);

bool is_empty(Queue *q);

bool is_full(Queue *q);

void enqueue(Queue *q, void *value);

void dequeue(Queue *q);

void *peek(Queue *q);