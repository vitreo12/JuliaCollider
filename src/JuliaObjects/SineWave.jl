@object Sine begin
    @inputs 2
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

        data::Data{Float32} = Data(Float32, Int32(100))

        buffer::Buffer = Buffer(2)

        #println(buffer)
        #println(buffer[Int32(1) + Int32(floor(0.5 * length(buffer)))])

        #__get_shared_buf__(buffer, Float32(0.0))

        #println(buffer)

        #Must always be last.
        @new(phasor, data, buffer)
    end

    function calc_cos(sample::Float32)
        return cos(sample)
    end

    @perform begin
        sampleRate::Float32 = Float32(@sampleRate())

        data_length::Int32 = Int32(length(data))

        #frequency_kr::Float32 = @in0(1)

        @sample begin
            phase::Float32 = phasor.p #equivalent to __unit__.phasor.p
            
            frequency::Float32 = @in(1)
            
            if(phase >= 1.0)
                phase = 0.0
            end
            
            out_value::Float32 = calc_cos(Float32(phase * 2pi))
            
            @out(1) = buffer[Int32(1) + Int32(floor(phase * length(buffer)))]
            
            #buffer[Int32(1) + Int32(floor(phase * length(buffer)))]
            
            #(phase * 2) - 1

            phase += abs(frequency) / (sampleRate - 1)
            
            data_index::Int32 = mod(@sample_index, data_length)
            if(data_index == 0)
                data_index = 1
            end

            data[data_index] = out_value
            
            phasor.p = phase
        end
    end
end