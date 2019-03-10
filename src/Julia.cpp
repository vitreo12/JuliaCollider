#include "JuliaFuncs.hpp"

jl_value_t* jl_call_no_gc(jl_value_t** args, uint32_t nargs)
{
    jl_value_t* v;
    //size_t last_age = jl_get_ptls_states()->world_age;
    //jl_get_ptls_states()->world_age = jl_get_world_counter();
    
    v = jl_apply(args, nargs);
    
    //jl_get_ptls_states()->world_age = last_age;
    jl_exception_clear();
    
    return v;
}

struct Julia : public SCUnit 
{
public:
    Julia() 
    {         
        if(!julia_global_state->is_initialized())
        {
            Print("WARNING: Julia hasn't been booted correctly \n");
            set_calc_function<Julia, &Julia::output_silence>();
            return;
        }

        int julia_object_unique_id = (int)in0(0);
        Print("UNIQUE ID: %i\n", julia_object_unique_id);

        bool array_state = retrieve_julia_object(julia_object_unique_id);
        if(!array_state)
        {
            Print("WARNING: JuliaObjectArray is resizing. Object creation deferred.\n");
            set_calc_function<Julia, &Julia::next_NRT_busy>();  
            return;
        }

        bool gc_state = julia_gc_barrier->RTChecklock();
        if(!gc_state) 
        {
            Print("WARNING: Julia's GC is running. Object creation deferred.\n");
            set_calc_function<Julia, &Julia::next_NRT_busy>();  
            return;
        }

        bool successful_allocation = allocate_julia_args();
        if(!successful_allocation)
        {
            Print("ERROR: Could not allocate UGen \n");
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->RTUnlock();
            return;
        }

        bool successful_ugen_ref = add_ugen_ref_to_global_object_id_dict();
        if(!successful_ugen_ref)
        {
            Print("ERROR: Could not allocate __UGenRef__ \n");
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->RTUnlock();
            return;
        }

        //Unlock the gc barrier
        julia_gc_barrier->RTUnlock();
        
        //set_calc_function already does one sample of audio.
        set_calc_function<Julia, &Julia::next_julia_code>();
    }

    ~Julia() 
    {
        if(julia_object)
        {
            if(julia_object->compiled)
                julia_object->RT_busy = false;
        }

        if(args)
            free_args();

        if(ins && outs)
            free_ins_outs();
    }

private:
    JuliaObject* julia_object = nullptr;
    int32_t nargs = 6;
    jl_value_t** args;
    jl_value_t** ins;
    jl_value_t** outs;

    int count = 0;

    inline bool retrieve_julia_object(int id)
    {
        bool successful_retrieval = julia_objects_array->get_julia_object(id, &julia_object);
        if(!successful_retrieval)
            return false;
        
        if(!julia_object->compiled)
            return false;

        //Set it to be busy from RT thread. Should it be atomic? 
        julia_object->RT_busy = true;

        return true;
    }

    inline void alloc_args()
    {
        args = (jl_value_t**)RTAlloc(mWorld, nargs * sizeof(jl_value_t*));
    }

    inline void free_args()
    {
        RTFree(mWorld, args);
    }

    inline void free_ins_outs()
    {
        RTFree(mWorld, ins);
        RTFree(mWorld, outs);
    }

    inline bool allocate_julia_args()
    {      
        /* Create __UGen__ for this julia_object */
        jl_function_t* ugen_constructor_fun = julia_object->constructor_fun;
        if(!ugen_constructor_fun)
        {
            Print("ERROR: Invalid __constructor__ function \n");
            return false;
        }

        jl_method_instance_t* ugen_constructor_instance = julia_object->constructor_instance;
        if(!ugen_constructor_instance)
        {
            Print("ERROR: Invalid __constructor__ instance \n");
            return false;
        }
        
        jl_value_t* ugen_object = jl_invoke_already_compiled_SC(ugen_constructor_instance, &ugen_constructor_fun, 1);
        if(!ugen_object)
        {
            Print("ERROR: Invalid __UGen__ object \n");
            return false;
        }

        jl_function_t* ugen_perform_fun = julia_object->perform_fun;
        if(!ugen_perform_fun)
        {
            Print("ERROR: Invalid __constructor__ function \n");
            return false;
        }

        jl_value_t* buf_size = jl_box_int32(bufferSize());
        if(!buf_size)
        {
            Print("ERROR: Invalid __buf_size__ argument \n");
            return false;
        }

        /* ARGS MEMORY */
        alloc_args();
        if(!args)
        {
            Print("WARNING: Could not allocate memory for UGen \n");
            return false;
        }
        
        /* INPUTS / OUTPUTS */
        /* Excluding first input, as it is the id number */
        size_t ugen_num_of_inputs = numInputs() - 1;

        jl_value_t* ins_vector = (jl_value_t*)jl_alloc_array_1d(julia_global_state->get_vector_of_vectors_float32(), ugen_num_of_inputs);
        ins = (jl_value_t**)RTAlloc(mWorld, sizeof(jl_value_t*) * ugen_num_of_inputs);
        if(!ins_vector || !ins)
        {
            free_args();
            Print("WARNING: Could not allocate memory for inputs \n");
            return false;
        }

        for(int i = 0; i < ugen_num_of_inputs; i++)
        {
            //Using nullptrs here. Real pointers will be set at each buffer cycle to SC ins[channel] buffer.
            ins[i] = (jl_value_t*)jl_ptr_to_array_1d(julia_global_state->get_vector_float32(), nullptr, bufferSize(), 0);

            /* I should have a method outstance for this set index call aswell. It's not compiled here */
            jl_call3(julia_global_state->get_set_index_fun(), ins_vector, ins[i], jl_box_int32(i + 1)); //Julia index from 1 onwards
        }

        jl_value_t* outs_vector = (jl_value_t*)jl_alloc_array_1d(julia_global_state->get_vector_of_vectors_float32(), numOutputs());
        outs = (jl_value_t**)RTAlloc(mWorld, sizeof(jl_value_t*) * numOutputs());
        if(!outs || !outs_vector)
        {
            free_args();
            RTFree(mWorld, outs);
            Print("WARNING: Could not allocate memory for outputs \n");
            return false;
        }

        for(int i = 0; i < numOutputs(); i++)
        {
            //Using nullptrs here. Real pointers will be set at each buffer cycle to SC outs[channel] buffer.
            outs[i] = (jl_value_t*)jl_ptr_to_array_1d(julia_global_state->get_vector_float32(), nullptr, bufferSize(), 0);

            /* I should have a method outstance for this set index call aswell. It's not compiled here */
            jl_call3(julia_global_state->get_set_index_fun(), outs_vector, outs[i], jl_box_int32(i + 1)); //Julia index from 1 onwards
        }

        /* ASSIGN TO ARRAY */
        args[0] = (jl_value_t*)ugen_perform_fun; //__perform__ function
        args[1] = ugen_object; //__ugen__::__UGen__
        args[2] = ins_vector;  //__ins__::Vector{Vector{Float32}}
        args[3] = outs_vector; //__outs__::Vector{Vector{Float32}}
        args[4] = buf_size; //__buffer_size__::Int32 
        args[5] = julia_global_state->get_scsynth(); //__scsynth__::__SCSynth__
        
        return true;
    }

    inline bool add_ugen_ref_to_global_object_id_dict()
    {
        return true;
    }

    inline void next_NRT_busy(int inNumSamples)
    {
        /* SILENCE */
        output_silence(inNumSamples);
        
        int julia_object_unique_id = (int)in0(0);
        Print("UNIQUE ID: %i\n", julia_object_unique_id);

        /* if(!array_state), next_NRT_busy will run again at next audio buffer */
        bool array_state = retrieve_julia_object(julia_object_unique_id);
        if(!array_state)
        {
            Print("WARNING: JuliaObjectArray is resizing. Object creation deferred.\n");
            return;
        }
        
        /* if(!gc_state), next_NRT_busy will run again at next audio buffer */
        bool gc_state = julia_gc_barrier->RTChecklock();
        if(!gc_state) 
        {
            Print("WARNING: Julia's GC is running. Object creation deferred.\n");
            return;
        }

        //GC lock acquired! Array lock has already been released in julia_objects_array->get_julia_object()
        Print("*** RT Lock aquired!! ***\n");

        bool successful_allocation = allocate_julia_args();
        if(!successful_allocation)
        {
            Print("ERROR: Could not allocate UGen \n");
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->RTUnlock();
            return;
        }

        bool successful_ugen_ref = add_ugen_ref_to_global_object_id_dict();
        if(!successful_ugen_ref)
        {
            Print("ERROR: Could not allocate __UGenRef__ \n");
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->RTUnlock();
            return;
        }

        julia_gc_barrier->RTUnlock();

        //Assign directly, without first sample ????
        mCalcFunc = make_calc_function<Julia, &Julia::next_julia_code>();
    }

    inline void next_julia_code(int inNumSamples) 
    {
        /* SETUP inNumSamples (Needed for the first sample to be calculated when assigning function)*/
        *(int*)jl_data_ptr(args[4]) = inNumSamples;

        /* SETUP INS/OUTS */
        /* CHange buffer they are pointing at. At setup, it was nullptr */
        for(int i = 0; i < (numInputs() - 1); i++) //One less input , as in(0) is unique_id
            ((jl_array_t*)(ins[i]))->data = (float*)in(i + 1); //i + 1 = correct SC buffer

        for(int i = 0; i < numOutputs(); i++)
            ((jl_array_t*)(outs[i]))->data = (float*)out(i);

        /* RETRIEVE METHOD INSTANCE. (It could be done beforehand...) */
        jl_method_instance_t* perform_instance = julia_object->perform_instance;

        if(!perform_instance)
        {
            Print("INVALID PERFORM INSTANCE\n");
            output_silence(inNumSamples);
            return;
        }

        if(jl_get_ptls_states()->world_age < 20000)
        {
            Print("ERROR: WORLD AGE %d\n", jl_get_ptls_states()->world_age);
            jl_get_ptls_states()->world_age = jl_get_world_counter();
        }

        for(int i = 0; i < nargs; i++)
        {
            if(!args[i])
            {
                Print("ERROR: INVALID ARG \n");
                output_silence(inNumSamples);
                return;
            }
        }

        jl_call_no_gc(args, nargs);
        
        

        //jl_get_ptls_states()->world_age = jl_get_world_counter();
        /* RUN COMPILED FUNCTION */
        /* RT CRASHES HAPPEN BECAUSE, IF I RUN A jl_call() IN NRT THREAD, jl_call() wraps
        JL_TRY AND CATCH, and that exception handling crashes the whole thing. CHECK IF MY JL_TRY/CATCH
        for jl_load() works. OTHERWISE, just have a simple if(!) check, and print out result in case */
        //jl_invoke_already_compiled_SC(perform_instance, args, nargs);
    }

    inline void output_silence(int inNumSamples)
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
}

//Destructor function called when the shared library Julia.so gets unloaded from server (when server is quitted)
void julia_destructor(void) __attribute__((destructor));
void julia_destructor(void)
{
    //If julia has been initialized but global_state failed from whatever reason
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