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

        data::__Data__{Float32} = __Data__(Float32, Int32(100))

        buffer::__Buffer__ = __Buffer__()

        #println(buffer)
        #println(buffer[Int32(1) + Int32(floor(0.5 * length(buffer)))])

        #get_shared_buf(buffer, Float32(0.0))

        #println(length(buffer))

        #Must always be last.
        @new(phasor, data, buffer)
    end

    function calc_cos(sample::Float32)
        return cos(sample)
    end

    @perform begin
        buffer::__Buffer__ = @unit(buffer)

        get_shared_buf(buffer, Float32(0.0))

        sampleRate::Float32 = Float32(@sampleRate())

        buf_length::Int32 = Int32(length(@unit(data)))

        #frequency_kr::Float32 = @in0(1)

        @sample begin
            phase::Float32 = @unit(phasor.p) #equivalent to __unit__.phasor.p
            
            frequency::Float32 = @in(1)
            
            if(phase >= 1.0)
                phase = 0.0
            end
            
            out_value::Float32 = calc_cos(Float32(phase * 2pi))
            
            @out(1) = @unit(buffer)[Int32(1) + Int32(floor(phase * length(@unit(buffer))))]
            
            #@unit(buffer)[Int32(1) + Int32(floor(phase * length(@unit(buffer))))]
            
            #(phase * 2) - 1

            phase += abs(frequency) / (sampleRate - 1)
            
            data_index::Int32 = mod(@sample_index, buf_length)
            if(data_index == 0)
                data_index = 1
            end

            @unit(data)[data_index] = out_value
            
            @unit(phasor.p) = phase
        end
    end

    #used to RTFree datas
    @destructor begin end
end