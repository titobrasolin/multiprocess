/* gb-dbus-daemon.c
 *
 * Copyright (C) 2013 Christian Hergert <christian@hergert.me>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <fcntl.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gunixinputstream.h>
#include <unistd.h>

#include "gb-dbus-daemon.h"

struct _GbDbusDaemonPrivate
{
  gchar           *address;
  GDBusConnection *connection;
  GSubprocess     *subprocess;
};

enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_CONNECTION,
  LAST_PROP
};

G_DEFINE_TYPE_WITH_CODE (GbDbusDaemon,
                         gb_dbus_daemon,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (GbDbusDaemon))

static GParamSpec * gParamSpecs[LAST_PROP];

static gchar *
write_config (void)
{
  GString *str;
  gchar *tmpl;
  gint fd;

  tmpl = g_build_filename (g_get_tmp_dir (),
                           "gb-dbus-daemon.conf-XXXXXX",
                           NULL);

  fd = g_mkstemp_full (tmpl, O_CREAT | O_RDWR, 0600);

  if (fd < 0)
    {
      g_free (tmpl);
      return NULL;
    }

  str = g_string_new (NULL);
  g_string_append_printf (str,
                          "<busconfig>"
                          " <type>session</type>"
                          " <listen>unix:tmpdir=%s</listen>"
                          " <policy context=\"default\">"
                          "  <allow send_destination=\"*\" eavesdrop=\"true\"/>"
                          "  <allow eavesdrop=\"true\"/>"
                          "  <allow own=\"*\"/>"
                          " </policy>"
                          "</busconfig>",
                          g_get_tmp_dir ());

  if (!g_file_set_contents (tmpl, str->str, str->len, NULL))
    {
      g_string_free (str, TRUE);
      g_unlink (tmpl);
      g_free (tmpl);
      close (fd);
      return NULL;
    }

  g_string_free (str, TRUE);
  close (fd);

  return tmpl;
}

static void
child_setup_func (gpointer user_data)
{
  /*
   * On Linux, tell the bus process to exit if this process goes away.
   */
#ifdef __linux
#include <sys/prctl.h>
  prctl (PR_SET_PDEATHSIG, 15);
#endif
}

static GSubprocess *
gb_dbus_daemon_launch (GbDbusDaemon  *daemon,
                       GError       **error)
{
  GSubprocessLauncher *launcher;
  GSubprocess *subprocess;
  gchar *config_file;

  g_return_if_fail (GB_IS_DBUS_DAEMON(daemon));

  if (!(config_file = write_config ()))
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   G_FILE_ERROR_IO,
                   _("Failed to write dbus configuration."));
      return NULL;
    }

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE);
  g_subprocess_launcher_set_child_setup (launcher,
                                         child_setup_func,
                                         NULL,
                                         NULL);

  subprocess = g_subprocess_launcher_spawn (launcher, error,
                                            "dbus-daemon",
                                            "--print-address",
                                            "--nofork",
                                            "--nopidfile",
                                            "--config-file",
                                            config_file,
                                            NULL);

  g_free (config_file);

  return subprocess;
}

static gchar *
gb_dbus_daemon_read_address (GbDbusDaemon *daemon,
                             GSubprocess  *subprocess)
{
  GDataInputStream *data_stream;
  GInputStream *raw_stream;
  gchar *line;
  gchar *ret = NULL;

  raw_stream = g_subprocess_get_stdout_pipe (subprocess);
  data_stream = g_data_input_stream_new (raw_stream);
  line = g_data_input_stream_read_line_utf8 (data_stream, NULL, NULL, NULL);

  if (!line)
    goto cleanup;

  ret = g_strdup (g_strchomp (line));

cleanup:
  g_clear_object (&data_stream);
  g_clear_object (&raw_stream);
  g_clear_pointer (&line, g_free);

  return ret;
}

GbDbusDaemon *
gb_dbus_daemon_new (void)
{
  return g_object_new (GB_TYPE_DBUS_DAEMON, NULL);
}

const gchar *
gb_dbus_daemon_get_address (GbDbusDaemon *daemon)
{
  g_return_val_if_fail (GB_IS_DBUS_DAEMON (daemon), NULL);

  return daemon->priv->address;
}

GDBusConnection *
gb_dbus_daemon_get_connection (GbDbusDaemon *daemon)
{
  g_return_val_if_fail (GB_IS_DBUS_DAEMON (daemon), NULL);

  return daemon->priv->connection;
}

void
gb_dbus_daemon_start (GbDbusDaemon *daemon)
{
  GbDbusDaemonPrivate *priv;
  GSubprocess *subprocess;
  GError *error = NULL;
  gchar *address;

  g_return_if_fail (GB_IS_DBUS_DAEMON (daemon));

  priv = daemon->priv;

  if (priv->subprocess)
    {
      g_warning ("dbus-daemon has already been launched.");
      return;
    }

  subprocess = gb_dbus_daemon_launch (daemon, &error);

  if (!subprocess)
    {
      g_warning ("Failed to launch dbus-daemon: %s", error->message);
      g_error_free (error);
      return;
    }

  address = gb_dbus_daemon_read_address (daemon, subprocess);

  if (!address)
    {
      g_warning ("Failed to parse dbus-daemon address.");
      g_object_unref (subprocess);
      return;
    }

  priv->subprocess = subprocess;
  priv->address = address;

  priv->connection =
    g_dbus_connection_new_for_address_sync (priv->address,
                                            G_DBUS_CONNECTION_FLAGS_NONE,
                                            NULL,
                                            NULL,
                                            &error);

  g_object_notify_by_pspec (G_OBJECT (daemon), gParamSpecs[PROP_ADDRESS]);
  g_object_notify_by_pspec (G_OBJECT (daemon), gParamSpecs[PROP_CONNECTION]);
}

void
gb_dbus_daemon_stop (GbDbusDaemon *daemon)
{
  GbDbusDaemonPrivate *priv;

  g_return_if_fail (GB_IS_DBUS_DAEMON (daemon));

  priv = daemon->priv;

  g_clear_object (&priv->connection);
  g_clear_pointer (&priv->address, g_free);

  if (priv->subprocess)
    {
      g_subprocess_force_exit (priv->subprocess);
      g_clear_object (&priv->subprocess);
    }

  g_object_notify_by_pspec (G_OBJECT (daemon), gParamSpecs[PROP_ADDRESS]);
  g_object_notify_by_pspec (G_OBJECT (daemon), gParamSpecs[PROP_CONNECTION]);
}

static void
gb_dbus_daemon_finalize (GObject *object)
{
  GbDbusDaemonPrivate *priv;

  priv = GB_DBUS_DAEMON (object)->priv;

  g_clear_object (&priv->connection);
  g_clear_pointer (&priv->address, g_free);

  if (priv->subprocess)
    {
      g_subprocess_force_exit (priv->subprocess);
      g_clear_object (&priv->subprocess);
    }

  G_OBJECT_CLASS (gb_dbus_daemon_parent_class)->finalize (object);
}

static void
gb_dbus_daemon_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  GbDbusDaemon *daemon = GB_DBUS_DAEMON (object);

  switch (prop_id) {
  case PROP_ADDRESS:
    g_value_set_string (value, daemon->priv->address);
    break;
  case PROP_CONNECTION:
    g_value_set_object (value, daemon->priv->connection);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gb_dbus_daemon_class_init (GbDbusDaemonClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = gb_dbus_daemon_finalize;
  object_class->get_property = gb_dbus_daemon_get_property;

  gParamSpecs[PROP_ADDRESS] =
    g_param_spec_string ("address",
                         _ ("Address"),
                         _ ("The DBus connection address."),
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_ADDRESS,
                                   gParamSpecs[PROP_ADDRESS]);

  gParamSpecs[PROP_CONNECTION] =
    g_param_spec_object ("connection",
                         _ ("Connection"),
                         _ ("A shared connection to the daemon."),
                         G_TYPE_DBUS_CONNECTION,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_CONNECTION,
                                   gParamSpecs[PROP_CONNECTION]);
}

static void
gb_dbus_daemon_init (GbDbusDaemon *daemon)
{
  daemon->priv = gb_dbus_daemon_get_instance_private (daemon);
}
