s.boot;

s.sendMsg(\cmd, \julia_boot);

s.sendMsg(\cmd, \julia_checkWorldAndFt);

s.sendMsg(\cmd, \julia_API_alloc);

s.sendMsg(\cmd, \julia_include);

s.sendMsg(\cmd, \julia_alloc);

100.do{s.sendMsg(\cmd, \julia_include)};

t = Server.new(\server2, NetAddr("127.0.0.1", 57111));
t.boot;

t.sendMsg(\cmd, \julia_boot);

t.sendMsg(\cmd, \julia_checkWorldAndFt);

t.sendMsg(\cmd, \julia_API_alloc);

t.sendMsg(\cmd, \julia_include);

t.sendMsg(\cmd, \julia_alloc);

100.do{t.sendMsg(\cmd, \julia_include)};

x = {Julia.ar(440)}.play
z = {SinOsc.ar(440)}.play

x.free;
z.free;

50.do{{Julia.ar(rrand(220, 1000)) / 50}.play}
50.do{{SinOsc.ar(rrand(220, 1000)) / 50}.play}

s.scope;
s.quit;

t.scope;
t.quit;