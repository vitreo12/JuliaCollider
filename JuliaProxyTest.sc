//scsynth -u 57111 -S 48000 -a 1024 -i 2 -o 2 -m 65536 -b 1024 -R 0 -C 0 -l 1

(
//Set "s" to the port we booted scsynth on.
Server.default = Server(\BootedServer, NetAddr("127.0.0.1", 57111));

//Set options to the same as the already booted server. The most important is maxLogins.
s.options.numAudioBusChannels = 1024;
s.options.numBuffers = 1024;
s.options.memSize = 65536;
s.options.maxLogins = 1;

//Connect to the server
s.startAliveThread;
)

Julia.bootWithServer(s);

Julia.boot(s);

a = JuliaDef(s, Platform.userExtensionDir ++ "/JuliaCollider/Examples/Sine.jl");

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
a = JuliaDef(s, Platform.userExtensionDir ++ "/JuliaCollider/Examples/Sine.jl");
b = JuliaDef(s, Platform.userExtensionDir ++ "/JuliaCollider/Examples/Saw.jl");
)

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

x.set(\julia_def_modulation, a);

a.free;
b.free;

b.query;
a.query;