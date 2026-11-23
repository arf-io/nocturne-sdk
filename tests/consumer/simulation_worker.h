#ifndef NOCTURNE_TEST_SIMULATION_WORKER_H
#define NOCTURNE_TEST_SIMULATION_WORKER_H

static int run_contract(void);

#ifdef NOCTURNE_TEST_PTHREAD
#include <pthread.h>
#include <stdio.h>

static void *run_on_worker(void *result)
{
    *(int *)result = run_contract();
    return NULL;
}
#endif

int main(void)
{
#ifdef NOCTURNE_TEST_PTHREAD
    /* Use the host's ordinary worker stack, including the musl default. */
    pthread_t thread;
    int result;
    if (pthread_create(&thread,NULL,run_on_worker,&result) || pthread_join(thread,NULL)) {
        fputs("Could not run the simulation contract on a worker thread.\n",stderr);
        return 1;
    }
    return result;
#else
    return run_contract();
#endif
}

#endif
