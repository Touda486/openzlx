// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/threading.h"

#include "openzl/common/allocation.h" // ZL_calloc, ZL_free
#include "openzl/common/assertion.h"  // ZL_ASSERT_*

enum { job_idle = 0, job_queued, job_running, job_done };

#if ZL_MULTITHREAD

#    include <pthread.h>

/* While waiting for a job, a thread runs other queued jobs. These run on top
 * of the waiting job's stack, and may wait (and help) in turn: bound the
 * nesting to bound the stack usage. Not helping is always safe: a waiting
 * thread runs the awaited job itself if it's not started yet, and running jobs
 * never transitively wait for a job started after them. */
#    define ZL_POOL_MAX_HELP_DEPTH 4
static _Thread_local int g_helpDepth = 0;

struct ZL_ThreadPool_s {
    pthread_mutex_t mutex;
    pthread_cond_t jobAvailable; // signaled when a job is queued, or on exit
    pthread_cond_t jobDone;      // broadcast when a job completes
    ZL_PoolJob* head;
    ZL_PoolJob* tail;
    int shutdown;
    unsigned nbThreads;
    pthread_t* threads;
};

static void ZL_ThreadPool_runJob(ZL_ThreadPool* pool, ZL_PoolJob* job)
{
    // @mutex is held on entry and on exit
    job->state = job_running;
    pthread_mutex_unlock(&pool->mutex);
    job->fn(job->opaque);
    pthread_mutex_lock(&pool->mutex);
    job->state = job_done;
    pthread_cond_broadcast(&pool->jobDone);
}

static ZL_PoolJob* ZL_ThreadPool_dequeueHead(ZL_ThreadPool* pool)
{
    ZL_PoolJob* const job = pool->head;
    if (job != NULL) {
        pool->head = job->next;
        if (pool->head == NULL) {
            pool->tail = NULL;
        }
        job->next = NULL;
    }
    return job;
}

// Removes queued @job from the queue. @mutex must be held.
static void ZL_ThreadPool_unlink(ZL_ThreadPool* pool, ZL_PoolJob* job)
{
    ZL_ASSERT_EQ(job->state, job_queued);
    ZL_PoolJob** link = &pool->head;
    ZL_PoolJob* prev  = NULL;
    while (*link != job) {
        ZL_ASSERT_NN(*link);
        prev = *link;
        link = &(*link)->next;
    }
    *link = job->next;
    if (pool->tail == job) {
        pool->tail = prev;
    }
    job->next = NULL;
}

static void* ZL_ThreadPool_worker(void* opaque)
{
    ZL_ThreadPool* const pool = (ZL_ThreadPool*)opaque;
    pthread_mutex_lock(&pool->mutex);
    for (;;) {
        while (pool->head == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->jobAvailable, &pool->mutex);
        }
        ZL_PoolJob* const job = ZL_ThreadPool_dequeueHead(pool);
        if (job == NULL) {
            break; // shutdown, and no more job
        }
        ZL_ThreadPool_runJob(pool, job);
    }
    pthread_mutex_unlock(&pool->mutex);
    return NULL;
}

static void ZL_ThreadPool_joinAll(ZL_ThreadPool* pool, unsigned nbStarted)
{
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->jobAvailable);
    pthread_mutex_unlock(&pool->mutex);
    for (unsigned n = 0; n < nbStarted; n++) {
        pthread_join(pool->threads[n], NULL);
    }
}

ZL_ThreadPool* ZL_ThreadPool_create(unsigned nbThreads)
{
    if (nbThreads == 0) {
        return NULL;
    }
    ZL_ThreadPool* const pool = ZL_calloc(sizeof(*pool));
    if (pool == NULL) {
        return NULL;
    }
    pool->threads = ZL_calloc(nbThreads * sizeof(pool->threads[0]));
    if (pool->threads == NULL) {
        ZL_free(pool);
        return NULL;
    }
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        ZL_free(pool->threads);
        ZL_free(pool);
        return NULL;
    }
    if (pthread_cond_init(&pool->jobAvailable, NULL) != 0) {
        pthread_mutex_destroy(&pool->mutex);
        ZL_free(pool->threads);
        ZL_free(pool);
        return NULL;
    }
    if (pthread_cond_init(&pool->jobDone, NULL) != 0) {
        pthread_cond_destroy(&pool->jobAvailable);
        pthread_mutex_destroy(&pool->mutex);
        ZL_free(pool->threads);
        ZL_free(pool);
        return NULL;
    }
    for (unsigned n = 0; n < nbThreads; n++) {
        if (pthread_create(
                    &pool->threads[n], NULL, ZL_ThreadPool_worker, pool)
            != 0) {
            pool->nbThreads = n;
            ZL_ThreadPool_free(pool);
            return NULL;
        }
    }
    pool->nbThreads = nbThreads;
    return pool;
}

void ZL_ThreadPool_free(ZL_ThreadPool* pool)
{
    if (pool == NULL) {
        return;
    }
    ZL_ASSERT_NULL(pool->head, "all jobs must be completed");
    ZL_ThreadPool_joinAll(pool, pool->nbThreads);
    pthread_cond_destroy(&pool->jobDone);
    pthread_cond_destroy(&pool->jobAvailable);
    pthread_mutex_destroy(&pool->mutex);
    ZL_free(pool->threads);
    ZL_free(pool);
}

unsigned ZL_ThreadPool_nbThreads(const ZL_ThreadPool* pool)
{
    return pool == NULL ? 0 : pool->nbThreads;
}

void ZL_ThreadPool_submit(ZL_ThreadPool* pool, ZL_PoolJob* job)
{
    ZL_ASSERT_NN(pool);
    ZL_ASSERT_NN(job);
    ZL_ASSERT_NN(job->fn);
    pthread_mutex_lock(&pool->mutex);
    job->next  = NULL;
    job->state = job_queued;
    if (pool->tail == NULL) {
        pool->head = job;
    } else {
        pool->tail->next = job;
    }
    pool->tail = job;
    pthread_cond_signal(&pool->jobAvailable);
    pthread_mutex_unlock(&pool->mutex);
}

void ZL_ThreadPool_waitOrRun(ZL_ThreadPool* pool, ZL_PoolJob* job)
{
    ZL_ASSERT_NN(pool);
    ZL_ASSERT_NN(job);
    pthread_mutex_lock(&pool->mutex);
    ZL_ASSERT_NE(job->state, job_idle, "job was never submitted");
    if (job->state == job_queued) {
        // Not started yet: run it on the calling thread
        ZL_ThreadPool_unlink(pool, job);
        ZL_ThreadPool_runJob(pool, job);
    }
    while (job->state != job_done) {
        // @job is running on another thread:
        // help with other queued jobs while waiting
        ZL_PoolJob* const other = (g_helpDepth < ZL_POOL_MAX_HELP_DEPTH)
                ? ZL_ThreadPool_dequeueHead(pool)
                : NULL;
        if (other != NULL) {
            g_helpDepth++;
            ZL_ThreadPool_runJob(pool, other);
            g_helpDepth--;
        } else {
            pthread_cond_wait(&pool->jobDone, &pool->mutex);
        }
    }
    job->state = job_idle;
    pthread_mutex_unlock(&pool->mutex);
}

int ZL_ThreadPool_cancel(ZL_ThreadPool* pool, ZL_PoolJob* job)
{
    ZL_ASSERT_NN(pool);
    ZL_ASSERT_NN(job);
    pthread_mutex_lock(&pool->mutex);
    int const cancelled = (job->state == job_queued);
    if (cancelled) {
        ZL_ThreadPool_unlink(pool, job);
        job->state = job_idle;
    }
    pthread_mutex_unlock(&pool->mutex);
    return cancelled;
}

#else // ZL_MULTITHREAD

ZL_ThreadPool* ZL_ThreadPool_create(unsigned nbThreads)
{
    (void)nbThreads;
    return NULL;
}

void ZL_ThreadPool_free(ZL_ThreadPool* pool)
{
    ZL_ASSERT_NULL(pool);
}

unsigned ZL_ThreadPool_nbThreads(const ZL_ThreadPool* pool)
{
    (void)pool;
    return 0;
}

void ZL_ThreadPool_submit(ZL_ThreadPool* pool, ZL_PoolJob* job)
{
    (void)pool;
    (void)job;
    ZL_ASSERT_FAIL("ZL_MULTITHREAD is disabled");
}

void ZL_ThreadPool_waitOrRun(ZL_ThreadPool* pool, ZL_PoolJob* job)
{
    (void)pool;
    (void)job;
    ZL_ASSERT_FAIL("ZL_MULTITHREAD is disabled");
}

int ZL_ThreadPool_cancel(ZL_ThreadPool* pool, ZL_PoolJob* job)
{
    (void)pool;
    (void)job;
    ZL_ASSERT_FAIL("ZL_MULTITHREAD is disabled");
    return 0;
}

#endif // ZL_MULTITHREAD
