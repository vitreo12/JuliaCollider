#include "JuliaHash.hpp"
#include "JuliaUtilities.hpp"
#include "SC_PlugIn.hpp"
#include <string>

#pragma once

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
    #define JULIA_DIRECTORY_PATH "i=4; ID=$(pgrep scsynth); complete_string=$(pmap -p $ID | grep -m 1 'Julia.so'); file_string=$(awk -v var=\"$i\" '{print $var}' <<< \"$complete_string\"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#\"$extra_string\"}; printf \"%s\" \"${final_string//\"Julia.so\"/}\""
#endif

std::string get_julia_dir() 
{
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

    return result;
}

static InterfaceTable *ft;
World* global_world;

jl_mutex_t* gc_mutex;

jl_function_t* sine_fun = nullptr;
jl_function_t* perform_fun = nullptr;
jl_function_t* id_dict_function = nullptr;
jl_value_t* global_id_dict = nullptr;
jl_function_t* set_index = nullptr;
jl_function_t* delete_index = nullptr;

bool julia_initialized = false; 
std::string julia_dir;
std::string julia_folder_structure = "julia/lib/julia";
std::string JuliaDSP_folder = "julia/JuliaDSP/";

inline void test_include()
{
    if(julia_initialized)
    {
        std::string sine_jl_path = julia_dir;
        sine_jl_path.append(JuliaDSP_folder);
        sine_jl_path.append("Sine_DSP.jl");

        jl_function_t* include_function = jl_get_function(jl_base_module, "include");
        //JL_GC_PUSH1(&include_function);
        jl_call2(include_function, (jl_value_t*)jl_main_module, jl_cstr_to_string(sine_jl_path.c_str()));
        //JL_GC_POP();
    }
}

float* dummy_sc_alloc(int size_alloc)
{
    float* buffer = (float*)RTAlloc(global_world, size_alloc * sizeof(float));

    //Add an extra check here to see if buffer has been allocated correctly. If not, return something
    //that will still be read by Julia meaningfully

    memset(buffer, 0, size_alloc * sizeof(float));
    return buffer;
}

inline void boot_julia()
{
    if(!jl_is_initialized())
    {
        //get path to the Julia.scx and julia lib and includes.
        //doing "const char* julia_dir = get_julia_dir().c_str()"" is buggy for I don't know what reason. Sometimes it 
        //just gives out a blank string, while the std::string actually stores the path.
        //Calling c_str() everytime is ugly but it works better here.
        julia_dir = get_julia_dir();
        
        std::string julia_image_folder = julia_dir;
        julia_image_folder.append(julia_folder_structure);

        printf("Path to Julia object and lib: \n%s\n", julia_image_folder.c_str());
        
        if(julia_image_folder.c_str())
        {
#ifdef __APPLE__
            jl_init_with_image(julia_image_folder.c_str(), "sys.dylib");
#elif __linux__
            jl_init_with_image(julia_image_folder.c_str(), "sys.so");
#endif
        }

        if(jl_is_initialized())
        {
            jl_gc_enable(0);
            printf("GC enabled: %i\n", jl_gc_is_enabled());

            global_id_dict = create_global_id_dict();

            id_dict_function = jl_get_function(jl_main_module, "IdDict");
            set_index = jl_get_function(jl_main_module, "setindex!");
			delete_index = jl_get_function(jl_main_module, "delete!");
            
            //maybe i can get rid of all the gc_pushes since gc is disabled here..
            //JL_GC_PUSH3(&id_dict_function, &set_index, &delete_index);
            jl_call3(set_index, global_id_dict, (jl_value_t*)id_dict_function, (jl_value_t*)id_dict_function);
            jl_call3(set_index, global_id_dict, (jl_value_t*)set_index, (jl_value_t*)set_index);
            jl_call3(set_index, global_id_dict, (jl_value_t*)delete_index, (jl_value_t*)delete_index);
            //JL_GC_POP();

            /* PASSING A POINTER TO A C FUNCTION TO BE CALLED FROM CCALL IN JULIA: PRECEDING TO WORK WITH SC BUFFER */
            //retrieving a void pointer to the C function
            void* c_void_pointer = (void*)dummy_sc_alloc;
            //converting the void* to a Julia Ptr{Nothing} (void pointers in julia)
            jl_value_t* julia_ptr_nothing = jl_box_voidpointer(c_void_pointer);
            //setting the Ptr{Nothing} address under the symbol :CFunctionPointer, in the global id dict
            jl_call3(set_index, global_id_dict, julia_ptr_nothing, (jl_value_t*)jl_symbol("CFunctionPointer"));
            //print the function pointer address
            jl_call1(jl_get_function(jl_main_module, "println"), julia_ptr_nothing);

            //precompile println fun for id dicts.
            //i should precompile all the prints i need.
            jl_call1(jl_get_function(jl_main_module, "println"), global_id_dict);

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
        delete_global_id_dict();
        perform_gc(1);
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

void precompile(World* world, jl_value_t* object_id_dict, jl_value_t** args)
{
    args[0] = perform_fun; //already in id dict
                
    jl_value_t* sine = jl_call0(sine_fun); //precompile sine object
    args[1] = sine;
    jl_call3(set_index, object_id_dict, sine, sine);
    
    args[2] = jl_box_float64(world->mSampleRate);
    jl_call3(set_index, object_id_dict, args[2], args[2]);

    //size 0 for the loop
    args[3] = jl_box_int32((int)0);
    jl_call3(set_index, object_id_dict, args[3], args[3]);

    //size 0 array
    args[4] = (jl_value_t*)jl_alloc_array_1d(jl_apply_array_type((jl_value_t*)jl_float32_type, 1), (size_t)0);
    jl_call3(set_index, object_id_dict, args[4], args[4]);

    args[5] = jl_box_float64((double)440.0);
    jl_call3(set_index, object_id_dict, args[5], args[5]);

    //precompile perform function
    jl_call_no_gc(args, 6);
}

void precompile_object(World* world)
{
    printf("Module precompilation...\n");

    jl_value_t* object_id_dict = create_object_id_dict(global_id_dict, id_dict_function, set_index);

    jl_value_t** args = (jl_value_t**)RTAlloc(world, sizeof(jl_value_t*) * 6);

    //precompile constructor and perform functions for this module
    precompile(world, object_id_dict, args);

    delete_object_id_dict(global_id_dict, object_id_dict, delete_index);

    RTFree(world, args);
}

//nrt thread. DO THE INCLUDES HERE!!!!!!!!!!!!!
bool include2(World* world, void* cmd)
{
    //preallocates memory for objects creation.. Use Instruments -> Allocations to check on scsynth
    //jl_gc_allocobj(100000000);

    test_include();

    sine_fun = jl_get_function(jl_get_module("Sine_DSP"), "Sine");
    perform_fun = jl_get_function(jl_get_module("Sine_DSP"), "perform");

    //JL_GC_PUSH2(sine_fun, perform_fun)
    jl_call3(set_index, global_id_dict, (jl_value_t*)sine_fun, (jl_value_t*)sine_fun);
    jl_call3(set_index, global_id_dict, (jl_value_t*)perform_fun, (jl_value_t*)perform_fun);
    //JL_GC_POP();  

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
    precompile_object(world);

    jl_call1(jl_get_function(jl_main_module, "println"), global_id_dict);
    //call on the GC after each include to cleanup stuff
    perform_gc(1);

    printf("-> Include completed\n");
    
    return true;
}

void includeCleanup(World* world, void* cmd){}

void JuliaInclude(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, nullptr, "", (void*)nullptr, (AsyncStageFn)include2, (AsyncStageFn)include3, (AsyncStageFn)include4, includeCleanup, 0, nullptr);
}

bool alloc2(World* world, void* cmd)
{
    return true;
}

bool alloc3(World* world, void* cmd)
{
    global_world = world;

    //TEST OF BUFFER ALLOCATION WITH SC ALLOCATOR FROM JULIA
    //ccall the function pointer stored in the GlobalIdDict under the sumbol CFunctionPointer. It returns a Ptr{Cfloat} (float* in C), which is
    //the location of the first allocated element, and it takes an argument for how many elements to alloc in SC RT allocator.
    //Then, wrap that pointer around a Vector{Float32} to be used in Julia directly. It just uses the jl_array_ptr_1d function that I also use to wrap
    //the audio buffer data in the audio processing loop.
    jl_call1(jl_get_function(jl_main_module, "println"), jl_eval_string("unsafe_wrap(Vector{Float32}, ccall(GlobalIdDict[:CFunctionPointer], Ptr{Cfloat}, (Cint,), 10), 10)"));
    
    //execute this 2 times to see that actually it's correctly allocating the memory in SC's pool!!!!!
    //jl_eval_string("unsafe_wrap(Vector{Float32}, ccall(GlobalIdDict[:CFunctionPointer], Ptr{Cfloat}, (Cint,), 1152000), 1152000)");

    return true;
}

bool alloc4(World* world, void* cmd)
{
    return true;
}

void allocCleanup(World* world, void* cmd){}

void JuliaAlloc(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, nullptr, "", (void*)nullptr, (AsyncStageFn)alloc2, (AsyncStageFn)alloc3, (AsyncStageFn)alloc4, allocCleanup, 0, nullptr);
}

bool gc2(World* world, void* cmd)
{
    perform_gc(1);
    return true;
}

bool gc3(World* world, void* cmd)
{
    return true;
}

bool gc4(World* world, void* cmd)
{
    return true;
}

void gcCleanup(World* world, void* cmd){}

void JuliaGC(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, nullptr, "", (void*)nullptr, (AsyncStageFn)gc2, (AsyncStageFn)gc3, (AsyncStageFn)gc4, gcCleanup, 0, nullptr);
}

inline void DefineJuliaCmds()
{
    DefinePlugInCmd("julia_quit", (PlugInCmdFunc)JuliaQuit, nullptr);
    DefinePlugInCmd("julia_include", (PlugInCmdFunc)JuliaInclude, nullptr);
    DefinePlugInCmd("julia_alloc", (PlugInCmdFunc)JuliaAlloc, nullptr);
}