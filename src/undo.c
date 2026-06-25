#include "../include/undo.h"
#include "../external/lz4/lz4.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

typedef struct CompressJob {
    uint32_t *raw_canvas;
    int width;
    int height;
    struct CompressJob *next;
} CompressJob;

static pthread_t worker_thread;
static pthread_mutex_t undo_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t ready_cond = PTHREAD_COND_INITIALIZER;

static CompressJob *job_queue_head = NULL;
static CompressJob *job_queue_tail = NULL;
static int is_running = 0;
static int pending_jobs = 0;

Snapshot *snapshots = NULL;
int MAX_SNAP = 50;
int count = 0;
int where = 0;

static void *compression_worker(void *arg)
{
    (void)arg;
    while (1)
    {
        pthread_mutex_lock(&undo_mutex);

        while (job_queue_head == NULL && is_running) {
            pthread_cond_wait(&queue_cond, &undo_mutex);
        }

        if (!is_running && job_queue_head == NULL) {
            pthread_mutex_unlock(&undo_mutex);
            break;
        }

        CompressJob *job = job_queue_head;
        job_queue_head = job_queue_head->next;
        if (!job_queue_head) {
            job_queue_tail = NULL;
        }

        pthread_mutex_unlock(&undo_mutex);

        int src_sz = job->width * job->height * (int)sizeof(uint32_t);
        int max_dst = LZ4_compressBound(src_sz);
        char *buf = malloc((size_t)max_dst);

        int compressed = 0;
        if (buf) {
            compressed = LZ4_compress_default(
                (const char *)job->raw_canvas,
                                              buf,
                                              src_sz,
                                              max_dst
            );
        }

        pthread_mutex_lock(&undo_mutex);

        if (compressed > 0) {
            Snapshot s;
            s.compressed_sz = compressed;
            s.data = realloc(buf, (size_t)compressed);
            s.w = job->width;
            s.h = job->height;

            if (count == MAX_SNAP) {
                free(snapshots[0].data);
                memmove(snapshots, snapshots + 1, sizeof(Snapshot) * (MAX_SNAP - 1));
                count--;
            }

            snapshots[count++] = s;
            where = count;
        } else {
            if (buf) free(buf);
        }

        free(job->raw_canvas);
        free(job);

        pending_jobs--;
        pthread_cond_broadcast(&ready_cond);
        pthread_mutex_unlock(&undo_mutex);
    }
    return NULL;
}


static void restore_snapshot(uint32_t **canvas, int *width, int *height, Snapshot *s)
{
    uint32_t *new_canvas = malloc((size_t)s->w * (size_t)s->h * sizeof(uint32_t));
    if (!new_canvas) return;

    int expected_size = s->w * s->h * (int)sizeof(uint32_t);
    int result = LZ4_decompress_safe(s->data, (char *)new_canvas, s->compressed_sz, expected_size);

    if (result < 0) {
        free(new_canvas);
        return;
    }

    free(*canvas);
    *canvas = new_canvas;
    *width = s->w;
    *height = s->h;
}


void undo_init(void)
{
    pthread_mutex_lock(&undo_mutex);
    snapshots = calloc((size_t)MAX_SNAP, sizeof(Snapshot));
    count = 0;
    where = 0;
    is_running = 1;
    pending_jobs = 0;
    pthread_create(&worker_thread, NULL, compression_worker, NULL);
    pthread_mutex_unlock(&undo_mutex);
}


void take_snapshot(uint32_t *canvas, int width, int height)
{
    size_t canvas_sz = (size_t)width * (size_t)height * sizeof(uint32_t);

    uint32_t *canvas_copy = malloc(canvas_sz);
    if (!canvas_copy) return;
    memcpy(canvas_copy, canvas, canvas_sz);

    CompressJob *job = malloc(sizeof(CompressJob));
    if (!job) {
        free(canvas_copy);
        return;
    }
    job->raw_canvas = canvas_copy;
    job->width = width;
    job->height = height;
    job->next = NULL;

    pthread_mutex_lock(&undo_mutex);

    for (int i = where; i < count; i++) {
        free(snapshots[i].data);
    }
    count = where;

    if (job_queue_tail == NULL) {
        job_queue_head = job;
        job_queue_tail = job;
    } else {
        job_queue_tail->next = job;
        job_queue_tail = job;
    }

    pending_jobs++;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&undo_mutex);
}

void undo(uint32_t **canvas, int *width, int *height)
{
    pthread_mutex_lock(&undo_mutex);

    while (pending_jobs > 0) {
        pthread_cond_wait(&ready_cond, &undo_mutex);
    }

    if (where <= 1) {
        pthread_mutex_unlock(&undo_mutex);
        return;
    }

    where--;
    restore_snapshot(canvas, width, height, &snapshots[where - 1]);
    pthread_mutex_unlock(&undo_mutex);
}

void redo(uint32_t **canvas, int *width, int *height)
{
    pthread_mutex_lock(&undo_mutex);

    while (pending_jobs > 0) {
        pthread_cond_wait(&ready_cond, &undo_mutex);
    }

    if (where >= count) {
        pthread_mutex_unlock(&undo_mutex);
        return;
    }

    restore_snapshot(canvas, width, height, &snapshots[where]);
    where++;
    pthread_mutex_unlock(&undo_mutex);
}


void undo_cleanup(void)
{
    pthread_mutex_lock(&undo_mutex);
    is_running = 0;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&undo_mutex);

    pthread_join(worker_thread, NULL);

    CompressJob *current = job_queue_head;
    while (current) {
        CompressJob *next = current->next;
        free(current->raw_canvas);
        free(current);
        current = next;
    }

    for (int i = 0; i < count; i++) {
        free(snapshots[i].data);
    }

    free(snapshots);
    snapshots = NULL;
    count = 0;
    where = 0;
}
