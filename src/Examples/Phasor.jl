@object Phasor begin
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

        #Must always be last.
        @new(phasor)
    end

    @perform begin
        sampleRate::Float32 = Float32(@sampleRate())

        @sample begin
            phase::Float32 = phasor.p
            
            frequency::Float32 = abs(@in(1))
            
            if(phase >= 1.0)
                phase = 0.0
            end
            
            @out(1) = phase

            phase += frequency / (sampleRate - 1)
            
            phasor.p = phase
        end
    end
end