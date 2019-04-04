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