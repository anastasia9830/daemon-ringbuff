#include "../include/ringbuf.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
//#include <pthread.h>

void ringbuffer_init(rbctx_t *context, void *buffer_location, size_t buffer_size)
{
    if (context == NULL || buffer_location == NULL) {
        //fprintf(stderr, "Initialization error: context or buffer_location is NULL\n");
        return;
    }

    context->begin = (uint8_t*)buffer_location; 
    context->end = context->begin + buffer_size; 
    context->read = context->begin; 
    context->write = context->begin; 

    pthread_mutex_init(&context->mtx, NULL); 
    
    pthread_cond_init(&context->sig, NULL);  
    
}

int ringbuffer_write(rbctx_t *context, void *message, size_t message_len)
{
    

    size_t total_len = message_len + sizeof(size_t); 
    pthread_mutex_lock(&context->mtx);

    
    /*
    size_t space_free = (context->read <= context->write)
                        ? context->end - context->write + context->read - context->begin - 1
                        : context->read - context->write - 1;
    */

    size_t space_free;
    if (context->read > context->write) {
        space_free = context->read - context->write - 1;
    } else {
        space_free = context->end - context->write + context->read - context->begin - 1;
    }
    
    if (total_len > space_free) {
        pthread_mutex_unlock(&context->mtx);
        //fprintf(stderr, "Write failed: buffer full, space_free=%zu, total_len=%zu\n", space_free, total_len);
        return RINGBUFFER_FULL;
    }

    
    size_t space_to_end = context->end - context->write;
    if (space_to_end >= total_len) {
        memcpy(context->write, &message_len, sizeof(size_t));
        memcpy(context->write + sizeof(size_t), message, message_len);
        context->write += total_len;
        if (context->write == context->end) {
            context->write = context->begin;
        }
    } else {
        if (space_to_end >= sizeof(size_t)) {
            memcpy(context->write, &message_len, sizeof(size_t));
            memcpy(context->write + sizeof(size_t), message, space_to_end - sizeof(size_t));
            memcpy(context->begin, (uint8_t*)message + (space_to_end - sizeof(size_t)), total_len - space_to_end);
        } else {
            memcpy(context->write, &message_len, space_to_end);
            memcpy(context->begin, ((uint8_t*)&message_len) + space_to_end, sizeof(size_t) - space_to_end);
            memcpy(context->begin + (sizeof(size_t) - space_to_end), message, total_len - sizeof(size_t));
        }
        context->write = context->begin + (total_len - space_to_end);
    }

    pthread_cond_signal(&context->sig);
    pthread_mutex_unlock(&context->mtx);
    return SUCCESS;
}

int ringbuffer_read(rbctx_t *context, void *buffer, size_t *buffer_len)
{
    if (context == NULL || buffer == NULL || buffer_len == NULL) {
        //fprintf(stderr, "Read error: context, buffer, or buffer_len is NULL\n");
        return -1;
    }

    pthread_mutex_lock(&context->mtx);

    if (context->read == context->write || (context->read == context->end && context->write == context->begin)) {
        pthread_mutex_unlock(&context->mtx);
        //fprintf(stderr, "Read failed: buffer empty\n");
        return RINGBUFFER_EMPTY; 
    }

    size_t message_len;
    size_t space_to_end = context->end - context->read;

    if (context->read == context->end) {
        context->read = context->begin;
        space_to_end = context->end - context->read; // end
    }

    if (space_to_end >= sizeof(size_t)) {
        memcpy(&message_len, context->read, sizeof(size_t));
    } else {
        memcpy(&message_len, context->read, space_to_end);
        memcpy(((uint8_t*)&message_len) + space_to_end, context->begin, sizeof(size_t) - space_to_end);
    }

    if (*buffer_len < message_len) {
        pthread_mutex_unlock(&context->mtx);
        //fprintf(stderr, "Read failed: output buffer too small, message_len=%zu, buffer_len=%zu\n", message_len, *buffer_len);
        return OUTPUT_BUFFER_TOO_SMALL;
    }

    *buffer_len = message_len;

    size_t total_len = message_len + sizeof(size_t);
    if (space_to_end >= total_len) {
        memcpy(buffer, context->read + sizeof(size_t), message_len);
        context->read += total_len;
    } else {
        if (space_to_end >= sizeof(size_t)) {
            memcpy(buffer, context->read + sizeof(size_t), space_to_end - sizeof(size_t));
            memcpy((uint8_t*)buffer + (space_to_end - sizeof(size_t)), context->begin, message_len - (space_to_end - sizeof(size_t)));
        } else {
            memcpy((uint8_t*)buffer, context->begin + (sizeof(size_t) - space_to_end), message_len);
        }
        context->read = context->begin + (total_len - space_to_end);
    }

    if (context->read == context->end) {
        context->read = context->begin; // end
    }

    pthread_cond_signal(&context->sig);
    pthread_mutex_unlock(&context->mtx);
    return SUCCESS;
}

void ringbuffer_destroy(rbctx_t *context)
{
    if (context == NULL) {
        //fprintf(stderr, "Destroy error: context is NULL\n");
        return;
    }

    pthread_mutex_destroy(&context->mtx);
    //pthread_mutex_destroy(&context->mutex_write);
    pthread_cond_destroy(&context->sig);
    //pthread_cond_destroy(&context->signal_write);
}