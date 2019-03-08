#include <string>
#include <atomic>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "JuliaHash.hpp"
#include "JuliaUtilities.hpp"
#include "SC_PlugIn.hpp"
#include "SC_Node.h"
//#include "scsynthsend.h"
//#include "SC_ReplyImpl.hpp"

//MAC: ./build_install_native.sh ~/Desktop/IP/JuliaCollider/vitreo12-julia/julia-native/ ~/SuperCollider ~/Library/Application\ Support/SuperCollider/Extensions
//LINUX: ./build_install_native.sh ~/Sources/JuliaCollider/vitreo12-julia/julia-native ~/Sources/SuperCollider-3.10.0 ~/.local/share/SuperCollider/Extensions

//for dlopen
#ifdef __linux__
#include <dlfcn.h>
#endif

#pragma once

//use vvmap to get a map of the active processes under scsynth. then grep Julia.scx to get the path where it's running from.
//-m1, first occurence
//i=10; complete_string=$(vmmap -w scsynth | grep -m 1 'Julia.scx'); file_string=$(awk -v var="$i" '{print $var}' <<< "$complete_string"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#"$extra_string"}; printf "%s" "${final_string//"Julia.scx"/}"
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
    #define JULIA_DIRECTORY_PATH "i=10; complete_string=$(vmmap -w $scsynthPID | grep -m 1 'Julia.scx'); file_string=$(awk -v var=\"$i\" '{print $var}' <<< \"$complete_string\"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#\"$extra_string\"}; printf \"%s\" \"${final_string//\"Julia.scx\"/}\""
#elif __linux__
    //in linux there is no vvmap.
    //the variable in pmap is 4.
    //I need to run: i=4; ID=$(pgrep scsynth); complete_string=$(pmap -p $ID | grep -m 1 'Julia.so'); file_string=$(awk -v var="$i" '{print $var}' <<< "$complete_string"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#"$extra_string"}; printf "%s" "${final_string//"Julia.so"/}"
    #define JULIA_DIRECTORY_PATH "i=4; complete_string=$(pmap -p $scsynthPID | grep -m 1 'Julia.so'); file_string=$(awk -v var=\"$i\" '{print $var}' <<< \"$complete_string\"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#\"$extra_string\"}; printf \"%s\" \"${final_string//\"Julia.so\"/}\""
#endif

/*
GC IS DISABLED AT ALL TIMES, EXCEPT WHEN IT IS CALLED DIRECTLY IN THE GC COLLECT FUNCTION.
SINCE ALL THE JULIA CODE HAPPENS IN NRT THREAD, THERE IS NO CONTENTION: GC WILL ALWAYS BE DISABLED:
NO NEED FOR ANY JL_GC_PUSH AND JL_GC_POP.
*/

/* GLOBAL VARIABLES */
static InterfaceTable *ft;
World* global_world;

typedef struct JuliaObject
{
    jl_module_t* evaluated_module;
    jl_function_t* constructor_fun;
    jl_function_t* perform_fun;
    jl_function_t* destructor_fun;
    jl_method_instance_t* constructor_instance;
    jl_method_instance_t* perform_instance;
    jl_method_instance_t* destructor_instance;
    bool compiled;
    //bool RT_busy;
} JuliaObject;

/* CLASSES */
class JuliaAtomicBarrier
{
    public:
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

class JuliaGlobalIdDict
{
    public:
        JuliaGlobalIdDict(){}
        ~JuliaGlobalIdDict(){}

        inline bool initialize_global_id_dict()
        {
            jl_function_t* id_dict_function = jl_get_function(jl_base_module, "IdDict");
            if(!id_dict_function)
                return false;

            global_id_dict = jl_call0(id_dict_function);
            if(!global_id_dict)
                return false;

            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalIdDict__"), global_id_dict);

            return true;
        }

        //This is perhaps useless. It's executed when Julia is booting off anyway.
        inline void unload_global_id_dict()
        {
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalIdDict__"), jl_nothing);
        }
        
        /* Will throw exception if things go wrong */
        inline void add_to_global_id_dict(jl_value_t* var)
        {
            jl_value_t* result = jl_call3(set_index, global_id_dict, var, var);
            if(!result)
                jl_error("Could not add element to JuliaIdDict");
        }

        /* Will throw exception if things go wrong */
        inline void remove_from_global_id_dict(jl_value_t* var)
        {
            jl_value_t* result = jl_call2(delete_index, global_id_dict, var);
            if(!result)
                jl_error("Could not remove element from JuliaIdDict");
        }

        inline jl_value_t* get_global_id_dict()
        {
            return global_id_dict;
        }

        inline jl_function_t* get_set_index()
        {
            return set_index;
        }

        inline jl_function_t* get_delete_index()
        {
            return delete_index;
        }

    private:
        jl_value_t* global_id_dict;

        jl_function_t* set_index;
        jl_function_t* delete_index;
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
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalSprintFun__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalShowerrortFun__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalVectorFloat32__"), jl_nothing);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalVectorOfVectorsFloat32__"), jl_nothing);
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
            
            //set global
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalSprintFun__"), sprint_fun);
            jl_set_global(jl_main_module, jl_symbol("__JuliaColliderGlobalShowerrortFun__"), showerror_fun);

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

            julia_lib_path = julia_folder_path;
            julia_lib_path.append(julia_folder_structure);

            printf("*** JULIA PATH: %s ***\n", julia_folder_path.c_str());
            printf("*** JULIA LIB PATH: %s ***\n", julia_lib_path.c_str());
        }

        const char* get_julia_folder_path()
        {
            return julia_folder_path.c_str();
        }

        const char* get_julia_lib_path()
        {
            return julia_lib_path.c_str();
        }

        const char* get_julia_folder_structure()
        {
            return julia_folder_structure.c_str();
        }

    private:
        std::string julia_folder_path;
        std::string julia_lib_path;
        const std::string julia_folder_structure = "julia/lib/julia";

        #ifdef __APPLE__
            const char* find_julia_diretory_cmd = "i=10; complete_string=$(vmmap -w $scsynthPID | grep -m 1 'Julia.scx'); file_string=$(awk -v var=\"$i\" '{print $var}' <<< \"$complete_string\"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#\"$extra_string\"}; printf \"%s\" \"${final_string//\"Julia.scx\"/}\"";
        #elif __linux__
            const char* find_julia_diretory_cmd = "i=4; complete_string=$(pmap -p $scsynthPID | grep -m 1 'Julia.so'); file_string=$(awk -v var=\"$i\" '{print $var}' <<< \"$complete_string\"); extra_string=${complete_string%$file_string*}; final_string=${complete_string#\"$extra_string\"}; printf \"%s\" \"${final_string//\"Julia.so\"/}\"";
        #endif
};

//new / delete
class JuliaState : public JuliaPath, public JuliaGlobalIdDict, public JuliaGlobalUtilities
{
    public:
        //Ignoring constructor, as initialization will happen AFTER object creation. It will happen when an 
        //async command is triggered, which would call into boot_julia.
        JuliaState(){}
        
        ~JuliaState(){}

        //Called with async command.
        inline void boot_julia(World* in_world, InterfaceTable* interface_table)
        {
            if(!initialized)
            {
                SCWorld = in_world;
                SCInterfaceTable = interface_table;

                if(!SCWorld || !SCInterfaceTable)
                {
                    printf("ERROR: Invalid World* or InterfaceTable* \n");
                    return;
                }

                printf("-> Booting Julia...\n");

                #ifdef __linux__
                    load_julia_shared_library();
                #endif

                const char* path_to_julia_lib = JuliaPath::get_julia_lib_path();

                printf("Path to Julia lib:\n %s\n", path_to_julia_lib);
                
                if(path_to_julia_lib)
                {
                    #ifdef __APPLE__
                        jl_init_with_image_SC(path_to_julia_lib, "sys.dylib", SCWorld, SCInterfaceTable);
                    #elif __linux__
                        jl_init_with_image_SC(path_to_julia_lib, "sys.so", SCWorld, SCInterfaceTable);
                    #endif
                }

                if(jl_is_initialized())
                {
                    jl_gc_enable(0);

                    bool initialized_julia_collider = initialize_julia_collider_module();
                    if(!initialized_julia_collider)
                    {
                        printf("ERROR: Could not intialize JuliaCollider module\n");
                        return;
                    }

                    bool initialized_global_id_dict = JuliaGlobalIdDict::initialize_global_id_dict();
                    if(!initialized_global_id_dict)
                    {
                        printf("ERROR: Could not intialize JuliaIdDict \n");
                        return;
                    }

                    bool initialized_global_utilities = JuliaGlobalUtilities::initialize_global_utilities(SCWorld);
                    if(!initialized_global_utilities)
                    {
                        printf("ERROR: Could not intialize JuliaGlobalUtilities\n");
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

        inline bool initialize_julia_collider_module()
        {            
            jl_value_t* eval_using_julia_collider = jl_eval_string("using JuliaCollider");
                
            if(!eval_using_julia_collider)
            {
                printf("ERROR: Failed in \"using JuliaCollider\"\n");
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
        
        #ifdef __linux__
            void* dl_handle;
        #endif
};

//uninitialized global julia_state. Only the constructor to JuliaPath is called now.
JuliaState julia_state;

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
        JuliaObjectCompiler(World* in_world_, JuliaState* julia_global_)
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

            if(evaluated_module)
            {
                //If failed any precompilation stage, nullptr.
                if(!precompile_julia_object(evaluated_module, julia_object))
                {
                    printf("WARNING: Failed in compiling Julia @object \"%s\"\n", jl_symbol_name(evaluated_module->name));
                    return nullptr;
                }

                julia_object->evaluated_module = evaluated_module;
            }

            return evaluated_module;
        }

        inline void unload_julia_object(JuliaObject* julia_object)
        {
            if(!julia_object)
            {
                printf("ERROR: Invalid Julia @object \n");
                return;
            }

            if(julia_object->compiled)
            {
                null_julia_object(julia_object);

                //Remove from GlobalIdDict
                remove_julia_object_from_global_id_dict(julia_object);
            }

            //Reset memory pointer for this object
            memset(julia_object, 0, sizeof(JuliaObject));
        }
    
    private:
        /* VARIABLES */
        World* in_world;
        JuliaState* julia_global;

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
            julia_object->evaluated_module = nullptr;
            julia_object->constructor_fun = nullptr;
            julia_object->perform_fun = nullptr;
            julia_object->destructor_fun = nullptr;
            julia_object->constructor_instance = nullptr;
            julia_object->perform_instance = nullptr;
            julia_object->destructor_instance = nullptr;
        }

        inline void add_julia_object_to_global_id_dict(JuliaObject* julia_object)
        {
            julia_global->add_to_global_id_dict((jl_value_t*)julia_object->evaluated_module);
            julia_global->add_to_global_id_dict((jl_value_t*)julia_object->constructor_fun);
            julia_global->add_to_global_id_dict((jl_value_t*)julia_object->perform_fun);
            julia_global->add_to_global_id_dict((jl_value_t*)julia_object->destructor_fun);
            julia_global->add_to_global_id_dict((jl_value_t*)julia_object->constructor_instance);
            julia_global->add_to_global_id_dict((jl_value_t*)julia_object->perform_instance);
            julia_global->add_to_global_id_dict((jl_value_t*)julia_object->destructor_instance);
        }

        inline void remove_julia_object_from_global_id_dict(JuliaObject* julia_object)
        {
            julia_global->remove_from_global_id_dict((jl_value_t*)julia_object->evaluated_module);
            julia_global->remove_from_global_id_dict((jl_value_t*)julia_object->constructor_fun);
            julia_global->remove_from_global_id_dict((jl_value_t*)julia_object->perform_fun);
            julia_global->remove_from_global_id_dict((jl_value_t*)julia_object->destructor_fun);
            julia_global->remove_from_global_id_dict((jl_value_t*)julia_object->constructor_instance);
            julia_global->remove_from_global_id_dict((jl_value_t*)julia_object->perform_instance);
            julia_global->remove_from_global_id_dict((jl_value_t*)julia_object->destructor_instance);
        }

        inline bool precompile_julia_object(jl_module_t* evaluated_module, JuliaObject* julia_object)
        {
            bool precompile_state = precompile_stages(evaluated_module, julia_object);

            //if any stage failed, keep nullptrs.
            if(!precompile_state)
            {
                null_julia_object(julia_object);
                return false;
            }

            JL_TRY {
                add_julia_object_to_global_id_dict(julia_object);
                
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

                null_julia_object(julia_object);

                precompile_state = false;
            }

            julia_object->compiled = precompile_state;
            return precompile_state;
        }

        inline bool precompile_stages(jl_module_t* evaluated_module, JuliaObject* julia_object)
        {
            bool precompile_state;
            
            JL_TRY {
                //jl_get_ptls_states()->world_age = jl_get_world_counter();

                /* These functions will throw a jl_error if anything goes wrong. */
                precompile_constructor(evaluated_module, julia_object);
                precompile_perform(evaluated_module, julia_object);
                precompile_destructor(evaluated_module, julia_object);
                
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

        inline void precompile_perform(jl_module_t* evaluated_module, JuliaObject* julia_object)
        {
            /* ARRAY CONSTRUCTION */
            size_t nargs = 6;
            jl_value_t** perform_args = get_perform_args(nargs);

            /* FUNCTION = perform_args[0] */
            jl_function_t* perform_function = jl_get_function(evaluated_module, "__perform__");
            if(!perform_function)
                jl_error("Invalid __perform__ function");

            /* OBJECT CONSTRUCTION = perform_args[1] */
            jl_function_t* ugen_constructor = jl_get_function(evaluated_module, "__constructor__");
            if(!ugen_constructor)
                jl_error("Invalid __constructor__ function");
            
            jl_value_t* ugen_object = jl_call0(ugen_constructor);

            /* INS / OUTS = perform_args[2]/[3] */
            int inputs =  jl_unbox_int32(jl_get_global_SC(evaluated_module, "__inputs__"));
            int outputs = jl_unbox_int32(jl_get_global_SC(evaluated_module, "__outputs__"));
            int buffer_size = 1;
            
            //ins::Vector{Vector{Float32}}
            jl_value_t* ins =  (jl_value_t*)jl_alloc_array_1d(julia_global->get_vector_of_vectors_float32(), inputs);
            
            //1D Array for each input buffer
            jl_value_t** ins_1d = (jl_value_t**)RTAlloc(in_world, sizeof(jl_value_t*) * inputs);

            float** dummy_ins = (float**)RTAlloc(in_world, inputs * sizeof(float*));

            for(int i = 0; i < inputs; i++)
            {
                dummy_ins[i] = (float*)RTAlloc(in_world, buffer_size * sizeof(float));
                for(int y = 0; y < buffer_size; y++)
                    dummy_ins[i][y] = 0.0f;
                
                ins_1d[i] = (jl_value_t*)jl_ptr_to_array_1d(julia_global->get_vector_float32(), dummy_ins[i], buffer_size, 0);
                jl_call3(julia_global->get_set_index(), ins, ins_1d[i], jl_box_int32(i + 1)); //Julia index from 1 onwards
            }

            //outs::Vector{Vector{Float32}}
            jl_value_t* outs = (jl_value_t*)jl_alloc_array_1d(julia_global->get_vector_of_vectors_float32(), outputs);

            //1D Array for each output buffer
            jl_value_t** outs_1d = (jl_value_t**)RTAlloc(in_world, sizeof(jl_value_t*) * outputs);

            float** dummy_outs = (float**)RTAlloc(in_world, outputs * sizeof(float*));

            for(int i = 0; i < outputs; i++)
            {
                dummy_outs[i] = (float*)RTAlloc(in_world, buffer_size * sizeof(float));
                for(int y = 0; y < buffer_size; y++)
                    dummy_outs[i][y] = 0.0f;
                
                outs_1d[i] = (jl_value_t*)jl_ptr_to_array_1d(julia_global->get_vector_float32(), dummy_outs[i], buffer_size, 0);
                jl_call3(julia_global->get_set_index(), outs, outs_1d[i], jl_box_int32(i + 1)); //Julia index from 1 onwards
            }

            /* ASSIGN TO ARRAY */
            perform_args[0] = perform_function;
            perform_args[1] = ugen_object;
            perform_args[2] = ins;  //__ins__
            perform_args[3] = outs; //__outs__
            perform_args[4] = jl_box_int32(buffer_size); //__buffer_size__ 
            perform_args[5] = julia_global->get_scsynth(); //__SCSynth__

            /* COMPILATION. Should it be with precompile() instead? */
            jl_method_instance_t* compiled_perform = jl_lookup_generic_and_compile_SC(perform_args, nargs);

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
            if(!compiled_perform)
                jl_error("Could not compile __perform__ function");

            //successful compilation...
            julia_object->perform_fun = perform_function;
            julia_object->perform_instance = compiled_perform;
        }

        inline void precompile_destructor(jl_module_t* evaluated_module, JuliaObject* julia_object)
        {
            jl_function_t* ugen_destructor = jl_get_function(evaluated_module, "__destructor__");
            if(!ugen_destructor)
                jl_error("Invalid __destructor__ function");
            
            /* COMPILATION */
            jl_method_instance_t* compiled_destructor = jl_lookup_generic_and_compile_SC(&ugen_destructor, 1);

            if(!compiled_destructor)
                jl_error("Could not compile __destructor__ function");

            julia_object->destructor_fun = ugen_destructor;
            julia_object->destructor_instance = compiled_destructor;
        }
};

#define JULIA_OBJECTS_ARRAY_INCREMENT 100

/* Allocate it with a unique_ptr? Or just a normal new/delete? */
class JuliaObjectsArray : public JuliaObjectCompiler, public JuliaAtomicBarrier, public JuliaEntriesCounter
{
    public:
        JuliaObjectsArray(World* in_world_, JuliaState* julia_global_)
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
            unsigned long new_id;
            JuliaObject* julia_object;
            
            if(get_active_entries() == num_total_entries)
            {
                //Lock the access to the array only when resizing.
                JuliaAtomicBarrier::Spinlock();

                resize_julia_objects_array();

                JuliaAtomicBarrier::Unlock();
            }

            //Retrieve a new ID be checking out the first free entry in the julia_objects_array
            for(unsigned long i = 0; i < num_total_entries; i++)
            {
                JuliaObject* this_julia_object = julia_objects_array + i;
                if(!this_julia_object) //If empty entry, it means I can take ownership
                {
                    julia_object = this_julia_object;
                    new_id = i;
                }
            }

            printf("ID: %lu\n", new_id);

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
                jl_call1(set_unique_id, jl_box_int64(new_id));

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
        inline bool get_julia_object(unsigned long unique_id, JuliaObject* julia_object)
        {
            bool barrier_acquired = JuliaAtomicBarrier::Checklock();

            if(barrier_acquired)
            {
                JuliaObject* this_julia_object = julia_objects_array + unique_id;

                if(this_julia_object)
                {
                    julia_object->constructor_instance = this_julia_object->constructor_instance;
                    julia_object->perform_instance     = this_julia_object->perform_instance;
                    julia_object->destructor_instance  = this_julia_object->destructor_instance;
                    julia_object->compiled             = this_julia_object->compiled;
                }
                else
                    printf("WARNING: Invalid julia_object\n");

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
            
            unload_julia_object(julia_object);

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

//WARNING: THIS METHOD DOESN'T SEEM TO WORK WITH MULTIPLE SERVERS ON MACOS. ON LINUX IT WORKS JUST FINE...
std::string get_julia_dir() 
{
    std::string result = "";

    //Get process id and convert it to string
    pid_t scsynth_pid = getpid();
    const char* scsynth_pid_string = (std::to_string(scsynth_pid)).c_str();

    //Print PID to check if multiple servers have it different...
    printf("PID: %i\n", scsynth_pid);

    //Set the scsynthPID enviromental variable, used in the JULIA_DIRECTORY_PATH bash script
    setenv("scsynthPID", scsynth_pid_string, 1);

    //run script and get a FILE pointer back to the result of the script (which is what's returned by printf in bash script)
    FILE* pipe = popen(JULIA_DIRECTORY_PATH, "r");
    
    if (!pipe) 
    {
        result = "ERROR: couldn't find Julia.";
        return "";
    }
    
    char buffer[128];
    while(!feof(pipe)) 
    {
        //get the text out line by line
        while(fgets(buffer, 128, pipe) != NULL)
            result += buffer;
    }

    pclose(pipe);

    return result;
}


#ifdef __linux__
void* handle;
#endif

jl_mutex_t* gc_mutex;

//false = free
//true = busy (either performing GC, or allocating an object)
std::atomic<bool> gc_allocation_state(false);
/* The mechanism is simple: when the RT thread allocates a new object, it checks if the state of
gc_allocation_state is set to true, which means that the NRT thread has set it to perform GC.
If the state is false, on the other hand, it means that the GC is not performing any collection, and thus
it is possible to allocate the new object. The RT thread, then, sets gc_allocation_state to true, preventing
the NRT thread from collecting while allocating a new object. The NRT thread will wait until the state has been
set back to false. If more objects are allocated within the same call, it doesn't affect the algorithm, as the
function calls are happening in the same thread one after the other. There is no risk that, on scsynth, an object
wouldn't be allocated becauase of another object being allocated. */
/* This wouldn't work with supernova, as multiple objects could be allocating at the same time
and thus, setting the gc_allocation_state to busy from one object, would prevent me to allocate another one
from another thread. A solution might be to have a:
struct gc_allocation_state
{
    std::atomic<bool> gc_busy;
    std::atomic<bool> object_busy;
}
gc_busy would be used only from NRT thread, while object_busy from any other audio thread.  
gc_busy will be used in objects to check if NRT thread is performing GC, and object_busy will be used only in
NRT thread to see if any object from any thread is allocating. This way, on any thread, I can just check on 
gc_busy, and set object_busy. Objects won't be looking if object_busy.load() = true, but only to gc_busy.load() = true*/

jl_function_t* sine_fun = nullptr;
jl_function_t* perform_fun = nullptr;
jl_function_t* id_dict_function = nullptr;
jl_value_t* global_id_dict = nullptr;
jl_function_t* set_index = nullptr;
jl_function_t* delete_index = nullptr;

jl_method_instance_t* method_instance_test = nullptr;
jl_function_t* dummy_lookup_function = nullptr;

jl_method_instance_t* method_instance_test_precompile = nullptr;
jl_function_t* dummy_lookup_function_precompile = nullptr;

bool julia_initialized = false; 
std::string julia_dir;
std::string julia_folder_structure = "julia/lib/julia";
std::string JuliaDSP_folder = "julia/JuliaDSP/";

/* GLOBAL VARS */
JuliaGCBarrier julia_gc_barrier;

#ifdef __linux__
//Loading libjulia directly. Opening it with RTLD_NOW | RTLD_GLOBAL allows for all symbols to be correctly found.
//In julia.h, #define JL_RTLD_DEFAULT (JL_RTLD_LAZY | JL_RTLD_DEEPBIND) is defined. I might just redefine the flags there?
inline void open_julia_shared_library()
{
    handle = dlopen("libjulia.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        fprintf (stderr, "%s\n", dlerror());
        printf("Could not find Julia. \n");
    }
}
#endif

inline bool test_include()
{
    bool include_completed;

    if(julia_initialized)
    {
        std::string sine_jl_path = julia_dir;
        sine_jl_path.append(JuliaDSP_folder);
        sine_jl_path.append("Sine_DSP.jl");

        JL_TRY {
            //DO I NEED TO ADVANCE AGE HERE???? Perhaps, I do.
            jl_get_ptls_states()->world_age = jl_get_world_counter();
            
            jl_module_t* loaded_module = (jl_module_t*)jl_load(jl_main_module, sine_jl_path.c_str());

            const char* module_name = jl_symbol_name(loaded_module->name);
            printf("MODULE NAME: %s\n", module_name);
            //jl_call1(jl_get_function(jl_main_module, "println"), (jl_value_t*)loaded_module);

            jl_exception_clear();

            include_completed = true;
        }
        JL_CATCH {
            jl_get_ptls_states()->previous_exception = jl_current_exception();

            /* These could just be global. And I could just use jl_method_instance_t* */
            jl_value_t* exception = jl_exception_occurred();
            jl_value_t* sprint_fun = jl_get_function(jl_base_module, "sprint");
            jl_value_t* showerror_fun = jl_get_function(jl_base_module, "showerror");

            if(exception)
            {
                const char* returned_exception = jl_string_ptr(jl_call2(sprint_fun, showerror_fun, exception));
                printf("ERROR: %s\n", returned_exception);
            }

            include_completed = false;
        }

        //Advance age after each include? YESSSS!! NEEDED OR CRASH WHEN precompiling()
        jl_get_ptls_states()->world_age = jl_get_world_counter();
    }

    return include_completed;
}

float* dummy_sc_alloc(int size_alloc)
{
    float* buffer = (float*)RTAlloc(global_world, size_alloc * sizeof(float));

    //Add an extra check here to see if buffer has been allocated correctly. If not, return something
    //that will still be read by Julia meaningfully

    memset(buffer, 0, size_alloc * sizeof(float));
    return buffer;
}


/* To be honest, it appears that jl_gc_enable() already works as a lock around the GC. 
In fact, by running old versions without the gc_allocation_state atomic, it still worked.
Nevertheless, the more checks, the better. 
Actually, by thinking on it, this atomic mechanism is required because while in the middle 
of this creation of objects, the GC could be enabled again by the NRT thread if there was a call
to the perform_gc() function. Now, the NRT thread will wait for 500ms to wait for the 
atomic "false" on gc_allocation_state, which is set at the end of object creation if it was previously
set to true. Check the constructor in Julia.cpp
/SHOULD I ALSO HAVE THIS MECHANISM FOR THE OTHER CALLS IN THE NRT? I don't think so, since they happen
on the same thread, and thus they are scheduled (Meaning, that no function on the NRT thread will ever be called
in between the jl_enable(1) and jl_enable(0)). Maybe I can just add a jl_gc_is_enabled() check */
inline void perform_gc(int full)
{
    julia_gc_barrier.NRTPerformGC(full);
}

inline void boot_julia(World* inWorld)
{
    if(!jl_is_initialized())
    {
        //get path to the Julia.scx and julia lib and includes.
        //doing "const char* julia_dir = get_julia_dir().c_str()"" is buggy because, when the std::string
        //gets popped out of the stack, also its const char* buffer (the one I get with .c_str()) gets.
        //Calling c_str() everytime is ugly but it works better here.
        julia_dir = get_julia_dir();
        
        std::string julia_image_folder = julia_dir;
        julia_image_folder.append(julia_folder_structure);

        printf("Path to Julia object and lib: \n%s\n", julia_image_folder.c_str());
        
        if(julia_image_folder.c_str())
        {
        #ifdef __APPLE__
            jl_init_with_image_SC(julia_image_folder.c_str(), "sys.dylib", inWorld, ft);
        #elif __linux__
            jl_init_with_image_SC(julia_image_folder.c_str(), "sys.so", inWorld, ft);
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

            //PERFORM GC AT START TO RESET STATE:
            perform_gc(1);

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
            julia_initialized = true;
        }
    }
    else if(jl_is_initialized())
        printf("*** Julia %s already booted ***\n", jl_ver_string());
    else
        printf("WARNING: Couldn't boot Julia \n");
}

bool boot2(World* world, void* cmd)
{
    return true;
}

bool boot3(World* world, void* cmd)
{
    boot_julia(world);
    return true;
}

bool boot4(World* world, void* cmd)
{
    return true;
}

void bootCleanup(World* world, void* cmd){}

void JuliaBoot(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_boot", (void*)nullptr, (AsyncStageFn)boot2, (AsyncStageFn)boot3, (AsyncStageFn)boot4, bootCleanup, 0, nullptr);
}

struct ArgumentTestData
{
    const char* string_argument;
};

bool argumentTest2(World* world, void* cmd)
{
    const char* string_argument = ((ArgumentTestData*)cmd)->string_argument;
    printf("ARGUMENT: %s\n", string_argument);
    return true;
}

bool argumentTest3(World* world, void* cmd)
{
    return true;
}

bool argumentTest4(World* world, void* cmd)
{
    //printf("FINISHED\n");
    return true;
}

void argumentTestCleanup(World* world, void* cmd)
{
    ArgumentTestData* arguments = (ArgumentTestData*)cmd;
    RTFree(world, arguments);
}

void JuliaArgumentTest(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    ArgumentTestData* arguments = (ArgumentTestData*)RTAlloc(inWorld, sizeof(ArgumentTestData));
    arguments->string_argument = args->gets();
    DoAsynchronousCommand(inWorld, replyAddr, "jl_argumentTest", (void*)arguments, (AsyncStageFn)argumentTest2, (AsyncStageFn)argumentTest3, (AsyncStageFn)argumentTest4, argumentTestCleanup, 0, nullptr);
}

bool checkWorldAndFt2(World* world, void* cmd)
{
    return true;
}

bool checkWorldAndFt3(World* world, void* cmd)
{
    jl_check_SC_world_and_ft(world, ft);
    return true;
}

bool checkWorldAndFt4(World* world, void* cmd)
{
    return true;
}

void checkWorldAndFtCleanup(World* world, void* cmd){}

void JuliaCheckWorldAndFt(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_checkWorldAndFt", (void*)nullptr, (AsyncStageFn)checkWorldAndFt2, (AsyncStageFn)checkWorldAndFt3, (AsyncStageFn)checkWorldAndFt4, checkWorldAndFtCleanup, 0, nullptr);
}

bool APIAlloc2(World* world, void* cmd)
{
    printf("NRT thread \n");
    jl_SC_alloc(0, 10);
    return true;
}

bool APIAlloc3(World* world, void* cmd)
{
    printf("RT thread: \n");
    jl_SC_alloc(1, 10);
    return true;
}

bool APIAlloc4(World* world, void* cmd)
{
    return true;
}

void APIAllocCleanup(World* world, void* cmd){}

void JuliaAPIAlloc(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_APIAlloc", (void*)nullptr, (AsyncStageFn)APIAlloc2, (AsyncStageFn)APIAlloc3, (AsyncStageFn)APIAlloc4, APIAllocCleanup, 0, nullptr);
}

bool PosixMemalign2(World* world, void* cmd)
{
    jl_SC_posix_memalign((size_t)64, (size_t)88200);
    return true;
}

bool PosixMemalign3(World* world, void* cmd)
{
    return true;
}

bool PosixMemalign4(World* world, void* cmd)
{
    return true;
}

void PosixMemalignCleanup(World* world, void* cmd){}

void JuliaPosixMemalign(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_PosixMemalign", (void*)nullptr, (AsyncStageFn)PosixMemalign2, (AsyncStageFn)PosixMemalign3, (AsyncStageFn)PosixMemalign4, PosixMemalignCleanup, 0, nullptr);
}

bool TestAlloc_include2(World* world, void* cmd)
{
    if(julia_initialized)
    {
        std::string sine_jl_path = julia_dir;
        sine_jl_path.append(JuliaDSP_folder);
        sine_jl_path.append("TestAlloc.jl");

        jl_function_t* include_function = jl_get_function(jl_base_module, "include");
        jl_call2(include_function, (jl_value_t*)jl_main_module, jl_cstr_to_string(sine_jl_path.c_str()));
    }
    return true;
}

bool TestAlloc_include3(World* world, void* cmd)
{
    return true;
}

bool TestAlloc_include4(World* world, void* cmd)
{
    return true;
}

void TestAlloc_includeCleanup(World* world, void* cmd){}

void JuliaTestAllocInclude(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_TestAlloc_include", (void*)nullptr, (AsyncStageFn)TestAlloc_include2, (AsyncStageFn)TestAlloc_include3, (AsyncStageFn)TestAlloc_include4, TestAlloc_includeCleanup, 0, nullptr);
}

bool TestAlloc_perform2(World* world, void* cmd)
{
    JL_TRY {
        jl_function_t* dict_int32 = jl_eval_string("Dict{Int32, Int32}");
        jl_call1(jl_get_function(jl_main_module, "println"), (jl_value_t*)dict_int32);

        jl_module_t* invalid_module = jl_get_module_in_main("TestAlloc");

        if(!invalid_module)
            jl_error("Invalid module");

        jl_function_t* invalid_fun = jl_get_function(invalid_module, "non_existing_function");
        
        jl_method_instance_t* invalid_method_instance = jl_lookup_generic_and_compile_SC(&invalid_fun, 1);

        if(!invalid_method_instance)
            jl_error("Invalid method instance");
        
        jl_function_t* invalid_var = jl_get_global_SC(invalid_module, "non_existing_var");

        if(!invalid_var)
            jl_error("Invalid variable");


        if(!invalid_fun)
            jl_error("Invalid function");

        jl_exception_clear();
    }
    JL_CATCH {
        jl_get_ptls_states()->previous_exception = jl_current_exception();

        jl_value_t* exception = jl_exception_occurred();
        jl_value_t* sprint_fun = jl_get_function(jl_base_module, "sprint");
        jl_value_t* showerror_fun = jl_get_function(jl_base_module, "showerror");

        if(exception)
        {
            const char* returned_exception = jl_string_ptr(jl_call2(sprint_fun, showerror_fun, exception));
            printf("ERROR: %s\n", returned_exception);
        }
    }
    
    return true;
}

bool TestAlloc_perform3(World* world, void* cmd)
{
    //Test if a = zeros(44100) and the allocation of an array would stop audio thread with 
    //GC allocating in the SC RT allocator.    
    //jl_function_t* alloc_fun = jl_get_function(jl_get_module_in_main("TestAlloc"), "test1");
    //jl_call0(alloc_fun);
    return true;
}

bool TestAlloc_perform4(World* world, void* cmd)
{
    return true;
}

void TestAlloc_performCleanup(World* world, void* cmd){}

void JuliaTestAllocPerform(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_TestAlloc_perform", (void*)nullptr, (AsyncStageFn)TestAlloc_perform2, (AsyncStageFn)TestAlloc_perform3, (AsyncStageFn)TestAlloc_perform4, TestAlloc_performCleanup, 0, nullptr);
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
    //The test_include() function already updates world age.
    bool include_completed = test_include();

    if(include_completed)
    {
        sine_fun = jl_get_function(jl_get_module_in_main("Sine_DSP"), "Sine");
        perform_fun = jl_get_function(jl_get_module_in_main("Sine_DSP"), "perform");

        jl_call3(set_index, global_id_dict, (jl_value_t*)sine_fun, (jl_value_t*)sine_fun);
        jl_call3(set_index, global_id_dict, (jl_value_t*)perform_fun, (jl_value_t*)perform_fun);

        precompile_object(world);

        printf("-> Julia: Include completed\n");
    }
    else
        printf("ERROR: Julia could not complete object precompilation \n");    

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
    return true;
}

void includeCleanup(World* world, void* cmd){}

void JuliaInclude(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_include", (void*)nullptr, (AsyncStageFn)include2, (AsyncStageFn)include3, (AsyncStageFn)include4, includeCleanup, 0, nullptr);
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
    DoAsynchronousCommand(inWorld, replyAddr, "jl_alloc", (void*)nullptr, (AsyncStageFn)alloc2, (AsyncStageFn)alloc3, (AsyncStageFn)alloc4, allocCleanup, 0, nullptr);
}

bool gc2(World* world, void* cmd)
{
    printf("*** BEFORE GC: \n");
    printf("GC allocd: %lli\n", (jl_gc_allocd_SC() / 1000000));
    printf("GC total_allcod %lli\n", (jl_gc_total_allocd_SC() / 1000000));
    printf("GC deferred alloc %lli\n", jl_gc_deferred_alloc_SC() / 1000000);
    printf("GC interval %lli\n", jl_gc_interval_SC() / 1000000);

    /* Problem with crash here is that, if exception is raised when RTAllocating and memory is not allocated, Julia doesn't
       actually know about that. And it would still try to free the pointer. Gotta protect the behaviour in some way. Maybe
       having the exception in the allocation code in Julia itself, so that pointers don't get allocated there??? */
    //try
    //{
        //perform_gc(0) causes a crash with exception. Check what that is about...
        perform_gc(1);
    //}
    //catch (std::exception& exc) 
    //{
    //    gc_allocation_state = false; 
    //    printf("RT Alloc exception: %s\n", exc.what());
    //    return true;
    //}

    printf("*** AFTER GC: \n");
    printf("GC allocd: %lli\n", (jl_gc_allocd_SC() / 1000000));
    printf("GC total_allcod %lli\n", (jl_gc_total_allocd_SC() / 1000000));
    printf("GC deferred alloc %lli\n", jl_gc_deferred_alloc_SC() / 1000000);
    printf("GC interval %lli\n", jl_gc_interval_SC() / 1000000);

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
    DoAsynchronousCommand(inWorld, replyAddr, "jl_gc", (void*)nullptr, (AsyncStageFn)gc2, (AsyncStageFn)gc3, (AsyncStageFn)gc4, gcCleanup, 0, nullptr);
}

bool testJuliaAlloc2(World* world, void* cmd)
{
    //try
    //{
        printf("*** BEFORE ALLOC: \n");
        printf("GC allocd: %lli\n", (jl_gc_allocd_SC() / 1000000));
        printf("GC total_allcod %lli\n", (jl_gc_total_allocd_SC() / 1000000));
        printf("GC deferred alloc %lli\n", jl_gc_deferred_alloc_SC() / 1000000);
        printf("GC interval %lli\n", jl_gc_interval_SC() / 1000000);

        jl_call0(jl_get_function(jl_get_module_in_main("Sine_DSP"), "dummy_alloc"));

        printf("*** AFTER ALLOC: \n");
        printf("GC allocd: %lli\n", (jl_gc_allocd_SC() / 1000000));
        printf("GC total_allcod %lli\n", (jl_gc_total_allocd_SC() / 1000000));
        printf("GC deferred alloc %lli\n", jl_gc_deferred_alloc_SC() / 1000000);
        printf("GC interval %lli\n", jl_gc_interval_SC() / 1000000);

        //INTERVAL (~22mb) is the limit after the GC collects
        //GC_ALLOCD (negative value from -22 up to 0) is the memory being allocated and not freed. It is modulod against the interval.
        //GC_DEFERRED is an accumulator for the GC_ALLOCD memory which surpassed the INTERVAL. It accumulates over and over.
        printf("*** Actual allocated memory: %lli\n", (jl_gc_interval_SC() + jl_gc_allocd_SC() + jl_gc_deferred_alloc_SC()) / 1000000);
    //}
    //catch (std::exception& exc) 
    //{
    //    gc_allocation_state = false; 
    //    printf("RT Alloc exception: %s\n", exc.what());
    //    return true;
    //}

    return true;
}

void testJuliaCleanup(World* world, void* cmd){}

void JuliaTestJuliaAlloc(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_testJuliaAlloc", (void*)nullptr, (AsyncStageFn)testJuliaAlloc2, 0, 0, testJuliaCleanup, 0, nullptr);
}


bool testLookup(World* world, void* cmd)
{
    JL_TRY {
        dummy_lookup_function = jl_get_function(jl_get_module_in_main("Sine_DSP"), "dummy_alloc");

        method_instance_test = jl_lookup_generic_and_compile_SC(&dummy_lookup_function, 1);

        //make the MethodInstance (this it the Julia type for a jl_method_instance_t) global, setting it in the GlobalIdDict
        jl_call3(set_index, global_id_dict, (jl_value_t*)method_instance_test, (jl_value_t*)method_instance_test);
        
        printf("Lookup completed\n");
        
        jl_exception_clear();
    }
    JL_CATCH {
        jl_get_ptls_states()->previous_exception = jl_current_exception();

        /* These could just be global. And I could just use jl_method_instance_t* */
        jl_value_t* exception = jl_exception_occurred();
        jl_value_t* sprint_fun = jl_get_function(jl_base_module, "sprint");
        jl_value_t* showerror_fun = jl_get_function(jl_base_module, "showerror");

        if(exception)
        {
            const char* returned_exception = jl_string_ptr(jl_call2(sprint_fun, showerror_fun, exception));
            printf("ERROR: %s\n", returned_exception);
        }
    }

    return true;
}

void testLookupCleanup(World* world, void* cmd){}

void JuliaTestLookup(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_testLookup", (void*)nullptr, (AsyncStageFn)testLookup, 0, 0, testLookupCleanup, 0, nullptr);
}

//CALLED ON RT THREAD
bool testInvoke(World* world, void* cmd)
{
    //Use the already retrieved and precompiled method_instance_test to run...
    jl_invoke_already_compiled_SC(method_instance_test, &dummy_lookup_function, 1);

    return true;
}

void testInvokeCleanup(World* world, void* cmd){}

void JuliaTestInvoke(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_testInvoke", (void*)nullptr, 0, (AsyncStageFn)testInvoke, 0, testInvokeCleanup, 0, nullptr);
}

/* IS this better than lookup methods??? */
bool testLookupPrecompile(World* world, void* cmd)
{
    JL_TRY {
        dummy_lookup_function_precompile = jl_get_function(jl_get_module_in_main("Sine_DSP"), "dummy_alloc");
        
        //tuple of arguments. need typeof() for function
        jl_svec_t* args_svec = jl_svec(1, (jl_value_t*)jl_typeof(dummy_lookup_function_precompile)); //empty tuple. just function as first element
        jl_tupletype_t* args_tuple = jl_apply_tuple_type(args_svec);

        jl_call1(jl_get_function(jl_main_module, "println"), (jl_value_t*)args_tuple);

        /* both calls are in the same jl_world_counter, since just called from NRT thread */
        if(jl_compile_hint_SC(args_tuple))
        {
            printf("PRECOMPILED!! \n");
            method_instance_test_precompile = jl_get_specialization1_SC(args_tuple, jl_get_world_counter(), 1);

            /* precompile() won't compile the whole function's functions (like calls to anything else, like printf in dummy_alloc()).
            Need to run it once to do that. */
            jl_invoke(method_instance_test_precompile, &dummy_lookup_function_precompile, 1);
        }

        jl_exception_clear();
    }
    JL_CATCH {
        jl_get_ptls_states()->previous_exception = jl_current_exception();

        /* These could just be global. And I could just use jl_method_instance_t* */
        jl_value_t* exception = jl_exception_occurred();
        jl_value_t* sprint_fun = jl_get_function(jl_base_module, "sprint");
        jl_value_t* showerror_fun = jl_get_function(jl_base_module, "showerror");

        if(exception)
        {
            const char* returned_exception = jl_string_ptr(jl_call2(sprint_fun, showerror_fun, exception));
            printf("ERROR: %s\n", returned_exception);
        }
    }

    return true;
}

void testLookupCleanupPrecompile(World* world, void* cmd){}

void JuliaTestLookupPrecompile(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_testLookupPrecompile", (void*)nullptr, (AsyncStageFn)testLookupPrecompile, 0, 0, testLookupCleanupPrecompile, 0, nullptr);
}

//CALLED ON RT THREAD
bool testInvokePrecompile(World* world, void* cmd)
{
    return true;
}

void testInvokeCleanupPrecompile(World* world, void* cmd){}

void JuliaTestInvokePrecompile(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    DoAsynchronousCommand(inWorld, replyAddr, "jl_testInvokePrecompile", (void*)nullptr, 0, (AsyncStageFn)testInvokePrecompile, 0, testInvokeCleanupPrecompile, 0, nullptr);
}

bool TestOuts2(World* world, void* cmd)
{
    long num_chans = 32;
    long buf_size = 512;
    
    /* Just like ins/outs of UGen. Memory is not contiguous */
    float** outs_sc = (float**)RTAlloc(world, sizeof(float*) * num_chans);
    float** outs_julia = (float**)RTAlloc(world, sizeof(float*) * num_chans);
    float** outs_julia_ptr = (float**)RTAlloc(world, sizeof(float*) * num_chans);
    for(int i = 0; i < num_chans; i++)
    {
        outs_sc[i] = (float*)RTAlloc(world, sizeof(float) * buf_size);
        outs_julia[i] = (float*)RTAlloc(world, sizeof(float) * buf_size);
        outs_julia_ptr[i] = (float*)RTAlloc(world, sizeof(float) * buf_size);
    }


    for(int i = 0; i < num_chans; i++)
    {
        for(int y = 0; y < buf_size; y++)
        {
            outs_julia[i][y] = 1.0f;
            outs_julia_ptr[i][y] = 1.0f;
        }
    }

    /*
    printf("SC: \n");

    for(int i = 0; i < num_chans; i++)
    {
        for(int y = 0; y < buf_size; y++)
        {
            outs_sc[i][y] = (float)y;
            printf("SC[%i, %i] = %f\n", i, y, outs_sc[i][y]);
        }
    }
    
    printf("JULIA: \n");
    */

    jl_value_t* num_chans_julia = jl_box_int64(num_chans);
    jl_value_t* buf_size_julia = jl_box_int64(buf_size);

    //Vector{Float32}
    jl_value_t* vector_float32 = jl_apply_array_type((jl_value_t*)jl_float32_type, 1);
    
    //Vector{Vector{Float32}} == Array{Array{Float32, 1}, 1} of size = num_chans
    jl_value_t* vector_of_vectors_float32 = jl_apply_array_type(vector_float32, 1);
    jl_value_t* outs_julia_vector = (jl_value_t*)jl_alloc_array_1d(vector_of_vectors_float32, num_chans);

    //jl_value_t* outs_julia_vector = (jl_value_t*)jl_ptr_to_array_1d(vector_of_vectors_float32, outs_julia, num_chans, 0);

    //1D Array for each output channel
    jl_value_t** outs_julia_vector_1d = (jl_value_t**)RTAlloc(world, sizeof(jl_value_t*) * num_chans);

    //In constructor: create ptrs of nullptr of buf_size, and assign them to the entries of outs::Vector{Vector{Float32}}
    for(int i = 0; i < num_chans; i++)
    {
        outs_julia_vector_1d[i] = (jl_value_t*)jl_ptr_to_array_1d(vector_float32, nullptr, buf_size, 0);
        jl_call3(set_index, outs_julia_vector, outs_julia_vector_1d[i], jl_box_int32(i + 1)); //Julia index from 1 onwards
    }

    //Then, in dsp loop, change the data the jl_ptr_to_array are pointing at, without allocating new ones:
    for(int i = 0; i < num_chans; i++)
        ((jl_array_t*)(outs_julia_vector_1d[i]))->data = outs_julia[i];

    jl_function_t* test_outs_fun = jl_get_function(jl_get_module_in_main("Sine_DSP"), "test_outs");
    jl_call3(test_outs_fun, num_chans_julia, buf_size_julia, outs_julia_vector);


    /* Ptr{Ptr{Float32}} solution */
    jl_value_t* ptr_ptr_float32 = jl_eval_string("Ptr{Ptr{Float32}}");
    jl_value_t* ptr_to_outs_julia = jl_call1(ptr_ptr_float32, jl_box_voidpointer(outs_julia_ptr));

    jl_function_t* test_outs_ptr_fun = jl_get_function(jl_get_module_in_main("Sine_DSP"), "test_outs_ptr");
    jl_call3(test_outs_ptr_fun, num_chans_julia, buf_size_julia, ptr_to_outs_julia);


    /* BENCHMARKS */
    clock_t begin_vec, end_vec, begin_ptr, end_ptr; // time_t is a datatype to store time values.
    
    begin_vec = clock(); // note time before execution
    for(long i = 0; i < 100000; i++)
    {   
        for(int y = 0; y < num_chans; y++)
            ((jl_array_t*)(outs_julia_vector_1d[y]))->data = outs_julia[y];
        jl_call3(test_outs_fun, num_chans_julia, buf_size_julia, outs_julia_vector);
    }
    end_vec = clock(); // note time after execution
    double difference_vec = double(end_vec - begin_vec)/CLOCKS_PER_SEC;

    begin_ptr = clock(); // note time before execution
    for(long i = 0; i < 100000; i++)
        jl_call3(test_outs_ptr_fun, num_chans_julia, buf_size_julia, ptr_to_outs_julia);
    end_ptr = clock(); // note time after execution
    double difference_ptr = double(end_ptr - begin_ptr)/CLOCKS_PER_SEC;

    printf("TIME VEC: %f seconds\n", difference_vec);
    printf("TIME PTR: %f seconds\n", difference_ptr);

    /* PRINT OUT
    for(int i = 0; i < num_chans; i++)
    {
        for(int y = 0; y < buf_size; y++)
            printf("JULIA[%i, %i] = %f\n", i, y, outs_julia[i][y]);
    } 

    for(int i = 0; i < num_chans; i++)
    {
        for(int y = 0; y < buf_size; y++)
            printf("JULIA_PTR[%i, %i] = %f\n", i, y, outs_julia_ptr[i][y]);
    } 
    */

    /* FREE MEMORY */
    for(int i = 0; i < num_chans; i++)
    {
        RTFree(world, outs_sc[i]);
        RTFree(world, outs_julia[i]);
        RTFree(world, outs_julia_ptr[i]);
    }

    RTFree(world, outs_sc);
    RTFree(world, outs_julia);
    RTFree(world, outs_julia_ptr);
    RTFree(world, outs_julia_vector_1d);

    return true;
}

void TestOutsCleanup(World* world, void* cmd) {}

void JuliaTestOuts(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{

    DoAsynchronousCommand(inWorld, replyAddr, "jl_test_outs", nullptr, (AsyncStageFn)TestOuts2, 0, 0, TestOutsCleanup, 0, nullptr);
}

inline void DefineJuliaCmds()
{
    DefinePlugInCmd("/julia_boot", (PlugInCmdFunc)JuliaBoot, nullptr);
    DefinePlugInCmd("/julia_argumentTest", (PlugInCmdFunc)JuliaArgumentTest, nullptr);
    DefinePlugInCmd("/julia_checkWorldAndFt", (PlugInCmdFunc)JuliaCheckWorldAndFt, nullptr);
    DefinePlugInCmd("/julia_API_alloc", (PlugInCmdFunc)JuliaAPIAlloc, nullptr);
    DefinePlugInCmd("/julia_posix_memalign", (PlugInCmdFunc)JuliaPosixMemalign, nullptr);
    DefinePlugInCmd("/julia_TestAlloc_include", (PlugInCmdFunc)JuliaTestAllocInclude, nullptr);
    DefinePlugInCmd("/julia_TestAlloc_perform", (PlugInCmdFunc)JuliaTestAllocPerform, nullptr);
    DefinePlugInCmd("/julia_GC", (PlugInCmdFunc)JuliaGC, nullptr);
    DefinePlugInCmd("/julia_testJuliaAlloc", (PlugInCmdFunc)JuliaTestJuliaAlloc, nullptr);
    DefinePlugInCmd("/julia_test_lookup", (PlugInCmdFunc)JuliaTestLookup, nullptr);
    DefinePlugInCmd("/julia_test_lookup_precompile", (PlugInCmdFunc)JuliaTestLookupPrecompile, nullptr);
    DefinePlugInCmd("/julia_test_invoke", (PlugInCmdFunc)JuliaTestInvoke, nullptr);
    DefinePlugInCmd("/julia_test_invoke_precompile", (PlugInCmdFunc)JuliaTestInvokePrecompile, nullptr);
    DefinePlugInCmd("/julia_include", (PlugInCmdFunc)JuliaInclude, nullptr);
    DefinePlugInCmd("/julia_alloc", (PlugInCmdFunc)JuliaAlloc, nullptr);
    DefinePlugInCmd("/julia_test_outs", (PlugInCmdFunc)JuliaTestOuts, nullptr);
}