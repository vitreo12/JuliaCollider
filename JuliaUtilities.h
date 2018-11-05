#include "julia.h"

#define jl_get_module(name) \
            (jl_module_t*)jl_get_global(jl_main_module, jl_symbol(name))

#define perform_gc() \
            if(!jl_gc_is_enabled())\
            { \
                printf("-> Enabling GC...\n"); \
                jl_gc_enable(1); \
            }\
            printf("-> Performing GC...\n"); \
            jl_gc_collect(1); \
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
jl_value_t* jl_call_no_gc(jl_value_t** args, uint32_t nargs)
{
    jl_value_t* v;
    JL_TRY {
        size_t last_age = jl_get_ptls_states()->world_age;
        jl_get_ptls_states()->world_age = jl_get_world_counter();
        
        v = jl_apply(args, nargs);
        
        jl_get_ptls_states()->world_age = last_age;
        jl_exception_clear();
    }
    JL_CATCH {
        v = nullptr;
    }
    
    return v;
}