// ----------------------------------------------------------------------------
//
//     Author     : Benny Bach <benny.bach@gmail.com>
//                  Copyright (C) 2015
//
// --- Description: -----------------------------------------------------------
//
//
// ----------------------------------------------------------------------------
#ifndef __http__websocket_handler_base_h__
#define __http__websocket_handler_base_h__

// ----------------------------------------------------------------------------
#include <http/request.h>
#include <http/websocket.h>
#include <http/base64.h>

// ----------------------------------------------------------------------------
namespace http
{
  class websocket_handler_base
  {
  public:
    websocket_handler_base(http::request_environment& env);
  public:
    virtual ~websocket_handler_base();
  public:
    void call(const std::string& path);
  protected:
    void switch_protocol(http::request_environment& env);
  protected:
    void dispatch(http::websocket::header& header, std::istream& payload);
  protected:
    virtual void on_connect() {}
    virtual void on_close() {}
    virtual void on_message(const std::string& message) {}
  public:
    void send_message(const std::string& message);
  protected:
    http::request_environment& env;
  private:
    static constexpr const char* crlf = "\r\n";
    static constexpr const char* guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  };
}
// ----------------------------------------------------------------------------
#endif
