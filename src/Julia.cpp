#include "JuliaFuncs.hpp"

//Functions declared in julia.h and defined here...
extern "C" 
{
    /* PERHAPS, With changed mechanism of GC and no GC when RT performs, there is no need
    in accessing the struct this way and I can just assign new values. Also, test Buffer as a 
    struct instead of a mutable struct */
    void* jl_get_buf_shared_SC(void* buffer_SCWorld, float fbufnum)
    {
        printf("*** NEW BUFFER!!! ***\n");
        World* SCWorld = (World*)buffer_SCWorld;

        uint32 bufnum = (int)fbufnum; 

        //If bufnum is not more that maximum number of buffers in World* it means bufnum doesn't point to a LocalBuf
        if(!(bufnum >= SCWorld->mNumSndBufs))
        {
            SndBuf* buf = SCWorld->mSndBufs + bufnum; 

            if(!buf->data)
            {
                printf("WARNING: Julia: Invalid buffer: %d\n", bufnum);
                return nullptr;
            }

            /* THIS MACRO IS USELESS HERE FOR SUPERNOVA. It should be set after each call to jl_get_buf_shared_SC to lock
            the buffer for the entirety of the Julia function... */
            LOCK_SNDBUF_SHARED(buf); 

            return (void*)buf;
        }
        else
        {
            printf("WARNING: Julia: local buffers are not yet supported \n");
            
            return nullptr;
        
            /* int localBufNum = bufnum - SCWorld->mNumSndBufs; 
            
            Graph *parent = unit->mParent; 
            
            if(localBufNum <= parent->localBufNum)
                unit->m_buf = parent->mLocalSndBufs + localBufNum; 
            else 
            { 
                bufnum = 0; 
                unit->m_buf = SCWorld->mSndBufs + bufnum; 
            } 

            return (void*)buf;
            */
        }
    }

    float jl_get_float_value_buf_SC(void* buf, size_t index, size_t channel)
    {
        //printf("INDEX: %d\n", index);
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;
            
            size_t c_index = index - 1; //Julia counts from 1, that's why index - 1
            
            size_t actual_index = (c_index * snd_buf->channels) + channel; //Interleaved data
            
            if(index && (actual_index < snd_buf->samples))
                return snd_buf->data[actual_index];
        }

        //printf("ERROR: Invalid access at index %d, channel %d\n", index, channel);
        
        return 0.f;
    }

    void jl_set_float_value_buf_SC(void* buf, float value, size_t index, size_t channel)
    {
        //printf("INDEX: %d\n", index);
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;

            size_t c_index = index - 1; //Julia counts from 1, that's why index - 1
            
            size_t actual_index = (c_index * snd_buf->channels) + channel; //Interleaved data
            
            if(index && (actual_index < snd_buf->samples))
            {
                snd_buf->data[actual_index] = value;
                return;
            }
        }
    }

    //Length of each channel
    int jl_get_frames_buf_SC(void* buf)
    {
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;
            return snd_buf->frames;
        }
            
        return 0;
    }

    //Total allocated length
    int jl_get_samples_buf_SC(void* buf)
    {
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;
            return snd_buf->samples;
        }

        return 0;
    }

    //Number of channels
    int jl_get_channels_buf_SC(void* buf)
    {
        if(buf)
        {
            SndBuf* snd_buf = (SndBuf*)buf;
            return snd_buf->channels;
        }
            
        return 0;
    }
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
            valid = false;
            return;
        }

        unique_id = (int)in0(0);

        //Actual inputs of Julia code, excluding ID as first input
        real_num_inputs = numInputs() - 1;

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

        bool gc_state = julia_gc_barrier->RTTrylock();
        printf("GC_STATE: %d\n", gc_state);
        if(!gc_state) 
        {
            Print("WARNING: Julia's GC is running. Object creation deferred.\n");
            set_calc_function<Julia, &Julia::next_NRT_busy>();  
            return;
        }

        bool compiler_state = julia_compiler_barrier->RTTrylock();
        if(!compiler_state)
        {
            Print("WARNING: Julia's compiler is running. Object creation deferred.\n");
            set_calc_function<Julia, &Julia::next_NRT_busy>(); 
            julia_gc_barrier->Unlock();
            return;
        }

        bool successful_allocation = allocate_julia_args();
        if(!successful_allocation)
        {
            Print("ERROR: Could not allocate UGen \n");
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }

        bool succesful_destructor = get_destructor();
        if(!succesful_destructor)
        {
            Print("ERROR: Could retrieve UGen destructor \n");
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }
    
        bool succesful_added_ugen_ref = add_ugen_ref_to_global_object_id_dict();
        if(!succesful_added_ugen_ref)
        {
            Print("ERROR: Could not allocate __UGenRef__ \n");
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }

        //Unlock the gc barrier
        julia_gc_barrier->Unlock();

        julia_compiler_barrier->Unlock();
        
        //set_calc_function already does one sample of audio.
        set_calc_function<Julia, &Julia::next_julia_code>();
    }

    ~Julia() 
    {
        if(valid)
        {
            if(args)
                free_args();

            if(ins && outs)
                free_ins_outs();

            /* Any Julia code here (destructor functon included) is WRONG, as I have no way of 
            making sure that the GC won't run as this object, and with it, and these last Julia calls 
            are running. What I can do is check if the GC is performing. If it is, have another IdDict in the GC class
            where I can push these Julia object and that will run these destructor calls at the before next GC collection */
            bool gc_state = julia_gc_barrier->RTTrylock();
            if(!gc_state)
            {
                /* GC PERFORMING */

                printf("WARNING: GC locked: posting __UGenRef__ destruction to gc_array \n");
                
                for(int i = 0; i < gc_array_num; i++)
                {
                    jl_value_t* this_ugen_ref = gc_array[i];
                    if(this_ugen_ref == nullptr)
                    {
                        gc_array[i] = ugen_ref_object;
                        break;
                    }
                }

                gc_array_needs_emptying = true;

                return;
            }
            
            bool compiler_state = julia_compiler_barrier->RTTrylock();
            if(!compiler_state)
            {
                /* GC AND COMPILER PERFORMING */

                printf("WARNING: Compiler and GC locked: posting __UGenRef__ destruction to gc_array \n");

                for(int i = 0; i < gc_array_num; i++)
                {
                    jl_value_t* this_ugen_ref = gc_array[i];
                    if(this_ugen_ref == nullptr)
                    {
                        gc_array[i] = ugen_ref_object;
                        break;
                    }
                }

                gc_array_needs_emptying = true;
                
                julia_gc_barrier->Unlock();
                return;
            }

            /* I know that, even if julia_object was removed from julia_objects_array, destructor_fun and instance and ugen_object
            are kept alive in __UGenRef__ */
            perform_destructor();

            //This call can't be concurrent to GC collection. If it is, the __UGenRef__ for this UGen won't be deleted
            //from the object_id_dict(), as the call would be concurrent to the locking of the GC. That __UGenRef__ would
            //be living forever, wasting memory.
            remove_ugen_ref_from_global_object_id_dict();

            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
        }
    }

private:
    JuliaObject* julia_object = nullptr;
    int unique_id = -1;

    int real_num_inputs = 0;

    bool print_once_inputs = false;
    bool print_once_outputs = false;

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

    jl_function_t* destructor_fun;
    jl_method_instance_t* destructor_instance;

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
        size_t ugen_num_of_inputs = real_num_inputs;

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

    inline bool get_destructor()
    {
        /* Get destructor too */
        destructor_fun = julia_object->destructor_fun;
        if(!destructor_fun)
        {
            Print("ERROR: Invalid __destructor__ function \n");
            return false;
        }

        destructor_instance = julia_object->destructor_instance;
        if(!destructor_instance)
        {
            Print("ERROR: Invalid __destructor__ instance \n");
            return false;
        }
        
        return true;
    }

    inline bool add_ugen_ref_to_global_object_id_dict()
    {  
        //First, create UGenRef object...
        int32_t ugen_ref_nargs = 6;
        jl_value_t* ugen_ref_args[ugen_ref_nargs];
        
        //__UGenRef__ constructor.
        /* DESTRUCTOR is added because, if object definition in julia_object gets deleted, destructor
        gets aswell, and I have no way of retrieving it. This way, __UGenRef__ will store everything I might
        need at destructor too (namely, destructor_fun and destructor_instance). */
        ugen_ref_args[0] = julia_object->ugen_ref_fun;
        ugen_ref_args[1] = ugen_object;
        ugen_ref_args[2] = ins_vector;
        ugen_ref_args[3] = outs_vector;
        ugen_ref_args[4] = destructor_fun;
        ugen_ref_args[5] = (jl_value_t*)destructor_instance;

        //Create __UGenRef__ for this object
        ugen_ref_object = jl_invoke_already_compiled_SC(julia_object->ugen_ref_instance, ugen_ref_args, ugen_ref_nargs);
        if(!ugen_ref_object)
        {
            Print("ERROR: Could not create __UGenRef__ object\n");
            return false;
        }

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

    inline bool remove_ugen_ref_from_global_object_id_dict()
    {
        if(!ugen_ref_object)
        {
            Print("Invalid __UGenRef__ to be freed \n");
            return false;
        }

        int32_t delete_index_nargs = 3;
        jl_value_t* delete_index_args[delete_index_nargs];

        delete_index_args[0] = julia_object->delete_index_ugen_ref_fun;
        delete_index_args[1] = julia_global_state->get_global_object_id_dict().get_id_dict();
        delete_index_args[2] = ugen_ref_object;

        //Now the GC can pick up this object (and relative allocated __Data__ objects, finalizing them)
        jl_value_t* delete_index_successful = jl_invoke_already_compiled_SC(julia_object->delete_index_ugen_ref_instance, delete_index_args, delete_index_nargs);
        if(!delete_index_successful)
        {
            Print("ERROR: Could not delete __UGenRef__ object from global object id dict\n");
            return false;
        }

        return true;
    }

    inline bool perform_destructor()
    {
        printf("PERFORMING DESTRUCTOR \n");

        int32_t destructor_nargs = 2;
        jl_value_t* destructor_args[destructor_nargs];
        destructor_args[0] = destructor_fun;
        destructor_args[1] = ugen_object;

        jl_value_t* destructor_call = jl_invoke_already_compiled_SC(destructor_instance, destructor_args, destructor_nargs);
        if(!destructor_call)
            return false;

        return true;
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
        bool gc_state = julia_gc_barrier->RTTrylock();
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

        bool succesful_destructor = get_destructor();
        if(!succesful_destructor)
        {
            Print("ERROR: Could retrieve UGen destructor \n");
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }

        bool succesful_added_ugen_ref = add_ugen_ref_to_global_object_id_dict();
        if(!succesful_added_ugen_ref)
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
        if(julia_compiler_barrier->RTTrylock())
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
                bool gc_state = julia_gc_barrier->RTTrylock();
                if(!gc_state)
                {
                    output_silence(inNumSamples);
                    julia_compiler_barrier->Unlock();
                    return;
                }

                just_reallocated = true;

                /* DESTRUCTOR must always be before the removal of __UGenRef__ from global object id dict */
                bool succesful_performed_destructor = perform_destructor();
                if(!succesful_performed_destructor)
                {
                    //Print("ERROR: Invalid __constructor__ instance \n");
                    output_silence(inNumSamples);
                    julia_gc_barrier->Unlock();
                    julia_compiler_barrier->Unlock();
                    return;
                }
                
                bool succesful_removed_ugen_ref = remove_ugen_ref_from_global_object_id_dict();
                if(!succesful_removed_ugen_ref)
                {
                    //Print("ERROR: Invalid __constructor__ instance \n");
                    output_silence(inNumSamples);
                    julia_gc_barrier->Unlock();
                    julia_compiler_barrier->Unlock();
                    return;
                }

                //Check I/O mismatch and print to user.
                if(julia_object->num_inputs > real_num_inputs)
                {
                    if(!print_once_inputs)
                    {
                        printf("WARNING: Julia @object \"%s\" inputs mismatch. Expected: %d. Have: %d\n", julia_object->name, julia_object->num_inputs, real_num_inputs);
                        print_once_inputs = true;
                    }
                    output_silence(inNumSamples);
                    julia_compiler_barrier->Unlock();
                    julia_gc_barrier->Unlock();
                    return;
                }

                if(julia_object->num_inputs < real_num_inputs)
                    printf("WARNING: Julia @object \"%s\" inputs mismatch. Expected: %d. Have: %d. Using only first %d inputs.\n", julia_object->name, julia_object->num_inputs, real_num_inputs, julia_object->num_inputs);

                if(numOutputs() > julia_object->num_outputs)
                {
                    if(!print_once_outputs)
                    {
                        printf("WARNING: Julia @object \"%s\" outputs mismatch. Expected: %d. Have: %d\n", julia_object->name, julia_object->num_outputs, numOutputs());
                        print_once_outputs = true;
                    }
                    output_silence(inNumSamples);
                    julia_compiler_barrier->Unlock();
                    julia_gc_barrier->Unlock();
                    return;
                }

                if(julia_object->num_outputs < numOutputs())
                    printf("WARNING: Julia @object \"%s\" outputs mismatch. Expected: %d. Have: %d. Using only first %d outputs.\n", julia_object->name, julia_object->num_outputs, numOutputs(), julia_object->num_outputs);

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

                /* Get new destructor too */
                bool succesful_destructor = get_destructor();
                if(!succesful_destructor)
                {
                    //Print("ERROR: Invalid __UGen__ object \n");
                    output_silence(inNumSamples);
                    julia_gc_barrier->Unlock();
                    julia_compiler_barrier->Unlock();
                    return;
                }

                bool succesful_added_ugen_ref = add_ugen_ref_to_global_object_id_dict();
                if(!succesful_added_ugen_ref)
                {
                    //Print("ERROR: Invalid __UGen__ object \n");
                    output_silence(inNumSamples);
                    julia_gc_barrier->Unlock();
                    julia_compiler_barrier->Unlock();
                    return;
                }


                //Only unlock GC. Compiler will be unlocked only if object was compiled succesfully
                julia_gc_barrier->Unlock();

                print_once_inputs = false;
                print_once_outputs = false;

                //julia_compiler_barrier->Unlock(); //SHOULD IT BE HERE TOO???
            }

            /* ACTUAL DSP CALCULATIONS */
            //If object's not been reallocated on this buffer cycle, execute it.
            //Otherwise, wait next buffer cycle to spread calculations
            if(!just_reallocated)
            {
                julia_instance(inNumSamples);
                julia_compiler_barrier->Unlock();
                return;
            }

            /* If every recompilation went through correctly, output silence and set false to the reallocation bool..
            At next cycle, we'll hear the newly compiled object */
            if(julia_object->compiled)
            {
                output_silence(inNumSamples);
                just_reallocated = false;
                julia_compiler_barrier->Unlock();
                return;
            }
        }

        //If compiler is busy, output silence
        output_silence(inNumSamples);
    }

    inline void julia_instance(int inNumSamples)
    {
        /* SETUP inNumSamples (Needed for the first sample to be calculated when assigning function)*/
        *(int*)jl_data_ptr(args[4]) = inNumSamples;

        /* SETUP INS/OUTS */
        /* Change (void*)data they are pointing at. At setup, it was nullptr */
        for(int i = 0; i < (real_num_inputs); i++) //in(0) is unique_id
            ((jl_array_t*)(ins[i]))->data = (float*)in(i + 1); //i + 1 = correct SC buffer (excluding unique_id at in(0))

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