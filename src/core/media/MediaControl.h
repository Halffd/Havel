#ifndef MEDIACONTROL_H
#define MEDIACONTROL_H

#include <dbus/dbus.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <memory>

class MediaControl {
private:
    DBusConnection* connection = nullptr;
    std::string active_player;

    // Auto-cleanup for DBus message
    struct DBusMessageDeleter {
        void operator()(DBusMessage* message) {
            if (message) dbus_message_unref(message);
        }
    };
    using DBusMessagePtr = std::unique_ptr<DBusMessage, DBusMessageDeleter>;

    // Find active MPRIS player
    void find_active_player() {
        DBusMessagePtr msg(dbus_message_new_method_call(
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            "ListNames"));

        if (!msg) throw std::runtime_error("Failed to create DBus message");

        DBusMessagePtr reply;
        DBusError error;
        dbus_error_init(&error);

        reply.reset(dbus_connection_send_with_reply_and_block(connection, msg.get(), 1000, &error));

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
            const char* name;
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
    void send_simple_command(const std::string& method) {
        if (active_player.empty()) {
            find_active_player();
            if (active_player.empty()) {
                throw std::runtime_error("No active media player found");
            }
        }

        DBusMessagePtr msg(dbus_message_new_method_call(
            active_player.c_str(),
            "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player",
            method.c_str()
        ));

        if (!msg) throw std::runtime_error("Failed to create DBus message");

        DBusError error;
        dbus_error_init(&error);

        dbus_connection_send_with_reply_and_block(connection, msg.get(), 1000, &error);

        if (dbus_error_is_set(&error)) {
            std::string err_msg = error.message;
            dbus_error_free(&error);

            // If the player is no longer available, try to find another one
            if (err_msg.find("org.freedesktop.DBus.Error.ServiceUnknown") != std::string::npos) {
                active_player = "";
                send_simple_command(method); // Retry with a new player
            } else {
                throw std::runtime_error("DBus error: " + err_msg);
            }
        }
    }

public:
    MediaControl() {
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

    ~MediaControl() {
        if (connection) {
            dbus_connection_unref(connection);
        }
    }

    // Media control functions
    void PlayPause() {
        send_simple_command("PlayPause");
    }

    void Play() {
        send_simple_command("Play");
    }

    void Pause() {
        send_simple_command("Pause");
    }

    void Stop() {
        send_simple_command("Stop");
    }

    void Next() {
        send_simple_command("Next");
    }

    void Previous() {
        send_simple_command("Previous");
    }

    // Set position in microseconds
    void SetPosition(int64_t position) {
        if (active_player.empty()) {
            find_active_player();
            if (active_player.empty()) {
                throw std::runtime_error("No active media player found");
            }
        }

        // First get the current track ID
        DBusMessagePtr msg_get(dbus_message_new_method_call(
            active_player.c_str(),
            "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties",
            "Get"
        ));

        if (!msg_get) throw std::runtime_error("Failed to create DBus message");

        const char* interface_name = "org.mpris.MediaPlayer2.Player";
        const char* property_name = "Metadata";

        dbus_message_append_args(msg_get.get(),
            DBUS_TYPE_STRING, &interface_name,
            DBUS_TYPE_STRING, &property_name,
            DBUS_TYPE_INVALID);

        DBusError error;
        dbus_error_init(&error);

        DBusMessagePtr reply(dbus_connection_send_with_reply_and_block(
            connection, msg_get.get(), 1000, &error));

        if (dbus_error_is_set(&error)) {
            std::string err_msg = error.message;
            dbus_error_free(&error);
            throw std::runtime_error("DBus error: " + err_msg);
        }

        // Extract the track ID
        DBusMessageIter iter;
        if (!dbus_message_iter_init(reply.get(), &iter)) {
            throw std::runtime_error("Reply has no arguments");
        }

        // Navigate to the trackid value (complex structure traversal)
        // This is a simplified approach - real implementation needs more robust parsing
        std::string track_id = "/org/mpris/MediaPlayer2/TrackList/NoTrack"; // default

        // Now send SetPosition command
        DBusMessagePtr msg_set(dbus_message_new_method_call(
            active_player.c_str(),
            "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player",
            "SetPosition"
        ));

        if (!msg_set) throw std::runtime_error("Failed to create DBus message");

        DBusMessageIter args;
        dbus_message_iter_init_append(msg_set.get(), &args);

        const char* track_id_cstr = track_id.c_str();
        dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &track_id_cstr);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_INT64, &position);

        dbus_connection_send_with_reply_and_block(connection, msg_set.get(), 1000, &error);

        if (dbus_error_is_set(&error)) {
            std::string err_msg = error.message;
            dbus_error_free(&error);
            throw std::runtime_error("DBus error: " + err_msg);
        }
    }

    // Set volume (0.0 to 1.0)
    void SetVolume(double volume) {
        if (active_player.empty()) {
            find_active_player();
            if (active_player.empty()) {
                throw std::runtime_error("No active media player found");
            }
        }

        DBusMessagePtr msg(dbus_message_new_method_call(
            active_player.c_str(),
            "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties",
            "Set"
        ));

        if (!msg) throw std::runtime_error("Failed to create DBus message");

        const char* interface_name = "org.mpris.MediaPlayer2.Player";
        const char* property_name = "Volume";

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

        dbus_connection_send_with_reply_and_block(connection, msg.get(), 1000, &error);

        if (dbus_error_is_set(&error)) {
            std::string err_msg = error.message;
            dbus_error_free(&error);
            throw std::runtime_error("DBus error: " + err_msg);
        }
    }

    // Get list of available players
    std::vector<std::string> GetAvailablePlayers() {
        DBusMessagePtr msg(dbus_message_new_method_call(
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            "ListNames"));

        if (!msg) throw std::runtime_error("Failed to create DBus message");

        DBusMessagePtr reply;
        DBusError error;
        dbus_error_init(&error);

        reply.reset(dbus_connection_send_with_reply_and_block(connection, msg.get(), 1000, &error));

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
            const char* name;
            dbus_message_iter_get_basic(&array_iter, &name);
            std::string name_str(name);

            if (name_str.find("org.mpris.MediaPlayer2.") == 0) {
                players.push_back(name_str);
            }

            dbus_message_iter_next(&array_iter);
        }

        return players;
    }

    // Set the active player
    void SetActivePlayer(const std::string& player) {
        if (player.find("org.mpris.MediaPlayer2.") == 0) {
            active_player = player;
        } else {
            active_player = "org.mpris.MediaPlayer2." + player;
        }
    }

    // Get the current active player
    std::string GetActivePlayer() const {
        return active_player;
    }
};

#endif //MEDIACONTROL_H
