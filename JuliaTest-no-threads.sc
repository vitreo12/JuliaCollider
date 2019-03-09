//server would crash with small memory. I need to wrap RTAlloc calls
//into checking if it actually has allocated...
s.options.memSize = 65536;

(
s.waitForBoot({
	s.sendMsg(\cmd, "/julia_boot");
	s.sync;
})
)

s.sendMsg(\cmd, "/julia_GC");

x = {Julia.ar(440)}.play;

s.quit;