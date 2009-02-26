/*
 * Copyright (C) 2003 Tommi Maekitalo
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * As a special exception, you may use this file as part of a free
 * software library without restriction. Specifically, if other files
 * instantiate templates or use macros or inline functions from this
 * file, or you compile this file and link it with other files to
 * produce an executable, this file does not by itself cause the
 * resulting executable to be covered by the GNU General Public
 * License. This exception does not however invalidate any other
 * reasons why the executable file might be covered by the GNU Library
 * General Public License.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include "tnt/listener.h"
#include "tnt/tntnet.h"
#include "tnt/job.h"
#include <cxxtools/log.h>
#include <cxxtools/net.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef HAVE_TCP_DEFER_ACCEPT
#  include <cxxtools/systemerror.h>
#endif

#ifdef WITH_GNUTLS
#  include "tnt/gnutls.h"
#endif

#ifdef WITH_OPENSSL
#  include "tnt/openssl.h"
#endif

#ifdef HAVE_TCP_DEFER_ACCEPT
#  include <netinet/tcp.h>
#endif

log_define("tntnet.listener")

static void doListenRetry(cxxtools::net::Server& server,
  const char* ipaddr, unsigned short int port)
{
  fcntl(server.getFd(), F_SETFD, FD_CLOEXEC);

  for (unsigned n = 1; true; ++n)
  {
    try
    {
      log_debug("listen " << ipaddr << ':' << port);
      server.listen(ipaddr, port, tnt::Listener::getBacklog());

#ifdef HAVE_TCP_DEFER_ACCEPT
      int deferSecs = 30;
      if (::setsockopt(server.getFd(), SOL_TCP, TCP_DEFER_ACCEPT,
          &deferSecs, sizeof(deferSecs)) < 0)
        throw cxxtools::SystemError("setsockopt(TCP_DEFER_ACCEPT)");
#endif

      return;
    }
    catch (const cxxtools::net::AddressInUse& e)
    {
      log_debug("cxxtools::net::AddressInUse");
      if (n > tnt::Listener::getListenRetry())
      {
        log_debug("rethrow exception");
        throw;
      }
      log_warn("address " << ipaddr << ':' << port << " in use - retry; n = " << n);
      ::sleep(1);
    }
  }
}

namespace tnt
{
  void ListenerBase::doStop()
  {
    log_info("stop listener " << ipaddr << ':' << port);
    try
    {
      // connect once to wake up listener, so it will check stop-flag
      cxxtools::net::Stream(ipaddr, port);
    }
    catch (const std::exception& e)
    {
      log_warn("error waking up listener: " << e.what() << " try 127.0.0.1");
      cxxtools::net::Stream("127.0.0.1", port);
    }
  }

  void ListenerBase::closePorts() 
  {
  }

  int Listener::backlog = 16;
  unsigned Listener::listenRetry = 5;

  Listener::Listener(Tntnet& application, const std::string& ipaddr, unsigned short int port, Jobqueue& q)
    : ListenerBase(ipaddr, port),
      queue(q)
  {
    log_info("listen ip=" << ipaddr << " port=" << port);
    doListenRetry(server, ipaddr.c_str(), port);
    queue.put(new Tcpjob(application, server, queue));
  }

  void Listener::closePorts()
  {
    server.close();
  }

#ifdef WITH_GNUTLS
#define USE_SSL

#endif

#ifdef WITH_OPENSSL
#define USE_SSL

#endif

#ifdef USE_SSL
  Ssllistener::Ssllistener(Tntnet& application, const char* certificateFile,
      const char* keyFile,
      const std::string& ipaddr, unsigned short int port,
      Jobqueue& q)
    : ListenerBase(ipaddr, port),
      server(certificateFile, keyFile),
      queue(q)
  {
    log_info("listen ip=" << ipaddr << " port=" << port << " (ssl)");
    doListenRetry(server, ipaddr.c_str(), port);
    queue.put(new SslTcpjob(application, server, queue));
  }

  void Ssllistener::closePorts()
  {
    server.close();
  }


#endif // USE_SSL

}
