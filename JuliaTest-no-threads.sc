//CHECK Julia.sc for new Julia functions on server (overridden quit)

s.boot;

s.sendMsg("/cmd", "julia_include");

{Julia.ar(DC.ar(0))}.play
{SinOsc.ar(DC.ar(440))}.play
{DelayC.ar(DC.ar(0), 1)}.play

100.do{{Julia.ar(DC.ar(0))}.play}
100.do{{SinOsc.ar()/100}.play}

s.quit;