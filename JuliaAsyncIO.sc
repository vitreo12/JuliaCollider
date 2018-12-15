t = Server.new(\server2, NetAddr("127.0.0.1", 57111));
t.boot;
s.boot;

(
~juliaReceiveIO = {
	arg server, name;
	OSCFunc({
		arg msg, time, addr;
		var julia_file, target_addr, inputs, outputs;
		target_addr = server.addr;
		if(addr == target_addr, {
			julia_file = msg[1];
			inputs = msg[2];
			outputs = msg[3];
			("Server: " ++ server.name.asString).postln;
			("Julia file: " ++ julia_file.asString).postln;
			("Inputs: " ++ inputs.asString).postln;
			("Outputs: " ++ outputs.asString).postln;
		});
	}, name
    );
};
)

//Receives on s...
~juliaReceiveIO.value(s, '/Julia_IO');

s.sendMsg(\cmd, \julia_send_reply);

//Test functions with audio to check it it drops...
{SinOsc.ar()}.play(s);

1000.do{s.sendMsg(\cmd, \julia_send_reply)};

//Receives on t...
~juliaReceiveIO.value(t, '/Julia_IO');

t.sendMsg(\cmd, \julia_send_reply);

//Test functions with audio to check it it drops...
{SinOsc.ar()}.play(t);

1000.do{t.sendMsg(\cmd, \julia_send_reply)};

s.quit;
t.quit;