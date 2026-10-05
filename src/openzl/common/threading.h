// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMMON_THREADING_H
#define OPENZL_COMMON_THREADING_H

#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/* ZL_MULTITHREAD :
 * 1 enables multi-threaded compression (see ZL_CParam_nbWorkers).
 * 0 disables it: ZL_ThreadPool_create() always returns NULL,
 *   and compression is always serial.
 * Currently only implemented on top of pthreads.
 */
#ifndef ZL_MULTITHREAD
#    if defined(_WIN32) \
            || (defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__))
#        define ZL_MULTITHREAD 0
#    else
#        define ZL_MULTITHREAD 1
#    endif
#endif

/* ZL_PoolJob :
 * A unit of work for ZL_ThreadPool.
 * Its storage is owned by the caller, and must remain valid
 * until the job is completed (see ZL_ThreadPool_waitOrRun()).
 * Fields are private, except @fn and @opaque, which must be set before
 * submission.
 */
typedef struct ZL_PoolJob_s {
    void (*fn)(void* opaque);
    void* opaque;
    // private
    struct ZL_PoolJob_s* next;
    int state;
} ZL_PoolJob;

typedef struct ZL_ThreadPool_s ZL_ThreadPool;

/**
 * Creates a pool of @p nbThreads background threads.
 * @returns NULL on failure, or when ZL_MULTITHREAD is disabled.
 */
ZL_ThreadPool* ZL_ThreadPool_create(unsigned nbThreads);

/**
 * Waits for all threads to terminate, then releases the pool.
 * All submitted jobs must have been completed (see waitOrRun()).
 * Accepts NULL.
 */
void ZL_ThreadPool_free(ZL_ThreadPool* pool);

/// @returns the number of background threads of @p pool. Accepts NULL.
unsigned ZL_ThreadPool_nbThreads(const ZL_ThreadPool* pool);

/**
 * Queues @p job for execution by a background thread.
 * Jobs are started in submission order.
 */
void ZL_ThreadPool_submit(ZL_ThreadPool* pool, ZL_PoolJob* job);

/**
 * Waits for completion of the submitted @p job.
 * If no background thread has started @p job yet,
 * it is executed by the calling thread instead.
 * While @p job runs on another thread, the calling thread executes
 * other queued jobs, if any.
 * After this call, @p job's storage can be reused.
 */
void ZL_ThreadPool_waitOrRun(ZL_ThreadPool* pool, ZL_PoolJob* job);

ZL_END_C_DECLS

#endif // OPENZL_COMMON_THREADING_H
