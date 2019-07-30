/*
 * pluma-docinfo-plugin.c
 *
 * Copyright (C) 2002-2005 Paolo Maggi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $Id$
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pluma-docinfo-plugin.h"

#include <string.h> /* For strlen (...) */

#include <glib/gi18n-lib.h>
#include <pango/pango-break.h>
#include <gmodule.h>
#include <libpeas/peas-activatable.h>

#include <pluma/pluma-window.h>
#include <pluma/pluma-debug.h>
#include <pluma/pluma-utils.h>

#define MENU_PATH "/MenuBar/ToolsMenu/ToolsOps_2"

static void peas_activatable_iface_init (PeasActivatableInterface *iface);

typedef struct
{
	GtkWidget *dialog;
	GtkWidget *file_name_label;
	GtkWidget *lines_label;
	GtkWidget *words_label;
	GtkWidget *chars_label;
	GtkWidget *chars_ns_label;
	GtkWidget *bytes_label;
	GtkWidget *selection_vbox;
	GtkWidget *selected_lines_label;
	GtkWidget *selected_words_label;
	GtkWidget *selected_chars_label;
	GtkWidget *selected_chars_ns_label;
	GtkWidget *selected_bytes_label;
} DocInfoDialog;

struct _PlumaDocInfoPluginPrivate
{
	GtkWidget *window;

	GtkActionGroup *ui_action_group;
	guint ui_id;

	DocInfoDialog *dialog;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (PlumaDocInfoPlugin,
                                pluma_docinfo_plugin,
                                PEAS_TYPE_EXTENSION_BASE,
                                0,
                                G_ADD_PRIVATE_DYNAMIC (PlumaDocInfoPlugin)
                                G_IMPLEMENT_INTERFACE_DYNAMIC (PEAS_TYPE_ACTIVATABLE,
                                                               peas_activatable_iface_init))

enum {
	PROP_0,
	PROP_OBJECT
};

static void docinfo_dialog_response_cb (GtkDialog   *widget,
					gint	    res_id,
					PlumaDocInfoPluginPrivate *data);

static void
docinfo_dialog_destroy_cb (GObject  *obj,
			   PlumaDocInfoPluginPrivate *data)
{
	pluma_debug (DEBUG_PLUGINS);

	if (data != NULL)
	{
		g_free (data->dialog);
		data->dialog = NULL;
	}
}

static DocInfoDialog *
get_docinfo_dialog (PlumaDocInfoPlugin *plugin)
{
	PlumaDocInfoPluginPrivate *data;
	PlumaWindow *window;
	DocInfoDialog *dialog;
	gchar *data_dir;
	gchar *ui_file;
	GtkWidget *content;
	GtkWidget *error_widget;
	gboolean ret;

	pluma_debug (DEBUG_PLUGINS);

	data = plugin->priv;
	window = PLUMA_WINDOW (data->window);

	dialog = g_new (DocInfoDialog, 1);

	data_dir = peas_extension_base_get_data_dir (PEAS_EXTENSION_BASE (plugin));
	ui_file = g_build_filename (data_dir, "docinfo.ui", NULL);
	ret = pluma_utils_get_ui_objects (ui_file,
					  NULL,
					  &error_widget,
					  "dialog", &dialog->dialog,
					  "docinfo_dialog_content", &content,
					  "file_name_label", &dialog->file_name_label,
					  "words_label", &dialog->words_label,
					  "bytes_label", &dialog->bytes_label,
					  "lines_label", &dialog->lines_label,
					  "chars_label", &dialog->chars_label,
					  "chars_ns_label", &dialog->chars_ns_label,
					  "selection_vbox", &dialog->selection_vbox,
					  "selected_words_label", &dialog->selected_words_label,
					  "selected_bytes_label", &dialog->selected_bytes_label,
					  "selected_lines_label", &dialog->selected_lines_label,
					  "selected_chars_label", &dialog->selected_chars_label,
					  "selected_chars_ns_label", &dialog->selected_chars_ns_label,
					  NULL);

	g_free (data_dir);
	g_free (ui_file);

	if (!ret)
	{
		const gchar *err_message;

		err_message = gtk_label_get_label (GTK_LABEL (error_widget));
		pluma_warning (GTK_WINDOW (window), "%s", err_message);

		g_free (dialog);
		gtk_widget_destroy (error_widget);

		return NULL;
	}

	gtk_dialog_set_default_response (GTK_DIALOG (dialog->dialog),
					 GTK_RESPONSE_OK);
	gtk_window_set_transient_for (GTK_WINDOW (dialog->dialog),
				      GTK_WINDOW (window));

	g_signal_connect (dialog->dialog,
			  "destroy",
			  G_CALLBACK (docinfo_dialog_destroy_cb),
			  data);

	g_signal_connect (dialog->dialog,
			  "response",
			  G_CALLBACK (docinfo_dialog_response_cb),
			  data);

	return dialog;
}

static void
calculate_info (PlumaDocument *doc,
		GtkTextIter   *start,
		GtkTextIter   *end,
		gint          *chars,
		gint          *words,
		gint          *white_chars,
		gint          *bytes)
{
	gchar *text;

	pluma_debug (DEBUG_PLUGINS);

	text = gtk_text_buffer_get_slice (GTK_TEXT_BUFFER (doc),
					  start,
					  end,
					  TRUE);

	*chars = g_utf8_strlen (text, -1);
	*bytes = strlen (text);

	if (*chars > 0)
	{
		PangoLogAttr *attrs;
		gint i;

		attrs = g_new0 (PangoLogAttr, *chars + 1);

		pango_get_log_attrs (text,
				     -1,
				     0,
				     pango_language_from_string ("C"),
				     attrs,
				     *chars + 1);

		for (i = 0; i < (*chars); i++)
		{
			if (attrs[i].is_white)
				++(*white_chars);

			if (attrs[i].is_word_start)
				++(*words);
		}

		g_free (attrs);
	}
	else
	{
		*white_chars = 0;
		*words = 0;
	}

	g_free (text);
}

static void
docinfo_real (PlumaDocument *doc,
	      DocInfoDialog *dialog)
{
	GtkTextIter start, end;
	gint words = 0;
	gint chars = 0;
	gint white_chars = 0;
	gint lines = 0;
	gint bytes = 0;
	gchar *tmp_str;
	gchar *doc_name;

	pluma_debug (DEBUG_PLUGINS);

	gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (doc),
				    &start,
				    &end);

	lines = gtk_text_buffer_get_line_count (GTK_TEXT_BUFFER (doc));

	calculate_info (doc,
			&start, &end,
			&chars, &words, &white_chars, &bytes);

	if (chars == 0)
		lines = 0;

	pluma_debug_message (DEBUG_PLUGINS, "Chars: %d", chars);
	pluma_debug_message (DEBUG_PLUGINS, "Lines: %d", lines);
	pluma_debug_message (DEBUG_PLUGINS, "Words: %d", words);
	pluma_debug_message (DEBUG_PLUGINS, "Chars non-space: %d", chars - white_chars);
	pluma_debug_message (DEBUG_PLUGINS, "Bytes: %d", bytes);

	doc_name = pluma_document_get_short_name_for_display (doc);
	tmp_str = g_strdup_printf ("<span weight=\"bold\">%s</span>", doc_name);
	gtk_label_set_markup (GTK_LABEL (dialog->file_name_label), tmp_str);
	g_free (doc_name);
	g_free (tmp_str);

	tmp_str = g_strdup_printf("%d", lines);
	gtk_label_set_text (GTK_LABEL (dialog->lines_label), tmp_str);
	g_free (tmp_str);

	tmp_str = g_strdup_printf("%d", words);
	gtk_label_set_text (GTK_LABEL (dialog->words_label), tmp_str);
	g_free (tmp_str);

	tmp_str = g_strdup_printf("%d", chars);
	gtk_label_set_text (GTK_LABEL (dialog->chars_label), tmp_str);
	g_free (tmp_str);

	tmp_str = g_strdup_printf("%d", chars - white_chars);
	gtk_label_set_text (GTK_LABEL (dialog->chars_ns_label), tmp_str);
	g_free (tmp_str);

	tmp_str = g_strdup_printf("%d", bytes);
	gtk_label_set_text (GTK_LABEL (dialog->bytes_label), tmp_str);
	g_free (tmp_str);
}

static void
selectioninfo_real (PlumaDocument *doc,
		    DocInfoDialog *dialog)
{
	gboolean sel;
	GtkTextIter start, end;
	gint words = 0;
	gint chars = 0;
	gint white_chars = 0;
	gint lines = 0;
	gint bytes = 0;
	gchar *tmp_str;

	pluma_debug (DEBUG_PLUGINS);

	sel = gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (doc),
						    &start,
						    &end);

	if (sel)
	{
		lines = gtk_text_iter_get_line (&end) - gtk_text_iter_get_line (&start) + 1;

		calculate_info (doc,
				&start, &end,
				&chars, &words, &white_chars, &bytes);

		pluma_debug_message (DEBUG_PLUGINS, "Selected chars: %d", chars);
		pluma_debug_message (DEBUG_PLUGINS, "Selected lines: %d", lines);
		pluma_debug_message (DEBUG_PLUGINS, "Selected words: %d", words);
		pluma_debug_message (DEBUG_PLUGINS, "Selected chars non-space: %d", chars - white_chars);
		pluma_debug_message (DEBUG_PLUGINS, "Selected bytes: %d", bytes);

		gtk_widget_set_sensitive (dialog->selection_vbox, TRUE);
	}
	else
	{
		gtk_widget_set_sensitive (dialog->selection_vbox, FALSE);

		pluma_debug_message (DEBUG_PLUGINS, "Selection empty");
	}

	if (chars == 0)
		lines = 0;

	tmp_str = g_strdup_printf("%d", lines);
	gtk_label_set_text (GTK_LABEL (dialog->selected_lines_label), tmp_str);
	g_free (tmp_str);

	tmp_str = g_strdup_printf("%d", words);
	gtk_label_set_text (GTK_LABEL (dialog->selected_words_label), tmp_str);
	g_free (tmp_str);

	tmp_str = g_strdup_printf("%d", chars);
	gtk_label_set_text (GTK_LABEL (dialog->selected_chars_label), tmp_str);
	g_free (tmp_str);

	tmp_str = g_strdup_printf("%d", chars - white_chars);
	gtk_label_set_text (GTK_LABEL (dialog->selected_chars_ns_label), tmp_str);
	g_free (tmp_str);

	tmp_str = g_strdup_printf("%d", bytes);
	gtk_label_set_text (GTK_LABEL (dialog->selected_bytes_label), tmp_str);
	g_free (tmp_str);
}

static void
docinfo_cb (GtkAction	*action,
	    PlumaDocInfoPlugin *plugin)
{
	PlumaDocInfoPluginPrivate *data;
	PlumaWindow *window;
	PlumaDocument *doc;

	pluma_debug (DEBUG_PLUGINS);

	data = plugin->priv;
	window = PLUMA_WINDOW (data->window);
	doc = pluma_window_get_active_document (window);
	g_return_if_fail (doc != NULL);

	if (data->dialog != NULL)
	{
		gtk_window_present (GTK_WINDOW (data->dialog->dialog));
		gtk_widget_grab_focus (GTK_WIDGET (data->dialog->dialog));
	}
	else
	{
		DocInfoDialog *dialog;

		dialog = get_docinfo_dialog (plugin);
		g_return_if_fail (dialog != NULL);

		data->dialog = dialog;

		gtk_widget_show (GTK_WIDGET (dialog->dialog));
	}

	docinfo_real (doc,
		      data->dialog);
	selectioninfo_real (doc,
			    data->dialog);
}

static void
docinfo_dialog_response_cb (GtkDialog	*widget,
			    gint	res_id,
			    PlumaDocInfoPluginPrivate *data)
{
	PlumaWindow *window;

	pluma_debug (DEBUG_PLUGINS);

	window = PLUMA_WINDOW (data->window);

	switch (res_id)
	{
		case GTK_RESPONSE_CLOSE:
		{
			pluma_debug_message (DEBUG_PLUGINS, "GTK_RESPONSE_CLOSE");
			gtk_widget_destroy (data->dialog->dialog);

			break;
		}

		case GTK_RESPONSE_OK:
		{
			PlumaDocument *doc;

			pluma_debug_message (DEBUG_PLUGINS, "GTK_RESPONSE_OK");

			doc = pluma_window_get_active_document (window);
			g_return_if_fail (doc != NULL);

			docinfo_real (doc,
				      data->dialog);

			selectioninfo_real (doc,
					    data->dialog);

			break;
		}
	}
}

static const GtkActionEntry action_entries[] =
{
	{ "DocumentStatistics",
	  NULL,
	  N_("_Document Statistics"),
	  NULL,
	  N_("Get statistical information on the current document"),
	  G_CALLBACK (docinfo_cb) }
};

static void
update_ui (PlumaDocInfoPluginPrivate *data)
{
	PlumaWindow *window;
	PlumaView *view;

	pluma_debug (DEBUG_PLUGINS);

	window = PLUMA_WINDOW (data->window);
	view = pluma_window_get_active_view (window);

	gtk_action_group_set_sensitive (data->ui_action_group,
					(view != NULL));

	if (data->dialog != NULL)
	{
		gtk_dialog_set_response_sensitive (GTK_DIALOG (data->dialog->dialog),
						   GTK_RESPONSE_OK,
						   (view != NULL));
	}
}

static void
pluma_docinfo_plugin_init (PlumaDocInfoPlugin *plugin)
{
	pluma_debug_message (DEBUG_PLUGINS, "PlumaDocInfoPlugin initializing");

	plugin->priv = pluma_docinfo_plugin_get_instance_private (plugin);
}

static void
pluma_docinfo_plugin_dispose (GObject *object)
{
	PlumaDocInfoPlugin *plugin = PLUMA_DOCINFO_PLUGIN (object);

	pluma_debug_message (DEBUG_PLUGINS, "PlumaDocInfoPlugin disposing");

	if (plugin->priv->window != NULL)
	{
		g_object_unref (plugin->priv->window);
		plugin->priv->window = NULL;
	}

	if (plugin->priv->ui_action_group != NULL)
	{
		g_object_unref (plugin->priv->ui_action_group);
		plugin->priv->ui_action_group = NULL;
	}

	G_OBJECT_CLASS (pluma_docinfo_plugin_parent_class)->dispose (object);
}

static void
pluma_docinfo_plugin_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
	PlumaDocInfoPlugin *plugin = PLUMA_DOCINFO_PLUGIN (object);

	switch (prop_id)
	{
		case PROP_OBJECT:
			plugin->priv->window = GTK_WIDGET (g_value_dup_object (value));
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
pluma_docinfo_plugin_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	PlumaDocInfoPlugin *plugin = PLUMA_DOCINFO_PLUGIN (object);

	switch (prop_id)
	{
		case PROP_OBJECT:
			g_value_set_object (value, plugin->priv->window);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
pluma_docinfo_plugin_activate (PeasActivatable *activatable)
{
	PlumaDocInfoPlugin *plugin;
	PlumaDocInfoPluginPrivate *data;
	PlumaWindow *window;
	GtkUIManager *manager;

	pluma_debug (DEBUG_PLUGINS);

	plugin = PLUMA_DOCINFO_PLUGIN (activatable);
	data = plugin->priv;
	window = PLUMA_WINDOW (data->window);

	data->dialog = NULL;
	data->ui_action_group = gtk_action_group_new ("PlumaDocInfoPluginActions");

	gtk_action_group_set_translation_domain (data->ui_action_group,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (data->ui_action_group,
				      action_entries,
				      G_N_ELEMENTS (action_entries),
				      plugin);

	manager = pluma_window_get_ui_manager (window);
	gtk_ui_manager_insert_action_group (manager,
					    data->ui_action_group,
					    -1);

	data->ui_id = gtk_ui_manager_new_merge_id (manager);

	gtk_ui_manager_add_ui (manager,
			       data->ui_id,
			       MENU_PATH,
			       "DocumentStatistics",
			       "DocumentStatistics",
			       GTK_UI_MANAGER_MENUITEM,
			       FALSE);

	update_ui (data);
}

static void
pluma_docinfo_plugin_deactivate (PeasActivatable *activatable)
{
	PlumaDocInfoPluginPrivate *data;
	PlumaWindow *window;
	GtkUIManager *manager;

	pluma_debug (DEBUG_PLUGINS);

	data = PLUMA_DOCINFO_PLUGIN (activatable)->priv;
	window = PLUMA_WINDOW (data->window);

	manager = pluma_window_get_ui_manager (window);

	gtk_ui_manager_remove_ui (manager,
				  data->ui_id);
	gtk_ui_manager_remove_action_group (manager,
					    data->ui_action_group);
}

static void
pluma_docinfo_plugin_update_state (PeasActivatable *activatable)
{
	pluma_debug (DEBUG_PLUGINS);

	update_ui (PLUMA_DOCINFO_PLUGIN (activatable)->priv);
}

static void
pluma_docinfo_plugin_class_init (PlumaDocInfoPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = pluma_docinfo_plugin_dispose;
	object_class->set_property = pluma_docinfo_plugin_set_property;
	object_class->get_property = pluma_docinfo_plugin_get_property;

	g_object_class_override_property (object_class, PROP_OBJECT, "object");
}

static void
pluma_docinfo_plugin_class_finalize (PlumaDocInfoPluginClass *klass)
{
	/* dummy function - used by G_DEFINE_DYNAMIC_TYPE_EXTENDED */
}

static void
peas_activatable_iface_init (PeasActivatableInterface *iface)
{
	iface->activate = pluma_docinfo_plugin_activate;
	iface->deactivate = pluma_docinfo_plugin_deactivate;
	iface->update_state = pluma_docinfo_plugin_update_state;
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	pluma_docinfo_plugin_register_type (G_TYPE_MODULE (module));

	peas_object_module_register_extension_type (module,
	                                            PEAS_TYPE_ACTIVATABLE,
	                                            PLUMA_TYPE_DOCINFO_PLUGIN);
}
