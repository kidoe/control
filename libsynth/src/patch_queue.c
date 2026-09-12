/*
 * A patch handed from another thread to the audio thread. See synth.h for why
 * this exists and what the two slots are for; this file is only the mechanics.
 *
 * Its own translation unit, like src/delay.c and src/midi.c, so a target with
 * one sound and no UI never links it. It costs the caller
 * sizeof(synth_patch_queue_t) of storage per track and nothing else.
 */
#include "synth/synth.h"

#ifdef __cplusplus
#error "this is C; the header is the part that compiles as both"
#endif

#include <stdatomic.h>

void synth_patch_queue_init(synth_patch_queue_t *q)
{
    int i;

    for (i = 0; i < 2; ++i) {
        q->counts[i] = 0;
    }
    atomic_store_explicit(&q->produced, 0u, memory_order_relaxed);
    atomic_store_explicit(&q->consumed, 0u, memory_order_relaxed);
    /* values[][] is deliberately left alone: nothing reads a slot that has not
       been published, and clearing 2 * SYNTH_PARAM_COUNT floats here would only
       hide a use of one that had not been. */
}

int synth_patch_send(synth_patch_queue_t *q, const float *patch, int count)
{
    unsigned produced = atomic_load_explicit(&q->produced, memory_order_relaxed);
    unsigned consumed = atomic_load_explicit(&q->consumed, memory_order_acquire);
    unsigned slot = produced & 1u;
    int i;

    /* Unsigned arithmetic, so this stays right across the wrap. */
    if (produced - consumed >= 2u) {
        return 0; /* both slots are the reader's; refuse rather than tear one */
    }
    if (count > SYNTH_PARAM_COUNT) {
        count = SYNTH_PARAM_COUNT;
    }
    if (count < 0) {
        count = 0;
    }
    for (i = 0; i < count; ++i) {
        q->values[slot][i] = patch[i];
    }
    q->counts[slot] = count;
    /* Release pairs with the reader's acquire, so the values and the count are
       visible before the index that publishes them. */
    atomic_store_explicit(&q->produced, produced + 1u, memory_order_release);
    return 1;
}

int synth_patch_apply(synth_patch_queue_t *q, synth_t *s)
{
    unsigned produced = atomic_load_explicit(&q->produced, memory_order_acquire);
    unsigned consumed = atomic_load_explicit(&q->consumed, memory_order_relaxed);
    unsigned newest;

    if (produced == consumed) {
        return 0;
    }
    /* Only the last one: nothing has been rendered between them, and loading A
       and then B leaves exactly what loading B leaves. */
    newest = (produced - 1u) & 1u;
    synth_load_patch_n(s, q->values[newest], q->counts[newest]);
    atomic_store_explicit(&q->consumed, produced, memory_order_release);
    return 1;
}
