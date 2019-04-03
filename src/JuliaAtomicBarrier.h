#include <atomic>

#pragma once

/* SHOULD I RE-IMPLEMENT THIS BARRIER WITH std::atomic_flag INSTEAD OF std::atomic<bool>??? */

class AtomicBarrier
{
    public:
        AtomicBarrier()
        {
            barrier.store(false);
        }

        ~AtomicBarrier(){}

        void Spinlock();

        bool Trylock();

        void Unlock();

        bool get_barrier_value();

    private:
        std::atomic<bool> barrier{false};
};

class JuliaAtomicBarrier : public AtomicBarrier
{
    public:
        JuliaAtomicBarrier(){}
        ~JuliaAtomicBarrier(){}

        void NRTSpinlock();

        /* Used in RT thread. Returns true if compare_exchange_strong succesfully exchange the value. False otherwise. */
        bool RTTrylock();

        /* void Unlock(); */
};