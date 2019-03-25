#=
Interpolation functions for AbstractFloat
=#

function linear_interp(a::AbstractFloat, x1::AbstractFloat, x2::AbstractFloat)
    return x1 + (a * (x2 - x1));
end

function cubic_interp(a::AbstractFloat, x0::AbstractFloat, x1::AbstractFloat, x2::AbstractFloat, x3::AbstractFloat)
    c0::Float32 = x1
    c1::Float32 = 0.5f0 * (x2 - x0)
    c2::Float32 = x0 - (2.5f0 * x1) + (2.0f0 * x2) - (0.5f0 * x3)
    c3::Float32 = 0.5f0 * (x3 - x0) + (1.5f0 * (x1 - x2))
    return (((((c3 * a) + c2) * a) + c1) * a) + c0
end