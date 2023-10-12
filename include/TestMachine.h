
#ifndef _MACHINE_H_
#define _MACHINE_H_

#include "RadixCache.h"
#include "DSM.h"
#include "Common.h"
#include "LocalLockTable.h"

#include <atomic>
#include <city.h>
#include <functional>
#include <map>
#include <algorithm>
#include <queue>
#include <set>
#include <iostream>
//One-sided Workers

//atomic CAS , read,

//node size
//retry time

//operation combine

//Two-sided Workers

//add two_sided multicast

class TestMachine {
public:
    TestMachine(TestDSM *dsm, int x, int y, int p, int q, int k, int z, int t) : dsm(dsm), x(x), y(y), p(p), q(q), k(k), z(z), t(t) {}
    TestMachine() = default;
    ~TestMachine() = default;

    uint16_t get_thread_id() {
        return dsm->getMyThreadID();
    }

    using MWorkFunc = std::function<void (TestMachine *, CoroContext *, int, int)>;
    void run_coroutine(MWorkFunc work_func, int coro_cnt, int req_num = 0);
    void read(CoroContext * ctx, int coro_id);
    void write(CoroContext * ctx, int coro_id);

// // n operations write (x atomicCAS (p% fail) y size t READ(q% retry) ) / read (y size t READ (q%retry)), add kn z sized two-sided RPC
private:
    void coro_worker(CoroYield &yield, MWorkFunc work_func, int coro_id);
    void coro_master(CoroYield &yield, int coro_cnt);


    TestDSM *dsm;
    int x;
    int y;
    int p;
    int q;
    int k; 
    int z; //B 
    int t; //B

    static thread_local CoroCall worker[MAX_CORO_NUM];
    static thread_local CoroCall master;
    static thread_local CoroQueue busy_waiting_queue;
};

class SenderMachine {
    
};

class ReceiverMachine {

};

#endif // _MACHINE_H_