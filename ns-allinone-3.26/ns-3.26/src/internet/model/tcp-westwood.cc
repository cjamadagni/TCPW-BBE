/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013 ResiliNets, ITTC, University of Kansas 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Siddharth Gangadhar <siddharth@ittc.ku.edu>,
 *          Truc Anh N. Nguyen <annguyen@ittc.ku.edu>,
 *          Greeshma Umapathi
 *
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  http://wiki.ittc.ku.edu/resilinets
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 *
 * Work supported in part by NSF FIND (Future Internet Design) Program
 * under grant CNS-0626918 (Postmodern Internet Architecture),
 * NSF grant CNS-1050226 (Multilayer Network Resilience Analysis and Experimentation on GENI),
 * US Department of Defense (DoD), and ITTC at The University of Kansas.
 */

#include "tcp-westwood.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "rtt-estimator.h"
#include "tcp-socket-base.h"

NS_LOG_COMPONENT_DEFINE ("TcpWestwood");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (TcpWestwood);

TypeId
TcpWestwood::GetTypeId (void)
{
  static TypeId tid = TypeId("ns3::TcpWestwood")
    .SetParent<TcpNewReno>()
    .SetGroupName ("Internet")
    .AddConstructor<TcpWestwood>()
    .AddAttribute("FilterType", "Use this to choose no filter or Tustin's approximation filter",
                  EnumValue(TcpWestwood::TUSTIN), MakeEnumAccessor(&TcpWestwood::m_fType),
                  MakeEnumChecker(TcpWestwood::NONE, "None", TcpWestwood::TUSTIN, "Tustin"))
    .AddAttribute("ProtocolType", "Use this to let the code run as Westwood or WestwoodPlus",
                  EnumValue(TcpWestwood::WESTWOOD),
                  MakeEnumAccessor(&TcpWestwood::m_pType),
                  MakeEnumChecker(TcpWestwood::WESTWOOD, "Westwood",TcpWestwood::WESTWOODPLUS, "WestwoodPlus"))
    .AddTraceSource("EstimatedBW", "The estimated bandwidth",
                    MakeTraceSourceAccessor(&TcpWestwood::m_currentBW),
                    "ns3::TracedValueCallback::Double")
  ;
  return tid;
}

TcpWestwood::TcpWestwood (void) :
  TcpNewReno (),
  m_currentBW (0),
  m_lastSampleBW (0),
  m_lastBW (0),
  m_minRtt (Time (0)),
  m_maxRtt (Time (0)),
  m_curRtt (Time (0)),
  m_ackedSegments (0),
  m_IsCount (false)
{
  NS_LOG_FUNCTION (this);
}

TcpWestwood::TcpWestwood (const TcpWestwood& sock) :
  TcpNewReno (sock),
  m_currentBW (sock.m_currentBW),
  m_lastSampleBW (sock.m_lastSampleBW),
  m_lastBW (sock.m_lastBW),
  m_minRtt (Time (0)),
  m_maxRtt (Time (0)),
  m_curRtt (Time (0)),
  m_pType (sock.m_pType),
  m_fType (sock.m_fType),
  m_IsCount (sock.m_IsCount)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor");
}

TcpWestwood::~TcpWestwood (void)
{
}

void
TcpWestwood::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t packetsAcked,
                        const Time& rtt)
{
  NS_LOG_FUNCTION (this << tcb << packetsAcked << rtt);

  if (rtt.IsZero ())
    {
      NS_LOG_WARN ("RTT measured is zero!");
      return;
    }

  m_ackedSegments += packetsAcked;

  // Update minRtt
  if (m_minRtt.IsZero ())
    {
      m_minRtt = rtt;
    }
  else
    {
      if (rtt < m_minRtt)
        {
          m_minRtt = rtt;
        }
    }

  // Update maxRtt
  if (m_maxRtt.IsZero ())
    {
      m_maxRtt = rtt;
      m_prevMaxRtt = static_cast<double> (m_maxRtt.GetSeconds ());
    }
  else
    {
      if (rtt > m_maxRtt)
        {
          m_prevMaxRtt = static_cast<double> (m_maxRtt.GetSeconds ());
          m_maxRtt = rtt;
        }
    }

  // Update curRTT
  m_curRtt = rtt;

  NS_LOG_LOGIC ("MinRtt: " << m_minRtt.GetMilliSeconds () << "ms");
  NS_LOG_LOGIC ("MaxRtt: " << m_maxRtt.GetMilliSeconds () << "ms");
  NS_LOG_LOGIC ("CurRtt: " << m_curRtt.GetMilliSeconds () << "ms");

  if (m_pType == TcpWestwood::WESTWOOD)
    {
      EstimateBW (rtt, tcb);
    }
  else if (m_pType == TcpWestwood::WESTWOODPLUS)
    {
      if (!(rtt.IsZero () || m_IsCount))
        {
          m_IsCount = true;
          m_bwEstimateEvent.Cancel ();
          m_bwEstimateEvent = Simulator::Schedule (rtt, &TcpWestwood::EstimateBW,
                                                   this, rtt, tcb);
        }
    }
}

void
TcpWestwood::EstimateBW (const Time &rtt, Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this);

  NS_ASSERT (!rtt.IsZero ());

  m_currentBW = m_ackedSegments * tcb->m_segmentSize / rtt.GetSeconds ();

  if (m_pType == TcpWestwood::WESTWOODPLUS)
    {
      m_IsCount = false;
    }

  m_ackedSegments = 0;
  NS_LOG_LOGIC ("Estimated BW: " << m_currentBW);

  // Filter the BW sample

  double alpha = 0.9;

  if (m_fType == TcpWestwood::NONE)
    {
    }
  else if (m_fType == TcpWestwood::TUSTIN)
    {
      double sample_bwe = m_currentBW;
      m_currentBW = (alpha * m_lastBW) + ((1 - alpha) * ((sample_bwe + m_lastSampleBW) / 2));
      m_lastSampleBW = sample_bwe;
      m_lastBW = m_currentBW;
    }

  NS_LOG_LOGIC ("Estimated BW after filtering: " << m_currentBW);
}

uint32_t
TcpWestwood::GetSsThresh (Ptr<const TcpSocketState> tcb,
                          uint32_t bytesInFlight)
{
  (void) bytesInFlight;
  NS_LOG_LOGIC ("CurrentBW: " << m_currentBW << " minRtt: " <<
                m_minRtt << " ssthresh: " <<
                m_currentBW * static_cast<double> (m_minRtt.GetSeconds ()));

  
  //Initializing constants required by TCPW-BBE
  double alpha = 10; 
  double beta = 0.25;
  double e = 2.71;

  // Type casting RTT values to double      
  double currentRTT = static_cast<double> (m_curRtt.GetSeconds ());
  double maxRTT = beta * static_cast<double> (m_maxRtt.GetSeconds ()) + (1-beta) * m_prevMaxRtt ;
  //maxRTT = std::max (maxRTT, (currentRTT + 8*10 / m_currentBW)) if m is bytes, why have they given packets in the paper?
  double minRTT = static_cast<double> (m_minRtt.GetSeconds ());
       

  // calculating d and dMax      
  double dMax = maxRTT - minRTT;
  double d = currentRTT - minRTT;

  // calculating U
  double u; 
  if (dMax != 0)            
     u = 1.0 / std::pow(e, ((d/dMax)*alpha));
  else
     u = 1.0;
  
  //std::cout<<"U = "<<u<<"\n";

  // calculating ERE              
  double ere = u * m_currentBW + (1-u) * (tcb->m_cWnd / currentRTT);
  //std::cout<<"ERE = "<<ere<<"\n";
  
  // calculating ssthresh
  uint32_t ssthresh = uint32_t (currentRTT * (dMax / (d + dMax)) * ere);
  //std::cout<<"ssthresh = "<<ssthresh<<"\n\n";

  if (tcb->m_cWnd > ssthresh && ssthresh!=0)      
    return ssthresh;
  else  
  	return std::max (2*tcb->m_segmentSize, uint32_t (m_currentBW * static_cast<double> (m_minRtt.GetSeconds ())));
              
        
}

Ptr<TcpCongestionOps>
TcpWestwood::Fork ()
{
  return CreateObject<TcpWestwood> (*this);
}

} // namespace ns3
