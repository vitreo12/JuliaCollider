@object AnalogDelay begin
    @inputs 5 ("input", "delay_length", "delay_time", "feedback", "damping")
    @outputs 1

    include("$(@__DIR__)/Interpolations.jl")

    mutable struct Histories
        phase::Int32
        prev_value::Float32
        function Histories()
            return new(0, 0.0f0)
        end
    end

    @constructor begin
        delay_length::Int32 = trunc(@in0(2) * @sampleRate)
        delay_length = delay_length < 1 ? 1 : delay_length
        
        delay_length_pow::Int32 = nextpow(2, delay_length)
        delay_mask::Int32 = delay_length_pow - 1

        delay_data::Data{Float32, 1} = Data(Float32, delay_length_pow) 

        histories::Histories = Histories()
        
        @new(delay_data, delay_mask, delay_length, histories)
    end

    @perform begin
        feedback::Float32 = @in0(4)
        feedback = feedback < 0.98 ? feedback : 0.98
        
        float_delay_time::Float32 = @in0(3) * @sampleRate
        float_delay_time = float_delay_time < delay_length ? float_delay_time : delay_length
        delay_time::Int32 = Int32(trunc(float_delay_time))
        
        frac::Float32 = float_delay_time - delay_time

        damping::Float32 = @in0(5)
        damping = damping > 0 ? damping : 0
        damping = damping < 0.98 ? damping : 0.98

        phase::Int32 = histories.phase
        prev_value::Float32 = histories.prev_value
        
        @sample begin
            input::Float32 = @in(1)
            
            #Read
            index_value::Int32 = ((phase - delay_time) & delay_mask) 
            delay_value1::Float32 = delay_data[(index_value & delay_mask) + 1]       #Julia index from one
            delay_value2::Float32 = delay_data[((index_value + 1) & delay_mask) + 1] #Julia index from one
            delay_value::Float32 = linear_interp(frac, delay_value1, delay_value2)

            @out(1) = delay_value

            feedback_value::Float32 = (delay_value * feedback)
            write_value::Float32 = input + feedback_value
            
            #Simple lowpass filter in feedback loop
            write_value = ((1.0f0 - damping) * write_value) + (damping * prev_value)
            
            #Write
            delay_data[phase + 1] = write_value                                      #Julia index from one

            #Advance reading index and store it for next iteration
            phase = (phase + 1) & delay_mask
            
            #Store filter value for next iteration
            prev_value = write_value
        end

        #Assign variables for next cycle
        histories.phase = phase
        histories.prev_value = prev_value
    end
end