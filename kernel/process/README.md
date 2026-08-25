# Process subsystem

The generic cooperative scheduler owns a fixed-size task table and dispatches
ready work in round-robin order without heap allocation. A task executes one
bounded quantum per callback and returns Ready, Blocked, or Finished. Timer
preemption and saved CPU contexts belong to the architecture layer.
