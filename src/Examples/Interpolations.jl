#=
Interpolation functions for Float32
=#

function linear_interp(a::Float32, x1::Float32, x2::Float32)
    return x1 + (a * (x2 - x1));
end

function cubic_interp(a::Float32, x0::Float32, x1::Float32, x2::Float32, x3::Float32)
    c0::Float32 = x1
    c1::Float32 = 0.5f0 * (x2 - x0)
    c2::Float32 = x0 - (2.5f0 * x1) + (2.0f0 * x2) - (0.5f0 * x3)
    c3::Float32 = 0.5f0 * (x3 - x0) + (1.5f0 * (x1 - x2))
    return (((((c3 * a) + c2) * a) + c1) * a) + c0
end