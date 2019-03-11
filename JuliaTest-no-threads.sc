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

s.sendMsg(\cmd, "/julia_query_id_dicts");

a = JuliaDef(s, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/SineWave.jl");

10.do{a = JuliaDef(s, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/SineWave.jl")};

s.sendMsg(\cmd, "/julia_test_load");

//1
{Julia.ar(a, DC.ar(440))}.play;

{SinOsc.ar(DC.ar(440))}.play;

//2
{Julia.ar(a, Julia.ar(a, DC.ar(1)) * 440) * Julia.ar(a, DC.ar(220))}.play;

{SinOsc.ar(SinOsc.ar(DC.ar(1)) * 440) * SinOsc.ar(DC.ar(220))}.play;

{LFSaw.ar(LFSaw.ar(DC.ar(1)) * 440) * LFSaw.ar(DC.ar(220))}.play;

//3
{Julia.ar(a, Julia.ar(a, DC.ar(1)) * 440)}.play;

{LFSaw.ar(LFSaw.ar(1) * 440)}.play;

(
s.bind({
	{Julia.ar(a, DC.ar(440))}.play;
	s.sendMsg(\cmd, "/julia_GC");
});
)


a.query;

s.scope;
s.quit;