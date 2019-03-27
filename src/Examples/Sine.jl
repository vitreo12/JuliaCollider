@object Sine begin
    @inputs 1
    @outputs 1
    
    mutable struct Phasor
        p::Float32
        function Phasor()
            return new(0.0)
        end
    end

    #initialization of variables
    @constructor begin
        phasor::Phasor = Phasor()
        
        @new(phasor)
    end

    @perform begin
        sampleRate::Float32 = Float32(@sampleRate())

        @sample begin
            phase::Float32 = phasor.p 

            frequency::Float32 = @in(1)
            
            if(phase >= 1.0)
                phase = 0.0
            end
            
            sine_value::Float32 = cos(phase * 2pi)
            
            @out(1) = sine_value

            phase += abs(frequency) / (sampleRate - 1)
            
            phasor.p = phase
        end
    end
end