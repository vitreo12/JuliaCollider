/*
    JuliaCollider: Julia's JIT compilation for low-level audio synthesis and prototyping in SuperCollider.
    Copyright (C) 2019 Francesco Cameli. All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <atomic>

#pragma once

/* Spinlock and Trylock classes */

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
        std::atomic<bool> barrier{false}; //Should it be atomic_flag instead? Would it be faster?
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