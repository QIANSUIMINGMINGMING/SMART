#include "TestMachine.h"
#include "RdmaBuffer.h"
#include "Timer.h"
#include "Node.h"

#include <algorithm>
#include <city.h>
#include <iostream>
#include <queue>
#include <utility>
#include <vector>
#include <atomic>
#include <mutex>

uint64_t latency[MAX_APP_THREAD][MAX_CORO_NUM][LATENCY_WINDOWS];

thread_local CoroCall TestMachine::worker[MAX_CORO_NUM];
thread_local CoroCall TestMachine::master;
thread_local CoroQueue TestMachine::busy_waiting_queue;

volatile bool need_stop = false;

void TestMachine::run_coroutine(MWorkFunc work_func, int coro_cnt, int req_num) {
  using namespace std::placeholders;

  assert(coro_cnt <= MAX_CORO_NUM);
  for (int i = 0; i < coro_cnt; ++i) {
    worker[i] = CoroCall(std::bind(&TestMachine::coro_worker, this, _1, work_func, i));
  }

  master = CoroCall(std::bind(&TestMachine::coro_master, this, _1, coro_cnt));

  master();
}


void TestMachine::coro_worker(CoroYield &yield, MWorkFunc work_func, int coro_id) {
  CoroContext ctx;
  ctx.coro_id = coro_id;
  ctx.master = &master;
  ctx.yield = &yield;

  Timer coro_timer;
  auto thread_id = dsm->getMyThreadID();

  while (!need_stop) {
    coro_timer.begin();
    work_func(this, &ctx, coro_id, 50);
    auto us_10 = coro_timer.end() / 100;

    if (us_10 >= LATENCY_WINDOWS) {
      us_10 = LATENCY_WINDOWS - 1;
    }
    latency[thread_id][coro_id][us_10]++;
  }
}

void TestMachine::coro_master(CoroYield &yield, int coro_cnt) {
  for (int i = 0; i < coro_cnt; ++i) {
    yield(worker[i]);
  }
  while (!need_stop) {
    uint64_t next_coro_id;

    if (dsm->poll_rdma_cq_once(next_coro_id)) {
      yield(worker[next_coro_id]);
    }
    // uint64_t wr_ids[POLL_CQ_MAX_CNT_ONCE];
    // int cnt = dsm->poll_rdma_cq_batch_once(wr_ids, POLL_CQ_MAX_CNT_ONCE);
    // for (int i = 0; i < cnt; ++ i) {
    //   yield(worker[wr_ids[i]]);
    // }

    if (!busy_waiting_queue.empty()) {
    // int cnt = busy_waiting_queue.size();
    // while (cnt --) {
      auto next = busy_waiting_queue.front();
      busy_waiting_queue.pop();
      next_coro_id = next.first;
      if (next.second()) {
        yield(worker[next_coro_id]);
      }
      else {
        busy_waiting_queue.push(next);
      }
    }
  }
}


void TestMachine::read(CoroContext * ctx, int coro_id) {
  assert(dsm->is_register());
  GlobalAddress start_ptr = dsm->getDSMBaseAddr();
  int read_time = y;

retry:
  while (read_time --) {
    GlobalAddress p_ptr = GADD(start_ptr, rand() % define::dsmSize * define::GB); // pointer to a random place
    auto read_buffer = (dsm->get_rbuf(coro_id)).get_read_buffer();
    dsm->read_sync(read_buffer, p_ptr, define::allocationReadSize, ctx);
    // random retry with q
    if (rand() % 100 < q) {
      read_time ++;
      goto retry;
    }
  }
}

void TestMachine::write(CoroContext *ctx, int coro_id) {
  assert(dsm->is_register());
  GlobalAddress start_ptr;
  int write_time = x;
  int read_time = y;

Rretry:
  while (read_time --) {
    GlobalAddress p_ptr = GADD(start_ptr, rand() % define::dsmSize * define::GB); // pointer to a random place
    auto read_buffer = (dsm->get_rbuf(coro_id)).get_read_buffer();
    dsm->read_sync(read_buffer, p_ptr, define::allocationReadSize, ctx);
    // random retry with q
    if (rand() % 100 < q) {
      read_time ++;
      goto Rretry;
    }
  }

Wretry:
  while (write_time --) {
    GlobalAddress p_ptr = GADD(start_ptr, rand() % define::dsmSize * define::GB); // pointer to a random place
    auto cas_buffer = (dsm->get_rbuf(coro_id)).get_cas_buffer();
    dsm->cas_sync(p_ptr, 0, 1, cas_buffer, ctx);
    // random retry with p
    if (rand() % 100 < p) {
      write_time ++;
      goto Wretry;
    }
  }
}