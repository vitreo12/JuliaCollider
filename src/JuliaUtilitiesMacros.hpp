#define jl_get_module_in_main(name) (jl_module_t*)jl_get_global(jl_main_module, jl_symbol(name))

#define jl_get_module(module, name) (jl_module_t*)jl_get_global(module, jl_symbol(name))

#define jl_get_global_SC(module, name) jl_get_global(module, jl_symbol(name))