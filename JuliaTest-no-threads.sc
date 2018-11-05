//CHECK Julia.sc for new Julia functions on server (overridden quit)

s.boot;

s.sendMsg("/cmd", "julia_include");

100.do{s.sendMsg("/cmd", "julia_include")};

{Julia.ar(440)}.play
{SinOsc.ar(440)}.play
{DelayN.ar(DC.ar(0))}.play

100.do{{Julia.ar(440) / 100}.play}
100.do{{SinOsc.ar() / 100}.play}

s.scope;
s.quit;