#ifndef __THREADSALIVE_H__
#define __THREADSALIVE_H__

void ta_libinit(void);
void ta_create(void (*)(void *), void *);
void ta_yield(void);
int ta_waitall(void);
int ta_self(void);

typedef int talock_t;
typedef int tachan_t;

static const int TALOCKED = 0;
static const int TAUNLOCKED = 1;

void ta_lock_init(talock_t *);
void ta_lock_destroy(talock_t *);
void ta_lock(talock_t *);
void ta_unlock(talock_t *);

void ta_chan_init(tachan_t *);
void ta_chan_destroy(tachan_t *);
int ta_chan_recv(tachan_t *);
void ta_chan_send(tachan_t *, int);

#endif /* __THREADSALIVE_H__ */
