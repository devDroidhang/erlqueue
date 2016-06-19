/* -------------------------------------------------------------------
%%
%% Copyright (c) 2016 Luis Rascão.  All Rights Reserved.
%%
%% This file is provided to you under the Apache License,
%% Version 2.0 (the "License"); you may not use this file
%% except in compliance with the License.  You may obtain
%% a copy of the License at
%%
%%   http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing,
%% software distributed under the License is distributed on an
%% "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
%% KIND, either express or implied.  See the License for the
%% specific language governing permissions and limitations
%% under the License.
%%
%% ------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "lqueue.h"

#define SHMEM_PREFIX "/tmp/lqueue.shm."

lqueue *
lqueue_create(char *name, size_t size)
{
    char filename[256];
    strcpy(filename, SHMEM_PREFIX);
    strcat(filename, name);
    FILE *fp = fopen(filename, "ab+");
    fclose(fp);
    // add the lqueue struct overhead to the requested size
    int shmid = shmget(ftok(filename, 1), size + sizeof(lqueue), IPC_CREAT | 0666);
    if (shmid == -1)
        return NULL;
    lqueue *q = shmat(shmid, NULL, 0);
    if (q == (void *) -1)
        return NULL;

    q->head = ATOMIC_VAR_INIT(0);
    q->tail = ATOMIC_VAR_INIT(0);
    q->size = size;
    strcpy(q->name, name);
    return q;
}

lqueue *
lqueue_connect(char *name)
{
    char filename[256];
    strcpy(filename, SHMEM_PREFIX);
    strcat(filename, name);
    int shmid = shmget(ftok(filename, 1), 0, 0);
    if (shmid == -1)
        return NULL;
    lqueue *q = shmat(shmid, NULL, 0);
    if (q == (void *) -1)
        return NULL;
    strcpy(q->name, name);
    return q;
}

void
lqueue_free(lqueue *q)
{
    char filename[256];
    strcpy(filename, SHMEM_PREFIX);
    strcat(filename, q->name);
    int shmid = shmget(ftok(filename, 1), 0, 0);

    shmdt(q);
    shmctl(shmid, IPC_RMID, NULL);
}

int
lqueue_queue(lqueue *q, void *v, size_t size)
{
    unsigned int tail = atomic_load(&q->tail);
    unsigned int next_tail;
    unsigned short overflow;
    do {
        overflow = 0;
        next_tail = tail + sizeof(header_t) + size;
        // if this write plus an extra header would exceed the buffer limits
        // then reset and start from the top
        // this gives the assurance that we always have enough
        // to write a special end of queue header
        if ((next_tail + sizeof(header_t)) > q->size) {
            next_tail = 0;
            overflow = 1;
        }
    } while (!atomic_compare_exchange_weak(&q->tail, &tail, next_tail));
    if (overflow) {
        header_t *header = (header_t *) (q->buffer + tail);
        // we have the assurance that there's always room for an header
        // so insert a special one with a size of the total queue size
        // dequeue will see this and know that it must circle back
        // to the beginning
        header->size = q->size;
        atomic_store(&header->marker, SET_UNREAD(VALID_MASK));
        // but still we have to deal with this queue request
        // so just try again
        return lqueue_queue(q, v, size);
    } else {
        // first we read the header, if it's a valid unread one
        // then we know we've reached the top of the queue
        // and we can't write anything more without rewriting
        // unconsumed data
        header_t *header = (header_t *) (q->buffer + tail);
        marker_t marker = atomic_load(&header->marker);
        if (IS_UNREAD(marker) && IS_VALID(marker)) {
            // restore the previous tail
            atomic_store(&q->tail, tail);
            return 1;
        }
        // copy the value onto the queue
        memcpy(q->buffer + tail + sizeof(header_t), v, size);
        // now set the header size and marker
        header->size = size;
        // the marker must be stored atomically to make sure
        // a concurrent dequeue won't get us mid-copy
        atomic_store(&header->marker, SET_UNREAD(VALID_MASK));
    }
    return 0;
}

int
lqueue_dequeue(lqueue *q, void **v, size_t *size)
{
    unsigned int head = atomic_load(&q->head);
    unsigned int tail = atomic_load(&q->tail);
    unsigned int next_head = 0;
    unsigned short overflow;
    header_t *header = NULL;
    marker_t marker = 0;
    do {
        overflow = 0;
        header = (header_t *) (q->buffer + head);
        // load the marker atomically for the same
        // we store it atomically in queue
        marker = atomic_load(&header->marker);
        // we're up against the end of the queue and there's
        // nothing more to read
        if (head == tail) {
            // this is an invalid or unread block
            if (!IS_VALID(marker) || IS_READ(marker))
                return 1;
        }
        // only try to read blocks that are valid and unread
        if (!IS_VALID(marker) || IS_READ(marker))
            return 1;

        next_head = head + sizeof(header_t) + header->size;
        if (next_head > q->size) {
            next_head = 0;
            overflow = 1;
        }
    } while (!atomic_compare_exchange_weak(&q->head, &head, next_head));
    if (overflow) {
        // we've reached the end of the queue
        // so just mark this block as read and try again
        atomic_store(&header->marker, SET_READ(VALID_MASK));
        return lqueue_dequeue(q, v, size);
    }
    else {
        // extract the queued value
        *size = header->size;
        *v = q->buffer + head + sizeof(header_t);
        // set the header as read
        atomic_store(&header->marker, SET_READ(marker));
    }
    return 0;
}