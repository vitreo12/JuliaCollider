#include "JuliaFuncs.hpp"

struct Julia : public SCUnit 
{
public:
    Julia() 
    {         
        printf("Object gc_allocation_state BEFORE: %i\n", julia_gc_barrier.get_barrier_value());
        
        bool gc_state = julia_gc_barrier.RTChecklock();
        
        if(gc_state) 
        {
            printf("Object gc_allocation_state MIDDLE: %i\n", julia_gc_barrier.get_barrier_value());

            julia_gc_barrier.RTUnlock();
        }
        else
            Print("WARNING: Julia's GC is running. Object was not created.\n");


        /* Changes here: If object couldn't get the lock on the atomic bool before, have another ::next function which would simply
        check at every buffer size if the lock has been freed. If that's the case, allocate the object that it wasn't
        previously allowed to allocate. Objects are allocated in the buffer period anyway, so it would just delay the allocation. */
        set_calc_function<Julia, &Julia::next>();
        next(1);
    }

    ~Julia() 
    {
        
    }

private:
    bool execute = false;

    inline void next(int inNumSamples) 
    {
        double input = (double)(in0(0));
        float* output = out(0);

        for (int i = 0; i < inNumSamples; i++) 
            output[i] = 0.0f;
    }
};

PluginLoad(JuliaUGens) 
{
    #ifdef __linux__
        open_julia_shared_library();
    #endif
    
    ft = inTable;
    registerUnit<Julia>(ft, "Julia");

    DefineJuliaCmds();
}

//Destructor function called when the shared library Julia.so gets unloaded from server (when server is quitted)
void julia_destructor(void) __attribute__((destructor));
void julia_destructor(void)
{
    if(jl_is_initialized())
    {
        printf("-> Quitting Julia..\n");
        delete_global_id_dict();
        
        //Since now GC lives inside of SC's allocator, there is no need to run a gc run, as all the memory will be collected anyway.
        //Not entirely true, as the GC also might collect memory from wrappers around external malloc() calls.
        //perform_gc(1);
        
        jl_atexit_hook(0); //on linux it freezes here
    }
    
    #ifdef __linux__
        //close handle to libjulia.so. It is probably not needed as Julia.so is unloaded anyway, and with it also the handle to libjulia.so
        dlclose(handle);
    #endif
    
}