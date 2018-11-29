Julia : UGen {
	*ar {
		//args
		arg in1 = 440.0;
		//multiNew
		^this.multiNew('audio', in1);
	}
	//mulOut
}