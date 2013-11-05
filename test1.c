/*
 * This is an example that starts a dbus daemon as a subprocess and makes
 * sure it is killed if our parent program crashes. Additionally, it shows
 * how to have a bunch of other processes also be watched by the same
 * supervisor sub-process.
 *
 * Both GbDbusDaemon and GbSupervisor need some resiliency work, but they
 * are probably good enough to move forward.
 */

#include "gb-supervisor.h"
#include "gb-dbus-daemon.h"

static void
add_sleep (GbSupervisor *supervisor,
           gint          seconds)
{
  GSubprocess *subprocess;
  GError *error = NULL;
  gchar str[12];

  g_snprintf (str, sizeof str, "%u", seconds);
  str[sizeof str - 1] = '\0';

  subprocess = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE,
                                 &error, "sleep", str, NULL);

  if (!subprocess)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      return;
    }

  gb_supervisor_add_subprocess (supervisor, subprocess);

  g_object_unref (subprocess);
}

gint
main (gint   argc,
      gchar *argv[])
{
  GbSupervisor *supervisor;
  GbDbusDaemon *daemon;
  GError *error = NULL;
  gint i;

  supervisor = gb_supervisor_new ();

  if (!gb_supervisor_run (supervisor, &error))
    {
      g_error ("%s", error->message);
      g_error_free (error);
      return 1;
    }

  daemon = gb_dbus_daemon_new (supervisor);

  for (i = 0; i < 10; i++)
    add_sleep (supervisor, 20);

  g_object_unref (daemon);

  return 0;
}
