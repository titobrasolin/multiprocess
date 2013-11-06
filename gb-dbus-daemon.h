/* gb-dbus-daemon.h
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

#ifndef GB_DBUS_DAEMON_H
#define GB_DBUS_DAEMON_H

#include <gio/gio.h>

#include "gb-supervisor.h"

G_BEGIN_DECLS

#define GB_TYPE_DBUS_DAEMON            (gb_dbus_daemon_get_type())
#define GB_DBUS_DAEMON(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GB_TYPE_DBUS_DAEMON, GbDbusDaemon))
#define GB_DBUS_DAEMON_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GB_TYPE_DBUS_DAEMON, GbDbusDaemon const))
#define GB_DBUS_DAEMON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GB_TYPE_DBUS_DAEMON, GbDbusDaemonClass))
#define GB_IS_DBUS_DAEMON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GB_TYPE_DBUS_DAEMON))
#define GB_IS_DBUS_DAEMON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  GB_TYPE_DBUS_DAEMON))
#define GB_DBUS_DAEMON_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GB_TYPE_DBUS_DAEMON, GbDbusDaemonClass))

typedef struct _GbDbusDaemon        GbDbusDaemon;
typedef struct _GbDbusDaemonClass   GbDbusDaemonClass;
typedef struct _GbDbusDaemonPrivate GbDbusDaemonPrivate;

struct _GbDbusDaemon
{
   GObject parent;

   /*< private >*/
   GbDbusDaemonPrivate *priv;
};

struct _GbDbusDaemonClass
{
   GObjectClass parent_class;
};

GbDbusDaemon    *gb_dbus_daemon_new            (void);
GType            gb_dbus_daemon_get_type       (void) G_GNUC_CONST;
GDBusConnection *gb_dbus_daemon_get_connection (GbDbusDaemon *daemon);
const gchar     *gb_dbus_daemon_get_address    (GbDbusDaemon *daemon);
void             gb_dbus_daemon_start          (GbDbusDaemon *daemon);
void             gb_dbus_daemon_stop           (GbDbusDaemon *daemon);

G_END_DECLS

#endif /* GB_DBUS_DAEMON_H */
