#include "JuliaAtomicBarrier.h"

void AtomicBarrier::Spinlock()
{
    bool expected_val = false;
    //Spinlock. Wait ad-infinitum until the RT thread has set barrier to false.
    //SHOULD IT BE compare_exchange_strong for extra sureness???
    while(!barrier.compare_exchange_weak(expected_val, true))
        expected_val = false; //reset expected_val to false as it's been changed in compare_exchange_weak to true
}

/* Used in RT thread. Returns true if compare_exchange_strong succesfully exchange the value. False otherwise. */
bool AtomicBarrier::Trylock()
{
    bool expected_val = false;
    return barrier.compare_exchange_strong(expected_val, true);
}

void AtomicBarrier::Unlock()
{
    barrier.store(false);
}

bool AtomicBarrier::get_barrier_value()
{
    return barrier.load();
}

void JuliaAtomicBarrier::NRTSpinlock()
{
    Spinlock();
}

bool JuliaAtomicBarrier::RTTrylock()
{
    return Trylock();
}

/* void Unlock()
{
    AtomicBarrier::Unlock();
} */