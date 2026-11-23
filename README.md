# Intelligent ECN Congestion Controller

A modular ns-3 implementation of an ECN-driven congestion controller with a BDP-aware congestion-window constraint.

## Final architecture

- `traffic-generator` — installs TCP BulkSend and PacketSink applications.
- `ecn-monitor` — counts queue-wide enqueues, successful ECN marks, ECN-ECE feedback events, and computes the mark ratio.
- `ecn-controller` — extends TCP NewReno and applies the proportional ECN-based CWND formula.
- `bdp-monitor` — measures RTT and computes BDP from bottleneck bandwidth and RTT.
- `bdp-controller` — constrains the ECN-derived CWND using the estimated BDP.
- `simulation/main.cc` — integrates the modules, builds the topology, installs RED+ECN, NetAnim, FlowMonitor, and outputs.

## Controller

ECN response, evaluated once per controller interval:

`CWND_new = CWND_old * (1 - Alpha * ECN_Ratio)`

BDP:

`BDP = Bottleneck Bandwidth * RTT`

Final controller:

`Final CWND = min(Formula CWND, BDP)`

## Default experiment

- TCP: NewReno
- ECN: enabled
- Queue: RED + ECN
- Sender -> Router: 1 Gbps / 5 ms
- Router -> Receiver: 5 Mbps / 50 ms
- RED MinTh: 1 packet
- RED MaxTh: 3 packets
- Queue: 100 packets
- Packet count: 100000
- Packet size: 1000 bytes
- Simulation time: 30 seconds
- Alpha: 1.0

The RED thresholds are command-line parameters so they can be changed later without editing the source.

## Building without modifying ns-3

The original helper copied files into the ns-3 source tree. That is intentionally no
longer done. Build the existing ns-3.42 tree first, then compile the project in place:

```bash
cd ~/ns-allinone-3.42/ns-3.42
./ns3 build
cd scratch/ecn-final
./build-final.sh ~/ns-allinone-3.42/ns-3.42
```

This uses the generated ns-3 shared libraries and does not change files outside
`scratch/ecn-final`. It produces `ecn-final-bin` in this directory.

Run a short experiment:

```bash
./ecn-final-bin --packetCount=1000 --simulationTime=10 --alpha=1.0 \
  --vbrMinRate=20 --vbrMaxRate=30 --outputPrefix=run-a10
```

Outputs are written in the current directory:

- `run-a10-timeseries.csv` — one row per controller update and TCP flow.
- `run-a10-results.csv` — aggregate measured metrics.
- `run-a10-flowmon.xml` — detailed FlowMonitor data.

`queue_enqueued_packets` and `queue_marked_packets` are queue-disc trace
counts across every installed queue disc. `queue_ecn_ratio` is marked/enqueued,
not a ratio of TCP ACKs. `ecn_feedback_events` counts transitions into
`ECN_ECE_RCVD`, so it measures sender-side ECN feedback rather than inferred
marks. Throughput uses each flow's measured first-TX to last-RX duration.

## Changing RED thresholds

For example:

```bash
./ecn-final-bin --redMinTh=20 --redMaxTh=50 --queueSize=100 \
  --outputPrefix=run-thresholds
```

The default values remain the reference configuration. The output prefix can
be changed with `--outputPrefix`; all existing experiment options remain
available.
