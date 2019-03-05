module Sine_DSP

    mutable struct Phase_Increment
        phase::Float64
        function Phase_Increment()
            return new(0.0)
        end
    end

    struct Sine
        p::Phase_Increment
        function Sine()
            p::Phase_Increment = Phase_Increment()
            return new(p)
        end
    end

    function dummy_alloc()
        a = zeros(10)
        println(a)
        a = nothing
    end

    function test_outs(numChannels::Int64, bufSize::Int64, outs::Vector{Vector{Float32}})
        for i = 1 : numChannels
            for y = 1 : bufSize
                outs[i][y] = Float32(y)
            end
        end
    end

    function test_outs_ptr(numChannels::Int64, bufSize::Int64, outs::Ptr{Ptr{Float32}})
        for i = 1 : numChannels
            for y = 1 : bufSize
                unsafe_store!(unsafe_load(outs, i), Float32(y), y)
            end
        end
    end

    #=
    #tests for speed:

    using BenchmarkTools

    function alloc_ptr_ptr_float32(num_chans, buf_size)
        ptr::Ptr{Ptr{Float32}} = Libc.malloc(num_chans * sizeof(Ptr{Float32}))
            for i = 1 : num_chans
            buffer::Ptr{Float32} = Libc.malloc(buf_size * sizeof(Float32))
            unsafe_store!(ptr, buffer, i)
        end
        return ptr
    end

    function alloc_vec_vec_float32(num_chans, buf_size)
        ptr = Vector{Vector{Float32}}(undef, num_chans)
            for i = 1 : num_chans
            ptr[i] =  zeros(Float32, buf_size)
            end
        return ptr
    end

    function test_outs(numChannels::Int64, bufSize::Int64, outs::Vector{Vector{Float32}})
        for i = 1 : numChannels
            this_chan::Vector{Float32} = outs[i]
            for y = 1 : bufSize
                this_chan[y] = Float32(y)
            end
        end
    end

    function test_outs_ptr(numChannels::Int64, bufSize::Int64, outs::Ptr{Ptr{Float32}})
        for i = 1 : numChannels
            this_chan::Ptr{Float32} = unsafe_load(outs, i)
            for y = 1 : bufSize
                unsafe_store!(this_chan, Float32(y), y)
            end
        end
    end

    a = alloc_ptr_ptr_float32(32, 512)
    b = alloc_vec_vec_float32(32, 512)

    @benchmark test_outs_ptr(32, 512, a)
    @benchmark test_outs(32, 512, b)

    =#

    function perform(unit::Sine, sample_rate::Float64, vector_size::Int32, output_vector::Vector{Float32}, frequency::Float64)
        @inbounds for i::Int64 = 1 : vector_size
            phase::Float64 = unit.p.phase

            if(phase >= 1.0)
                phase = 0.0
            end

            output_vector[i] = cos(phase * 2pi) * 0.1

            phase += frequency / (sample_rate - 1)
            unit.p.phase = phase
        end
    end

    function destructor(unit::Sine) end
    
end
