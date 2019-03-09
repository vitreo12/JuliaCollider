#include <string>
#include <atomic>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "julia.h"
#include "JuliaUtilities.hpp"

#include "SC_PlugIn.hpp"
#include "SC_Node.h"

//MAC: ./build_install_native.sh ~/Desktop/IP/JuliaCollider/vitreo12-julia/julia-native/ ~/SuperCollider ~/Library/Application\ Support/SuperCollider/Extensions
//LINUX: ./build_install_native.sh ~/Sources/JuliaCollider/vitreo12-julia/julia-native ~/Sources/SuperCollider-3.10.0 ~/.local/share/SuperCollider/Extensions

//for dlopen
#ifdef __linux__
#include <dlfcn.h>
#endif

#pragma once

/***************************************************************************/
                        /* JULIA VERSION std::string */
/***************************************************************************/
std::string julia_version_string = std::string(jl_ver_string());
const std::string julia_version_maj_min = julia_version_string.substr(0, julia_version_string.size()-2); //Remove last two characters. Result is "1.1"

/***************************************************************************/
                            /* INTERFACE TABLE */
/***************************************************************************/
static InterfaceTable* ft;

/***************************************************************************/
                                /* STRUCTS */
/***************************************************************************/
typedef struct JuliaObject
{
    jl_value_t* julia_def; //THIS JuliaDef for this JuliaObject.
    jl_module_t* evaluated_module;
    jl_function_t* ugen_ref_fun;
    jl_function_t* constructor_fun;
    jl_function_t* perform_fun;
    jl_function_t* destructor_fun;
    jl_method_instance_t* ugen_ref_instance;
    jl_method_instance_t* constructor_instance;
    jl_method_instance_t* perform_instance;
    jl_method_instance_t* destructor_instance;
    jl_method_instance_t* set_index_ugen_ref_instance; //set_index and delete index for this __UGenRef__ in the global_object_id_dict()
    jl_method_instance_t* delete_index_ugen_ref_instance; 
    bool compiled;
    bool RT_busy;
} JuliaObject;

/***************************************************************************/
                                /* CLASSES */
/***************************************************************************/
class JuliaAtomicBarrier
{
    public:
        JuliaAtomicBarrier(){}
        ~JuliaAtomicBarrier(){}

        /* To be called from NRT thread only. */
        inline void Spinlock()
        {
            bool expected_val = false;
            //Spinlock. Wait ad-infinitum until the RT thread has set barrier to false.
            while(!barrier.compare_exchange_weak(expected_val, true))
                expected_val = false; //reset expected_val to false as it's been changed in compare_exchange_weak to true
        }

        /* Used in RT thread. Returns true if compare_exchange_strong succesfully exchange the value. False otherwise. */
        inline bool Checklock()
        {
            bool expected_val = false;
            return barrier.compare_exchange_strong(expected_val, true);
        }

        inline void Unlock()
        {
            barrier.store(false);
        }

        inline bool get_barrier_value()
        {
            return barrier.load();
        }

    private:
        std::atomic<bool> barrier{false};
};

class JuliaGCBarrier : public JuliaAtomicBarrier
{
    public:
        JuliaGCBarrier(){}
        ~JuliaGCBarrier(){}

        /* SHOULD I BE USING this-> INSTEAD OF JuliaAtomicBarrier:: ????*/
        inline void NRTPerformGC(int full)
        {
            JuliaAtomicBarrier::Spinlock();

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

            JuliaAtomicBarrier::Unlock();
        }

        inline bool RTChecklock()
        {
            return JuliaAtomicBarrier::Checklock();
        }

        inline void RTUnlock()
        {
            JuliaAtomicBarrier::Unlock();
        }
};

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

            jl_function_t* id_dict_function = jl_get_function(jl_base_module, "IdDict");
            if(!id_dict_function)
                return false;

            id_dict = jl_call0(id_dict_function);
            if(!id_dict)
                return false;

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
            jl_value_t* result = jl_call3(set_index_fun, id_dict, var, var);
            if(!result)
                jl_errorf("Could not add element to %s", global_var_name);
        }

        /* Will throw exception if things go wrong */
        inline void remove_from_id_dict(jl_value_t* var)
        {
            jl_value_t* result = jl_call2(delete_index_fun, id_dict, var);
            if(!result)
                jl_errorf("Could not remove element from %s", global_var_name);
        }

        inline jl_value_t* get_id_dict()
        {
            return id_dict;
        }

    private:
        jl_value_t* id_dict;
        const char* global_var_name;

        jl_function_t* set_index_fun;
        jl_function_t* delete_index_fun;
};

class JuliaGlobalUtilities
{
    public:
        JuliaGlobalUtilities(){}

        ~JuliaGlobalUtilities(){}

        //Actual constructor, called from child class after Julia initialization
        inline bool initialize_global_utilities(World* in_world)
        {
            if(!create_scsynth(in_world) || !create_utils_functions() || !create_datatypes())
                return false;

            return true;
        }

        //This is perhaps useless. It's executed when Julia is booting off anyway.
        inline void unload_global_utilities()
        {
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalSCSynth__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalVectorFloat32__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalVectorOfVectorsFloat32__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaDefFun__"), jl_nothing);
        }

        //Requires "using JuliaCollider" to be ran already
        inline bool create_scsynth(World* in_world)
        {
            jl_module_t* julia_collider_module = jl_get_module_in_main("JuliaCollider");
            if(!julia_collider_module)
                return false;

            jl_module_t* scsynth_module = jl_get_module(julia_collider_module, "SCSynth");
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

            //set global
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalSCSynth__"), scsynth);

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
            
            //Gives for granted that "using JuliaCollider.JuliaDef" was succesfully executed at bootup
            julia_def_fun = jl_get_function(jl_main_module, "__JuliaDef__");
            if(!julia_def_fun)
                return false;
            
            //Unneeded?
            jl_set_global(jl_main_module, jl_symbol("__JuliaDefFun__"), julia_def_fun);

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
        /* Global objects */
        jl_value_t* scsynth;
        
        /* Utilities functions */
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
        JuliaGlobalState(World* SCWorld_, InterfaceTable* SCInterfaceTable_)
        {
            SCWorld = SCWorld_;
            SCInterfaceTable = SCInterfaceTable_;

            if(!SCWorld || !SCInterfaceTable)
            {
                printf("ERROR: Invalid World* or InterfaceTable* \n");
                return;
            }

            boot_julia();
        }
        
        ~JuliaGlobalState()
        {
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
                
                if(path_to_julia_sysimg)
                {
                    #ifdef __APPLE__
                        jl_init_with_image_SC(path_to_julia_sysimg, "sys.dylib", SCWorld, SCInterfaceTable);
                    #elif __linux__
                        jl_init_with_image_SC(path_to_julia_sysimg, "sys.so", SCWorld, SCInterfaceTable);
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

                    //perform_gc(1);

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

            jl_value_t* eval_import_julia_def = jl_eval_string("using JuliaCollider.JuliaDef");
            if(!eval_import_julia_def)
            {
                printf("ERROR: Failed in \"using JuliaCollider.JuliaDef\"\n");
                return false;
            }

            return true;
        }

        //Called when unloading the .so from the server
        inline void quit_julia()
        {
            if(initialized)
            {
                printf("-> Quitting Julia..\n");

                //Run one last GC here?

                JuliaGlobalUtilities::unload_global_utilities();
                global_def_id_dict.unload_id_dict();
                global_object_id_dict.unload_id_dict();

                jl_atexit_hook(0); //on linux it freezes here

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

        jl_module_t* get_julia_collider_module()
        {
            return julia_collider_module;
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
        InterfaceTable* SCInterfaceTable;

        //Extra variable to check if also the JuliaGlobalUtilities thing went through, not just Julia initialization.       
        bool initialized = false;

        JuliaGlobalIdDict global_def_id_dict;
        JuliaGlobalIdDict global_object_id_dict;

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
            //printf("RTALLOC\n");
            return (void*)RTAlloc(in_world, size);
        }

        void operator delete(void* p, World* in_world) 
        {
            //printf("RTFREE\n");
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
        
        int append_string(char* buffer_, size_t size, int value)
        {
            return snprintf(buffer_, size, "%i\n", value);
        }

        //for jl_unbox_int64
        int append_string(char* buffer_, size_t size, long value)
        {
            return snprintf(buffer_, size, "%ld\n", value);
        }

        //for id
        int append_string(char* buffer_, size_t size, unsigned long value)
        {
            return snprintf(buffer_, size, "%lu\n", value);
        }

        //for id
        int append_string(char* buffer_, size_t size, unsigned long long value)
        {
            return snprintf(buffer_, size, "%llu\n", value);
        }

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

        inline unsigned long get_active_entries()
        {
            return active_entries;
        }

    private:
        unsigned long active_entries = 0;
};

/* eval and compile an individual JuliaObject in NRT thread. Using just one args and changing 
args[0] for each function call, as only one thread (the NRT one) at a time will call into this*/
class JuliaObjectCompiler
{
    public:
        JuliaObjectCompiler(World* in_world_, JuliaGlobalState* julia_global_)
        {
            in_world = in_world_;
            julia_global = julia_global_;
        }

        ~JuliaObjectCompiler() {}

        inline jl_module_t* compile_julia_object(JuliaObject* julia_object, const char* path, JuliaReply* julia_reply)
        {
            if(julia_object)
            {
                printf("ERROR: Already assigned Julia @object \n");
                return nullptr;
            }

            jl_module_t* evaluated_module = eval_julia_file(path);

            if(!evaluated_module)
            {
                printf("ERROR: Invalid module\n");
                return nullptr;
            }

            //If failed any precompilation stage, return nullptr.
            if(!precompile_julia_object(evaluated_module, julia_object))
            {
                printf("ERROR: Failed in compiling Julia @object \"%s\"\n", jl_symbol_name(evaluated_module->name));
                return nullptr;
            }

            julia_object->evaluated_module = evaluated_module;

            return evaluated_module;
        }

        inline bool unload_julia_object(JuliaObject* julia_object)
        {
            if(!julia_object)
            {
                printf("ERROR: Invalid Julia @object \n");
                return false;
            }

            if(julia_object->RT_busy)
            {
                printf("WARNING: %s @object is still being used in a SynthDef\n", jl_symbol_name(julia_object->evaluated_module->name));
                return false;
            }

            if(julia_object->compiled)
            {   
                /* JL_TRY/CATCH here? */
                remove_julia_object_from_global_def_id_dict(julia_object);
                null_julia_object(julia_object);
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
                jl_module_t* evaluated_module = (jl_module_t*)jl_load(jl_main_module, path);

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
                
                /* HOW CAN I ADD A CHECK FOR @sample??? */
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

        /* PRECOMPILATION */
        inline jl_value_t** get_perform_args(size_t nargs)
        {
            jl_value_t** perform_args = (jl_value_t**)RTAlloc(in_world, nargs * sizeof(jl_value_t*));
            return perform_args;
        }

        inline void free_perform_args(jl_value_t** perform_args)
        {
            RTFree(in_world, perform_args);
        }

        inline void null_julia_object(JuliaObject* julia_object)
        {
            julia_object->compiled = false;
            julia_object->RT_busy = false;
            julia_object->evaluated_module = nullptr;
            julia_object->ugen_ref_fun = nullptr;
            julia_object->constructor_fun = nullptr;
            julia_object->perform_fun = nullptr;
            julia_object->destructor_fun = nullptr;
            julia_object->ugen_ref_instance = nullptr;
            julia_object->constructor_instance = nullptr;
            julia_object->perform_instance = nullptr;
            julia_object->destructor_instance = nullptr;
            julia_object->set_index_ugen_ref_instance = nullptr;
            julia_object->delete_index_ugen_ref_instance = nullptr;
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

            //if any stage failed, keep nullptrs and memset to zero.
            if(!precompile_state)
            {
                null_julia_object(julia_object);
                unload_julia_object(julia_object);
                return false;
            }

            /* JL_TRY/CATCH here? */
            add_julia_object_to_global_def_id_dict(julia_object);
            
            /* precompile_state = true */
            julia_object->compiled = precompile_state;

            return precompile_state;
        }

        inline bool precompile_stages(jl_module_t* evaluated_module, JuliaObject* julia_object)
        {
            bool precompile_state;
            
            JL_TRY {
                //jl_get_ptls_states()->world_age = jl_get_world_counter();

                //object that will be created in perform and passed in destructor and ugen_ref, without creating new ones...
                jl_value_t* ugen_object;
                jl_value_t* ins;
                jl_value_t* outs;
                jl_value_t* ugen_ref_object;

                /* These functions will throw a jl_error if anything goes wrong. */
                precompile_constructor(evaluated_module, julia_object);
                precompile_perform(evaluated_module, ugen_object, ins, outs, julia_object);
                precompile_destructor(evaluated_module, ugen_object, julia_object);
                precompile_ugen_ref(evaluated_module, ugen_object, ins, outs, ugen_ref_object, julia_object);
                precompile_set_index_delete_index_julia_def(ugen_ref_object, julia_object);
                create_julia_def(evaluated_module, julia_object);
                
                precompile_state = true;

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

                precompile_state = false;
            }

            //jl_get_ptls_states()->world_age = jl_get_world_counter();

            return precompile_state;
        }

        inline void precompile_constructor(jl_module_t* evaluated_module, JuliaObject* julia_object)
        {
            jl_function_t* ugen_constructor = jl_get_function(evaluated_module, "__constructor__");
            if(!ugen_constructor)
                jl_error("Invalid __constructor__ function");
            
            /* COMPILATION */
            jl_method_instance_t* compiled_constructor = jl_lookup_generic_and_compile_SC(&ugen_constructor, 1);

            if(!compiled_constructor)
                jl_error("Could not compile __constructor__ function");

            julia_object->constructor_fun = ugen_constructor;
            julia_object->constructor_instance = compiled_constructor;
        }

        inline void precompile_perform(jl_module_t* evaluated_module, jl_value_t* ugen_object, jl_value_t* ins, jl_value_t* outs, JuliaObject* julia_object)
        {
            /* ARRAY CONSTRUCTION */
            size_t nargs = 6;
            jl_value_t** perform_args = get_perform_args(nargs);
            if(!perform_args)
                jl_error("Could not allocate memory for perform_args");

            /* FUNCTION = perform_args[0] */
            jl_function_t* perform_fun = jl_get_function(evaluated_module, "__perform__");
            if(!perform_fun)
            {
                free_perform_args(perform_args);
                jl_error("Invalid __perform__ function");
            }

            /* OBJECT CONSTRUCTION = perform_args[1] */
            jl_function_t* ugen_constructor = jl_get_function(evaluated_module, "__constructor__");
            if(!ugen_constructor)
            {
                free_perform_args(perform_args);
                jl_error("Invalid __constructor__ function");
            }
            
            ugen_object = jl_call0(ugen_constructor);
            if(!ugen_object)
            {
                free_perform_args(perform_args);
                jl_error("Invalid __constructor__ function");
            }

            /* INS / OUTS = perform_args[2]/[3] */
            int inputs =  jl_unbox_int32(jl_get_global_SC(evaluated_module, "__inputs__"));
            int outputs = jl_unbox_int32(jl_get_global_SC(evaluated_module, "__outputs__"));
            int buffer_size = 1;
            
            //ins::Vector{Vector{Float32}}
            ins =  (jl_value_t*)jl_alloc_array_1d(julia_global->get_vector_of_vectors_float32(), inputs);
            
            //1D Array for each input buffer
            jl_value_t** ins_1d = (jl_value_t**)RTAlloc(in_world, sizeof(jl_value_t*) * inputs);

            float** dummy_ins = (float**)RTAlloc(in_world, inputs * sizeof(float*));

            for(int i = 0; i < inputs; i++)
            {
                dummy_ins[i] = (float*)RTAlloc(in_world, buffer_size * sizeof(float));
                for(int y = 0; y < buffer_size; y++)
                    dummy_ins[i][y] = 0.0f;
                
                ins_1d[i] = (jl_value_t*)jl_ptr_to_array_1d(julia_global->get_vector_float32(), dummy_ins[i], buffer_size, 0);
                jl_call3(julia_global->get_set_index_fun(), ins, ins_1d[i], jl_box_int32(i + 1)); //Julia index from 1 onwards
            }

            //outs::Vector{Vector{Float32}}
            outs = (jl_value_t*)jl_alloc_array_1d(julia_global->get_vector_of_vectors_float32(), outputs);

            //1D Array for each output buffer
            jl_value_t** outs_1d = (jl_value_t**)RTAlloc(in_world, sizeof(jl_value_t*) * outputs);

            float** dummy_outs = (float**)RTAlloc(in_world, outputs * sizeof(float*));

            for(int i = 0; i < outputs; i++)
            {
                dummy_outs[i] = (float*)RTAlloc(in_world, buffer_size * sizeof(float));
                for(int y = 0; y < buffer_size; y++)
                    dummy_outs[i][y] = 0.0f;
                
                outs_1d[i] = (jl_value_t*)jl_ptr_to_array_1d(julia_global->get_vector_float32(), dummy_outs[i], buffer_size, 0);
                jl_call3(julia_global->get_set_index_fun(), outs, outs_1d[i], jl_box_int32(i + 1)); //Julia index from 1 onwards
            }

            /* ASSIGN TO ARRAY */
            perform_args[0] = perform_fun;
            perform_args[1] = ugen_object;
            perform_args[2] = ins;  //__ins__
            perform_args[3] = outs; //__outs__
            perform_args[4] = jl_box_int32(buffer_size); //__buffer_size__ 
            perform_args[5] = julia_global->get_scsynth(); //__SCSynth__

            /* COMPILATION. Should it be with precompile() instead? */
            jl_method_instance_t* perform_instance = jl_lookup_generic_and_compile_SC(perform_args, nargs);

            /* FREE MEMORY */
            free_perform_args(perform_args);

            for(int i = 0; i < inputs; i++)
                RTFree(in_world, dummy_ins[i]);
            RTFree(in_world, dummy_ins);
            RTFree(in_world, ins_1d);

            for(int i = 0; i < outputs; i++)
                RTFree(in_world, dummy_outs[i]);
            RTFree(in_world, dummy_outs);
            RTFree(in_world, outs_1d);

            /* JULIA OBJECT ASSIGN */
            if(!perform_instance)
                jl_error("Could not compile __perform__ function");

            //successful compilation...
            julia_object->perform_fun = perform_fun;
            julia_object->perform_instance = perform_instance;
        }

        inline void precompile_destructor(jl_module_t* evaluated_module, jl_value_t* ugen_object, JuliaObject* julia_object)
        {
            jl_function_t* destructor_fun = jl_get_function(evaluated_module, "__destructor__");
            if(!destructor_fun)
                jl_error("Invalid __destructor__ function");

            int32_t nargs = 2;
            jl_value_t** destructor_args = (jl_value_t**)RTAlloc(in_world, nargs * sizeof(jl_value_t*));
            if(!destructor_args)
                jl_error("Could not allocate memory for destructor_args");

            destructor_args[0] = destructor_fun;
            destructor_args[1] = ugen_object;
            
            /* COMPILATION */
            jl_method_instance_t* destructor_instance = jl_lookup_generic_and_compile_SC(destructor_args, nargs);

            RTFree(in_world, destructor_args);

            if(!destructor_instance)
                jl_error("Could not compile __destructor__ function");

            julia_object->destructor_fun = destructor_fun;
            julia_object->destructor_instance = destructor_instance;
        }

        inline void precompile_ugen_ref(jl_module_t* evaluated_module, jl_value_t* ugen_object, jl_value_t* ins, jl_value_t* outs, jl_value_t* ugen_ref_object, JuliaObject* julia_object)
        {
            jl_function_t* ugen_ref_fun = jl_get_function(evaluated_module, "__UGenRef__");
            if(!ugen_ref_fun)
                jl_error("Invalid __UGenRef__ function");

            //Ins and outs are pointing to junk data, but I don't care. I just need to precompile the Ref to it.

            int32_t nargs = 4;
            jl_value_t** ugen_ref_args = (jl_value_t**)RTAlloc(in_world, nargs * sizeof(jl_value_t*));
            if(!ugen_ref_args)
                jl_error("Could not allocate memory for ugen_ref_args");
            
            //__UGenRef__ constructor
            ugen_ref_args[0] = ugen_ref_fun;
            ugen_ref_args[1] = ugen_object;
            ugen_ref_args[2] = ins;
            ugen_ref_args[3] = outs;

            /* COMPILATION */
            jl_method_instance_t* ugen_ref_instance = jl_lookup_generic_and_compile_SC(ugen_ref_args, nargs);

            if(!ugen_ref_instance)
            {
                jl_error("Could not compile __UGenRef__ function");
                RTFree(in_world, ugen_ref_args);
            }

            //Create an actual object with same args to pass it to the precompilation of set_index and delete_index functions.
            //Maybe I should not use exception here?
            ugen_ref_object = jl_invoke_exception_SC(ugen_ref_instance, ugen_ref_args, nargs);
            
            RTFree(in_world, ugen_ref_args);
            
            if(!ugen_ref_object)
                jl_error("Could not precompile a __UGenRef__ object");

            julia_object->ugen_ref_fun = ugen_ref_fun;
            julia_object->ugen_ref_instance = ugen_ref_instance;
        }

        inline void precompile_set_index_delete_index_julia_def(jl_value_t* ugen_ref_object, JuliaObject* julia_object)
        {
            //Precompile set_index and delete_index
            jl_function_t* set_index_fun = julia_global->get_set_index_fun();
            jl_function_t* delete_index_fun = julia_global->get_delete_index_fun();
            if(!set_index_fun || !delete_index_fun)
                jl_error("Invalid set_index or delete_index");

            jl_value_t* global_object_id_dict = julia_global->get_global_object_id_dict().get_id_dict();
            if(!global_object_id_dict)
                jl_error("Invalid global_object_id_dict");

            int32_t set_index_nargs = 4;
            jl_value_t** set_index_args = (jl_value_t**)RTAlloc(in_world, set_index_nargs * sizeof(jl_value_t*));
            if(!set_index_args)
                jl_error("Failed in allocating memory for set_index_args");
            
            int32_t delete_index_nargs = 3;
            jl_value_t** delete_index_args = (jl_value_t**)RTAlloc(in_world, delete_index_nargs * sizeof(jl_value_t*));
            if(!delete_index_args)
                jl_error("Failed in allocating memory for delete_index_args");

            //set index
            set_index_args[0] = set_index_fun;
            set_index_args[1] = global_object_id_dict;
            set_index_args[2] = ugen_ref_object;
            set_index_args[3] = ugen_ref_object;

            //delete index
            delete_index_args[0] = delete_index_fun;
            delete_index_args[1] = global_object_id_dict;
            delete_index_args[2] = ugen_ref_object;

            /* COMPILATION */
            jl_method_instance_t* set_index_ugen_ref_instance = jl_lookup_generic_and_compile_SC(set_index_args, set_index_nargs);
            jl_method_instance_t* delete_index_ugen_ref_instance = jl_lookup_generic_and_compile_SC(delete_index_args, delete_index_nargs);

            RTFree(in_world, set_index_args);
            RTFree(in_world, delete_index_args);

            if(!set_index_ugen_ref_instance || !delete_index_ugen_ref_instance)
                jl_error("Could not compile set_index_ugen_ref_instance or delete_index_ugen_ref_instance");

            julia_object->set_index_ugen_ref_instance = set_index_ugen_ref_instance;
            julia_object->delete_index_ugen_ref_instance = delete_index_ugen_ref_instance;
        }

        inline void create_julia_def(jl_module_t* evaluated_module, JuliaObject* julia_object)
        {
            jl_function_t* julia_def_fun = julia_global->get_julia_def_fun();
            if(!julia_def_fun)
                jl_error("Invalid julia_def_fun");

            int32_t nargs = 11;
            jl_value_t** julia_def_args = (jl_value_t**)RTAlloc(in_world, nargs * sizeof(jl_value_t*));
            if(!julia_def_args)
                jl_error("Could not allocate memory for julia_def_args");
            
            //__JuliaDef__ constructor
            julia_def_args[0]  = (jl_value_t*)evaluated_module;
            julia_def_args[1]  = (jl_value_t*)julia_object->ugen_ref_fun;
            julia_def_args[2]  = (jl_value_t*)julia_object->constructor_fun;
            julia_def_args[3]  = (jl_value_t*)julia_object->perform_fun;
            julia_def_args[4]  = (jl_value_t*)julia_object->destructor_fun;
            julia_def_args[5]  = (jl_value_t*)julia_object->ugen_ref_instance;
            julia_def_args[6]  = (jl_value_t*)julia_object->constructor_instance;
            julia_def_args[7]  = (jl_value_t*)julia_object->perform_instance;
            julia_def_args[8]  = (jl_value_t*)julia_object->destructor_instance;
            julia_def_args[9]  = (jl_value_t*)julia_object->set_index_ugen_ref_instance;
            julia_def_args[10] = (jl_value_t*)julia_object->delete_index_ugen_ref_instance;

            //Create julia_def
            jl_value_t* julia_def = jl_call(julia_def_fun, julia_def_args, nargs);
            
            //SHOULD I RTFREE AFTER USING julia_def???
            RTFree(in_world, julia_def_args);

            if(!julia_def)
                jl_error("Could not create a __JuliaDef__");            

            julia_object->julia_def = julia_def;
        }
};

#define JULIA_OBJECTS_ARRAY_INCREMENT 100

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
        inline bool create_julia_object(JuliaReply* julia_reply, const char* path)
        {  
            bool result;
            unsigned long long new_id;
            JuliaObject* julia_object;
            
            if(get_active_entries() == num_total_entries)
            {
                //Lock the access to the array only when resizing.
                JuliaAtomicBarrier::Spinlock();

                resize_julia_objects_array();

                JuliaAtomicBarrier::Unlock();
            }

            //Retrieve a new ID be checking out the first free entry in the julia_objects_array
            for(unsigned long long i = 0; i < num_total_entries; i++)
            {
                JuliaObject* this_julia_object = julia_objects_array + i;
                if(!this_julia_object) //If empty entry, it means I can take ownership
                {
                    julia_object = this_julia_object;
                    new_id = i;
                }
            }

            printf("ID: %llu\n", new_id);

            //Run precompilation
            jl_module_t* evaluated_module = compile_julia_object(julia_object, path, julia_reply);

            if(!evaluated_module)
                return false;

            //Check if compilation went through. If it did, increase counter of active entries and formulate correct JuliaReply
            if(julia_object->compiled)
            {
                advance_active_entries();

                const char* name = jl_symbol_name(evaluated_module->name);
                int inputs =  jl_unbox_int32(jl_get_global_SC(evaluated_module, "__inputs__"));
                int outputs = jl_unbox_int32(jl_get_global_SC(evaluated_module, "__outputs__"));

                //Set unique_id in newly created module aswell. Used to retrieve JuliaDef by module name.
                jl_function_t* set_unique_id = jl_get_function(evaluated_module, "__set_unique_id__");
                jl_call1(set_unique_id, jl_box_uint64(new_id));

                //MSG: OSC id, cmd, id, name, inputs, outputs
                julia_reply->create_done_command(julia_reply->get_OSC_unique_id(), "/jl_compile", new_id, name, inputs, outputs);

                result = true;
            }
            else
            {
                //Failed. No id, name, inputs, outputs
                julia_reply->create_done_command(julia_reply->get_OSC_unique_id(), "/jl_compile", -1, "", -1, -1);
                
                result = false;
            }

            return result;
        }

        /* RT THREAD. Called when a Julia UGen is created on the server */
        inline bool get_julia_object(unsigned long long unique_id, JuliaObject* julia_object)
        {
            bool barrier_acquired = JuliaAtomicBarrier::Checklock();

            if(barrier_acquired)
            {
                JuliaObject* this_julia_object = julia_objects_array + unique_id;

                if(this_julia_object)
                    julia_object = this_julia_object;
                else
                    printf("WARNING: Invalid @object\n");

                JuliaAtomicBarrier::Unlock();
            }

            //Return if barrier was acquired. If not, it means the code must be run again at next audio buffer.
            return barrier_acquired;
        }

        /* NRT THREAD. Called at JuliaDef.free() */
        inline void delete_julia_object(unsigned long unique_id, JuliaObject* julia_object)
        {
            JuliaAtomicBarrier::Spinlock();

            JuliaObject* this_julia_object = julia_objects_array + unique_id;
            
            if(unload_julia_object(julia_object))
                decrease_active_entries();

            JuliaAtomicBarrier::Unlock();
        }

    private:
        //Array of JuliaObject(s)
        JuliaObject* julia_objects_array = nullptr;

        //incremental size
        unsigned long num_total_entries = JULIA_OBJECTS_ARRAY_INCREMENT;

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
            unsigned long previous_num_total_entries = num_total_entries;

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
            free(previous_julia_objects_array);
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
JuliaGlobalState*  julia_global_state;
JuliaGCBarrier*    julia_gc_barrier;
JuliaObjectsArray* julia_objects_array;

/****************************************************************************/
                            /* ASYNC COMMANDS */
/****************************************************************************/
//On RT Thread for now.
inline bool julia_boot(World* inWorld, void* cmd)
{
    if(!jl_is_initialized())
    {
        julia_global_state = new JuliaGlobalState(inWorld, ft);
        if(julia_global_state->is_initialized())
            julia_gc_barrier = new JuliaGCBarrier();
    }
    else
        printf("WARNING: Julia already booted \n");
    

    return true;
}

void julia_boot_cleanup(World* world, void* cmd) {}

void JuliaBoot(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "/jl_boot", nullptr, 0, (AsyncStageFn)julia_boot, 0, julia_boot_cleanup, 0, nullptr);
}

bool julia_load(World* world, void* cmd)
{
    if(jl_is_initialized() && julia_global_state->is_initialized())
    {

    }
    else
        printf("WARNING: Julia hasn't been booted correctly \n");

    return true;
}

void julia_load_cleanup(World* world, void* cmd) {}

void JuliaLoad(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "/jl_load", nullptr, (AsyncStageFn)julia_load, 0, 0, julia_load_cleanup, 0, nullptr);
}

inline bool julia_perform_gc(World* world, void* cmd)
{
    if(jl_is_initialized() && julia_global_state->is_initialized())
        julia_gc_barrier->NRTPerformGC(1);
    else
        printf("WARNING: Julia hasn't been booted correctly \n");
    
    return true;
}
void julia_gc_cleanup(World* world, void* cmd) {}

void JuliaGC(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_gc", nullptr, (AsyncStageFn)julia_perform_gc, 0, 0, julia_gc_cleanup, 0, nullptr);
}

inline void DefineJuliaCmds()
{
    DefinePlugInCmd("/julia_boot", (PlugInCmdFunc)JuliaBoot, nullptr);
    DefinePlugInCmd("/julia_load", (PlugInCmdFunc)JuliaLoad, nullptr);
    DefinePlugInCmd("/julia_GC", (PlugInCmdFunc)JuliaGC, nullptr);
}