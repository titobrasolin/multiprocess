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
  GbSupervisor    *supervisor;
  GPid             pid;
};

enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_CONNECTION,
  PROP_SUPERVISOR,
  LAST_PROP
};

G_DEFINE_TYPE_WITH_CODE (GbDbusDaemon,
                         gb_dbus_daemon,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (GbDbusDaemon))

static GParamSpec * gParamSpecs[LAST_PROP];

GbDbusDaemon *
gb_dbus_daemon_new (GbSupervisor *supervisor)
{
  return g_object_new (GB_TYPE_DBUS_DAEMON,
                       "supervisor", supervisor,
                       NULL);
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

static void
gb_dbus_daemon_set_supervisor (GbDbusDaemon *daemon,
                               GbSupervisor *supervisor)
{
  g_return_if_fail (GB_IS_DBUS_DAEMON (daemon));
  g_return_if_fail (!supervisor || GB_IS_SUPERVISOR (supervisor));

  daemon->priv->supervisor = supervisor ? g_object_ref (supervisor) : NULL;
}

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

void
gb_dbus_daemon_start (GbDbusDaemon *daemon)
{
  static const gchar *argv[] = { "dbus-daemon",
                                 "--config-file",
                                 "foo",
                                 "--print-address",
                                 "--nofork",
                                 "--nopidfile",
                                 NULL };
  GbDbusDaemonPrivate *priv;
  GDataInputStream *data_stream = NULL;
  GInputStream *raw_stream;
  gboolean r;
  GError *error = NULL;
  gchar *line = NULL;
  gchar *path = NULL;
  gint standard_output = -1;

  g_return_if_fail (GB_IS_DBUS_DAEMON (daemon));

  priv = daemon->priv;

  if (priv->pid)
    {
      g_warning ("Cannot launch daemon, it has already been launched.");
      return;
    }

  path = write_config ();
  argv[2] = path;

  r = g_spawn_async_with_pipes (NULL,
                                (gchar **)argv,
                                NULL,
                                G_SPAWN_SEARCH_PATH,
                                NULL,
                                NULL,
                                &priv->pid,
                                NULL,
                                &standard_output,
                                NULL,
                                &error);

  if (!r)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      goto cleanup;
    }

  gb_supervisor_add_pid (priv->supervisor, priv->pid);

  raw_stream = g_unix_input_stream_new (standard_output, TRUE);
  data_stream = g_data_input_stream_new (raw_stream);
  g_object_unref (raw_stream);

  line = g_data_input_stream_read_line_utf8 (data_stream, NULL, NULL, &error);

  if (!line)
    {
      kill (priv->pid, SIGTERM);
      priv->pid = 0;
      goto cleanup;
    }

  g_clear_pointer (&priv->address, g_free);
  priv->address = g_strdup (g_strchomp (line));

  g_clear_object (&priv->connection);
  priv->connection =
    g_dbus_connection_new_for_address_sync (priv->address,
                                            G_DBUS_CONNECTION_FLAGS_NONE,
                                            NULL,
                                            NULL,
                                            &error);

  if (!priv->connection)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      goto cleanup;
    }

  if (error)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      goto cleanup;
    }

cleanup:
  g_clear_object (&data_stream);
  g_clear_pointer (&line, g_free);
  g_clear_pointer (&path, g_free);
}

void
gb_dbus_daemon_stop (GbDbusDaemon *daemon)
{
  GbDbusDaemonPrivate *priv;

  g_return_if_fail (GB_IS_DBUS_DAEMON (daemon));

  priv = daemon->priv;

  g_clear_object (&priv->connection);
  g_clear_pointer (&priv->address, g_free);

  if (priv->pid)
    {
      kill (priv->pid, SIGTERM);
      priv->pid = 0;
    }
}

static void
gb_dbus_daemon_constructed (GObject *object)
{
  GbDbusDaemonPrivate *priv = GB_DBUS_DAEMON (object)->priv;

  if (!priv->supervisor)
    {
      priv->supervisor = gb_supervisor_new ();
    }
}

static void
gb_dbus_daemon_finalize (GObject *object)
{
  GbDbusDaemonPrivate *priv;

  priv = GB_DBUS_DAEMON (object)->priv;

  g_clear_object (&priv->connection);
  g_clear_pointer (&priv->address, g_free);

  if (priv->pid)
    {
      kill (priv->pid, SIGTERM);
      priv->pid = 0;
    }

  g_clear_object (&priv->supervisor);

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
gb_dbus_daemon_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  GbDbusDaemon *daemon = GB_DBUS_DAEMON (object);

  switch (prop_id) {
  case PROP_SUPERVISOR:
    gb_dbus_daemon_set_supervisor (daemon, g_value_get_object (value));
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
  object_class->constructed = gb_dbus_daemon_constructed;
  object_class->finalize = gb_dbus_daemon_finalize;
  object_class->get_property = gb_dbus_daemon_get_property;
  object_class->set_property = gb_dbus_daemon_set_property;

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

  gParamSpecs[PROP_SUPERVISOR] =
    g_param_spec_object ("supervisor",
                         _ ("Supervisor"),
                         _ ("The supervisor of the daemon."),
                         GB_TYPE_SUPERVISOR,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_SUPERVISOR,
                                   gParamSpecs[PROP_SUPERVISOR]);
}

static void
gb_dbus_daemon_init (GbDbusDaemon *daemon)
{
  daemon->priv = gb_dbus_daemon_get_instance_private (daemon);
}
