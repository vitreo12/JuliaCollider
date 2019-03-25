s.options.sampleRate = 48000;

s.bootWithJulia;

p = Platform.resourceDir +/+ "sounds/a11wlk01.wav";
b = Buffer.read(s, p);
b.free;

s.quitWithJulia;

s.sendMsg(\cmd, "/julia_query_id_dicts");

s.sendMsg(\cmd, "/julia_test_alloc_pool_safe");

a = JuliaDef(s, Platform.userExtensionDir ++ "/JuliaCollider/Examples/Sine.jl");

a.edit;
a.query;
a.recompile;
a.free;

//It's async.
d = JuliaDef.getCompiledJuliaDefs(s);

//Now t will follow "Sine". Recompiling one will recompile the other.
t = JuliaDef.retrieve(s, d[0]);
t.recompile;
t.query;

c = JuliaDef(s, Platform.userExtensionDir ++ "/JuliaCollider/Examples/Phasor.jl");

c.edit;
c.query;
c.recompile;
c.free;

d = JuliaDef(s, Platform.userExtensionDir ++ "/JuliaCollider/Examples/AnalogDelay.jl");

d.edit;
d.query;
d.recompile;
d.free;

h = JuliaDef(s, Platform.userExtensionDir ++ "/JuliaCollider/Examples/Saw.jl");

h.edit;
h.query;
h.recompile;
h.free;

p = Platform.resourceDir +/+ "sounds/a11wlk01.wav";
b = Buffer.read(s, p);
b.query
~hannWindow = Buffer.loadCollection(s, Signal.hanningWindow(1024));
~hannWindow.plot

k = JuliaDef(s, Platform.userExtensionDir ++ "/JuliaCollider/Examples/Granulator.jl");

k.edit;
k.query;
k.recompile;
k.free;

{Julia.ar(k, DC.ar(b), DC.ar(~hannWindow), DC.ar(1), DC.ar(0.5), DC.ar(1), DC.ar(1), DC.ar(1), DC.ar(0), DC.ar(0.5))}.play

{Julia.ar(k, DC.ar(b), DC.ar(~hannWindow), DC.ar(1), LFNoise1.ar(1).linlin(-1,1,0,1), DC.ar(0), DC.ar(0.5), DC.ar(1), DC.ar(0), DC.ar(0.5))}.play

s.scope

10.do{a.recompile};

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
{Julia.ar(a, DC.ar(200))}.play;

{Julia.ar(c, DC.ar(100))}.play

50.do{{Julia.ar(c, DC.ar(100)) / 50}.play};

{SinOsc.ar(DC.ar(440))}.play;

(
{
	var noise = PinkNoise.ar(EnvGen.kr(Env.perc, Impulse.kr(1)));
	Julia.ar(d, noise, DC.ar(1.0), DC.ar(0.2), DC.ar(0.9), DC.ar(0.8));
}.play
)

{Julia.ar(h, SinOsc.ar(1).linlin(-1,1,20,300))}.play

{Saw.ar(-200)}.play

{Julia.ar(d, Saw.ar(500), DC.ar(1.0), LFNoise1.ar(1).linlin(-1,1,0.1, 0.6), DC.ar(0.5), DC.ar(1.0))}.play

{DelayN.ar(LFSaw.ar(1), DC.ar(1.0), DC.ar(0.2))}.play


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