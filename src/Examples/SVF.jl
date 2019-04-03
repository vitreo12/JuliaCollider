#https://cycling74.com/forums/zero-delay-feedback-svf-in-gen
@object SVF begin
    @inputs 4 ("input", "filter_type", "cutoff", "resonance")
    @outputs 1

    mutable struct Histories
        s1::Float64
        s2::Float64
        function Histories()
            return new(0.0, 0.0)
        end
    end
        
    @constructor begin
        inverse_sampleRate::Float64 = 1.0 / @sampleRate
        histories::Histories = Histories()
        
        @new(inverse_sampleRate, histories)
    end

    @perform begin
        filter_type::Int32 = Int32(trunc(@in0(2)))
        filter_type = filter_type > 0 ? filter_type : 0
        filter_type = filter_type <= 2 ? filter_type : 2
        
        #Resonance at KR
        r::Float64 = 1.0 - Float64(@in0(4))
        r = r > 0.005 ? r : 0.005
        r = r < 1.0 ? r : 1.0

        @sample begin
            #Audio rate (more expensive) cutoff control
            g::Float64 = Float64(@in(3))
            g = g > 10.0 ? g : 10.0
            g = g < 20000.0 ? g : 20000.0

            g = ((2.0 / inverse_sampleRate) * tan((g * 2pi) * inverse_sampleRate / 2.0) * inverse_sampleRate / 2.0)

            hp::Float64 = (Float64(@in(1)) - 2.0 * r * histories.s1 - g * histories.s1 - histories.s2) / (1.0 + 2.0 * r * g + g * g)

            bp::Float64 = g * hp + histories.s1
            histories.s1 = g * hp + bp

            lp::Float64 = g * bp + histories.s2
            histories.s2 = g * bp + lp

            if(filter_type == 0)
                @out(1) = Float32(lp)
            elseif(filter_type == 1)
                @out(1) = Float32(hp)
            else
                @out(1) = Float32(bp)
            end
        end
    end
end