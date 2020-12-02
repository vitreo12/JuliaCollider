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
	var <unique_id = -1;
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
			("ERROR: JuliaDef: first argument must be a Server.").postln;
			^this;
		});

		if((server.serverRunning).not, {
			("ERROR: JuliaDef: Server is not running.").postln;
			^this;
		});

		if((path.class == String).not, {
			("ERROR: JuliaDef: second argument must be a String.").postln;
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
			unique_id = srvr_id;
			name = obj_name;
			inputs = ins;
			outputs = outs;

			/* Add JuliaDef to IdentityDictionary, regardless if compilation was successful or not,
			so that it's still possible to run the "recompile" method on it. */

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

			//If inputs is more than -1, it means the compilation has been successful.
			if(inputs > -1, {
				//Only add file_path if the compilation was successful
				file_path = path;
				compiled = true;

				("-> Compilation successful: ").postln;
			}, {
				compiled = false;
				("-> Failed Compilation: ").postln;
			}
			);

			//Sync server right after variables assignment
			server.sync;

			//Print out
			this.query.value();
		}
	}

	/* Retrieve a JuliaDef from the IdentityDictionary. */
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
		("*** Compiled : " ++ compiled ++ " ***").postln;
		("*** Server: " ++ srvr ++ " ***").postln;
		("*** ID: " ++ julia_def.unique_id ++ " ***").postln;
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
		julia_object_id = unique_id;

		server.sendMsg(\cmd, "/julia_free", julia_object_id);

		//Remove [s, JuliaDef] pair from dictionary
		julia_def_to_dict_index = Array.newClear(2);
		julia_def_to_dict_index[0] = server;
		julia_def_to_dict_index[1] = name.asSymbol;
		julia_defs_dictionary.removeAt(julia_def_to_dict_index);

		//Reset state anyway, no need for response from the server.
		unique_id = -1;
		inputs = -1;
		outputs = -1;
		name = "@No_Name";
		file_path = "";
		compiled = false;

		this.query.value();
	}

	/* Free a JuliaDef */
	free {
		if(unique_id != -1, {
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
			this.getCompiledJuliaDef(srvr, name.asSymbol);
		}, {
			"WARNING: Invalid JuliaDef to update".postln;
		});
	}

	/* Retrieve a JuliaDef from server by name, and assign it to a new JuliaDef, returning it */
	*retrieve {
		arg server, obj_name;
		^this.new(server, "__NEW_JULIADEF__").getCompiledJuliaDef(server, obj_name.asSymbol); //Create a dummy JuliaDef
	}

	getCompiledJuliaDef {
		arg server, obj_name;
		var path, ins, outs, srvr_id, condition, osc_unique_id, osc_func, julia_def_to_dict, julia_def_to_dict_index;

		if((server.class == Server).not, {
			("ERROR: JuliaDef: first argument must be a Server.").postln;
			^this;
		});

		if((server.serverRunning).not, {
			("ERROR: JuliaDef: Server is not running.").postln;
			^this;
		});

		if((obj_name.class == Symbol).not, {
			("ERROR: JuliaDef: second argument must be a Symbol.").postln;
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
			unique_id = srvr_id;
			file_path = path;
			inputs = ins;
			outputs = outs;

			/* Update the IdentityDictionary entry aswell */

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

			//If inputs is more than -1, it means that the retrieved JuliaDef is compiled.
			if(inputs > -1, {
				name = obj_name;
			});

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

		//It will be empty at the moment of return, but filled when server command returns.
		^return_array;
	}

	/* Used to pass a JuliaDef to a JuliaProxy */
	//Used when passed to a UGen as input. (Retrieved from Buffer, which uses this.bufnum).
	//This will be bypassed when using normal Julia.ar method, as the JuliaDef input there is a
	//direct pointer to a variable in the SuperCollider environment.
	isValidUGenInput { ^true }
	asUGenInput { ^this.unique_id }
	asControlInput { ^this.unique_id }

	/* Precompile a JuliaDef in the Julia sysimg. At next boot, it will be loaded. */
	precompile {
		"WARNING: To be implemented...".postln;
	}
}

/******************************************/
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
		var new_args, is_julia_proxy = false, julia_def_name, julia_def_server, julia_object_id, inputs, outputs, julia_def;

		if((args.size == 0), {
			Error("Julia: no arguments provided.").throw;
		});

		//For JuliaProxy args will be in the form of
		// a nested array, like so: [["__JuliaProxy__", 1, 2, ....]]. Need to unpack them.
		//I can just check if the class is array (and not some superclass) as it's created
		//as array in the JuliaProxy.ar method. (args = Array.newClear... etc...)
		if((args.size == 1 && args[0].class == Array), {
			if((args[0][0] == "__JuliaProxy__"), {
				is_julia_proxy = true;
				args = args[0]; //Actually unpack args and assign them, retrieving the array inside the [[ ]] args
			});
		});

		//args.postln;

		if(is_julia_proxy.not, {

			/********************/
			/* NOT A JuliaProxy */
			/********************/

			//If first argument is not a JuliaDef, error
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

			/* Unpack JuliaDef from the IdentityDictionary, to pick up eventual changes made
			when recompiling the same JuliaDef from a different variable. */
			julia_def = JuliaDef.getJuliaDef(julia_def_server, julia_def_name);

			if(julia_def == nil, {
				Error("Julia: invalid JuliaDef.").throw;
			});

			julia_object_id = julia_def.unique_id;
			inputs = julia_def.inputs;
			outputs = julia_def.outputs;

			if((inputs < 0) || (inputs > 32) || (outputs < 0) || (outputs > 32) || (julia_object_id < 0), {
				Error("Julia: invalid JuliaDef.").throw;
			});

			//Remove the JuliaDef from args
			args.removeAt(0);

		}, {

			/****************/
			/* A JuliaProxy */
			/****************/

			julia_object_id = args[1];

			inputs = args[2];
			if((inputs < 0) || (inputs > 32), {
				Error("JuliaProxy: invalid number of inputs.").throw;
			});

			outputs = args[3];
			if((outputs < 0) || (outputs > 32), {
				Error("JuliaProxy: invalid number of inputs.").throw;
			});

			//Remove the "__JuliaProxy__" from args
			args.removeAt(0);

			//Remove the julia_def input number from args, now at pos 0
			args.removeAt(0);

			//Remove inputs, now at pos 0
			args.removeAt(0);

			//Remove outputs, now at pos 0
			args.removeAt(0);
		});

		//Check rates of inputs. Right now, only audio rate is supported
		args.do({
			arg item, i;
			if(item.rate != 'audio', {
				if(is_julia_proxy, {
					Error("Julia '%': UGen argument % is not audio rate".format(julia_def_name.asString, (i+1).asString)).throw;
				}, {
					Error("JuliaProxy: UGen argument % is not audio rate".format((i+1).asString)).throw;
				});
			});
		});

		//Check number of inputs
		if((args.size != inputs), {
			if(is_julia_proxy, {
				Error("Julia '%': wrong number of UGen inputs: %. Expected %".format(julia_def_name.asString, (args.size).asString, inputs.asString)).throw;
			}, {
				Error("JuliaProxy : wrong number of UGen inputs: %. Expected %".format((args.size).asString, inputs.asString)).throw;
			});
		});

		//New array. Make up space for 'audio', number of outputs and julia_object_id (first three entries)
		new_args = Array.newClear(args.size + 3);

		//Copy elements over to new_args
		args.do({
			arg item, i;
			//Shift UGen args by three. It will leave first three entries free
			new_args[i + 3] = item;
		});

		//Add 'audio' as first entry to new_args array
		new_args[0] = 'audio';
		//Add output number as second entry to new_args array.
		new_args[1] = outputs;
		//Add julia_object_id as third entry to new_args array
		new_args[2] = julia_object_id;

		//new_args.postln;

		//Pass array args for initialization.
		^this.multiNewList(new_args);
	}

	*kr {^this.shouldNotImplement(thisMethod)}

	init { arg ... theInputs;
		var outputs = 0;

		/* At this stage, 'audio' as already been removed as first element in array and
		assigned to class variable "rate". Now, the number of outputs is first element.
		Retrieve it, and remove it from the array. */
		outputs = theInputs[0];
		theInputs.removeAt(0);

		/* Assign inputs array of class to be theInputs, when "outputs" is removed, theInputs
		will have the julia_object_id as first input, then the UGens arguments. */
		inputs = theInputs;
		^this.initOutputs(outputs, rate)
	}

	*boot {
		arg server, pool_size = 131072;

		if((server.class == Server).not, {
			("ERROR: Julia: first argument is not a Server.").postln;
			^this;
		});

		if(pool_size < 131072, {
			"WARNING: Julia: minimum memory size is 131072. Using 131072.".postln;
			pool_size = 131072;
		});

		//Can't reset it here. I might have multiple server setup...
		//JuliaDef.initClass;

		Routine.run {
			server.sendMsg(\cmd, "/julia_boot", pool_size);

			server.sync;
		};
	}

	*bootWithServer {
		arg server, pool_size = 131072;

		if((server.class == Server).not, {
			("ERROR: Julia: first argument is not a Server.").postln;
			^this;
		});

		server.waitForBoot({
			this.boot(server, pool_size);
		});
	}

	/* FUTURE FEATURE */
	*runtimeMode {
		arg server, new_mode;

		if((server.class == Server).not, {
			("ERROR: Julia runtimeMode: first argument is not a Server.").postln;
			^this;
		});

		if((new_mode.class == String).not, {
			("ERROR: Julia runtimeMode: second argument is not a String.").postln;
			^this;
		});

		if((new_mode != "perform") && (new_mode != "debug"), {
			("ERROR: Julia runtimeMode: invalid mode.").postln;
			^this;
		});

		server = server ?? Server.default;

		server.sendMsg(\cmd, "/julia_set_perform_debug_mode", new_mode);
	}
}

JuliaProxy : MultiOutUGen {
	*ar { |... args|

		//args are to be in this format: [julia_def, inputs, outputs, UGens...]

		var new_args = new_args = Array.newClear(args.size + 1);

		//Copy elements over to new_args
		args.do({
			arg item, i;

			//Shift args by 1. It will leave first entry free
			new_args[i + 1] = item;
		});

		new_args[0] = "__JuliaProxy__";

		//new_args will look like:
		//["__JuliaProxy__", inputs, outputs, UGens...]

		^Julia.ar(new_args);

	}
}