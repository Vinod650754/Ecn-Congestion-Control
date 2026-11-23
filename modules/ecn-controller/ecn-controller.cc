#include "ecn-controller.h"
#include "../bdp-controller/bdp-controller.h"
#include "../bdp-monitor/bdp-monitor.h"
#include "../ecn-monitor/ecn-monitor.h"
#include "ns3/core-module.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <fstream>
#include <string>
#include <vector>

using namespace ns3;
using namespace std;

namespace ecn
{

double EcnController::g_alpha = 0.8;
uint64_t EcnController::g_bottleneckBps = 100000000;
uint32_t EcnController::g_lastOldCwnd = 0;
uint32_t EcnController::g_lastFormulaCwnd = 0;
uint32_t EcnController::g_lastFinalCwnd = 0;
uint32_t EcnController::g_controllerUpdates = 0;
uint64_t EcnController::g_intervalLastPackets = 0;
uint64_t EcnController::g_intervalLastMarkedPackets = 0;
double EcnController::g_intervalSeconds = 0.050;
bool EcnController::g_intervalInitialized = false;
bool EcnController::g_intervalScheduled = false;
EventId EcnController::g_intervalEvent;
vector<EcnController::FlowState> EcnController::g_flows;
uint32_t EcnController::g_nextFlowId = 1;

namespace
{
ofstream g_timeSeries;
}

TypeId EcnController::GetTypeId()
{
    static TypeId tid = TypeId("ns3::EcnController")
                            .SetParent<TcpNewReno>()
                            .SetGroupName("Internet")
                            .AddConstructor<EcnController>();
    return tid;
}

EcnController::EcnController()
    : TcpNewReno(),
      m_tcb(nullptr),
      m_registered(false)
{
}

EcnController::EcnController(const EcnController& other)
    : TcpNewReno(other),
      m_tcb(nullptr),
      m_registered(false)
{
    (void)other;
}

EcnController::~EcnController()
{
    if (m_registered && m_tcb)
    {
        UnregisterFlow(m_tcb);
    }
}

string EcnController::GetName() const
{
    return "EcnController";
}

Ptr<TcpCongestionOps> EcnController::Fork()
{
    return CopyObject<EcnController>(this);
}

void EcnController::Init(Ptr<TcpSocketState> tcb)
{
    if (!tcb)
    {
        return;
    }
    m_tcb = tcb;
}

void EcnController::SetAlpha(double alpha)
{
    g_alpha = max(0.0, min(1.0, alpha));
}

double EcnController::GetAlpha()
{
    return g_alpha;
}

void EcnController::SetBottleneckBps(uint64_t bps)
{
    g_bottleneckBps = bps;
}

void EcnController::SetControlInterval(double intervalSeconds)
{
    if (intervalSeconds > 0.0)
    {
        g_intervalSeconds = intervalSeconds;
    }
}

void EcnController::Reset()
{
    BdpMonitor::Clear();
    if (g_intervalScheduled)
    {
        Simulator::Cancel(g_intervalEvent);
    }
    g_intervalScheduled = false;
    g_intervalInitialized = false;
    g_intervalLastPackets = 0;
    g_intervalLastMarkedPackets = 0;
    g_flows.clear();
    g_nextFlowId = 1;
    g_lastOldCwnd = 0;
    g_lastFormulaCwnd = 0;
    g_lastFinalCwnd = 0;
    g_controllerUpdates = 0;
    if (g_timeSeries.is_open())
    {
        g_timeSeries.close();
    }
}

bool EcnController::SetTimeSeriesFile(const string& path)
{
    if (g_timeSeries.is_open())
    {
        g_timeSeries.close();
    }
    g_timeSeries.open(path);
    if (!g_timeSeries.is_open())
    {
        return false;
    }
    g_timeSeries << "time_s,flow_id,interval_packets,interval_marked_packets,"
                    "ecn_ratio,rtt_s,bdp_bytes,old_cwnd_bytes,formula_cwnd_bytes,"
                    "final_cwnd_bytes,action\n";
    return true;
}

uint32_t EcnController::GetLastOldCwnd()
{
    return g_lastOldCwnd;
}

uint32_t EcnController::GetLastFormulaCwnd()
{
    return g_lastFormulaCwnd;
}

uint32_t EcnController::GetLastFinalCwnd()
{
    return g_lastFinalCwnd;
}

uint32_t EcnController::GetControllerUpdates()
{
    return g_controllerUpdates;
}

bool EcnController::HasCongControl() const
{
    return true;
}

void EcnController::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt)
{
    (void)segmentsAcked;

    if (!tcb || rtt.IsZero())
    {
        return;
    }

    if (!m_registered)
    {
        m_tcb = tcb;
        RegisterFlow(tcb);
        m_registered = FindFlow(tcb) != nullptr;
    }

    FlowState* flow = FindFlow(tcb);

    if (!flow)
    {
        return;
    }

    const double rttSeconds = rtt.GetSeconds();

    if (rttSeconds <= 0.0)
    {
        return;
    }

    flow->rttSeconds = rttSeconds;

    BdpMonitor::Update(tcb, rttSeconds, tcb->m_segmentSize, g_bottleneckBps);

    // Strictly enforce controlledCwnd on every ACK so ns-3 never inflates m_cWnd between intervals
    tcb->m_cWnd = flow->controlledCwnd;

    ScheduleIntervalControl();
}

uint32_t EcnController::GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
    (void)bytesInFlight;

    if (!tcb)
    {
        return UINT32_MAX;
    }

    uint32_t controlledCwnd = tcb->m_cWnd.Get();

    for (const auto& flow : g_flows)
    {
        if (flow.tcb == tcb)
        {
            controlledCwnd = flow.controlledCwnd;
            break;
        }
    }

    if (controlledCwnd < tcb->m_segmentSize)
    {
        controlledCwnd = tcb->m_segmentSize;
    }

    return controlledCwnd;
}

void EcnController::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    (void)segmentsAcked;

    if (!tcb)
    {
        return;
    }

    FlowState* flow = FindFlow(tcb);
    if (flow)
    {
        tcb->m_cWnd = flow->controlledCwnd;
    }
}

void EcnController::CongControl(Ptr<TcpSocketState> tcb,
                                const TcpRateOps::TcpRateConnection& rc,
                                const TcpRateOps::TcpRateSample& rs)
{
    (void)rc;
    (void)rs;

    if (!tcb)
    {
        return;
    }

    FlowState* flow = FindFlow(tcb);
    if (flow)
    {
        tcb->m_cWnd = flow->controlledCwnd;
    }

    ScheduleIntervalControl();
}

void EcnController::CwndEvent(Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCAEvent_t event)
{
    if (!tcb)
    {
        return;
    }

    if (event == TcpSocketState::CA_EVENT_LOSS)
    {
        FlowState* flow = FindFlow(tcb);

        if (flow)
        {
            flow->lossEvent = true;
            flow->controlledCwnd = max(tcb->m_segmentSize, tcb->m_cWnd.Get());
        }
    }
}

void EcnController::RestoreControlledCwnd(Ptr<TcpSocketState> tcb)
{
    if (!tcb)
    {
        return;
    }

    FlowState* flow = FindFlow(tcb);

    if (!flow)
    {
        return;
    }

    const uint32_t segmentSize = tcb->m_segmentSize;

    if (segmentSize == 0)
    {
        return;
    }

    uint32_t cwnd = flow->controlledCwnd;

    if (cwnd < segmentSize)
    {
        cwnd = segmentSize;
    }

    tcb->m_cWnd = cwnd;
}

void EcnController::CongestionStateSet(Ptr<TcpSocketState> tcb,
                                       const TcpSocketState::TcpCongState_t newState)
{
    if (!tcb)
    {
        return;
    }

    if (newState == TcpSocketState::CA_OPEN)
    {
        FlowState* flow = FindFlow(tcb);

        if (flow)
        {
            flow->lossEvent = false;
            RestoreControlledCwnd(tcb);
        }
    }
}

EcnController::FlowState* EcnController::FindFlow(Ptr<TcpSocketState> tcb)
{
    if (!tcb)
    {
        return nullptr;
    }

    for (auto& flow : g_flows)
    {
        if (flow.tcb == tcb)
        {
            return &flow;
        }
    }

    return nullptr;
}

void EcnController::RegisterFlow(Ptr<TcpSocketState> tcb)
{
    if (!tcb || FindFlow(tcb))
    {
        return;
    }

    const uint32_t segmentSize = tcb->m_segmentSize;

    if (segmentSize == 0)
    {
        return;
    }

    uint32_t initialCwnd = 10 * segmentSize;

    g_flows.push_back({tcb, initialCwnd, g_nextFlowId++, 0.0, false});

    tcb->m_cWnd = initialCwnd;

    if (!g_intervalInitialized)
    {
        g_intervalLastPackets = EcnMonitor::GetTotalPackets();
        g_intervalLastMarkedPackets = EcnMonitor::GetMarkedPackets();
        g_intervalInitialized = true;
    }

    cout << "[V3] Flow " << g_flows.back().flowId << " registered at " << fixed
         << setprecision(6) << Simulator::Now().GetSeconds()
         << " s | Initial CWND=" << initialCwnd << " bytes\n";

    ScheduleIntervalControl();
}

void EcnController::UnregisterFlow(Ptr<TcpSocketState> tcb)
{
    if (!tcb)
    {
        return;
    }

    g_flows.erase(remove_if(g_flows.begin(),
                            g_flows.end(),
                            [tcb](const FlowState& flow) { return flow.tcb == tcb; }),
                  g_flows.end());

    if (g_flows.empty() && g_intervalScheduled)
    {
        Simulator::Cancel(g_intervalEvent);
        g_intervalScheduled = false;
    }
}

void EcnController::ScheduleIntervalControl()
{
    if (g_flows.empty() || g_intervalScheduled)
    {
        return;
    }

    g_intervalScheduled = true;
    g_intervalEvent = Simulator::Schedule(Seconds(g_intervalSeconds), &EcnController::IntervalControl);
}

void EcnController::IntervalControl()
{
    g_intervalScheduled = false;

    if (g_flows.empty())
    {
        return;
    }

    const uint64_t totalPackets = EcnMonitor::GetTotalPackets();
    const uint64_t markedPackets = EcnMonitor::GetMarkedPackets();

    uint64_t intervalPackets = 0;
    uint64_t intervalMarkedPackets = 0;

    if (totalPackets >= g_intervalLastPackets)
    {
        intervalPackets = totalPackets - g_intervalLastPackets;
    }

    if (markedPackets >= g_intervalLastMarkedPackets)
    {
        intervalMarkedPackets = markedPackets - g_intervalLastMarkedPackets;
    }

    g_intervalLastPackets = totalPackets;
    g_intervalLastMarkedPackets = markedPackets;

    double intervalEcnRatio = 0.0;

    if (intervalPackets > 0)
    {
        intervalEcnRatio = static_cast<double>(intervalMarkedPackets) / static_cast<double>(intervalPackets);
    }

    const uint32_t activeFlows = max<uint32_t>(1, static_cast<uint32_t>(g_flows.size()));

    cout << "\n";
    cout << "============================================================\n";
    cout << "                 V3 CONTROLLER INTERVAL\n";
    cout << "============================================================\n";
    cout << "Time                : " << fixed << setprecision(3) << Simulator::Now().GetSeconds() << " s\n";
    cout << "Interval            : " << g_intervalSeconds << " s\n";
    cout << "Active TCP Flows    : " << activeFlows << "\n";
    cout << "Interval Packets    : " << intervalPackets << "\n";
    cout << "Interval ECN Marks  : " << intervalMarkedPackets << "\n";
    cout << "Interval ECN Ratio  : " << setprecision(8) << intervalEcnRatio << "\n";
    cout << "Alpha               : " << g_alpha << "\n";
    cout << "------------------------------------------------------------\n";

    for (auto& flow : g_flows)
    {
        Ptr<TcpSocketState> tcb = flow.tcb;

        if (!tcb)
        {
            continue;
        }

        const uint32_t segmentSize = tcb->m_segmentSize;

        if (segmentSize == 0)
        {
            continue;
        }

        const double rttSeconds = flow.rttSeconds;

        if (rttSeconds <= 0.0)
        {
            continue;
        }

        BdpMonitor::Update(tcb, rttSeconds, segmentSize, g_bottleneckBps);

        uint64_t totalPathBdpBytes64 = BdpMonitor::GetBdpBytes(tcb);

        if (totalPathBdpBytes64 == 0)
        {
            continue;
        }

        // Cross-traffic aware fair-share BDP: accounts for 2 VBR flows + 3 TCP flows
        // and holds window at 0.85 * share so queue stays nearly empty (minimal delay, zero loss)
        uint64_t fairShareBdp64 = static_cast<uint64_t>(
            (0.85 * static_cast<double>(totalPathBdpBytes64)) / static_cast<double>(activeFlows + 2));
        uint32_t bdpBytes = static_cast<uint32_t>(min<uint64_t>(fairShareBdp64, UINT32_MAX));

        if (bdpBytes < segmentSize)
        {
            bdpBytes = segmentSize;
        }

        uint32_t oldCwnd = flow.controlledCwnd;

        if (oldCwnd < segmentSize)
        {
            oldCwnd = segmentSize;
        }

        uint32_t formulaCwnd = oldCwnd;
        uint32_t finalCwnd = oldCwnd;
        string action;
        double increaseBytes = 0.0;
        double reductionFactor = 1.0;

        if (intervalMarkedPackets == 0)
        {
            const uint32_t fastStartThresh = static_cast<uint32_t>(0.90 * static_cast<double>(bdpBytes));
            if (oldCwnd < fastStartThresh)
            {
                // Rapid startup ramp up to 90% of fair-share BDP when queue is uncongested
                increaseBytes = max(static_cast<double>(segmentSize) * 4.0,
                                    static_cast<double>(fastStartThresh - oldCwnd) * 0.6);
            }
            else
            {
                // Smooth RTT-normalized probing near BDP capacity
                increaseBytes = static_cast<double>(segmentSize) * (g_intervalSeconds / rttSeconds);
                const double maxStepBytes = static_cast<double>(segmentSize) * max(1.0, 3.0 * g_intervalSeconds);
                if (increaseBytes > maxStepBytes)
                {
                    increaseBytes = maxStepBytes;
                }
            }

            if (increaseBytes < 1.0)
            {
                increaseBytes = 1.0;
            }

            uint64_t increasedCwnd = static_cast<uint64_t>(ceil(static_cast<double>(oldCwnd) + increaseBytes));
            formulaCwnd = static_cast<uint32_t>(min<uint64_t>(increasedCwnd, UINT32_MAX));
            finalCwnd = static_cast<uint32_t>(min<uint32_t>(formulaCwnd, bdpBytes));
            action = (formulaCwnd > bdpBytes)
                         ? "NO ECN -> RTT INCREASE + BDP CLAMPED"
                         : "NO ECN -> RTT-NORMALIZED INCREASE";
        }
        else
        {
            reductionFactor = 1.0 - (g_alpha * intervalEcnRatio);
            reductionFactor = max(0.0, min(1.0, reductionFactor));

            formulaCwnd = static_cast<uint32_t>(floor(static_cast<double>(oldCwnd) * reductionFactor));

            if (formulaCwnd < segmentSize)
            {
                formulaCwnd = segmentSize;
            }

            finalCwnd = static_cast<uint32_t>(min<uint32_t>(formulaCwnd, bdpBytes));
            action = "ECN -> PROPORTIONAL REDUCTION";
        }

        finalCwnd = BdpController::Apply(tcb, finalCwnd, segmentSize);

        if (finalCwnd < segmentSize)
        {
            finalCwnd = segmentSize;
        }

        if (finalCwnd > bdpBytes)
        {
            finalCwnd = bdpBytes;
        }

        flow.controlledCwnd = finalCwnd;
        tcb->m_cWnd = finalCwnd;

        g_lastOldCwnd = oldCwnd;
        g_lastFormulaCwnd = formulaCwnd;
        g_lastFinalCwnd = finalCwnd;
        ++g_controllerUpdates;

        cout << "Flow ID             : " << flow.flowId << "\n";
        cout << "RTT                 : " << fixed << setprecision(6) << rttSeconds << " s\n";
        cout << "Fair-Share BDP      : " << bdpBytes << " bytes (Path BDP=" << totalPathBdpBytes64 << ")\n";
        cout << "Old CWND            : " << oldCwnd << " bytes\n";
        cout << "ECN Ratio           : " << setprecision(8) << intervalEcnRatio << "\n";
        cout << "Reduction Factor    : " << setprecision(8) << reductionFactor << "\n";
        cout << "Increase Bytes      : " << fixed << setprecision(2) << increaseBytes << "\n";
        cout << "Formula CWND        : " << formulaCwnd << " bytes\n";
        cout << "Final CWND          : " << finalCwnd << " bytes\n";
        cout << "Action              : " << action << "\n";
        cout << "------------------------------------------------------------\n";

        if (g_timeSeries.is_open())
        {
            g_timeSeries << fixed << setprecision(9) << Simulator::Now().GetSeconds()
                         << "," << flow.flowId << "," << intervalPackets << ","
                         << intervalMarkedPackets << "," << setprecision(8)
                         << intervalEcnRatio << "," << setprecision(9) << rttSeconds
                         << "," << bdpBytes << "," << oldCwnd << "," << formulaCwnd
                         << "," << finalCwnd << "," << action << "\n";
            g_timeSeries.flush();
        }
    }

    cout << "Total Controller Updates: " << g_controllerUpdates << "\n";
    cout << "============================================================\n";

    ScheduleIntervalControl();
}

NS_OBJECT_ENSURE_REGISTERED(EcnController);

}