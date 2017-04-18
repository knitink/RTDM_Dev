
#include <pthread.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdint>
#include <csignal>
#include <stdio.h>
#include <atomic>
#include <ctime>
#include <cassert>
#include <iostream>



/* Simple benchmarking too that launches 2 threads that atomically increment a variable.
 * Either a local variable or an atomic shared between the threads. Usefull to gauge
 * the extra overhead generated by the inter-thread communication.
 */

/* The type to use for the shared variable */
typedef int TestType;


/* If defined, the 2 threads increment the same atomic variable
 * If not, they use local variables */
#define USE_SHARED_VARIABLE

constexpr int INTERATIONS_PER_WAKEUP = 10000;
constexpr int WAKEUPS = 100000;

bool running = true;

struct ReturnData
{
    int counts;
    int64_t nanosecs;
};

void signal_handler(int sig_number)
{
    running = false;
}

void* realtime_thread(void* s)
{
    int64_t rt_count=0, total_time = 0;
    struct timespec start,ts;
    struct timespec end;
	ts.tv_sec = 0;
    ts.tv_nsec = 10000; /* 10 us */

#ifdef USE_SHARED_VARIABLE
    std::atomic<TestType>* count = static_cast<std::atomic<TestType>*>(s);
#else
    std::atomic<TestType> local_data(0);
    std::atomic<TestType>* count = &local_data;
#endif
    ReturnData* return_value = new ReturnData;
    while (running)
    {
        clock_gettime(CLOCK_MONOTONIC , &start);
        for (int i = 0; i < INTERATIONS_PER_WAKEUP; ++i)
        {   
            (*count)++; 
			rt_count++;
        }

        clock_gettime(CLOCK_MONOTONIC , &end);
        int64_t timediff = (end.tv_nsec - start.tv_nsec + (end.tv_sec - start.tv_sec) * 1000000000);
        total_time += timediff;

        clock_nanosleep(CLOCK_MONOTONIC , 0, &ts, NULL);
    }
    //return_value->counts = count->load();
	return_value->counts = rt_count;
    return_value->nanosecs = total_time;
    pthread_exit(static_cast<void*>(return_value));
}


int main()
{
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    std::atomic<TestType> count(0);
    assert(count.is_lock_free());
	struct sched_param rtparam = { .sched_priority = 99 };
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&attributes, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attributes, SCHED_FIFO);
    pthread_attr_setschedparam(&attributes, &rtparam);
	
    pthread_t rt_thread;
    pthread_create(&rt_thread, &attributes, &realtime_thread, static_cast<void*>(&count));

    int64_t nrt_count=0, total_time = 0;
    struct timespec start,ts;
    struct timespec end;
	ts.tv_sec = 0;
    ts.tv_nsec = 10000; /* 10 us */

    for (int w = 0; w < WAKEUPS; ++w)
    {
        clock_gettime(CLOCK_MONOTONIC , &start);
    	for (int i = 0; i < INTERATIONS_PER_WAKEUP; ++i)
        {
            count++;
			nrt_count++;
        }
        clock_gettime(CLOCK_MONOTONIC , &end);
        int64_t timediff = (end.tv_nsec - start.tv_nsec + (end.tv_sec - start.tv_sec) * 1000000000);
        total_time += timediff;

        clock_nanosleep(CLOCK_MONOTONIC , 0, &ts, NULL);
        if (!running)
        {
            break;
        }
    }
    running = false;
    ReturnData* return_value;
    int ret = pthread_join(rt_thread, reinterpret_cast<void**>(&return_value));

    std::cout << "Time 1: " << total_time << "ns, " << total_time/1000000 << "ms, time 2: " 
            << return_value->nanosecs << "ns, " << return_value->nanosecs/1000000 << "ms. " 
            << "Total counts: " << count << "\n NRT count: "<<nrt_count<< "\n RT count: "<<return_value->counts << std::endl;
}