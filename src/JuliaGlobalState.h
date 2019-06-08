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
#include "JuliaGlobalIdDict.h"
#include "SC_AllocPoolSafe.h"
#include <string>

#define JC_VER_MAJ 0
#define JC_VER_MID 1
#define JC_VER_MIN 0

/* Series of global variables and initialization/quit routines for Julia */

/************************/
/* JuliaGlobalUtilities */
/************************/

class JuliaGlobalUtilities
{
    public:
        JuliaGlobalUtilities(){}

        ~JuliaGlobalUtilities(){}

        //Actual constructor, called from child class after Julia initialization
        bool initialize_global_utilities(World* in_world);

        //This is perhaps useless. It's executed when Julia is booting off anyway.
        void unload_global_utilities();

        //Requires "using JuliaCollider" to be ran already
        bool create_scsynth(World* in_world);

        bool create_utils_functions();

        bool create_julia_def_module();

        bool create_ugen_object_macro_module();
        
        bool create_datatypes();

        jl_value_t* get_scsynth();

        jl_function_t* get_set_index_audio_vector_fun();

        jl_module_t* get_scsynth_module();

        jl_function_t* get_sprint_fun();

        jl_function_t* get_showerror_fun();

        jl_function_t* get_set_index_fun();

        jl_function_t* get_delete_index_fun();

        jl_function_t* get_julia_def_fun();
        
        jl_value_t* get_vector_float32();

        jl_value_t* get_vector_of_vectors_float32();
    
    private:
        /* JuliaCollider modules */
        jl_module_t* julia_collider_module;
        jl_module_t* scsynth_module;
        jl_module_t* julia_def_module;
        jl_module_t* ugen_object_macro_module;

        /* Global objects */
        jl_value_t* scsynth;
        
        /* Utilities functions */
        jl_function_t* set_index_audio_vector_fun;
        jl_function_t* sprint_fun;
        jl_function_t* showerror_fun;
        jl_function_t* set_index_fun;
        jl_function_t* delete_index_fun;
        jl_function_t* julia_def_fun;
        
        /* Datatypes */
        jl_value_t* vector_float32;
        jl_value_t* vector_of_vectors_float32;
};

/*************/
/* JuliaPath */
/*************/

class JuliaPath
{
    public:
        JuliaPath();

        ~JuliaPath(){}

        void retrieve_julia_dir();

        const char* get_julia_folder_path();

        const char* get_julia_sysimg_path();

        const char* get_julia_stdlib_path();

        const char* get_julia_startup_path();

    private:
        std::string julia_folder_path;
        std::string julia_sysimg_path;
        std::string julia_stdlib_path;
        std::string julia_startup_path;

        std::string julia_path_to_sysimg;
        std::string julia_path_to_stdlib;
        std::string julia_path_to_startup;

        std::string julia_version_string;
        std::string julia_version_maj_min;

        #ifdef __APPLE__
            const char* find_julia_diretory_cmd = "i=10; complete_string=$(vmmap -w $scsynthPID | grep -m 1 'Julia.scx'); file_string=$(awk -v var=\"$i\" '{print $var}' <<< \"$complete_string\"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#\"$extra_string\"}; printf \"%s\" \"${final_string//\"Julia.scx\"/}\"";
        #elif __linux__
            const char* find_julia_diretory_cmd = "i=4; complete_string=$(pmap -p $scsynthPID | grep -m 1 'Julia.so'); file_string=$(awk -v var=\"$i\" '{print $var}' <<< \"$complete_string\"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#\"$extra_string\"}; printf \"%s\" \"${final_string//\"Julia.so\"/}\"";
        #endif
};

/********************/
/* JuliaGlobalState */
/********************/

class JuliaGlobalState : public JuliaPath, public JuliaGlobalUtilities
{
    public:
        //Ignoring constructor, as initialization will happen AFTER object creation. It will happen when an 
        //async command is triggered, which would call into boot_julia.
        JuliaGlobalState(World* SCWorld_, int julia_pool_alloc_mem_size);
        
        ~JuliaGlobalState();

        //Called with async command.
        bool boot_julia();

        bool run_startup_file();

        bool initialize_julia_collider_module();

        //Called when unloading the .so from the server
        void quit_julia();

        bool is_initialized();

        JuliaGlobalIdDict& get_global_def_id_dict();

        JuliaGlobalIdDict& get_global_object_id_dict();

        JuliaGlobalIdDict& get_global_gc_id_dict();

        jl_module_t* get_julia_collider_module();

        JuliaAllocPool* get_julia_alloc_pool();

        JuliaAllocFuncs* get_julia_alloc_funcs();

        //In julia.h, #define JL_RTLD_DEFAULT (JL_RTLD_LAZY | JL_RTLD_DEEPBIND) is defined. Could I just redefine the flags there?
        #ifdef __linux__
            bool load_julia_shared_library();

            void close_julia_shared_library();
        #endif

    private:
        World* SCWorld;

        AllocPoolSafe* alloc_pool;
        JuliaAllocPool* julia_alloc_pool;
        JuliaAllocFuncs* julia_alloc_funcs;

        //Extra variable to check if also the JuliaGlobalUtilities thing went through, not just Julia initialization.       
        bool initialized = false;

        JuliaGlobalIdDict global_def_id_dict;
        JuliaGlobalIdDict global_object_id_dict;
        JuliaGlobalIdDict global_gc_id_dict;

        jl_module_t* julia_collider_module;
        
        #ifdef __linux__
            void* dl_handle;
        #endif
};
