#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/red-queue-disc.h"
#include "ns3/traffic-control-module.h"
#include "bdp-monitor.h"
#include "ecn-controller.h"
#include "ecn-monitor.h"
#include "traffic-generator.h"
#include "vbr-generator.h"
#include <iomanip>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

using namespace ns3;
using namespace ecn;
using namespace std;

static void QueueEnqueueTrace(Ptr<const QueueDiscItem> item)
{
    EcnMonitor::OnEnqueue(item);
}

static void QueueMarkTrace(Ptr<const QueueDiscItem> item, const char* reason)
{
    EcnMonitor::OnMark(item, reason);
}

static void TcpEcnStateTrace(TcpSocketState::EcnState_t oldState,
                             TcpSocketState::EcnState_t newState)
{
    EcnMonitor::RecordEcnFeedback(oldState, newState, Simulator::Now().GetSeconds());
}

static void ConnectTcpEcnTraces()
{
    Config::ConnectWithoutContext(
        "/NodeList/*/$ns3::TcpL4Protocol/SocketList/*/$ns3::TcpSocketBase/EcnState",
        MakeCallback(&TcpEcnStateTrace));
}

int main(int argc, char* argv[])
{
    uint64_t packetCount = 1000000;
    double simulationTime = 60.0;
    double alpha = 0.8;
    double controlInterval = 0.05;
    uint32_t packetSize = 1448;
    string tcpLinkRate = "1Gbps";
    string vbrLinkRate = "100Mbps";
    string bottleneckRate = "50Mbps";
    string tcpLinkDelay = "1ms";
    string vbrLinkDelay = "1ms";
    string bottleneckDelay = "55ms";
    string receiverLinkRate = "1Gbps";
    string receiverLinkDelay = "1ms";
    uint32_t redMinTh = 40;
    uint32_t redMaxTh = 100;
    uint32_t queueSize = 120;
    uint32_t vbrMinRate = 6;
    uint32_t vbrMaxRate = 14;
    uint32_t runNumber = 1;
    string outputPrefix = "ecn-final";
    string tcpAlgo = "EcnController";

    CommandLine cmd(__FILE__);
    cmd.AddValue("packetCount", "Number of packets for each TCP flow", packetCount);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("alpha", "ECN controller alpha", alpha);
    cmd.AddValue("controlInterval", "Controller interval in seconds", controlInterval);
    cmd.AddValue("packetSize", "Packet size in bytes", packetSize);
    cmd.AddValue("tcpLinkRate", "TCP sender access-link data rate", tcpLinkRate);
    cmd.AddValue("tcpLinkDelay", "TCP sender access-link delay", tcpLinkDelay);
    cmd.AddValue("vbrLinkRate", "VBR sender access-link data rate", vbrLinkRate);
    cmd.AddValue("vbrLinkDelay", "VBR sender access-link delay", vbrLinkDelay);
    cmd.AddValue("bottleneckRate", "Shared bottleneck data rate", bottleneckRate);
    cmd.AddValue("bottleneckDelay", "Shared bottleneck delay", bottleneckDelay);
    cmd.AddValue("receiverLinkRate", "Receiver link data rate", receiverLinkRate);
    cmd.AddValue("receiverLinkDelay", "Receiver link delay", receiverLinkDelay);
    cmd.AddValue("redMinTh", "RED minimum threshold in packets", redMinTh);
    cmd.AddValue("redMaxTh", "RED maximum threshold in packets", redMaxTh);
    cmd.AddValue("queueSize", "Maximum queue size in packets", queueSize);
    cmd.AddValue("vbrMinRate", "Minimum VBR rate in Mbps", vbrMinRate);
    cmd.AddValue("vbrMaxRate", "Maximum VBR rate in Mbps", vbrMaxRate);
    cmd.AddValue("runNumber", "Experiment run number", runNumber);
    cmd.AddValue("outputPrefix", "Prefix for CSV and FlowMonitor output files", outputPrefix);
    cmd.AddValue("tcpAlgo",
                 "TCP congestion control algorithm (EcnController, TcpLinuxReno, TcpNewReno, TcpCubic, TcpBbr, TcpDctcp)",
                 tcpAlgo);
    cmd.Parse(argc, argv);

    if (simulationTime <= 1.0 || packetSize == 0 || vbrMinRate == 0 || vbrMaxRate == 0 || controlInterval <= 0.0)
    {
        cerr << "simulationTime must exceed 1 s; packetSize, controlInterval, and VBR rates must be non-zero\n";
        return 2;
    }

    RngSeedManager::SetRun(runNumber);

    uint64_t totalBytes = packetCount * static_cast<uint64_t>(packetSize);
    uint64_t bottleneckBps = DataRate(bottleneckRate).GetBitRate();

    bool isCustomController = (tcpAlgo == "EcnController");
    bool useEcn = (isCustomController || tcpAlgo == "TcpDctcp");

    EcnController::Reset();
    VbrGenerator::SetRateRange(vbrMinRate, vbrMaxRate);
    EcnMonitor::Reset();

    if (isCustomController)
    {
        EcnController::SetAlpha(alpha);
        EcnController::SetBottleneckBps(bottleneckBps);
        EcnController::SetControlInterval(controlInterval);

        if (!EcnController::SetTimeSeriesFile(outputPrefix + "-timeseries.csv"))
        {
            cerr << "Unable to open controller time-series output: "
                 << outputPrefix << "-timeseries.csv\n";
            return 1;
        }
    }

    Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue(useEcn ? "On" : "Off"));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));
    Config::SetDefault("ns3::TcpSocketBase::Timestamp", BooleanValue(true));
    Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));
    Config::SetDefault("ns3::TcpSocketBase::LimitedTransmit", BooleanValue(true));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(packetSize));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4194304));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(4194304));

    TypeId tcpTid;
    if (isCustomController)
    {
        tcpTid = EcnController::GetTypeId();
    }
    else
    {
        string fullTypeName = (tcpAlgo.rfind("ns3::", 0) == 0) ? tcpAlgo : ("ns3::" + tcpAlgo);
        if (!TypeId::LookupByNameFailSafe(fullTypeName, &tcpTid))
        {
            cerr << "Unknown TCP congestion control TypeId: " << fullTypeName << "\n";
            return 2;
        }
    }
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(tcpTid));

    NodeContainer tcpSenders;
    tcpSenders.Create(3);
    NodeContainer vbrSender;
    vbrSender.Create(1);
    NodeContainer routerNode;
    routerNode.Create(1);
    NodeContainer bottleneckNode;
    bottleneckNode.Create(1);
    NodeContainer receivers;
    receivers.Create(2);

    Ptr<Node> tcp1 = tcpSenders.Get(0);
    Ptr<Node> tcp2 = tcpSenders.Get(1);
    Ptr<Node> tcp3 = tcpSenders.Get(2);
    Ptr<Node> vbr = vbrSender.Get(0);
    Ptr<Node> router = routerNode.Get(0);
    Ptr<Node> bottleneck = bottleneckNode.Get(0);
    Ptr<Node> receiver1 = receivers.Get(0);
    Ptr<Node> receiver2 = receivers.Get(1);

    InternetStackHelper internet;
    internet.Install(tcpSenders);
    internet.Install(vbrSender);
    internet.Install(routerNode);
    internet.Install(bottleneckNode);
    internet.Install(receivers);

    PointToPointHelper tcpLink;
    tcpLink.SetDeviceAttribute("DataRate", StringValue(tcpLinkRate));
    tcpLink.SetChannelAttribute("Delay", StringValue(tcpLinkDelay));

    NetDeviceContainer tcp1Router = tcpLink.Install(tcp1, router);
    NetDeviceContainer tcp2Router = tcpLink.Install(tcp2, router);
    NetDeviceContainer tcp3Router = tcpLink.Install(tcp3, router);

    PointToPointHelper vbrLink;
    vbrLink.SetDeviceAttribute("DataRate", StringValue(vbrLinkRate));
    vbrLink.SetChannelAttribute("Delay", StringValue(vbrLinkDelay));

    NetDeviceContainer vbrRouter = vbrLink.Install(vbr, router);

    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue(bottleneckRate));
    bottleneckLink.SetChannelAttribute("Delay", StringValue(bottleneckDelay));

    NetDeviceContainer routerBottleneck = bottleneckLink.Install(router, bottleneck);

    PointToPointHelper receiverLink;
    receiverLink.SetDeviceAttribute("DataRate", StringValue(receiverLinkRate));
    receiverLink.SetChannelAttribute("Delay", StringValue(receiverLinkDelay));

    NetDeviceContainer bottleneckReceiver1 = receiverLink.Install(bottleneck, receiver1);
    NetDeviceContainer bottleneckReceiver2 = receiverLink.Install(bottleneck, receiver2);

    TrafficControlHelper red;
    red.SetRootQueueDisc("ns3::RedQueueDisc",
                         "LinkBandwidth", StringValue(bottleneckRate),
                         "LinkDelay", StringValue(bottleneckDelay),
                         "MinTh", DoubleValue(redMinTh),
                         "MaxTh", DoubleValue(redMaxTh),
                         "MaxSize", QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, queueSize)),
                         "UseEcn", BooleanValue(true));

    QueueDiscContainer queueDiscs = red.Install(routerBottleneck);

    Ipv4AddressHelper address;

    address.SetBase("10.0.1.0", "255.255.255.0");
    Ipv4InterfaceContainer tcp1If = address.Assign(tcp1Router);

    address.SetBase("10.0.2.0", "255.255.255.0");
    Ipv4InterfaceContainer tcp2If = address.Assign(tcp2Router);

    address.SetBase("10.0.3.0", "255.255.255.0");
    Ipv4InterfaceContainer tcp3If = address.Assign(tcp3Router);

    address.SetBase("10.0.4.0", "255.255.255.0");
    Ipv4InterfaceContainer vbrIf = address.Assign(vbrRouter);

    address.SetBase("10.0.5.0", "255.255.255.0");
    Ipv4InterfaceContainer routerBottleneckIf = address.Assign(routerBottleneck);

    address.SetBase("10.0.6.0", "255.255.255.0");
    Ipv4InterfaceContainer receiver1If = address.Assign(bottleneckReceiver1);

    address.SetBase("10.0.7.0", "255.255.255.0");
    Ipv4InterfaceContainer receiver2If = address.Assign(bottleneckReceiver2);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    for (uint32_t i = 0; i < queueDiscs.GetN(); ++i)
    {
        Ptr<QueueDisc> queue = queueDiscs.Get(i);
        queue->TraceConnectWithoutContext("Enqueue", MakeCallback(&QueueEnqueueTrace));
        queue->TraceConnectWithoutContext("Mark", MakeCallback(&QueueMarkTrace));
    }

    cout << "[V3] Queue Enqueue Trace: CONNECTED\n";
    cout << "[V3] Queue Mark Trace: CONNECTED\n";

    uint16_t tcpPort1 = 5001;
    uint16_t tcpPort2 = 5002;
    uint16_t tcpPort3 = 5003;
    uint16_t vbrPort1 = 6001;
    uint16_t vbrPort2 = 6002;

    TrafficGenerator::InstallReceiver(receiver1, tcpPort1, simulationTime);
    TrafficGenerator::InstallReceiver(receiver1, tcpPort2, simulationTime);
    TrafficGenerator::InstallReceiver(receiver2, tcpPort3, simulationTime);
    TrafficGenerator::InstallUdpReceiver(receiver1, vbrPort1, 1.0, simulationTime);
    TrafficGenerator::InstallUdpReceiver(receiver2, vbrPort2, 1.0, simulationTime);

    Address tcp1Destination = InetSocketAddress(receiver1If.GetAddress(1), tcpPort1);
    Address tcp2Destination = InetSocketAddress(receiver1If.GetAddress(1), tcpPort2);
    Address tcp3Destination = InetSocketAddress(receiver2If.GetAddress(1), tcpPort3);

    TrafficGenerator::InstallSender(tcp1, tcp1Destination, tcpPort1, totalBytes, packetSize, 1.0, simulationTime);
    TrafficGenerator::InstallSender(tcp2, tcp2Destination, tcpPort2, totalBytes, packetSize, 1.0, simulationTime);
    TrafficGenerator::InstallSender(tcp3, tcp3Destination, tcpPort3, totalBytes, packetSize, 1.0, simulationTime);

    Address vbrDestination1 = InetSocketAddress(receiver1If.GetAddress(1), vbrPort1);
    Address vbrDestination2 = InetSocketAddress(receiver2If.GetAddress(1), vbrPort2);

    VbrGenerator::InstallBoth(vbr, vbrDestination1, vbrDestination2, 1000, 1.0, simulationTime);
    Simulator::Schedule(Seconds(1.001), &ConnectTcpEcnTraces);

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowmon = flowmonHelper.InstallAll();

    cout << "\n";
    cout << "============================================================\n";
    cout << "          INTELLIGENT ECN CONGESTION CONTROLLER V3\n";
    cout << "============================================================\n";
    cout << "Topology             : 3 TCP + 2 VBR\n";
    cout << "TCP Flows            : 3\n";
    cout << "VBR Flows            : 2\n";
    cout << "Controller           : " << (isCustomController ? "ECN + BDP (Custom)" : tcpAlgo) << "\n";
    cout << "TCP Socket Type      : " << tcpTid.GetName() << "\n";
    cout << "ECN                  : " << (useEcn ? "ENABLED" : "DISABLED") << "\n";
    cout << "Queue Management     : " << (useEcn ? "RED + ECN" : "RED (Drop)") << "\n";
    cout << "TCP -> Router        : " << tcpLinkRate << " / " << tcpLinkDelay << "\n";
    cout << "VBR -> Router        : " << vbrLinkRate << " / " << vbrLinkDelay << "\n";
    cout << "Shared Bottleneck    : " << bottleneckRate << " / " << bottleneckDelay << "\n";
    cout << "Router -> Receiver   : " << receiverLinkRate << " / " << receiverLinkDelay << "\n";
    cout << "RED MinTh            : " << redMinTh << " packets\n";
    cout << "RED MaxTh            : " << redMaxTh << " packets\n";
    cout << "Queue Size           : " << queueSize << " packets\n";
    cout << "Alpha                : " << alpha << "\n";
    cout << "Packet Count         : " << packetCount << " per TCP flow\n";
    cout << "Packet Size          : " << packetSize << " bytes\n";
    cout << "VBR Rate Range       : " << vbrMinRate << " - " << vbrMaxRate << " Mbps\n";
    cout << "Simulation Time      : " << simulationTime << " seconds\n";
    cout << "Controller Interval  : " << fixed << setprecision(3) << controlInterval << " second\n";
    cout << "============================================================\n";
    if (isCustomController)
    {
        cout << "ECN Response         : CWND reduction based on ECN ratio\n";
        cout << "No-ECN Response      : RTT-normalized CWND increase\n";
        cout << "BDP Constraint       : CWND <= Fair-Share BDP\n";
        cout << "============================================================\n";
    }

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    flowmon->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    map<FlowId, FlowMonitor::FlowStats> stats = flowmon->GetFlowStats();

    double totalTcpThroughput = 0.0;
    double totalTcpDelay = 0.0;
    uint64_t totalTcpRxPackets = 0;
    uint64_t totalTcpTxPackets = 0;
    uint64_t totalTcpLostPackets = 0;
    uint32_t tcpFlowCount = 0;

    cout << "\n";
    cout << "============================================================\n";
    cout << "                     FLOW RESULTS\n";
    cout << "============================================================\n";

    for (const auto& flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flow.first);
        FlowMonitor::FlowStats st = flow.second;

        double lossPercent = 0.0;
        double throughputMbps = 0.0;
        double meanDelayMs = 0.0;
        double meanJitterMs = 0.0;

        if (st.txPackets > 0)
        {
            lossPercent = 100.0 * static_cast<double>(st.lostPackets) / static_cast<double>(st.txPackets);
        }

        double flowDuration = st.timeLastRxPacket.GetSeconds() - st.timeFirstTxPacket.GetSeconds();
        if (flowDuration <= 0.0)
        {
            flowDuration = simulationTime;
        }
        if (flowDuration > 0.0)
        {
            throughputMbps = static_cast<double>(st.rxBytes) * 8.0 / flowDuration / 1000000.0;
        }

        if (st.rxPackets > 0)
        {
            meanDelayMs = st.delaySum.GetSeconds() / static_cast<double>(st.rxPackets) * 1000.0;
        }

        if (st.rxPackets > 1)
        {
            meanJitterMs = st.jitterSum.GetSeconds() / static_cast<double>(st.rxPackets - 1) * 1000.0;
        }

        cout << "\n";
        cout << "---------------- FLOW " << flow.first << " ----------------\n";
        cout << "Source              : " << tuple.sourceAddress << "\n";
        cout << "Destination         : " << tuple.destinationAddress << "\n";
        cout << "Protocol            : " << static_cast<uint32_t>(tuple.protocol) << "\n";
        cout << "TX Packets          : " << st.txPackets << "\n";
        cout << "RX Packets          : " << st.rxPackets << "\n";
        cout << "Lost Packets        : " << st.lostPackets << "\n";
        cout << "Packet Loss         : " << fixed << setprecision(4) << lossPercent << " %\n";
        cout << "TX Bytes            : " << st.txBytes << "\n";
        cout << "RX Bytes            : " << st.rxBytes << "\n";
        cout << "Throughput          : " << fixed << setprecision(4) << throughputMbps << " Mbps\n";
        cout << "Mean Delay          : " << fixed << setprecision(4) << meanDelayMs << " ms\n";
        cout << "Mean Jitter         : " << fixed << setprecision(4) << meanJitterMs << " ms\n";
        cout << "Measured Duration   : " << fixed << setprecision(4) << flowDuration << " s\n";
        cout << "------------------------------------------\n";

        if (tuple.protocol == 6 &&
            (tuple.destinationPort == tcpPort1 || tuple.destinationPort == tcpPort2 ||
             tuple.destinationPort == tcpPort3))
        {
            ++tcpFlowCount;
            totalTcpThroughput += throughputMbps;
            totalTcpDelay += meanDelayMs;
            totalTcpRxPackets += st.rxPackets;
            totalTcpTxPackets += st.txPackets;
            totalTcpLostPackets += st.lostPackets;
        }
    }

    double averageTcpThroughput = 0.0;
    double averageTcpDelay = 0.0;
    double tcpLossPercent = 0.0;

    if (tcpFlowCount > 0)
    {
        averageTcpThroughput = totalTcpThroughput / static_cast<double>(tcpFlowCount);
        averageTcpDelay = totalTcpDelay / static_cast<double>(tcpFlowCount);
    }

    if (totalTcpTxPackets > 0)
    {
        tcpLossPercent = 100.0 * static_cast<double>(totalTcpLostPackets) / static_cast<double>(totalTcpTxPackets);
    }

    cout << "\n";
    cout << "============================================================\n";
    cout << "                     FINAL RESULTS\n";
    cout << "============================================================\n";
    cout << "Algorithm            : " << tcpAlgo << "\n";
    cout << "Total Packets        : " << EcnMonitor::GetTotalPackets() << "\n";
    cout << "ECN Marked Packets   : " << EcnMonitor::GetMarkedPackets() << "\n";
    cout << "ECN Ratio            : " << fixed << setprecision(8) << EcnMonitor::GetCumulativeRatio() << "\n";
    cout << "ECN Percentage       : " << fixed << setprecision(4) << EcnMonitor::GetCumulativeRatio() * 100.0 << " %\n";
    cout << "ECN ACK Count        : " << EcnMonitor::GetEcnAckCount() << "\n";
    cout << "Last ECN ACK Time    : " << EcnMonitor::GetLastEcnAckTime() << " s\n";
    cout << "Controller Updates   : " << EcnController::GetControllerUpdates() << "\n";
    cout << "Last Old CWND        : " << EcnController::GetLastOldCwnd() << " bytes\n";
    cout << "Last Formula CWND    : " << EcnController::GetLastFormulaCwnd() << " bytes\n";
    cout << "Last Final CWND      : " << EcnController::GetLastFinalCwnd() << " bytes\n";
    cout << "Average TCP Throughput: " << fixed << setprecision(4) << averageTcpThroughput << " Mbps\n";
    cout << "Aggregate TCP Throughput: " << fixed << setprecision(4) << totalTcpThroughput << " Mbps\n";
    cout << "Average TCP Delay    : " << fixed << setprecision(4) << averageTcpDelay << " ms\n";
    cout << "Aggregate TCP Loss   : " << fixed << setprecision(4) << tcpLossPercent << " %\n";
    cout << "============================================================\n";

    ofstream results(outputPrefix + "-results.csv");
    if (!results.is_open())
    {
        cerr << "Unable to open results output: " << outputPrefix << "-results.csv\n";
        Simulator::Destroy();
        return 1;
    }
    results << "metric,value\n";
    results << "algorithm," << tcpAlgo << "\n";
    results << "alpha," << EcnController::GetAlpha() << "\n";
    results << "control_interval_s," << controlInterval << "\n";
    results << "queue_enqueued_packets," << EcnMonitor::GetTotalPackets() << "\n";
    results << "queue_marked_packets," << EcnMonitor::GetMarkedPackets() << "\n";
    results << "queue_ecn_ratio," << EcnMonitor::GetCumulativeRatio() << "\n";
    results << "ecn_feedback_events," << EcnMonitor::GetEcnAckCount() << "\n";
    results << "controller_updates," << EcnController::GetControllerUpdates() << "\n";
    results << "average_tcp_throughput_mbps," << averageTcpThroughput << "\n";
    results << "aggregate_tcp_throughput_mbps," << totalTcpThroughput << "\n";
    results << "average_tcp_delay_ms," << averageTcpDelay << "\n";
    results << "aggregate_tcp_loss_percent," << tcpLossPercent << "\n";
    results.close();

    flowmon->SerializeToXmlFile(outputPrefix + "-flowmon.xml", true, true);

    Simulator::Destroy();
    return 0;
}