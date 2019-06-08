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

#include "julia.h"
#include <string>

typedef struct JuliaObject
{
    /* STATE */
    bool compiled;
    //bool RT_busy;
    bool being_replaced;

    /* PATH */
    std::string path;
    
    /* JULIADEF */
    jl_value_t* julia_def; 

    /* MODULE */
    jl_module_t* evaluated_module;
    const char* name;
    int num_inputs;
    int num_outputs;
    
    /* FUNCTIONS */
    jl_function_t* ugen_ref_fun;
    jl_function_t* constructor_fun;
    jl_function_t* perform_fun;
    jl_function_t* destructor_fun;
    jl_function_t* set_index_ugen_ref_fun; 
    jl_function_t* delete_index_ugen_ref_fun; 
    
    /* METHOD INSTANCES */
    /* Mirror to __JuliaDef__ in Julia */
    jl_method_instance_t* ugen_ref_instance;
    jl_method_instance_t* constructor_instance;
    jl_method_instance_t* perform_instance;
    jl_method_instance_t* destructor_instance;
    jl_method_instance_t* set_index_ugen_ref_instance;
    jl_method_instance_t* delete_index_ugen_ref_instance; 
    jl_method_instance_t* set_index_audio_vector_instance; //Used in ins/outs
} JuliaObject;
