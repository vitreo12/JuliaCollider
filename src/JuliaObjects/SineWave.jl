@object Sine begin
    @inputs 1 ("frequency")
    @outputs 1

    #Declaration of structs (possibly, include() calls aswell)
    mutable struct Phasor
        p::Float64
        function Phasor()
            return new(0.0)
        end
    end

    #initialization of variables
    @constructor begin
        phasor::Phasor = Phasor()
        counter::Float64 = 1.0

        #Must always be last.
        @new(phasor, counter)
    end

    @perform begin
        sampleRate::Float64 = @sampleRate()

        frequency_kr::Float32 = @in0(1)

        @sample begin
            phase::Float64 = @unit(phasor.p) #equivalent to __unit__.phasor.p
            
            frequency::Float32 = @in(1)
            
            if(phase >= 1.0)
                phase = 0.0
            end
            
            out_value::Float32 = cos(phase * 2pi)
            
            @out(1) = out_value
            
            phase += frequency / (sampleRate - 1)
            
            @unit(phasor.p) = phase
        end
    end

    #used to RTFree buffers
    @destructor begin end
end