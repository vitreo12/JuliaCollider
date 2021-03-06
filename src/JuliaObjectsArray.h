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

#pragma once

#include "JuliaObjectCompiler.h"
#include "JuliaAtomicBarrier.h"

/* Array of JuliaObject*. Fixed size of 1000 (for now) */

/***********************/
/* JuliaEntriesCounter */
/***********************/

class JuliaEntriesCounter
{
    public:
        void advance_active_entries();

        void decrease_active_entries();

        int get_active_entries();

    private:
        int active_entries = 0;
};

/*********************/
/* JuliaObjectsArray */
/*********************/

//For retrieval on RT thread. Busy will only be usde when added support for resizing the array
enum class JuliaObjectsArrayState {Busy, Free, Invalid};

#define JULIA_OBJECTS_ARRAY_COUNT 1000

/* Allocate it with a unique_ptr? Or just a normal new/delete? */
class JuliaObjectsArray : public JuliaObjectCompiler, public JuliaAtomicBarrier, public JuliaEntriesCounter
{
    public:
        JuliaObjectsArray(World* in_world_, JuliaGlobalState* julia_global_);

        ~JuliaObjectsArray();

        /* NRT THREAD. Called at JuliaDef.new() */
        bool create_julia_object(JuliaReplyWithLoadPath* julia_reply_with_load_path);

        int check_existing_module(jl_module_t* evaluated_module);

        /* RT THREAD. Called when a Julia UGen is created on the server.
        No need to run locks here, as RT execution will only happen if compiler barrier
        is inactive anyway: it's already locked from the Julia.cpp code.*/
        JuliaObjectsArrayState get_julia_object(int unique_id, JuliaObject** julia_object);

        /* NRT THREAD. Called at JuliaDef.free() */
        void delete_julia_object(int unique_id);

        void get_julia_objects_list(JuliaReply* julia_reply);

        void get_julia_object_by_name(JuliaReplyWithLoadPath* julia_reply);

    private:
        //Array of JuliaObject(s)
        JuliaObject* julia_objects_array = nullptr;

        //Fixed size: 1000 @object entries for the array.
        int num_total_entries = JULIA_OBJECTS_ARRAY_COUNT;

        //Constructor
        void init_julia_objects_array();

        //Destructor
        void destroy_julia_objects_array();
        
        /* 
        // FUTURE: Resizable array
        void check_resizable_array()
        {

        }

        void advance_num_total_entries()
        {
            num_total_entries += JULIA_OBJECTS_ARRAY_COUNT;
        }

        void decrease_num_total_entries()
        {
            num_total_entries -= JULIA_OBJECTS_ARRAY_COUNT;
            if(num_total_entries < 0)
                num_total_entries = 0;
        }
        */
};
