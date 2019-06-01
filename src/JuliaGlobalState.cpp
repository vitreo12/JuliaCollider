#include "JuliaGlobalState.h"
#include "JuliaUtilitiesMacros.hpp"
#include "SC_PlugIn.hpp"
#include <string>
#include <unistd.h>

//for dlopen
#ifdef __linux__
#include <dlfcn.h>
#endif

/* Series of global variables and initialization/quit routines for Julia */

/************************/
/* JuliaGlobalUtilities */
/************************/

bool JuliaGlobalUtilities::initialize_global_utilities(World* in_world)
{
    if(!create_scsynth(in_world) || !create_utils_functions() || !create_datatypes() || !create_julia_def_module() || !create_ugen_object_macro_module())
        return false;

    return true;
}

//This is perhaps useless. It's executed when Julia is booting off anyway.
void JuliaGlobalUtilities::unload_global_utilities()
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
bool JuliaGlobalUtilities::create_scsynth(World* in_world)
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

bool JuliaGlobalUtilities::create_utils_functions()
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

bool JuliaGlobalUtilities::create_julia_def_module()
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

bool JuliaGlobalUtilities::create_ugen_object_macro_module()
{
    ugen_object_macro_module = jl_get_module(julia_collider_module, "UGenObjectMacro");
    if(!ugen_object_macro_module)
        return false;

    //Unneeded?
    jl_set_global(jl_main_module, jl_symbol("__JuliaColliderUGenObjectMacroModule__"), (jl_value_t*)ugen_object_macro_module);

    return true;
}

bool JuliaGlobalUtilities::create_datatypes()
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

jl_value_t* JuliaGlobalUtilities::get_scsynth()
{
    return scsynth;
}

jl_function_t* JuliaGlobalUtilities::get_set_index_audio_vector_fun()
{
    return set_index_audio_vector_fun;
}

jl_module_t* JuliaGlobalUtilities::get_scsynth_module()
{
    return scsynth_module;
}

jl_function_t* JuliaGlobalUtilities::get_sprint_fun()
{
    return sprint_fun;
}

jl_function_t* JuliaGlobalUtilities::get_showerror_fun()
{
    return showerror_fun;
}

jl_function_t* JuliaGlobalUtilities::get_set_index_fun()
{
    return set_index_fun;
}

jl_function_t* JuliaGlobalUtilities::get_delete_index_fun()
{
    return delete_index_fun;
}

jl_function_t* JuliaGlobalUtilities::get_julia_def_fun()
{
    return julia_def_fun;
}

jl_value_t* JuliaGlobalUtilities::get_vector_float32()
{
    return vector_float32;
}
jl_value_t* JuliaGlobalUtilities::get_vector_of_vectors_float32()
{
    return vector_of_vectors_float32;
}

/*************/
/* JuliaPath */
/*************/

JuliaPath::JuliaPath()
{
    julia_version_string = std::string(jl_ver_string());
    julia_version_maj_min = julia_version_string.substr(0, julia_version_string.size()-2); //Remove last two characters. Result is "1.1"

    julia_path_to_sysimg  = "julia/scide_lib/julia";
    julia_path_to_stdlib  = std::string("julia/stdlib/v").append(julia_version_maj_min);
    julia_path_to_startup = "julia/startup/startup.jl";

    retrieve_julia_dir();
}

void JuliaPath::retrieve_julia_dir() 
{
    //Get process id and convert it to string
    pid_t scsynth_pid = getpid();
    const char* scsynth_pid_string = (std::to_string(scsynth_pid)).c_str();

    //printf("PID: %i\n", scsynth_pid);

    //Set the scsynthPID enviromental variable, used in the "find_julia_diretory_cmd" bash script
    setenv("scsynthPID", scsynth_pid_string, 1);

    //run script and get a FILE pointer back to the result of the script (which is what's returned by printf in bash script)
    FILE* pipe = popen(find_julia_diretory_cmd, "r");
    
    if (!pipe) 
    {
        printf("ERROR: Could not run bash script to find Julia \n");
        return;
    }
    
    //Maximum of 2048 characters.. It should be enough
    char buffer[2048];
    while(!feof(pipe)) 
    {
        while(fgets(buffer, 2048, pipe) != NULL)
            julia_folder_path += buffer;
    }

    pclose(pipe);

    julia_sysimg_path = julia_folder_path;
    julia_sysimg_path.append(julia_path_to_sysimg);

    julia_stdlib_path = julia_folder_path;
    julia_stdlib_path.append(julia_path_to_stdlib);

    julia_startup_path = julia_folder_path;
    julia_startup_path.append(julia_path_to_startup);

    /*
    printf("*** JULIA PATH: %s ***\n", julia_folder_path.c_str());
    printf("*** JULIA LIB PATH: %s ***\n", julia_sysimg_path.c_str());
    printf("*** JULIA STARTUP PATH : %s ***\n", julia_startup_path.c_str());
    printf("*** JULIA STDLIB PATH : %s ***\n",  julia_stdlib_path.c_str());
    */
}

const char* JuliaPath::get_julia_folder_path()
{
    return julia_folder_path.c_str();
}

const char* JuliaPath::get_julia_sysimg_path()
{
    return julia_sysimg_path.c_str();
}

const char* JuliaPath::get_julia_stdlib_path()
{
    return julia_stdlib_path.c_str();
}

const char* JuliaPath::get_julia_startup_path()
{
    return julia_startup_path.c_str();
}

/********************/
/* JuliaGlobalState */
/********************/

//Ignoring constructor, as initialization will happen AFTER object creation. It will happen when an 
//async command is triggered, which would call into boot_julia.
JuliaGlobalState::JuliaGlobalState(World* SCWorld_, int julia_pool_alloc_mem_size)
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

JuliaGlobalState::~JuliaGlobalState()
{
    //First quit julia. It will run finalizers and it will still need the memory pool to be alive
    quit_julia();

    //Then delete pool 
    delete alloc_pool;
    free(julia_alloc_pool);
    free(julia_alloc_funcs);
}

//Called with async command.
void JuliaGlobalState::boot_julia()
{
    if(!jl_is_initialized() && !initialized)
    {
        printf("-> Booting Julia...\n");

        #ifdef __linux__
            bool loaded_julia_shared_library = load_julia_shared_library();
            if(!loaded_julia_shared_library)
                return;
        #endif

        const char* path_to_julia_sysimg = JuliaPath::get_julia_sysimg_path();
        const char* path_to_julia_stdlib  = JuliaPath::get_julia_stdlib_path();

        //printf("***\nPath to Julia lib:\n %s\n***", path_to_julia_sysimg);

        //Set env variable for JULIA_LOAD_PATH to set correct path to stdlib. It would be
        //much better if I can find a way to set the DATAROOTDIR julia variable to do this.
        //Julia will then use JULIA_LOAD_PATH to retrieve stdlib.
        setenv("JULIA_LOAD_PATH", path_to_julia_stdlib, 1);

        void* pool_starting_position = julia_alloc_pool->alloc_pool->get_area_ptr()->get_unaligned_pointer();
        size_t pool_size = julia_alloc_pool->alloc_pool->get_area_ptr()->get_size();
        
        if(path_to_julia_sysimg)
        {
            #ifdef __APPLE__
                #ifndef SUPERNOVA
                    jl_init_with_image_SC(path_to_julia_sysimg, "sys.dylib", SCWorld, julia_alloc_pool, julia_alloc_funcs, pool_starting_position, pool_size, 0);
                #else
                    jl_init_with_image_SC(path_to_julia_sysimg, "sys.dylib", SCWorld, julia_alloc_pool, julia_alloc_funcs, pool_starting_position, pool_size, 1);
                #endif
            #elif __linux__
                #ifndef SUPERNOVA
                    jl_init_with_image_SC(path_to_julia_sysimg, "sys.so", SCWorld, julia_alloc_pool, julia_alloc_funcs, pool_starting_position, pool_size, 0);
                #else
                    jl_init_with_image_SC(path_to_julia_sysimg, "sys.so", SCWorld, julia_alloc_pool, julia_alloc_funcs, pool_starting_position, pool_size, 1);
                #endif
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
            
            /*
            printf("BINDIR:\n");
            jl_eval_string("println(Sys.BINDIR)");

            //I would need it to be "../../etc"
            printf("SYSCONFDIR: \n");
            jl_eval_string("println(Base.SYSCONFDIR)");

            printf("LOAD_PATH: \n");
            jl_eval_string("println(Base.load_path_expand.(LOAD_PATH))");
            */

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

            /* Thid id dict should be removed */
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

            printf("****************************\n");
            printf("****************************\n");
            printf("**** JuliaCollider %d.%d.%d ***\n", JC_VER_MAJ, JC_VER_MID, JC_VER_MIN);
            printf("**** Julia %s booted ****\n", jl_ver_string());
            printf("****************************\n");
            printf("****************************\n");
            
            initialized = true;
        }
    }
    else if(initialized)
        printf("*** Julia %s already booted ***\n", jl_ver_string());
    else
        printf("ERROR: Could not boot Julia \n"); 
}

bool JuliaGlobalState::run_startup_file()
{
    const char* path_to_julia_startup = JuliaPath::get_julia_startup_path();
    jl_value_t* load_startup = jl_load(jl_main_module, path_to_julia_startup);
    if(!load_startup)
        return false;
    return true;
}

bool JuliaGlobalState::initialize_julia_collider_module()
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
void JuliaGlobalState::quit_julia()
{
    if(initialized)
    {
        printf("-> Quitting Julia...\n");

        JuliaGlobalUtilities::unload_global_utilities();
        global_def_id_dict.unload_id_dict();
        global_object_id_dict.unload_id_dict();
        global_gc_id_dict.unload_id_dict();

        jl_gc_enable(1);

        jl_atexit_hook(0); 
        
        printf("-> Quitted Julia \n");

        #ifdef __linux__
            close_julia_shared_library();
        #endif
    }
}

bool JuliaGlobalState::is_initialized()
{
    return initialized;
}

JuliaGlobalIdDict& JuliaGlobalState::get_global_def_id_dict()
{
    return global_def_id_dict;
}

JuliaGlobalIdDict& JuliaGlobalState::get_global_object_id_dict()
{
    return global_object_id_dict;
}

JuliaGlobalIdDict& JuliaGlobalState::get_global_gc_id_dict()
{
    return global_gc_id_dict;
}

jl_module_t* JuliaGlobalState::get_julia_collider_module()
{
    return julia_collider_module;
}

JuliaAllocPool* JuliaGlobalState::get_julia_alloc_pool()
{
    return julia_alloc_pool;
}

JuliaAllocFuncs* JuliaGlobalState::get_julia_alloc_funcs()
{
    return julia_alloc_funcs;
}

//In julia.h, #define JL_RTLD_DEFAULT (JL_RTLD_LAZY | JL_RTLD_DEEPBIND) is defined. Could I just redefine the flags there?
#ifdef __linux__
    bool JuliaGlobalState::load_julia_shared_library()
    {
        //Should it be RLTD_LAZY instead of RLTD_NOW ?
        dl_handle = dlopen("libjulia.so", RTLD_NOW | RTLD_DEEPBIND | RTLD_GLOBAL);
        if (!dl_handle) {
            fprintf (stderr, "%s\n", dlerror());
            return false;
        }

        return true;
    }

    void JuliaGlobalState::close_julia_shared_library()
    {
        if(dl_handle)
            dlclose(dl_handle);
    }
#endif