#include "JuliaFuncs.hpp"

struct Julia : public SCUnit 
{
public:
    Julia() 
    { 
        if(julia_initialized)
        {
            //ONLY Called once.
            //This means that, even if changed the reference script, the synth will still play using the old references, which are stored in args anyway.
            //Also, if synth is still playing, the object id dict is still pushed to the global one, keeping all the references alive.

            //ONLY EVER CREATE A NEW OBJECT IS GC IS DISABLED, meaning that it is now safe to call into it.
            //This is not safe yet, because while in the middle of this creation of objects, GC could be disabled again by the
            //NRT thread if there is a call there. Need a way to prevent the calls from either ways if one of them is dealing with the GC.
            if(sine_fun != nullptr && !jl_gc_is_enabled())
            {
                //maybe avoid this and simply use the global id dict?
                object_id_dict = create_object_id_dict(global_id_dict, id_dict_function, set_index);

                args = (jl_value_t**)RTAlloc(mWorld, sizeof(jl_value_t*) * 5);

                args[0] = perform_fun; //already in id dict
                
                //I should make NO GC functions to jl_call0, 1, 2, 3, 4....
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

                noise_args = (jl_value_t**)RTAlloc(mWorld, 2 * sizeof(jl_value_t*));
                noise_args[0] = give_me_noise_fun;
                noise_args[1] = jl_box_float64((double)0.8574); 
    
                execute = true;
            }
            else
                Print("WARNING: NO FUNCTION DEFINITION\n");
        }
        else
            Print("WARNING: Julia not initialized\n");


        set_calc_function<Julia, &Julia::next>();
        next(1);
    }

    ~Julia() 
    {
        if(execute == true)
        {
            delete_object_id_dict(global_id_dict, object_id_dict, delete_index);
            RTFree(mWorld, args);
            RTFree(mWorld, noise_args);
            //JuliaGC(mWorld, nullptr, nullptr, nullptr);
        }
    }

private:
    jl_value_t* sine;
    jl_value_t** args;
    jl_value_t* object_id_dict;

    jl_value_t** noise_args;
    
    bool execute = false;
    
    bool just_reallocated = false;

    inline void next(int inNumSamples) 
    {
        if(execute)
        {            
            if(julia_barrier.Checklock())
            {
                //If function changed, allocate it new object
                if(args[0] != perform_fun)
                {
                    args[0] = perform_fun;

                    sine = jl_call_no_gc(&sine_fun, 1); //create sine obj
                    args[1] = sine;
                    jl_call3(set_index, object_id_dict, sine, sine);

                    just_reallocated = true;
                }

                //If not new object, run, otherwise wait for next cycle
                if(!just_reallocated)
                {
                    julia_perform_fun(inNumSamples);
                    julia_barrier.Unlock();
                    return;
                }
            
                silence_fun(inNumSamples);
                just_reallocated = false;
                julia_barrier.Unlock();
                return;
            }

            silence_fun(inNumSamples);
        }
    }

    inline void julia_perform_fun(int inNumSamples)
    {
        double input = (double)(in0(0));
        float* output = out(0);

        *(int*)jl_data_ptr(args[3]) = inNumSamples;
        //point julia buffer to correct audio buffer.
        ((jl_array_t*)(args[4]))->data = output;
        //set frequency
        *(double*)jl_data_ptr(args[5]) = input;

        //jl_get_ptls_states()->world_age = jl_get_world_counter();
        jl_call_no_gc(args, 6);
    }
        
    inline void silence_fun(int inNumSamples)
    {
        for(int i = 0; i < numOutputs(); i++)
        {
            for (int y = 0; y < inNumSamples; y++) 
                out(i)[y] = 0.0f;
        }
    }
};

PluginLoad(JuliaUGens) 
{
    ft = inTable;
    registerUnit<Julia>(ft, "Julia");

    DefineJuliaCmds();

    #ifdef __linux__
        open_julia_shared_library();
    #endif
}

//Destructor function called when the shared library Julia.so gets unloaded from server (when server is quitted)
void julia_destructor(void) __attribute__((destructor));
void julia_destructor(void)
{
    if(jl_is_initialized())
    {
        printf("-> Quitting Julia..\n");
        delete_global_id_dict();
        perform_gc(1);
        jl_atexit_hook(0);
    }

    #ifdef __linux__
        //close handle to libjulia.so. It is probably not needed as Julia.so is unloaded anyway, and with it also the handle to libjulia.so
        dlclose(handle);
    #endif
}