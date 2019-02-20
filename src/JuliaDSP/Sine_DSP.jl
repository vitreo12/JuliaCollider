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

    function dummy_alloc()
        a = zeros(10^6)
        a = nothing
    end

    function perform(unit::Sine, sample_rate::Float64, vector_size::Int32, output_vector::Vector{Float32}, frequency::Float64)
        @inbounds for i::Int64 = 1 : vector_size
            phase::Float64 = unit.p.phase

            if(phase >= 1.0)
                phase = 0.0
            end

            output_vector[i] = cos(phase * 2pi) * 0.1

            phase += frequency / (sample_rate - 1)
            unit.p.phase = phase
        end
    end

    function destructor(unit::Sine) end
    
end
