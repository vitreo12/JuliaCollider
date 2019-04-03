#https://cycling74.com/forums/zero-delay-feedback-svf-in-gen
@object DiodeLadder begin
    @inputs 3 ("input", "cutoff", "resonance")
    @outputs 1

    @constructor begin
        z::Data{Float64, 1} = Data(Float64, 4)

        inverse_sampleRate::Float64 = 1.0 / @sampleRate
        
        @new(z, inverse_sampleRate)
    end

    @perform begin
        resonance::Float64 = @in0(3)
        resonance = resonance > 0.0 ? resonance : 0.0
        resonance = resonance < 1.0 ? resonance : 1.0
        k::Float64 = resonance * 20.0
        A::Float64 = 1.0 + 0.5 * k

        @sample begin
            cutoff::Float64 = @in(2)
            
            wc::Float64 = pi * cutoff * inverse_sampleRate
            #wc = 2.0 * tan(0.5 * wc)
            wc2::Float64 = wc * wc
            wc3::Float64 = wc2 * wc
            wc4::Float64 = wc3 * wc

            b::Float64 = 1.0 / (1.0 + 8.0 * wc + 20.0 * wc2 + 16.0 * wc3 + 2.0 * wc4)
            g::Float64 = 2.0 * wc4 * b
            
            s::Float64 = (z[1] * wc3 + z[2] * (wc2 + 2.0 * wc3) + z[3] * (wc + 4.0 * wc2 + 2.0 * wc3) + z[4] * (1.0 + 6.0 * wc + 9.0 * wc2 + 2.0 * wc3)) * b

            input::Float64 = @in(1)
            y4::Float64 = (g * input + s) / (1.0 + g * k)
            y0::Float64 = tanh(input - k * y4)

            y1::Float64 = (y0 * (2.0 * wc + 12.0 * wc2 + 20.0 * wc3 + 8.0 * wc4) + z[1] * (1.0 + 6.0 * wc + 10.0 * wc2 + 4.0 * wc3) + z[2] * (2.0 * wc + 8.0 * wc2 + 6.0 * wc3) + z[3] * (2.0 * wc2 + 4.0 * wc3) + z[4] * 2.0 * wc3) * b;
            y2::Float64 = (y0 * (2.0 * wc2 + 8.0 * wc3 + 6.0 * wc4) + z[1] * (wc + 4.0 * wc2 + 3.0 * wc3) + z[2] * (1.0 + 6.0 * wc + 11.0 * wc2 + 6.0 * wc3) + z[3] * (wc + 4.0 * wc2 + 4.0 * wc3) + z[4] * (wc2 + 2.0 * wc3)) * b;
            y3::Float64 = (y0 * (2.0 * wc3 + 4.0 * wc4) + z[1] * (wc2 + 2.0 * wc3) + z[2] * (wc + 4.0 * wc2 + 4.0 * wc3) + z[3] * (1.0 + 6.0 * wc + 10 * wc2 + 4.0 * wc3) + z[4] * (wc + 4.0 * wc2 + 2.0 * wc3)) * b;
            y4 = g * y0 + s;

            z[1] = z[1] + (4.0 * wc * (y0 - y1 + y2));
            z[2] = z[2] + (2.0 * wc * (y1 - 2.0 * y2 + y3));
            z[3] = z[3] + (2.0 * wc * (y2 - 2.0 * y3 + y4));
            z[4] = z[4] + (2.0 * wc * (y3 - 2.0 * y4));
            
            out_value::Float32 = A * y4
            @out(1) = out_value
        end
    end
end