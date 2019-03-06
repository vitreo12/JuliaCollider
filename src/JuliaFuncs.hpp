#include "JuliaHash.hpp"
#include "JuliaUtilities.hpp"
#include "SC_PlugIn.hpp"
#include "SC_Node.h"
#include "scsynthsend.h"
#include "SC_ReplyImpl.hpp"
#include <string>
#include <atomic>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

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

static InterfaceTable *ft;
World* global_world;

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
#define JULIA_OBJECTS_NUM_ENTRIES 100

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

typedef struct JuliaGlobalUtilities
{
    /* Global objects */
    jl_value_t* sc_synth;
    jl_value_t* global_id_dict;
    /* Utilities functions */
    jl_function_t* set_index;
    jl_function_t* delete_index;
    jl_function_t* sprint_fun;
    jl_function_t* showerror_fun;
    /* Datatypes */
    jl_value_t* vector_float32;
    jl_value_t* vector_of_vectors_float32;
    jl_value_t* ptr_ptr_float32;
} JuliaGlobalUtilities;

typedef struct JuliaObject
{
    jl_method_instance_t* constructor;
    jl_method_instance_t* perform;
    jl_method_instance_t* destructor;
    bool compiled;
    //bool RT_busy;
} JuliaObject;

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
        unsigned long active_entries;
};

/* eval and compile an individual JuliaObject in NRT thread. Using just one args and changing 
args[0] for each function call, as only one thread (the NRT one) at a time will call into this*/
class JuliaObjectCompiler
{
    public:
        JuliaObjectCompiler()
        {
            init_perform_args();
        }

        ~JuliaObjectCompiler()
        {
            destroy_perform_args();
        }

        inline jl_module_t* compile_julia_object(JuliaObject* julia_object, const char* path, JuliaReply* julia_reply)
        {
            jl_module_t* evaluated_module = eval_julia_file(path);

            if(evaluated_module)
            {
                if(precompile_julia_object(evaluated_module))
                {
                    
                } 
            }

            if(!julia_object)
                printf("WARNING: Failed in compiling JuliaObject \n");

            return evaluated_module;
        }

        inline void unload_julia_object(JuliaObject* julia_object)
        {
            if(julia_object->compiled)
            {
                //Remove from GlobalIdDict and set memory back to 0s

                //Reset memory pointer for this object
                memset(julia_object, 0, sizeof(JuliaObject));
            }
        }
    
    private:
        /* EVAL FILE */
        inline jl_module_t* eval_julia_file(const char* path)
        {
            jl_module_t* evaluated_module;
            
            JL_TRY {
                //DO I NEED TO ADVANCE AGE HERE???? Perhaps, I do.
                jl_get_ptls_states()->world_age = jl_get_world_counter();
                
                //The file MUST ONLY contain an @object definition (which loads a module)
                jl_module_t* evaluated_module = (jl_module_t*)jl_load(jl_main_module, path);

                if(!jl_is_module(evaluated_module))
                    jl_error("Included file is not a Julia module\n");
                
                //Try to retrieve something like __inputs__. If not, it's not a @object genereated module
                if(!jl_get_global(evaluated_module, jl_symbol("__inputs__")))
                    jl_error("Included file is not a Julia @object\n");

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

                evaluated_module = nullptr;
            }

            //Advance age after each include
            jl_get_ptls_states()->world_age = jl_get_world_counter();

            return evaluated_module;
        };

        /* PRECOMPILATION */
        jl_value_t** perform_args;

        inline void init_perform_args()
        {
            perform_args = (jl_value_t**)calloc(4, sizeof(jl_value_t*));
            //perform_args[0] is the individual object out of __constructor__()
            //perform_args[1] = ; //__ins__
            //perform_args[2] = ; //__outs__
            //perform_args[3] = ; //__SCSynth__
        }

        inline void destroy_perform_args()
        {
            free(perform_args);
        }

        inline bool precompile_julia_object(jl_module_t* module)
        {
            bool precompile_state;

            //Add to global_id_dict
            
            return precompile_state;
        }
};

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

#define JULIA_OBJECTS_ARRAY_INCREMENT 100

/* Allocate it with a unique_ptr? Or just a normal new/delete? */
class JuliaObjectsArray : public JuliaObjectCompiler, public JuliaAtomicBarrier, public JuliaEntriesCounter
{
    public:
        JuliaObjectsArray(World* in_world_, JuliaGlobalUtilities* julia_global_)
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

            //Check if compilation went through. If it did, increase counter of active entries and formulate correct JuliaReply
            if(julia_object->compiled)
            {
                advance_active_entries();

                const char* name = jl_symbol_name(evaluated_module->name);
                long inputs = jl_unbox_int64(jl_get_global(evaluated_module, jl_symbol("__inputs__")));
                long outputs = jl_unbox_int64(jl_get_global(evaluated_module, jl_symbol("__outputs__")));

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
        unsigned long num_entries = JULIA_OBJECTS_NUM_ENTRIES;

        //Constructor
        inline void init_julia_objects_array()
        {
            //RTalloc?
            JuliaObject* res = (JuliaObject*)calloc(num_entries, sizeof(JuliaObject));
            
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
            unsigned long previous_num_entries = num_entries;

            //Add more entries
            advance_num_entries();

            //Manual realloc()... more control on the different stages
            JuliaObject* res = (JuliaObject*)calloc(num_entries, sizeof(JuliaObject));

            //If failed, reset num entries to before and exit.
            if(!res)
            {
                decrease_num_entries();
                printf("Failed to allocate more memory for JuliaObjects class \n");
                return; //julia_objects_array is still valid
            }

            //copy previous entries to new array
            memcpy(res, julia_objects_array, previous_num_entries);
            
            //previous array to be freed
            JuliaObject* previous_julia_objects_array = julia_objects_array; 

            //swap pointers. No need for atomic, since NRT_Lock has acquired already
            julia_objects_array = res;

            //free previous array.
            free(previous_julia_objects_array);
        }

        inline void advance_num_entries()
        {
            num_entries += JULIA_OBJECTS_NUM_ENTRIES;
        }

        inline void decrease_num_entries()
        {
            num_entries -= JULIA_OBJECTS_NUM_ENTRIES;
            if(num_entries < 0)
                num_entries = 0;
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
JuliaAtomicBarrier JuliaGCBarrier;

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
    printf("GC gc_allocation_state BEFORE: %i\n", JuliaGCBarrier.get_barrier_value());

    JuliaGCBarrier.Spinlock();

    printf("GC gc_allocation_state MIDDLE: %i\n", JuliaGCBarrier.get_barrier_value());

    if(!jl_gc_is_enabled())
    {
        printf("-> Enabling GC...\n");
        jl_gc_enable(1);
    }
    printf("-> Performing GC...\n");
    jl_gc_collect(full);
    printf("-> Completed GC\n");
    if(jl_gc_is_enabled())
    {
        printf("-> Disabling GC...\n");
        jl_gc_enable(0);
    }

    //Reset gc_allocation_state to false, "free". Assignment operation is atomic. (same as gc_allocation_state.store(false))
    JuliaGCBarrier.Unlock();
    
    printf("GC gc_allocation_state AFTER: %i\n", JuliaGCBarrier.get_barrier_value());
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
    jl_function_t* alloc_fun = jl_get_function(jl_get_module("TestAlloc"), "test2");
    jl_call0(alloc_fun);
    return true;
}

bool TestAlloc_perform3(World* world, void* cmd)
{
    //Test if a = zeros(44100) and the allocation of an array would stop audio thread with 
    //GC allocating in the SC RT allocator.    
    jl_function_t* alloc_fun = jl_get_function(jl_get_module("TestAlloc"), "test1");
    jl_call0(alloc_fun);
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
        sine_fun = jl_get_function(jl_get_module("Sine_DSP"), "Sine");
        perform_fun = jl_get_function(jl_get_module("Sine_DSP"), "perform");

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

        jl_call0(jl_get_function(jl_get_module("Sine_DSP"), "dummy_alloc"));

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
    dummy_lookup_function = jl_get_function(jl_get_module("Sine_DSP"), "dummy_alloc");

    method_instance_test = jl_lookup_generic_and_compile_SC(&dummy_lookup_function, 1);

    //make the MethodInstance (this it the Julia type for a jl_method_instance_t) global, setting it in the GlobalIdDict
    jl_call3(set_index, global_id_dict, (jl_value_t*)method_instance_test, (jl_value_t*)method_instance_test);
    
    printf("Lookup completed\n");

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

ReplyAddress* server_reply_address;

struct MyCmdData // data for each command
{
	ReplyAddress* reply_addr;
};

void send_osc_to_sclang(ReplyAddress* replyAddr)
{
    small_scpacket packet;
	packet.adds("/Julia_IO");
	packet.maketags(4);
	packet.addtag(',');
	packet.addtag('s');
	packet.adds("/Sine"); //name representing symbol of the compiled Julia program...
	packet.addtag('i');
	packet.addi(1); //number of inputs here...
    packet.addtag('i');
	packet.addi(1); //number of outputs here...

	SendReply(replyAddr, packet.data(), packet.size());
}

bool sendReply2(World* world, void* cmd)
{
    //MyCmdData* myCmdData = (MyCmdData*)cmd;
    
    printf("-> Request received by server...\n");
    send_osc_to_sclang(server_reply_address);
    printf("-> Data sent by server to sclang...\n");
    return true;
}

//RT thread
bool sendReply3(World* world, void* cmd)
{
    return true;
}

bool sendReply4(World* world, void* cmd)
{
    return true;
}

void sendReplyCleanup(World* world, void* cmd)
{
    MyCmdData* myCmdData = (MyCmdData*)cmd;
    RTFree(world, myCmdData);
}

//DON'T KNOW WHY if using myCmdData->replyAddr it crashes for SendReply()
//logs says that it is a problem with:
//boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::asio::ip::bad_address_cast> >: bad address cast
//The problem is that probably, if sending a lot of requests, the RTAlloc is not called fast enough to allocate them all and
//then, some of them, would be pointing at junk memory instead of the actual replyAddr
void JuliaSendReply(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
	ReplyAddress* replyAddrCast = (ReplyAddress*)replyAddr;
    
    MyCmdData* myCmdData = (MyCmdData*)RTAlloc(inWorld, sizeof(MyCmdData));
    myCmdData->reply_addr = replyAddrCast;

    if(replyAddrCast != server_reply_address)
        server_reply_address = replyAddrCast;

    DoAsynchronousCommand(inWorld, replyAddr, "jl_send_reply", (void*)myCmdData, (AsyncStageFn)sendReply2, (AsyncStageFn)sendReply3, (AsyncStageFn)sendReply4, sendReplyCleanup, 0, nullptr);
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

    jl_function_t* test_outs_fun = jl_get_function(jl_get_module("Sine_DSP"), "test_outs");
    jl_call3(test_outs_fun, num_chans_julia, buf_size_julia, outs_julia_vector);


    /* Ptr{Ptr{Float32}} solution */
    jl_value_t* ptr_ptr_float32 = jl_eval_string("Ptr{Ptr{Float32}}");
    jl_value_t* ptr_to_outs_julia = jl_call1(ptr_ptr_float32, jl_box_voidpointer(outs_julia_ptr));

    jl_function_t* test_outs_ptr_fun = jl_get_function(jl_get_module("Sine_DSP"), "test_outs_ptr");
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
    DefinePlugInCmd("/julia_test_invoke", (PlugInCmdFunc)JuliaTestInvoke, nullptr);
    DefinePlugInCmd("/julia_include", (PlugInCmdFunc)JuliaInclude, nullptr);
    DefinePlugInCmd("/julia_alloc", (PlugInCmdFunc)JuliaAlloc, nullptr);
    DefinePlugInCmd("/julia_send_reply", (PlugInCmdFunc)JuliaSendReply, nullptr);
    DefinePlugInCmd("/julia_test_outs", (PlugInCmdFunc)JuliaTestOuts, nullptr);
}