#include "julia.h"
#include "SC_PlugIn.hpp"
#include <string>

//use vvmap to get a map of the active processes under scsynth. then grep Julia.scx to get the path where it's running from.
//-m1, first occurence
//i=10; final_string=""; complete_string=$(vmmap -w scsynth | grep -m 1 'Julia.scx'); while true; do test_string=$(awk -v var="$i" '{print $var}' <<< "$complete_string" | grep -o 'Julia.scx'); final_string+=$(awk -v var="$i" '{print $var}' <<< "$complete_string")" "; if [ "$test_string" == "Julia.scx" ]; then break; fi; let "i+=1"; done; printf '%s' "${final_string//"Julia.scx"/}"
/* 
BREAKDOWN:
- i = 10; variable for looping. 10 is the position in the awk output where the string of the directory should be
- complete_string=$(vmmap -w scsynth | grep -m 1 'Julia.scx'); -w = wide output. full path. string that contains the first line where the occurrence (grep -m 1 'Julia.scx') of 'Julia.scx' appears when running the map of active processes under scsynth.
- file_string=$(awk -v var="$i" '{print $var}' <<< \"$complete_string\"); extract the string that tells where the directory is. This is needed because if there are spaces in the path, it will be split. This way, I am just finding the first path before splitting (e.g. it finds "/Users/francescocameli/Library/Application "), splitting Application Support
- extra_string=${complete_string%$file_string*}; extract the string before the string that tells where the file is. it contains all the extra stuff that comes out of vmmap that I don't care about.
- final_string=${complete_string#"$extra_string"}; remove the extra string from the complete one. This will give out the path that I need, including spaces in the name of folders.
- printf "%s" "${final_string//"Julia.scx"/}" return the path without the "Julia.scx". It is the path to the Julia folder.
*/
#ifdef __APPLE__
    #define JULIA_DIRECTORY_PATH "i=10; complete_string=$(vmmap -w scsynth | grep -m 1 'Julia.scx'); file_string=$(awk -v var=\"$i\" '{print $var}' <<< \"$complete_string\"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#\"$extra_string\"}; printf \"%s\" \"${final_string//\"Julia.scx\"/}\""
#elif __linux__
    //in linux there is no vvmap.
    //pgrep gives me the PID, pmap shows me the map as vmmap. -p shows the full path to files.
    //the variable in pmap is 4.
    //I need to run: i=4; ID=$(pgrep scsynth); complete_string=$(pmap -p $ID | grep -m 1 'Julia.so'); file_string=$(awk -v var="$i" '{print $var}' <<< "$complete_string"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#"$extra_string"}; printf "%s" "${final_string//"Julia.so"/}"
#endif

const char* get_julia_dir() 
{
#ifdef __APPLE__
    //run script and get a FILE pointer back to the result of the script (which is what's returned by printf in bash script)
    FILE* pipe = popen(JULIA_DIRECTORY_PATH, "r");
    
    if (!pipe) 
        return "ERROR: couldn't run command.";
    
    char buffer[128];
    std::string result = "";
    while(!feof(pipe)) 
    {
        //get the text out
        if(fgets(buffer, 128, pipe) != NULL)
            result += buffer;
    }
    pclose(pipe);

    result += "julia/lib/julia";
    return result.c_str();
#elif __linux__
#endif 
}

static InterfaceTable *ft;

jl_function_t* phasor_fun = nullptr;
bool julia_initialized = false; 

inline void test_include()
{
    if(julia_initialized)
    {
        jl_function_t* include_function = jl_get_function(jl_base_module, "include");
        JL_GC_PUSH1(&include_function);
        jl_call2(include_function, (jl_value_t*)jl_main_module, jl_cstr_to_string("/Users/francescocameli/Desktop/Embed_Julia_in_C/Julia_0_6_2/Source/Sine.jl"));
        JL_GC_POP();
    }
}

inline void boot_julia()
{
    if(!jl_is_initialized())
    {
        //get path to the Julia.scx and julia lib and includes.
        const char* dir_path = get_julia_dir();
        printf("Path to Julia object and lib: \n%s\n", dir_path);
        
        if(dir_path)
        {
#ifdef __APPLE__
            jl_init_with_image(dir_path, "sys.dylib");
#elif __linux__
            jl_init_with_image(dir_path, "sys.so");
#endif
        }

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
    jl_call2(jl_get_function(jl_main_module, "precompile"), jl_get_function((jl_module_t*)jl_get_global(jl_main_module, jl_symbol("Sine_DSP")), "Phasor"), jl_eval_string("Tuple([])"));
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
    DefinePlugInCmd("julia_quit", (PlugInCmdFunc)JuliaQuit, nullptr);
    DefinePlugInCmd("julia_include", (PlugInCmdFunc)JuliaInclude, nullptr);
}