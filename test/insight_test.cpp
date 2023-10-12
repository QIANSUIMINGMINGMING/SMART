// n operations write (x atomicCAS (p% fail) y READ(q% retry) ) / read (y READ (q%retry)), add kn z sized two-sided RPC

//Experiment 1 fixed all set add two-sided communication (The Cost)
    // 1. change k
    // 2. change z

//Experiment 2 fixed k and z, change one-sided communication (The gain) 
    // 1. change x and y
    // 2. change p and q

//Experiment 3 fixed one-sided and two-sided operation, change number of cores

#include <city.h>

#include <stdlib.h>
#include <thread>
#include <time.h>
#include <vector>
#include <iostream>
#include <string>
#include <fstream>
#include <random>
#include "TestMachine.h"
#include "Timer.h"

#define TIME_INTERVAL 0.5
#define TEST_EPOCH 10

std::thread th[MAX_APP_THREAD];
uint64_t tp[MAX_APP_THREAD][MAX_CORO_NUM];

extern volatile bool need_stop;

int kThreadCount;
int kNodeCount;
int kCoroCnt = 8;

TestMachine *machine;
TestDSM *dsm;

Timer bench_timer;
std::atomic<int64_t> warmup_cnt{0};
std::atomic_bool ready{false};

void work_func(TestMachine * machine , CoroContext *ctx, int coro_id, int op_rate = 50) {
    std::random_device rd;     
    std::mt19937 rng(rd());    
    std::uniform_int_distribution<int> uni(0, 100);

    int random_integer = uni(rng);

    if (random_integer < op_rate) {
        machine->write(ctx, coro_id);
    } else {
        machine->read(ctx, coro_id);
    }
}

void thread_run(int id) {
    bindCore(id); // ignore NUMA effect 

    uint64_t my_id; 

    dsm->registerThread();
    my_id = dsm->getMyNodeID() * kThreadCount + id;
    printf("I am %lu\n", my_id);

    if(id == 0) {
        bench_timer.begin();
    }

    warmup_cnt.fetch_add(1);

    if (id == 0) {
        while (warmup_cnt.load() != kThreadCount)
            ;
        printf("node %d finish\n", dsm->getMyNodeID());
        dsm->barrier("warm_finish");

        uint64_t ns = bench_timer.end();
        printf("warmup time %lds\n", ns / 1000 / 1000 / 1000);

        ready = true;
        warmup_cnt.store(-1);
  }

  while (warmup_cnt.load() != -1)
    ;

  machine->run_coroutine(work_func, kCoroCnt);
  printf("thread %d exit.\n", id);
}

void parse_args(int argc, char *argv[]) {
  kNodeCount = atoi(argv[1]);
  kThreadCount = atoi(argv[2]);
  kCoroCnt = atoi(argv[3]);

  printf("kNodeCount %d, kThreadCount %d, kCoroCnt %d\n", kNodeCount, kThreadCount, kCoroCnt);
}

int main(int argc, char *argv[]) {
    parse_args(argc, argv);

    DSMConfig config;
    assert(kNodeCount >= MEMORY_NODE_NUM);
    config.machineNR = kNodeCount;
    config.threadNR = kThreadCount;
    dsm = TestDSM::getInstance(config);
    bindCore(kThreadCount);

    for (int i = 0; i < kThreadCount; i ++) {
        th[i] = std::thread(thread_run, i);
    }

    while (!ready.load())
        ;
    
    timespec s, e;
    uint64_t pre_tp = 0;
    int count = 0;

    clock_gettime(CLOCK_REALTIME, &s);
    while (!need_stop) {
        sleep(TIME_INTERVAL); //TIMEINTERVAL (thread doing sth)
        clock_gettime(CLOCK_REALTIME, &e);

        int microseconds = (e.tv_sec - s.tv_sec) * 1000000 +
                       (double)(e.tv_nsec - s.tv_nsec) / 1000;

        //calculate stastical numbers

        uint64_t all_tp = 0;
        for (int i = 0; i < MAX_APP_THREAD; ++i) {
            for (int j = 0; j < kCoroCnt; ++j)
                all_tp += tp[i][j];
        }

        uint64_t cap = all_tp - pre_tp;
        pre_tp = all_tp;

        double per_node_tp = cap * 1.0 / microseconds;
        uint64_t cluster_tp = dsm->sum((uint64_t)(per_node_tp * 1000));  // only node 0 return the sum

        printf("%d, throughput %.4f\n", dsm->getMyNodeID(), per_node_tp);

        if (dsm->getMyNodeID() == 0) {
            printf("epoch %d passed!\n", count);
            printf("cluster throughput %.3f Mops\n", cluster_tp / 1000.0);
            printf("\n\n");
        }
        if (count >= TEST_EPOCH) {
            need_stop = true;
        }
        clock_gettime(CLOCK_REALTIME, &s);
    }

    printf("[END]\n");
    for (int i = 0; i < kThreadCount; i++) {
        th[i].join();
        printf("Thread %d joined.\n", i);
    }    

    dsm->barrier("fin");

    return 0;
}