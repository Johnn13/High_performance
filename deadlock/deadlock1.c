#define _GNU_SOURCE
#include <dlfcn.h>

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define BACKWARD_HAS_DW 1
#include "deadlock_detetor.h"

/*  
通过重载加锁和解锁的系统函数，可以看到哪些线程持有锁，想要获得哪些锁
gcc deadlock.c -o deadlock -g -lpthread -g -ldl

sudo apt install libdwarf-dev
*/

typedef int(*pthread_mutex_lock_t)(pthread_mutex_t *mutex);
pthread_mutex_lock_t pthread_mutex_lock_f = NULL;

typedef int(*pthread_mutex_unlock_t)(pthread_mutex_t *mutex);
pthread_mutex_unlock_t pthread_mutex_unlock_f = NULL;

pthread_mutex_t mtx1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx2 = PTHREAD_MUTEX_INITIALIZER;

int pthread_mutex_lock(pthread_mutex_t *mtx)
{
    printf("Before pthread_mutex_lock %ld, %p\n", pthread_self(), mtx);

    pthread_mutex_lock_f(mtx);

    printf("Afterpthread_mutex_lock \n");
}

int pthread_mutex_unlock(pthread_mutex_t *mtx)
{
    printf("Before pthread_mutex_unlock %ld, %p\n", pthread_self(), mtx);

    pthread_mutex_unlock_f(mtx);
}

void init_hook(void)
{
    if(!pthread_mutex_lock_f)
    {
        pthread_mutex_lock_f = dlsym(RTLD_NEXT, "pthread_mutex_lock");
    }

    if(!pthread_mutex_unlock_f)
    {
        pthread_mutex_unlock_f = dlsym(RTLD_NEXT, "pthread_mutex_unlock");
    }
}

void* t1_cb(void *arg)
{
    pthread_mutex_lock(&mtx1);
    sleep(1);
    pthread_mutex_lock(&mtx2);
    
    pthread_mutex_unlock(&mtx2);
    pthread_mutex_unlock(&mtx1);
}

void* t2_cb(void *arg)
{
    pthread_mutex_lock(&mtx2);
    sleep(1);
    pthread_mutex_lock(&mtx1);

    pthread_mutex_unlock(&mtx1);
    pthread_mutex_unlock(&mtx2);
}

int main()
{

    DeadLockGraphic::getInstance().start_check();
    init_hook();
    pthread_t t1, t2;

    pthread_create(&t1, NULL, t1_cb, NULL);
    pthread_create(&t2, NULL, t2_cb, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    return 0;
}