Julia : UGen {
	*ar {
		//args
		 arg in1 = 440.0;
		//multiNew
		^this.multiNew('audio', in1);
	}
	//mulOut
}

+ Server {
	//Instance methods: "this" refers to instance, not to class.

	bootWithJulia {
		this.waitForBoot({
			this.sendMsg("/cmd", "julia_boot");
		});
	}

	//overwrite quit function for Julia unload
	quit { |onComplete, onFailure, watchShutDown = true|
		var func;
		var condition;

		//quit Julia
		Routine{
			addr.sendMsg("/cmd", "julia_quit");
			this.sync;
			addr.sendMsg("/quit");
		}.play;

		if(watchShutDown and: { this.unresponsive }) {
			"Server '%' was unresponsive. Quitting anyway.".format(name).postln;
			watchShutDown = false;
		};

		if(options.protocol == \tcp) {
			statusWatcher.quit({ addr.tryDisconnectTCP(onComplete, onFailure) }, nil, watchShutDown);
		} {
			statusWatcher.quit(onComplete, onFailure, watchShutDown);
		};

		if(inProcess) {
			this.quitInProcess;
			"Internal server has quit.".postln;
		} {
			"'/quit' message sent to server '%'.".format(name).postln;
		};

		// let server process reset pid to nil!
		// pid = nil;
		sendQuit = nil;
		maxNumClients = nil;

		if(scopeWindow.notNil) { scopeWindow.quit };
		volume.freeSynth;
		RootNode(this).freeAll;
		this.newAllocators;
	}
}