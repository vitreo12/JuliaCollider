#include "julia.h"
#include "SC_PlugIn.hpp"

//JULIA_DEFINE_FAST_TLS()

static InterfaceTable *ft;

jl_function_t* phasor_fun = nullptr;
bool julia_initialized = false; 

inline void test_include()
{
    if(jl_is_initialized())
    {
        jl_function_t* include_function = jl_get_function(jl_base_module, "include");
        JL_GC_PUSH1(&include_function);
        jl_call2(include_function, (jl_value_t*)jl_main_module, jl_cstr_to_string("/Users/francescocameli/Desktop/Embed_Julia_in_C/Julia_0_6_2/Source/Sine.jl"));
        jl_call2(jl_get_function(jl_main_module, "precompile"), jl_get_function((jl_module_t*)jl_get_global(jl_main_module, jl_symbol("Sine_DSP")), "Phasor"), jl_eval_string("Tuple([])"));
        JL_GC_POP();
    }
}

inline void boot_julia()
{
    if(!jl_is_initialized())
    {
        jl_init();
        
        if(jl_is_initialized())
        {
            printf("**************************\n");
            printf("**************************\n");
            printf("*** Julia %s booted ***\n", jl_ver_string());
            printf("**************************\n");
            printf("**************************\n");
            julia_initialized = true;
        }
    }
    else if(jl_is_initialized())
        printf("*** Julia %s already booted ***\n", jl_ver_string());
    else
        printf("WARNING: Couldn't boot Julia \n");
}

inline void quit_julia()
{
    if(jl_is_initialized())
    {
        printf("-> Quitting Julia..\n");
        jl_atexit_hook(0);
    }
}

/*
bool boot2(World* world, void* cmd)
{
    return true;
}

//rt thread (audio blocks if too heavy of a call!!!)
bool boot3(World* world, void* cmd)
{
    boot_julia(); //needs to be in audio thread, apparently. doesn't work with PluginLoad  
    return true;
}

//nrt thread
bool boot4(World* world, void* cmd)
{
    return true;
}

void bootCleanup(World* world, void* cmd){}

void JuliaBoot(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, nullptr, "", (void*)nullptr, (AsyncStageFn)boot2, (AsyncStageFn)boot3, (AsyncStageFn)boot4, bootCleanup, 0, nullptr);
}
*/

//nrt thread. 
bool quit2(World* world, void* cmd)
{
    quit_julia(); 
    return true;
}

//rt thread (audio blocks if too heavy of a call!!!)
bool quit3(World* world, void* cmd)
{ 
    return true;
}

//nrt thread
bool quit4(World* world, void* cmd)
{
    return true;
}

void quitCleanup(World* world, void* cmd){}

void JuliaQuit(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, nullptr, "", (void*)nullptr, (AsyncStageFn)quit2, (AsyncStageFn)quit3, (AsyncStageFn)quit4, quitCleanup, 0, nullptr);
}

//nrt thread. DO THE INCLUDES HERE!!!!!!!!!!!!!
bool include2(World* world, void* cmd)
{
    test_include();
    phasor_fun = jl_get_function((jl_module_t*)jl_get_global(jl_main_module, jl_symbol("Sine_DSP")), "Phasor");
    return true;
}

//rt thread (audio blocks if too heavy of a call!!!). need to have a second thread with partr here
bool include3(World* world, void* cmd)
{ 
    return true;
}

//nrt thread
bool include4(World* world, void* cmd)
{
    for(int i = 0; i < 1000; i++)
        printf("%s\n", jl_string_ptr(jl_cstr_to_string("from Julia: hi there")));
    
    printf("-> Include completed\n");
    return true;
}

void includeCleanup(World* world, void* cmd){}

void JuliaInclude(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, nullptr, "", (void*)nullptr, (AsyncStageFn)include2, (AsyncStageFn)include3, (AsyncStageFn)include4, includeCleanup, 0, nullptr);
}

inline void DefineJuliaCmds()
{
    //DefinePlugInCmd("julia_boot", (PlugInCmdFunc)JuliaBoot, nullptr);
    DefinePlugInCmd("julia_quit", (PlugInCmdFunc)JuliaQuit, nullptr);
    DefinePlugInCmd("julia_include", (PlugInCmdFunc)JuliaInclude, nullptr);
}