#ifndef ECN_MONITOR_H
#define ECN_MONITOR_H

#include "ns3/queue-disc.h"
#include "ns3/tcp-socket-state.h"
#include <cstdint>

namespace ecn
{
class EcnMonitor
{
public:
    static void Reset();
    static void OnEnqueue(ns3::Ptr<const ns3::QueueDiscItem> item);
    static void OnMark(ns3::Ptr<const ns3::QueueDiscItem> item, const char* reason);
    static double GetCumulativeRatio();
    static double GetRatio();
    static uint64_t GetTotalPackets();
    static uint64_t GetMarkedPackets();
    static void RecordEcnAck(double timeSeconds);
    static uint64_t GetEcnAckCount();
    static double GetLastEcnAckTime();
    static void RecordEcnFeedback(ns3::TcpSocketState::EcnState_t oldState,
                                  ns3::TcpSocketState::EcnState_t newState,
                                  double timeSeconds);
};
}

#endif