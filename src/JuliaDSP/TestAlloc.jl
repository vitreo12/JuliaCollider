module TestAlloc
    
    #macro to create a struct with n number of elements of type d, initialized to value initValue
    macro create_struct(name, n, types, initValue)
        local entries = []
        local entriesConstructor = []

        for i = 1 : n
            local currentName = Symbol("var$i")
            push!(entries, :($(currentName)::$(types)))
            push!(entriesConstructor, :($(currentName) = $(initValue)))
        end

        local constructor_body = quote
            function $(name)()
                return new($(entriesConstructor...))
            end
        end

        esc(quote
            struct $name
            $(entries...)
            $(constructor_body)
            end
        end)
    end

    #create the struct
    @create_struct Test 1000 Float64 0.5

    #create an object
    function test1()
        a = Test()
    end

    #alternative call to create object
    function test2()
        a = ccall(:jl_call0, Any, (Any,), Test)
    end

end