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

#include "SC_PlugIn.hpp"
#include "JuliaGlobalState.h"
#include "JuliaObject.h"
#include "JuliaReplyCmds.h"

/* Actual compilation of an @object: from its module, to constructor, perform and destructor functions.
Also, all utilities functions (like __UGenRef__ stuff) are compiled here and assigned to a JuliaObject* */

/***********************/
/* JuliaObjectCompiler */
/***********************/

class JuliaObjectCompiler
{
    public:
        JuliaObjectCompiler(World* in_world_, JuliaGlobalState* julia_global_);

        ~JuliaObjectCompiler() {}

        jl_module_t* eval_julia_object(JuliaReplyWithLoadPath* julia_reply_with_load_path);

        bool compile_julia_object(JuliaObject* julia_object, jl_module_t* evaluated_module);

        /*************************************************************************************************/
        /*************************************************************************************************/
        /*** This should also find a way to remove the module from Main, perhaps set it to jl_nothing? ***/
        /*************************************************************************************************/
        /*************************************************************************************************/
        
        bool unload_julia_object(JuliaObject* julia_object);
    
    private:
        /* VARIABLES */
        World* in_world;
        JuliaGlobalState* julia_global;

        /* EVAL FILE */
        jl_module_t* eval_julia_file(const char* path);

        /* FUNCTIONS */
        void null_julia_object(JuliaObject* julia_object);

        void add_julia_object_to_global_def_id_dict(JuliaObject* julia_object);

        void remove_julia_object_from_global_def_id_dict(JuliaObject* julia_object);

        bool precompile_julia_object(jl_module_t* evaluated_module, JuliaObject* julia_object);

        bool precompile_stages(jl_module_t* evaluated_module, JuliaObject* julia_object);

        bool precompile_constructor(jl_module_t* evaluated_module, jl_value_t** ugen_object, jl_value_t** ins, float*** ins_float, JuliaObject* julia_object);

        bool precompile_perform(jl_module_t* evaluated_module, jl_value_t** ugen_object, jl_value_t** ins, float*** ins_float, jl_value_t** outs, JuliaObject* julia_object);

        bool precompile_destructor(jl_module_t* evaluated_module, jl_value_t** ugen_object, jl_value_t** destructor_fun, jl_method_instance_t** destructor_instance, JuliaObject* julia_object);

        bool precompile_ugen_ref(jl_module_t* evaluated_module, jl_value_t** ugen_object, jl_value_t** ins, jl_value_t** outs, jl_value_t** destructor_fun, jl_method_instance_t** destructor_instance, jl_value_t** ugen_ref_object, JuliaObject* julia_object);

        bool precompile_set_index_delete_index_julia_def(jl_module_t* evaluated_module, jl_value_t** ugen_ref_object, JuliaObject* julia_object);

        bool create_julia_def(jl_module_t* evaluated_module, JuliaObject* julia_object);

        bool delete_methods_from_table(JuliaObject* julia_object);
};