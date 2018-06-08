#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <strings.h>
#include <ucontext.h>
#include "threadsalive.h"

/* globals */
static const int STACKSIZE = (1024*256);
static const int MAXLOCKS = 1024;

static struct q_node *head; // head of the ready queue
static struct q_node *tail; // tail of rq
static struct q_node *cur_thread; //current user thread running

ucontext_t m_thread;

int lockIndex = 0; //index into lockInfoArray
int chanIndex = 0; //index into chanArray
int id_counter = 0; // keeps track of thread id's

struct lock_info *lockInfoArray[MAXLOCKS];
struct channel *chanArray[MAXLOCKS];

struct channel *ch;  // global channel

/*-------------STRUCTS-------------------*/
struct q_node {
  ucontext_t context;
  struct q_node *next;
  int id;
};

/*lock struct */
struct lock_info {
  struct q_node *threads_waiting; // queue of threads waiting on the lock
  talock_t lock; // the lock itself - index into lockInfoArray
  int status; // TALOCKED or TAUNLOCKED
  int owner_id;
};


/* channel struct */
struct channel {
  int val;
  struct q_node  *sender_q; //queue of senders on the channel
  struct q_node *receiver_q; //queue of receivers on the channel
  int sender_id; // current owner information
  int receiver_id;
  int send_flag;
  int rec_flag;
};

/* ------------------------Helper functions------------------------------ */
struct q_node *pop() {
  if (head == NULL) {
    //empty ready queue
    return NULL;
  } else if (head->next == NULL) {
    //one item on ready queue
    struct q_node *toReturn = head;
    head = NULL;
    tail = NULL;
    toReturn -> next = NULL;
    return toReturn;
  } else {
    //2 or more items on rq
    struct q_node *toReturn = head;
    head = head -> next;
    toReturn -> next = NULL;
    return toReturn;
  }
}

void push(struct q_node *n) {
  //empty ready queue
  if (head == NULL){
    head = n;
    tail = n;
    head -> next = NULL;
    tail -> next = NULL;
  } else  {
    //one or more items on rq
    tail -> next = n;
    tail = n;
    n -> next = NULL;
  }
}

struct q_node *var_push(struct q_node *n, struct q_node *head) {
    //push function that with variable head
    n -> next = NULL;
    if (head == NULL) {
      return n;
    } else {
      struct q_node *temp = head;
      while (temp -> next != NULL) {
        temp = temp -> next;
      }
    //  n -> next = NULL;
      temp -> next = n;
      return head;
    }
}

void print_queue(){ // print threads on ready queue
  printf("\n----------------------\n");
  printf("PRINTING READY QUEUE: \n");
  for (struct q_node *temp = head; temp != NULL; temp = temp->next) {
    printf("thread #%d \n", temp->id);
  }
  printf("-----------------------\n");
}

void print_waiting(struct q_node *n) { // print threads on waiting queue of lock
  printf("\n******************************\n");
  printf("PRINTING THREADS WAITING:\n");
  for (struct q_node *temp = n; temp != NULL; temp = temp -> next) {
    printf("thread #%d\n", temp->id);
  }
  printf("******************************\n");
  printf("\n\n");
}

// yeild without putting thread on ready queue
void give_up_CPU(void){
  if (cur_thread == NULL || head == NULL){
    return;
  }
  else {
    struct q_node *old = cur_thread;
    cur_thread = pop();
    swapcontext(&old->context,&cur_thread->context);
  }
}

int check_blocked_threads() {
  for (int i = 0; i < MAXLOCKS; i ++){
    struct lock_info *lock = lockInfoArray[i];
    if (lock != NULL ){
      if (lock->threads_waiting != NULL) {
        return -1;
      }
    }
  }
   for (int i = 0; i < MAXLOCKS; i++) {
     struct channel *chan = chanArray[i];
     if (chan != NULL) {
       if (chan->sender_q != NULL || chan->receiver_q != NULL) {
         return -1;
       }
     }
   }
   return 0;
 }

/* library functions */
void ta_libinit(void) {
  // ready queue
  head = NULL;
  tail = NULL;
  cur_thread = NULL;
}

void ta_create(void (*func)(void *), void *arg) {
  struct q_node *thread_node = malloc(sizeof(struct q_node));
  getcontext(&(thread_node->context));
  thread_node->next = NULL;
  thread_node->id = id_counter;
  id_counter++;
  unsigned char *stack = malloc(1024*128);
  thread_node->context.uc_stack.ss_sp = stack;
  thread_node->context.uc_stack.ss_size = 1024*128;
  thread_node->context.uc_link = &m_thread;
  makecontext(&(thread_node->context), (void (*)(void)) func, 1, arg);
  push(thread_node);
}

// cause the current thread to yield the CPU to the next runnable thread
void ta_yield(void) {
  if (cur_thread == NULL || head == NULL){
    return;
  } else {
    struct q_node *old = cur_thread;
    push(old);
    cur_thread = pop();
    swapcontext(&old->context,&cur_thread->context);
  }
}

int ta_waitall(void) {
  // go through ready queue, running each thread untill the queue is empty
  while (head != NULL) {
    cur_thread = pop();
    if (cur_thread == NULL) {
      break;
    }
    swapcontext(&(m_thread), &(cur_thread->context));
    free(cur_thread->context.uc_stack.ss_sp);
    free(cur_thread);
  }
    // check for blocked threads
    int returnVal = check_blocked_threads();
    return returnVal;
}

/* mutex lock library functions */
void ta_lock_init(talock_t *lock) {
    // allocating memory
    // put lock into array
    struct lock_info *newLockInfo = malloc(sizeof(struct lock_info));
    *lock = lockIndex;
    lockIndex++;
    newLockInfo->lock = lock;
    newLockInfo->status = TAUNLOCKED;
    lockInfoArray[*lock] = newLockInfo;
}

void ta_lock_destroy(talock_t *lock) {
    free(lockInfoArray[*lock]);
}


// set status to locked if unlocked; if locked, add thread to waiting queue
void ta_lock(talock_t *lock) {
    //gather info about lock
    struct lock_info *info = lockInfoArray[*lock];
    // if lock is locked, put thread on queue
    if (info->status == TALOCKED) {
      if (info->owner_id != cur_thread->id) {
        info->threads_waiting = var_push(cur_thread, info->threads_waiting);
        give_up_CPU(); //yeild without putting onto ready queue
      }
    }
    else if (info->status == TAUNLOCKED){
       info->status = TALOCKED;
    }
}

void ta_unlock(talock_t *lock) {
    struct lock_info *info = lockInfoArray[*lock];
    // queue is empty, flip flag
    if (info->threads_waiting == NULL){
      info->status = TAUNLOCKED;
    }
    else if (info->threads_waiting != NULL) {
      // pop from queue of waiting threads
      struct q_node *owner_thread = info->threads_waiting;
      info->owner_id = owner_thread->id;
      // incrememnt pointer on the lock's thread queue
      info->threads_waiting = info->threads_waiting->next;
      push(owner_thread);
    }
}


/* channel library functions */
void ta_chan_init(tachan_t *chan) {
  struct channel *newChan = malloc(sizeof(struct channel));
  newChan->sender_q = NULL;
  newChan->receiver_q = NULL;
  newChan->send_flag = 0;
  newChan->rec_flag = 0;
  *chan = chanIndex;
  chanIndex++;
  chanArray[*chan] = newChan;
}

void ta_chan_destroy(tachan_t *chan) {
  free(chanArray[*chan]);
}

int ta_chan_recv(tachan_t *chan) {
  printf("RECEIVE: ");
  struct channel *ch = chanArray[*chan];
  if (ch->sender_q == NULL && ch->receiver_q == NULL) {
    //no receiver and no sender - push self to receiver queue
    ch->receiver_q = var_push(cur_thread, ch->receiver_q);
    give_up_CPU();

  } else if (ch->sender_q != NULL && ch->receiver_q == NULL) {
    //sender but no receiver: invoke sender, then collect value
    struct q_node *sender = ch->sender_q;
    ch->sender_q = ch->sender_q->next;
    push(sender);
    push(cur_thread);

    give_up_CPU();

  } else if (ch->sender_q == NULL && ch->receiver_q != NULL) {
    //no sender, but receiver: push self to receiver QUEUE
    ch->receiver_q = var_push(cur_thread, ch->receiver_q);
    give_up_CPU();
  }
  else {
    //receiver and sender: invoke sender then receiver, push self to receiver queue
    struct q_node *sender = ch->sender_q;
    struct q_node *receiver = ch->receiver_q;
    ch->sender_q = ch->sender_q->next;
    ch->receiver_q = ch->receiver_q->next;
    push(receiver);
    push(sender);
    ch->receiver_q = var_push(cur_thread, ch->receiver_q);
    give_up_CPU();
  }
  return ch->val;
}

void ta_chan_send(tachan_t *chan, int value) {
  struct channel *ch = chanArray[*chan];
  if (ch->receiver_q == NULL && ch->sender_q == NULL) {
    //no receiver, no sender - push self to sender queue
    ch->val = value;
    ch->sender_q = var_push(cur_thread, ch->sender_q);
    give_up_CPU();

  } else if (ch->sender_q == NULL && ch->receiver_q != NULL) {
    //no sender but receiver - set the value, and invoke the receiver
    ch->val = value;
    struct q_node *receiver = ch->receiver_q;
    ch->receiver_q = ch->receiver_q->next;
    push(receiver);

  } else if (ch->sender_q != NULL && ch->receiver_q == NULL) {
    //sender, but no receiver - push self to sender queue, then give up cpu
    ch->sender_q = var_push(cur_thread, ch->sender_q);
    give_up_CPU();
    ch->val = value;
  } else {
    //both not NULL
    //sender and receiver - invoke both and push self to sender queue
    struct q_node *sender = ch->sender_q;
    struct q_node *receiver = ch->receiver_q;
    ch->sender_q = ch->sender_q->next;
    ch->receiver_q = ch->receiver_q->next;
    push(sender);
    ch->sender_q = var_push(cur_thread, ch->sender_q);
    give_up_CPU();
    ch->val = value;
  }
}
