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

/* IdDict() Julia wrapper. It is used to make global
all the __JuliaDef__ (for a JuliaObject) and the __UGenRef__ and __IORef__ (for single UGens)
in order to keep them referenced for the GC not to pick on them until they are released */

class JuliaGlobalIdDict
{
    public:
        JuliaGlobalIdDict(){}
        ~JuliaGlobalIdDict(){}

        bool initialize_id_dict(const char* global_var_name_);

        //This is perhaps useless. It's executed when Julia is booting off anyway.
        void unload_id_dict();
        
        /* Will throw exception if things go wrong */
        void add_to_id_dict(jl_value_t* var);

        /* Will throw exception if things go wrong */
        void remove_from_id_dict(jl_value_t* var);

        /* void empty_id_dict()
        {
            size_t nargs = 2;
            jl_value_t* args[nargs];

            args[0] = empty_fun;
            args[1] = id_dict;

            jl_value_t* result = jl_invoke_already_compiled_SC(empty_instance, args, nargs);
            if(!result)
                printf("ERROR: Could not empty %s\n", global_var_name);
        } */

        jl_value_t* get_id_dict();

    private:
        jl_value_t* id_dict;
        const char* global_var_name;
        
        //method instances for __UGenRef__ already live in JuliaObject. Those are the ones called in RT calls.
        jl_function_t* set_index_fun;
        jl_function_t* delete_index_fun;
        
        //jl_function_t* empty_fun;
        //jl_method_instance_t* empty_instance;
};
