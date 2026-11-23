# Intelligent ECN Congestion Controller (ns-3.42)

An Explicit Congestion Notification (ECN) and Bandwidth-Delay Product (BDP) aware TCP congestion control module (`ns3::EcnController`) implemented and evaluated in **ns-3.42** against standard baselines (`TcpNewReno`, `CUBIC`, `DCTCP`, and `BBR`).

## Key Components
- **`ecn-controller.cc / .h`**: Interval-based congestion window controller using ECN marking ratio feedback (`CWND_new = CWND * (1 - alpha * r_ECN)`), RTT-normalized additive probing during uncongested intervals, and cross-traffic-aware fair-share BDP clamping.
- **`bdp-monitor.cc / .h` & `bdp-controller.cc / .h`**: Minimum-RTT propagation tracking and upper-bound BDP enforcement to prevent bufferbloat.
- **`ecn-monitor.cc / .h`**: Router RED queue enqueue/mark telemetry and TCP ECN state tracking.
- **`traffic-generator.cc / .h` & `vbr-generator.cc / .h`**: Multi-flow bottleneck workload generator combining 3 bulk TCP senders and 2 Variable Bit Rate (VBR) UDP cross-traffic flows.

## Running a Simulation
Place this directory inside `ns-3.42/scratch/ecn-final/` and run:

    ./ns3 build
    ./ns3 run "ecn-final --tcpAlgo=EcnController --bottleneckRate=50Mbps --bottleneckDelay=55ms --alpha=0.8"
