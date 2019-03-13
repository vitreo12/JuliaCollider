#include "JuliaFuncs.hpp"

struct Julia : public SCUnit 
{
public:
    Julia() 
    {    
        if(!julia_global_state->is_initialized())
        {
            Print("WARNING: Julia hasn't been booted correctly \n");
            set_calc_function<Julia, &Julia::output_silence>();
            valid = false;
            return;
        }

        unique_id = (int)in0(0);

        if(unique_id < 0)
        {
            Print("WARNING: Invalid unique id \n");
            valid = false;
            set_calc_function<Julia, &Julia::output_silence>();
        }
        
        //Print("UNIQUE ID: %i\n", julia_object_unique_id);

        JuliaObjectsArrayState array_state = retrieve_julia_object();
        if(array_state == JuliaObjectsArrayState::Invalid)
        {
            printf("WARNING: Invalid unique id \n");
            valid = false;
            set_calc_function<Julia, &Julia::output_silence>();
            return;
        }

        if(array_state == JuliaObjectsArrayState::Busy)
        {
            Print("WARNING: JuliaObjectArray is resizing. Object creation deferred.\n");
            set_calc_function<Julia, &Julia::next_NRT_busy>();  
            return;
        }

        bool gc_state = julia_gc_barrier->RTChecklock();
        printf("GC_STATE: %d\n", gc_state);
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
            julia_gc_barrier->Unlock();
            return;
        }

        bool successful_ugen_ref = add_ugen_ref_to_global_object_id_dict();
        if(!successful_ugen_ref)
        {
            Print("ERROR: Could not allocate __UGenRef__ \n");
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            return;
        }

        //Unlock the gc barrier
        julia_gc_barrier->Unlock();
        
        //set_calc_function already does one sample of audio.
        set_calc_function<Julia, &Julia::next_julia_code>();
    }

    ~Julia() 
    {
        /* if(julia_object)
        {
            if(julia_object->compiled)
                julia_object->RT_busy = false;
        } */

        if(valid)
        {
            remove_ugen_ref_from_global_object_id_dict();

            if(args)
                free_args();

            if(ins && outs)
                free_ins_outs();
        }
    }

private:
    JuliaObject* julia_object = nullptr;
    int unique_id = -1;

    //If unique_id = -1 or if sending a JuliaDef that's not valid on server side.
    bool valid = true;

    bool just_reallocated = false;

    jl_value_t* ugen_object;
    int32_t nargs = 6;
    jl_value_t** args;
    jl_value_t* ins_vector;
    jl_value_t* outs_vector;
    jl_value_t** ins;
    jl_value_t** outs;
    
    jl_function_t* perform_fun;
    jl_method_instance_t* perform_instance;

    jl_value_t* ugen_ref_object;

    inline JuliaObjectsArrayState retrieve_julia_object()
    {
        JuliaObjectsArrayState julia_objects_array_state = julia_objects_array->get_julia_object(unique_id, &julia_object);
        return julia_objects_array_state;
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

    /* JUST ONCE */
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
        
        ugen_object = jl_invoke_already_compiled_SC(ugen_constructor_instance, &ugen_constructor_fun, 1);
        if(!ugen_object)
        {
            Print("ERROR: Invalid __UGen__ object \n");
            return false;
        }

        perform_fun = julia_object->perform_fun;
        if(!perform_fun)
        {
            Print("ERROR: Invalid __perform__ function \n");
            return false;
        }

        jl_value_t* buf_size = jl_box_int32(bufferSize());
        if(!buf_size)
        {
            Print("ERROR: Invalid __buf_size__ argument \n");
            return false;
        }

        perform_instance = julia_object->perform_instance;
        if(!perform_instance)
        {
            Print("ERROR: Invalid __perform__ method instance \n");
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

        ins_vector = (jl_value_t*)jl_alloc_array_1d(julia_global_state->get_vector_of_vectors_float32(), ugen_num_of_inputs);
        ins = (jl_value_t**)RTAlloc(mWorld, sizeof(jl_value_t*) * ugen_num_of_inputs);
        if(!ins_vector || !ins)
        {
            free_args();
            Print("WARNING: Could not allocate memory for inputs \n");
            return false;
        }

        //Should all memory here be allocated on stack like this??
        size_t nargs_set_index_audio_vector = 4;
        jl_value_t* args_set_index_audio_vector[nargs_set_index_audio_vector];

        for(int i = 0; i < ugen_num_of_inputs; i++)
        {
            //Using nullptrs here. Real pointers will be set at each buffer cycle to SC ins[channel] buffer.
            ins[i] = (jl_value_t*)jl_ptr_to_array_1d(julia_global_state->get_vector_float32(), nullptr, bufferSize(), 0);

            args_set_index_audio_vector[0] = julia_global_state->get_set_index_audio_vector_fun();
            args_set_index_audio_vector[1] = ins_vector;
            args_set_index_audio_vector[2] = ins[i];
            args_set_index_audio_vector[3] = jl_box_int32(i + 1); //Julia index from 1 onwards

            jl_invoke_already_compiled_SC(julia_object->set_index_audio_vector_instance, args_set_index_audio_vector, nargs_set_index_audio_vector);
        }

        outs_vector = (jl_value_t*)jl_alloc_array_1d(julia_global_state->get_vector_of_vectors_float32(), numOutputs());
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

            args_set_index_audio_vector[0] = julia_global_state->get_set_index_audio_vector_fun();
            args_set_index_audio_vector[1] = outs_vector;
            args_set_index_audio_vector[2] = outs[i];
            args_set_index_audio_vector[3] = jl_box_int32(i + 1); //Julia index from 1 onwards

            jl_invoke_already_compiled_SC(julia_object->set_index_audio_vector_instance, args_set_index_audio_vector, nargs_set_index_audio_vector);
        }

        /* ASSIGN TO ARRAY */
        args[0] = (jl_value_t*)perform_fun; //__perform__ function
        args[1] = ugen_object; //__ugen__::__UGen__
        args[2] = ins_vector;  //__ins__::Vector{Vector{Float32}}
        args[3] = outs_vector; //__outs__::Vector{Vector{Float32}}
        args[4] = buf_size; //__buffer_size__::Int32 
        args[5] = julia_global_state->get_scsynth(); //__scsynth__::__SCSynth__
        
        return true;
    }

    inline bool add_ugen_ref_to_global_object_id_dict()
    {  
        //First, create UGenRef object...
        int32_t ugen_ref_nargs = 4;
        jl_value_t* ugen_ref_args[ugen_ref_nargs];
        
        //__UGenRef__ constructor
        ugen_ref_args[0] = julia_object->ugen_ref_fun;
        ugen_ref_args[1] = ugen_object;
        ugen_ref_args[2] = ins_vector;
        ugen_ref_args[3] = outs_vector;

        //Create __UGenRef__ for this object
        ugen_ref_object = jl_invoke_already_compiled_SC(julia_object->ugen_ref_instance, ugen_ref_args, ugen_ref_nargs);
        if(!ugen_ref_object)
        {
            Print("ERROR: Could not create __UGenRef__ object\n");
            return false;
        }

        //SHould it be RTAlloc()???
        int32_t set_index_nargs = 3;
        jl_value_t* set_index_args[set_index_nargs];

        //set index
        set_index_args[0] = julia_object->set_index_ugen_ref_fun;
        set_index_args[1] = julia_global_state->get_global_object_id_dict().get_id_dict();
        set_index_args[2] = ugen_ref_object;

        jl_value_t* set_index_successful = jl_invoke_already_compiled_SC(julia_object->set_index_ugen_ref_instance, set_index_args, set_index_nargs);
        if(!set_index_successful)
        {
            Print("ERROR: Could not assign __UGenRef__ object to global object id dict\n");
            return false;
        }

        return true;
    }

    inline void remove_ugen_ref_from_global_object_id_dict()
    {
        if(!ugen_ref_object)
        {
            Print("Invalid __UGenRef__ to be freed \n");
            return;
        }

        int32_t delete_index_nargs = 3;
        jl_value_t* delete_index_args[delete_index_nargs];

        //delete index
        delete_index_args[0] = julia_object->delete_index_ugen_ref_fun;
        delete_index_args[1] = julia_global_state->get_global_object_id_dict().get_id_dict();
        delete_index_args[2] = ugen_ref_object;

        jl_invoke_already_compiled_SC(julia_object->delete_index_ugen_ref_instance, delete_index_args, delete_index_nargs);
    }

    inline void next_NRT_busy(int inNumSamples)
    {
        /* Output silence */
        output_silence(inNumSamples);

        /* if(!array_state), next_NRT_busy will run again at next audio buffer */
        JuliaObjectsArrayState array_state = retrieve_julia_object();
        if(array_state == JuliaObjectsArrayState::Invalid)
        {
            printf("WARNING: Invalid unique id \n");
            valid = false;
            set_calc_function<Julia, &Julia::output_silence>();
            return;
        }
        
        if(array_state == JuliaObjectsArrayState::Busy)
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
            julia_gc_barrier->Unlock();
            return;
        }

        bool successful_ugen_ref = add_ugen_ref_to_global_object_id_dict();
        if(!successful_ugen_ref)
        {
            Print("ERROR: Could not allocate __UGenRef__ \n");
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            return;
        }

        julia_gc_barrier->Unlock();

        //Assign directly, without first sample ????
        mCalcFunc = make_calc_function<Julia, &Julia::next_julia_code>();
    }

    inline void next_julia_code(int inNumSamples) 
    {
        if(julia_compiler_barrier->RTChecklock())
        {
            //If function changed, allocate it new object on this cycle
            if(args[0] != julia_object->perform_fun)
            {
                if(!julia_object->compiled)
                {
                    output_silence(inNumSamples);
                    julia_compiler_barrier->Unlock();
                    return;
                }

                //Reaquire GC lock to allocate all objects later...
                bool gc_state = julia_gc_barrier->RTChecklock();
                if(!gc_state)
                {
                    output_silence(inNumSamples);
                    julia_compiler_barrier->Unlock();
                    return;
                }

                just_reallocated = true;

                remove_ugen_ref_from_global_object_id_dict();

                //Recompile...
                perform_instance = julia_object->perform_instance;
                if(!perform_instance)
                {
                    //Print("ERROR: Invalid __perform__ method instance \n");
                    output_silence(inNumSamples);
                    julia_compiler_barrier->Unlock();
                    julia_gc_barrier->Unlock();
                    return;
                }

                args[0] = julia_object->perform_fun;

                jl_function_t* ugen_constructor_fun = julia_object->constructor_fun;
                if(!ugen_constructor_fun)
                {
                    //Print("ERROR: Invalid __constructor__ function \n");
                    output_silence(inNumSamples);
                    julia_compiler_barrier->Unlock();
                    julia_gc_barrier->Unlock();
                    return;
                }

                jl_method_instance_t* ugen_constructor_instance = julia_object->constructor_instance;
                if(!ugen_constructor_instance)
                {
                    //Print("ERROR: Invalid __constructor__ instance \n");
                    output_silence(inNumSamples);
                    julia_gc_barrier->Unlock();
                    julia_compiler_barrier->Unlock();
                    return;
                }
                
                ugen_object = jl_invoke_already_compiled_SC(ugen_constructor_instance, &ugen_constructor_fun, 1);
                if(!ugen_object)
                {
                    //Print("ERROR: Invalid __UGen__ object \n");
                    output_silence(inNumSamples);
                    julia_gc_barrier->Unlock();
                    julia_compiler_barrier->Unlock();
                    return;
                }

                args[1] = ugen_object;

                add_ugen_ref_to_global_object_id_dict();

                julia_gc_barrier->Unlock();

                //julia_compiler_barrier->Unlock(); //SHOULD IT BE HERE TOO???
            }

            //Next cycle (after recreation of object)
            if(!just_reallocated)
            {
                julia_instance(inNumSamples);
                julia_compiler_barrier->Unlock();
                return;
            }

            if(julia_object->compiled)
            {
                output_silence(inNumSamples);
                just_reallocated = false;
                julia_compiler_barrier->Unlock();
                return;
            }
        }

        //If compiler is on, output silence...
        output_silence(inNumSamples);
    }

    inline void julia_instance(int inNumSamples)
    {
        /* SETUP inNumSamples (Needed for the first sample to be calculated when assigning function)*/
        *(int*)jl_data_ptr(args[4]) = inNumSamples;

        /* SETUP INS/OUTS */
        /* CHange buffer they are pointing at. At setup, it was nullptr */
        for(int i = 0; i < (numInputs() - 1); i++) //One less input , as in(0) is unique_id
            ((jl_array_t*)(ins[i]))->data = (float*)in(i + 1); //i + 1 = correct SC buffer (excluding unique_id)

        for(int i = 0; i < numOutputs(); i++)
            ((jl_array_t*)(outs[i]))->data = (float*)out(i);    

        jl_invoke_already_compiled_SC(perform_instance, args, nargs);
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
    //Could not run any thread join here, as threads already got dealt with. Just delete the objects
    /* delete julia_objects_array;
    delete julia_gc_barrier;
    delete julia_compiler_barrier;
    delete julia_global_state; */
}