/*
 * Copyright (c) 2018-2019, The Mybtcfx Developers.
 * Portions Copyright (c) 2012-2017, The CryptoNote Developers, The Bytecoin Developers.
 *
 * This file is part of 4xBIT.
 *
 * This file is subject to the terms and conditions defined in the
 * file 'LICENSE', which is part of this source code package.
 */

#include "HttpServer.h"
#include <boost/scope_exit.hpp>

#include <HTTP/HttpParser.h>
#include <System/InterruptedException.h>
#include <System/TcpStream.h>
#include <System/Ipv4Address.h>

using namespace Logging;

namespace CryptoNote {

HttpServer::HttpServer(System::Dispatcher& dispatcher, Logging::ILogger& log)
  : m_dispatcher(dispatcher), workingContextGroup(dispatcher), logger(log, "HttpServer") {

}

void HttpServer::start(const std::string& address, uint16_t port) {
  m_listener = System::TcpListener(m_dispatcher, System::Ipv4Address(address), port);
  workingContextGroup.spawn(std::bind(&HttpServer::acceptLoop, this));
}

void HttpServer::stop() {
  workingContextGroup.interrupt();
  workingContextGroup.wait();
}

void HttpServer::acceptLoop() {
  try {
    System::TcpConnection connection;
    bool accepted = false;

    while (!accepted) {
      try {
        connection = m_listener.accept();
        accepted = true;
      } catch (System::InterruptedException&) {
        throw;
      } catch (std::exception&) {
        // try again
      }
    }

    m_connections.insert(&connection);
    BOOST_SCOPE_EXIT_ALL(this, &connection) { 
      m_connections.erase(&connection); };

    auto addr = connection.getPeerAddressAndPort();

    logger(DEBUGGING) << "Incoming connection from " << addr.first.toDottedDecimal() << ":" << addr.second;

    workingContextGroup.spawn(std::bind(&HttpServer::acceptLoop, this));

    System::TcpStreambuf streambuf(connection);
    std::iostream stream(&streambuf);
    HttpParser parser;

    for (;;) {
      HttpRequest req;
      HttpResponse resp;

      parser.receiveRequest(stream, req);
      processRequest(req, resp);

      stream << resp;
      stream.flush();

      if (stream.peek() == std::iostream::traits_type::eof()) {
        break;
      }
    }

    logger(DEBUGGING) << "Closing connection from " << addr.first.toDottedDecimal() << ":" << addr.second << " total=" << m_connections.size();

  } catch (System::InterruptedException&) {
  } catch (std::exception& e) {
    logger(WARNING) << "Connection error: " << e.what();
  }
}

}
