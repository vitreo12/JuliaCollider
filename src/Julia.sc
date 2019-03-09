JuliaDef {
	var srvr;
	/* Only define getters to be accessed at UGen instantiation */
	var <name = "__INVALID?_NAME?__";
	var <inputs = -1;
	var <input_names;
	var <outputs = -1;
	var <output_names;
	var <server_id = -1;
	var <compiled = false;

	/* CHECK STRUCTURE OF THINGS INSIDE Buffer.sc */

	/* ADD INPUT NAMES FOR PARAMETERS */

	*new {
		arg server, path;
		^super.new.init(server, path);
	}

	init {
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
						srvr_id = msg_unpacked[2];
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
			server.sendMsg(\cmd, "/julia_load", osc_unique_id);

			//Wait for positive response from the OSCFunc
			condition.hang;

			//Assign variables to JuliaDef.
			server_id = srvr_id;
			name = obj_name;
			inputs = ins;
			outputs = outs;

			//Sync server right after variables assignment
			server.sync;

			//Print out
			this.query.value();
		}
	}

	query {
		("Server: " ++ srvr).postln;
		("Julia @object: " ++ name).postln;
		("ID: " ++ server_id).postln;
		("Inputs: " ++ inputs).postln;
		("Outputs: " ++ outputs).postln;
	}

	/* Add method to retrieve a JuliaDef from the server by name
	   It must have been evaluated first. So there could be a
	   Julia function where it includes without assigning.
	   The name retrieved is the name of the @object, so that
	   it is possible to retrieve the module from Julia quite easily, without
	   having a IdDict() with custom names, saving allocation space.*/

	/* Add free() method */

}


//Executed in SynthDef...
Julia : MultiOutUGen {
	*ar { |... args|
		var juliaDef, new_args, name, inputs, outputs, zero_output;

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
		inputs = args[0].inputs;
		outputs = args[0].outputs;

		//Check validity of JuliaDef
		if(((name == "") || (inputs < 0) || (outputs < 0)), {
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

		//New array. Make up space for 'audio' and number of outputs (first two entries)
		new_args = Array.newClear(args.size + 2);

		//Copy elements over to new_args
		args.do({
			arg item, i;
			//Shift UGen args by two. It will leave first two entries free
			new_args[i + 2] = item;
		});

		//Add 'audio' as first entry to new_args array
		new_args[0] = 'audio';
		//Add output number as second entry to new_args array.
		new_args[1] = outputs;

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
		//Assign input array to be theInputs, when "output" is removed, theInputs will just have UGens.
		inputs = theInputs;
		^this.initOutputs(outputs, rate)
	}
}