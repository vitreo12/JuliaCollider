JuliaDef {
	/* Dictionary that will store single JuliaDefs per @object.
	They will be stored in the form [server, JuliaDef] */
	classvar julia_defs_dictionary;

	var <srvr;
	var <file_path = "";
	var <name = "@No_Name";
	var <inputs = -1;
	var <input_names;
	var <outputs = -1;
	var <output_names;
	var <server_id = -1;
	var <compiled = false;

	*new {
		arg server = Server.default, path = "";
		^super.new.init(server, path);
	}

	init {
		arg server, path;
		^this.newJuliaDef(server, path);
	}

	/* This is initialized at interpreter boot. It will be valid from any server booted from the same client. */
	*initClass {
		julia_defs_dictionary = Dictionary(1000);
	}

	newJuliaDef {
		arg server, path;

		var obj_name, ins, outs, srvr_id, condition, osc_unique_id, osc_func, julia_def_to_dict, julia_def_to_dict_index;

		//Empty constructor used in retrieve function
		if(path == "__NEW_JULIADEF__", {
			^this;
		});

		if((server.class == Server).not, {
			("ERROR: JuliaDef: first argument is not a Server.").postln;
			^this;
		});

		if((server.serverRunning).not, {
			("ERROR: JuliaDef: Server is not running.").postln;
			^this;
		});

		if((path.class == String).not, {
			("ERROR: JuliaDef: second argument is not a String.").postln;
			^this;
		});

		if(path == "", {
			("ERROR: JuliaDef: second argument is an empty String.").postln;
			^this;
		});

		if((File.existsCaseSensitive(path)).not, {
			("ERROR: JuliaDef: path: \"" ++ path ++ "\" does not exist.").postln;
			^this;
		});

		//Should it be assigned later with the other variables?
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

			//[s, name.AsSymbol]
			julia_def_to_dict_index = Array.newClear(2);
			julia_def_to_dict_index[0] = server;
			julia_def_to_dict_index[1] = name.asSymbol;

			/* Assign this newly created JuliaDef to the IdentityDictionary,
			in the form [server, name] */
			julia_def_to_dict = Array.newClear(2);
			julia_def_to_dict[0] = server;
			julia_def_to_dict[1] = this; //This newly created JuliaDef

			//[s, name.AsSymbol] -> [s, JuliaDef]
			julia_defs_dictionary.put(julia_def_to_dict_index, julia_def_to_dict);

			//postln("DICT: " ++ julia_defs_dictionary);

			//Sync server right after variables assignment
			server.sync;

			//Print out
			this.query.value();
		}
	}

	//Retrieve a JuliaDef from the IdentityDictionary.
	*getJuliaDef {
		arg server, julia_def_name;

		var julia_def_to_dict_index, julia_def_from_dict;

		julia_def_to_dict_index = Array.newClear(2);
		julia_def_to_dict_index[0] = server;
		julia_def_to_dict_index[1] = julia_def_name.asSymbol;

		julia_def_from_dict = julia_defs_dictionary.at(julia_def_to_dict_index);

		if(julia_def_from_dict != nil, {
			julia_def_from_dict = julia_def_from_dict[1]; //Extract second element (first one is server) [s, JuliaDef]
		});

		^julia_def_from_dict;
	}

	query {
		var julia_def;

		if(name == "@No_Name", {
			("Warning: Empty JuliaDef").postln;
			^this;
		});

		/* Always get the JuliaDef from the Dictionary, as it could have been updated by another JuliaDef */
		julia_def = JuliaDef.getJuliaDef(srvr, name);

		if(julia_def == nil, {
			Error("Julia: invalid JuliaDef.").throw;
		});

		/* Queried JuliaDef will be the updated version from the Dictionary */
		("*** Julia @object: " ++ name ++ " ***").postln;
		("*** Server: " ++ srvr ++ " ***").postln;
		("*** ID: " ++ julia_def.server_id ++ " ***").postln;
		("*** Inputs: " ++ julia_def.inputs ++ " ***").postln;
		("*** Outputs: " ++ julia_def.outputs ++ " ***").postln;
		("*** File Path: " ++ julia_def.file_path ++ " ***").postln;
	}

	edit {
		("open \"" ++ file_path ++ "\"").unixCmd;
	}

	freeJuliaDef {
		var server, julia_object_id, julia_def_to_dict_index;

		server = srvr;
		julia_object_id = server_id;

		server.sendMsg(\cmd, "/julia_free", julia_object_id);

		//Remove [s, JuliaDef] pair from dictionary
		julia_def_to_dict_index = Array.newClear(2);
		julia_def_to_dict_index[0] = server;
		julia_def_to_dict_index[1] = name.asSymbol;
		julia_defs_dictionary.removeAt(julia_def_to_dict_index);

		//Reset state anyway, no need for response from the server.
		server_id = -1;
		inputs = -1;
		outputs = -1;
		name = "@No_Name";
		file_path = "";

		this.query.value();
	}

	/* Free a JuliaDef */
	free {
		if(server_id != -1, {
			this.freeJuliaDef;
		}, {
			"WARNING: Invalid JuliaDef to free".postln;
		});
	}

	/* Trigger recompilation of file_path */
	recompile {
		if(file_path != "", {
			this.newJuliaDef(srvr, file_path);
		}, {
			"WARNING: Invalid JuliaDef to recompile".postln;
		});
	}

	/* Update this JuliaDef if it has been recompiled by another client. Retrieve it from the server directly. */
	update {
		if(name != "@No_Name", {
			this.getCompiledJuliaDef(srvr, name);
		}, {
			"WARNING: Invalid JuliaDef to update".postln;
		});
	}

	/* Retrieve a JuliaDef from server by name, and assign it to a new JuliaDef, returning it */
	*retrieve {
		arg server, obj_name;
		^this.new(server, "__NEW_JULIADEF__").getCompiledJuliaDef(server, obj_name);
	}

	getCompiledJuliaDef {
		arg server, obj_name;
		var path, ins, outs, srvr_id, condition, osc_unique_id, osc_func, julia_def_to_dict, julia_def_to_dict_index;

		if((server.class == Server).not, {
			("ERROR: JuliaDef: first argument is not a Server.").postln;
			^this;
		});

		if((server.serverRunning).not, {
			("ERROR: JuliaDef: Server is not running.").postln;
			^this;
		});

		if((obj_name.class == Symbol).not, {
			("ERROR: JuliaDef: second argument is not a Symbol.").postln;
			^this;
		});

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
				if(msg_unpacked[0].asInteger == osc_unique_id, {
					//Test against correct command.
					if(msg_unpacked[1] == "/jl_get_julia_object_by_name", {
						//Unpack message
						srvr_id = msg_unpacked[2].asInteger;
						path = msg_unpacked[3];
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
			server.sendMsg(\cmd, "/julia_get_julia_object_by_name", osc_unique_id, obj_name);

			//Wait for positive response from the OSCFunc
			condition.hang;

			//Assign variables to JuliaDef after unhanging.
			server_id = srvr_id;
			file_path = path;
			inputs = ins;
			outputs = outs;

			//Assign name, the opposite as newJuliaDef.
			if(inputs > -1, {name = obj_name;});

			//[s, name.AsSymbol]
			julia_def_to_dict_index = Array.newClear(2);
			julia_def_to_dict_index[0] = server;
			julia_def_to_dict_index[1] = name.asSymbol;

			/* Assign this newly created JuliaDef to the IdentityDictionary,
			in the form [server, name] */
			julia_def_to_dict = Array.newClear(2);
			julia_def_to_dict[0] = server;
			julia_def_to_dict[1] = this; //This newly created JuliaDef

			//[s, name.AsSymbol] -> [s, JuliaDef]
			julia_defs_dictionary.put(julia_def_to_dict_index, julia_def_to_dict);

			//postln("DICT: " ++ julia_defs_dictionary);

			//Sync server right after variables assignment
			server.sync;

			//Print out
			this.query.value();
		}
	}

	/* List of all compiled JuliaDefs on a server */
	*getCompiledJuliaDefs {
		arg server;
		var osc_unique_id, condition, osc_func, return_array;

		if((server.class == Server).not, {
			("ERROR: JuliaDef: first argument is not a Server.").postln;
			^this;
		});

		if((server.serverRunning).not, {
			("ERROR: JuliaDef: Server is not running.").postln;
			^this;
		});

		return_array = List.newClear();

		server = server ?? Server.default;
		condition = Condition(false);

		//Unique number generator
		osc_unique_id = UniqueID.next;

		//Define the OSC receiving function
		osc_func = OSCFunc.newMatching({
			arg msg, time, addr;
			var server_addr, msg_unpacked, i;
			server_addr = server.addr;
			//If OSC comes from the right server address...
			if(addr == server_addr, {
				//Split characters at new line. (unpacking the message, effectively)
				msg_unpacked = msg[1].asString.split($\n);
				if(msg_unpacked[0].asInteger == osc_unique_id, {
					//Test against correct command.
					if(msg_unpacked[1] == "/jl_get_julia_objects_list", {

						//Add from third element onwards
						msg_unpacked.do({
							arg item, i;
							if(i > 1, {
								return_array.add(item.asSymbol);
							});
						});

						//Remove last (empty character)
						return_array.pop;

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
			server.sendMsg(\cmd, "/julia_get_julia_objects_list", osc_unique_id);

			//Wait for positive response from the OSCFunc
			condition.hang;

			("-> Compiled JuliaDefs: " ++ return_array).postln;

			//Sync server right after variables assignment
			server.sync;
		}

		//It will be empty at the moment of return, but filled when server command returns
		^return_array;
	}

	/* FUTURE */

	/* Precompile a JuliaDef in the Julia sysimg. At next boot, it will be loaded. */
	precompile {
		"WARNING: To be implemented...".postln;
	}
}

/******************************************/
/* FUTURE */
JuliaDefProxy {
	*new {
		arg server, num_inputs, num_outputs;
		^super.new.init(server, num_inputs, num_outputs);
	}

	init {
		arg server, num_inputs, num_outputs;
		"WARNING: To be implemented...".postln;
	}
}

//Turn on/off GC timed collections or force GC runs
JuliaGC {
	*new {
		arg server;
		^super.new.init(server);
	}

	init {
		arg server;
		"WARNING: To be implemented...".postln;
	}
}
/******************************************/

Julia : MultiOutUGen {
	*ar { |... args|
		var new_args, julia_def_name, julia_def_server, server_id, inputs, outputs, julia_def;

		if((args.size == 0), {
			Error("Julia: no arguments provided.").throw;
		});

		//If first argument is not a JuliaDef, return silence. Return silence immediately
		// in order not to unpack things from objects that might not be JuliaDefs...
		if((args[0].class == JuliaDef).not, {
			Error("Julia: first argument is not a JuliaDef.").throw;
		});

		//Get the name of the JuliaDef
		julia_def_name = args[0].name;

		if(julia_def_name == "@No_Name", {
			Error("Julia: invalid JuliaDef.").throw;
		});

		//Get the server of the JuliaDef
		julia_def_server = args[0].srvr;

		/* Unpack JuliaDef from the IdentityDictionary, to pick up eventual changes made by other JuliaDefs */
		julia_def = JuliaDef.getJuliaDef(julia_def_server, julia_def_name);

		if(julia_def == nil, {
			Error("Julia: invalid JuliaDef.").throw;
		});

		server_id =julia_def.server_id;
		inputs = julia_def.inputs;
		outputs = julia_def.outputs;

		if((inputs < 0) || (outputs < 0) || (server_id < 0), {
			Error("Julia: invalid JuliaDef.").throw;
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

		//new_args.postln;

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
		arg pool_size = 131072;

		if(pool_size < 131072, {
			"WARNING: Julia: minimum memory size is 131072. Using 131072.".postln;
			pool_size = 131072;
		});

		//Can't reset it here. I might have multiple server setup...
		//JuliaDef.initClass;

		Routine.run {
			//pool_size.postln;
			this.sendMsg(\cmd, "/julia_boot", pool_size);

			this.sync;
		};
	}

	bootWithJulia {
		arg pool_size = 131072;

		this.waitForBoot({
			this.bootJulia(pool_size);
		});
	}
}