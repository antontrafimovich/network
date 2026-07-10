// C Program to demonstrate how to Implement a queue
#include <stdbool.h>
#include <stdio.h>
#include "queue.h"

// Function to initialize the queue
void initialize_queue(Queue *q)
{
    q->front = -1;
    q->rear = 0;
}

// Function to check if the queue is empty
bool is_empty(Queue *q)
{
    return (q->front == q->rear - 1);
}

// Function to check if the queue is full
bool is_full(Queue *q)
{
    return (q->rear == MAX_SIZE);
}

// Function to add an element to the queue (Enqueue
// operation)
void enqueue(Queue *q, void *value)
{
    if (is_full(q))
    {
        printf("Queue is full\n");
        return;
    }
    q->items[q->rear] = value;
    q->rear++;
}

// Function to remove an element from the queue (Dequeue
// operation)
void dequeue(Queue *q)
{
    if (is_empty(q))
    {
        printf("Queue is empty\n");
        return;
    }
    q->front++;
}

// Function to get the element at the front of the queue
// (Peek operation)
void *peek(Queue *q)
{
    if (is_empty(q))
    {
        printf("Queue is empty\n");
        return NULL; // return some default value or handle
                   // error differently
    }
    return q->items[q->front + 1];
}