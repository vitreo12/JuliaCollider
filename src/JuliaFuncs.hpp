#include <string>
#include <atomic>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <thread>
#include <chrono>

#include "julia.h"
#include "JuliaUtilities.hpp"

#include "SC_PlugIn.hpp"

#include "JuliaAtomicBarrier.h"

//Modified SC_AllocPool.h header to retrieve private members of AllocPool class
#include "SC_AllocPoolSafe.h"

//MAC: ./build_install_native.sh ~/Desktop/IP/JuliaCollider/vitreo12-julia/julia-native/ ~/SuperCollider ~/Library/Application\ Support/SuperCollider/Extensions
//LINUX: ./build_install_native.sh ~/Sources/JuliaCollider/vitreo12-julia/julia-native ~/Sources/SuperCollider-3.10.0 ~/.local/share/SuperCollider/Extensions

//for dlopen
#ifdef __linux__
#include <dlfcn.h>
#endif

#pragma once

/***************************************************************************/
                            /* INTERFACE TABLE */
/***************************************************************************/
static InterfaceTable* ft;

/***************************************************************************/
                        /* JULIA VERSION std::string */
/***************************************************************************/
std::string julia_version_string = std::string(jl_ver_string());
const std::string julia_version_maj_min = julia_version_string.substr(0, julia_version_string.size()-2); //Remove last two characters. Result is "1.1"

/***************************************************************************/
                                /* STRUCTS */
/***************************************************************************/
typedef struct JuliaObject
{
    /* STATE */
    bool compiled;
    //bool RT_busy;
    bool being_replaced;
    
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

/***************************************************************************/
                                /* CLASSES */
/***************************************************************************/
/* IdDict() wrapper.
Dict() is faster than IdDict(), but it allocates more memory. 
Also, I must use IdDict{Any, Any} because every __UGenRef__ will be different, as it's defined in each module differently.
Anyway, it looks like IdDict() is fast enough, at least for now it's fine. */
class JuliaGlobalIdDict
{
    public:
        JuliaGlobalIdDict(){}
        ~JuliaGlobalIdDict(){}

        inline bool initialize_id_dict(const char* global_var_name_)
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
        inline void unload_id_dict()
        {
            jl_set_global(jl_main_module, jl_symbol(global_var_name), jl_nothing);
        }
        
        /* Will throw exception if things go wrong */
        inline void add_to_id_dict(jl_value_t* var)
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
        inline void remove_from_id_dict(jl_value_t* var)
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

        /* inline void empty_id_dict()
        {
            size_t nargs = 2;
            jl_value_t* args[nargs];

            args[0] = empty_fun;
            args[1] = id_dict;

            jl_value_t* result = jl_invoke_already_compiled_SC(empty_instance, args, nargs);
            if(!result)
                printf("ERROR: Could not empty %s\n", global_var_name);
        } */

        inline jl_value_t* get_id_dict()
        {
            return id_dict;
        }

    private:
        jl_value_t* id_dict;
        const char* global_var_name;
        
        //method instances for __UGenRef__ already live in JuliaObject. Those are the ones called in RT calls.
        jl_function_t* set_index_fun;
        jl_function_t* delete_index_fun;
        
        //jl_function_t* empty_fun;
        //jl_method_instance_t* empty_instance;
};

class JuliaGlobalUtilities
{
    public:
        JuliaGlobalUtilities(){}

        ~JuliaGlobalUtilities(){}

        //Actual constructor, called from child class after Julia initialization
        inline bool initialize_global_utilities(World* in_world)
        {
            if(!create_scsynth(in_world) || !create_utils_functions() || !create_datatypes() || !create_julia_def_module() || !create_ugen_object_macro_module())
                return false;

            return true;
        }

        //This is perhaps useless. It's executed when Julia is booting off anyway.
        inline void unload_global_utilities()
        {
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderModule__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderSCSynthModule__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalSCSynth__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalVectorFloat32__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalVectorOfVectorsFloat32__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderJuliaDefModule__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderJuliaDefFun__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderUGenObjectMacroModule__"), jl_nothing);
        }

        //Requires "using JuliaCollider" to be ran already
        inline bool create_scsynth(World* in_world)
        {
            julia_collider_module = jl_get_module_in_main("JuliaCollider");
            if(!julia_collider_module)
                return false;

            scsynth_module = jl_get_module(julia_collider_module, "SCSynth");
            if(!scsynth_module)
                return false;

            jl_function_t* scsynth_constructor = jl_get_function(scsynth_module, "__SCSynth__");
            if(!scsynth_constructor)
                return false;
            
            jl_value_t* sample_rate = jl_box_float64(in_world->mSampleRate);
            jl_value_t* buf_size = jl_box_int32(in_world->mBufLength);
            if(!sample_rate || !buf_size)
                return false;

            scsynth = jl_call2(scsynth_constructor, sample_rate, buf_size);
            if(!scsynth)
                return false;

            set_index_audio_vector_fun = jl_get_function(scsynth_module, "set_index_audio_vector");
            if(!set_index_audio_vector_fun)
                return false;

            //UNNEEDED?
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderModule__"), (jl_value_t*)julia_collider_module);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderSCSynthModule__"), (jl_value_t*)scsynth_module);
            //Needed
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalSCSynth__"), (jl_value_t*)scsynth);

            return true;
        }

        inline bool create_utils_functions()
        {
            sprint_fun = jl_get_function(jl_base_module, "sprint");
            if(!sprint_fun)
                return false;

            showerror_fun = jl_get_function(jl_base_module, "showerror");
            if(!showerror_fun)
                return false;

            set_index_fun = jl_get_function(jl_base_module, "setindex!");
            if(!set_index_fun)
                return false;

            delete_index_fun = jl_get_function(jl_base_module, "delete!");
            if(!delete_index_fun)
                return false;

            //FUNCTIONS ARE ALREADY GLOBAL (in fact, jl_get_function is just a wrapper of jl_get_global)

            return true;
        }

        inline bool create_julia_def_module()
        {
            julia_def_module = jl_get_module(julia_collider_module, "JuliaDef");
            if(!julia_def_module)
                return false;

            julia_def_fun = jl_get_function(julia_def_module, "__JuliaDef__");
            if(!julia_def_fun)
                return false;
            
            //Unneeded?
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderJuliaDefModule__"), (jl_value_t*)julia_def_module);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderJuliaDefFun__"), (jl_value_t*)julia_def_fun);

            return true;
        }

        inline bool create_ugen_object_macro_module()
        {
            ugen_object_macro_module = jl_get_module(julia_collider_module, "UGenObjectMacro");
            if(!ugen_object_macro_module)
                return false;

            //Unneeded?
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderUGenObjectMacroModule__"), (jl_value_t*)ugen_object_macro_module);

            return true;
        }
        
        inline bool create_datatypes()
        {
            //Vector{Float32}
            vector_float32 = jl_apply_array_type((jl_value_t*)jl_float32_type, 1);
            if(!vector_float32)
                return false;

            //Vector{Vector{Float32}}
            vector_of_vectors_float32 = jl_apply_array_type(vector_float32, 1);
            if(!vector_of_vectors_float32)
                return false;
            
            //set global
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalVectorFloat32__"), vector_float32);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalVectorOfVectorsFloat32__"), vector_of_vectors_float32);

            return true;
        }

        inline jl_value_t* get_scsynth()
        {
            return scsynth;
        }

        inline jl_function_t* get_set_index_audio_vector_fun()
        {
            return set_index_audio_vector_fun;
        }

        inline jl_module_t* get_scsynth_module()
        {
            return scsynth_module;
        }

        inline jl_function_t* get_sprint_fun()
        {
            return sprint_fun;
        }

        inline jl_function_t* get_showerror_fun()
        {
            return showerror_fun;
        }

        inline jl_function_t* get_set_index_fun()
        {
            return set_index_fun;
        }

        inline jl_function_t* get_delete_index_fun()
        {
            return delete_index_fun;
        }

        inline jl_function_t* get_julia_def_fun()
        {
            return julia_def_fun;
        }
        
        inline jl_value_t* get_vector_float32()
        {
            return vector_float32;
        }
        inline jl_value_t* get_vector_of_vectors_float32()
        {
            return vector_of_vectors_float32;
        }
    
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

class JuliaPath
{
    public:
        JuliaPath()
        {
            retrieve_julia_dir();
        }

        ~JuliaPath(){}

        inline void retrieve_julia_dir() 
        {
            //Get process id and convert it to string
            pid_t scsynth_pid = getpid();
            const char* scsynth_pid_string = (std::to_string(scsynth_pid)).c_str();

            printf("PID: %i\n", scsynth_pid);

            //Set the scsynthPID enviromental variable, used in the "find_julia_diretory_cmd" bash script
            setenv("scsynthPID", scsynth_pid_string, 1);

            //run script and get a FILE pointer back to the result of the script (which is what's returned by printf in bash script)
            FILE* pipe = popen(find_julia_diretory_cmd, "r");
            
            if (!pipe) 
            {
                printf("ERROR: Could not run bash script to find Julia \n");
                return;
            }
            
            char buffer[128];
            while(!feof(pipe)) 
            {
                //get the text out 128 characters at a time...
                while(fgets(buffer, 128, pipe) != NULL)
                    julia_folder_path += buffer;
            }

            pclose(pipe);

            julia_sysimg_path = julia_folder_path;
            julia_sysimg_path.append(julia_path_to_sysimg);


            julia_stdlib_path = julia_folder_path;
            julia_stdlib_path.append(julia_path_to_stdlib);

            julia_startup_path = julia_folder_path;
            julia_startup_path.append(julia_path_to_startup);

            printf("*** JULIA PATH: %s ***\n", julia_folder_path.c_str());
            printf("*** JULIA LIB PATH: %s ***\n", julia_sysimg_path.c_str());
            printf("*** JULIA STARTUP PATH : %s ***\n", julia_startup_path.c_str());
            printf("*** JULIA STDLIB PATH : %s ***\n",  julia_stdlib_path.c_str());
        }

        const char* get_julia_folder_path()
        {
            return julia_folder_path.c_str();
        }

        const char* get_julia_sysimg_path()
        {
            return julia_sysimg_path.c_str();
        }

        const char* get_julia_stdlib_path()
        {
            return julia_stdlib_path.c_str();
        }

        const char* get_julia_startup_path()
        {
            return julia_startup_path.c_str();
        }

    private:
        std::string julia_folder_path;
        std::string julia_sysimg_path;
        std::string julia_stdlib_path;
        std::string julia_startup_path;

        const std::string julia_path_to_sysimg  = "julia/lib/julia";
        const std::string julia_path_to_stdlib  = std::string("julia/stdlib/v").append(julia_version_maj_min);
        const std::string julia_path_to_startup = "julia/startup/startup.jl";

        #ifdef __APPLE__
            const char* find_julia_diretory_cmd = "i=10; complete_string=$(vmmap -w $scsynthPID | grep -m 1 'Julia.scx'); file_string=$(awk -v var=\"$i\" '{print $var}' <<< \"$complete_string\"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#\"$extra_string\"}; printf \"%s\" \"${final_string//\"Julia.scx\"/}\"";
        #elif __linux__
            const char* find_julia_diretory_cmd = "i=4; complete_string=$(pmap -p $scsynthPID | grep -m 1 'Julia.so'); file_string=$(awk -v var=\"$i\" '{print $var}' <<< \"$complete_string\"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#\"$extra_string\"}; printf \"%s\" \"${final_string//\"Julia.so\"/}\"";
        #endif
};

class JuliaGlobalState : public JuliaPath, public JuliaGlobalUtilities
{
    public:
        //Ignoring constructor, as initialization will happen AFTER object creation. It will happen when an 
        //async command is triggered, which would call into boot_julia.
        JuliaGlobalState(World* SCWorld_, int julia_pool_alloc_mem_size)
        {
            SCWorld = SCWorld_;

            if(!SCWorld)
            {
                printf("ERROR: Invalid World*\n");
                return;
            }

            julia_alloc_pool = (JuliaAllocPool*)malloc(sizeof(JuliaAllocPool));
            julia_alloc_funcs = (JuliaAllocFuncs*)malloc(sizeof(JuliaAllocFuncs));
            
            /* Thread safe version of the standard SuperCollider's AllocPool class. Perhaps, supernova's simple_pool
            would be a better choice here.*/
            alloc_pool = new AllocPoolSafe(malloc, free, julia_pool_alloc_mem_size * 1024, 0);
            
            if(!julia_alloc_pool || !julia_alloc_funcs || !alloc_pool)
            {
                printf("ERROR: Could not allocate memory for Julia memory pool\n");
                return;
            }

            /* Assign pool */
            julia_alloc_pool->alloc_pool = alloc_pool;
            
            /* Set allocation functions for the pool to be used from julia */
            julia_alloc_funcs->fRTAlloc = &julia_pool_malloc;
            julia_alloc_funcs->fRTRealloc = &julia_pool_realloc;
            julia_alloc_funcs->fRTFree = &julia_pool_free;
            julia_alloc_funcs->fRTTotalFreeMemory = &julia_pool_total_free_memory;

            boot_julia();
        }
        
        ~JuliaGlobalState()
        {
            delete alloc_pool;
            free(julia_alloc_pool);
            free(julia_alloc_funcs);

            quit_julia();
        }

        //Called with async command.
        inline void boot_julia()
        {
            if(!jl_is_initialized() && !initialized)
            {
                printf("-> Booting Julia...\n");

                #ifdef __linux__
                    load_julia_shared_library();
                #endif

                const char* path_to_julia_sysimg = JuliaPath::get_julia_sysimg_path();
                const char* path_to_julia_stdlib  = JuliaPath::get_julia_stdlib_path();

                printf("***\nPath to Julia lib:\n %s\n***", path_to_julia_sysimg);

                //Set env variable for JULIA_LOAD_PATH to set correct path to stdlib. It would be
                //much better if I can find a way to set the DATAROOTDIR julia variable to do this.
                setenv("JULIA_LOAD_PATH", path_to_julia_stdlib, 1);

                void* pool_starting_position = julia_alloc_pool->alloc_pool->get_area_ptr()->get_unaligned_pointer();
                size_t pool_size = julia_alloc_pool->alloc_pool->get_area_ptr()->get_size();
                
                if(path_to_julia_sysimg)
                {
                    #ifdef __APPLE__
                        jl_init_with_image_SC(path_to_julia_sysimg, "sys.dylib", SCWorld, julia_alloc_pool, julia_alloc_funcs, pool_starting_position, pool_size);
                    #elif __linux__
                        jl_init_with_image_SC(path_to_julia_sysimg, "sys.so", SCWorld, julia_alloc_pool, julia_alloc_funcs, pool_starting_position, pool_size);
                    #endif
                }

                if(jl_is_initialized())
                {
                    //Disable GC.
                    jl_gc_enable(0);

                    /* Since my Sys.BINDIR is directly linked to sys.so, and not to a julia executable, 
                    both Base.SYSCONFDIR (for startup.jl) and Base.DATAROOTDIR (for /share folder) are wrong, 
                    as they are defaulted to "../etc" and "../share" respectively. 
                    I would need them to be "../../etc" and ../../share" */
                    
                    printf("BINDIR:\n");
                    jl_eval_string("println(Sys.BINDIR)");

                    //I would need it to be "../../etc"
                    printf("SYSCONFDIR: \n");
                    jl_eval_string("println(Base.SYSCONFDIR)");

                    printf("LOAD_PATH: \n");
                    jl_eval_string("println(Base.load_path_expand.(LOAD_PATH))");

                    //Manually load the startup file, since I can't set Base.SYSCONFDIR to "../../etc". Workaround:
                    bool initialized_startup_file = run_startup_file();
                    if(!initialized_startup_file)
                    {
                        printf("ERROR: Could not run startup.jl\n");
                        return;
                    }

                    bool initialized_julia_collider = initialize_julia_collider_module();
                    if(!initialized_julia_collider)
                    {
                        printf("ERROR: Could not intialize JuliaCollider module\n");
                        return;
                    }

                    bool initialized_global_utilities = JuliaGlobalUtilities::initialize_global_utilities(SCWorld);
                    if(!initialized_global_utilities)
                    {
                        printf("ERROR: Could not intialize JuliaGlobalUtilities\n");
                        return;
                    }

                    bool initialized_global_def_id_dict = global_def_id_dict.initialize_id_dict("__JuliaGlobalDefIdDict__");
                    if(!initialized_global_def_id_dict)
                    {
                        printf("ERROR: Could not intialize JuliaGlobalDefIdDict \n");
                        return;
                    }

                    bool initialized_global_object_id_dict = global_object_id_dict.initialize_id_dict("__JuliaGlobalObjectIdDict__");
                    if(!initialized_global_object_id_dict)
                    {
                        printf("ERROR: Could not intialize JuliaGlobalObjectIdDict \n");
                        return;
                    }

                    bool initialized_global_gc_id_dict = global_gc_id_dict.initialize_id_dict("__JuliaGlobalGCIdDict__");
                    if(!initialized_global_gc_id_dict)
                    {
                        printf("ERROR: Could not intialize JuliaGlobalGCIdDict \n");
                        return;
                    }

                    //Get world_counter right away, otherwise last_age, in include sections, would be
                    //still age 1. This update here allows me to only advance age on the NRT thread, while 
                    //on the RT thread I only invoke methods that are been already compiled and would work 
                    //between their world age minimum and maximum.
                    jl_get_ptls_states()->world_age = jl_get_world_counter();

                    printf("**************************\n");
                    printf("**************************\n");
                    printf("*** Julia %s booted ***\n", jl_ver_string());
                    printf("**************************\n");
                    printf("**************************\n");
                    
                    initialized = true;
                }
            }
            else if(initialized)
                printf("*** Julia %s already booted ***\n", jl_ver_string());
            else
                printf("ERROR: Could not boot Julia \n"); 
        }

        inline bool run_startup_file()
        {
            const char* path_to_julia_startup = JuliaPath::get_julia_startup_path();
            jl_value_t* load_startup = jl_load(jl_main_module, path_to_julia_startup);
            if(!load_startup)
                return false;
            return true;
        }

        inline bool initialize_julia_collider_module()
        {         
            jl_value_t* eval_using_julia_collider = jl_eval_string("using JuliaCollider");
                
            if(!eval_using_julia_collider)
            {
                printf("ERROR: Failed in \"using JuliaCollider\"\n");
                return false;
            }

            julia_collider_module = jl_get_module_in_main("JuliaCollider");

            if(!julia_collider_module)
            {
                printf("ERROR: Failed in retrieving JuliaCollider \n");
                return false;
            }

            jl_value_t* eval_using_julia_def = jl_eval_string("using JuliaCollider.JuliaDef");
            if(!eval_using_julia_def)
            {
                printf("ERROR: Failed in \"using JuliaCollider.JuliaDef\"\n");
                return false;
            }

            jl_value_t* eval_using_ugen_object_macro = jl_eval_string("using JuliaCollider.UGenObjectMacro");
            if(!eval_using_ugen_object_macro)
            {
                printf("ERROR: Failed in \"using JuliaCollider.UGenObjectMacro\"\n");
                return false;
            }

            return true;
        }

        //Called when unloading the .so from the server
        inline void quit_julia()
        {
            if(initialized)
            {
                printf("-> Quitting Julia...\n");

                JuliaGlobalUtilities::unload_global_utilities();
                global_def_id_dict.unload_id_dict();
                global_object_id_dict.unload_id_dict();
                global_gc_id_dict.unload_id_dict();

                jl_atexit_hook(0); 
                
                printf("-> Quitted Julia \n");

                #ifdef __linux__
                    close_julia_shared_library();
                #endif
            }
        }

        bool is_initialized()
        {
            return initialized;
        }

        JuliaGlobalIdDict &get_global_def_id_dict()
        {
            return global_def_id_dict;
        }

        JuliaGlobalIdDict &get_global_object_id_dict()
        {
            return global_object_id_dict;
        }

        JuliaGlobalIdDict &get_global_gc_id_dict()
        {
            return global_gc_id_dict;
        }

        jl_module_t* get_julia_collider_module()
        {
            return julia_collider_module;
        }

        JuliaAllocPool* get_julia_alloc_pool()
        {
            return julia_alloc_pool;
        }

        JuliaAllocFuncs* get_julia_alloc_funcs()
        {
            return julia_alloc_funcs;
        }

        //In julia.h, #define JL_RTLD_DEFAULT (JL_RTLD_LAZY | JL_RTLD_DEEPBIND) is defined. Could I just redefine the flags there?
        #ifdef __linux__
            inline void load_julia_shared_library()
            {
                dl_handle = dlopen("libjulia.so", RTLD_NOW | RTLD_GLOBAL);
                if (!dl_handle) {
                    fprintf (stderr, "%s\n", dlerror());
                    printf("ERROR: Could not find Julia. \n");
                }
            }

            inline void close_julia_shared_library()
            {
                if(dl_handle)
                    dlclose(dl_handle);
            }
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

//Overload new and delete operators with RTAlloc and RTFree calls
class RTClassAlloc
{
    public:
        void* operator new(size_t size, World* in_world)
        {
            return (void*)RTAlloc(in_world, size);
        }

        void operator delete(void* p, World* in_world) 
        {
            RTFree(in_world, p);
        }
    private:
};

#define JULIA_CHAR_BUFFER_SIZE 500

class JuliaReply : public RTClassAlloc
{
    public:
        JuliaReply(int OSC_unique_id_)
        {
            count_char = 0;
            OSC_unique_id = OSC_unique_id_;
        }

        ~JuliaReply(){}

        /* Using pointers to the buffer, shifted by count_char. */
        int append_string(char* buffer_, size_t size, const char* string)
        {
            return snprintf(buffer_, size, "%s\n", string);
        }
        
        //for id
        int append_string(char* buffer_, size_t size, int value)
        {
            return snprintf(buffer_, size, "%i\n", value);
        }

        /* //for jl_unbox_int64
        int append_string(char* buffer_, size_t size, long value)
        {
            return snprintf(buffer_, size, "%ld\n", value);
        }

        //for id
        int append_string(char* buffer_, size_t size, int value)
        {
            return snprintf(buffer_, size, "%lu\n", value);
        } */

        //Exit condition. No more VarArgs to consume
        void create_done_command() {return;}

        template<typename T, typename... VarArgs>
        void create_done_command(T&& arg, VarArgs&&... args)
        {    
            //Append string to the end of the previous one. Keep count of the position with "count_char"
            count_char += append_string(buffer + count_char, JULIA_CHAR_BUFFER_SIZE - count_char, arg); //std::forward<T>(arg...) ?

            //Call function recursively
            if(count_char && count_char < JULIA_CHAR_BUFFER_SIZE)
                create_done_command(args...); //std::forward<VarArgs>(args...) ?
        }

        int get_OSC_unique_id()
        {
            return OSC_unique_id;
        }

        char* get_buffer()
        {
            return buffer;
        }

    private:
        char buffer[JULIA_CHAR_BUFFER_SIZE];
        int count_char;
        int OSC_unique_id; //sent from SC. used for OSC parsing
};

class JuliaReplyWithLoadPath : public JuliaReply
{
    public:
        JuliaReplyWithLoadPath(int OSC_unique_id_, const char* julia_load_path_)
        : JuliaReply(OSC_unique_id_)
        {
            //std::string performs deep copy on char*
            julia_load_path = julia_load_path_;
        }

        inline const char* get_julia_load_path()
        {
            return julia_load_path.c_str();
        }

    private:
        std::string julia_load_path;
};

class JuliaObjectId : public RTClassAlloc
{
    public:
        JuliaObjectId(int julia_object_id_)
        {
            julia_object_id = julia_object_id_;
        }

        inline int get_julia_object_id()
        {
            return julia_object_id;
        }

    private:
        int julia_object_id;
};


/* Number of active entries */
class JuliaEntriesCounter
{
    public:
        inline void advance_active_entries()
        {
            active_entries++;
        }

        inline void decrease_active_entries()
        {
            active_entries--;
            if(active_entries < 0)
                active_entries = 0;
        }

        inline int get_active_entries()
        {
            return active_entries;
        }

    private:
        int active_entries = 0;
};

/* PERHAPS I CAN ALLOCATE MEMORY WITH STANDARD MALLOC HERE; SAVE RT MEMORY FOR JULIA */
class JuliaObjectCompiler
{
    public:
        JuliaObjectCompiler(World* in_world_, JuliaGlobalState* julia_global_)
        {
            in_world = in_world_;
            julia_global = julia_global_;
        }

        ~JuliaObjectCompiler() {}

        inline jl_module_t* eval_julia_object(JuliaReplyWithLoadPath* julia_reply_with_load_path)
        {
            const char* julia_load_path = julia_reply_with_load_path->get_julia_load_path();

            printf("*** OSC_UNIQUE_ID %d ***\n", julia_reply_with_load_path->get_OSC_unique_id());
            printf("*** LOAD PATH: %s ***\n", julia_load_path);

            jl_module_t* evaluated_module = eval_julia_file(julia_load_path);

            if(!evaluated_module)
            {
                printf("ERROR: Invalid module\n");
                return nullptr;
            }

            return evaluated_module;
        }

        inline bool compile_julia_object(JuliaObject* julia_object, jl_module_t* evaluated_module)
        {
            printf("*** MODULE NAME: %s *** \n", jl_symbol_name(evaluated_module->name));

            //If failed any precompilation stage, return false.
            if(!precompile_julia_object(evaluated_module, julia_object))
            {
                printf("ERROR: Failed in compiling Julia @object \"%s\"\n", jl_symbol_name(evaluated_module->name));
                return false;
            }

            //Set this object module to the evaluated one.
            julia_object->evaluated_module = evaluated_module;

            return true;
        }

        /*************************************************************************************************/
        /*************************************************************************************************/
        /*** This should also find a way to remove the module from Main, perhaps set it to jl_nothing? ***/
        /*************************************************************************************************/
        /*************************************************************************************************/
        
        inline bool unload_julia_object(JuliaObject* julia_object)
        {
            if(!julia_object)
            {
                printf("ERROR: Invalid Julia @object \n");
                return false;
            }

            /* if(julia_object->RT_busy)
            {
                printf("WARNING: %s @object is still being used in a SynthDef\n", jl_symbol_name(julia_object->evaluated_module->name));
                return false;
            } */

            if(julia_object->compiled)
            {   
                /* JL_TRY/CATCH here? */
                /* LOOK INTO Base.delete_method to delete method instances directly right now */
                if(delete_methods_from_table(julia_object))
                {
                    remove_julia_object_from_global_def_id_dict(julia_object);
                    null_julia_object(julia_object);
                }
            }

            //Reset memory pointer for this object
            memset(julia_object, 0, sizeof(JuliaObject));

            return true;
        }
    
    private:
        /* VARIABLES */
        World* in_world;
        JuliaGlobalState* julia_global;

        /* EVAL FILE */
        inline jl_module_t* eval_julia_file(const char* path)
        {
            jl_module_t* evaluated_module;
            
            JL_TRY {
                //DO I NEED TO ADVANCE AGE HERE???? Perhaps, I do.
                jl_get_ptls_states()->world_age = jl_get_world_counter();
                
                //The file MUST ONLY contain an @object definition (which loads a module)
                evaluated_module = (jl_module_t*)jl_load(jl_main_module, path);
                
                if(!evaluated_module)
                    jl_error("Invalid julia file");

                if(!jl_is_module(evaluated_module))
                    jl_error("Included file is not a Julia module");
                
                if(!jl_get_global_SC(evaluated_module, "__inputs__"))
                    jl_error("Undefined @inputs");

                if(!jl_get_global_SC(evaluated_module, "__outputs__"))
                    jl_error("Undefined @outputs");

                if(!jl_get_global_SC(evaluated_module, "__UGen__"))
                    jl_error("Undefined @object");
                
                if(!jl_get_global_SC(evaluated_module, "__constructor__"))
                    jl_error("Undefined @constructor");
                
                if(!jl_get_global_SC(evaluated_module, "__perform__"))
                    jl_error("Undefined @perform");

                if(!jl_get_global_SC(evaluated_module, "__destructor__"))
                    jl_error("Undefined @destructor"); 

                jl_exception_clear();
            }
            JL_CATCH {
                jl_get_ptls_states()->previous_exception = jl_current_exception();

                jl_value_t* exception = jl_exception_occurred();
                jl_value_t* sprint_fun = julia_global->get_sprint_fun();
                jl_value_t* showerror_fun = julia_global->get_showerror_fun();

                if(exception)
                {
                    const char* returned_exception = jl_string_ptr(jl_call2(sprint_fun, showerror_fun, exception));
                    printf("ERROR: %s\n", returned_exception);
                }

                evaluated_module = nullptr;
            }

            //Advance age after each include?? THIS IS PROBABLY UNNEEDED
            jl_get_ptls_states()->world_age = jl_get_world_counter();

            return evaluated_module;
        };

        inline void null_julia_object(JuliaObject* julia_object)
        {
            /* STATE */
            julia_object->being_replaced = false;
            julia_object->compiled = false;
            //julia_object->RT_busy = false;
            
            /* MODULE */
            julia_object->evaluated_module = nullptr;
            julia_object->name = nullptr;
            julia_object->num_inputs = -1;
            julia_object->num_outputs = -1;

            /* FUNCTIONS */
            julia_object->ugen_ref_fun = nullptr;
            julia_object->constructor_fun = nullptr;
            julia_object->perform_fun = nullptr;
            julia_object->destructor_fun = nullptr;
            julia_object->set_index_ugen_ref_fun = nullptr;
            julia_object->delete_index_ugen_ref_fun = nullptr;

            /* METHOD INSTANCES */
            julia_object->ugen_ref_instance = nullptr;
            julia_object->constructor_instance = nullptr;
            julia_object->perform_instance = nullptr;
            julia_object->destructor_instance = nullptr;
            julia_object->set_index_ugen_ref_instance = nullptr;
            julia_object->delete_index_ugen_ref_instance = nullptr;
            julia_object->set_index_audio_vector_instance = nullptr;
        }

        inline void add_julia_object_to_global_def_id_dict(JuliaObject* julia_object)
        {
            julia_global->get_global_def_id_dict().add_to_id_dict(julia_object->julia_def);
        }

        inline void remove_julia_object_from_global_def_id_dict(JuliaObject* julia_object)
        {
            julia_global->get_global_def_id_dict().remove_from_id_dict(julia_object->julia_def);
        }

        inline bool precompile_julia_object(jl_module_t* evaluated_module, JuliaObject* julia_object)
        {
            bool precompile_state = precompile_stages(evaluated_module, julia_object);

            printf("PRECOMPILE STATE (precompile_julia_object): %i \n", precompile_state);

            //if any stage failed, keep nullptrs and memset to zero.
            if(!precompile_state)
            {
                null_julia_object(julia_object);
                unload_julia_object(julia_object);
                return false;
            }

            /* REMOVE jl_call() from here--- */
            add_julia_object_to_global_def_id_dict(julia_object);
            
            printf("AFTER ID DICT\n");

            /* precompile_state = true */
            julia_object->compiled = precompile_state;

            return precompile_state;
        }

        inline bool precompile_stages(jl_module_t* evaluated_module, JuliaObject* julia_object)
        {
            bool precompile_state = false;
            
            //jl_get_ptls_states()->world_age = jl_get_world_counter();

            //object that will be created in perform and passed in destructor and ugen_ref, without creating new ones...
            jl_value_t* ugen_object;
            jl_value_t* ins;
            jl_value_t* outs;
            jl_value_t* ugen_ref_object;
            jl_value_t* destructor_fun;
            jl_method_instance_t* destructor_instance;

            printf("PRECOMPILE STATE (precompile_stages): %i \n", precompile_state);

            //jl_get_ptls_states()->world_age = jl_get_world_counter();
            /* These functions will return false if anything goes wrong. */
            if(precompile_constructor(evaluated_module, julia_object))
            {
                //jl_get_ptls_states()->world_age = jl_get_world_counter();
                printf("CONSTRUCTOR DONE\n");
                if(precompile_perform(evaluated_module, &ugen_object, &ins, &outs, julia_object))
                {
                    //jl_get_ptls_states()->world_age = jl_get_world_counter();
                    printf("PERFORM DONE\n");
                    //jl_call1(jl_get_function(jl_base_module, "println"), ugen_object);
                    if(precompile_destructor(evaluated_module, &ugen_object, &destructor_fun, &destructor_instance, julia_object))
                    {
                        //jl_get_ptls_states()->world_age = jl_get_world_counter();
                        printf("DESTRUCTOR DONE\n");
                        if(precompile_ugen_ref(evaluated_module, &ugen_object, &ins, &outs, &destructor_fun, &destructor_instance, &ugen_ref_object, julia_object))
                        {
                            //jl_get_ptls_states()->world_age = jl_get_world_counter();
                            printf("UGEN REF DONE\n");
                            if(precompile_set_index_delete_index_julia_def(evaluated_module, &ugen_ref_object, julia_object))
                            {
                                //jl_get_ptls_states()->world_age = jl_get_world_counter();
                                printf("SET INDEX DONE\n");
                                if(create_julia_def(evaluated_module, julia_object))
                                {
                                    //jl_get_ptls_states()->world_age = jl_get_world_counter();
                                    precompile_state = true;
                                }
                            }
                        }
                    }
                }
            }
            
            //jl_get_ptls_states()->world_age = jl_get_world_counter();
            printf("PRECOMPILE STATE (precompile_stages): %i \n", precompile_state);

            return precompile_state;
        }

        inline bool precompile_constructor(jl_module_t* evaluated_module, JuliaObject* julia_object)
        {
            jl_function_t* ugen_constructor = jl_get_function(evaluated_module, "__constructor__");
            if(!ugen_constructor)
            {
                printf("ERROR: Invalid __constructor__ function\n");
                return false;
            }
            
            /* COMPILATION */
            jl_method_instance_t* compiled_constructor = jl_lookup_generic_and_compile_SC(&ugen_constructor, 1);

            if(!compiled_constructor)
            {
                printf("ERROR: Could not compile __constructor__ function\n");
                return false;
            }

            julia_object->constructor_fun = ugen_constructor;
            julia_object->constructor_instance = compiled_constructor;
            
            return true;
        }

        inline bool precompile_perform(jl_module_t* evaluated_module, jl_value_t** ugen_object, jl_value_t** ins, jl_value_t** outs, JuliaObject* julia_object)
        {
            /* ARRAY CONSTRUCTION */
            size_t perform_nargs = 6;
            jl_value_t* perform_args[perform_nargs];

            /* FUNCTION = perform_args[0] */
            jl_function_t* perform_fun = jl_get_function(evaluated_module, "__perform__");
            if(!perform_fun)
            {
                printf("ERROR: Invalid __perform__ function\n");
                return false;
            }

            /* OBJECT CONSTRUCTION = perform_args[1] */
            jl_function_t* ugen_constructor = jl_get_function(evaluated_module, "__constructor__");
            if(!ugen_constructor)
            {
                printf("ERROR: Invalid __constructor__ function\n");
                return false;
            }
            
            jl_value_t* ugen_object_temp = jl_call0(ugen_constructor);
            if(!ugen_object_temp)
            {
                printf("ERROR: Invalid __constructor__ function\n");
                return false;
            }

            /* SET INDEX AUDIO VECTOR */
            size_t nargs_set_index_audio_vector = 4;
            jl_value_t* args_set_index_audio_vector[nargs_set_index_audio_vector];
            jl_method_instance_t* set_index_audio_vector_instance;

            /* INS / OUTS = perform_args[2]/[3] */
            int num_inputs =  jl_unbox_int32(jl_get_global_SC(evaluated_module, "__inputs__"));
            int num_outputs = jl_unbox_int32(jl_get_global_SC(evaluated_module, "__outputs__"));
            int buffer_size = 16; //Try compiling with a 16 samples period.
            
            //ins::Vector{Vector{Float32}}
            jl_value_t* ins_temp =  (jl_value_t*)jl_alloc_array_1d(julia_global->get_vector_of_vectors_float32(), num_inputs);
            if(!ins_temp)
            {
                printf("ERROR: Could not allocate memory for inputs \n");
                return false;
            }

            //Multiple 1D Arrays for each output buffer
            jl_value_t* ins_1d[num_inputs];

            //Dummy float** in()
            float* dummy_ins[num_inputs];

            //Initialize seed for random num generation in dummy_ins
            srand(time(NULL));

            for(int i = 0; i < num_inputs; i++)
            {
                dummy_ins[i] = (float*)RTAlloc(in_world, buffer_size * sizeof(float));
                if(!dummy_ins[i])
                {
                    printf("ERROR: Could not allocate memory for inputs \n");
                    return false;
                }

                //Emulate some sort of (-1 / 1) random float data as Input 
                for(int y = 0; y < buffer_size; y++)
                    dummy_ins[i][y] = (float)((((float)(rand() / RAND_MAX)) * 2.0) - 1.0);
                
                ins_1d[i] = (jl_value_t*)jl_ptr_to_array_1d(julia_global->get_vector_float32(), dummy_ins[i], buffer_size, 0);
                if(!ins_1d[i])
                {
                    printf("ERROR: Could not create 1d vectors (inputs)\n");
                    return false;
                }

                /* Replace values each loop. No need to allocate more */
                args_set_index_audio_vector[0] = julia_global->get_set_index_audio_vector_fun();
                args_set_index_audio_vector[1] = ins_temp;
                args_set_index_audio_vector[2] = ins_1d[i];
                args_set_index_audio_vector[3] = jl_box_int32(i + 1); //Julia index from 1 onwards

                //compile the audio vector set_index function at first iteration
                if(i == 0)
                {
                    set_index_audio_vector_instance = jl_lookup_generic_and_compile_SC(args_set_index_audio_vector, nargs_set_index_audio_vector);
                    if(!set_index_audio_vector_instance)
                    {
                        printf("ERROR: Could not compile set_index_audio_vector function\n");
                        return false;
                    }
                }
                
                //Use it to set the actual stuff inside the array
                jl_value_t* set_index_success = jl_lookup_generic_and_compile_return_value_SC(args_set_index_audio_vector, nargs_set_index_audio_vector);
                if(!set_index_success)
                {
                    printf("ERROR: Could not instantiate set_index_audio_vector function (inputs)\n");
                    return false;
                }
            }

            //outs::Vector{Vector{Float32}}
            jl_value_t* outs_temp = (jl_value_t*)jl_alloc_array_1d(julia_global->get_vector_of_vectors_float32(), num_outputs);
            if(!outs_temp)
            {
                printf("ERROR: Could not allocate memory for outputs \n");
                return false;
            }

            //Multiple 1D Arrays for each output buffer
            jl_value_t* outs_1d[num_outputs];

            //Dummy float** out()
            float* dummy_outs[num_outputs];

            for(int i = 0; i < num_outputs; i++)
            {
                dummy_outs[i] = (float*)RTAlloc(in_world, buffer_size * sizeof(float));
                if(!dummy_outs[i])
                {
                    printf("ERROR: Could not allocate memory for outs \n");
                    return false;
                }

                for(int y = 0; y < buffer_size; y++)
                    dummy_outs[i][y] = 0.0f;   
                
                outs_1d[i] = (jl_value_t*)jl_ptr_to_array_1d(julia_global->get_vector_float32(), dummy_outs[i], buffer_size, 0);
                if(!outs_1d[i])
                {
                    printf("ERROR: Could not create 1d vectors (outputs)\n");
                    return false;
                }

                /* Replace values each loop. No need to allocate more */
                args_set_index_audio_vector[0] = julia_global->get_set_index_audio_vector_fun();
                args_set_index_audio_vector[1] = outs_temp;
                args_set_index_audio_vector[2] = outs_1d[i];
                args_set_index_audio_vector[3] = jl_box_int32(i + 1); //Julia index from 1 onwards

                //Use it
                jl_value_t* set_index_success = jl_lookup_generic_and_compile_return_value_SC(args_set_index_audio_vector, nargs_set_index_audio_vector);
                if(!set_index_success)
                {
                    printf("ERROR: Could not instantiate set_index_audio_vector function (outputs)\n");
                    return false;
                }
            }

            /* ASSIGN TO ARRAY */
            perform_args[0] = perform_fun;
            perform_args[1] = ugen_object_temp;
            perform_args[2] = ins_temp;  //__ins__
            perform_args[3] = outs_temp; //__outs__
            perform_args[4] = jl_box_int32(buffer_size); //__buffer_size__ 
            perform_args[5] = julia_global->get_scsynth(); //__SCSynth__

            /* COMPILATION. Should it be with precompile() instead? */
            jl_method_instance_t* perform_instance = jl_lookup_generic_and_compile_SC(perform_args, perform_nargs);

            for(int i = 0; i < num_inputs; i++)
                RTFree(in_world, dummy_ins[i]);

            for(int i = 0; i < num_outputs; i++)
                RTFree(in_world, dummy_outs[i]);

            /* JULIA OBJECT ASSIGN */
            if(!perform_instance)
            {
                printf("ERROR: Could not compile __perform__ function\n");
                return false;
            }

            ugen_object[0] = ugen_object_temp;
            ins[0] = ins_temp;
            outs[0] = outs_temp;

            //successful compilation...
            julia_object->perform_fun = perform_fun;
            julia_object->perform_instance = perform_instance;
            julia_object->set_index_audio_vector_instance = set_index_audio_vector_instance;

            return true;
        }

        inline bool precompile_destructor(jl_module_t* evaluated_module, jl_value_t** ugen_object, jl_value_t** destructor_fun, jl_method_instance_t** destructor_instance, JuliaObject* julia_object)
        {
            jl_function_t* destructor_fun_temp = jl_get_function(evaluated_module, "__destructor__");
            if(!destructor_fun_temp)
            {
                printf("ERROR: Invalid __destructor__ function\n");
                return false;
            }

            printf("DESTRUCTOR FUN DONE\n");

            int32_t destructor_nargs = 2;
            jl_value_t* destructor_args[destructor_nargs];

            printf("DESTRUCTOR ALLOC DONE\n");

            destructor_args[0] = destructor_fun_temp;
            destructor_args[1] = ugen_object[0];

            if(!ugen_object)
            {
                printf("ERROR: Invalid ugen_object to destructor\n");
                return false;
            }
            
            /* COMPILATION */
            jl_method_instance_t* destructor_instance_temp = jl_lookup_generic_and_compile_SC(destructor_args, destructor_nargs);

            printf("DESTRUCTOR METHOD DONE\n");

            if(!destructor_instance_temp)
            {
                printf("ERROR: Could not compile __destructor__ function\n");
                return false;
            }

            julia_object->destructor_fun = destructor_fun_temp;
            julia_object->destructor_instance = destructor_instance_temp;

            destructor_fun[0] = destructor_fun_temp;
            destructor_instance[0] = destructor_instance_temp;

            return true;
        }

        inline bool precompile_ugen_ref(jl_module_t* evaluated_module, jl_value_t** ugen_object, jl_value_t** ins, jl_value_t** outs, jl_value_t** destructor_fun, jl_method_instance_t** destructor_instance, jl_value_t** ugen_ref_object, JuliaObject* julia_object)
        {
            jl_function_t* ugen_ref_fun = jl_get_function(evaluated_module, "__UGenRef__");
            if(!ugen_ref_fun)
            {
                printf("ERROR: Invalid __UGenRef__ function\n");
                return false;
            }

            //Ins and outs are pointing to junk data, but I don't care. I just need to precompile the Ref to it.

            int32_t ugen_ref_nargs = 6;
            jl_value_t* ugen_ref_args[ugen_ref_nargs];
            
            //__UGenRef__ constructor
            ugen_ref_args[0] = ugen_ref_fun;
            ugen_ref_args[1] = ugen_object[0];
            ugen_ref_args[2] = ins[0];
            ugen_ref_args[3] = outs[0];
            ugen_ref_args[4] = destructor_fun[0];
            ugen_ref_args[5] = (jl_value_t*)destructor_instance[0];

            /* COMPILATION */
            jl_method_instance_t* ugen_ref_instance = jl_lookup_generic_and_compile_SC(ugen_ref_args, ugen_ref_nargs);

            if(!ugen_ref_instance)
            {
                printf("ERROR: Could not compile __UGenRef__ function\n");
                return false;
            }

            //Create an actual object with same args to pass it to the precompilation of set_index and delete_index functions.
            //Maybe I should not use exception here?
            jl_value_t* ugen_ref_object_temp = jl_lookup_generic_and_compile_return_value_SC(ugen_ref_args, ugen_ref_nargs);
            
            if(!ugen_ref_object)
            {
                printf("ERROR: Could not precompile a __UGenRef__ object\n");
                return false;
            }

            //ASSIGN OBJECT TO POINTER
            ugen_ref_object[0] = ugen_ref_object_temp;

            julia_object->ugen_ref_fun = ugen_ref_fun;
            julia_object->ugen_ref_instance = ugen_ref_instance;

            return true;
        }

        inline bool precompile_set_index_delete_index_julia_def(jl_module_t* evaluated_module, jl_value_t** ugen_ref_object, JuliaObject* julia_object)
        {
            //Precompile set_index and delete_index
            jl_function_t* set_index_ugen_ref_fun = jl_get_function(evaluated_module, "set_index_ugen_ref");
            jl_function_t* delete_index_ugen_ref_fun = jl_get_function(evaluated_module, "delete_index_ugen_ref");
            
            if(!set_index_ugen_ref_fun || !delete_index_ugen_ref_fun)
            {
                printf("ERROR: Invalid set_index or delete_index\n");
                return false;
            }

            jl_value_t* global_object_id_dict = julia_global->get_global_object_id_dict().get_id_dict();
            if(!global_object_id_dict)
            {
                printf("ERROR: Invalid global_object_id_dict\n");
                return false;
            }

            int32_t set_index_nargs = 3;
            jl_value_t* set_index_args[set_index_nargs];
            
            int32_t delete_index_nargs = 3;
            jl_value_t* delete_index_args[delete_index_nargs];

            //set index
            set_index_args[0] = set_index_ugen_ref_fun;
            set_index_args[1] = global_object_id_dict;
            set_index_args[2] = ugen_ref_object[0];

            //delete index
            delete_index_args[0] = delete_index_ugen_ref_fun;
            delete_index_args[1] = global_object_id_dict;
            delete_index_args[2] = ugen_ref_object[0];

            /* COMPILATION */
            jl_method_instance_t* set_index_ugen_ref_instance = jl_lookup_generic_and_compile_SC(set_index_args, set_index_nargs);
            jl_method_instance_t* delete_index_ugen_ref_instance = jl_lookup_generic_and_compile_SC(delete_index_args, delete_index_nargs);

            if(!set_index_ugen_ref_instance || !delete_index_ugen_ref_instance)
            {
                printf("ERROR: Could not compile set_index_ugen_ref_instance or delete_index_ugen_ref_instance\n");
                return false;
            }

            julia_object->set_index_ugen_ref_fun = set_index_ugen_ref_fun;
            julia_object->delete_index_ugen_ref_fun = delete_index_ugen_ref_fun;
            julia_object->set_index_ugen_ref_instance = set_index_ugen_ref_instance;
            julia_object->delete_index_ugen_ref_instance = delete_index_ugen_ref_instance;

            return true;
        }

        inline bool create_julia_def(jl_module_t* evaluated_module, JuliaObject* julia_object)
        {
            jl_function_t* julia_def_fun = julia_global->get_julia_def_fun();
            if(!julia_def_fun)
            {
                printf("ERROR: Invalid julia_def_fun\n");
                return false;
            }

            //Stack allocation...
            int julia_def_function_nargs = 8;
            int32_t nargs = julia_def_function_nargs + 1;
            jl_value_t* julia_def_args[nargs];

            //__JuliaDef__ constructor
            /* Only setting the module and the method_instance_t*.
            No need to add the function too, as they are kept alive as long as the module.*/
            julia_def_args[0] = (jl_value_t*)julia_def_fun;
            julia_def_args[1] = (jl_value_t*)evaluated_module;
            julia_def_args[2] = (jl_value_t*)julia_object->ugen_ref_instance;
            julia_def_args[3] = (jl_value_t*)julia_object->constructor_instance;
            julia_def_args[4] = (jl_value_t*)julia_object->perform_instance;
            julia_def_args[5] = (jl_value_t*)julia_object->destructor_instance;
            julia_def_args[6] = (jl_value_t*)julia_object->set_index_ugen_ref_instance;
            julia_def_args[7] = (jl_value_t*)julia_object->delete_index_ugen_ref_instance;
            julia_def_args[8] = (jl_value_t*)julia_object->set_index_audio_vector_instance;

            //jl_call1(jl_get_function(jl_base_module, "println"), julia_def_fun);

            printf("JULIA_DEF BEFORE CALL\n");
            
            jl_value_t* julia_def = jl_lookup_generic_and_compile_return_value_SC(julia_def_args, nargs);

            //jl_call1(jl_get_function(jl_base_module, "println"), julia_def);

            if(!julia_def)
            {
                printf("ERROR: Could not create a __JuliaDef__\n");   
                return false;
            }         

            julia_object->julia_def = julia_def;

            return true;
        }

        /* ALL THESE SHOULD NOT jl_call(), but INVOKE */
        inline bool delete_methods_from_table(JuliaObject* julia_object)
        {
            /*
            jl_function_t* delete_method_fun = jl_get_function(jl_base_module, "delete_method");
            if(!delete_method_fun)
            {
                printf("ERROR: Could not retrieve \"delete method\" function \n");
                return false;
            }

            jl_method_t* ugen_ref_method = (julia_object->ugen_ref_instance)->def.method;
            jl_value_t* ugen_ref_method_call = jl_call2(delete_method_fun, (jl_value_t*)ugen_ref_method, (julia_object->ugen_ref_instance)->specTypes); //IS specTypes a Tuple???
            if(!ugen_ref_method || !ugen_ref_method_call)
            {
                printf("ERROR: Could not retrieve method for ugen_ref_instance\n");
                return false;
            }

            jl_method_t* constructor_method = (julia_object->constructor_instance)->def.method;
            jl_value_t* constructor_method_call = jl_call2(delete_method_fun, (jl_value_t*)constructor_method, (julia_object->constructor_instance)->specTypes); //IS specTypes a Tuple???
            if(!constructor_method || constructor_method_call)
            {
                printf("ERROR: Could not retrieve method for constructor_instance\n");
                return false;
            }

            jl_method_t* perform_method = (julia_object->perform_instance)->def.method;
            jl_value_t* perform_method_call = jl_call2(delete_method_fun, (jl_value_t*)perform_method, (julia_object->perform_instance)->specTypes); //IS specTypes a Tuple???
            if(!perform_method || !perform_method_call)
            {
                printf("ERROR: Could not retrieve method for perform_instance\n");
                return false;
            }

            jl_method_t* destructor_method = (julia_object->destructor_instance)->def.method;
            jl_value_t* destructor_method_call = jl_call2(delete_method_fun, (jl_value_t*)destructor_method, (julia_object->destructor_instance)->specTypes); //IS specTypes a Tuple???
            if(!destructor_method || !destructor_method_call)
            {
                printf("ERROR: Could not retrieve method for destructor_instance\n");
                return false;
            }

            jl_method_t* set_index_ugen_ref_method = (julia_object->set_index_ugen_ref_instance)->def.method;
            jl_value_t* set_index_ugen_ref_method_call = jl_call2(delete_method_fun, (jl_value_t*)set_index_ugen_ref_method, (julia_object->set_index_ugen_ref_instance)->specTypes); //IS specTypes a Tuple???
            if(!set_index_ugen_ref_method || !set_index_ugen_ref_method_call)
            {
                printf("ERROR: Could not retrieve method for set_index_ugen_ref_instance\n");
                return false;
            }

            jl_method_t* delete_index_ugen_ref_method = (julia_object->delete_index_ugen_ref_instance)->def.method;
            jl_value_t* delete_index_ugen_ref_method_call = jl_call2(delete_method_fun, (jl_value_t*)delete_index_ugen_ref_method, (julia_object->delete_index_ugen_ref_instance)->specTypes); //IS specTypes a Tuple???
            if(!delete_index_ugen_ref_method || !delete_index_ugen_ref_method_call)
            {
                printf("ERROR: Could not retrieve method for delete_index_ugen_ref_instance\n");
                return false;
            }

            jl_method_t* set_index_audio_vector_method = (julia_object->set_index_audio_vector_instance)->def.method;
            jl_value_t* set_index_audio_vector_method_call = jl_call2(delete_method_fun, (jl_value_t*)set_index_audio_vector_method, (julia_object->set_index_audio_vector_instance)->specTypes); //IS specTypes a Tuple???
            if(!set_index_audio_vector_method || !set_index_audio_vector_method_call)
            {
                printf("ERROR: Could not retrieve method for set_index_audio_vector_instance\n");
                return false;
            }
            */
            
            return true;
        }
};

#define JULIA_OBJECTS_ARRAY_INCREMENT 100

//For retrieval on RT thread
enum class JuliaObjectsArrayState {Busy, Free, Invalid};

/* Allocate it with a unique_ptr? Or just a normal new/delete? */
class JuliaObjectsArray : public JuliaObjectCompiler, public JuliaAtomicBarrier, public JuliaEntriesCounter
{
    public:
        JuliaObjectsArray(World* in_world_, JuliaGlobalState* julia_global_)
        : JuliaObjectCompiler(in_world_, julia_global_)
        {
            init_julia_objects_array();
        }

        ~JuliaObjectsArray()
        {
            destroy_julia_objects_array();
        }

        /* NRT THREAD. Called at JuliaDef.new() */
        inline bool create_julia_object(JuliaReplyWithLoadPath* julia_reply_with_load_path)
        {  
            int new_id;
            JuliaObject* julia_object;
            
            /*
            IGNORE RESIZING FOR NOW:
            This won't work, as if the memory location is moved with the new calloc call, the RT thread's JuliaObject* pointers
            will be pointing at junk memory. What I can have is a tagged approach, where every 100 entries I allocate new memory 
            without reallocating the previous one, and just tag the last pointer of previous 100 entries to be the first one of the
            new entries, so it won't be a contiguous block of memory, but it will not change the pointers of previously allocated objects.
            */
            /*
            if(get_active_entries() == num_total_entries)
            {
                //Lock the access to the array only when resizing.
                JuliaAtomicBarrier::Spinlock();

                resize_julia_objects_array();

                JuliaAtomicBarrier::Unlock();
            }
            */

            //Run code evaluation and module compilation
            jl_module_t* evaluated_module = eval_julia_object(julia_reply_with_load_path);
            if(!evaluated_module)
            {
                julia_reply_with_load_path->create_done_command(julia_reply_with_load_path->get_OSC_unique_id(), "/jl_load", -1, "@No_Name", -1, -1);
                return false;
            }
            
            //If an @object already exist, just replace the content at that ID and set its "being_replaced" flag to true.
            new_id = check_existing_module(evaluated_module);
            if(new_id >= 0) //Found a julia_object to be replaced
                julia_object = julia_objects_array + new_id; 
            else if(new_id < 0) //No existing julia_objects with same module name. Get a new id
            {
                //Retrieve a new ID be checking out the first free entry in the julia_objects_array
                for(int i = 0; i < num_total_entries; i++)
                {
                    JuliaObject* this_julia_object = julia_objects_array + i;
                    if(!this_julia_object->compiled) //If empty entry, it means I can take ownership
                    {
                        julia_object = this_julia_object;
                        new_id = i;
                        break;
                    }
                }
            }

            //Unload previous object from same id if it's being replaced by same @object. It assumes NRT thread has lock.
            if(julia_object->being_replaced)
                delete_julia_object(new_id);

            //Run object's compilation.
            bool succesful_compilation = compile_julia_object(julia_object, evaluated_module);
            if(!succesful_compilation)
            {
                julia_reply_with_load_path->create_done_command(julia_reply_with_load_path->get_OSC_unique_id(), "/jl_load", -1, "@No_Name", -1, -1);
                return false;
            }

            printf("ID: %d\n", new_id);

            const char* name = jl_symbol_name(evaluated_module->name);
            int num_inputs =  jl_unbox_int32(jl_get_global_SC(evaluated_module, "__inputs__"));
            int num_outputs = jl_unbox_int32(jl_get_global_SC(evaluated_module, "__outputs__"));

            //SHOULD BE SET BEFORE AT OBJECT COMPILATION. NOT NOW.
            julia_object->name = name;
            julia_object->num_inputs = num_inputs;
            julia_object->num_outputs = num_outputs;

            printf("AFTER COMPILATION \n");

            //Set unique_id in newly created module aswell. Used to retrieve JuliaDef by module name.
            jl_function_t* set_unique_id_fun = jl_get_function(evaluated_module, "__set_unique_id__");

            int32_t nargs_set_unique_id = 2;
            jl_value_t* args_set_unique_id[nargs_set_unique_id];
            
            args_set_unique_id[0] = set_unique_id_fun;
            args_set_unique_id[1] = jl_box_int32(num_inputs);

            jl_value_t* succesful_set_unique_id = jl_lookup_generic_and_compile_return_value_SC(args_set_unique_id, nargs_set_unique_id);
            if(!succesful_set_unique_id)
            {
                julia_reply_with_load_path->create_done_command(julia_reply_with_load_path->get_OSC_unique_id(), "/jl_load", -1, "@No_Name", -1, -1);
                return false;
            }
            
            //MSG: OSC id, cmd, id, name, inputs, outputs
            julia_reply_with_load_path->create_done_command(julia_reply_with_load_path->get_OSC_unique_id(), "/jl_load", new_id, name, num_inputs, num_outputs);

            advance_active_entries();

            return true;
        }

        inline int check_existing_module(jl_module_t* evaluated_module)
        {
            int active_entries = get_active_entries();
            if(!active_entries)
                return -1;
            
            int entries_count = 0;

            for(int i = 0; i < num_total_entries; i++)
            {
                JuliaObject* this_julia_object = julia_objects_array + i;
                if(this_julia_object->compiled)
                {
                    char* eval_module_name = jl_symbol_name(evaluated_module->name);
                    char* this_julia_object_module_name = jl_symbol_name((this_julia_object->evaluated_module)->name);
                    
                    //Compare string content
                    if(strcmp(eval_module_name, this_julia_object_module_name) == 0)
                    {
                        printf("WARNING: Replacing @object: %s\n", eval_module_name);
                        this_julia_object->being_replaced = true;
                        return i;
                    }

                    entries_count++;
                    //Scanned through all active entries.
                    if(entries_count == active_entries)
                        return -1;
                }
            }

            //No other module with same name
            return -1;
        }

        /* RT THREAD. Called when a Julia UGen is created on the server */
        inline JuliaObjectsArrayState get_julia_object(int unique_id, JuliaObject** julia_object)
        {
            bool barrier_acquired = JuliaAtomicBarrier::RTTrylock();

            if(barrier_acquired)
            {
                JuliaObject* this_julia_object = julia_objects_array + unique_id;

                if(this_julia_object->compiled)
                    julia_object[0] = this_julia_object;
                else
                {
                    printf("WARNING: Invalid @object. Perhaps JuliaDef is not valid on server \n");
                    JuliaAtomicBarrier::Unlock();
                    return JuliaObjectsArrayState::Invalid;
                }

                JuliaAtomicBarrier::Unlock();
                return JuliaObjectsArrayState::Free;
            }

            
            return JuliaObjectsArrayState::Busy;
        }

        /* NRT THREAD. Called at JuliaDef.free() */
        inline void delete_julia_object(int unique_id)
        {
            //JuliaAtomicBarrier::NRTSpinlock();

            JuliaObject* this_julia_object = julia_objects_array + unique_id;
            
            unload_julia_object(this_julia_object);
            if(unload_julia_object(this_julia_object))
                decrease_active_entries();

            //JuliaAtomicBarrier::Unlock();
        }

    private:
        //Array of JuliaObject(s)
        JuliaObject* julia_objects_array = nullptr;

        //incremental size
        int num_total_entries = JULIA_OBJECTS_ARRAY_INCREMENT;

        //Constructor
        inline void init_julia_objects_array()
        {
            //RTalloc?
            JuliaObject* res = (JuliaObject*)calloc(num_total_entries, sizeof(JuliaObject));
            
            if(!res)
            {
                printf("Failed to allocate memory for JuliaObjects class \n");
                return;
            }

            julia_objects_array = res;
        }

        //Destructor
        inline void destroy_julia_objects_array()
        {
            free(julia_objects_array);
        }

        //Called when id > num_entries.
        inline void resize_julia_objects_array()
        {
            /* int previous_num_total_entries = num_total_entries;

            //Add more entries
            advance_num_total_entries();

            //Manual realloc()... more control on the different stages
            JuliaObject* res = (JuliaObject*)calloc(num_total_entries, sizeof(JuliaObject));

            //If failed, reset num entries to before and exit.
            if(!res)
            {
                decrease_num_total_entries();
                printf("Failed to allocate more memory for JuliaObjects class \n");
                return; //julia_objects_array is still valid
            }

            //copy previous entries to new array
            memcpy(res, julia_objects_array, previous_num_total_entries);
            
            //previous array to be freed
            JuliaObject* previous_julia_objects_array = julia_objects_array; 

            //swap pointers. No need for atomic, since JuliaAtomicBarrier::Spinlock has acquired already
            julia_objects_array = res;

            //free previous array.
            free(previous_julia_objects_array); */
        }

        inline void advance_num_total_entries()
        {
            num_total_entries += JULIA_OBJECTS_ARRAY_INCREMENT;
        }

        inline void decrease_num_total_entries()
        {
            num_total_entries -= JULIA_OBJECTS_ARRAY_INCREMENT;
            if(num_total_entries < 0)
                num_total_entries = 0;
        }
};

/***************************************************************************/
                            /* GLOBAL VARS */
/***************************************************************************/
/* GLOBAL STATE */
JuliaGlobalState*   julia_global_state;

/* OBJECT COMPILER AND ARRAY OF JuliaObject* */
JuliaAtomicBarrier* julia_compiler_barrier;
JuliaObjectsArray*  julia_objects_array;

/* GC */
JuliaAtomicBarrier* julia_gc_barrier;
std::thread perform_gc_thread;
std::atomic<bool> perform_gc_thread_run;
int gc_count = 0;

/* For edge cases only where GC is performing at a UGen destructor */
std::atomic<bool>gc_array_needs_emptying{false};
int gc_array_num = 1000; //Maximum of 1000 concurrent UGens... Should be enough
jl_value_t** gc_array;

/****************************************************************************/
                            /* ASYNC COMMANDS */
/****************************************************************************/

/* I ONCE HAD A : 
gc_assert_datatype_fail(ptls, vt, sp); 
IT WAS CAUSED BY THE FACT THAT AllocPool IS NOT THREAD SAFE. SOME MEMORY ALLOCATED, THEN
WAS CORRUPTED AND UNRECOGNIZED BY JULIA. FOR NOW, I SHOULD HAVE AN EXCLUSIVE ACCESS TO THE ALLOCATOR, 
EITHER THE RT THREAD OR NRT THREAD CAN RUN. GC CANNOT HAPPEN AT THE SAME TIME OF RT THREAD.
IF WANTING A THREAD-SAFE ALLOCATOR, LOOK INTO supernova's simple_pool CLASS
*/
inline void perform_gc(int full)
{
    julia_gc_barrier->NRTSpinlock();

    /*
     
    */
    if(gc_array_needs_emptying)
    {
        for(int i = 0; i < gc_array_num; i++)
        {
            jl_value_t* this_ugen_ref = gc_array[i];

            //Remove UGenRef from global object id dict here
            if(this_ugen_ref != nullptr)
            {
                printf("WARNING: DELETING __UGenRef__ FROM GC AND GLOBAL_OBJECT_ID_DICT\n");
                
                int32_t delete_index_nargs = 3;
                jl_value_t* delete_index_args[delete_index_nargs];

                /* Quite inefficient (I can't use julia_object->delete_index_ugen_ref_fun), but it should not mess up with World Age in RT thread. */
                delete_index_args[0] = julia_global_state->get_delete_index_fun();
                delete_index_args[1] = julia_global_state->get_global_object_id_dict().get_id_dict();
                delete_index_args[2] = this_ugen_ref;

                /* This jl_lookup_generic_and_compile_return_value_SC() function has JL_TRY and JL_CATCH blocks. I should get rid of those */
                jl_value_t* delete_index_successful = jl_lookup_generic_and_compile_return_value_SC(delete_index_args, delete_index_nargs);
                if(!delete_index_successful)
                    Print("ERROR: Could not delete __UGenRef__ object from global object id dict\n");

                gc_array[i] = nullptr;
            }
        }

        gc_array_needs_emptying = false;
    }

    if(!jl_gc_is_enabled())
    {
        printf("-> Enabling GC...\n");
        jl_gc_enable(1);
    }
    
    jl_gc_collect(full);
    
    printf("-> Completed GC\n");
    
    if(jl_gc_is_enabled())
    {
        printf("-> Disabling GC...\n");
        jl_gc_enable(0);
    }

    julia_gc_barrier->Unlock();
}

inline bool julia_perform_gc(World* world, void* cmd)
{
    printf("*** GC (NRT) Thread ID: %d ***\n", std::this_thread::get_id());
    
    if(julia_global_state->is_initialized())
        perform_gc(1);
    else
        printf("WARNING: Julia hasn't been booted correctly \n");
    
    return true;
}

void julia_gc_cleanup(World* world, void* cmd) {}

void JuliaGC(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "/jl_gc", nullptr, (AsyncStageFn)julia_perform_gc, 0, 0, julia_gc_cleanup, 0, nullptr);
}

/* There is no need to wrap every other NRT call into Julia into a check with the GC call, as all the calls (including GC) happen
on the NRT thread anyway, and so they are sequentially executed. There is no parallelism at any time, because the third thread I am using
for scheduling the GC calls, simply calls the function that will post the GC perform functio into the NRT thread FIFO. It's not actually executing
that function. */

/* 
THIS THREAD COULD JUST CHECK EVERY 10 SECONDS IF THE global_object_id_dict IS EMPTY, LOCKING GC BEFOREHAND.
ACTUALLY, I could just have a static global variable that counts the number of active Julia UGens, and simply check
that value, without checking the id dict size. When value is zero, lock GC, empty gc_array and perform.
THIS WAY I KNOW I WON'T EVER PERFORM GC WHILE ANY UGEN IS PLAYING, AND NO UGEN WILL EVER BE ALLOCATED WHILE GC IS PERFORMING.
*/
inline void perform_gc_on_NRT_thread(World* inWorld)
{
    while(perform_gc_thread_run)
    {
        if(julia_compiler_barrier->RTTrylock())
        {
            JuliaGC(inWorld, nullptr, nullptr, nullptr);
            julia_compiler_barrier->Unlock();
        }

        printf("*** My Thread ID: %d ***\n", std::this_thread::get_id());

        //Run every 10 seconds. Should probably set it to something higher for a full sweep (like once a minute), 
        //And a perform_gc(0) every 30 seconds
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    printf("*** MY THREAD DONE ***\n");
}

struct JuliaBootArgs
{
    int julia_pool_alloc_mem_size;
};

//On RT Thread for now.
inline bool julia_boot(World* inWorld, void* cmd)
{
    if(!jl_is_initialized())
    {
        JuliaBootArgs* julia_boot_args = (JuliaBootArgs*)cmd;

        int julia_pool_alloc_mem_size = julia_boot_args->julia_pool_alloc_mem_size;

        julia_global_state = new JuliaGlobalState(inWorld, julia_pool_alloc_mem_size);
        if(julia_global_state->is_initialized())
        {
            julia_gc_barrier       = new JuliaAtomicBarrier();
            julia_compiler_barrier = new JuliaAtomicBarrier();
            julia_objects_array    = new JuliaObjectsArray(inWorld, julia_global_state);

            gc_array = (jl_value_t**)malloc(sizeof(jl_value_t*) * gc_array_num);
            if(!gc_array)
            {
                printf("ERROR: Could not allocate gc_array\n");
                return false;
            }

            for(int i = 0; i < gc_array_num; i++)
                gc_array[i] = nullptr;

            perform_gc(1);

            //Setup thread for GC collection every 10 seconds. This thread will call into the NRT thread async mechanism
            //perform_gc_thread_run.store(true);
            //perform_gc_thread = std::thread(perform_gc_on_NRT_thread, inWorld);
        }
    }
    else
        printf("WARNING: Julia already booted \n");
    
    return true;
}

void julia_boot_cleanup(World* world, void* cmd) 
{
    JuliaBootArgs* julia_boot_args = (JuliaBootArgs*)cmd;
    if(julia_boot_args)
        RTFree(world, julia_boot_args);
}

void JuliaBoot(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    /* first argument is the memory size for JuliaAllocPool */
    int julia_pool_alloc_mem_size = args->geti();

    printf("POOL MEM SIZE %d\n", julia_pool_alloc_mem_size);

    JuliaBootArgs* julia_boot_args = (JuliaBootArgs*)RTAlloc(inWorld, sizeof(JuliaBootArgs));
    if(!julia_boot_args)
        return;

    julia_boot_args->julia_pool_alloc_mem_size = julia_pool_alloc_mem_size;

    DoAsynchronousCommand(inWorld, replyAddr, "/jl_boot", (void*)julia_boot_args, 0, (AsyncStageFn)julia_boot, 0, julia_boot_cleanup, 0, nullptr);
}

bool julia_load(World* world, void* cmd)
{
    JuliaReplyWithLoadPath* julia_reply_with_load_path = (JuliaReplyWithLoadPath*)cmd;

    if(julia_global_state->is_initialized())
    {
        julia_compiler_barrier->NRTSpinlock();

        if(gc_count == 0)
            perform_gc(1);
        
        julia_objects_array->create_julia_object(julia_reply_with_load_path);

        perform_gc(1);
        
        julia_compiler_barrier->Unlock();

        //run extra gc before creating object after every 5 calls
        gc_count++;
        if(gc_count == 5)
            gc_count = 0;
    }
    else
    {
        julia_reply_with_load_path->create_done_command(julia_reply_with_load_path->get_OSC_unique_id(), "/jl_load", -1, "@No_Name", -1, -1);
        printf("WARNING: Julia hasn't been booted correctly \n");
    }

    return true;
}

void julia_load_cleanup(World* world, void* cmd) 
{
    JuliaReplyWithLoadPath* julia_reply_with_load_path = (JuliaReplyWithLoadPath*)cmd;

    if(julia_reply_with_load_path)
        JuliaReplyWithLoadPath::operator delete(julia_reply_with_load_path, world); //Needs to be called excplicitly
}

void JuliaLoad(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    if(jl_is_initialized())
    {
        int osc_unique_id = args->geti();
        const char* julia_load_path = args->gets(); //this const char* will be deep copied in constructor.

        //Alloc with overloaded new operator
        JuliaReplyWithLoadPath* julia_reply_with_load_path = new(inWorld) JuliaReplyWithLoadPath(osc_unique_id, julia_load_path);
        
        if(!julia_reply_with_load_path)
        {
            printf("Could not allocate Julia Reply\n");
            return;
        }

        //julia_reply_with_load_path->get_buffer() is the return message that will be sent at "/done" of this async command.
        DoAsynchronousCommand(inWorld, replyAddr, julia_reply_with_load_path->get_buffer(), julia_reply_with_load_path, (AsyncStageFn)julia_load, 0, 0, julia_load_cleanup, 0, nullptr);
    }
    else
        printf("WARNING: Julia hasn't been initialized yet\n");
    
}

bool julia_free(World* world, void* cmd)
{
    if(julia_global_state->is_initialized())
    {
        julia_compiler_barrier->NRTSpinlock();
        
        JuliaObjectId* julia_object_id = (JuliaObjectId*)cmd;
        int object_id = julia_object_id->get_julia_object_id();
        
        julia_objects_array->delete_julia_object(object_id);
        
        julia_compiler_barrier->Unlock();
    }
    return true;
}

void julia_free_cleanup(World* world, void* cmd) 
{
    JuliaObjectId* julia_object_id = (JuliaObjectId*)cmd;

    if(julia_object_id)
        JuliaObjectId::operator delete(julia_object_id, world); //Needs to be called excplicitly
}

void JuliaFree(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    if(jl_is_initialized())
    {
        int object_id = args->geti(); 

        //Alloc with overloaded new operator
        JuliaObjectId* julia_object_id = new(inWorld) JuliaObjectId(object_id);
        
        if(!julia_object_id)
        {
            printf("Could not allocate Julia Reply\n");
            return;
        }

        //julia_reply_with_load_path->get_buffer() is the return message that will be sent at "/done" of this async command.
        DoAsynchronousCommand(inWorld, replyAddr, "/jl_free", julia_object_id, (AsyncStageFn)julia_free, 0, 0, julia_free_cleanup, 0, nullptr);
    }
    else
        printf("WARNING: Julia hasn't been initialized yet\n");
    
}

inline bool julia_quit(World* world, void* cmd)
{
    //If julia has been initialized but global_state failed from whatever reason
    if(jl_is_initialized() && !julia_global_state->is_initialized())
    {
        jl_atexit_hook(0);
        return true;
    }
    
    if(julia_global_state->is_initialized())
    {        
        //perform_gc_thread_run.store(false);
        //perform_gc_thread.join();

        //julia_objects_array->~JuliaObjectsArray();
        delete julia_objects_array;
        
        //julia_gc_barrier->~JuliaAtomicBarrier();
        delete julia_gc_barrier;

        //julia_compiler_barrier->~JuliaAtomicBarrier();
        delete julia_compiler_barrier;

        //julia_global_state->~JuliaGlobalState();
        delete julia_global_state;

        free(gc_array);
    }

    printf("-> Julia: Finished quitting \n");

    return true;
}

void julia_quit_cleanup(World* world, void* cmd) {}

void JuliaQuit(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    if(jl_is_initialized())
        DoAsynchronousCommand(inWorld, replyAddr, "/jl_quit", nullptr, (AsyncStageFn)julia_quit, 0, 0, julia_quit_cleanup, 0, nullptr);
}

inline bool julia_query_id_dicts(World* world, void* cmd)
{
    if(julia_global_state->is_initialized())
    {
        julia_compiler_barrier->NRTSpinlock();

        jl_call1(jl_get_function(jl_base_module, "println"), julia_global_state->get_global_def_id_dict().get_id_dict());
        jl_call1(jl_get_function(jl_base_module, "println"), julia_global_state->get_global_object_id_dict().get_id_dict());
        
        julia_compiler_barrier->Unlock();
    }

    return true;
}

void JuliaQueryIdDicts(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    if(jl_is_initialized())
        DoAsynchronousCommand(inWorld, replyAddr, "/jl_query_id_dicts", nullptr, (AsyncStageFn)julia_query_id_dicts, 0, 0, julia_gc_cleanup, 0, nullptr);
}

/* Debug memory footprint of Julia. Notice how UGens don't allocate memory, as they
use GC's pools. Also, notice how the recompilation of same module still takes quite some memory.
Try of ways of reducing the memory footprint of recompiling the same Julia module */
inline bool julia_total_free_memory(World* world, void* cmd)
{
    printf("FREE MEMORY: %zu\n", julia_global_state->get_julia_alloc_funcs()->fRTTotalFreeMemory(julia_global_state->get_julia_alloc_pool()));
    return true;
}

void JuliaTotalFreeMemory(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    if(jl_is_initialized())
        DoAsynchronousCommand(inWorld, replyAddr, "/jl_total_free_memory", nullptr, (AsyncStageFn)julia_total_free_memory, 0, 0, julia_gc_cleanup, 0, nullptr);
}

inline void DefineJuliaCmds()
{
    DefinePlugInCmd("/julia_boot", (PlugInCmdFunc)JuliaBoot, nullptr);
    DefinePlugInCmd("/julia_load", (PlugInCmdFunc)JuliaLoad, nullptr);
    DefinePlugInCmd("/julia_free", (PlugInCmdFunc)JuliaFree, nullptr);
    //DefinePlugInCmd("/julia_replace", (PlugInCmdFunc)JuliaReplace, nullptr);
    DefinePlugInCmd("/julia_GC",   (PlugInCmdFunc)JuliaGC, nullptr);
    DefinePlugInCmd("/julia_quit", (PlugInCmdFunc)JuliaQuit, nullptr);
    DefinePlugInCmd("/julia_query_id_dicts",   (PlugInCmdFunc)JuliaQueryIdDicts, nullptr);
    DefinePlugInCmd("/julia_total_free_memory", (PlugInCmdFunc)JuliaTotalFreeMemory, nullptr);
}