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
/**
 * C11 lock-free bounded shared memory queue. Supports multiple writers and multiple
 * readers. To simplify memory management queue users, data offered to
 * the queue are copied into the queue's buffers and copied back out
 * on retrieval.
 *
 * Queue functions return non-zero if the queue is full/empty.
 */
#ifndef LQUEUE_H
#define LQUEUE_H

#include <stddef.h>

typedef struct {
    char name[64];
    _Atomic unsigned int head;
    _Atomic unsigned int tail;
    size_t size;
    char buffer[];
} lqueue;

typedef unsigned short marker_t;

typedef struct {
    _Atomic marker_t marker;
    unsigned int size;
} header_t;

#define VALID_MASK 0x5ead
#define IS_VALID(marker) ((marker & VALID_MASK) == VALID_MASK)
#define SET_VALID(marker) (marker | VALID_MASK)

#define UNREAD_MASK (1 << ((sizeof(marker_t) * 8) - 1))
#define IS_UNREAD(marker) ((marker & UNREAD_MASK) == UNREAD_MASK)
#define IS_READ(marker) !IS_UNREAD(marker)
#define SET_UNREAD(marker) (marker | UNREAD_MASK)
#define SET_READ(marker) (marker & ~UNREAD_MASK)

lqueue *
lqueue_connect(char *name);

lqueue *
lqueue_create(char *name, size_t size);

void
lqueue_free(lqueue *);

int
lqueue_queue(lqueue *q, void *v, size_t size);

int
lqueue_dequeue(lqueue *q, void **v, size_t *size);

#endif