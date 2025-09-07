#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "../include/daemon.h"
#include "../include/ringbuf.h"

/********************************************************************
* NETWORK TRAFFIC SIMULATION: 
* This section simulates incoming messages from various ports using 
* files. Think of these input files as data sent by clients over the
* network to our computer. The data isn't transmitted in a single 
* large file but arrives in multiple small packets. This concept
* is discussed in more detail in the advanced module: 
* Rechnernetze und Verteilte Systeme
*
* To simulate this parallel packet-based data transmission, we use multiple 
* threads. Each thread reads small segments of the files and writes these 
* smaller packets into the ring buffer. Between each packet, the
* thread sleeps for a random time between 1 and 100 us. This sleep
* simulates that data packets take varying amounts of time to arrive.
*********************************************************************/
typedef struct {
    rbctx_t* ctx;
    connection_t* connection;
} w_thread_args_t;

void* write_packets(void* arg) {
    /* extract arguments */
    rbctx_t* ctx = ((w_thread_args_t*) arg)->ctx;
    size_t from = (size_t) ((w_thread_args_t*) arg)->connection->from;
    size_t to = (size_t) ((w_thread_args_t*) arg)->connection->to;
    char* filename = ((w_thread_args_t*) arg)->connection->filename;

    /* open file */
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "Cannot open file with name %s\n", filename);
        exit(1);
    }

    /* read file in chunks and write to ringbuffer with random delay */
    unsigned char buf[MESSAGE_SIZE];
    size_t packet_id = 0;
    size_t read = 1;
    while (read > 0) {
        size_t msg_size = MESSAGE_SIZE - 3 * sizeof(size_t);
        read = fread(buf + 3 * sizeof(size_t), 1, msg_size, fp);
        if (read > 0) {
            memcpy(buf, &from, sizeof(size_t));
            memcpy(buf + sizeof(size_t), &to, sizeof(size_t));
            memcpy(buf + 2 * sizeof(size_t), &packet_id, sizeof(size_t));
            while(ringbuffer_write(ctx, buf, read + 3 * sizeof(size_t)) != SUCCESS){
                usleep(((rand() % 50) + 25)); // sleep for a random time between 25 and 75 us
            }
        }
        packet_id++;
        usleep(((rand() % (100 - 1)) + 1)); // sleep for a random time between 1 and 100 us
    }
    fclose(fp);
    return NULL;
}


// 1. read functionality
// 2. filtering functionality
// 3. (thread-safe) write to file functionality

typedef struct {
    rbctx_t* ctx;
    pthread_mutex_t* file_mutex;
} r_thread_args_t;

void* process_packets(void* arg) {
    r_thread_args_t* args = (r_thread_args_t*)arg;
    rbctx_t* ctx = args->ctx;
    pthread_mutex_t* file_mutex = args->file_mutex;
    uint8_t buffer[MESSAGE_SIZE];
    size_t len;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    while (1) {
        len = sizeof(buffer);
        int status = ringbuffer_read(ctx, buffer, &len);
        if (status == SUCCESS) {
            size_t from, to, packet_id;
            memcpy(&from, buffer, sizeof(size_t));
            memcpy(&to, buffer + sizeof(size_t), sizeof(size_t));
            memcpy(&packet_id, buffer + 2 * sizeof(size_t), sizeof(size_t));
            char* message = (char*)(buffer + 3 * sizeof(size_t));

            if (len > 3 * sizeof(size_t)) {
                message[len - 3 * sizeof(size_t)] = '\0';
            } else {
                message[0] = '\0'; 
            }

            printf("Processing packet from %zu to %zu: %s\n", from, to, message);

            if (from == to || from == 42 || to == 42 || from + to == 42) {
                continue; 
            }

            char* malicious_str = "malicious";
            int is_malicious = 0;
            for (size_t i = 0, j = 0; message[i] != '\0'; i++) {
                if (tolower((unsigned char)message[i]) == malicious_str[j]) {
                    j++;
                    if (malicious_str[j] == '\0') {
                        is_malicious = 1;
                        break;
                    }
                }
            }
            if (is_malicious) {
                continue; 
            }

            char filename[20];
            sprintf(filename, "%zu.txt", to);

            pthread_mutex_lock(file_mutex);
            FILE *fp = fopen(filename, "a");
            if (fp) {
                fwrite(message, 1, strlen(message), fp);
                fclose(fp);
            }
            pthread_mutex_unlock(file_mutex);
        } else if (status == RINGBUFFER_EMPTY) {
            usleep(100); 
        }

        pthread_testcancel();
    }
    return NULL;
}

int simpledaemon(connection_t* connections, int nr_of_connections) {
    /* initialize ringbuffer */
    rbctx_t rb_ctx;
    size_t rbuf_size = 1024;
    void *rbuf = malloc(rbuf_size);
    if (rbuf == NULL) {
        fprintf(stderr, "Error allocation ringbuffer\n");
        return -1;  // return error code if memory allocation fails
    }

    ringbuffer_init(&rb_ctx, rbuf, rbuf_size);

    /****************************************************************
    * WRITER THREADS 
    * ***************************************************************/

    /* prepare writer thread arguments */
    w_thread_args_t w_thread_args[nr_of_connections];
    for (int i = 0; i < nr_of_connections; i++) {
        w_thread_args[i].ctx = &rb_ctx;
        w_thread_args[i].connection = &connections[i];
        /* guarantee that port numbers range from MINIMUM_PORT (0) - MAXIMUMPORT */
        if (connections[i].from > MAXIMUM_PORT || connections[i].to > MAXIMUM_PORT ||
            connections[i].from < MINIMUM_PORT || connections[i].to < MINIMUM_PORT) {
            fprintf(stderr, "Port numbers %d and/or %d are too large\n", connections[i].from, connections[i].to);
            return -1;  // return error code if port numbers are invalid
        }
    }

    /* start writer threads */
    pthread_t w_threads[nr_of_connections];
    for (int i = 0; i < nr_of_connections; i++) {
        pthread_create(&w_threads[i], NULL, write_packets, &w_thread_args[i]);
    }

    pthread_t r_threads[NUMBER_OF_PROCESSING_THREADS];


    // 1. think about what arguments you need to pass to the processing threads
    // 2. start the processing threads
    // mutex for writing to files
    pthread_mutex_t file_mutex;
    pthread_mutex_init(&file_mutex, NULL);

    // reader thread arguments
    r_thread_args_t r_thread_args[NUMBER_OF_PROCESSING_THREADS];
    for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
        r_thread_args[i].ctx = &rb_ctx;
        r_thread_args[i].file_mutex = &file_mutex;
        pthread_create(&r_threads[i], NULL, process_packets, &r_thread_args[i]);
    }
 

    /* after 5 seconds JOIN all threads (we should definitely have received all messages by then) */
    printf("daemon: waiting for 5 seconds before canceling reading threads\n");
    sleep(5);
    for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
        pthread_cancel(r_threads[i]);
    }

    /* wait for all threads to finish */
    for (int i = 0; i < nr_of_connections; i++) {
        pthread_join(w_threads[i], NULL);
    }

    /* join all threads */
    for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
        pthread_join(r_threads[i], NULL);
    }

    // use this section to free any memory, destroy mutexes, etc.
    pthread_mutex_destroy(&file_mutex);


    free(rbuf);
    ringbuffer_destroy(&rb_ctx);

    return 0;

}