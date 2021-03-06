// ----------------------------------------------------------------------------
//
//     Author     : Benny Bach <benny.bach@gmail.com>
//                  Copyright (C) 2016
//
// ----------------------------------------------------------------------------
#include "player_task.h"
#include "player_session.h"
#include "player_album_provider.h"
#include "player_tag_provider.h"

// ----------------------------------------------------------------------------
#include "../dm/tracks.h"
#include "../dm/albums.h"
#include "../dm/player.h"

// ----------------------------------------------------------------------------
#include <cassert>

// ----------------------------------------------------------------------------
namespace musciteer
{
  player_task::player_task(message_channel message_ch)
    :
    state_(stopped),
    message_ch_(message_ch),
    audio_output_(),
    audio_output_device_(),
    observers_(),
    play_q_(),
    list_provider_(),
    tag_provider_re_("pl:tag:(.*)"),
    continuous_playback_(true),
    ctpb_provider_(this),
    replaygain_enabled_(false)
  {
  }

  player_task::~player_task()
  {
  }

  void player_task::main()
  {
    load_settings();

    while ( true )
    {
      auto msg = message_ch_.recv(this);

      if ( msg.type == 0 ) {
        break;
      }

      dispatch(msg);
    }
  }

  void player_task::load_settings()
  {
    musciteer::dm::player settings;

    audio_output_device_ = settings.audio_device();
    continuous_playback_ = settings.ctpb_enabled();
    replaygain_enabled_  = settings.replaygain_enabled();

    if ( continuous_playback_ )
    {
      try
      {
        ctpb_provider_.load(settings.ctpb_type());
      }
      catch(const std::exception& e)
      {
        std::cerr << "player_task - failed to load ctpb " << e.what() << std::endl;
      }
    }

    audio_output_.set_replaygain_enabled(replaygain_enabled_);

    std::cout
      << "player_task - loaded settings audio_device=" << audio_output_device_
      << ", ctpb_enabled=" << continuous_playback_
      << ", replaygain_enabled=" << replaygain_enabled_
      << std::endl;

    player_state_notify();
  }

  void player_task::dispatch(message& m)
  {
    switch ( m.type )
    {
      case message::subscribe_id:
        handle(m.subscribe);
        break;
      case message::unsubscribe_id:
        handle(m.unsubscribe);
        break;
      case message::device_list_req_id:
        handle(m.device_list_req);
        break;
      case message::settings_changed_id:
        handle(m.settings_changed);
        break;
      case message::stream_data_req_id:
        handle(m.stream_data_req);
        break;
      case message::play_req_id:
        handle(m.play_req);
        break;
      case message::pause_req_id:
        handle(m.pause_req);
        break;
      case message::stop_req_id:
        handle(m.stop_req);
        break;
      case message::skip_req_id:
        handle(m.skip_req);
        break;
      case message::queue_req_id:
        handle(m.queue_req);
        break;
      case message::stream_begin_notify_id:
        handle(m.stream_begin_notify);
        break;
      case message::stream_progress_notify_id:
        handle(m.stream_progress_notify);
        break;
      case message::stream_end_notify_id:
        handle(m.stream_end_notify);
        break;
      case message::source_notify_id:
        handle(m.source_notify);
        break;
    }
  }

  void player_task::handle(subscribe& m)
  {
    observers_.insert(m.channel);

    // Send source status to new observer.
    for ( const auto& s : source_status_ )
    {
      message n(message::source_notify_id);

      n.source_notify.type = source_notification::id::status;
      n.source_notify.source_name = s.first;
      n.source_notify.message = s.second;

      m.channel.send(std::move(n));
    }

    player_state_notify();
  }

  void player_task::handle(unsubscribe& m)
  {
    observers_.erase(m.channel);
  }

  void player_task::handle(device_list_request& m)
  {
    message r(message::device_list_res_id);

    audio_output_alsa::each([&](std::string&& device_name) {
      r.device_list_res.device_names.push_back(device_name);
    });

    r.device_list_res.current = audio_output_device_;

    m.reply.send(std::move(r));
  }

  void player_task::handle(settings_changed_message& m)
  {
    load_settings();
  }

  void player_task::handle(stream_data_request& m)
  {
    message r(message::stream_data_res_id);

    if ( session_ && session_->id() == m.stream_id )
    {
      r.stream_data_res.stream_id = session_->id();
      r.stream_data_res.track = session_->track();
    }
    else
    {
      r.stream_data_res.stream_id = -1;
    }

    m.reply.send(std::move(r));
  }

  void player_task::handle(play_request& m)
  {
    auto& id = m.id;

    switch ( state_ )
    {
      case stopped:
      {
        if ( id.empty() )
        {
          if ( !play_q_.empty() )
          {
            assert(!session_);

            auto track = play_q_.front();
            play_q_.pop_front();

            become_playing(track);
          }
          else if ( ctpb_provider_.is_loaded() )
          {
            auto track = ctpb_provider_.get_track();

            if ( !track.id_is_null() )
            {
              assert(!session_);
              become_playing(track);
            }
            else
            {
              std::cerr << "player_task - ctpb provider returned empty track!" << std::endl;
            }
          }
        }
        else
        {
          assert(!session_);

          std::smatch match;

          if ( id[0] == 't' )
          {
            auto tracks = musciteer::dm::tracks();

            become_playing(tracks.find_by_id(id));
          }
          else if ( id.length() > 2 && id[0] == 'a' && id[1] == 'l' )
          {
            auto albums = musciteer::dm::albums();
            auto album = albums.find_by_id(id);

            if ( !album.id_is_null() )
            {
              list_provider_ = std::unique_ptr<album_provider>(new album_provider(album));
              become_playing(list_provider_->next());
            }
            else
            {
              std::cerr << "player_task - album with id " << id << " not found!" << std::endl;
            }
          }
          else if ( std::regex_match(id, match, tag_provider_re_) )
          {
            auto& tag = match[1];

            list_provider_ = std::unique_ptr<tag_provider>(new tag_provider(tag));

            if ( !list_provider_->done() ) {
              become_playing(list_provider_->next());
            }
            else {
              std::cerr << "player_task - no tracks with tag " << tag << std::endl;
            }
          }
          else
          {
            std::cerr << "player_task - cannot play " << id << std::endl;
          }
        }
        break;
      }
      case playing:
      {
        std::smatch match;

        if ( id[0] == 't' )
        {
          auto tracks = musciteer::dm::tracks();
          auto track = tracks.find_by_id(id);

          if ( track.id_is_null() )
          {
            // ERROR!
            return;
          }

          // Put track in front of the queue.
          play_q_.push_front(track);
        }
        else if ( id.length() > 2 && id[0] == 'a' && id[1] == 'l' )
        {
          auto albums = musciteer::dm::albums();
          auto album = albums.find_by_id(id);

          if ( !album.id_is_null() ) {
            list_provider_ = std::unique_ptr<album_provider>(new album_provider(album));
          }
          else {
            std::cerr << "player_task - album with id " << id << " not found!" << std::endl;
          }
        }
        else if ( std::regex_match(id, match, tag_provider_re_) )
        {
          auto& tag = match[1];

          list_provider_ = std::unique_ptr<tag_provider>(new tag_provider(tag));

          if ( list_provider_->done() ) {
            std::cerr << "player_task - no tracks with tag " << tag << std::endl;
          }
        }

        // Stop playing track.
        assert(session_);
        session_->stop();
        break;
      }
      case stopping:
        break;
      case paused:
        break;
    }
  }

  void player_task::handle(pause_request& m)
  {
  }

  void player_task::handle(stop_request& m)
  {
    switch ( state_ )
    {
      case stopped:
        break;
      case playing:
        assert(session_);
        list_provider_.reset();
        session_->stop();
        state_ = stopping;
        break;
      case stopping:
        list_provider_.reset();
        break;
      case paused:
        break;
    }
  }

  void player_task::handle(skip_request& m)
  {
    switch ( state_ )
    {
      case stopped:
        break;
      case playing:
      {
        assert(session_);
        auto track = session_->track();
        assert(track);
        track->increment_skip_count();
        session_->stop();
        break;
      }
      case stopping:
        break;
      case paused:
        break;
    }
  }

  void player_task::handle(queue_request& m)
  {
    auto tracks = musciteer::dm::tracks();

    auto track = tracks.find_by_id(m.id);

    if ( track.id_is_null() )
    {
      // ERROR!
      return;
    }

    switch ( state_ )
    {
      case stopped:
      {
        assert(!session_);
        become_playing(track);
        break;
      }
      case playing:
      {
        play_q_.push_back(track);
        queue_update_notify(track);
        player_state_notify();
        break;
      }
      case stopping:
        break;
      case paused:
        break;
    }
  }

  void player_task::handle(stream_begin_notify& m)
  {
    if ( session_ )
    {
      auto track = session_->track();

      assert(track);

      std::cout
        << "player_task - stream begin " << track->id()
        << " [ replaygain: " << (m.replaygain_enabled ? "on" : "off")
        << ", replaygain: " << m.replaygain << ", scale: " << m.scale
        << ", plays/skips: " << track->play_count() << "/" << track->skip_count()
        << " ]"
        << std::endl;

      for ( auto observer : observers_ )
      {
        message n(message::stream_begin_notify_id);
        auto& body = n.stream_begin_notify;

        body.stream_id = m.stream_id;
        body.replaygain_enabled = m.replaygain_enabled;
        body.replaygain = m.replaygain;
        body.scale = m.scale;

        observer.send(std::move(n));
      }
    }
    else
    {
      std::cerr << "player_task - received stream begin notify with session_ == nullptr" << std::endl;
    }
  }

  void player_task::handle(stream_progress_notify& m)
  {
    if ( session_ )
    {
      if ( m.stream_id == session_->id() )
      {
        if ( m.duration > 0 )
        {
          session_->fraction_played(float(m.duration) / float(m.length));
        }
      }
      else
      {
        std::cerr << "player_task - session id mismatch on stream progress notification" << std::endl;
      }

      for ( auto observer : observers_ )
      {
        message n(message::stream_progress_notify_id);
        auto& body = n.stream_progress_notify;

        body.stream_id = m.stream_id;
        body.duration = m.duration;
        body.length = m.length;

        observer.send(std::move(n));
      }
    }
    else
    {
      std::cerr << "player_task - received stream progress notify with session_ == nullptr" << std::endl;
    }
  }

  void player_task::handle(stream_end_notify& m)
  {
    if ( session_ )
    {
      auto tracks = musciteer::dm::tracks();
      auto track = session_->track();

      assert(track);

      if ( session_->fraction_played() > 0.80 ) {
        track->increment_play_count();
      }

      for ( auto observer : observers_ )
      {
        message n(message::stream_end_notify_id);
        auto& body = n.stream_end_notify;

        body.stream_id = m.stream_id;

        observer.send(std::move(n));
      }

      tracks.update(*track);
      end_session();
    }
    else
    {
      std::cerr << "player_task - received stream end notify with session_ == nullptr" << std::endl;
    }
  }

  void player_task::handle(source_notification& m)
  {
    std::cout
      << "got source notification"
      << " type=" << size_t(m.type)
      << ", source_name=" << m.source_name
      << ", message=" << m.message << std::endl;

    // Relay source notification to observers.
    for ( auto observer : observers_ )
    {
      message n(message::source_notify_id);

      n.source_notify.type = m.type;
      n.source_notify.source_name = m.source_name;
      n.source_notify.message = m.message;

      observer.send(std::move(n));
    }

    if ( m.type == source_notification::id::status )
    {
      // Save source status.
      source_status_[m.source_name] = std::move(m.message);
    }
    else if ( m.type == source_notification::id::track_unavailable )
    {
      end_session();
    }
    else if ( m.type == source_notification::id::error )
    {
      audio_output_.close();
      end_session();
    }
    else if ( m.type == source_notification::id::session_error )
    {
      become_stopped();
    }
  }

  void player_task::become_playing(const musciteer::dm::track& track)
  {
    if ( !audio_output_ )
    {
      audio_output_.open(audio_output_device_);
    }

    session_ = std::make_shared<player_session>(message_ch_);
    session_->track(track);
    session_->play(audio_output_);

    state_ = playing;

    player_state_notify();
  }

  void player_task::become_stopped()
  {
    session_->done();
    session_.reset();

    audio_output_.close();

    state_ = stopped;

    player_state_notify();
  }

  void player_task::end_session()
  {
    switch ( state_ )
    {
      case stopped:
        break;
      case playing:
      {
        session_->done();

        if ( list_provider_ && list_provider_->done() ) {
          list_provider_.reset();
        }

        if ( list_provider_ )
        {
          become_playing(list_provider_->next());
        }
        else if ( !play_q_.empty() )
        {
          auto track = play_q_.front();
          play_q_.pop_front();

          become_playing(track);
        }
        else if ( continuous_playback_ )
        {
          assert(ctpb_provider_.is_loaded());

          auto track = ctpb_provider_.get_track();

          if ( !track.id_is_null() )
          {
            become_playing(track);
          }
          else
          {
            std::cerr << "player_task - ctpb provider returned empty track!" << std::endl;
            become_stopped();
          }
        }
        else
        {
          become_stopped();
        }
        break;
      }
      case stopping:
        become_stopped();
        break;
      case paused:
        break;
    }
  }

  void player_task::player_state_notify()
  {
    for ( auto observer : observers_ )
    {
      message n(message::player_state_id);

      n.player_state.state = state_;

      if ( list_provider_ )
      {
        n.player_state.provider = list_provider_->info();
      }
      else if ( !play_q_.empty() )
      {
        std::stringstream ss;

        ss << "queue (" << play_q_.size() << ")";

        n.player_state.provider = ss.str();
      }
      else if ( continuous_playback_ )
      {
        n.player_state.provider = ctpb_provider_.info();
      }
      else
      {
        n.player_state.provider = "[none]";
      }

      observer.send(std::move(n));
    }
  }

  void player_task::queue_update_notify(const musciteer::dm::track& track)
  {
    for ( auto observer : observers_ )
    {
      message n(message::queue_update_id);

      n.queue_update.queue_size = play_q_.size();
      n.queue_update.track = std::make_shared<musciteer::dm::track>(track);

      observer.send(std::move(n));
    }
  }
}
