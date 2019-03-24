@object Saw begin
    @inputs 1 ("frequency")
    @outputs 1
    
    mutable struct Histories
        phase::Float64
        prev_value::Float64
        function Histories()
            return new(0.0, 0.0)
        end
    end

    @constructor begin
        histories::Histories = Histories()
        
        @new(histories)
    end


    @perform begin
        sample_rate::Float64 = @sampleRate
        
        phase::Float64 = histories.phase
        prev_value::Float64 = histories.prev_value

        @sample begin
            freq::Float64 = abs(Float64(@in(1)))
            if(freq == 0.0)
                freq = 0.01
            end

            #0.0 would result in 0 / 0 -> NaN
            if(phase == 0.0)
                phase = 1.0
            end

            #BLIT
            N::Float64 = trunc((sample_rate * 0.5) / freq)
            phase_2pi::Float64 = phase * 2pi
            BLIT::Float64 = 0.5 * (sin(phase_2pi * (N + 0.5)) / (sin(phase_2pi * 0.5)) - 1.0)

            #Leaky integrator
            freq_over_samplerate::Float64 = (freq * 2pi) / sample_rate * 0.25
            out_value::Float64 = (freq_over_samplerate * (BLIT - prev_value)) + prev_value
            @out(1) = Float32(out_value)
            
            phase += freq / (sample_rate - 1.0)
            phase = mod(phase, 1.0)
            prev_value = out_value
        end

        histories.prev_value = prev_value
        histories.phase = phase
    end
end