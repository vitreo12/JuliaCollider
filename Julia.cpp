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
                sine = jl_call0(sine_fun);
                
                args = (jl_value_t**)RTAlloc(mWorld, sizeof(jl_value_t*) * 5);
                args[0] = sine;
                args[1] = jl_box_float64(sampleRate());
                args[2] = jl_box_int32(bufferSize());
                args[3] = (jl_value_t*)jl_ptr_to_array_1d(jl_apply_array_type((jl_value_t*)jl_float32_type, 1), nullptr, bufferSize(), 0);
                args[4] = jl_box_float64((double)440.0);

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
        RTFree(mWorld, args);
    }

private:
    jl_value_t* sine;
    jl_value_t** args;
    bool execute = false;

    inline void next(int inNumSamples) 
    {
        double input = (double)(in0(0));
        float* output = out(0);

        if(execute)
        {
            *(int*)jl_data_ptr(args[2]) = inNumSamples;
            //point julia buffer to correct audio buffer.
            ((jl_array_t*)(args[3]))->data = output;
            //set frequency
            *(double*)jl_data_ptr(args[4]) = input;

            jl_call(perform_fun, args, 5);
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

/* IT DOESN'T WORK IF BOOTING JULIA FROM HERE. NEED TO BOOT IT WITH A COMMAND */
PluginLoad(JuliaUGens) 
{
    ft = inTable;
    registerUnit<Julia>(ft, "Julia");

    DefineJuliaCmds();

    boot_julia();
}