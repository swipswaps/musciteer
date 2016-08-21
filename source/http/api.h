// ----------------------------------------------------------------------------
//
//     Author     : Benny Bach <benny.bach@gmail.com>
//                  Copyright (C) 2015
//
// ----------------------------------------------------------------------------
#ifndef __http__api_h__
#define __http__api_h__

// ----------------------------------------------------------------------------
#include "../dm/track.h"

// ----------------------------------------------------------------------------
#include <json.h>

// ----------------------------------------------------------------------------
namespace musciteer
{
  inline json to_json(const musciteer::dm::track& track)
  {
    json artists;

    for ( auto& artist : track.artists() )
    {
      json j = {
        { "id",   artist.id() },
        { "name", artist.name() }
      };
      artists.push_back(j);
    }

    const auto& album = track.album();

    json jalbum = {
      { "id",    album.id() },
      { "title", album.title() },
    };

    json t = {
      { "id", track.id() },
      { "title", track.title() },
      { "tn", track.track_number() },
      { "dn", track.disc_number() },
      { "duration", track.duration() },
      { "artists", artists },
      { "album", jalbum },
      { "play_count", track.play_count() },
      { "skip_count", track.skip_count() }
    };

    return t;
  }

  inline json to_json(const musciteer::dm::album& album)
  {
    auto artist = album.artist();

    json jalbum = {
      { "id", album.id() },
      { "title", album.title() },
      //{ "tracks", album.track_ids() },
      { "artist", {
        { "id", artist.id() },
        { "name", artist.name() } }
      },
      { "cover", "/api/albums/"+album.id()+"/cover" }
    };

    return jalbum;
  }
}

// ----------------------------------------------------------------------------
#endif