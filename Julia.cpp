#include "JuliaFuncs.hpp"

struct Julia : public SCUnit 
{
public:
    Julia() 
    { 
        set_calc_function<Julia, &Julia::next>();

        if(julia_initialized)
        {
            if(phasor_fun != nullptr)
            {
                jl_value_t* phasor = jl_call0(phasor_fun);
                //dummy print. julia's println doesn't work with this build???
                Print("%f\n", jl_unbox_float32(jl_call2(jl_get_function(jl_main_module, "+"), jl_box_float32((float)50.0), jl_box_float32((float)12.0))));
            }
            else
                Print("WARNING: NO FUNCTION DEFINITION\n");
        }
        else
            Print("WARNING: Julia not initialized\n");

        next(1);
    }

    ~Julia() {}

private:
    inline void next(int inNumSamples) 
    {
        //Print("%f\n", jl_unbox_float32(jl_call2(jl_get_function(jl_main_module, "+"), jl_box_float32((float)50.0), jl_box_float32((float)12.0))));     
        const float* input1 = in(0);
        float* output1 = out(0);
        for (int i = 0; i < inNumSamples; i++) 
        {
            const double in1 = input1[i];
            output1[i] = 0.0f;
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