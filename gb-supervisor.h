/* gb-supervisor.h
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

#ifndef GB_SUPERVISOR_H
#define GB_SUPERVISOR_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define GB_TYPE_SUPERVISOR            (gb_supervisor_get_type())
#define GB_SUPERVISOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GB_TYPE_SUPERVISOR, GbSupervisor))
#define GB_SUPERVISOR_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GB_TYPE_SUPERVISOR, GbSupervisor const))
#define GB_SUPERVISOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GB_TYPE_SUPERVISOR, GbSupervisorClass))
#define GB_IS_SUPERVISOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GB_TYPE_SUPERVISOR))
#define GB_IS_SUPERVISOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  GB_TYPE_SUPERVISOR))
#define GB_SUPERVISOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GB_TYPE_SUPERVISOR, GbSupervisorClass))

typedef struct _GbSupervisor        GbSupervisor;
typedef struct _GbSupervisorClass   GbSupervisorClass;
typedef struct _GbSupervisorPrivate GbSupervisorPrivate;

struct _GbSupervisor
{
   GObject parent;

   /*< private >*/
   GbSupervisorPrivate *priv;
};

struct _GbSupervisorClass
{
   GObjectClass parent_class;
};

void          gb_supervisor_add_launcher   (GbSupervisor         *supervisor,
                                            GSubprocessLauncher  *launcher,
                                            const gchar * const  *argv);
void          gb_supervisor_add_pid        (GbSupervisor         *supervisor,
                                            GPid                  pid);
void          gb_supervisor_add_subprocess (GbSupervisor         *supervisor,
                                            GSubprocess          *subprocess);
GType         gb_supervisor_get_type       (void) G_GNUC_CONST;
GbSupervisor *gb_supervisor_new            (void);
gboolean      gb_supervisor_run            (GbSupervisor         *supervisor,
                                            GError              **error);
void          gb_supervisor_shutdown       (GbSupervisor         *supervisor);

G_END_DECLS

#endif /* GB_SUPERVISOR_H */
