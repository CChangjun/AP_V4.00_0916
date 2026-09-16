// Pairmask.h
#pragma once
#include <atomic>
//! lock free 동기화ㅗㄹ, RMW 한 번에 하는듯 fetch는 
extern std::atomic<uint32_t> g_pairMask;
// bit i == peer[i].pairFlag
//* peerFlag update할때 같이 update해주고 걔를 쓰기 
static inline void PairMask_Set(int idx, bool on) 
{
    const uint32_t bit = (1u << idx);
    if (on)  g_pairMask.fetch_or(bit, std::memory_order_release);
    else     g_pairMask.fetch_and(~bit, std::memory_order_release);
}

static inline uint32_t PairMask_Snapshot() 
{
    return g_pairMask.load(std::memory_order_acquire);//cst로 갈수록, release 와는 반대로 해당 명령 뒤에 오는 모든 메모리 명령들이 해당 명령 위로 재배치 되는 것을 금지시킨단다
}

static inline bool PairMask_Test(uint32_t snap, int idx) 
{
    return (snap >> idx) & 1u;
}
