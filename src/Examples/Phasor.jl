@object MyPhasor begin
    @inputs 1
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

        buffer::Data{Float32} = Data(Float32, Int32(100))

        #Must always be last.
        @new(phasor, buffer)
    end

    @perform begin
        sampleRate::Float32 = Float32(@sampleRate())

        buf_length::Int32 = Int32(length(buffer))

        #frequency_kr::Float32 = @in0(1)

        @sample begin
            phase::Float32 = phasor.p #equivalent to __unit__.phasor.p
            
            frequency::Float32 = @in(1)
            
            if(phase >= 1.0)
                phase = 0.0
            end
            
            out_value::Float32 = (phase * 2) - 1
            
            @out(1) = out_value * 0.2

            phase += abs(frequency) / (sampleRate - 1)
            
            buffer_index::Int32 = mod(@sample_index, buf_length)
            if(buffer_index == 0)
                buffer_index = 1
            end

            buffer[buffer_index] = out_value
            
            phasor.p = phase
        end
    end
end