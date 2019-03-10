//server would crash with small memory. I need to wrap RTAlloc calls
//into checking if it actually has allocated...
s.options.memSize = 131072;
s.options.sampleRate = 48000;

(
s.waitForBoot({
	s.sendMsg(\cmd, "/julia_boot");
	s.sync;
})
)

s.sendMsg(\cmd, "/julia_GC");

a = JuliaDef(s, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/SineWave.jl");

s.sendMsg(\cmd, "/julia_test_load");

x = {Julia.ar(a, DC.ar(240))}.play;

100.do{a = JuliaDef(s, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/SineWave.jl")};

a.query;

s.quit;