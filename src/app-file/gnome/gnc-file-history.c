/********************************************************************\
 * gnc-file-history.c -- functions to maintain file history menu    *
 * Copyright (C) 2000 Robby Stephenson         	                    *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
\********************************************************************/

#include "config.h"

#include <gnome.h>

#include "gnc-file-history-gnome.h"
#include "gnc-file.h"

static GSList *history_list = NULL;
static gint num_menu_entries = -1;
static char *add_after_string = NULL;


void
gnc_file_history_add_after (const char *menu_string)
{
  g_free (add_after_string);
  add_after_string = g_strdup (menu_string);
}

static void
gnc_history_config_write (void)
{
  int max_files, i = 0;
  char *key;
  GSList *tmp;

  if (history_list == NULL)
    return;

  key = g_strdup_printf ("/GnuCash/History/MaxFiles=%d", MAX_HISTORY);
  max_files = gnome_config_get_int (key);
  g_free (key);

  gnome_config_clean_section ("/GnuCash/History/");
  gnome_config_push_prefix ("/GnuCash/History/");
  gnome_config_set_int ("MaxFiles", max_files);

  for (tmp = history_list; tmp != NULL; tmp = tmp->next)
  {
    key = g_strdup_printf ("File%d", i);
    gnome_config_set_string (key, tmp->data);
    g_free(key);
    i++;
  }

  gnome_config_sync ();
  gnome_config_pop_prefix ();
}

static void
gnc_history_get_list (void)
{
  int max_files, i;
  char *key, *filename;

  key = g_strdup_printf ("/GnuCash/History/MaxFiles=%d", MAX_HISTORY);
  max_files = gnome_config_get_int (key);
  g_free (key);

  gnome_config_push_prefix ("/GnuCash/History/");

  for (i = 0; i < max_files; i++)
  {
    key = g_strdup_printf ("File%d", i);
    filename = gnome_config_get_string (key);

    if(filename == NULL)
    {
      g_free(key);
      break;
    }

    history_list = g_slist_prepend (history_list, filename);
    g_free (key);
  }

  gnome_config_pop_prefix ();

  history_list = g_slist_reverse (history_list);
}

static void
gnc_history_file_cb (GtkWidget *w, char *data)
{
  gnc_file_open_file (data);
}

void
gnc_history_add_file (const char *newfile)
{
  int i, max_files;
  gboolean used_default, matched = FALSE;
  char *key = NULL;
  GSList *tmp, *new_list = NULL;

  if (newfile == NULL)
    return;

  gnome_config_push_prefix ("/GnuCash/History/");
  key = g_strdup_printf ("/GnuCash/History/MaxFiles=%d", MAX_HISTORY);
  max_files = gnome_config_get_int_with_default (key, &used_default);
  g_free (key);

  if (used_default)
    gnome_config_set_int ("MaxFiles", max_files);

  if (history_list == NULL)
    gnc_history_get_list ();

  i = 0;
  tmp = history_list;
  while (tmp != NULL && i < max_files)
  {
    if (!matched &&                                     /* no match yet */
        ((i == max_files - 1) ||                        /* last entry */
         (strcmp(newfile, (char *)tmp->data) == 0)))    /* filename match */
    {
      g_free (tmp->data);
      matched = TRUE;
    }
    else
    {
      new_list = g_slist_prepend (new_list, tmp->data);
    }

    i++;
    tmp = tmp->next;
  }

  new_list = g_slist_reverse (new_list);
  new_list = g_slist_prepend (new_list, g_strdup (newfile));
  g_slist_free (history_list);
  history_list = new_list;

  gnc_history_config_write ();

  /* Update apps immediately */
  {
    GList *containers = gtk_container_get_toplevels ();
  
    while (containers)
    {
      GtkWidget *w = containers->data;

      if (GNOME_IS_APP (w))
        gnc_history_update_menu (w);

      containers = containers->next;
    }
  }
}

const char *
gnc_history_get_last (void)
{
  if (history_list == NULL)
    gnc_history_get_list ();

  if (history_list == NULL)
    return NULL;

  return history_list->data;
}

void
gnc_history_update_menu (GtkWidget * app_w)
{
  GnomeUIInfo *menu;
  GnomeDockItem *di;
  GtkWidget *menushell;
  GnomeApp *app;
  gpointer data;
  char *path;
  char *file;
  char *name;
  char *p, *q;
  int count;
  int i, n;
  int pos;

  if (!app_w)
    return;

  g_return_if_fail (GNOME_IS_APP (app_w));
  g_return_if_fail (add_after_string != NULL);

  app = GNOME_APP (app_w);

  di = gnome_app_get_dock_item_by_name (app, GNOME_APP_MENUBAR_NAME);
  if (di == NULL)
    return;

  menushell = gnome_dock_item_get_child (di);
  if (menushell == NULL)
    return;

  if (!gnome_app_find_menu_pos (menushell, GNOME_MENU_FILE_PATH, &pos))
    return;

  path = g_strdup_printf ("%s%s", GNOME_MENU_FILE_PATH, add_after_string);

  if (!gnome_app_find_menu_pos (menushell, path, &pos))
    return;

  data = gtk_object_get_data (GTK_OBJECT (app), "gnc_set_history");
  if (data)
  {
    int num_entries;

    data = gtk_object_get_data (GTK_OBJECT (app), "gnc_num_history");
    num_entries = GPOINTER_TO_INT (data);

    gnome_app_remove_menu_range (app, path, 0, num_entries);
  }

  if (history_list == NULL)
    gnc_history_get_list ();

  if (history_list == NULL)
    return;

  n = g_slist_length (history_list);
  /* one for each filename entry, plus one for end */
  menu = g_new (GnomeUIInfo, n + 1);

  for (i = 0; i < n; i++)
  {
    (menu+i)->type = GNOME_APP_UI_ITEM;

    /* get the file name */
    file = g_slist_nth_data (history_list, i);
    if (file == NULL)
      file = "";

    /* count the underscores */
    count = 0;
    for (p = file; *p != '\0'; p++)
      if (*p == '_')
        count++;

    /* make the name, doubling the underscores to prevent underlining */
    name = g_new (char, strlen(file) + count + 1);
    for (p = file, q = name; *p != '\0'; p++)
    {
      *q++ = *p;
      if (*p == '_')
        *q++ = '_';
    }
    *q = '\0';

    (menu+i)->label = g_strdup_printf ("_%d. %s", i+1, name);

    g_free (name);

    (menu+i)->hint = NULL;

    (menu+i)->moreinfo = gnc_history_file_cb;
    (menu+i)->user_data = file;
    (menu+i)->unused_data = NULL;
    (menu+i)->pixmap_type = 0;
    (menu+i)->pixmap_info = NULL;
    (menu+i)->accelerator_key = 0;    
  }
  (menu+i)->type = GNOME_APP_UI_ENDOFINFO;

  gnome_app_insert_menus (GNOME_APP(app), path, menu);
  num_menu_entries = n;

  gtk_object_set_data (GTK_OBJECT (app), "gnc_set_history", app);
  gtk_object_set_data (GTK_OBJECT (app), "gnc_num_history",
                       GINT_TO_POINTER (num_menu_entries));

  for (i = 0; i < n; i++)
    g_free ((menu+i)->label);

  g_free (menu);
  g_free (path);
}