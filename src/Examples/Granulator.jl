@object Granulator begin
    @inputs 9 ("audio_buffer", "envelope_buffer", "density", "position", "position_range", "length", "length_range", "pitch_shift", "pitch_shift_range")
    @outputs 1
    
    include("$(@__DIR__)/Interpolations.jl")

    mutable struct Phasor
        phase::Float32
        rand_val::Float32
        function Phasor()
            return new(0.0, 0.0)
        end
    end

    #IF they are all specialized, they are much faster to be accessed. The compiler knows all about them.
    mutable struct GrainArray
        phase::Data{Float32, 1}
        start_position::Data{Int32, 1}
        length_samples::Data{Int32, 1}
        phase_length_increment::Data{Float32, 1}
        busy::Data{Int32, 1}
        function GrainArray(num_grains::Int32)
            phase::Data{Float32, 1} = Data(Float32, num_grains)
            start_position::Data{Int32, 1} = Data(Int32, num_grains)
            length_samples::Data{Int32, 1} = Data(Int32, num_grains)
            phase_length_increment::Data{Float32, 1} = Data(Float32, num_grains)
            busy::Data{Int32, 1} = Data(Int32, num_grains)
            return new(phase, start_position, length_samples, phase_length_increment, busy)
        end
    end

    @constructor begin
        audio_buffer::Buffer = Buffer(1)
        envelope_buffer::Buffer = Buffer(2)

        #Maximum of 20 grains per second
        max_num_grains::Int32 = 20
        grain_array::GrainArray = GrainArray(max_num_grains)
        for i = Int32(1) : max_num_grains
            grain_array.phase[i] = 0.0f0
            grain_array.start_position[i] = Int32(0)
            grain_array.length_samples[i] = Int32(0)
            grain_array.phase_length_increment[i] = 0.0f0
            grain_array.busy[i] = Int32(0)
        end

        phasor::Phasor = Phasor()
        one_over_sampleRate::Float32 = 1.0f0 / @sampleRate

        @new(audio_buffer, envelope_buffer, max_num_grains, grain_array, phasor, one_over_sampleRate)
    end

    function new_rand_val_phasor(one_over_sampleRate::Float32)
        return Float32(abs(randn(Float32)) * one_over_sampleRate * 1.5f0)
    end

    function trigger_new_grain(grain_array::GrainArray, max_num_grains::Int32, length_audio_buffer::Int32, position_samples::Int32, position_range_samples::Int32, length_samples::Int32, length_range_samples::Int32, pitch_shift::Float32, pitch_shift_range::Float32)
        for i = Int32(1) : max_num_grains
            if(grain_array.busy[i] == Int32(0))
                grain_array.busy[i] = Int32(1)
                grain_array.phase[i] = 0.0f0

                #Convert pitch to linear scale 2^(semitone / 12)
                grain_pitch::Float32 = Float32(2^((pitch_shift + (randn(Float32) * pitch_shift_range)) / 12))

                grain_position::Int32 = trunc(position_samples + (randn(Float32) * position_range_samples))
                grain_position = grain_position > 0 ? grain_position : 0
                grain_position = grain_position < length_audio_buffer ? grain_position : length_audio_buffer

                grain_length::Int32 = trunc(length_samples + (randn(Float32) * length_range_samples))
                if((grain_position + grain_length) > length_audio_buffer)
                    grain_length = length_audio_buffer - grain_position - 2 #-2 for linear interpolation (Julia counts from 1, that's why not -1)
                end

                #Calculate phase increment with pitch
                grain_phase_length_increment::Float32 = (1.0f0 / grain_length) * grain_pitch
                
                grain_array.length_samples[i] = grain_length
                grain_array.phase_length_increment[i] = grain_phase_length_increment
                grain_array.start_position[i] = grain_position
                break
            end
        end
    end

    @perform begin

        length_audio_buffer::Int32 = length(audio_buffer)
        nchans_audio_buffer::Int32 = nchans(audio_buffer)
        length_envelope_buffer::Int32 = length(envelope_buffer)

        #0 to 20
        density::Float32 = @in0(3)
        density = density > 0.0 ? density : 0.0
        density = density < 1.0 ? density : 1.0 
        density = density * max_num_grains

        #0 to 1
        position::Float32 = @in0(4)
        position = position > 0.0 ? position : 0.0
        position = position < 1.0 ? position : 1.0
        position_samples::Int32 = Int32(trunc(position * length_audio_buffer))

        #0 to 0.3333 (one third of the file)
        position_range::Float32 = @in0(5)
        position_range = position_range > 0.0 ? position_range : 0.0
        position_range = position_range < 1.0 ? position_range : 1.0
        position_range = position_range * 0.3333 #max range = 1/3 of length of file 
        position_range_samples::Int32 = Int32(trunc(position_range * length_audio_buffer))

        #0 to 1s
        length_par::Float32 = @in0(6)
        length_par = length_par > 0.0 ? length_par : 0.0
        length_par = length_par < 1.0 ? length_par : 1.0 #max one second length
        length_samples::Int32 = Int32(trunc(length_par * @sampleRate))

        #0 to 500ms
        length_range::Float32 = @in0(7)
        length_range = length_range > 0.0 ? length_range : 0.0
        length_range = length_range < 1.0 ? length_range : 1.0 
        length_range = length_range * 0.5 #max 500 ms of random range
        length_range_samples::Int32 = Int32(trunc(length_range * @sampleRate))

        #-12 to 12 semitones
        pitch_shift::Float32 = @in0(8)
        pitch_shift = pitch_shift > -12.0 ? pitch_shift : -12.0
        pitch_shift = pitch_shift < 12.0 ? pitch_shift : 12.0

        #0 to 1 semitones, scaled to 0 to 12
        pitch_shift_range::Float32 = @in0(9)
        pitch_shift_range = pitch_shift_range > 0.0 ? pitch_shift_range : 0.0
        pitch_shift_range = pitch_shift_range < 1.0 ? pitch_shift_range : 1.0
        pitch_shift_range = pitch_shift_range * 12.0f0 

        #New grain to trigger
        new_grain::Bool = false

        phase::Float32 = phasor.phase
        rand_val::Float32 = phasor.rand_val

        @sample begin
            #Trigger a new grain with a new random increment at the reset of the phasor
            if(phase >= 1.0f0)
                phase = 0.0f0
                new_grain = true
                rand_val = new_rand_val_phasor(one_over_sampleRate)
            end

            #Trigger the new grain and set its boundaries
            if(new_grain)
                trigger_new_grain(grain_array, max_num_grains, length_audio_buffer, position_samples, position_range_samples, length_samples, length_range_samples, pitch_shift, pitch_shift_range)
                new_grain = false
            end

            out_value::Float32 = 0.0f0
            
            #Advance all active grains
            for i = Int32(1) : max_num_grains
                #grain_array_busy::Data{Int32, 1} = grain_array.busy
                if(grain_array.busy[i] == Int32(1))
                    
                    #Retrieve values for current grain
                    phase_grain::Float32 = grain_array.phase[i]
                    start_pos_grain::Int32 = grain_array.start_position[i]
                    length_samples_grain::Int32 = grain_array.length_samples[i]
                    phase_length_increment::Float32 = grain_array.phase_length_increment[i]

                    #Scale phasor for audio buffer.
                    phasor_audio_buffer_float::Float32 = (phase_grain * Float32(length_samples_grain)) + Float32(start_pos_grain)
                    phasor_audio_buffer::Int32 = Int32(trunc(phasor_audio_buffer_float))
                    
                    #Scale phasor for envelope buffer.
                    phasor_envelope_buffer_float::Float32 = phase_grain * Float32(length_envelope_buffer - 1)
                    phasor_envelope_buffer::Int32 = Int32(trunc(phasor_envelope_buffer_float))

                    buffer_value_1::Float32 = audio_buffer[phasor_audio_buffer + Int32(1)]        #Julia counts from one
                    buffer_value_2::Float32 = audio_buffer[phasor_audio_buffer + Int32(2)]        #Next value to linear interpolate
                    frac_buffer_value::Float32 = phasor_audio_buffer_float - phasor_audio_buffer
                    buffer_value::Float32 = linear_interp(frac_buffer_value, buffer_value_1, buffer_value_2)
                    
                    envelope_value_1::Float32 = envelope_buffer[phasor_envelope_buffer + Int32(1)] #Julia counts from 1
                    envelope_value_2::Float32 = envelope_buffer[phasor_envelope_buffer + Int32(2)] #Julia counts from 1
                    frac_envelope_value::Float32 = phasor_envelope_buffer_float - phasor_envelope_buffer
                    envelope_value::Float32 = linear_interp(frac_envelope_value, envelope_value_1, envelope_value_2)

                    #Accumulate results of all grains
                    out_value += (buffer_value * envelope_value)
                    
                    #Advance phasor for this grain according to length
                    phase_grain += phase_length_increment

                    grain_array.phase[i] = phase_grain

                    #Release grain if cycle of the phasor is finished
                    if(phase_grain >= 1.0)
                        grain_array.busy[i] = Int32(0)
                    end
                end
            end

            @out(1) = out_value

            #Advance trigger phasor
            phase += (one_over_sampleRate + rand_val) * density
        end

        phasor.phase = phase
        phasor.rand_val = rand_val
    end
end