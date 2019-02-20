//server would crash with small memory. I need to wrap RTAlloc calls
//into checking if it actually has allocated...
s.options.memSize = 65536;

(
s.waitForBoot({
	s.sendMsg(\cmd, "/julia_boot");
	s.sync;
})
)

s.sendMsg(\cmd, "/julia_argumentTest", "HELLO");

s.sendMsg(\cmd, "/julia_checkWorldAndFt");

s.sendMsg(\cmd, "/julia_API_alloc");

s.sendMsg(\cmd, "/julia_posix_memalign");

s.sendMsg(\cmd, "/julia_GC");

s.sendMsg(\cmd, "/julia_include");

s.sendMsg(\cmd, "/julia_testJuliaAlloc");

(
s.bind({
	50.do{{Julia.ar(rrand(220, 1000)) / 50}.play};
	s.sendMsg(\cmd, "/julia_GC");
})
)

(
s.bind({
	{Julia.ar(440)}.play;
	s.sendMsg(\cmd, "/julia_GC");
})
)

(
s.bind({
	s.sendMsg(\cmd, "/julia_include");
	s.sendMsg(\cmd, "/julia_GC");
})
)

s.sendMsg(\cmd, "/julia_alloc");

100.do{s.sendMsg(\cmd, "/julia_include")};

s.sendMsg(\cmd, "/julia_TestAlloc_include");
//Profile it with Instruments. Both RT and NRT thread are calling to see where memory is allocated. All calls are into posix_memalign() and malloc
s.sendMsg(\cmd, "/julia_TestAlloc_perform");

x = {Julia.ar(440)}.play;
z = {SinOsc.ar(440)}.play;

x.free;
z.free;

50.do{{Julia.ar(rrand(220, 1000)) / 50}.play}
50.do{{SinOsc.ar(rrand(220, 1000)) / 50}.play}

s.scope;
s.quit;

/* SECOND SERVER */
t = Server.new(\server2, NetAddr("127.0.0.1", 57111));

t.options.memSize = 65536;

(
t.waitForBoot({
	t.sendMsg(\cmd, "/julia_boot");
	t.sync;
})
)

t.sendMsg(\cmd, "/julia_checkWorldAndFt");

t.sendMsg(\cmd, "/julia_API_alloc");

t.sendMsg(\cmd, "/julia_posix_memalign");

t.sendMsg(\cmd, "/julia_GC");

t.sendMsg(\cmd, "/julia_include");

(
t.bind({
	50.do{{Julia.ar(rrand(220, 1000)) / 50}.play(t)};
	t.sendMsg(\cmd, "/julia_GC");
})
)

(
t.bind({
	{Julia.ar(440)}.play(t);
	t.sendMsg(\cmd, "/julia_GC");
})
)

(
t.bind({
	t.sendMsg(\cmd, "/julia_include");
	t.sendMsg(\cmd, "/julia_GC");
})
)

t.sendMsg(\cmd, "/julia_alloc");

100.do{t.sendMsg(\cmd, "/julia_include")};

t.sendMsg(\cmd, "/julia_TestAlloc_include");
//Profile it with Instruments. Both RT and NRT thread are calling to see where memory is allocated. All calls are into posix_memalign() and malloc
t.sendMsg(\cmd, "/julia_TestAlloc_perform");

r = {Julia.ar(440)}.play(t)
u = {SinOsc.ar(440)}.play(t)

r.free;
u.free;

50.do{{Julia.ar(rrand(220, 1000)) / 50}.play(t)}
50.do{{SinOsc.ar(rrand(220, 1000)) / 50}.play(t)}

t.scope;
t.quit;