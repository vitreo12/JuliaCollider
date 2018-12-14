~sinePath = Platform.userExtensionDir++"/JuliaCollider/Sine.jl";

//edit the file in whatever text editor
Julia.edit(~sinePath);

//Add to server. Including, parsing and precompiling on NRT thread.
//ALSO:
//The server will have a hash table with all symbol stored with their
//Julia function pointers to construct, perform dsp, and destruct,
//and inputs and outputs:
//JuliaHash(s.addr, \Sine, construct_ptr, perform_ptr, destruct_ptr, 1, 1)
Julia.add(s, \Sine, ~sinePath);

//Here, at the instantiation of the Julia.ar (in the Julia class, before
//it sends to the server), there will be the OSC retrieval of inputs and
//outputs to correctly use the right Julia function (in case it needs MulOut).
//This is done by sending a request to the server to an async function which
//will send an OSC message to a specific receiver, called with the name of the symbol
//followed by "Inputs" or "Outputs": e.g. "JuliaSineInputs". Gotta find a way
//to just receive from a specific server, as there could be more than one loading
//Julia with same names for different files.
SynthDef(\JuliaSound, {
	arg frequency = 440;
    var signal = Julia.ar(\Sine, frequency);
    Out.ar(0, signal);
}).add;

x = Synth(\JuliaSound);

//Simulate the message that the server would run to return the IO for /Sine to sclang
{SendReply.kr(Impulse.kr(3), '/JuliaSineInputsOutputs', [4, 2])}.play(s);

//Receiver function which would then receive the values. It will be limited to the server
//that they were sent from.
(
~juliaReceiveIO = {
	arg targetAddr, name;
	OSCFunc({
		arg msg, time, addr;
		if(addr == targetAddr, {
			msg.postln
		});
	}, name
    );
};
)

//Actually instantiate with the correct name.
~juliaReceiveIO.value(s.addr, '/JuliaSineInputsOutputs');