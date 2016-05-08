// ----------------------------------------------------------------------------
//
//     Author     : Benny Bach <benny.bach@gmail.com>
//                  Copyright (C) 2015
//
// --- Description: -----------------------------------------------------------
//
//
// ----------------------------------------------------------------------------
#include "http/http_connection.h"

// ----------------------------------------------------------------------------
#include "player/player.h"

// ----------------------------------------------------------------------------
#include <dripcore/loop.h>
#include <dripcore/task.h>
#include <dripcore/tcp_server.h>

// ----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
  dripcore::loop loop;

  musicbox::player::start(&loop);

  auto on_connection = [&](dripcore::socket socket) {
    loop.spawn<http_connection>(std::move(socket));
  };

  loop.spawn<dripcore::tcp_server>("0.0.0.0", 4321, on_connection);
  loop.run();

  return 0;
}
