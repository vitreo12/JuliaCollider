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

#pragma once

#include <thread>
#include <atomic>

#include "SC_PlugIn.hpp"
#include "JuliaGlobalState.h"
#include "JuliaObjectsArray.h"

/* Global variables needed for the execution of JuliaCollider */

/* INTERFACE TABLE */
static InterfaceTable* ft;

/* GLOBAL STATE */
extern JuliaGlobalState*   julia_global_state;

/* OBJECT COMPILER AND ARRAY OF JuliaObject* */
extern JuliaAtomicBarrier* julia_compiler_barrier;
extern JuliaObjectsArray*  julia_objects_array;

/* GC */
extern JuliaAtomicBarrier* julia_gc_barrier;
extern std::thread perform_gc_thread;
extern std::atomic<bool> perform_gc_thread_run;
extern int previous_pool_size;

/* GC and Compiler interaction (they are on two different threads).
It's been made this way in order to do future works where GC can happen at the same time
of RT thread, but not RT thread */
extern JuliaAtomicBarrier* julia_compiler_gc_barrier;

/* Active Julia UGen counter */
extern std::atomic<unsigned int> active_julia_ugens;

/* For edge cases only where GC is performing at a UGen destructor */
extern std::atomic<bool> gc_array_needs_emptying;
extern int gc_array_num;
extern jl_value_t** gc_array;

/* Julia mode */
//Start with debug mode. This is always set on RT thread, so no need for atomic here.
extern bool debug_or_perform_mode;