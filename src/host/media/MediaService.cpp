/*
 * MediaService.cpp - Media control via MPRIS/DBus
 */
#include "MediaService.hpp"

#include <dbus/dbus.h>
#include <memory>
#include <stdexcept>

namespace havel::host {

struct MediaService::Impl {
  DBusConnection *connection = nullptr;
  std::string active_player;

  // Auto-cleanup for DBus message
  struct DBusMessageDeleter {
    void operator()(DBusMessage *message) {
      if (message)
        dbus_message_unref(message);
    }
  };
  using DBusMessagePtr = std::unique_ptr<DBusMessage, DBusMessageDeleter>;

  Impl() {
    DBusError error;
    dbus_error_init(&error);

    connection = dbus_bus_get(DBUS_BUS_SESSION, &error);

    if (dbus_error_is_set(&error)) {
      std::string err_msg = error.message;
      dbus_error_free(&error);
      throw std::runtime_error("Failed to connect to DBus: " + err_msg);
    }

    if (!connection) {
      throw std::runtime_error("Failed to connect to DBus");
    }

    find_active_player();
  }

  ~Impl() {
    if (connection) {
      dbus_connection_unref(connection);
    }
  }

  // Find active MPRIS player
  void find_active_player() {
    DBusMessagePtr msg(dbus_message_new_method_call(
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "ListNames"));

    if (!msg)
      throw std::runtime_error("Failed to create DBus message");

    DBusMessagePtr reply;
    DBusError error;
    dbus_error_init(&error);

    reply.reset(dbus_connection_send_with_reply_and_block(
        connection, msg.get(), 1000, &error));

    if (dbus_error_is_set(&error)) {
      std::string err_msg = error.message;
      dbus_error_free(&error);
      throw std::runtime_error("DBus error: " + err_msg);
    }

    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply.get(), &iter)) {
      throw std::runtime_error("Reply has no arguments");
    }

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
      throw std::runtime_error("Argument is not an array");
    }

    DBusMessageIter array_iter;
    dbus_message_iter_recurse(&iter, &array_iter);

    std::vector<std::string> players;

    while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRING) {
      const char *name;
      dbus_message_iter_get_basic(&array_iter, &name);
      std::string name_str(name);

      if (name_str.find("org.mpris.MediaPlayer2.") == 0) {
        players.push_back(name_str);
      }

      dbus_message_iter_next(&array_iter);
    }

    // Select the first available player
    active_player = players.empty() ? "" : players[0];
  }

  // Send a simple method call with no arguments
  void send_simple_command(const std::string &method) {
    if (active_player.empty()) {
      find_active_player();
      if (active_player.empty()) {
        throw std::runtime_error("No active media player found");
      }
    }

    DBusMessagePtr msg(dbus_message_new_method_call(
        active_player.c_str(), "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player", method.c_str()));

    if (!msg)
      throw std::runtime_error("Failed to create DBus message");

    DBusError error;
    dbus_error_init(&error);

    dbus_connection_send_with_reply_and_block(connection, msg.get(), 1000,
                                              &error);

    if (dbus_error_is_set(&error)) {
      std::string err_msg = error.message;
      dbus_error_free(&error);

      // If the player is no longer available, try to find another one
      if (err_msg.find("org.freedesktop.DBus.Error.ServiceUnknown") !=
          std::string::npos) {
        active_player = "";
        send_simple_command(method); // Retry with a new player
      } else {
        throw std::runtime_error("DBus error: " + err_msg);
      }
    }
  }

  // Get list of available players
  std::vector<std::string> get_available_players() {
    DBusMessagePtr msg(dbus_message_new_method_call(
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "ListNames"));

    if (!msg)
      throw std::runtime_error("Failed to create DBus message");

    DBusMessagePtr reply;
    DBusError error;
    dbus_error_init(&error);

    reply.reset(dbus_connection_send_with_reply_and_block(
        connection, msg.get(), 1000, &error));

    if (dbus_error_is_set(&error)) {
      std::string err_msg = error.message;
      dbus_error_free(&error);
      throw std::runtime_error("DBus error: " + err_msg);
    }

    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply.get(), &iter)) {
      throw std::runtime_error("Reply has no arguments");
    }

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
      throw std::runtime_error("Argument is not an array");
    }

    DBusMessageIter array_iter;
    dbus_message_iter_recurse(&iter, &array_iter);

    std::vector<std::string> players;

    while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRING) {
      const char *name;
      dbus_message_iter_get_basic(&array_iter, &name);
      std::string name_str(name);

      if (name_str.find("org.mpris.MediaPlayer2.") == 0) {
        players.push_back(name_str);
      }

      dbus_message_iter_next(&array_iter);
    }

    return players;
  }

  // Get property value
  double get_volume_property() {
    if (active_player.empty()) {
      find_active_player();
      if (active_player.empty()) {
        throw std::runtime_error("No active media player found");
      }
    }

    DBusMessagePtr msg(dbus_message_new_method_call(
        active_player.c_str(), "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties", "Get"));

    if (!msg)
      throw std::runtime_error("Failed to create DBus message");

    const char *interface_name = "org.mpris.MediaPlayer2.Player";
    const char *property_name = "Volume";

    dbus_message_append_args(msg.get(), DBUS_TYPE_STRING, &interface_name,
                             DBUS_TYPE_STRING, &property_name,
                             DBUS_TYPE_INVALID);

    DBusError error;
    dbus_error_init(&error);

    DBusMessagePtr reply(dbus_connection_send_with_reply_and_block(
        connection, msg.get(), 1000, &error));

    if (dbus_error_is_set(&error)) {
      std::string err_msg = error.message;
      dbus_error_free(&error);
      throw std::runtime_error("DBus error: " + err_msg);
    }

    // Parse variant containing volume
    DBusMessageIter iter, variant_iter;
    if (!dbus_message_iter_init(reply.get(), &iter)) {
      throw std::runtime_error("Reply has no arguments");
    }

    dbus_message_iter_recurse(&iter, &variant_iter);
    
    double volume = 0.0;
    if (dbus_message_iter_get_arg_type(&variant_iter) == DBUS_TYPE_DOUBLE) {
      dbus_message_iter_get_basic(&variant_iter, &volume);
    }

    return volume;
  }
};

MediaService::MediaService() : impl_(new Impl()) {}

MediaService::~MediaService() { delete impl_; }

void MediaService::playPause() { impl_->send_simple_command("PlayPause"); }

void MediaService::play() { impl_->send_simple_command("Play"); }

void MediaService::pause() { impl_->send_simple_command("Pause"); }

void MediaService::stop() { impl_->send_simple_command("Stop"); }

void MediaService::next() { impl_->send_simple_command("Next"); }

void MediaService::previous() { impl_->send_simple_command("Previous"); }

double MediaService::getVolume() const { return impl_->get_volume_property(); }

void MediaService::setVolume(double volume) {
  if (impl_->active_player.empty()) {
    impl_->find_active_player();
    if (impl_->active_player.empty()) {
      throw std::runtime_error("No active media player found");
    }
  }

  Impl::DBusMessagePtr msg(dbus_message_new_method_call(
      impl_->active_player.c_str(), "/org/mpris/MediaPlayer2",
      "org.freedesktop.DBus.Properties", "Set"));

  if (!msg)
    throw std::runtime_error("Failed to create DBus message");

  const char *interface_name = "org.mpris.MediaPlayer2.Player";
  const char *property_name = "Volume";

  DBusMessageIter args;
  DBusMessageIter variant;

  dbus_message_iter_init_append(msg.get(), &args);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &interface_name);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &property_name);

  dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "d", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_DOUBLE, &volume);
  dbus_message_iter_close_container(&args, &variant);

  DBusError error;
  dbus_error_init(&error);

  dbus_connection_send_with_reply_and_block(impl_->connection, msg.get(), 1000,
                                            &error);

  if (dbus_error_is_set(&error)) {
    std::string err_msg = error.message;
    dbus_error_free(&error);
    throw std::runtime_error("DBus error: " + err_msg);
  }
}

int64_t MediaService::getPosition() const {
  // TODO: Implement position retrieval
  return 0;
}

void MediaService::setPosition(int64_t position) {
  // TODO: Implement position setting
  (void)position;
}

std::vector<std::string> MediaService::getAvailablePlayers() const {
  return impl_->get_available_players();
}

std::string MediaService::getActivePlayer() const {
  return impl_->active_player;
}

void MediaService::setActivePlayer(const std::string &player) {
  if (player.find("org.mpris.MediaPlayer2.") == 0) {
    impl_->active_player = player;
  } else {
    impl_->active_player = "org.mpris.MediaPlayer2." + player;
  }
}

bool MediaService::hasPlayer() const { return !impl_->active_player.empty(); }

} // namespace havel::host
