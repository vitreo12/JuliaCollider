#include "JuliaFuncs.hpp"

struct Julia : public SCUnit 
{
public:
    Julia() 
    {         
        printf("Object gc_allocation_state BEFORE: %i\n", julia_gc_barrier->get_barrier_value());
        
        bool gc_state = julia_gc_barrier->RTChecklock();
        
        if(gc_state) 
        {
            printf("Object gc_allocation_state MIDDLE: %i\n", julia_gc_barrier->get_barrier_value());

            julia_gc_barrier->RTUnlock();
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
    ft = inTable;
    registerUnit<Julia>(ft, "Julia");

    DefineJuliaCmds();
}

//Destructor function called when the shared library Julia.so gets unloaded from server (when server is quitted)
void julia_destructor(void) __attribute__((destructor));
void julia_destructor(void)
{
    if(jl_is_initialized() && !julia_global_state->is_initialized())
    {
        jl_atexit_hook(0);
        return;
    }
    
    if(julia_global_state->is_initialized())
    {
        delete julia_global_state;
        delete julia_gc_barrier;
    }
}