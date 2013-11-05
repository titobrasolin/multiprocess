/* gb-supervisor.c
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

#include <errno.h>
#include <fcntl.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "gb-supervisor.h"

struct _GbSupervisorPrivate
{
  GHashTable *launchers;
  GIOChannel *channel;
  GArray     *pids;
  GPid        pid;
  guint       running : 1;
};

G_DEFINE_TYPE_WITH_CODE (GbSupervisor,
                         gb_supervisor,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (GbSupervisor))

static void
gb_supervisor_send_command (GbSupervisor *supervisor,
                            const gchar  *command)
{
  GbSupervisorPrivate *priv;
  GString *str;

  g_return_if_fail (GB_IS_SUPERVISOR (supervisor));
  g_return_if_fail (command);

  priv = supervisor->priv;

  str = g_string_new (command);
  g_string_append_c (str, '\n');

  g_io_channel_write_chars (priv->channel, str->str, str->len, NULL, NULL);
  g_io_channel_flush (priv->channel, NULL);

  g_string_free (str, TRUE);
}

static void
wait_cb (GObject      *object,
         GAsyncResult *result,
         gpointer      user_data)
{
  GbSupervisor *supervisor = user_data;
  GSubprocess *child = (GSubprocess *)object;
  const gchar *identifier;
  gboolean ret;
  GError *error;
  gchar *command;

  g_return_if_fail (G_IS_SUBPROCESS (child));
  g_return_if_fail (GB_IS_SUPERVISOR (supervisor));

  ret = g_subprocess_wait_finish (child, result, &error);

  if (!ret)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  identifier = g_object_get_data (G_OBJECT (child), "identifier");
  command = g_strdup_printf ("r %s", identifier);

  gb_supervisor_send_command (supervisor, command);

  g_free (command);
}

static void
gb_supervisor_launch (GbSupervisor        *supervisor,
                      GSubprocessLauncher *launcher,
                      const gchar *const  *argv)
{
  GSubprocess *child;
  const gchar *identifier;
  GError *error = NULL;
  gchar *command;

  g_return_if_fail (GB_IS_SUPERVISOR (supervisor));

  child = g_subprocess_launcher_spawnv (launcher, argv, &error);

  if (!child)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      return;
    }

  identifier = g_subprocess_get_identifier (child);

  g_object_set_data_full (G_OBJECT (child),
                          "identifier",
                          g_strdup (identifier),
                          g_free);

  g_object_set_data_full (G_OBJECT (child),
                          "launcher",
                          g_object_ref (launcher),
                          g_object_unref);

  command = g_strdup_printf ("a %s", identifier);
  gb_supervisor_send_command (supervisor, command);
  g_free (command);

  g_subprocess_wait_async (child,
                           NULL,
                           wait_cb,
                           g_object_ref (supervisor));

  g_object_unref (child);
}

gboolean
gb_supervisor_run (GbSupervisor *supervisor,
                   GError      **error)
{
  GbSupervisorPrivate *priv;
  GIOChannel *channel;
  GIOStatus status;
  GString *str;
  GArray *array;
  gchar *name;
  gchar mode;
  GPid pid;
  gint pipefds[2];
  gint ret;
  gint i;

  g_return_val_if_fail (GB_IS_SUPERVISOR (supervisor), FALSE);

  priv = supervisor->priv;

  /*
   * Make a pipe that we can use to detect the parent process has
   * exited.
   */
  errno = 0;
  ret = pipe (pipefds);

  if (ret != 0)
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   G_FILE_ERROR_IO,
                   strerror (errno));
      return FALSE;
    }

  /*
   * Fork a child process that will do the monitoring.
   */
  errno = 0;
  pid = fork ();

  if (pid == -1)
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   G_FILE_ERROR_IO,
                   strerror (errno));
      close (pipefds[0]);
      close (pipefds[1]);
      return FALSE;
    }

  /*
   * If we are the parent process, setup our communication channel
   * and then we are done.
   */
  if (pid)
    {
      GHashTableIter iter;
      gpointer key;
      gpointer value;
      gchar *command;

      priv->pid = pid;

      priv->channel = g_io_channel_unix_new (pipefds[1]);
      g_io_channel_set_close_on_unref (priv->channel, TRUE);

      close (pipefds[0]);

      priv->running = TRUE;

      g_hash_table_iter_init (&iter, priv->launchers);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          gb_supervisor_launch (supervisor, key, value);
        }

      for (i = 0; i < priv->pids->len; i++)
        {
          pid = g_array_index (priv->pids, GPid, i);
          command = g_strdup_printf ("a %u", (guint)pid);
          gb_supervisor_send_command (supervisor, command);
          g_free (command);
        }

      return TRUE;
    }

  /*
   * Rename the process to make it easier to find in `top'.
   */
  name = g_strdup_printf ("%s-supervisor", g_get_prgname ());
  g_set_prgname (name);
  g_free (name);

  /*
   * TODO: Close all file-descriptors except pipefds[0].
   */
  close (pipefds[1]);

  channel = g_io_channel_unix_new (pipefds[0]);
  g_io_channel_set_close_on_unref (channel, TRUE);

  str = g_string_new (NULL);
  array = g_array_new (FALSE, FALSE, sizeof (GPid));

again:
  status = g_io_channel_read_line_string (channel, str, NULL, NULL);

  if (status != G_IO_STATUS_NORMAL)
    {
      goto kill_targets;
    }

  if (2 != sscanf (str->str, "%c %u", &mode, &pid))
    {
      goto kill_targets;
    }

  switch (mode) {
    case 'a':
      g_array_append_val (array, pid);
      break;
    case 'r':

      for (i = 0; i < array->len; i++)
        {
          if (g_array_index (array, GPid, i) == pid)
            {
              g_array_remove_index_fast (array, i);
              break;
            }
        }
      break;
    default:
      goto kill_targets;
    }

  goto again;

kill_targets:

  for (i = 0; i < array->len; i++)
    {
      pid = g_array_index (array, GPid, i);
      g_printerr ("Reaping %u\n", (guint)pid);
      kill (pid, SIGTERM);
    }

  exit (EXIT_SUCCESS);

  return TRUE;
}

void
gb_supervisor_add_pid (GbSupervisor *supervisor,
                       GPid          pid)
{
  GbSupervisorPrivate *priv;
  gchar *command;

  g_return_if_fail (GB_IS_SUPERVISOR (supervisor));
  g_return_if_fail (pid);

  priv = supervisor->priv;

  if (priv->running)
    {
      command = g_strdup_printf ("a %u", (guint)pid);
      gb_supervisor_send_command (supervisor, command);
      g_free (command);
    } else{
      g_array_append_val (priv->pids, pid);
    }
}

void
gb_supervisor_add_subprocess (GbSupervisor *supervisor,
                              GSubprocess  *subprocess)
{
  const gchar *identifier;
  guint64 val;

  g_return_if_fail (GB_IS_SUPERVISOR (supervisor));

  identifier = g_subprocess_get_identifier (subprocess);

  if (1 == sscanf (identifier, "%"G_GUINT64_FORMAT, &val))
    {
      gb_supervisor_add_pid (supervisor, (GPid)val);
    } else{
      g_warning ("Failed to parse pid %s", identifier);
    }
}

static void
gb_supervisor_dispose (GObject *object)
{
  GbSupervisorPrivate *priv = GB_SUPERVISOR (object)->priv;

  g_clear_pointer (&priv->launchers, (GDestroyNotify)g_hash_table_unref);

  G_OBJECT_CLASS (gb_supervisor_parent_class)->dispose (object);
}

void
gb_supervisor_add_launcher (GbSupervisor        *supervisor,
                            GSubprocessLauncher *launcher,
                            const gchar *const  *argv)
{
  GbSupervisorPrivate *priv;

  g_return_if_fail (GB_IS_SUPERVISOR (supervisor));
  g_return_if_fail (G_IS_SUBPROCESS_LAUNCHER (launcher));

  priv = supervisor->priv;

  g_hash_table_insert (priv->launchers,
                       g_object_ref (launcher),
                       g_strdupv ((gchar **)argv));

  if (priv->running)
    {
      gb_supervisor_launch (supervisor, launcher, argv);
    }
}

void
gb_supervisor_shutdown (GbSupervisor *supervisor)
{
  GbSupervisorPrivate *priv;

  g_return_if_fail (GB_IS_SUPERVISOR (supervisor));

  /*
   * TODO: Close parent fd, cause child to cleanup.
   */

  priv = supervisor->priv;

  g_clear_pointer (&priv->channel, (GDestroyNotify)g_io_channel_unref);

  priv->running = FALSE;
}

GbSupervisor *
gb_supervisor_new (void)
{
  return g_object_new (GB_TYPE_SUPERVISOR, NULL);
}

static void
gb_supervisor_finalize (GObject *object)
{
  GbSupervisorPrivate *priv = GB_SUPERVISOR (object)->priv;

  g_clear_pointer (&priv->pids, (GDestroyNotify)g_array_unref);
  g_clear_pointer (&priv->channel, (GDestroyNotify)g_io_channel_unref);
  g_clear_pointer (&priv->launchers, (GDestroyNotify)g_hash_table_unref);

  G_OBJECT_CLASS (gb_supervisor_parent_class)->finalize (object);
}

static void
gb_supervisor_class_init (GbSupervisorClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->dispose = gb_supervisor_dispose;
  object_class->finalize = gb_supervisor_finalize;
}

static void
gb_supervisor_init (GbSupervisor *supervisor)
{
  supervisor->priv = gb_supervisor_get_instance_private (supervisor);

  supervisor->priv->pids = g_array_new (FALSE, FALSE, sizeof (GPid));

  supervisor->priv->launchers =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           g_object_unref,
                           (GDestroyNotify)g_strfreev);
}
