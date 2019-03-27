#include "JuliaFuncs.hpp"

struct Julia : public SCUnit 
{
public:
    Julia() 
    {   
        //It should have a more refined mechanism, linked to actual constructors and destructors of Julia code.
        active_julia_ugens++;
        //

        if(!julia_global_state->is_initialized())
        {
            Print("WARNING: Julia hasn't been booted correctly \n");
            set_calc_function<Julia, &Julia::output_silence>();
            return;
        }

        unique_id = (int)in0(0);

        //Actual inputs of Julia code, excluding ID as first input
        real_num_inputs = numInputs() - 1;

        if(unique_id < 0)
        {
            Print("WARNING: Invalid unique id \n");
            set_calc_function<Julia, &Julia::output_silence>();
        }

        bool gc_lock = julia_gc_barrier->RTTrylock();
        printf("gc_lock: %d\n", gc_lock);
        if(!gc_lock) 
        {
            Print("WARNING: Julia's GC is running. Object creation deferred.\n");
            set_calc_function<Julia, &Julia::next_NRT_busy>();  
            return;
        }

        bool compiler_lock = julia_compiler_barrier->RTTrylock();
        if(!compiler_lock)
        {
            Print("WARNING: Julia's compiler is running. Object creation deferred.\n");
            set_calc_function<Julia, &Julia::next_NRT_busy>(); 
            julia_gc_barrier->Unlock();
            return;
        }

        /* THIS will change the pointer of julia_object* */
        JuliaObjectsArrayState array_state = retrieve_julia_object();
        if(array_state == JuliaObjectsArrayState::Invalid)
        {
            printf("WARNING: Invalid unique id \n");
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }

        if(array_state == JuliaObjectsArrayState::Busy) //Future work...
        {
            Print("WARNING: JuliaObjectArray is resizing. Object creation deferred.\n");
            set_calc_function<Julia, &Julia::next_NRT_busy>();  
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }

        if(real_num_inputs != julia_object->num_inputs)
        {
            Print("ERROR: Input number mismatch. Have %d, expected %d. Run update method on JuliaDef\n", numInputs(), julia_object->num_inputs);
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }

        if(numOutputs() != julia_object->num_outputs)
        {
            Print("ERROR: Output number mismatch. Have %d, expected %d. Run update method on JuliaDef\n", numOutputs(), julia_object->num_outputs);
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }

        bool successful_allocation = allocate_args_costructor_perform();
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
            Print("ERROR: Could not retrieve UGen destructor \n");
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

        //set validity
        valid = true;

        //Unlock both gc and compiler barriers
        julia_gc_barrier->Unlock();
        julia_compiler_barrier->Unlock();
        
        //set_calc_function already does one sample of audio. Set it
        //after unlocking so that Julia code will be run once (otherwise lock would
        //still be maintained)
        set_calc_function<Julia, &Julia::next_julia_code>();
    }

    ~Julia() 
    {
        //It should have a more refined mechanism, linked to actual constructors and destructors of Julia code.
        active_julia_ugens--;
        //

        if(args)
            free_args();

        if(ins)
            free_ins();
        
        if(outs)
            free_outs();

        Print("IS VALID? %d\n", valid);

        if(valid)
        {
            //Check if GC/compiler are currently performing. If it is, post the UGen ref to an array
            //that will be consumed on next GC.
            bool gc_lock = julia_gc_barrier->RTTrylock();
            if(!gc_lock)
            {
                /* GC PERFORMING */
                printf("WARNING: GC locked: posting __UGenRef__ destruction to gc_array \n");
                
                /* This is not thread-safe for supernova */
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
            
            bool compiler_lock = julia_compiler_barrier->RTTrylock();
            if(!compiler_lock)
            {
                /* GC AND COMPILER PERFORMING */
                printf("WARNING: Compiler and GC locked: posting __UGenRef__ destruction to gc_array \n");

                /* This is not thread-safe for supernova */
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

            Print("*** RT Locks aquired!! ***\n");

            perform_destructor();

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

    bool print_once_exception = false;

    //If unique_id = -1 or if sending a JuliaDef that's not valid on server side.
    /* SHOULD INVERT VALID TO ONLY SET IT TO TRUE WHEN NEEDED */
    bool valid = false;

    bool just_reallocated = false;

    jl_value_t* ugen_object;
    int32_t nargs = 6;
    jl_value_t** args = nullptr;
    jl_value_t* ins_vector;
    jl_value_t* outs_vector;
    jl_value_t** ins = nullptr;
    jl_value_t** outs = nullptr;
    
    jl_function_t* perform_fun;
    jl_method_instance_t* perform_instance;

    jl_function_t* destructor_fun;
    jl_method_instance_t* destructor_instance;
    jl_function_t* delete_index_ugen_ref_fun;
    jl_method_instance_t* delete_index_ugen_ref_instance;

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

    inline void free_ins()
    {
        RTFree(mWorld, ins);
    }

    inline void free_outs()
    {
        RTFree(mWorld, outs);
    }

    /* JUST ONCE */
    inline bool allocate_args_costructor_perform()
    {      
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
            RTFree(mWorld, ins);
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
            RTFree(mWorld, ins);
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

        /* CONSTRUCTOR */
        jl_function_t* constructor_fun = julia_object->constructor_fun;
        if(!constructor_fun)
        {
            free_args();
            RTFree(mWorld, ins);
            RTFree(mWorld, outs);
            Print("ERROR: Invalid __constructor__ function \n");
            return false;
        }

        jl_method_instance_t* constructor_instance = julia_object->constructor_instance;
        if(!constructor_instance)
        {
            free_args();
            RTFree(mWorld, ins);
            RTFree(mWorld, outs);
            Print("ERROR: Invalid __constructor__ instance \n");
            return false;
        }

        /* Copy data inside the vector to be seen from constructor */
        for(int i = 0; i < (real_num_inputs); i++) //in(0) is unique_id
            ((jl_array_t*)(ins[i]))->data = (float*)in(i + 1); //i + 1 = correct SC buffer (excluding unique_id at in(0))

        size_t constructor_nargs = 3;
        jl_value_t* constructor_args[constructor_nargs];

        constructor_args[0] = constructor_fun;
        constructor_args[1] = ins_vector;
        constructor_args[2] = julia_global_state->get_scsynth();
        
        /* Create __UGen__ for this julia_object */
        ugen_object = jl_invoke_already_compiled_SC(constructor_instance, constructor_args, constructor_nargs);
        if(!ugen_object)
        {
            free_args();
            RTFree(mWorld, ins);
            RTFree(mWorld, outs);
            Print("ERROR: Invalid __UGen__ object \n");
            return false;
        }

        /* PERFORM FUNCTION */
        perform_fun = julia_object->perform_fun;
        if(!perform_fun)
        {
            free_args();
            RTFree(mWorld, ins);
            RTFree(mWorld, outs);
            Print("ERROR: Invalid __perform__ function \n");
            return false;
        }

        jl_value_t* buf_size = jl_box_int32(bufferSize());
        if(!buf_size)
        {
            free_args();
            RTFree(mWorld, ins);
            RTFree(mWorld, outs);
            Print("ERROR: Invalid __buf_size__ argument \n");
            return false;
        }

        perform_instance = julia_object->perform_instance;
        if(!perform_instance)
        {
            free_args();
            RTFree(mWorld, ins);
            RTFree(mWorld, outs);
            Print("ERROR: Invalid __perform__ method instance \n");
            return false;
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

        /* Get delete_index_ugen_ref */
        delete_index_ugen_ref_fun = julia_object->delete_index_ugen_ref_fun;
        if(!delete_index_ugen_ref_fun)
        {
            Print("ERROR: Invalid delete_index_ugen_ref_fun \n");
            return false;
        }

        delete_index_ugen_ref_instance = julia_object->delete_index_ugen_ref_instance;
        if(!delete_index_ugen_ref_instance)
        {
            Print("ERROR: Invalid delete_index_ugen_ref_instance \n");
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

        delete_index_args[0] = delete_index_ugen_ref_fun;
        delete_index_args[1] = julia_global_state->get_global_object_id_dict().get_id_dict();
        delete_index_args[2] = ugen_ref_object;

        //Now the GC can pick up this object (and relative allocated __Data__ objects, finalizing them)
        jl_value_t* delete_index_successful = jl_invoke_already_compiled_SC(delete_index_ugen_ref_instance, delete_index_args, delete_index_nargs);
        if(!delete_index_successful)
        {
            Print("ERROR: Could not delete __UGenRef__ object from global object id dict\n");
            return false;
        }

        return true;
    }

    inline bool perform_destructor()
    {
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

        /* if(!gc_lock), next_NRT_busy will run again at next audio buffer */
        bool gc_lock = julia_gc_barrier->RTTrylock();
        if(!gc_lock) 
        {
            Print("WARNING: Julia's GC is running. Object creation deferred.\n");
            return;
        }

        /* if(!compiler_lock), next_NRT_busy will run again at next audio buffer */
        bool compiler_lock = julia_compiler_barrier->RTTrylock();
        if(!compiler_lock)
        {
            Print("WARNING: Julia's compiler is running. Object creation deferred.\n");
            julia_gc_barrier->Unlock();
            return;
        }

        //GC/compiler locks acquired!
        Print("*** RT Locks aquired!! ***\n");

        /* THIS will change the pointer of julia_object* */
        JuliaObjectsArrayState array_state = retrieve_julia_object();
        if(array_state == JuliaObjectsArrayState::Invalid)
        {
            printf("WARNING: Invalid unique id \n");
            valid = false;
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }
        
        if(array_state == JuliaObjectsArrayState::Busy) //Future work...
        {
            Print("WARNING: JuliaObjectArray is resizing. Object creation deferred.\n");
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }
        
        bool successful_allocation = allocate_args_costructor_perform();
        if(!successful_allocation)
        {
            Print("ERROR: Could not allocate UGen \n");
            valid = false;
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }

        bool succesful_destructor = get_destructor();
        if(!succesful_destructor)
        {
            Print("ERROR: Could retrieve UGen destructor \n");
            valid = false;
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }

        bool succesful_added_ugen_ref = add_ugen_ref_to_global_object_id_dict();
        if(!succesful_added_ugen_ref)
        {
            Print("ERROR: Could not allocate __UGenRef__ \n");
            valid = false;
            set_calc_function<Julia, &Julia::output_silence>();
            julia_gc_barrier->Unlock();
            julia_compiler_barrier->Unlock();
            return;
        }

        //set validity
        valid = true;

        //Unlock barriers
        julia_gc_barrier->Unlock();
        julia_compiler_barrier->Unlock();

        //Assign directly the next function, without first sample. (check this again)
        //mCalcFunc = make_calc_function<Julia, &Julia::next_julia_code>();

        //set_calc_function already does one sample of audio.
        set_calc_function<Julia, &Julia::next_julia_code>();
    }

    inline void next_julia_code(int inNumSamples) 
    {
        bool compiler_lock = julia_compiler_barrier->RTTrylock();
        if(compiler_lock)
        {
            //If THIS @object has changed, start the recompilation. Otherwise, it meant that another @object
            //object was being recompiled.
            if(args[0] != julia_object->perform_fun)
            {
                //Acquire GC lock to allocate all objects later
                bool gc_lock = julia_gc_barrier->RTTrylock();
                if(!gc_lock)
                {
                    output_silence(inNumSamples);
                    julia_compiler_barrier->Unlock();
                    return;
                }

                if(!julia_object->compiled)
                {
                    //JuliaDef.free()
                    if(valid)
                    {
                        //Functions will use previous julia_object functions here
                        perform_destructor();
                        remove_ugen_ref_from_global_object_id_dict();
                        
                        //valid = false so that ~Julia() won't call into Julia at all.
                        valid = false;
                        
                        //Set silence forever.
                        set_calc_function<Julia, &Julia::output_silence>();

                        julia_gc_barrier->Unlock();
                        julia_compiler_barrier->Unlock();
                        return;
                    }
                    
                    output_silence(inNumSamples);

                    julia_gc_barrier->Unlock();
                    julia_compiler_barrier->Unlock();
                    return;
                }

                /* DESTRUCTOR must always be before the removal of __UGenRef__ from global object id dict */
                bool succesful_performed_destructor = perform_destructor();
                if(!succesful_performed_destructor)
                {
                    //Print("ERROR: Invalid __constructor__ instance \n");
                    output_silence(inNumSamples);
                    valid = false;
                    julia_gc_barrier->Unlock();
                    julia_compiler_barrier->Unlock();
                    return;
                }
                
                bool succesful_removed_ugen_ref = remove_ugen_ref_from_global_object_id_dict();
                if(!succesful_removed_ugen_ref)
                {
                    //Print("ERROR: Invalid __constructor__ instance \n");
                    output_silence(inNumSamples);
                    valid = false;
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
                    valid = false;
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
                    valid = false;
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
                    valid = false;
                    julia_compiler_barrier->Unlock();
                    julia_gc_barrier->Unlock();
                    return;
                }

                args[0] = julia_object->perform_fun;

                jl_function_t* constructor_fun = julia_object->constructor_fun;
                if(!constructor_fun)
                {
                    //Print("ERROR: Invalid __constructor__ function \n");
                    output_silence(inNumSamples);
                    valid = false;
                    julia_compiler_barrier->Unlock();
                    julia_gc_barrier->Unlock();
                    return;
                }

                jl_method_instance_t* constructor_instance = julia_object->constructor_instance;
                if(!constructor_instance)
                {
                    //Print("ERROR: Invalid __constructor__ instance \n");
                    output_silence(inNumSamples);
                    valid = false;
                    julia_gc_barrier->Unlock();
                    julia_compiler_barrier->Unlock();
                    return;
                }
                
                /* Should this be RTAlloced, just like args?? */
                size_t constructor_nargs = 3;
                jl_value_t* constructor_args[constructor_nargs];

                constructor_args[0] = constructor_fun;
                constructor_args[1] = ins_vector;
                constructor_args[2] = julia_global_state->get_scsynth();
                
                /* Create __UGen__ for this julia_object */
                ugen_object = jl_invoke_already_compiled_SC(constructor_instance, constructor_args, constructor_nargs);
                if(!ugen_object)
                {
                    output_silence(inNumSamples);
                    valid = false;
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
                    valid = false;
                    julia_gc_barrier->Unlock();
                    julia_compiler_barrier->Unlock();
                    return;
                }

                bool succesful_added_ugen_ref = add_ugen_ref_to_global_object_id_dict();
                if(!succesful_added_ugen_ref)
                {
                    //Print("ERROR: Invalid __UGen__ object \n");
                    output_silence(inNumSamples);
                    valid = false;
                    julia_gc_barrier->Unlock();
                    julia_compiler_barrier->Unlock();
                    return;
                }

                //Only unlock GC. Compiler lock will be unlocked later, either at
                //succesful compilation or at exit of the function.
                julia_gc_barrier->Unlock();

                just_reallocated = true;
                
                print_once_inputs = false;
                print_once_outputs = false;
                print_once_exception = false;
            }

            /* ACTUAL DSP CALCULATIONS */
            /* It will be executed from one cycle later than recompilation */
            if(!just_reallocated)
            {
                julia_instance(inNumSamples);
                julia_compiler_barrier->Unlock();
                return;
            }

            /* If the recompilation went through correctly, output silence and set false to the reallocation bool.
            At next cycle, we'll hear the newly compiled object */
            if(julia_object->compiled)
            {
                output_silence(inNumSamples);
                just_reallocated = false;
                valid = true;
                julia_compiler_barrier->Unlock();
                return;
            }

            /* If any of the previous failed, unlock it anyway, and
            perhaps wait for next cycle for a correctly compiled JuliaDef */
            julia_compiler_barrier->Unlock();
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
        for(int i = 1; i < numInputs(); i++) //Skip first entry, it is the unique_id number
        {
            //need to index back, Julia's first buffer will be SC's second one, and so on, skipping unique_id at in(0)
            ((jl_array_t*)(ins[i - 1]))->data = (float*)in(i);

            /*
            //Is this a SC bug? input and output buffers at the same channel point at the same memory.
            //It maybe is a conservative memory feature, where same buffer is used for both of them.
            //However, this gives for granted that input value will be retrieved before output one.
            for(int y = 0; y < numOutputs(); y++)
            {
                if(in(i) == out(y))
                {
                    Print("Index: %d\n", i);
                    Print("SAME BUFFER \n");
                }
            }
            */
        }

        for(int i = 0; i < numOutputs(); i++)
            ((jl_array_t*)(outs[i]))->data = (float*)out(i);    

        /* I could perhaps have a "debug" and "perform" mode
        "debug" will run here, "perform" will just be used when you know the code 
        is working and is in ready state */
        /* MOREOVER, with supernova, I would need locks here for debugging code */
        JL_TRY {
            jl_invoke_already_compiled_SC(perform_instance, args, nargs);
            
            jl_exception_clear();
        }
        JL_CATCH {
            jl_get_ptls_states()->previous_exception = jl_current_exception();

            if(!print_once_exception)
            {
                jl_value_t* exception = jl_exception_occurred();
                jl_value_t* sprint_fun = julia_global_state->get_sprint_fun();
                jl_value_t* showerror_fun = julia_global_state->get_showerror_fun();

                if(exception)
                {
                    //Can i avoid this jl_call2 somehow???? Maybe I can emulate a fake exception at bootup and store that method pointer?
                    const char* returned_exception = jl_string_ptr(jl_call2(sprint_fun, showerror_fun, exception));
                    printf("ERROR: %s\n", returned_exception);
                }

                print_once_exception = true;
            }

            output_silence(inNumSamples);

            //Clear exception for successive calls into Julia.
            jl_exception_clear();
        }
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