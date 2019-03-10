//CHECK Julia.sc for new Julia functions on server (overridden quit)
s.options.memSize = 120000;

(
s.waitForBoot({
	s.sendMsg(\cmd, \julia_boot);
	s.sync;
});
)

s.sendMsg("/cmd", "julia_include");

s.sendMsg("/cmd", "julia_alloc");

100.do{s.sendMsg("/cmd", "julia_include")};

x = {Julia.ar(440)}.play

z = {SinOsc.ar(440)}.play

x.free;
z.free;

50.do{{Julia.ar(rrand(220, 1000)) / 50}.play}
50.do{{SinOsc.ar(rrand(220, 1000)) / 50}.play}

s.scope;
s.quit;