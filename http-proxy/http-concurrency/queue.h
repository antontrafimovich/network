#define MAX_SIZE 100

typedef struct
{
    void *items[MAX_SIZE];
    int front;
    int rear;
} Queue;

void initializeQueue(Queue *q);

bool isEmpty(Queue *q);

bool isFull(Queue *q);

void enqueue(Queue *q, int value);

void dequeue(Queue *q);

int peek(Queue *q);

void printQueue(Queue *q);
