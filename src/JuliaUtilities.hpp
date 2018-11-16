#include "julia.h"

#pragma once

#define jl_get_module(name) \
            (jl_module_t*)jl_get_global(jl_main_module, jl_symbol(name))

//NON THREAD SAFE. When I am enabling back the GC, the audio thread (if creating a new object), might call into it
//Need a lock. jl_mutex_t crashes though. I think it is just for threading
#define perform_gc(full) \
            if(!jl_gc_is_enabled())\
            { \
                printf("-> Enabling GC...\n"); \
                jl_gc_enable(1); \
            } \
            printf("-> Performing GC...\n"); \
            jl_gc_collect(full); \
            printf("-> Completed GC\n"); \
            if(jl_gc_is_enabled) \
            { \
                printf("-> Disabling GC...\n"); \
                jl_gc_enable(0); \
            }

//This is the same as jl_call, but it doesn't perform the GC pushing and popping, since
//it will be called on objects that I know already that won't be picked up by
//the GC, as they are referenced in the global IdDict. This avoids the alloca() function
//that would be called in JL_GC_PUSHARGS.
//I should also take a look at the jl_invoke() function.
//args and nargs here take already in count that the first args[0] is the jl_function* to call.

//GOT RID OF JL_TRY and JL_CATCH. I should know already at this stage if the calls produce exceptions. REVIEW THOUGH.
//Might use JL_TRY and JL_CATCH, where JL_CATCH just fills the input buffer with zeros.
jl_value_t* jl_call_no_gc(jl_value_t** args, uint32_t nargs)
{
    jl_value_t* v;
    size_t last_age = jl_get_ptls_states()->world_age;
    jl_get_ptls_states()->world_age = jl_get_world_counter();
    
    v = jl_apply(args, nargs);
    
    jl_get_ptls_states()->world_age = last_age;
    jl_exception_clear();
    
    return v;
}