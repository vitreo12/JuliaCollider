Julia.bootWithServer(s);

Julia.boot(s);

(
a = JuliaDef(s, Platform.userExtensionDir ++ "/JuliaCollider/Examples/Sine.jl");
b = JuliaDef(s, Platform.userExtensionDir ++ "/JuliaCollider/Examples/Phasor.jl");
)

(
SynthDef(\JuliaProxyTestSingle, {
	arg julia_def_sound, julia_def_modulation;

	var sound;

	sound = JuliaProxy.ar(julia_def_sound, 1, 1, DC.ar(440));

	Out.ar([0, 1], sound);

}).send(s);
)

x = Synth(\JuliaProxyTestSingle, [\julia_def_sound, a]);

a.free;
a = JuliaDef(s, Platform.userExtensionDir ++ "/JuliaCollider/Examples/Sine.jl");
a.recompile;

s.quit;

(
SynthDef(\JuliaProxyTest, {
	arg julia_def_sound, julia_def_modulation;

	var modulation, sound;

	modulation = JuliaProxy.ar(julia_def_modulation, 1, 1, DC.ar(1));

	sound = JuliaProxy.ar(julia_def_sound, 1, 1, DC.ar(440) + modulation.linlin(-1,1,-50, 50));

	Out.ar([0,1], sound);

}).send(s);
)

x = Synth(\JuliaProxyTest, [\julia_def_sound, a, \julia_def_modulation, b]);

x.set(\julia_def_sound, b);
x.set(\julia_def_sound, a);

x.set(\julia_def_modulation, a);
x.set(\julia_def_modulation, b);

a.free;
b.free;

b.query;
a.query;