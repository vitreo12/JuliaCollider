#include "julia.h"

jl_value_t* create_global_id_dict()
{
    jl_function_t* id_dict_function = jl_get_function(jl_main_module, "IdDict");
    jl_value_t* global_id_dict;
    //JL_GC_PUSH2(&id_dict_function, &global_id_dict);
    global_id_dict = jl_call0(id_dict_function);
    jl_set_global(jl_main_module, jl_symbol("GlobalIdDict"), global_id_dict);
    //JL_GC_POP();

    return global_id_dict;
}

void delete_global_id_dict()
{
    jl_set_global(jl_main_module, jl_symbol("GlobalIdDict"), jl_nothing);
}

jl_value_t* create_object_id_dict(jl_value_t* global_id_dict, jl_value_t* id_dict_function, jl_value_t* set_index)
{
    jl_value_t* object_id_dict;
    object_id_dict = jl_call0(id_dict_function);
    jl_call3(set_index, global_id_dict, object_id_dict, object_id_dict);
    
    return object_id_dict;
}

void delete_object_id_dict(jl_value_t* global_id_dict, jl_value_t* object_id_dict, jl_function_t* delete_function)
{
    jl_call2(delete_function, global_id_dict, object_id_dict);
}