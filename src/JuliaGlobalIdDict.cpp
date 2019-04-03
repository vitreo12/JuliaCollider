#include <cstdio>
#include "JuliaGlobalIdDict.h"

bool JuliaGlobalIdDict::initialize_id_dict(const char* global_var_name_)
{
    global_var_name = global_var_name_;

    set_index_fun = jl_get_function(jl_base_module, "setindex!");
    if(!set_index_fun)
        return false;

    delete_index_fun = jl_get_function(jl_base_module, "delete!");
    if(!delete_index_fun)
        return false;

    /* empty_fun = jl_get_function(jl_base_module, "empty!");
    if(!empty_fun)
        return false; */

    jl_function_t* id_dict_function = jl_get_function(jl_base_module, "IdDict");
    if(!id_dict_function)
        return false;

    id_dict = jl_call0(id_dict_function);
    if(!id_dict)
        return false;

    /* size_t nargs = 2;
    jl_value_t* args[nargs];
    args[0] = empty_fun;
    args[1] = id_dict;

    empty_instance = jl_lookup_generic_and_compile_SC(args, nargs);
    if(!empty_instance)
        return false; */

    //Set it to global in main
    jl_set_global(jl_main_module, jl_symbol(global_var_name), id_dict);

    return true;
}

//This is perhaps useless. It's executed when Julia is booting off anyway.
void JuliaGlobalIdDict::unload_id_dict()
{
    jl_set_global(jl_main_module, jl_symbol(global_var_name), jl_nothing);
}

/* Will throw exception if things go wrong */
void JuliaGlobalIdDict::add_to_id_dict(jl_value_t* var)
{
    size_t nargs = 4;
    jl_value_t* args[nargs];
    
    args[0] = set_index_fun;
    args[1] = id_dict;
    args[2] = var;
    args[3] = var;

    jl_value_t* result = jl_lookup_generic_and_compile_return_value_SC(args, nargs);
    
    if(!result)
        printf("ERROR: Could not add element to %s\n", global_var_name);
}

/* Will throw exception if things go wrong */
void JuliaGlobalIdDict::remove_from_id_dict(jl_value_t* var)
{
    size_t nargs = 3;
    jl_value_t* args[nargs];
    
    args[0] = delete_index_fun;
    args[1] = id_dict;
    args[2] = var;

    jl_value_t* result = jl_lookup_generic_and_compile_return_value_SC(args, nargs);

    if(!result)
        printf("ERROR: Could not remove element from %s\n", global_var_name);
}

jl_value_t* JuliaGlobalIdDict::get_id_dict()
{
    return id_dict;
}

