@object Sine begin
    @inputs 1 ("frequency")
    @outputs 1
    
    #Declaration of structs (possibly, include() calls aswell)
    mutable struct Phasor
        p::Float32
        function Phasor()
            return new(0.0)
        end
    end

    #initialization of variables
    @constructor begin
        phasor::Phasor = Phasor()

        buffer::__Data__{Float32} = __Data__(Float32, Int32(100))

        #Must always be last.
        @new(phasor, buffer)
    end

    function calc_cos(sample::Float32)
        return cos(sample)
    end

    @perform begin
        sampleRate::Float32 = Float32(@sampleRate())

        buf_length::Int32 = Int32(length(@unit(buffer)))

        #frequency_kr::Float32 = @in0(1)

        @sample begin
            phase::Float32 = @unit(phasor.p) #equivalent to __unit__.phasor.p
            
            frequency::Float32 = @in(1)
            
            if(phase >= 1.0)
                phase = 0.0
            end
            
            out_value::Float32 = calc_cos(Float32(phase * 2pi))
            
            @out(1) = out_value
            
            #(phase * 2) - 1

            phase += abs(frequency) / (sampleRate - 1)
            
            buffer_index::Int32 = mod(@sample_index, buf_length)
            if(buffer_index == 0)
                buffer_index = 1
            end

            @unit(buffer)[buffer_index] = out_value
            
            @unit(phasor.p) = phase
        end
    end

    #used to RTFree buffers
    @destructor begin end
end