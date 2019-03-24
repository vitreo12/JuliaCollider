@object Delay begin
    @inputs 4
    @outputs 1

    mutable struct Phasor
        p::Int32
        function Phasor()
            return new(0)
        end
    end

    @constructor begin
        delay_length::Int32 = trunc(@in0(2) * @sampleRate)
        delay_length = delay_length < 1 ? 1 : delay_length

        delay_length_pow::Int32 = nextpow(2, delay_length)
        delay_mask::Int32 = delay_length_pow - 1

        delay_data::Data = Data(Float32, delay_length_pow) 

        delay_phasor::Phasor = Phasor()
        
        @new(delay_data, delay_mask, delay_length, delay_phasor)
    end

    @perform begin
        feedback::Float32 = @in0(4)
        feedback = feedback > 0.98 ? 0.98 : feedback

        delay_time::Int32 = Int32(trunc(@in0(3) * @sampleRate))
        delay_time > delay_length ? delay_length : delay_time

        phase::Int32 = delay_phasor.p
        
        @sample begin
            input::Float32 = @in(1)

            #REMEMBER: Julia arrays index starts from 1
            index_value::Int32 = ((phase - delay_time) & delay_mask) + 1
        
            delay_value::Float32 = delay_data[index_value]

            @out(1) = delay_value

            #REMEMBER: Julia arrays index starts from 1
            delay_data[phase + 1] = input + (delay_value * feedback)

            phase = (phase + 1) & delay_mask
        end

        delay_phasor.p = phase
    end
end