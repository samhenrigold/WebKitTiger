/* thread_local destructor support. We compile TLS-using code with -target ...10.7 (clang refuses
 * thread_local below that even with -femulated-tls), so clang emits Darwin's _tlv_atexit rather than
 * __cxa_thread_atexit. Tiger's dyld has no TLV support at all, so run the callbacks off a pthread key. */
#include <pthread.h>
#include <stdlib.h>

struct tlv_dtor { void (*func)(void *); void *obj; struct tlv_dtor *next; };

static pthread_key_t tlvKey;
static int tlvKeyReady;

static void tigerRunTLVDtors(void *head)
{
    for (struct tlv_dtor *d = head, *next; d; d = next) { next = d->next; d->func(d->obj); free(d); }
}

/* The main thread never runs pthread key destructors, so flush it from atexit instead. */
static void tigerFlushMainThreadTLV(void)
{
    if (!tlvKeyReady) return;
    void *head = pthread_getspecific(tlvKey);
    pthread_setspecific(tlvKey, NULL);
    tigerRunTLVDtors(head);
}

/* Claim the key in a constructor: pthread destructors run in key order, and compiler-rt's emutls
 * creates its own key lazily, so getting in first means our destructors run before emutls frees
 * the storage they are about to touch. */
__attribute__((constructor)) static void tigerTLVInit(void)
{
    if (pthread_key_create(&tlvKey, tigerRunTLVDtors) == 0) {
        tlvKeyReady = 1;
        atexit(tigerFlushMainThreadTLV);
    }
}

void _tlv_atexit(void (*func)(void *), void *obj)
{
    if (!tlvKeyReady) tigerTLVInit();
    struct tlv_dtor *d = malloc(sizeof *d);
    if (!d) return;
    d->func = func;
    d->obj = obj;
    d->next = pthread_getspecific(tlvKey); /* LIFO, like real tlv_atexit */
    pthread_setspecific(tlvKey, d);
}
