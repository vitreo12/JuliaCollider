t = Server.new(\server2, NetAddr("127.0.0.1", 57111));
t.boot;
s.boot;

(
~juliaReceiveIO = {
	arg targetAddr, name;
	OSCFunc({
		arg msg, time, addr;
		var julia_file, inputs, outputs;
		if(addr == targetAddr, {
			julia_file = msg[1];
			inputs = msg[2];
			outputs = msg[3];
			("Julia file: " ++ julia_file.asString).postln;
			("Inputs: " ++ inputs.asString).postln;
			("Outputs: " ++ outputs.asString).postln;
		});
	}, name
    );
};
)

//Receives on s...
~juliaReceiveIO.value(s.addr, '/Julia_IO');

s.sendMsg(\cmd, \julia_send_reply);

1000.do{s.sendMsg(\cmd, \julia_send_reply)};

//Receives on t...
~juliaReceiveIO.value(t.addr, '/Julia_IO');

t.sendMsg(\cmd, \julia_send_reply);

1000.do{t.sendMsg(\cmd, \julia_send_reply)};

s.quit;
t.quit;
