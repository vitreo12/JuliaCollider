#include "JuliaFuncs.hpp"

struct Julia : public SCUnit 
{
public:
    Julia() 
    { 
        if(julia_initialized && sine_fun != nullptr)
        {   
            //If the gc_allocation_state is false, which means that the GC is not running, set
            //gc_allocation_state to true and run the object allocation function. 
            //Otherwise, if gc_allocation_state is true, it means that the GC is running, so don't make any calls.
            bool expected_val = false;
            bool allocation_state = gc_allocation_state.compare_exchange_strong(expected_val, true); //Set gc_allocation_state to true, busy, only if gc_allocation_state was false.
            if(!jl_gc_is_enabled() && allocation_state) //extra check to be sure the GC is disabled. 
            {
                //maybe avoid this and simply use the global id dict?
                object_id_dict = create_object_id_dict(global_id_dict, id_dict_function, set_index);

                args = (jl_value_t**)RTAlloc(mWorld, sizeof(jl_value_t*) * 6);

                args[0] = perform_fun; //already in id dict
                
                //Can't use jl_new_struct, as I need to call the constructor function. jl_new_struct would require me to know all the elements
                //to create the struct.
                sine = jl_call0(sine_fun); //create sine obj
                args[1] = sine;
                jl_call3(set_index, object_id_dict, sine, sine);
                
                args[2] = jl_box_float64(sampleRate());
                jl_call3(set_index, object_id_dict, args[2], args[2]);

                args[3] = jl_box_int32(bufferSize());
                jl_call3(set_index, object_id_dict, args[3], args[3]);

                args[4] = (jl_value_t*)jl_ptr_to_array_1d(jl_apply_array_type((jl_value_t*)jl_float32_type, 1), nullptr, bufferSize(), 0);
                jl_call3(set_index, object_id_dict, args[4], args[4]);

                args[5] = jl_box_float64((double)440.0);
                jl_call3(set_index, object_id_dict, args[5], args[5]);

                //execute = successful object creation.
                execute = true;

                //reset gc_allocation_state to false. 
                gc_allocation_state = false; //operation is atomic
                //gc_allocation_state.compare_exchange_strong(true_value, false); //(compare_exchange_strong here, perhaps?)
            }
            else
                Print("WARNING: Julia's GC is running. Object was not created.\n");
        }
        else
            Print("WARNING: Julia not initialized or function not defined\n");


        set_calc_function<Julia, &Julia::next>();
        next(1);
    }

    ~Julia() 
    {
        if(execute == true)
        {
            delete_object_id_dict(global_id_dict, object_id_dict, delete_index);
            RTFree(mWorld, args);
            //JuliaGC(mWorld, nullptr, nullptr, nullptr);
        }
    }

private:
    jl_value_t* sine;
    jl_value_t** args;
    jl_value_t* object_id_dict;
    bool execute = false;

    inline void next(int inNumSamples) 
    {
        double input = (double)(in0(0));
        float* output = out(0);

        if(execute)
        {
            *(int*)jl_data_ptr(args[3]) = inNumSamples;
            //point julia buffer to correct audio buffer.
            ((jl_array_t*)(args[4]))->data = output;
            //set frequency
            *(double*)jl_data_ptr(args[5]) = input;

            jl_call_no_gc(args, 6);
        }
        else
        {
            for (int i = 0; i < inNumSamples; i++) 
                output[i] = 0.0f;
        }
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