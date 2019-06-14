/*
    JuliaCollider: Julia's JIT compilation for low-level audio synthesis and prototyping in SuperCollider.
    Copyright (C) 2019 Francesco Cameli. All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <random>
#include "JuliaObjectCompiler.h"
#include "JuliaUtilitiesMacros.hpp"

/* Actual compilation of an @object: from its module, to constructor, perform and destructor functions.
Also, all utilities functions (like __UGenRef__ stuff) are compiled here and assigned to a JuliaObject* */

JuliaObjectCompiler::JuliaObjectCompiler(World* in_world_, JuliaGlobalState* julia_global_)
{
    in_world = in_world_;
    julia_global = julia_global_;
}

jl_module_t* JuliaObjectCompiler::eval_julia_object(JuliaReplyWithLoadPath* julia_reply_with_load_path)
{
    const char* julia_load_path = julia_reply_with_load_path->get_julia_load_path();

    //printf("*** OSC_UNIQUE_ID %d ***\n", julia_reply_with_load_path->get_OSC_unique_id());
    //printf("*** LOAD PATH: %s ***\n", julia_load_path);

    jl_module_t* evaluated_module = eval_julia_file(julia_load_path);

    if(!evaluated_module)
    {
        printf("ERROR: Invalid module\n");
        return nullptr;
    }

    return evaluated_module;
}

bool JuliaObjectCompiler::compile_julia_object(JuliaObject* julia_object, jl_module_t* evaluated_module)
{
    //printf("*** MODULE NAME: %s *** \n", jl_symbol_name(evaluated_module->name));

    //If failed any precompilation stage, return false.
    if(!precompile_julia_object(evaluated_module, julia_object))
    {
        printf("ERROR: Failed in compiling Julia @object \"%s\"\n", jl_symbol_name(evaluated_module->name));
        return false;
    }

    //Set this object module to the evaluated one.
    julia_object->evaluated_module = evaluated_module;

    return true;
}

/*************************************************************************************************/
/*************************************************************************************************/
/*** This should also find a way to remove the module from Main, perhaps set it to jl_nothing? ***/
/*************************************************************************************************/
/*************************************************************************************************/

bool JuliaObjectCompiler::unload_julia_object(JuliaObject* julia_object)
{
    printf("Freeing @object...\n");

    if(!julia_object)
    {
        printf("ERROR: Invalid Julia @object \n");
        return false;
    }

    /* if(julia_object->RT_busy)
    {
        printf("WARNING: %s @object is still being used in a SynthDef\n", jl_symbol_name(julia_object->evaluated_module->name));
        return false;
    } */

    printf("IS COMPILED? %d\n", julia_object->compiled);

    if(julia_object->compiled)
    {   
        /* JL_TRY/CATCH here? */
        /* LOOK INTO Base.delete_method to delete method instances directly right now */
        if(delete_methods_from_table(julia_object))
        {
            remove_julia_object_from_global_def_id_dict(julia_object);
            null_julia_object(julia_object);
        }
    }

    printf("IS COMPILED? %d\n", julia_object->compiled);

    //Reset memory pointer for this object
    memset(julia_object, 0, sizeof(JuliaObject));

    printf("IS COMPILED? %d\n\n", julia_object->compiled);

    return true;
}

/* EVAL FILE */
jl_module_t* JuliaObjectCompiler::eval_julia_file(const char* path)
{
    jl_module_t* evaluated_module;
    
    JL_TRY {
        //DO I NEED TO ADVANCE AGE HERE???? Perhaps, I do.
        jl_get_ptls_states()->world_age = jl_get_world_counter();
        
        //The file MUST ONLY contain an @object definition (which loads a module)
        evaluated_module = (jl_module_t*)jl_load(jl_main_module, path);
        
        if(!evaluated_module)
            jl_error("Invalid julia file");

        if(!jl_is_module(evaluated_module))
            jl_error("Included file is not a Julia module");
        
        if(!jl_get_global_SC(evaluated_module, "__inputs__"))
            jl_error("Undefined @inputs");

        if(!jl_get_global_SC(evaluated_module, "__outputs__"))
            jl_error("Undefined @outputs");

        if(!jl_get_global_SC(evaluated_module, "__UGen__"))
            jl_error("Undefined @object");
        
        if(!jl_get_global_SC(evaluated_module, "__constructor__"))
            jl_error("Undefined @constructor");
        
        if(!jl_get_global_SC(evaluated_module, "__perform__"))
            jl_error("Undefined @perform");

        if(!jl_get_global_SC(evaluated_module, "__destructor__"))
            jl_error("Undefined @destructor"); 

        jl_exception_clear();
    }
    JL_CATCH {
        jl_get_ptls_states()->previous_exception = jl_current_exception();

        jl_value_t* exception = jl_exception_occurred();
        jl_value_t* sprint_fun = julia_global->get_sprint_fun();
        jl_value_t* showerror_fun = julia_global->get_showerror_fun();

        if(exception)
        {
            const char* returned_exception = jl_string_ptr(jl_call2(sprint_fun, showerror_fun, exception));
            printf("ERROR: %s\n", returned_exception);
        }

        evaluated_module = nullptr;
    }

    //Advance age after each include?? THIS IS PROBABLY UNNEEDED
    jl_get_ptls_states()->world_age = jl_get_world_counter();

    return evaluated_module;
};

void JuliaObjectCompiler::null_julia_object(JuliaObject* julia_object)
{
    /* STATE */
    julia_object->being_replaced = false;
    julia_object->compiled = false;
    //julia_object->RT_busy = false;

    /* PATH */
    julia_object->path.clear();

    /* JULIADEF */
    julia_object->julia_def = nullptr;
    
    /* MODULE */
    julia_object->evaluated_module = nullptr;
    julia_object->name = nullptr;
    julia_object->num_inputs = -1;
    julia_object->num_outputs = -1;

    /* FUNCTIONS */
    julia_object->ugen_ref_fun = nullptr;
    julia_object->constructor_fun = nullptr;
    julia_object->perform_fun = nullptr;
    julia_object->destructor_fun = nullptr;
    julia_object->set_index_ugen_ref_fun = nullptr;
    julia_object->delete_index_ugen_ref_fun = nullptr;

    /* METHOD INSTANCES */
    julia_object->ugen_ref_instance = nullptr;
    julia_object->constructor_instance = nullptr;
    julia_object->perform_instance = nullptr;
    julia_object->destructor_instance = nullptr;
    julia_object->set_index_ugen_ref_instance = nullptr;
    julia_object->delete_index_ugen_ref_instance = nullptr;
    julia_object->set_index_audio_vector_instance = nullptr;
}

void JuliaObjectCompiler::add_julia_object_to_global_def_id_dict(JuliaObject* julia_object)
{
    if(julia_object->julia_def)
        julia_global->get_global_def_id_dict().add_to_id_dict(julia_object->julia_def);
}

void JuliaObjectCompiler::remove_julia_object_from_global_def_id_dict(JuliaObject* julia_object)
{
    if(julia_object->julia_def)
        julia_global->get_global_def_id_dict().remove_from_id_dict(julia_object->julia_def);
}

bool JuliaObjectCompiler::precompile_julia_object(jl_module_t* evaluated_module, JuliaObject* julia_object)
{
    bool precompile_state = precompile_stages(evaluated_module, julia_object);

    //if any stage failed, keep nullptrs and memset to zero.
    if(!precompile_state)
    {
        null_julia_object(julia_object);
        unload_julia_object(julia_object);
        return false;
    }

    /* REMOVE jl_call() from here--- */
    add_julia_object_to_global_def_id_dict(julia_object);

    /* precompile_state = true */
    julia_object->compiled = precompile_state;

    return precompile_state;
}

bool JuliaObjectCompiler::precompile_stages(jl_module_t* evaluated_module, JuliaObject* julia_object)
{
    bool precompile_state = false;

    //object that will be created in perform and passed in destructor and ugen_ref, without creating new ones...
    jl_value_t* ugen_object;
    jl_value_t* ins;
    float** ins_float;
    jl_value_t* outs;
    jl_value_t* ugen_ref_object;
    jl_value_t* destructor_fun;
    jl_method_instance_t* destructor_instance;

    //jl_get_ptls_states()->world_age = jl_get_world_counter();
    
    /* These functions will return false if anything goes wrong. */
    if(precompile_constructor(evaluated_module, &ugen_object, &ins, &ins_float, julia_object))
    {
        //jl_get_ptls_states()->world_age = jl_get_world_counter();
        //printf("CONSTRUCTOR DONE\n");
        if(precompile_perform(evaluated_module, &ugen_object, &ins, &ins_float, &outs, julia_object))
        {
            //jl_get_ptls_states()->world_age = jl_get_world_counter();
            //printf("PERFORM DONE\n");
            //jl_call1(jl_get_function(jl_base_module, "println"), ugen_object);
            if(precompile_destructor(evaluated_module, &ugen_object, &destructor_fun, &destructor_instance, julia_object))
            {
                //jl_get_ptls_states()->world_age = jl_get_world_counter();
                //printf("DESTRUCTOR DONE\n");
                if(precompile_ugen_ref(evaluated_module, &ugen_object, &ins, &outs, &destructor_fun, &destructor_instance, &ugen_ref_object, julia_object))
                {
                    //jl_get_ptls_states()->world_age = jl_get_world_counter();
                    //printf("UGEN REF DONE\n");
                    if(precompile_set_index_delete_index_julia_def(evaluated_module, &ugen_ref_object, julia_object))
                    {
                        //jl_get_ptls_states()->world_age = jl_get_world_counter();
                        //printf("SET INDEX DONE\n");
                        if(create_julia_def(evaluated_module, julia_object))
                        {
                            //jl_get_ptls_states()->world_age = jl_get_world_counter();
                            precompile_state = true;
                        }
                    }
                }
            }
        }
    }
    
    //jl_get_ptls_states()->world_age = jl_get_world_counter();

    return precompile_state;
}

bool JuliaObjectCompiler::precompile_constructor(jl_module_t* evaluated_module, jl_value_t** ugen_object, jl_value_t** ins, float*** ins_float, JuliaObject* julia_object)
{
    size_t constructor_nargs = 3;
    jl_value_t* constructor_args[constructor_nargs];

    jl_function_t* constructor_fun = jl_get_function(evaluated_module, "__constructor__");
    if(!constructor_fun)
    {
        printf("ERROR: Invalid __constructor__ function\n");
        return false;
    }

    /* SET INDEX AUDIO VECTOR */
    size_t nargs_set_index_audio_vector = 4;
    jl_value_t* args_set_index_audio_vector[nargs_set_index_audio_vector];
    jl_method_instance_t* set_index_audio_vector_instance;

    int num_inputs =  jl_unbox_int32(jl_get_global_SC(evaluated_module, "__inputs__"));
    int buffer_size = 8; //Try compiling with a 8 samples period.

    //ins::Vector{Vector{Float32}}
    jl_value_t* ins_temp =  (jl_value_t*)jl_alloc_array_1d(julia_global->get_vector_of_vectors_float32(), num_inputs);
    if(!ins_temp)
    {
        printf("ERROR: Could not allocate memory for inputs \n");
        return false;
    }

    //Multiple 1D Arrays for each output buffer
    jl_value_t* ins_1d[num_inputs];

    //Dummy float** in()
    float** dummy_ins = (float**)malloc(num_inputs * sizeof(float*));

    //Initialize seed for random num generation in dummy_ins
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> rand_vals(-1.0, 1.0);

    for(int i = 0; i < num_inputs; i++)
    {
        dummy_ins[i] = (float*)malloc(buffer_size * sizeof(float));
        if(!dummy_ins[i])
        {
            printf("ERROR: Could not allocate memory for inputs \n");
            for(int y = 0; y < i; y++)
                free(dummy_ins[y]);
            free(dummy_ins);
            return false;
        }

        //Emulate some sort of (-1 / 1) random float data as Input 
        for(int y = 0; y < buffer_size; y++)
            dummy_ins[i][y] = rand_vals(gen);
        
        ins_1d[i] = (jl_value_t*)jl_ptr_to_array_1d(julia_global->get_vector_float32(), dummy_ins[i], buffer_size, 0);
        if(!ins_1d[i])
        {
            printf("ERROR: Could not create 1d vectors (inputs)\n");
            for(int y = 0; y < i; y++)
                free(dummy_ins[y]);
            free(dummy_ins);
            return false;
        }

        /* Replace values each loop. No need to allocate more */
        args_set_index_audio_vector[0] = julia_global->get_set_index_audio_vector_fun();
        args_set_index_audio_vector[1] = ins_temp;
        args_set_index_audio_vector[2] = ins_1d[i];
        args_set_index_audio_vector[3] = jl_box_int32(i + 1); //Julia index from 1 onwards

        //compile the audio vector set_index function at first iteration
        if(i == 0)
        {
            set_index_audio_vector_instance = jl_lookup_generic_and_compile_SC(args_set_index_audio_vector, nargs_set_index_audio_vector);
            if(!set_index_audio_vector_instance)
            {
                for(int y = 0; y < i; y++)
                    free(dummy_ins[y]);
                free(dummy_ins);
                return false;
            }
        }
        
        //Use it to set the actual stuff inside the array
        jl_value_t* set_index_success = jl_lookup_generic_and_compile_return_value_SC(args_set_index_audio_vector, nargs_set_index_audio_vector);
        if(!set_index_success)
        {
            printf("ERROR: Could not instantiate set_index_audio_vector function (inputs)\n");
            for(int y = 0; y < i; y++)
                free(dummy_ins[y]);
            free(dummy_ins);
            return false;
        }
    }

    constructor_args[0] = constructor_fun;
    constructor_args[1] = ins_temp;
    constructor_args[2] = julia_global->get_scsynth();
    
    /* COMPILATION */
    jl_method_instance_t* constructor_instance = jl_lookup_generic_and_compile_SC(constructor_args, constructor_nargs);
    if(!constructor_instance)
    {
        printf("ERROR: Could not compile __constructor__ function\n");
        for(int i = 0; i < num_inputs; i++)
            free(dummy_ins[i]);
        free(dummy_ins);
        return false;
    }

    /* Build an object */
    jl_value_t* ugen_object_temp = jl_invoke_already_compiled_SC(constructor_instance, constructor_args, constructor_nargs);
    if(!ugen_object_temp)
    {
        printf("ERROR: Could not construct a __UGen__ object\n");
        for(int i = 0; i < num_inputs; i++)
            free(dummy_ins[i]);
        free(dummy_ins);
        return false;
    }

    ugen_object[0] = ugen_object_temp;
    ins[0] = ins_temp;
    ins_float[0] = dummy_ins;

    julia_object->constructor_fun = constructor_fun;
    julia_object->constructor_instance = constructor_instance;
    julia_object->set_index_audio_vector_instance = set_index_audio_vector_instance;
    
    return true;
}

bool JuliaObjectCompiler::precompile_perform(jl_module_t* evaluated_module, jl_value_t** ugen_object, jl_value_t** ins, float*** ins_float, jl_value_t** outs, JuliaObject* julia_object)
{
    float** dummy_ins = ins_float[0];

    /* INS / OUTS = perform_args[2]/[3] */
    int num_inputs =  jl_unbox_int32(jl_get_global_SC(evaluated_module, "__inputs__"));
    int num_outputs = jl_unbox_int32(jl_get_global_SC(evaluated_module, "__outputs__"));
    int buffer_size = 8; //Try compiling with a 8 samples period.

    /* ARRAY CONSTRUCTION */
    size_t perform_nargs = 6;
    jl_value_t* perform_args[perform_nargs];

    /* FUNCTION = perform_args[0] */
    jl_function_t* perform_fun = jl_get_function(evaluated_module, "__perform__");
    if(!perform_fun)
    {
        printf("ERROR: Invalid __perform__ function\n");
        for(int i = 0; i < num_inputs; i++)
            free(dummy_ins[i]);
        free(dummy_ins);
        return false;
    }

    /* OBJECT CONSTRUCTION = perform_args[1] */
    jl_function_t* constructor_fun = jl_get_function(evaluated_module, "__constructor__");
    if(!constructor_fun)
    {
        printf("ERROR: Invalid __constructor__ function\n");
        for(int i = 0; i < num_inputs; i++)
            free(dummy_ins[i]);
        free(dummy_ins);
        return false;
    }

    /* SET INDEX AUDIO VECTOR */
    size_t nargs_set_index_audio_vector = 4;
    jl_value_t* args_set_index_audio_vector[nargs_set_index_audio_vector];
    //jl_method_instance_t* set_index_audio_vector_instance;

    //outs::Vector{Vector{Float32}}
    jl_value_t* outs_temp = (jl_value_t*)jl_alloc_array_1d(julia_global->get_vector_of_vectors_float32(), num_outputs);
    if(!outs_temp)
    {
        printf("ERROR: Could not allocate memory for outputs \n");
        for(int i = 0; i < num_inputs; i++)
            free(dummy_ins[i]);
        free(dummy_ins);
        return false;
    }

    //Multiple 1D Arrays for each output buffer
    jl_value_t* outs_1d[num_outputs];

    //Dummy float** out()
    float* dummy_outs[num_outputs];

    for(int i = 0; i < num_outputs; i++)
    {
        dummy_outs[i] = (float*)malloc(buffer_size * sizeof(float));
        if(!dummy_outs[i])
        {
            printf("ERROR: Could not allocate memory for outs \n");

            for(int y = 0; y < num_inputs; y++)
                free(dummy_ins[y]);
            free(dummy_ins);

            for(int z = 0; z < i; z++)
                free(dummy_outs[z]);

            return false;
        }

        for(int y = 0; y < buffer_size; y++)
            dummy_outs[i][y] = 0.0f;   
        
        outs_1d[i] = (jl_value_t*)jl_ptr_to_array_1d(julia_global->get_vector_float32(), dummy_outs[i], buffer_size, 0);
        if(!outs_1d[i])
        {
            printf("ERROR: Could not create 1d vectors (outputs)\n");
            
            for(int y = 0; y < num_inputs; y++)
                free(dummy_ins[y]);
            free(dummy_ins);

            for(int z = 0; z < i; z++)
                free(dummy_outs[z]);

            return false;
        }

        /* Replace values each loop. No need to allocate more */
        args_set_index_audio_vector[0] = julia_global->get_set_index_audio_vector_fun();
        args_set_index_audio_vector[1] = outs_temp;
        args_set_index_audio_vector[2] = outs_1d[i];
        args_set_index_audio_vector[3] = jl_box_int32(i + 1); //Julia index from 1 onwards

        //Use it
        jl_value_t* set_index_success = jl_lookup_generic_and_compile_return_value_SC(args_set_index_audio_vector, nargs_set_index_audio_vector);
        if(!set_index_success)
        {
            printf("ERROR: Could not instantiate set_index_audio_vector function (outputs)\n");

            for(int y = 0; y < num_inputs; y++)
                free(dummy_ins[y]);
            free(dummy_ins);

            for(int z = 0; z < i; z++)
                free(dummy_outs[z]);

            return false;
        }
    }

    /* ASSIGN TO ARRAY */
    perform_args[0] = perform_fun;
    perform_args[1] = ugen_object[0];
    perform_args[2] = ins[0];  //__ins__
    perform_args[3] = outs_temp; //__outs__
    perform_args[4] = jl_box_int32(buffer_size); //__buffer_size__ 
    perform_args[5] = julia_global->get_scsynth(); //__SCSynth__

    /* COMPILATION. Should it be with precompile() instead? */
    jl_method_instance_t* perform_instance = jl_lookup_generic_and_compile_SC(perform_args, perform_nargs);

    for(int i = 0; i < num_inputs; i++)
        free(dummy_ins[i]);

    free(dummy_ins);

    for(int i = 0; i < num_outputs; i++)
        free(dummy_outs[i]);

    /* JULIA OBJECT ASSIGN */
    if(!perform_instance)
    {
        printf("ERROR: Could not compile __perform__ function\n");
        return false;
    }

    outs[0] = outs_temp;

    //successful compilation...
    julia_object->perform_fun = perform_fun;
    julia_object->perform_instance = perform_instance;

    return true;
}

bool JuliaObjectCompiler::precompile_destructor(jl_module_t* evaluated_module, jl_value_t** ugen_object, jl_value_t** destructor_fun, jl_method_instance_t** destructor_instance, JuliaObject* julia_object)
{
    jl_function_t* destructor_fun_temp = jl_get_function(evaluated_module, "__destructor__");
    if(!destructor_fun_temp)
    {
        printf("ERROR: Invalid __destructor__ function\n");
        return false;
    }

    //printf("DESTRUCTOR FUN DONE\n");

    int32_t destructor_nargs = 2;
    jl_value_t* destructor_args[destructor_nargs];

    //printf("DESTRUCTOR ALLOC DONE\n");

    destructor_args[0] = destructor_fun_temp;
    destructor_args[1] = ugen_object[0];

    if(!ugen_object)
    {
        printf("ERROR: Invalid ugen_object to destructor\n");
        return false;
    }
    
    /* COMPILATION */
    jl_method_instance_t* destructor_instance_temp = jl_lookup_generic_and_compile_SC(destructor_args, destructor_nargs);

    //printf("DESTRUCTOR METHOD DONE\n");

    if(!destructor_instance_temp)
    {
        printf("ERROR: Could not compile __destructor__ function\n");
        return false;
    }

    julia_object->destructor_fun = destructor_fun_temp;
    julia_object->destructor_instance = destructor_instance_temp;

    destructor_fun[0] = destructor_fun_temp;
    destructor_instance[0] = destructor_instance_temp;

    return true;
}

bool JuliaObjectCompiler::precompile_ugen_ref(jl_module_t* evaluated_module, jl_value_t** ugen_object, jl_value_t** ins, jl_value_t** outs, jl_value_t** destructor_fun, jl_method_instance_t** destructor_instance, jl_value_t** ugen_ref_object, JuliaObject* julia_object)
{
    jl_function_t* ugen_ref_fun = jl_get_function(evaluated_module, "__UGenRef__");
    if(!ugen_ref_fun)
    {
        printf("ERROR: Invalid __UGenRef__ function\n");
        return false;
    }

    //Ins and outs are pointing to junk data, but I don't care. I just need to precompile the Ref to it.

    int32_t ugen_ref_nargs = 6;
    jl_value_t* ugen_ref_args[ugen_ref_nargs];
    
    //__UGenRef__ constructor
    ugen_ref_args[0] = ugen_ref_fun;
    ugen_ref_args[1] = ugen_object[0];
    ugen_ref_args[2] = ins[0];
    ugen_ref_args[3] = outs[0];
    ugen_ref_args[4] = destructor_fun[0];
    ugen_ref_args[5] = (jl_value_t*)destructor_instance[0];

    /* COMPILATION */
    jl_method_instance_t* ugen_ref_instance = jl_lookup_generic_and_compile_SC(ugen_ref_args, ugen_ref_nargs);

    if(!ugen_ref_instance)
    {
        printf("ERROR: Could not compile __UGenRef__ function\n");
        return false;
    }

    //Create an actual object with same args to pass it to the precompilation of set_index and delete_index functions.
    //Maybe I should not use exception here?
    jl_value_t* ugen_ref_object_temp = jl_lookup_generic_and_compile_return_value_SC(ugen_ref_args, ugen_ref_nargs);
    
    if(!ugen_ref_object)
    {
        printf("ERROR: Could not precompile a __UGenRef__ object\n");
        return false;
    }

    //ASSIGN OBJECT TO POINTER
    ugen_ref_object[0] = ugen_ref_object_temp;

    julia_object->ugen_ref_fun = ugen_ref_fun;
    julia_object->ugen_ref_instance = ugen_ref_instance;

    return true;
}

bool JuliaObjectCompiler::precompile_set_index_delete_index_julia_def(jl_module_t* evaluated_module, jl_value_t** ugen_ref_object, JuliaObject* julia_object)
{
    //Precompile set_index and delete_index
    jl_function_t* set_index_ugen_ref_fun = jl_get_function(evaluated_module, "set_index_ugen_ref");
    jl_function_t* delete_index_ugen_ref_fun = jl_get_function(evaluated_module, "delete_index_ugen_ref");
    
    if(!set_index_ugen_ref_fun || !delete_index_ugen_ref_fun)
    {
        printf("ERROR: Invalid set_index or delete_index\n");
        return false;
    }

    jl_value_t* global_object_id_dict = julia_global->get_global_object_id_dict().get_id_dict();
    if(!global_object_id_dict)
    {
        printf("ERROR: Invalid global_object_id_dict\n");
        return false;
    }

    int32_t set_index_nargs = 3;
    jl_value_t* set_index_args[set_index_nargs];
    
    int32_t delete_index_nargs = 3;
    jl_value_t* delete_index_args[delete_index_nargs];

    //set index
    set_index_args[0] = set_index_ugen_ref_fun;
    set_index_args[1] = global_object_id_dict;
    set_index_args[2] = ugen_ref_object[0];

    //delete index
    delete_index_args[0] = delete_index_ugen_ref_fun;
    delete_index_args[1] = global_object_id_dict;
    delete_index_args[2] = ugen_ref_object[0];

    /* COMPILATION */
    //This will add to global_object_id_dict
    jl_method_instance_t* set_index_ugen_ref_instance = jl_lookup_generic_and_compile_SC(set_index_args, set_index_nargs);
    if(!set_index_ugen_ref_instance)
    {
        printf("ERROR: Could not compile set_index_ugen_ref_instance\n");
        return false;
    }

    //This will delete right away what's been just added.
    jl_method_instance_t* delete_index_ugen_ref_instance = jl_lookup_generic_and_compile_SC(delete_index_args, delete_index_nargs);
    if(!delete_index_ugen_ref_instance)
    {
        printf("ERROR: Could not compile delete_index_ugen_ref_instance\n");
        return false;
    }

    julia_object->set_index_ugen_ref_fun = set_index_ugen_ref_fun;
    julia_object->delete_index_ugen_ref_fun = delete_index_ugen_ref_fun;
    julia_object->set_index_ugen_ref_instance = set_index_ugen_ref_instance;
    julia_object->delete_index_ugen_ref_instance = delete_index_ugen_ref_instance;

    return true;
}

bool JuliaObjectCompiler::create_julia_def(jl_module_t* evaluated_module, JuliaObject* julia_object)
{
    jl_function_t* julia_def_fun = julia_global->get_julia_def_fun();
    if(!julia_def_fun)
    {
        printf("ERROR: Invalid julia_def_fun\n");
        return false;
    }

    //Stack allocation...
    int julia_def_function_nargs = 8;
    int32_t nargs = julia_def_function_nargs + 1;
    jl_value_t* julia_def_args[nargs];

    //__JuliaDef__ constructor
    /* Only setting the module and the method_instance_t*.
    No need to add the function too, as they are kept alive as long as the module.*/
    julia_def_args[0] = (jl_value_t*)julia_def_fun;
    julia_def_args[1] = (jl_value_t*)evaluated_module;
    julia_def_args[2] = (jl_value_t*)julia_object->ugen_ref_instance;
    julia_def_args[3] = (jl_value_t*)julia_object->constructor_instance;
    julia_def_args[4] = (jl_value_t*)julia_object->perform_instance;
    julia_def_args[5] = (jl_value_t*)julia_object->destructor_instance;
    julia_def_args[6] = (jl_value_t*)julia_object->set_index_ugen_ref_instance;
    julia_def_args[7] = (jl_value_t*)julia_object->delete_index_ugen_ref_instance;
    julia_def_args[8] = (jl_value_t*)julia_object->set_index_audio_vector_instance;

    //jl_call1(jl_get_function(jl_base_module, "println"), julia_def_fun);

    //printf("JULIA_DEF BEFORE CALL\n");
    
    jl_value_t* julia_def = jl_lookup_generic_and_compile_return_value_SC(julia_def_args, nargs);

    //jl_call1(jl_get_function(jl_base_module, "println"), julia_def);

    if(!julia_def)
    {
        printf("ERROR: Could not create a __JuliaDef__\n");   
        return false;
    }         

    julia_object->julia_def = julia_def;

    return true;
}

/* ALL THESE SHOULD NOT jl_call(), but INVOKE */
bool JuliaObjectCompiler::delete_methods_from_table(JuliaObject* julia_object)
{
    /*
    jl_function_t* delete_method_fun = jl_get_function(jl_base_module, "delete_method");
    if(!delete_method_fun)
    {
        printf("ERROR: Could not retrieve \"delete method\" function \n");
        return false;
    }

    jl_method_t* ugen_ref_method = (julia_object->ugen_ref_instance)->def.method;
    jl_value_t* ugen_ref_method_call = jl_call2(delete_method_fun, (jl_value_t*)ugen_ref_method, (julia_object->ugen_ref_instance)->specTypes); //IS specTypes a Tuple???
    if(!ugen_ref_method || !ugen_ref_method_call)
    {
        printf("ERROR: Could not retrieve method for ugen_ref_instance\n");
        return false;
    }

    jl_method_t* constructor_method = (julia_object->constructor_instance)->def.method;
    jl_value_t* constructor_method_call = jl_call2(delete_method_fun, (jl_value_t*)constructor_method, (julia_object->constructor_instance)->specTypes); //IS specTypes a Tuple???
    if(!constructor_method || constructor_method_call)
    {
        printf("ERROR: Could not retrieve method for constructor_instance\n");
        return false;
    }

    jl_method_t* perform_method = (julia_object->perform_instance)->def.method;
    jl_value_t* perform_method_call = jl_call2(delete_method_fun, (jl_value_t*)perform_method, (julia_object->perform_instance)->specTypes); //IS specTypes a Tuple???
    if(!perform_method || !perform_method_call)
    {
        printf("ERROR: Could not retrieve method for perform_instance\n");
        return false;
    }

    jl_method_t* destructor_method = (julia_object->destructor_instance)->def.method;
    jl_value_t* destructor_method_call = jl_call2(delete_method_fun, (jl_value_t*)destructor_method, (julia_object->destructor_instance)->specTypes); //IS specTypes a Tuple???
    if(!destructor_method || !destructor_method_call)
    {
        printf("ERROR: Could not retrieve method for destructor_instance\n");
        return false;
    }

    jl_method_t* set_index_ugen_ref_method = (julia_object->set_index_ugen_ref_instance)->def.method;
    jl_value_t* set_index_ugen_ref_method_call = jl_call2(delete_method_fun, (jl_value_t*)set_index_ugen_ref_method, (julia_object->set_index_ugen_ref_instance)->specTypes); //IS specTypes a Tuple???
    if(!set_index_ugen_ref_method || !set_index_ugen_ref_method_call)
    {
        printf("ERROR: Could not retrieve method for set_index_ugen_ref_instance\n");
        return false;
    }

    jl_method_t* delete_index_ugen_ref_method = (julia_object->delete_index_ugen_ref_instance)->def.method;
    jl_value_t* delete_index_ugen_ref_method_call = jl_call2(delete_method_fun, (jl_value_t*)delete_index_ugen_ref_method, (julia_object->delete_index_ugen_ref_instance)->specTypes); //IS specTypes a Tuple???
    if(!delete_index_ugen_ref_method || !delete_index_ugen_ref_method_call)
    {
        printf("ERROR: Could not retrieve method for delete_index_ugen_ref_instance\n");
        return false;
    }

    jl_method_t* set_index_audio_vector_method = (julia_object->set_index_audio_vector_instance)->def.method;
    jl_value_t* set_index_audio_vector_method_call = jl_call2(delete_method_fun, (jl_value_t*)set_index_audio_vector_method, (julia_object->set_index_audio_vector_instance)->specTypes); //IS specTypes a Tuple???
    if(!set_index_audio_vector_method || !set_index_audio_vector_method_call)
    {
        printf("ERROR: Could not retrieve method for set_index_audio_vector_instance\n");
        return false;
    }
    */
    
    return true;
}
