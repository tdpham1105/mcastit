/*******************************************************************************
 * Panasonic Avionics Corporation
 *
 * Copyright (c) Nov 23, 2017   All Rights Reserved
 *
 * Filename: McastModuleInterface.cpp
 *
 * Description: 
 *
 * Limitations: 
 *
 * Environment: ANSI C, X-series
 *
 * Version: @(#) (Internal Only)
 *
 * History: 
 *
 ******************************************************************************/

#include "McastModuleInterface.h"

McastModuleInterface::McastModuleInterface(const vector<IfaceData>& ifaces,
    const string& mcastAddress, int mcastPort, bool useIpV6) :
    mIfaces(ifaces), mMcastAddress(mcastAddress), mMcastPort(mcastPort), mIsIpV6(useIpV6)
{
  string ipVer = (useIpV6)? "IPV6" : "IPV4";
  cout << "MCAST with " << ipVer << " " << mcastAddress << " port " << mcastPort << endl;
  mAckPort = mMcastPort + 1;
}

bool McastModuleInterface::isIpV6() const
{
  return mIsIpV6;
}

bool McastModuleInterface::associateMcastWithIfaceName(int fd,
    const char* ifaceName, bool isLoopBack)
{
  // Set the TTL hop count
  int opt = 6;
  int res = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, (void*) &opt, sizeof(opt));
  if (0 > res)
  {
    LOG_ERROR("sockopt TTL: " << strerror(errno));
    return false;
  }

  // Allow broadcast on this socket.
  opt = 1;
  res = setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (void*) &opt, sizeof(opt));
  if (0 > res)
  {
    LOG_ERROR("sockopt SO_BROADCAST: " << strerror(errno));
    return false;
  }

  // Enable reuse ip/port
  if (-1 == setReuseSocket(fd))
  {
    LOG_ERROR("sockopt cannot reuse socket " << fd);
    return false;
  }

  // Enable loop back if set.
  opt = isLoopBack;
  res = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (void*) &opt, sizeof(opt));
  if (0 > res)
  {
    LOG_ERROR("sockopt IP_MULTICAST_LOOP: " << strerror(errno));
    return false;
  }

  // Next, bind socket to a fix port
  in_addr_t meSinAddr = htonl(INADDR_ANY);
  vector<string> meIps;
  if (0 == getIfaceIPFromIfaceName(ifaceName, meIps, false))
  {
    meSinAddr = inet_addr(meIps[0].c_str());
  }

  struct sockaddr_in me_addr;
  memset((char*) &me_addr, 0, sizeof(me_addr));
  me_addr.sin_port = htons(0);
  me_addr.sin_family = AF_INET;
  me_addr.sin_addr.s_addr = meSinAddr;
  if (-1 == ::bind(fd, (struct sockaddr*)&me_addr, sizeof(me_addr)))
  {
    LOG_ERROR("sockopt bind: " << strerror(errno));
    return false;
  }

  // If iface name specified, set iface name for the socket
  if (0 < strlen(ifaceName))
  {
    struct ip_mreqn ifaddrn;
    memset(&ifaddrn, 0, sizeof(ifaddrn));

    ifaddrn.imr_ifindex = if_nametoindex(ifaceName);
    if (0 != (res = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ifaddrn, sizeof(ifaddrn))))
    {
      LOG_ERROR("sockopt IP_MULTICAST_IF: " << strerror(errno));
    }
  }

  return true;
}

bool McastModuleInterface::associateMcastV6WithIfaceName(int fd,
    const char* ifaceName, bool isLoopBack)
{
  // Set the TTL hop count
  int opt = 6;
  int res = setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (void*) &opt, sizeof(opt));
  if (0 > res)
  {
    LOG_ERROR("sockopt TTL: " << strerror(errno));
    return false;
  }

  // Allow broadcast on this socket.
  opt = 1;
  res = setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (void*) &opt, sizeof(opt));
  if (0 > res)
  {
    LOG_ERROR("sockopt SO_BROADCAST: " << strerror(errno));
    return false;
  }

  // Enable reuse ip/port
  if (-1 == setReuseSocket(fd))
  {
    LOG_ERROR("sockopt cannot reuse socket " << fd);
    return false;
  }

  // Enable loop back if set.
  opt = isLoopBack;
  res = setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (void*) &opt, sizeof(opt));
  if (0 > res)
  {
    LOG_ERROR("sockopt IPV6_MULTICAST_LOOP: " << strerror(errno));
    return false;
  }

  // Bind the socket to the multicast port.
  struct sockaddr_in6 bindAddr6;
  memset(&bindAddr6, 0, sizeof(bindAddr6));

  vector<string> meIps;
  if (0 == getIfaceIPFromIfaceName(ifaceName, meIps, true))
  {
    if (1 != inet_pton(AF_INET6, meIps[0].c_str(), &(bindAddr6.sin6_addr)))
    {
      LOG_ERROR("Error parsing address for " << mMcastAddress);
      return -1;
    }
  }

  bindAddr6.sin6_family = AF_INET6;
  bindAddr6.sin6_port = htons(0);
  res = ::bind(fd, (struct sockaddr *) &bindAddr6, sizeof(bindAddr6));
  if (0 > res)
  {
    LOG_ERROR("bind: " << strerror(errno));
    return -1;
  }

  // If iface name specified, set iface name for the socket
  if (0 < strlen(ifaceName))
  {
    unsigned ifIndex = if_nametoindex(ifaceName);
    if (0 != (res = setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifIndex, sizeof(ifIndex))))
    {
      LOG_ERROR("sockopt IPV6_MULTICAST_IF: " << strerror(errno));
      return false;
    }
  }

  return true;
}

int McastModuleInterface::joinMcastIface(int sock, const char* ifaceName)
{
  // Set the recv buffer size.
  uint32_t buffSz = MCAST_BUFF_LEN;
  int res = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buffSz, sizeof(buffSz));
  if (0 > res)
  {
    LOG_ERROR("sockopt BuffSz: " << strerror(errno));
    return -1;
  }

  // Let's set reuse port & address to on to allow multiple binds per host.
  if (-1 == setReuseSocket(sock))
  {
    LOG_ERROR("can't reuse socket");
    return -1;
  }

  // Bind the socket to the multicast port.
  struct sockaddr_in bindAddr;
  memset(&bindAddr, 0, sizeof(bindAddr));
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  bindAddr.sin_port = htons(mMcastPort);
  res = ::bind(sock, (struct sockaddr *) &bindAddr, sizeof(bindAddr));
  if (0 > res && errno != EINVAL)
  // ignore EINVAL for allowing multiple mcast interfaces in same socket
  {
    LOG_ERROR("bind: " << strerror(errno));
    return -1;
  }

  // If ifaceName is specified, bind directly to that iface,
  // otherwise bind to general interface
  if (0 == strlen(ifaceName))
  {
    struct ip_mreq mcastReq;
    mcastReq.imr_multiaddr.s_addr = inet_addr(mMcastAddress.c_str());
    mcastReq.imr_interface.s_addr = htonl(INADDR_ANY);
    res = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*) &mcastReq, sizeof(mcastReq));
  }
  else
  {
    struct ip_mreqn mcastReqn;
    mcastReqn.imr_multiaddr.s_addr = inet_addr(mMcastAddress.c_str());
    mcastReqn.imr_ifindex = if_nametoindex(ifaceName);
    res = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*) &mcastReqn, sizeof(mcastReqn));
  }

  if (0 != res)
  {
    printf("Error: join mcast group<%s> interface<%s>: %s\n",
              mMcastAddress.c_str(), ifaceName, strerror(errno));
  }

  return res;
}

int McastModuleInterface::joinMcastIfaceV6(int sock, const char* ifaceName)
{
  // Set the recv buffer size.
  uint32_t buffSz = MCAST_BUFF_LEN;
  int res = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buffSz, sizeof(buffSz));
  if (0 > res)
  {
    LOG_ERROR("sockopt BuffSz: " << strerror(errno));
    return -1;
  }

  // Let's set reuse port to on to allow multiple binds per host.
  if (-1 == setReuseSocket(sock))
  {
    LOG_ERROR("can't reuse socket");
    return -1;
  }

  // Bind the socket to the multicast port.
  struct sockaddr_in6 bindAddr;
  memset(&bindAddr, 0, sizeof(bindAddr));
  bindAddr.sin6_family = AF_INET6;
  bindAddr.sin6_port = htons(mMcastPort);
  res = ::bind(sock, (struct sockaddr *) &bindAddr, sizeof(bindAddr));
  if (0 > res && errno != EINVAL)
    // ignore EINVAL for allowing multiple mcast interfaces in same socket
  {
    LOG_ERROR("bind: " << strerror(errno));
    return -1;
  }

  // If ifaceName is specified, bind directly to that iface,
  // otherwise bind to general interface
  struct ipv6_mreq mcastReq;
  if (inet_pton(AF_INET6, mMcastAddress.c_str(), &mcastReq.ipv6mr_multiaddr) != 1)
  {
    LOG_ERROR("Error parsing address for " << mMcastAddress);
    return -1;
  }

  mcastReq.ipv6mr_interface = if_nametoindex(ifaceName); // no need to check since it will return 0 on error
  res = setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (void*) &mcastReq, sizeof(mcastReq));

  if (0 != res)
  {
    printf("ERR: join mcast GRP<%s> INF<%s> ERR<%s>\n", mMcastAddress.c_str(), ifaceName,
        strerror(errno));
  }

  return res;
}
