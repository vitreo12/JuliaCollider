#include "JuliaFuncs.hpp"

struct Julia : public SCUnit 
{
public:
    Julia() 
    { 
        if(julia_initialized)
        {
            if(sine_fun != nullptr)
            {
                object_id_dict = create_object_id_dict(global_id_dict, id_dict_function, set_index);

                args = (jl_value_t**)RTAlloc(mWorld, sizeof(jl_value_t*) * 6);

                args[0] = perform_fun; //already in id dict
                
                sine = jl_call0(sine_fun);
                args[1] = sine;
                jl_call3(set_index, object_id_dict, sine, sine);
                
                args[2] = jl_box_float64(sampleRate());
                jl_call3(set_index, object_id_dict, args[2], args[2]);

                args[3] = jl_box_int32(bufferSize());
                jl_call3(set_index, object_id_dict, args[3], args[3]);

                args[4] = (jl_value_t*)jl_ptr_to_array_1d(jl_apply_array_type((jl_value_t*)jl_float32_type, 1), nullptr, bufferSize(), 0);
                jl_call3(set_index, object_id_dict, args[4], args[4]);

                args[5] = jl_box_float64((double)440.0);
                jl_call3(set_index, object_id_dict, args[5], args[5]);

                execute = true;
            }
            else
                Print("WARNING: NO FUNCTION DEFINITION\n");
        }
        else
            Print("WARNING: Julia not initialized\n");


        set_calc_function<Julia, &Julia::next>();
        next(1);
    }

    ~Julia() 
    {
        delete_object_id_dict(global_id_dict, object_id_dict, delete_index);
        RTFree(mWorld, args);
    }

private:
    jl_value_t* sine;
    jl_value_t** args;
    jl_value_t* object_id_dict;
    bool execute = false;

    inline void next(int inNumSamples) 
    {
        double input = (double)(in0(0));
        float* output = out(0);

        if(execute)
        {
            *(int*)jl_data_ptr(args[3]) = inNumSamples;
            //point julia buffer to correct audio buffer.
            ((jl_array_t*)(args[4]))->data = output;
            //set frequency
            *(double*)jl_data_ptr(args[5]) = input;

            jl_call_no_gc(args, 6);
        }
        else
        {
            for (int i = 0; i < inNumSamples; i++) 
            {
                output[i] = 0.0f;
            }
        }
    }
};

PluginLoad(JuliaUGens) 
{
    ft = inTable;
    registerUnit<Julia>(ft, "Julia");

    DefineJuliaCmds();

    boot_julia();
}