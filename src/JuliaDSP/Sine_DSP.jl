module Sine_DSP
    
    mutable struct Phase_Increment
        phase::Float64
        function Phase_Increment()
            return new(0.0)
        end
    end

    struct Sine
        p::Phase_Increment
        function Sine()
            p::Phase_Increment = Phase_Increment()
            return new(p)
        end
    end

    function give_me_noise(scale_value::Float64)
        return rand(Float64) * rand(Float64) * rand(Float64) * 0.5 * (scale_value * 2)
    end

    function perform(unit::Sine, sample_rate::Float64, vector_size::Int32, output_vector::Vector{Float32}, frequency::Float64)
        @inbounds for i::Int64 = 1 : vector_size
            phase::Float64 = unit.p.phase

            if(phase >= 1.0)
                phase = 0.0
            end

            output_vector[i] = cos(phase * 2pi)

            phase += (give_me_noise(1.0) * frequency) / (sample_rate - 1)
            unit.p.phase = phase
        end
    end

    function destructor(unit::Sine) end
    
end
