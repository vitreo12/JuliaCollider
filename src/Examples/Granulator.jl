@object Granulator begin
    @inputs 9 ("audio_buffer", "envelope_buffer", "density", "position", "position_range", "length", "length_range", "pitch", "pitch_range")
    @outputs 2
    
    include("$(@__DIR__)/Interpolations.jl")

    mutable struct Phasor
        phase::Float32
        function Phasor()
            return new(0.0)
        end
    end

    mutable struct GrainArray
        phasors::Data
        start_positions::Data
        end_positions::Data
        pitches::Data
        busy::Data
        function GrainArray(num_grains::Int32)
            phasors::Data = Data(Float32, num_grains)
            start_positions::Data = Data(Int32, num_grains)
            end_positions::Data = Data(Int32, num_grains)
            pitches::Data = Data(Float32, num_grains)
            busy::Data = Data(Int32, num_grains)
            return new(phasors, start_positions, end_positions, pitches, busy)
        end
    end

    mutable struct Delta
        prev_val::Float32
        function Delta()
            return new(0.0f0)
        end
    end

    @constructor begin
        audio_buffer::Buffer = Buffer(1)
        envelope_buffer::Buffer = Buffer(2)

        #Maximum of 20 grains per second
        max_num_grains::Int32 = 20
        grain_array::GrainArray = GrainArray(max_num_grains)
        for i = Int32(1) : max_num_grains
            grain_array.pitches[i] = 1.0f0
        end

        phasor::Phasor = Phasor()
        one_second_increment::Float32 = 1.0f0 / @sampleRate
        delta::Delta = Delta()

        @new(audio_buffer, envelope_buffer, max_num_grains, grain_array, phasor, one_second_increment, delta)
    end

    @perform begin
        phase::Float32 = phasor.phase

        #0 to 20
        density::Float32 = @in0(3)
        density = density > 0.0 ? density : 0.0
        density = density < 1.0 ? density : 1.0 
        density = density * max_num_grains

        length_audio_buffer::Int32 = length(audio_buffer)
        nchans_audio_buffer::Int32 = nchans(audio_buffer)
        length_envelope_buffer::Int32 = length(envelope_buffer)

        @sample begin
            new_grain::Bool = false

            if(abs(delta.prev_val - phase) > 0.5)
                new_grain = true
            end

            out_value::Float32 = 0.0f0
            @out(1) = out_value

            phase += (one_second_increment * density)
            if(phase >= 1.0f0)
                phase = 0.0f0
            end
            
            delta.prev_val = phase
        end

        phasor.phase = phase
    end
end