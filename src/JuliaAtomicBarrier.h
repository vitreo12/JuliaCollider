#include <atomic>

#pragma once

/* SHOULD I RE-IMPLEMENT THIS BARRIER WITH std::atomic_flag INSTEAD OF std::atomic<bool>??? 
IT MIGHT BE FASTER!!!!!!!!! */

class AtomicBarrier
{
    public:
        AtomicBarrier(){}
        ~AtomicBarrier(){}

        /* To be called from NRT thread only. */
        void Spinlock();

        /* Used in RT thread. Returns true if compare_exchange_strong succesfully exchange the value. False otherwise. */
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

        bool RTTrylock();

        /* inline void Unlock(); */
};