#ifndef ECN_CONTROLLER_H
#define ECN_CONTROLLER_H

#include "ns3/event-id.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-socket-state.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ecn
{

class EcnController : public ns3::TcpNewReno
{
public:
    struct FlowState
    {
        ns3::Ptr<ns3::TcpSocketState> tcb;
        uint32_t controlledCwnd;
        uint32_t flowId;
        double rttSeconds;
        bool lossEvent;
    };

    static ns3::TypeId GetTypeId();

    EcnController();
    EcnController(const EcnController& other);
    ~EcnController() override;

    std::string GetName() const override;
    ns3::Ptr<ns3::TcpCongestionOps> Fork() override;

    void Init(ns3::Ptr<ns3::TcpSocketState> tcb) override;
    void PktsAcked(ns3::Ptr<ns3::TcpSocketState> tcb, uint32_t segmentsAcked, const ns3::Time& rtt) override;
    uint32_t GetSsThresh(ns3::Ptr<const ns3::TcpSocketState> tcb, uint32_t bytesInFlight) override;
    void IncreaseWindow(ns3::Ptr<ns3::TcpSocketState> tcb, uint32_t segmentsAcked) override;
    void CongControl(ns3::Ptr<ns3::TcpSocketState> tcb, const ns3::TcpRateOps::TcpRateConnection& rc, const ns3::TcpRateOps::TcpRateSample& rs) override;
    void CwndEvent(ns3::Ptr<ns3::TcpSocketState> tcb, const ns3::TcpSocketState::TcpCAEvent_t event) override;
    void CongestionStateSet(ns3::Ptr<ns3::TcpSocketState> tcb, const ns3::TcpSocketState::TcpCongState_t newState) override;
    bool HasCongControl() const override;

    static void SetAlpha(double alpha);
    static double GetAlpha();
    static void SetBottleneckBps(uint64_t bps);
    static void SetControlInterval(double intervalSeconds);
    static void Reset();
    static bool SetTimeSeriesFile(const std::string& path);

    static uint32_t GetLastOldCwnd();
    static uint32_t GetLastFormulaCwnd();
    static uint32_t GetLastFinalCwnd();
    static uint32_t GetControllerUpdates();

private:
    static FlowState* FindFlow(ns3::Ptr<ns3::TcpSocketState> tcb);
    static void RegisterFlow(ns3::Ptr<ns3::TcpSocketState> tcb);
    static void UnregisterFlow(ns3::Ptr<ns3::TcpSocketState> tcb);
    static void ScheduleIntervalControl();
    static void IntervalControl();
    static void RestoreControlledCwnd(ns3::Ptr<ns3::TcpSocketState> tcb);

    ns3::Ptr<ns3::TcpSocketState> m_tcb;
    bool m_registered;

    static double g_alpha;
    static uint64_t g_bottleneckBps;

    static uint32_t g_lastOldCwnd;
    static uint32_t g_lastFormulaCwnd;
    static uint32_t g_lastFinalCwnd;
    static uint32_t g_controllerUpdates;

    static uint64_t g_intervalLastPackets;
    static uint64_t g_intervalLastMarkedPackets;

    static double g_intervalSeconds;

    static bool g_intervalInitialized;
    static bool g_intervalScheduled;

    static ns3::EventId g_intervalEvent;

    static std::vector<FlowState> g_flows;
    static uint32_t g_nextFlowId;
};

}

#endif