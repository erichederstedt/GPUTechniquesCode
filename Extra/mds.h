#ifndef MDS_H
#define MDS_H

/*
        More Data Structures

    Simple public domain library containing simple and semi-performant data structures.
    Currently contains these data structures:
    - MDS_Queue: A double-ended queue, implemented via a growing circular buffer.
*/

#ifdef MDS_NO_LIBC
#ifndef MDS_ALLOC
#error You must specify MDS_ALLOC if not using libc
#endif
#ifndef MDS_FREE
#error You must specify MDS_FREE if not using libc
#endif
#endif

#ifndef MDS_SIZE_T
#define MDS_SIZE_T unsigned long long
#endif
typedef MDS_SIZE_T mds_size_t;
#ifndef MDS_BYTE_T
#define MDS_BYTE_T unsigned char
#endif
typedef MDS_BYTE_T mds_byte_t;

#ifndef MDS_ALLOC
#include <stdlib.h>
#define MDS_ALLOC(size) malloc(size) 
#endif
#ifndef MDS_FREE
#include <stdlib.h>
#define MDS_FREE(ptr) free(ptr)
#endif
#ifndef MDS_MEMCPY
#define MDS_MEMCPY(dst_ptr, src_ptr, size) { for (mds_size_t _mds_i = 0; _mds_i < (mds_size_t)(size); _mds_i++) { (dst_ptr)[_mds_i] = ((mds_byte_t*)(src_ptr))[_mds_i]; } }
#endif

#ifndef MDS_QUEUE_DEFAULT_SIZE
#define MDS_QUEUE_DEFAULT_SIZE 64
#endif
typedef struct MDS_Queue
{
    mds_size_t element_size;
    mds_size_t buffer_capacity;
    mds_size_t length;
    mds_size_t start;
    mds_byte_t* buffer;
} MDS_Queue;
static inline struct MDS_Queue mds_queue_ex(mds_size_t element_size, mds_size_t default_size)
{
    return {
        element_size,
        default_size,
        0,
        0,
        (mds_byte_t*)MDS_ALLOC(element_size * default_size)
    };
}
static inline struct MDS_Queue mds_queue(mds_size_t element_size)
{
    return mds_queue_ex(element_size, MDS_QUEUE_DEFAULT_SIZE);
}
static inline mds_size_t mds_queue_get_real_index(struct MDS_Queue* queue, mds_size_t index)
{
    return (queue->start + index) % queue->buffer_capacity;
}
static inline void* mds_queue_get(struct MDS_Queue* queue, mds_size_t index)
{
    return queue->buffer + mds_queue_get_real_index(queue, index);
}
static inline void mds_queue_grow(struct MDS_Queue* queue)
{
    mds_size_t new_buffer_capacity = queue->buffer_capacity * 2;
    mds_byte_t* new_buffer = (mds_byte_t*)malloc(queue->element_size * new_buffer_capacity);
    for (mds_size_t i = 0; i < queue->length; i++)
    {
        MDS_MEMCPY(new_buffer + i, queue->buffer + mds_queue_get_real_index(queue, i), queue->element_size);
    }
    queue->start = 0;
    free(queue->buffer);
    queue->buffer = new_buffer;
    queue->buffer_capacity = new_buffer_capacity;
}
static inline void mds_queue_push_back(struct MDS_Queue* queue, void* data)
{
    mds_size_t index = (queue->start + queue->length) % queue->buffer_capacity;
    MDS_MEMCPY(queue->buffer+index, data, queue->element_size);
    queue->length++;

    if (queue->length == queue->buffer_capacity)
    {
        mds_queue_grow(queue);
    }
}
static inline void mds_queue_push_front(struct MDS_Queue* queue, void* data)
{
    queue->start = (queue->start + queue->buffer_capacity - 1) % queue->buffer_capacity;
    MDS_MEMCPY(queue->buffer + queue->start, data, queue->element_size);
    queue->length++;

    if (queue->length == queue->buffer_capacity)
    {
        mds_queue_grow(queue);
    }
}
static inline void* mds_queue_pop_front(struct MDS_Queue* queue)
{
    unsigned int index = queue->start;
    queue->start = (queue->start + 1) % queue->buffer_capacity;
    queue->length--;
    return queue->buffer + index;
}
static inline void* mds_queue_pop_back(struct MDS_Queue* queue)
{
    queue->length--;
    unsigned int index = (queue->start + queue->length) % queue->buffer_capacity;
    return queue->buffer + index;
}

#endif