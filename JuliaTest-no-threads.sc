//server would crash with small memory. I need to wrap RTAlloc calls
//into checking if it actually has allocated...
s.options.memSize = 65536;

(
s.waitForBoot({
	s.sendMsg(\cmd, "/julia_boot");
	s.sync;
})
)

s.sendMsg(\cmd, "/julia_GC");

a = JuliaDef(s, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/SineWave.jl");

100.do{a = JuliaDef(s, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/SineWave.jl")};

a.query;

x = {Julia.ar(440)}.play;

s.quit;