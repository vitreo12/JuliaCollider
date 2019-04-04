#pragma once

#include "SC_PlugIn.hpp"
#include "JuliaReplyCmds.h"
#include "GlobalVariables.h"

/* Asynchronous functions used to communicate between sclang and scsynth. This is 
where all classes come to place and get used. */

void julia_empty_cleanup(World* world, void* cmd) {return;}

/* Actual GC collection function */
void perform_gc(int full, bool already_has_lock = false)
{
    if(!already_has_lock)
        julia_gc_barrier->NRTSpinlock();

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

                delete_index_args[0] = julia_global_state->get_delete_index_fun();
                delete_index_args[1] = julia_global_state->get_global_object_id_dict().get_id_dict();
                delete_index_args[2] = this_ugen_ref;

                /* 
                Should I simply run the destructor instead?
                Well, it depends if Data is a mutable struct with finalizer or not.
                If it has finalizer, this method is just fine.
                */
                jl_value_t* delete_index_successful = jl_lookup_generic_and_compile_return_value_SC(delete_index_args, delete_index_nargs);
                if(!delete_index_successful)
                    printf("ERROR: Could not delete __UGenRef__ object from global object id dict\n");

                gc_array[i] = nullptr;
            }
        }

        gc_array_needs_emptying = false;
    }

    /* Check if memory has been allocated */
    int current_pool_size = julia_global_state->get_julia_alloc_funcs()->fRTTotalFreeMemory(julia_global_state->get_julia_alloc_pool());

    /* Only perform GC if memory has been allocated from last time */
    if(previous_pool_size != current_pool_size)
    {
        if(!jl_gc_is_enabled())
            jl_gc_enable(1);
        
        jl_gc_collect(full);
        
        printf("-> Julia: Completed GC\n");
        
        if(jl_gc_is_enabled())
            jl_gc_enable(0);
    }

    previous_pool_size = current_pool_size;

    if(!already_has_lock)
        julia_gc_barrier->Unlock();
}

/* Function to be performed on spawned thread */
void perform_gc_on_spawned_thread(World* inWorld)
{
    while(perform_gc_thread_run)
    {
        /* This lock is used to schedule things between NRT thread (where compiler operates) and
        GC thread, which is executed once every certain time (depending on thread sleep). The interaction between
        the two does not affect the RT audio thread */
        /* It could also be a spinlock here. Depends on the sleep period of the thread */
        if(julia_compiler_gc_barrier->RTTrylock())
        {
            //printf("*** MY THREAD ***\n");

            julia_gc_barrier->NRTSpinlock();

            /* 
            Thanks to the thread-safe allocator, the GC would actually working fine
            together with the RT thread.
            Things to watch out: 
            
            1 Do GC finalizers work with RT thread?
            2 Does GC work together with "debug" mode and its JL_TRY/CATCH? Or does it require locks?

            However, this will all be implemented in a future release, together
            with a better deletion of modules when running JuliaDef.free.
            */

            if(!active_julia_ugens.load())
                perform_gc(1, true); //already acquired GC lock to check for number of active ugens

            julia_gc_barrier->Unlock(); 
            
            julia_compiler_gc_barrier->Unlock();
        }

        //Run every 10 seconds.
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

struct JuliaBootArgs
{
    int julia_pool_alloc_mem_size;
};

//On NRT thread
bool julia_boot(World* inWorld, void* cmd)
{
    if(!jl_is_initialized())
    {
        JuliaBootArgs* julia_boot_args = (JuliaBootArgs*)cmd;

        int julia_pool_alloc_mem_size = julia_boot_args->julia_pool_alloc_mem_size;

        julia_global_state = new JuliaGlobalState(inWorld, julia_pool_alloc_mem_size);
        if(julia_global_state->is_initialized())
        {
            julia_gc_barrier          = new JuliaAtomicBarrier();
            julia_compiler_barrier    = new JuliaAtomicBarrier();
            julia_compiler_gc_barrier = new JuliaAtomicBarrier();
            julia_objects_array       = new JuliaObjectsArray(inWorld, julia_global_state);

            gc_array = (jl_value_t**)malloc(sizeof(jl_value_t*) * gc_array_num);
            if(!gc_array)
            {
                printf("ERROR: Could not allocate gc_array\n");
                return false;
            }

            for(int i = 0; i < gc_array_num; i++)
                gc_array[i] = nullptr;

            perform_gc(1);

            //Setup thread for GC collection every 10 seconds.
            perform_gc_thread_run.store(true);
            perform_gc_thread = std::thread(perform_gc_on_spawned_thread, inWorld);
        }
    }
    else
        printf("WARNING: Julia already booted \n");
    
    return true;
}

/* This gets executed on RT thread,
so it's safe to RTFree into World*. */
void julia_boot_cleanup(World* world, void* cmd) 
{
    JuliaBootArgs* julia_boot_args = (JuliaBootArgs*)cmd;
    if(julia_boot_args)
        RTFree(world, julia_boot_args);
}

/* This gets executed on RT thread,
so it's safe to RTAlloc into World*. */
void JuliaBoot(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    /* first argument is the memory size for JuliaAllocPool */
    int julia_pool_alloc_mem_size = args->geti();

    JuliaBootArgs* julia_boot_args = (JuliaBootArgs*)RTAlloc(inWorld, sizeof(JuliaBootArgs));
    if(!julia_boot_args)
        return;

    julia_boot_args->julia_pool_alloc_mem_size = julia_pool_alloc_mem_size;

    DoAsynchronousCommand(inWorld, replyAddr, "/jl_boot", (void*)julia_boot_args, (AsyncStageFn)julia_boot, 0, 0, julia_boot_cleanup, 0, nullptr);
}

/* on NRT thread. Actually compiles a @object */
bool julia_load(World* world, void* cmd)
{
    JuliaReplyWithLoadPath* julia_reply_with_load_path = (JuliaReplyWithLoadPath*)cmd;

    if(julia_global_state->is_initialized())
    {
        //Wait on compiler barrier
        julia_compiler_barrier->NRTSpinlock();

        //GC interaction from spawned thread
        julia_compiler_gc_barrier->NRTSpinlock();
        
        bool object_created = julia_objects_array->create_julia_object(julia_reply_with_load_path);
        if(!object_created)
        {
            julia_reply_with_load_path->create_done_command(julia_reply_with_load_path->get_OSC_unique_id(), "/jl_load", -1, "@No_Name", -1, -1);
            printf("ERROR: Could not create JuliaDef\n");
        }
        
        //perform gc after each "/jl_load"
        perform_gc(1);
        
        julia_compiler_barrier->Unlock();

        //GC interaction from spawned thread
        julia_compiler_gc_barrier->Unlock();
    }
    else
    {
        julia_reply_with_load_path->create_done_command(julia_reply_with_load_path->get_OSC_unique_id(), "/jl_load", -1, "@No_Name", -1, -1);
        printf("WARNING: Julia hasn't been booted correctly \n");
    }

    return true;
}

/* This gets executed on RT thread,
so it's safe to RTFree into World*. */
void julia_load_cleanup(World* world, void* cmd) 
{
    JuliaReplyWithLoadPath* julia_reply_with_load_path = (JuliaReplyWithLoadPath*)cmd;

    if(julia_reply_with_load_path)
        JuliaReplyWithLoadPath::operator delete(julia_reply_with_load_path, world, ft); //Needs to be called excplicitly
}

/* This gets executed on RT thread,
so it's safe to RTAlloc into World*. */
void JuliaLoad(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    if(jl_is_initialized())
    {
        int osc_unique_id = args->geti();
        const char* julia_load_path = args->gets(); //this const char* will be deep copied in constructor.

        //Alloc with overloaded new operator
        JuliaReplyWithLoadPath* julia_reply_with_load_path = new(inWorld, ft) JuliaReplyWithLoadPath(osc_unique_id, julia_load_path);
        
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

/* Delete an @object */
bool julia_free(World* world, void* cmd)
{
    if(julia_global_state->is_initialized())
    {
        julia_compiler_barrier->NRTSpinlock();

        //GC interaction from spawned thread
        julia_compiler_gc_barrier->NRTSpinlock();
        
        JuliaReceiveObjectId* julia_object_id = (JuliaReceiveObjectId*)cmd;
        int object_id = julia_object_id->get_julia_object_id();
        
        julia_objects_array->delete_julia_object(object_id);
        
        julia_compiler_barrier->Unlock();

        //GC interaction from spawned thread
        julia_compiler_gc_barrier->Unlock();
    }
    return true;
}

/* This gets executed on RT thread,
so it's safe to RTFree into World*. */
void julia_free_cleanup(World* world, void* cmd) 
{
    JuliaReceiveObjectId* julia_object_id = (JuliaReceiveObjectId*)cmd;

    if(julia_object_id)
        JuliaReceiveObjectId::operator delete(julia_object_id, world, ft); //Needs to be called excplicitly
}

/* This gets executed on RT thread,
so it's safe to RTAlloc into World*. */
void JuliaFree(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    if(jl_is_initialized())
    {
        int object_id = args->geti(); 

        //Alloc with overloaded new operator
        JuliaReceiveObjectId* julia_object_id = new(inWorld, ft) JuliaReceiveObjectId(object_id);
        
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

/* Get array of all @object(s) */
bool julia_get_julia_objects_list(World* world, void* cmd)
{
    if(julia_global_state->is_initialized())
    {
        /* SHOULD THESE LOCKS BE REMOVED? They are mainly needed to make sure
        that the julia_object won't get deleted as it's getting retrieved... */
        
        julia_compiler_barrier->NRTSpinlock();

        //GC interaction from spawned thread
        julia_compiler_gc_barrier->NRTSpinlock();

        JuliaReply* julia_reply = (JuliaReply*)cmd;
        julia_objects_array->get_julia_objects_list(julia_reply);

        julia_compiler_barrier->Unlock();

        //GC interaction from spawned thread
        julia_compiler_gc_barrier->Unlock();
    }

    return true;
}

void julia_get_julia_objects_list_cleanup(World* world, void* cmd) 
{

    JuliaReply* julia_reply = (JuliaReply*)cmd;
    if(julia_reply)
        JuliaReply::operator delete(julia_reply, world, ft);
}

/* This gets executed on RT thread,
so it's safe to RTAlloc into World*. */
void JuliaGetJuliaObjectsList(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    if(jl_is_initialized())
    {
        int OSC_unique_id = args->geti();

        JuliaReply* julia_reply = new(inWorld, ft) JuliaReply(OSC_unique_id);
        
        if(!julia_reply)
        {
            printf("ERROR: Could not allocate JuliaReply\n");
            return;
        }

        //julia_reply_with_load_path->get_buffer() is the return message that will be sent at "/done" of this async command.
        DoAsynchronousCommand(inWorld, replyAddr, julia_reply->get_buffer(), julia_reply, (AsyncStageFn)julia_get_julia_objects_list, 0, 0, julia_get_julia_objects_list_cleanup, 0, nullptr);
    }
    else
        printf("WARNING: Julia hasn't been initialized yet\n");
}

/* Retrieve just one @object by name */
bool julia_get_julia_object_by_name(World* world, void* cmd)
{
    if(julia_global_state->is_initialized())
    {
        /* SHOULD THESE LOCKS BE REMOVED? They are mainly needed to make sure
        that the julia_object won't get deleted as it's getting retrieved... */

        julia_compiler_barrier->NRTSpinlock();

        //GC interaction from spawned thread
        julia_compiler_gc_barrier->NRTSpinlock();
        
        JuliaReplyWithLoadPath* julia_reply = (JuliaReplyWithLoadPath*)cmd;
        julia_objects_array->get_julia_object_by_name(julia_reply);

        julia_compiler_barrier->Unlock();

        //GC interaction from spawned thread
        julia_compiler_gc_barrier->Unlock();
    }

    return true;
}

/* This gets executed on RT thread,
so it's safe to RTFree into World*. */
void julia_get_julia_object_by_name_cleanup(World* world, void* cmd) 
{
    JuliaReplyWithLoadPath* julia_reply = (JuliaReplyWithLoadPath*)cmd;
    if(julia_reply)
        JuliaReplyWithLoadPath::operator delete(julia_reply, world, ft);
}

/* This gets executed on RT thread,
so it's safe to RTAlloc into World*. */
void JuliaGetJuliaObjectByName(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    if(jl_is_initialized())
    {
        int OSC_unique_id = args->geti();
        const char* object_name = args->gets();

        //Use JuliaReplyPath's load path as a string replacement for object_name
        JuliaReplyWithLoadPath* julia_reply = new(inWorld, ft) JuliaReplyWithLoadPath(OSC_unique_id, object_name);
        
        if(!julia_reply)
        {
            printf("ERROR: Could not allocate JuliaReply\n");
            return;
        }

        //julia_reply_with_load_path->get_buffer() is the return message that will be sent at "/done" of this async command.
        DoAsynchronousCommand(inWorld, replyAddr, julia_reply->get_buffer(), julia_reply, (AsyncStageFn)julia_get_julia_object_by_name, 0, 0, julia_get_julia_object_by_name_cleanup, 0, nullptr);
    }
    else
        printf("WARNING: Julia hasn't been initialized yet\n");
}

/* Called when unloading the shared library from the SC server with the void unload(InterfaceTable *inTable) function */
void julia_quit()
{
    if(!jl_is_initialized())
        return;

    //If julia has been initialized but global_state failed for whatever reason
    if(jl_is_initialized() && !julia_global_state->is_initialized())
    {
        jl_atexit_hook(0);
        return;
    }
    
    if(julia_global_state->is_initialized())
    {        
        perform_gc_thread_run.store(false);
        perform_gc_thread.join();

        delete julia_objects_array;

        julia_gc_barrier->NRTSpinlock();
        julia_compiler_barrier->NRTSpinlock();

        //This will quit Julia
        delete julia_global_state;

        julia_gc_barrier->Unlock();
        
        delete julia_gc_barrier;

        julia_compiler_barrier->Unlock();

        delete julia_compiler_barrier;

        delete julia_compiler_gc_barrier;

        free(gc_array);

        printf("-> Julia: Finished quitting \n");
    }
}

/*
bool julia_query_id_dicts(World* world, void* cmd)
{
    if(julia_global_state->is_initialized())
    {
        julia_compiler_barrier->NRTSpinlock();

        //GC interaction from spawned thread
        julia_compiler_gc_barrier->NRTSpinlock();

        jl_call1(jl_get_function(jl_base_module, "println"), julia_global_state->get_global_def_id_dict().get_id_dict());
        jl_call1(jl_get_function(jl_base_module, "println"), julia_global_state->get_global_object_id_dict().get_id_dict());
        
        julia_compiler_barrier->Unlock();

        //GC interaction from spawned thread
        julia_compiler_gc_barrier->Unlock();
    }

    return true;
}

void JuliaQueryIdDicts(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    if(jl_is_initialized())
        DoAsynchronousCommand(inWorld, replyAddr, "/jl_query_id_dicts", nullptr, (AsyncStageFn)julia_query_id_dicts, 0, 0, julia_empty_cleanup, 0, nullptr);
}
*/

/* Debug memory footprint of Julia. Notice how UGens don't allocate memory, as they
use GC's pools. Try ways of reducing the memory footprint of recompiling the same Julia module. */
bool julia_total_free_memory(World* world, void* cmd)
{
    printf("FREE MEMORY: %zu\n", julia_global_state->get_julia_alloc_funcs()->fRTTotalFreeMemory(julia_global_state->get_julia_alloc_pool()));
    return true;
}

void JuliaTotalFreeMemory(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    if(jl_is_initialized())
        DoAsynchronousCommand(inWorld, replyAddr, "/jl_total_free_memory", nullptr, (AsyncStageFn)julia_total_free_memory, 0, 0, julia_empty_cleanup, 0, nullptr);
}

/* This is executed on RT thread, so no need of checks with performing UGens */
void JuliaChangeMode(World *inWorld, void* inUserData, struct sc_msg_iter *args, void *replyAddr)
{
    const char* mode = args->gets();
    if(strcmp(mode, "perform") == 0)
        debug_or_perform_mode = true;
    else
        debug_or_perform_mode = false;

    printf("MODE: %d\n", debug_or_perform_mode);   
}

/*
NOTES:
1) The module deletion problem could simply be solved with an improved allocation pool that, when surpassed the size
would just allocate a new chunk. This way, Julia would operate normally (and would delete old modules
after long periods of time. If allocation pool allows it, it's all good). Moreover, what should be optimized
is the actual memory footprint of each module and its precompilation process: some of the things could be simplified
*/

void DefineJuliaCmds()
{
    DefinePlugInCmd("/julia_boot", (PlugInCmdFunc)JuliaBoot, nullptr);
    DefinePlugInCmd("/julia_load", (PlugInCmdFunc)JuliaLoad, nullptr);
    DefinePlugInCmd("/julia_free", (PlugInCmdFunc)JuliaFree, nullptr);
    DefinePlugInCmd("/julia_get_julia_objects_list", (PlugInCmdFunc)JuliaGetJuliaObjectsList, nullptr);
    DefinePlugInCmd("/julia_get_julia_object_by_name", (PlugInCmdFunc)JuliaGetJuliaObjectByName, nullptr);
    DefinePlugInCmd("/julia_total_free_memory", (PlugInCmdFunc)JuliaTotalFreeMemory, nullptr);
    DefinePlugInCmd("/julia_set_perform_debug_mode", (PlugInCmdFunc)JuliaChangeMode, nullptr);
    //DefinePlugInCmd("/julia_query_id_dicts",   (PlugInCmdFunc)JuliaQueryIdDicts, nullptr);
}