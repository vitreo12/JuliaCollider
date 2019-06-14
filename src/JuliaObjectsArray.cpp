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

#include "JuliaObjectsArray.h"
#include "JuliaUtilitiesMacros.hpp"

/* Array of JuliaObject*. Fixed size of 1000 (for now) */

/***********************/
/* JuliaEntriesCounter */
/***********************/

void JuliaEntriesCounter::advance_active_entries()
{
    active_entries++;
}

void JuliaEntriesCounter::decrease_active_entries()
{
    active_entries--;
    if(active_entries < 0)
        active_entries = 0;
}

int JuliaEntriesCounter::get_active_entries()
{
    return active_entries;
}

/*********************/
/* JuliaObjectsArray */
/*********************/

JuliaObjectsArray::JuliaObjectsArray(World* in_world_, JuliaGlobalState* julia_global_) : JuliaObjectCompiler(in_world_, julia_global_)
{
    init_julia_objects_array();
}

JuliaObjectsArray::~JuliaObjectsArray()
{
    destroy_julia_objects_array();
}

/* NRT THREAD. Called at JuliaDef.new() */
bool JuliaObjectsArray::create_julia_object(JuliaReplyWithLoadPath* julia_reply_with_load_path)
{  
    int new_id;
    JuliaObject* julia_object;
    
    // FUTURE: resizable array
    /*
    check_resizable_array();
    */

    //Array is full
    if(get_active_entries() >= num_total_entries)
    {
        printf("ERROR: Reached maximum limit (%d) of active JuliaDefs. Free before creating new ones. \n", num_total_entries);
        return false;
    }

    //Run code evaluation and module compilation
    jl_module_t* evaluated_module = eval_julia_object(julia_reply_with_load_path);
    if(!evaluated_module)
    {
        julia_reply_with_load_path->create_done_command(julia_reply_with_load_path->get_OSC_unique_id(), "/jl_load", -1, "@No_Name", -1, -1);
        return false;
    }
    
    //If an @object already exist, just replace the content at that ID and set its "being_replaced" flag to true.
    new_id = check_existing_module(evaluated_module);
    if(new_id >= 0) //Found a julia_object to be replaced
        julia_object = julia_objects_array + new_id; 
    else if(new_id < 0) //No existing julia_objects with same module name. Get a new id
    {
        //Retrieve a new ID be checking out the first free entry in the julia_objects_array
        for(int i = 0; i < num_total_entries; i++)
        {
            JuliaObject* this_julia_object = julia_objects_array + i;
            if(!this_julia_object->compiled) //If empty entry, it means I can take ownership
            {
                julia_object = this_julia_object;
                new_id = i;
                break;
            }
        }
    }

    //Unload previous object from same id if it's being replaced by same @object. It assumes NRT thread has lock.
    if(julia_object->being_replaced)
        delete_julia_object(new_id);

    //Run object's compilation.
    bool succesful_compilation = compile_julia_object(julia_object, evaluated_module);
    if(!succesful_compilation)
    {
        julia_reply_with_load_path->create_done_command(julia_reply_with_load_path->get_OSC_unique_id(), "/jl_load", -1, "@No_Name", -1, -1);
        return false;
    }

    const char* name = jl_symbol_name(evaluated_module->name);
    int num_inputs =  jl_unbox_int32(jl_get_global_SC(evaluated_module, "__inputs__"));
    int num_outputs = jl_unbox_int32(jl_get_global_SC(evaluated_module, "__outputs__"));

    //SHOULD IT BE SET BEFORE AT OBJECT COMPILATION?
    julia_object->path = julia_reply_with_load_path->get_julia_load_path(); //string performs deep copy on a const char*
    julia_object->name = name;
    julia_object->num_inputs = num_inputs;
    julia_object->num_outputs = num_outputs;

    //Set unique_id in newly created module aswell. Used to retrieve JuliaDef by module name.
    jl_function_t* set_unique_id_fun = jl_get_function(evaluated_module, "__set_unique_id__");

    int32_t nargs_set_unique_id = 2;
    jl_value_t* args_set_unique_id[nargs_set_unique_id];
    
    args_set_unique_id[0] = set_unique_id_fun;
    args_set_unique_id[1] = jl_box_int32(num_inputs);

    jl_value_t* succesful_set_unique_id = jl_lookup_generic_and_compile_return_value_SC(args_set_unique_id, nargs_set_unique_id);
    if(!succesful_set_unique_id)
    {
        julia_reply_with_load_path->create_done_command(julia_reply_with_load_path->get_OSC_unique_id(), "/jl_load", -1, "@No_Name", -1, -1);
        return false;
    }
    
    //MSG: OSC id, cmd, id, name, inputs, outputs
    julia_reply_with_load_path->create_done_command(julia_reply_with_load_path->get_OSC_unique_id(), "/jl_load", new_id, name, num_inputs, num_outputs);

    advance_active_entries();

    return true;
}

int JuliaObjectsArray::check_existing_module(jl_module_t* evaluated_module)
{
    int active_entries = get_active_entries();
    if(!active_entries)
        return -1;
    
    int entries_count = 0;

    for(int i = 0; i < num_total_entries; i++)
    {
        JuliaObject* this_julia_object = julia_objects_array + i;
        if(this_julia_object->compiled)
        {
            char* eval_module_name = jl_symbol_name(evaluated_module->name);
            char* this_julia_object_module_name = jl_symbol_name((this_julia_object->evaluated_module)->name);
            
            //Compare string content
            if(strcmp(eval_module_name, this_julia_object_module_name) == 0)
            {
                printf("WARNING: Replacing @object: %s\n", eval_module_name);
                this_julia_object->being_replaced = true;
                return i;
            }

            entries_count++;
            //Scanned through all active entries.
            if(entries_count == active_entries)
                return -1;
        }
    }

    //No other module with same name
    return -1;
}

/* RT THREAD. Called when a Julia UGen is created on the server.
No need to run locks here, as RT execution will only happen if compiler barrier
is inactive anyway: it's already locked from the Julia.cpp code.*/
JuliaObjectsArrayState JuliaObjectsArray::get_julia_object(int unique_id, JuliaObject** julia_object)
{
    //This barrier will be useful when support for resizing the array will be added.
    //bool barrier_acquired = JuliaAtomicBarrier::RTTrylock();

    //if(barrier_acquired)
    //{
        JuliaObject* this_julia_object = julia_objects_array + unique_id;

        if(this_julia_object->compiled)
            julia_object[0] = this_julia_object;
        else
        {
            //JuliaDef compiled for another server.
            //printf("WARNING: Invalid @object. Perhaps this JuliaDef is not valid on this server \n");
            //JuliaAtomicBarrier::Unlock();
            return JuliaObjectsArrayState::Invalid;
        }

        //JuliaAtomicBarrier::Unlock();
        return JuliaObjectsArrayState::Free;
    //}
    
    //return JuliaObjectsArrayState::Busy;
}

/* NRT THREAD. Called at JuliaDef.free() */
void JuliaObjectsArray::delete_julia_object(int unique_id)
{
    JuliaObject* this_julia_object = julia_objects_array + unique_id;
    
    if(unload_julia_object(this_julia_object))
        decrease_active_entries();
}

void JuliaObjectsArray::get_julia_objects_list(JuliaReply* julia_reply)
{
    julia_reply->create_done_command(julia_reply->get_OSC_unique_id(), "/jl_get_julia_objects_list");
    
    int active_entries = get_active_entries();
    if(!active_entries)
        return;
    
    int entries_count = 0;

    for(int i = 0; i < num_total_entries; i++)
    {
        JuliaObject* this_julia_object = julia_objects_array + i;

        if(this_julia_object->compiled)
        {
            //Accumulate results
            julia_reply->create_done_command(this_julia_object->name);
            
            entries_count++;
            
            //Scanned through all active entries.
            if(entries_count == active_entries)
                return;
        }
    }
}

void JuliaObjectsArray::get_julia_object_by_name(JuliaReplyWithLoadPath* julia_reply)
{
    int active_entries = get_active_entries();
    if(!active_entries)
        return;

    const char* name = julia_reply->get_julia_load_path();

    int entries_count = 0;

    for(int i = 0; i < num_total_entries; i++)
    {
        JuliaObject* this_julia_object = julia_objects_array + i;

        if(this_julia_object->compiled)
        {
            if(strcmp(name, this_julia_object->name) == 0)
            {
                int new_id = i;
                //send path in place of name
                julia_reply->create_done_command(julia_reply->get_OSC_unique_id(), "/jl_get_julia_object_by_name", new_id, this_julia_object->path.c_str(), this_julia_object->num_inputs, this_julia_object->num_outputs);
                break;
            }
            
            entries_count++;
            
            //Scanned through all active entries, no success.
            if(entries_count == active_entries)
            {
                printf("WARNING: Unable to find any @object with name: %s\n", name);
                julia_reply->create_done_command(julia_reply->get_OSC_unique_id(), "/jl_get_julia_object_by_name", -1, "", -1, -1);
                return;
            }
        }
    }
}

//Constructor
void JuliaObjectsArray::init_julia_objects_array()
{
    julia_objects_array = (JuliaObject*)calloc(num_total_entries, sizeof(JuliaObject));
    
    if(!julia_objects_array)
    {
        printf("ERROR:Failed to allocate memory for JuliaObjects class \n");
        return;
    }
}

//Destructor
void JuliaObjectsArray::destroy_julia_objects_array()
{
    free(julia_objects_array);
}
