JuliaDef {
	/* Only define getters  for variables to be accessed at Julia UGen instantiation */
	var srvr;
	var file_path = "";
	var <name = "@No_Name";
	var <inputs = -1;
	var <input_names;
	var <outputs = -1;
	var <output_names;
	var <server_id = -1;
	var <compiled = false;

	*new {
		arg server, path;
		^super.new.init(server, path);
	}

	init {
		arg server, path;
		^this.newJuliaDef(server, path);
	}

	newJuliaDef {
		arg server, path;
		var obj_name, ins, outs, srvr_id, condition, osc_unique_id, osc_func;

		srvr = server ?? Server.default;

		condition = Condition(false);

		//Unique number generator
		osc_unique_id = UniqueID.next;

		//Define the OSC receiving function
		osc_func = OSCFunc.newMatching({
			arg msg, time, addr;
			var server_addr, msg_unpacked;
			server_addr = server.addr;
			//If OSC comes from the right server address...
			if(addr == server_addr, {
				//Split characters at new line. (unpacking the message, effectively)
				msg_unpacked = msg[1].asString.split($\n);
				/* Test against correct unique id. The unique id should be sent back even if failing
				to execute a Julia file, in order to still free this OSCFunc.
				It would send: -1 to inputs and outputs and "" to obj_name */
				if(msg_unpacked[0].asInteger == osc_unique_id, {
					//Test against correct command.
					if(msg_unpacked[1] == "/jl_load", {
						//Unpack message
						srvr_id = msg_unpacked[2].asInteger;
						obj_name = msg_unpacked[3];
						ins = msg_unpacked[4].asInteger;
						outs = msg_unpacked[5].asInteger;

						//Unhang condition
						condition.unhang;

						//Free the OSCFunc. It's not possible to use .oneShot, as,
						//if it doesn't pick correct ID, it would still be deleted.
						osc_func.free;
					});
				});
			});
		}, '/done'
		);

		Routine.run {
			//Tell the server to send an OSC message. Its "/done" msg will be received by the OSCFunc.
			//Send the UniqueID number aswell to be then re-sent back and parsed in OSCFunc.
			server.sendMsg(\cmd, "/julia_load", osc_unique_id, path);

			//Wait for positive response from the OSCFunc
			condition.hang;

			//Assign variables to JuliaDef after unhanging.
			server_id = srvr_id;
			name = obj_name;
			inputs = ins;
			outputs = outs;

			//Add file_path only on positive response of server (inputs > -1)
			if(inputs > -1, {file_path = path;});

			//Sync server right after variables assignment
			server.sync;

			//Print out
			this.query.value();
		}
	}

	query {
		("-> Julia @object: " ++ name).postln;
		("-> Server: " ++ srvr).postln;
		("-> ID: " ++ server_id).postln;
		("-> Inputs: " ++ inputs).postln;
		("-> Outputs: " ++ outputs).postln;
		("-> File Path: " ++ file_path).postln;
	}

	/* Trigger recompilation of file_path */
	recompile {

	}

	/* Replaces file_path , frees the previous one and recompiles the new one */
	replace {

	}

	/* Retrieve a JuliaDef from server by name */
	retrieve {

	}

	/* Free a JuliaDef */
	free {

	}

	/* List of all compiled JuliaDefs on the server */
	getCompiledJuliaDefs {

	}

	/* Precompile a JuliaDef in the Julia sysimg. At next boot, it will be loaded. */
	precompile {

	}
}

JuliaDefProxy {
	*new {
		arg server, num_inputs, num_outputs;
		^super.new.init(server, num_inputs, num_outputs);
	}

	init {
		arg server, num_inputs, num_outputs;
	}
}

//Turn on/off GC timed collections or force GC runs
//Register a GC call for CmdPeriod.run
JuliaGC {

}

//Executed in SynthDef...
Julia : MultiOutUGen {
	*ar { |... args|
		var new_args, name, server_id, inputs, outputs, zero_output;

		if((args.size == 0), {
			Error("Julia: no arguments provided.").throw;
		});

		//If first argument is not a JuliaDef, return silence. Return silence immediately
		// in order not to unpack things from objects that might not be JuliaDefs...
		if((args[0].class == JuliaDef).not, {
			Error("Julia: first argument is not a JuliaDef.").throw
		});

		//Unpack JuliaDef
		name = args[0].name;
		server_id = args[0].server_id;
		inputs = args[0].inputs;
		outputs = args[0].outputs;

		//Check validity of JuliaDef
		if(((name == "@No_Name") || (inputs < 0) || (outputs < 0) || (server_id < 0)), {
			Error("Julia: invalid JuliaDef.").throw
		});

		//Remove the JuliaDef from args
		args.removeAt(0);

		//Check rates of inputs. Right now, only audio rate is supported
		args.do({
			arg item, i;
			if(item.rate != 'audio', {
				Error("Julia '%': argument % is not audio rate".format(name.asString, (i+1).asString)).throw;
			});
		});

		//Check number of inputs
		if((args.size != inputs), {
			Error("Julia '%': wrong number of inputs: %. Expected %".format(name.asString, (args.size).asString, inputs.asString)).throw;
		});

		//New array. Make up space for 'audio', number of outputs and server_id (first three entries)
		new_args = Array.newClear(args.size + 3);

		//Copy elements over to new_args
		args.do({
			arg item, i;
			//Shift UGen args by three. It will leave first two entries free
			new_args[i + 3] = item;
		});

		//Add 'audio' as first entry to new_args array
		new_args[0] = 'audio';
		//Add output number as second entry to new_args array.
		new_args[1] = outputs;
		//Add server_id as third entry to new_args array
		new_args[2] = server_id;

		new_args.postln;

		//Pass array args for initialization.
		^this.multiNewList(new_args);
	}

	*kr {^this.shouldNotImplement(thisMethod)}

	init{ arg ... theInputs;
		var outputs = 0;
		/* At this stage, 'audio' as already been removed as first element in array and
		assigned to class variable "rate". Now "outputs" is first element.
		Retrieve it, and remove it from the array. */
		outputs = theInputs[0];
		theInputs.removeAt(0);
		/*Assign input array to be theInputs, when "output" is removed, theInputs
		will have the server_id as first input, then UGens. */
		inputs = theInputs;
		^this.initOutputs(outputs, rate)
	}
}

+ Server {
	bootJulia {
		if(this.options.memSize < 65536, {
			("ERROR: Could not boot Julia: Minimum server.options.memSize must be of at least 65536.").postln;
		}, {
			Routine.run {
				this.sendMsg(\cmd, "/julia_boot");
				this.sync;
			}
		});
	}

	bootWithJulia {
		this.waitForBoot({
			this.bootJulia;
		});
	}

	quitWithJulia {
		Routine.run {
			this.sendMsg(\cmd, "/julia_quit");
			this.sync;
			this.quit;
		};
	}
}