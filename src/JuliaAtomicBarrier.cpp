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

#include "JuliaAtomicBarrier.h"

/* Spinlock and Trylock classes */

void AtomicBarrier::Spinlock()
{
    bool expected_val = false;
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