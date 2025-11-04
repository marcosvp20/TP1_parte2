
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

NS_LOG_COMPONENT_DEFINE ("lab2-part1"); 



static std::map<uint32_t, bool> firstCwnd;
static std::map<uint32_t, bool> firstSshThr;
static std::map<uint32_t, bool> firstRtt;
static std::map<uint32_t, bool> firstRto;
static std::map<uint32_t, Ptr<OutputStreamWrapper>> cWndStream;
static std::map<uint32_t, Ptr<OutputStreamWrapper>> ssThreshStream;
static std::map<uint32_t, Ptr<OutputStreamWrapper>> rttStream;
static std::map<uint32_t, Ptr<OutputStreamWrapper>> rtoStream;
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
  if (cWndStream.find(nodeId) == cWndStream.end()) return; // Proteção

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
  // Parâmetros do Lab 2
  std::string dataRate = "1Mbps";
  std::string delay = "20ms";
  double errorRate = 0.00001;
  uint16_t nFlows = 1;
  std::string transport_prot = "TcpCubic"; 


  uint64_t data_mbytes = 0; 
  uint32_t mtu_bytes = 1500; 
  double duration = 20.0; 
  uint32_t run = 0;
  bool flow_monitor = true; 
  std::string prefix_file_name = "lab2-part1";

  CommandLine cmd (__FILE__);

  cmd.AddValue ("dataRate", "Bottleneck link data rate", dataRate);
  cmd.AddValue ("delay", "Bottleneck link delay", delay);
  cmd.AddValue ("errorRate", "Bottleneck link error rate", errorRate);
  cmd.AddValue ("nFlows", "Number of TCP flows (max 20)", nFlows);
  cmd.AddValue ("transport_prot", "Transport protocol to use (TcpCubic or TcpNewReno)", transport_prot);
  cmd.AddValue ("run", "Run index (for setting repeatable seeds)", run);
  cmd.Parse (argc, argv);


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


  // Remoção das linhas Config::SetDefault 
  // Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (1 << 21));
  // Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (1 << 21));
  // Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (sack));
  // Config::SetDefault ("ns3::TcpL4Protocol::RecoveryType", ...); 


  // Modelo de erro de fifth.cc 
  Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
  em->SetAttribute ("ErrorRate", DoubleValue (errorRate)); //
  


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
  nodes.Create (4);
  NodeContainer source_n (nodes.Get (0));
  NodeContainer r1_n (nodes.Get (1));
  NodeContainer r2_n (nodes.Get (2));
  NodeContainer dest_n (nodes.Get (3));

  InternetStackHelper stack;
  stack.Install (nodes);

  // Link 1: source -> r1 (100 Mbps, 0.01 ms)
  PointToPointHelper p2pAccess;
  p2pAccess.SetDeviceAttribute ("DataRate", StringValue ("100Mbps"));
  p2pAccess.SetChannelAttribute ("Delay", StringValue ("0.01ms"));

  // Link 2: r1 -> r2 (Gargalo) 
  PointToPointHelper p2pBottleneck;
  p2pBottleneck.SetDeviceAttribute ("DataRate", StringValue (dataRate));
  p2pBottleneck.SetChannelAttribute ("Delay", StringValue (delay));
  p2pBottleneck.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (em)); 

  // Link 3: r2 -> dest (100 Mbps, 0.01 ms) 
  
  NetDeviceContainer d_s_r1 = p2pAccess.Install (source_n.Get (0), r1_n.Get (0));
  NetDeviceContainer d_r1_r2 = p2pBottleneck.Install (r1_n.Get (0), r2_n.Get (0));
  NetDeviceContainer d_r2_d = p2pAccess.Install (r2_n.Get (0), dest_n.Get (0));


  TrafficControlHelper tchPfifo;
  tchPfifo.SetRootQueueDisc ("ns3::PfifoFastQueueDisc");
  tchPfifo.Install (d_s_r1);
  tchPfifo.Install (d_r1_r2);
  tchPfifo.Install (d_r2_d);


  Ipv4AddressHelper address;
  Ipv4InterfaceContainer i_s_r1, i_r1_r2, i_r2_d;

  address.SetBase ("10.1.1.0", "255.255.255.0");
  i_s_r1 = address.Assign (d_s_r1);
  
  address.SetBase ("10.1.2.0", "255.255.255.0");
  i_r1_r2 = address.Assign (d_r1_r2);

  address.SetBase ("10.1.3.0", "255.255.255.0");
  i_r2_d = address.Assign (d_r2_d);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
--
  uint16_t port = 50000;
  Ipv4Address destIp = i_r2_d.GetAddress (1); // IP do nó 'dest'

  // Sink Application (no nó 'dest')
  PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApp = sinkHelper.Install (dest_n.Get (0));
  sinkApp.Start (Seconds (sink_start_time)); 
  sinkApp.Stop (Seconds (stop_time));

 
  ApplicationContainer sourceApps;
  for (uint16_t i = 0; i < nFlows; i++)
    {
      AddressValue remoteAddress (InetSocketAddress (destIp, port + i)); 
      Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (tcp_adu_size));
      BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
      ftp.SetAttribute ("Remote", remoteAddress);
      ftp.SetAttribute ("SendSize", UintegerValue (tcp_adu_size));
      ftp.SetAttribute ("MaxBytes", UintegerValue (data_mbytes * 1000000)); 

      ApplicationContainer sourceApp = ftp.Install (source_n.Get (0));
      sourceApp.Start (Seconds (start_time)); 
      sourceApp.Stop (Seconds (stop_time - 1.0)); 
      sourceApps.Add (sourceApp);
      
      
      
      firstCwnd[0] = true;
      firstSshThr[0] = true;
      std::string flowString = "-flow" + std::to_string(i);
      Simulator::Schedule (Seconds (start_time + 0.00001), &TraceCwnd,
                           prefix_file_name + flowString + "-cwnd.data", 0, i);
      Simulator::Schedule (Seconds (start_time + 0.00001), &TraceSsThresh,
                           prefix_file_name + flowString + "-ssth.data", 0, i);
    }

  
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> monitor = flowHelper.InstallAll ();

  Simulator::Stop (Seconds (stop_time));
  Simulator::Run ();

  
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();

  double totalGoodput = 0.0;

  std::cout << "--- Lab 2, Part 1: " << transport_prot << " Results ---" << std::endl;
  std::cout << "nFlows: " << nFlows << ", dataRate: " << dataRate
            << ", delay: " << delay << ", errorRate: " << errorRate << std::endl;
  std::cout << "-----------------------------------------------" << std::endl;

  for (auto it = stats.begin (); it != stats.end (); ++it)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (it->first);
    
      if (t.sourceAddress == i_s_r1.GetAddress(0) && t.destinationAddress == destIp)
        {
     
          double flowDuration = 19.0;
          

          
          double goodput_bps = (it->second.rxBytes * 8.0) / flowDuration;
          totalGoodput += goodput_bps;

          std::cout << "Flow " << it->first << " (Src: " << t.sourceAddress 
                    << " Dst: " << t.destinationAddress << ")" << std::endl;
          std::cout << "  Tx Packets: " << it->second.txPackets << std::endl;
          std::cout << "  Rx Packets: " << it->second.rxPackets << std::endl;
          std::cout << "  Total Rx Bytes: " << it->second.rxBytes << std::endl;
          std::cout << "  Flow Duration (s): " << flowDuration << std::endl;
          std::cout << "  Goodput (bps): " << goodput_bps << std::endl;
        }
    }
  
  std::cout << "-----------------------------------------------" << std::endl;
  std::cout << "Total Aggregate Goodput (bps): " << totalGoodput << std::endl;
  std::cout << "-----------------------------------------------" << std::endl;


  if (flow_monitor)
    {
      monitor->SerializeToXmlFile (prefix_file_name + ".flowmonitor", true, true);
    }

  Simulator::Destroy ();
  return 0;
}
