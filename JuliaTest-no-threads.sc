s.options.memSize = 131072;

s.bootWithJulia;

p = Platform.resourceDir +/+ "sounds/a11wlk01.wav";
b = Buffer.read(s, p);
b.free;

s.quitWithJulia;

s.sendMsg(\cmd, "/julia_query_id_dicts");

a = JuliaDef(s, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/SineWave.jl");

a.query;
a.recompile;
a.free;

c = JuliaDef(s, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/Phasor.jl");

c.query;
c.recompile;
c.free;

10.do{a = JuliaDef(s, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/SineWave.jl")};

(
Routine.run{
	loop{
		s.sendMsg(\cmd, "/julia_GC");
		1.wait;
	}
};
)

(
Routine.run{
	loop{
		s.sendMsg(\cmd, "/julia_total_free_memory");
		2.wait;
	}
};
)

//1
{Julia.ar(a, DC.ar(0.25), DC.ar(b))}.play;

{Julia.ar(c, DC.ar(100))}.play

50.do{{Julia.ar(c, DC.ar(100)) / 50}.play};

{SinOsc.ar(DC.ar(440))}.play;

//2
{Julia.ar(a, Julia.ar(a, DC.ar(1)) * 440) * Julia.ar(a, DC.ar(220))}.play;

{SinOsc.ar(SinOsc.ar(DC.ar(1)) * 440) * SinOsc.ar(DC.ar(220))}.play;

{LFSaw.ar(LFSaw.ar(DC.ar(1)) * 440) * LFSaw.ar(DC.ar(220))}.play;

//3
{Julia.ar(a, Julia.ar(a, DC.ar(1)) * 440)}.play;

{LFSaw.ar(LFSaw.ar(1) * 440)}.play;

50.do{{Julia.ar(a, DC.ar(1.0.rand), DC.ar(b)) / 50}.play};

//Multiple ins/outs
(
SynthDef(\JuliaSine, {
	Out.ar(0, Julia.ar(a, DC.ar(440), DC.ar(0.5)));
}).add;
)

Synth(\JuliaSine)

(
s.bind({
	{Julia.ar(a, DC.ar(440))}.play;
	s.sendMsg(\cmd, "/julia_GC");
});
)

(
s.bind({
	b = JuliaDef(s, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/Phasor.jl");
	s.sendMsg(\cmd, "/julia_GC");
})
)

/***********************************************************/
                   /* SECOND SERVER */
/***********************************************************/
t = Server.new(\server2, NetAddr("127.0.0.1", 57111));

t.options.memSize = 131072;

t.bootWithJulia;

t.quitWithJulia;

b = JuliaDef(t, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/SineWave.jl");

b.query;

10.do{b = JuliaDef(t, "/Users/francescocameli/Library/Application Support/SuperCollider/Extensions/Julia/julia/JuliaObjects/SineWave.jl")};

//1
{Julia.ar(b, DC.ar(440))}.play(t);

{SinOsc.ar(DC.ar(440))}.play(t);

//2
{Julia.ar(b, Julia.ar(b, DC.ar(1)) * 440) * Julia.ar(b, DC.ar(220))}.play(t);

{SinOsc.ar(SinOsc.ar(DC.ar(1)) * 440) * SinOsc.ar(DC.ar(220))}.play(t);

{LFSaw.ar(LFSaw.ar(DC.ar(1)) * 440) * LFSaw.ar(DC.ar(220))}.play(t);

//3
{Julia.ar(b, Julia.ar(b, DC.ar(1)) * 440)}.play(t);

{LFSaw.ar(LFSaw.ar(1) * 440)}.play(t);

(
t.bind({
	{Julia.ar(b, DC.ar(440))}.play;
	t.sendMsg(\cmd, "/julia_GC");
});
)