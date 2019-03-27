#https://github.com/ddiakopoulos/MoogLadders/blob/master/src/KrajeskiModel.h
@object KrajeskiMoog begin
    @inputs 4 ("input", "cutoff", "resonance", "drive")
    @outputs 1

    @constructor begin
        state::Data{Float64, 1} = Data(Float64, 5)
        delay::Data{Float64, 1} = Data(Float64, 5)

        @new(state, delay)
    end

    @perform begin
        drive::Float64 = @in0(4)
        gComp::Float64 = 1.0f0

        @sample begin
            #Audio rate cutoff / resonance
            cutoff::Float64 = Float64(@in(2))
            cutoff = cutoff < 20000.0 ? cutoff : 20000.0
            cutoff = cutoff > 1.0 ? cutoff : 1.0
            wc::Float64 = 2pi * (cutoff / @sampleRate)
            g::Float64 = (0.9892 * wc) - (0.4342 * wc^2) + (0.1381 * wc^3) - (0.0202 * wc^4)

            resonance::Float64 = Float64(@in(3))
            resonance = resonance < 0.999 ? resonance : 0.999
            resonance = resonance > 0.0 ? resonance : 0.0
            gRes::Float64 = resonance * (1.0029 + (0.0526 * wc) - (0.926 * wc^2) + (0.0218 * wc^3))

            state[1] = tanh(drive * (Float64(@in(1)) - 4 * gRes * (state[5] - gComp * Float64(@in(1)))))

            for i = 1 : 4
                state[i + 1] = g * (0.3 / 1.3 * state[i] + 1 / 1.3 * delay[i] - state[i + 1]) + state[i + 1]
                delay[i] = state[i]
            end

            @out(1) = Float32(state[5])
        end
    end
end