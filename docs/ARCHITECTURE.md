# Final Modular Architecture

```text
                         main.cc
                            |
                            v
                  +-------------------+
                  | Traffic Generator |
                  +---------+---------+
                            |
                            v
                         TCP
                            |
                            v
                       RED + ECN
                            |
              +-------------+-------------+
              |                           |
              v                           v
        ECN Monitor                 BDP Monitor
              |                           |
              v                           v
       ECN Controller                 RTT + BDP
              |                           |
              +-------------+-------------+
                            |
                            v
                     BDP Controller
                            |
                            v
                       Final CWND
                            |
                            v
                          TCP
```

## Separation of responsibilities

### Traffic Generator
Only creates the TCP source and sink.

### ECN Monitor
Measures every installed queue disc's enqueue and successful mark traces.
Sender-side feedback is counted only when a TCP socket transitions into
`ECN_ECE_RCVD`; it is not inferred from queue marks.

### ECN Controller
Owns the proposed proportional ECN response:

`CWND_new = CWND_old * (1 - Alpha * ECN_Ratio)`

The ratio and marks are sampled over each one-second controller interval.
Loss callbacks preserve NewReno's reduced window instead of restoring a
pre-loss controlled value.

### BDP Monitor
Measures RTT and computes:

`BDP = Bandwidth * RTT`

### BDP Controller
Applies:

`Final CWND = min(Formula CWND, BDP)`

The controller records one CSV row per flow and interval, including the
measured RTT, BDP, formula window, final window, interval packet counts, and
action. Aggregate flow metrics are written separately by the simulation.

### Main Simulation
Only integrates the modules and performs experiment setup/result collection.
