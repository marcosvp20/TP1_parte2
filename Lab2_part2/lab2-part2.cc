#include <iostream>
#include <fstream>
#include <string>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/error-model.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/enum.h"
#include "ns3/event-id.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h" 

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("lab2-part2"); 

static std::map<uint32_t, bool> firstCwnd;
static std::map<uint32_t, bool> firstSshThr;
static std::map<uint32_t, Ptr<OutputStreamWrapper>> cWndStream;
static std::map<uint32_t, Ptr<OutputStreamWrapper>> ssThreshStream;
static std::map<uint32_t, uint32_t> cWndValue;
static std::map<uint32_t, uint32_t> ssThreshValue;


static uint32_t
GetNodeIdFromContext (std::string context)
{
  std::size_t const n1 = context.find_first_of ("/", 1);
  std::size_t const n2 = context.find_first_of ("/", n1 + 1);
  return std::stoul (context.substr (n1 + 1, n2 - n1 - 1));
}

static void
CwndTracer (std::string context,  uint32_t oldval, uint32_t newval)
{
  uint32_t nodeId = GetNodeIdFromContext (context);
  if (cWndStream.find(nodeId) == cWndStream.end()) return; 

  if (firstCwnd[nodeId])
    {
      *cWndStream[nodeId]->GetStream () << "0.0 " << oldval << std::endl;
      firstCwnd[nodeId] = false;
    }
  *cWndStream[nodeId]->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  cWndValue[nodeId] = newval;

  if (!firstSshThr[nodeId] && ssThreshStream.find(nodeId) != ssThreshStream.end())
    {
      *ssThreshStream[nodeId]->GetStream ()
          << Simulator::Now ().GetSeconds () << " " << ssThreshValue[nodeId] << std::endl;
    }
}

static void
SsThreshTracer (std::string context, uint32_t oldval, uint32_t newval)
{
  uint32_t nodeId = GetNodeIdFromContext (context);
  if (ssThreshStream.find(nodeId) == ssThreshStream.end()) return; 

  if (firstSshThr[nodeId])
    {
      *ssThreshStream[nodeId]->GetStream () << "0.0 " << oldval << std::endl;
      firstSshThr[nodeId] = false;
    }
  *ssThreshStream[nodeId]->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  ssThreshValue[nodeId] = newval;

  if (!firstCwnd[nodeId] && cWndStream.find(nodeId) != cWndStream.end())
    {
      *cWndStream[nodeId]->GetStream () << Simulator::Now ().GetSeconds () << " " << cWndValue[nodeId] << std::endl;
    }
}

static void
TraceCwnd (std::string cwnd_tr_file_name, uint32_t nodeId, uint32_t socketId)
{
  AsciiTraceHelper ascii;
  cWndStream[nodeId] = ascii.CreateFileStream (cwnd_tr_file_name.c_str ());
  Config::Connect ("/NodeList/" + std::to_string (nodeId) + "/$ns3::TcpL4Protocol/SocketList/" + std::to_string(socketId) + "/CongestionWindow",
                   MakeCallback (&CwndTracer));
}

static void
TraceSsThresh (std::string ssthresh_tr_file_name, uint32_t nodeId, uint32_t socketId)
{
  AsciiTraceHelper ascii;
  ssThreshStream[nodeId] = ascii.CreateFileStream (ssthresh_tr_file_name.c_str ());
  Config::Connect ("/NodeList/" + std::to_string (nodeId) + "/$ns3::TcpL4Protocol/SocketList/" + std::to_string(socketId) + "/SlowStartThreshold",
                   MakeCallback (&SsThreshTracer));
}


int main (int argc, char *argv[])
{
  std::string dataRate = "1Mbps";
  std::string delay = "20ms";
  double errorRate = 0.00001;
  uint16_t nFlows = 2; 
  std::string transport_prot = "TcpCubic";

  uint64_t data_mbytes = 0; 
  uint32_t mtu_bytes = 1500;
  double duration = 20.0; 
  uint32_t run = 0;
  bool flow_monitor = true;
  std::string prefix_file_name = "lab2-part2";

  CommandLine cmd (__FILE__);
  cmd.AddValue ("dataRate", "Bottleneck link data rate", dataRate);
  cmd.AddValue ("delay", "Bottleneck link delay", delay);
  cmd.AddValue ("errorRate", "Bottleneck link error rate", errorRate);
  cmd.AddValue ("nFlows", "Number of TCP flows (must be even)", nFlows);
  cmd.AddValue ("transport_prot", "Transport protocol to use (TcpCubic or TcpNewReno)", transport_prot);
  cmd.AddValue ("run", "Run index (for setting repeatable seeds)", run);
  cmd.Parse (argc, argv);

  if (nFlows % 2 != 0)
    {
      NS_FATAL_ERROR ("nFlows must be an even number.");
    }

  if (transport_prot.compare ("TcpCubic") == 0)
    {
      transport_prot = "ns3::TcpCubic";
    }
  else if (transport_prot.compare ("TcpNewReno") == 0)
    {
      transport_prot = "ns3::TcpNewReno";
    }
  else
    {
      NS_FATAL_ERROR ("Invalid transport protocol. Use TcpCubic or TcpNewReno.");
    }
  
  SeedManager::SetSeed (1);
  SeedManager::SetRun (run); 
 
  Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
  em->SetAttribute ("ErrorRate", DoubleValue (errorRate));
  em->SetAttribute ("ErrorUnit", EnumValue (RateErrorModel::ERROR_UNIT_PACKET)); 

  TypeId tcpTid;
  NS_ABORT_MSG_UNLESS (TypeId::LookupByNameFailSafe (transport_prot, &tcpTid), "TypeId " << transport_prot << " not found");
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (transport_prot)));
  
  Header* temp_header = new Ipv4Header ();
  uint32_t ip_header = temp_header->GetSerializedSize ();
  delete temp_header;
  temp_header = new TcpHeader ();
  uint32_t tcp_header = temp_header->GetSerializedSize ();
  delete temp_header;
  uint32_t tcp_adu_size = mtu_bytes - (ip_header + tcp_header);

  double start_time = 1.0;
  double sink_start_time = 0.0;
  double stop_time = start_time + duration;

  NodeContainer nodes;
  nodes.Create (5);
  NodeContainer source_n (nodes.Get (0));
  NodeContainer r1_n (nodes.Get (1));
  NodeContainer r2_n (nodes.Get (2));
  NodeContainer dest1_n (nodes.Get (3));
  NodeContainer dest2_n (nodes.Get (4));

  InternetStackHelper stack;
  stack.Install (nodes);

  PointToPointHelper p2pAccessFast;
  p2pAccessFast.SetDeviceAttribute ("DataRate", StringValue ("100Mbps"));
  p2pAccessFast.SetChannelAttribute ("Delay", StringValue ("0.01ms"));

  PointToPointHelper p2pBottleneck;
  p2pBottleneck.SetDeviceAttribute ("DataRate", StringValue (dataRate));
  p2pBottleneck.SetChannelAttribute ("Delay", StringValue (delay));
  p2pBottleneck.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (em));

  PointToPointHelper p2pAccessSlow;
  p2pAccessSlow.SetDeviceAttribute ("DataRate", StringValue ("100Mbps"));
  p2pAccessSlow.SetChannelAttribute ("Delay", StringValue ("50ms"));
  
  NetDeviceContainer d_s_r1 = p2pAccessFast.Install (source_n.Get (0), r1_n.Get (0));
  NetDeviceContainer d_r1_r2 = p2pBottleneck.Install (r1_n.Get (0), r2_n.Get (0));
  NetDeviceContainer d_r2_d1 = p2pAccessFast.Install (r2_n.Get (0), dest1_n.Get (0)); 
  NetDeviceContainer d_r2_d2 = p2pAccessSlow.Install (r2_n.Get (0), dest2_n.Get (0)); 

  TrafficControlHelper tchPfifo;
  tchPfifo.SetRootQueueDisc ("ns3::PfifoFastQueueDisc");
  tchPfifo.Install (d_s_r1);
  tchPfifo.Install (d_r1_r2);
  tchPfifo.Install (d_r2_d1);
  tchPfifo.Install (d_r2_d2);

  Ipv4AddressHelper address;
  Ipv4InterfaceContainer i_s_r1, i_r1_r2, i_r2_d1, i_r2_d2;

  address.SetBase ("10.1.1.0", "255.255.255.0");
  i_s_r1 = address.Assign (d_s_r1);
  
  address.SetBase ("10.1.2.0", "255.255.255.0");
  i_r1_r2 = address.Assign (d_r1_r2);

  address.SetBase ("10.1.3.0", "255.255.255.0");
  i_r2_d1 = address.Assign (d_r2_d1); 

  address.SetBase ("10.1.4.0", "255.255.255.0");
  i_r2_d2 = address.Assign (d_r2_d2); 

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  uint16_t port = 50000;
  Ipv4Address dest1Ip = i_r2_d1.GetAddress (1); 
  Ipv4Address dest2Ip = i_r2_d2.GetAddress (1); 

  uint16_t flowsToDest1 = nFlows / 2;
  ApplicationContainer sourceApps;
  ApplicationContainer sinkApps;

  for (uint16_t i = 0; i < nFlows; i++)
  {
    AddressValue remoteAddress;
    Ptr<Node> sinkNode;
    uint16_t currentPort = port + i; 

    if (i < flowsToDest1)
    {
      remoteAddress = AddressValue(InetSocketAddress (dest1Ip, currentPort));
      sinkNode = dest1_n.Get (0);
    }
    else
    {
      remoteAddress = AddressValue(InetSocketAddress (dest2Ip, currentPort));
      sinkNode = dest2_n.Get (0);
    }

    PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory",
                                 InetSocketAddress (Ipv4Address::GetAny (), currentPort)); 
    ApplicationContainer sinkApp = sinkHelper.Install (sinkNode);
    sinkApp.Start (Seconds (sink_start_time));
    sinkApp.Stop (Seconds (stop_time));
    sinkApps.Add (sinkApp);

    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (tcp_adu_size));
    BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
    ftp.SetAttribute ("Remote", remoteAddress); 
    ftp.SetAttribute ("SendSize", UintegerValue (tcp_adu_size));
    ftp.SetAttribute ("MaxBytes", UintegerValue (data_mbytes * 1000000));

    ApplicationContainer sourceApp = ftp.Install (source_n.Get (0));
    sourceApp.Start (Seconds (start_time));
    sourceApp.Stop (Seconds (stop_time - 1.0));
    sourceApps.Add (sourceApp);
  }
  
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> monitor = flowHelper.InstallAll ();

  Simulator::Stop (Seconds (stop_time));
  Simulator::Run ();

  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();

  double totalGoodput_d1 = 0.0;
  double totalGoodput_d2 = 0.0;
  uint32_t count_d1 = 0;
  uint32_t count_d2 = 0;

  std::cout << "--- Lab 2, Part 2: " << transport_prot << " Results ---" << std::endl;
  std::cout << "nFlows: " << nFlows << ", Run: " << run << std::endl;
  std::cout << "-----------------------------------------------" << std::endl;

  for (auto it = stats.begin (); it != stats.end (); ++it)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (it->first);
      if (t.sourceAddress == i_s_r1.GetAddress(0))
        {
          double flowDuration = 19.0;
          
          double goodput_bps = (it->second.rxBytes * 8.0) / flowDuration;

          if (t.destinationAddress == dest1Ip)
            {
              totalGoodput_d1 += goodput_bps;
              count_d1++;
            }
          else if (t.destinationAddress == dest2Ip)
            {
              totalGoodput_d2 += goodput_bps;
              count_d2++;
            }
        }
    }
  
  double avgGoodput_d1 = (count_d1 > 0) ? (totalGoodput_d1 / count_d1) : 0;
  double avgGoodput_d2 = (count_d2 > 0) ? (totalGoodput_d2 / count_d2) : 0;

  std::cout << "Resultados para o Run: " << run << std::endl;
  std::cout << "Média Goodput (dest1): " << avgGoodput_d1 << " bps (baseado em " << count_d1 << " fluxos)" << std::endl;
  std::cout << "Média Goodput (dest2): " << avgGoodput_d2 << " bps (baseado em " << count_d2 << " fluxos)" << std::endl;
  std::cout << "-----------------------------------------------" << std::endl;


  if (flow_monitor)
    {
      monitor->SerializeToXmlFile (prefix_file_name + ".flowmonitor", true, true);
    }

  Simulator::Destroy ();
  return 0;
}