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

#include "GlobalVariables.h"

/* Initialization of the global variables */

/* GLOBAL STATE */
JuliaGlobalState*   julia_global_state = nullptr;

/* OBJECT COMPILER AND ARRAY OF JuliaObject* */
JuliaAtomicBarrier* julia_compiler_barrier = nullptr;
JuliaObjectsArray*  julia_objects_array = nullptr;

/* GC */
JuliaAtomicBarrier* julia_gc_barrier = nullptr;
std::thread perform_gc_thread;
std::atomic<bool> perform_gc_thread_run{false};
int previous_pool_size = 0;

/* GC and Compiler interaction (they are on two different threads).
It's been made this way in order to do future works where GC can happen at the same time
of RT thread, but not RT thread */
JuliaAtomicBarrier* julia_compiler_gc_barrier = nullptr;

/* Active Julia UGen counter */
std::atomic<unsigned int> active_julia_ugens{0};

/* For edge cases only where GC is performing at a UGen destructor */
std::atomic<bool> gc_array_needs_emptying{false};
int gc_array_num = 1000;
jl_value_t** gc_array = nullptr;

/* Julia mode */
//Start with debug mode. This is always set on RT thread, so no need for atomic here.
bool debug_or_perform_mode = false;