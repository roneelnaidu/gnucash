/* 
 * gnc-tree-model-account.c -- GtkTreeModel implementation to
 *	display accounts in a GtkTreeView.
 *
 * Copyright (C) 2003 Jan Arne Petersen <jpetersen@uni-bonn.de>
 * Copyright (C) 2003 David Hampton <hampton@employees.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, contact:
 *
 * Free Software Foundation           Voice:  +1-617-542-5942
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652
 * Boston, MA  02111-1307,  USA       gnu@gnu.org
 */

#include "config.h"
#include <string.h>
#include <gtk/gtkmain.h>

#include "gnc-tree-model-account.h"

#include "gnc-component-manager.h"
#include "Account.h"
#include "Group.h"
#include "gnc-commodity.h"
#include "gnc-engine-util.h"
#include "gnc-ui-util.h"
#include "messages.h"

#define TREE_MODEL_ACCOUNT_CM_CLASS "tree-model-account"

/** Static Globals *******************************************************/
static short module = MOD_GUI;
static GList *active_models = NULL;

/** Declarations *********************************************************/
static void gnc_tree_model_account_class_init (GncTreeModelAccountClass *klass);
static void gnc_tree_model_account_init (GncTreeModelAccount *model);
static void gnc_tree_model_account_finalize (GObject *object);
static void gnc_tree_model_account_destroy (GtkObject *object);

static void gnc_tree_model_account_tree_model_init (GtkTreeModelIface *iface);
static guint gnc_tree_model_account_get_flags (GtkTreeModel *tree_model);
static int gnc_tree_model_account_get_n_columns (GtkTreeModel *tree_model);
static GType gnc_tree_model_account_get_column_type (GtkTreeModel *tree_model,
						     int index);
static gboolean gnc_tree_model_account_get_iter (GtkTreeModel *tree_model,
						 GtkTreeIter *iter,
						 GtkTreePath *path);
static GtkTreePath *gnc_tree_model_account_get_path (GtkTreeModel *tree_model,
						     GtkTreeIter *iter);
static void gnc_tree_model_account_get_value (GtkTreeModel *tree_model,
					      GtkTreeIter *iter,
					      int column,
					      GValue *value);
static gboolean	gnc_tree_model_account_iter_next (GtkTreeModel *tree_model,
						  GtkTreeIter *iter);
static gboolean	gnc_tree_model_account_iter_children (GtkTreeModel *tree_model,
						      GtkTreeIter *iter,
						      GtkTreeIter *parent);
static gboolean	gnc_tree_model_account_iter_has_child (GtkTreeModel *tree_model,
						       GtkTreeIter *iter);
static int gnc_tree_model_account_iter_n_children (GtkTreeModel *tree_model,
						   GtkTreeIter *iter);
static gboolean	gnc_tree_model_account_iter_nth_child (GtkTreeModel *tree_model,
						       GtkTreeIter *iter,
						       GtkTreeIter *parent,
						       int n);
static gboolean	gnc_tree_model_account_iter_parent (GtkTreeModel *tree_model,
						    GtkTreeIter *iter,
    						    GtkTreeIter *child);

static void gnc_tree_model_account_set_toplevel (GncTreeModelAccount *model,
						 Account *toplevel);

static void gnc_tree_model_account_event_handler (GUID *entity, QofIdType type,
						  GNCEngineEventType event_type,
						  gpointer user_data);

struct GncTreeModelAccountPrivate
{
	AccountGroup *root;
	Account *toplevel;
	gint event_handler_id;
};


/************************************************************/
/*               g_object required functions                */
/************************************************************/

static GtkObjectClass *parent_class = NULL;

GType
gnc_tree_model_account_get_type (void)
{
	static GType gnc_tree_model_account_type = 0;

	if (gnc_tree_model_account_type == 0) {
		static const GTypeInfo our_info = {
			sizeof (GncTreeModelAccountClass), /* class_size */
			NULL,   			   /* base_init */
			NULL,				   /* base_finalize */
			(GClassInitFunc) gnc_tree_model_account_class_init,
			NULL,				   /* class_finalize */
			NULL,				   /* class_data */
			sizeof (GncTreeModelAccount),	   /* */
			0,				   /* n_preallocs */
			(GInstanceInitFunc) gnc_tree_model_account_init
		};
		
		static const GInterfaceInfo tree_model_info = {
			(GInterfaceInitFunc) gnc_tree_model_account_tree_model_init,
			NULL,
			NULL
		};

		gnc_tree_model_account_type = g_type_register_static (GTK_TYPE_OBJECT,
								      "GncTreeModelAccount",
								      &our_info, 0);
		
		g_type_add_interface_static (gnc_tree_model_account_type,
					     GTK_TYPE_TREE_MODEL,
					     &tree_model_info);
	}

	return gnc_tree_model_account_type;
}

#if DEBUG_REFERENCE_COUNTING
static void
dump_model (GncTreeModelAccount *model, gpointer dummy)
{
    g_warning("GncTreeModelAccount %p still exists.", model);
}

static gint
gnc_tree_model_account_report_references (void)
{
  g_list_foreach(active_models, (GFunc)dump_model, NULL);
  return 0;
}
#endif

static void
gnc_tree_model_account_class_init (GncTreeModelAccountClass *klass)
{
	GObjectClass *o_class;
	GtkObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	o_class = G_OBJECT_CLASS (klass);
	object_class = GTK_OBJECT_CLASS (klass);

	/* GObject signals */
	o_class->finalize = gnc_tree_model_account_finalize;

	/* GtkObject signals */
	object_class->destroy = gnc_tree_model_account_destroy;

#if DEBUG_REFERENCE_COUNTING
	gtk_quit_add (0,
		      (GtkFunction)gnc_tree_model_account_report_references,
		      NULL);
#endif
}

static void
gnc_tree_model_account_init (GncTreeModelAccount *model)
{
	ENTER("model %p", model);
	while (model->stamp == 0) {
		model->stamp = g_random_int ();
	}

	model->priv = g_new0 (GncTreeModelAccountPrivate, 1);
	model->priv->root = NULL;
	model->priv->toplevel = NULL;

	active_models = g_list_append (active_models, model);

	LEAVE(" ");
}

static void
gnc_tree_model_account_finalize (GObject *object)
{
	GncTreeModelAccount *model;

	ENTER("model %p", object);
	g_return_if_fail (object != NULL);
	g_return_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (object));

	model = GNC_TREE_MODEL_ACCOUNT (object);
	active_models = g_list_remove (active_models, model);

	g_free (model->priv);

	if (G_OBJECT_CLASS (parent_class)->finalize)
	  (* G_OBJECT_CLASS (parent_class)->finalize) (object);
	LEAVE(" ");
}

static void
gnc_tree_model_account_destroy (GtkObject *object)
{
	GncTreeModelAccount *model;

	ENTER("model %p", object);
	g_return_if_fail (object != NULL);
	g_return_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (object));

	model = GNC_TREE_MODEL_ACCOUNT (object);

	if (model->priv->event_handler_id) {
	  gnc_engine_unregister_event_handler (model->priv->event_handler_id);
	  model->priv->event_handler_id = 0;
	}

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
	  (* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
	LEAVE(" ");
}


/************************************************************/
/*                   New Model Creation                     */
/************************************************************/

GtkTreeModel *
gnc_tree_model_account_new (AccountGroup *group)
{
	GncTreeModelAccount *model;
	GncTreeModelAccountPrivate *priv;
	GList *item;
	
	ENTER("group %p", group);
	for (item = active_models; item; item = g_list_next(item)) {
		model = (GncTreeModelAccount *)item->data;
		if (model->priv->root == group) {
			LEAVE("returning existing model %p", model);
			return GTK_TREE_MODEL(model);
		}
	}

	model = g_object_new (GNC_TYPE_TREE_MODEL_ACCOUNT,
			      NULL);

	priv = model->priv;
	priv->root = group;

	{
	  Account *account;

	  account = xaccMallocAccount(gnc_get_current_book());
	  gnc_tree_model_account_set_toplevel (model, account);
	}

	priv->event_handler_id =
	  gnc_engine_register_event_handler (gnc_tree_model_account_event_handler, model);

	LEAVE("model %p", model);
	return GTK_TREE_MODEL (model);
}


/************************************************************/
/*        Gnc Tree Model Debugging Utility Function         */
/************************************************************/

#define ITER_STRING_LEN 128

static const gchar *
iter_to_string (GtkTreeIter *iter)
{
#ifdef G_THREADS_ENABLED
  static GStaticPrivate gtmits_buffer_key = G_STATIC_PRIVATE_INIT;
  gchar *string;

  string = g_static_private_get (&gtmits_buffer_key);
  if (string == NULL) {
    string = malloc(ITER_STRING_LEN + 1);
    g_static_private_set (&gtmits_buffer_key, string, g_free);
  }
#else
  static char string[ITER_STRING_LEN + 1];
#endif

  if (iter)
    snprintf(string, ITER_STRING_LEN,
	     "[stamp:%x data:%p (%s), %p, %d]",
	     iter->stamp, iter->user_data,
	     xaccAccountGetName ((Account *) iter->user_data),
	     iter->user_data2, GPOINTER_TO_INT(iter->user_data3));
  else
    strcpy(string, "(null)");
  return string;
}


/************************************************************/
/*       Gtk Tree Model Required Interface Functions        */
/************************************************************/

static void
gnc_tree_model_account_tree_model_init (GtkTreeModelIface *iface)
{
	iface->get_flags       = gnc_tree_model_account_get_flags;
	iface->get_n_columns   = gnc_tree_model_account_get_n_columns;
	iface->get_column_type = gnc_tree_model_account_get_column_type;
	iface->get_iter        = gnc_tree_model_account_get_iter;
	iface->get_path        = gnc_tree_model_account_get_path;
	iface->get_value       = gnc_tree_model_account_get_value;
	iface->iter_next       = gnc_tree_model_account_iter_next;
	iface->iter_children   = gnc_tree_model_account_iter_children;
	iface->iter_has_child  = gnc_tree_model_account_iter_has_child;
	iface->iter_n_children = gnc_tree_model_account_iter_n_children;
	iface->iter_nth_child  = gnc_tree_model_account_iter_nth_child;
	iface->iter_parent     = gnc_tree_model_account_iter_parent;
}

static guint
gnc_tree_model_account_get_flags (GtkTreeModel *tree_model)
{
	return 0;
}

static int
gnc_tree_model_account_get_n_columns (GtkTreeModel *tree_model)
{
	return GNC_TREE_MODEL_ACCOUNT_NUM_COLUMNS;
}

static GType
gnc_tree_model_account_get_column_type (GtkTreeModel *tree_model,
					int index)
{
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (tree_model), G_TYPE_INVALID);
	g_return_val_if_fail ((index < GNC_TREE_MODEL_ACCOUNT_NUM_COLUMNS) && (index >= 0), G_TYPE_INVALID);

	switch (index) {
		case GNC_TREE_MODEL_ACCOUNT_COL_NAME:
		case GNC_TREE_MODEL_ACCOUNT_COL_TYPE:
		case GNC_TREE_MODEL_ACCOUNT_COL_COMMODITY:
		case GNC_TREE_MODEL_ACCOUNT_COL_CODE:
		case GNC_TREE_MODEL_ACCOUNT_COL_DESCRIPTION:
		case GNC_TREE_MODEL_ACCOUNT_COL_PRESENT:
		case GNC_TREE_MODEL_ACCOUNT_COL_PRESENT_REPORT:
		case GNC_TREE_MODEL_ACCOUNT_COL_BALANCE:
		case GNC_TREE_MODEL_ACCOUNT_COL_BALANCE_REPORT:
		case GNC_TREE_MODEL_ACCOUNT_COL_CLEARED:
		case GNC_TREE_MODEL_ACCOUNT_COL_CLEARED_REPORT:
		case GNC_TREE_MODEL_ACCOUNT_COL_RECONCILED:
		case GNC_TREE_MODEL_ACCOUNT_COL_RECONCILED_REPORT:
		case GNC_TREE_MODEL_ACCOUNT_COL_FUTURE_MIN:
		case GNC_TREE_MODEL_ACCOUNT_COL_FUTURE_MIN_REPORT:
		case GNC_TREE_MODEL_ACCOUNT_COL_TOTAL:
		case GNC_TREE_MODEL_ACCOUNT_COL_TOTAL_REPORT:
		case GNC_TREE_MODEL_ACCOUNT_COL_NOTES:
		case GNC_TREE_MODEL_ACCOUNT_COL_TAX_INFO:
		case GNC_TREE_MODEL_ACCOUNT_COL_LASTNUM:

		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_PRESENT:
		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_BALANCE:
		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_CLEARED:
		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_RECONCILED:
		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_FUTURE_MIN:
		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_TOTAL:
			return G_TYPE_STRING;

		case GNC_TREE_MODEL_ACCOUNT_COL_PLACEHOLDER:
			return G_TYPE_BOOLEAN;

		case GNC_TREE_MODEL_ACCOUNT_COL_ALIGN_RIGHT:
			return G_TYPE_FLOAT;

		default:
			g_assert_not_reached ();
			return G_TYPE_INVALID;
	}
}

static gboolean
gnc_tree_model_account_get_iter (GtkTreeModel *tree_model,
				 GtkTreeIter *iter,
				 GtkTreePath *path)
{
	GncTreeModelAccount *model;
	Account *account = NULL;
	AccountGroup *group = NULL, *children;
	gint i = 0, *indices;
	GtkTreePath *path_copy;

	{
	  gchar *path_string = gtk_tree_path_to_string(path);
	  ENTER("model %p, iter %p, path %s", tree_model, iter, path_string);
	  g_free(path_string);
	}
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (tree_model), FALSE);

	model = GNC_TREE_MODEL_ACCOUNT (tree_model);

	path_copy = gtk_tree_path_copy (path);

	if (model->priv->toplevel != NULL) {
		if (gtk_tree_path_get_depth (path) > 1) {
			i++;
		} else {

			iter->user_data = model->priv->toplevel;
			iter->user_data2 = NULL;
			iter->user_data3 = GINT_TO_POINTER (0);
			iter->stamp = model->stamp;

			LEAVE("iter (1) %s", iter_to_string(iter));
			return TRUE;
		}
	}

	if (model->priv->root == NULL) {
		LEAVE("failed (2)");
		return FALSE;
	}

	children = model->priv->root;

	indices = gtk_tree_path_get_indices (path);
	for (; i < gtk_tree_path_get_depth (path); i++) {
		group = children;
		if (indices[i] >= xaccGroupGetNumAccounts (group)) {
			iter->stamp = 0;

			LEAVE("failed (3)");
			return FALSE;
		}

		account = xaccGroupGetAccount (group, indices[i]);
		children = xaccAccountGetChildren (account);
	}

	if (account == NULL || group == NULL) {
		iter->stamp = 0;

		LEAVE("failed (4)");
		return FALSE;
	}

	iter->stamp = model->stamp;
	iter->user_data = account;
	iter->user_data2 = group;
	iter->user_data3 = GINT_TO_POINTER (indices[i - 1]);

	LEAVE("iter (5) %s", iter_to_string(iter));
	return TRUE;
}

static GtkTreePath *
gnc_tree_model_account_get_path (GtkTreeModel *tree_model,
				 GtkTreeIter *iter)
{
	GncTreeModelAccount *model = GNC_TREE_MODEL_ACCOUNT (tree_model);
	Account *account;
	AccountGroup *group;
	GtkTreePath *path;
	gint i;
	gboolean found, finished = FALSE;

	ENTER("model %p, iter %s", model, iter_to_string(iter));
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (model), NULL);
	g_return_val_if_fail (iter != NULL, NULL);
	g_return_val_if_fail (iter->user_data != NULL, NULL);
	g_return_val_if_fail (iter->stamp == model->stamp, NULL);
	
	if (model->priv->root == NULL) {
		LEAVE("failed (1)");
		return NULL;
	}

	account = (Account *) iter->user_data;
	group = (AccountGroup *) iter->user_data2;

	path = gtk_tree_path_new ();

	if (model->priv->toplevel != NULL) {
		if (account == model->priv->toplevel) {
			gtk_tree_path_append_index (path, 0);

			{
			  gchar *path_string = gtk_tree_path_to_string(path);
			  LEAVE("path (2) %s", path_string);
			  g_free(path_string);
			}
			return path;
		}
	}

	do {
		found = FALSE;
		for (i = 0; i < xaccGroupGetNumAccounts (group); i++) {
			if (xaccGroupGetAccount (group, i) == account) {
				found = TRUE;
				if (group == model->priv->root)
					finished = TRUE;
				break;
			}
		}

		if (!found) {
			gtk_tree_path_free (path);
			LEAVE("failed (3)");
			return NULL;
		}

		gtk_tree_path_prepend_index (path, i);

		account = xaccAccountGetParentAccount (account);
		group = xaccAccountGetParent (account);
	} while (!finished);

	if (model->priv->toplevel != NULL) {
		gtk_tree_path_prepend_index (path, 0);
	}

	{
	  gchar *path_string = gtk_tree_path_to_string(path);
	  LEAVE("path (4) %s", path_string);
	  g_free(path_string);
	}
	return path;
}

static void
gnc_tree_model_account_get_value (GtkTreeModel *tree_model,
				  GtkTreeIter *iter,
				  int column,
				  GValue *value)
{
	GncTreeModelAccount *model = GNC_TREE_MODEL_ACCOUNT (tree_model);
	Account *account;
	gboolean negative; /* used to set "defecit style" aka red numbers */
	gchar *string;

	// ENTER("model %p, iter %s, col %d", tree_model,
	//       iter_to_string(iter), column);
	g_return_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (model));
	g_return_if_fail (iter != NULL);
	g_return_if_fail (iter->user_data != NULL);
	g_return_if_fail (iter->stamp == model->stamp);

	account = (Account *) iter->user_data;

	switch (column) {
		case GNC_TREE_MODEL_ACCOUNT_COL_NAME:
			g_value_init (value, G_TYPE_STRING);
			if (account == model->priv->toplevel)
			  g_value_set_string (value, _("New top level account"));
			else
			  g_value_set_string (value, xaccAccountGetName (account));
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_TYPE:
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, 
					    xaccAccountGetTypeStr (xaccAccountGetType (account)));
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_CODE:
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, xaccAccountGetCode (account));
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_COMMODITY:
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value,
					    gnc_commodity_get_fullname(xaccAccountGetCommodity (account)));
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_DESCRIPTION:
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, xaccAccountGetDescription (account));
			break;

		case GNC_TREE_MODEL_ACCOUNT_COL_PRESENT:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetPresentBalanceInCurrency,
								  account, FALSE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_PRESENT_REPORT:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_report_balance(xaccAccountGetPresentBalanceInCurrency,
									 account, FALSE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_PRESENT:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetPresentBalanceInCurrency,
								  account, FALSE, &negative);
			g_value_set_static_string (value, negative ? "red" : "black");
			break;

		case GNC_TREE_MODEL_ACCOUNT_COL_BALANCE:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetBalanceInCurrency,
								  account, FALSE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_BALANCE_REPORT:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_report_balance(xaccAccountGetBalanceInCurrency,
									 account, FALSE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_BALANCE:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetBalanceInCurrency,
								  account, FALSE, &negative);
			g_value_set_static_string (value, negative ? "red" : "black");
			break;

		case GNC_TREE_MODEL_ACCOUNT_COL_CLEARED:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetClearedBalanceInCurrency,
								  account, FALSE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_CLEARED_REPORT:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_report_balance(xaccAccountGetClearedBalanceInCurrency,
									 account, FALSE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_CLEARED:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetClearedBalanceInCurrency,
								  account, FALSE, &negative);
			g_value_set_static_string (value, negative ? "red" : "black");
			break;

		case GNC_TREE_MODEL_ACCOUNT_COL_RECONCILED:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetReconciledBalanceInCurrency,
								  account, FALSE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_RECONCILED_REPORT:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_report_balance(xaccAccountGetReconciledBalanceInCurrency,
									 account, FALSE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_RECONCILED:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetReconciledBalanceInCurrency,
								  account, FALSE, &negative);
			g_value_set_static_string (value, negative ? "red" : "black");
			break;

		case GNC_TREE_MODEL_ACCOUNT_COL_FUTURE_MIN:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetProjectedMinimumBalanceInCurrency,
								  account, FALSE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_FUTURE_MIN_REPORT:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_report_balance(xaccAccountGetProjectedMinimumBalanceInCurrency,
									 account, FALSE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_FUTURE_MIN:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetProjectedMinimumBalanceInCurrency,
								  account, FALSE, &negative);
			g_value_set_static_string (value, negative ? "red" : "black");
			break;

		case GNC_TREE_MODEL_ACCOUNT_COL_TOTAL:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetBalanceInCurrency,
								  account, TRUE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_TOTAL_REPORT:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_report_balance(xaccAccountGetBalanceInCurrency,
									 account, TRUE, &negative);
			g_value_set_string_take_ownership (value, string);
			break;
		case GNC_TREE_MODEL_ACCOUNT_COL_COLOR_TOTAL:
			g_value_init (value, G_TYPE_STRING);
			string = gnc_ui_account_get_print_balance(xaccAccountGetBalanceInCurrency,
								  account, TRUE, &negative);
			g_value_set_static_string (value, negative ? "red" : "black");
			break;

		case GNC_TREE_MODEL_ACCOUNT_COL_NOTES:
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, xaccAccountGetNotes (account));
			break;

		case GNC_TREE_MODEL_ACCOUNT_COL_TAX_INFO:
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, gnc_ui_account_get_tax_info_string (account));
			break;

		case GNC_TREE_MODEL_ACCOUNT_COL_LASTNUM:
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, xaccAccountGetLastNum (account));
			break;

		case GNC_TREE_MODEL_ACCOUNT_COL_PLACEHOLDER:
			g_value_init (value, G_TYPE_BOOLEAN);
			g_value_set_boolean (value, xaccAccountGetPlaceholder (account));
			break;

		case GNC_TREE_MODEL_ACCOUNT_COL_ALIGN_RIGHT:
			g_value_init (value, G_TYPE_FLOAT);
			g_value_set_float (value, 1.0);
			break;

		default:
			g_assert_not_reached ();
	}
	//LEAVE(" ");
}

static gboolean
gnc_tree_model_account_iter_next (GtkTreeModel *tree_model,
				  GtkTreeIter *iter)
{
	GncTreeModelAccount *model = GNC_TREE_MODEL_ACCOUNT (tree_model);
	Account *account;
	AccountGroup *group;
	gint i;

	ENTER("model %p, iter %s", tree_model, iter_to_string(iter));
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (model), FALSE);
	g_return_val_if_fail (iter != NULL, FALSE);
	g_return_val_if_fail (iter->user_data != NULL, FALSE);
	g_return_val_if_fail (iter->stamp == model->stamp, FALSE);

	if (iter->user_data == model->priv->toplevel) {
		iter->stamp = 0;
		LEAVE("failed (1)");
		return FALSE;
	}

	group = (AccountGroup *) iter->user_data2;
	i = GPOINTER_TO_INT (iter->user_data3);

	if (i > xaccGroupGetNumAccounts (group) - 2) {
		iter->stamp = 0;
		LEAVE("failed (2)");
		return FALSE;
	}

	account = xaccGroupGetAccount (group, i + 1);

	if (account == NULL) {
		iter->stamp = 0;
		LEAVE("failed (3)");
		return FALSE;
	}

	iter->user_data = account;
	iter->user_data2 = group;
	iter->user_data3 = GINT_TO_POINTER (i + 1);

	LEAVE("iter %s", iter_to_string(iter));
	return TRUE;
}

static gboolean
gnc_tree_model_account_iter_children (GtkTreeModel *tree_model,
				      GtkTreeIter *iter,
				      GtkTreeIter *parent)
{
	GncTreeModelAccount *model;
	Account *account;
	AccountGroup *group;

	if (parent) {
	  ENTER("model %p, iter %p (to be filed in), parent %s",
		tree_model, iter, iter_to_string(parent));
	} else {
	  ENTER("model %p, iter %p (to be filed in), parent (null)", tree_model, iter);
	}
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (tree_model), FALSE);

	model = GNC_TREE_MODEL_ACCOUNT (tree_model);

	if (model->priv->toplevel != NULL) {
		if (parent == NULL) {
			iter->user_data = model->priv->toplevel;
			iter->user_data2 = NULL;
			iter->user_data3 = GINT_TO_POINTER (0);
			iter->stamp = model->stamp;
			LEAVE("iter (1) %s", iter_to_string(iter));
			return TRUE;
		} else if (parent->user_data == model->priv->toplevel) {
			parent = NULL;
		}
	}

	if (model->priv->root == NULL || xaccGroupGetNumAccounts (model->priv->root) == 0) {
		iter->stamp = 0;
		LEAVE("failed (1)");
		return FALSE;
	}

	if (parent == NULL) {
		account = xaccGroupGetAccount (model->priv->root, 0);
		
		if (account == NULL) {
			iter->stamp = 0;
			LEAVE("failed (2)");
			return FALSE;
		}

		iter->user_data = account;
		iter->user_data2 = model->priv->root;
		iter->user_data3 = GINT_TO_POINTER (0);
		iter->stamp = model->stamp;
		LEAVE("iter (2) %s", iter_to_string(iter));
		return TRUE;	
	}

	g_return_val_if_fail (parent != NULL, FALSE);
	g_return_val_if_fail (parent->user_data != NULL, FALSE);
	g_return_val_if_fail (parent->stamp == model->stamp, FALSE);	

	group = xaccAccountGetChildren ((Account *) parent->user_data);

	if (group == NULL || xaccGroupGetNumAccounts (group) == 0) {
		iter->stamp = 0;
		LEAVE("failed (3)");
		return FALSE;
	}

	account = xaccGroupGetAccount (group, 0);
	
	if (account == NULL) {
		iter->stamp = 0;
		LEAVE("failed (4)");
		return FALSE;
	}

	iter->user_data = account;
	iter->user_data2 = group;
	iter->user_data3 = GINT_TO_POINTER (0);
	iter->stamp = model->stamp;
	LEAVE("iter (3) %s", iter_to_string(iter));
	return TRUE;
}

static gboolean
gnc_tree_model_account_iter_has_child (GtkTreeModel *tree_model,
				       GtkTreeIter *iter)
{
	GncTreeModelAccount *model;
	AccountGroup *group;

	ENTER("model %p, iter %s", tree_model, iter_to_string(iter));
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (tree_model), FALSE);

	model = GNC_TREE_MODEL_ACCOUNT (tree_model);

	g_return_val_if_fail (iter != NULL, FALSE);
	g_return_val_if_fail (iter->user_data != NULL, FALSE);
	g_return_val_if_fail (iter->stamp == model->stamp, FALSE);

	if (iter->user_data == model->priv->toplevel) {
		group = model->priv->root;
	} else {
		group = xaccAccountGetChildren ((Account *) iter->user_data);
	}

	if (group == NULL || xaccGroupGetNumAccounts (group) == 0) {
		LEAVE("no");
		return FALSE;
	}

	LEAVE("yes");
	return TRUE;
}

static int
gnc_tree_model_account_iter_n_children (GtkTreeModel *tree_model,
					GtkTreeIter *iter)
{
	GncTreeModelAccount *model;
	AccountGroup *group;

	ENTER("model %p, iter %s", tree_model, iter_to_string(iter));
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (tree_model), FALSE);

	model = GNC_TREE_MODEL_ACCOUNT (tree_model);

	if (iter == NULL) {
		if (model->priv->toplevel != NULL) {
			LEAVE("count is 1");
			return 1;
		} else {
			LEAVE("count is %d", xaccGroupGetNumAccounts (model->priv->root));
			return xaccGroupGetNumAccounts (model->priv->root);
		}
	}

	g_return_val_if_fail (iter != NULL, FALSE);
	g_return_val_if_fail (iter->user_data != NULL, FALSE);
	g_return_val_if_fail (iter->stamp == model->stamp, FALSE);

	if (model->priv->toplevel == iter->user_data) {
		group = model->priv->root;
	} else {
		group = xaccAccountGetChildren ((Account *) iter->user_data);
	}

	LEAVE("count is %d", xaccGroupGetNumAccounts (group));
	return xaccGroupGetNumAccounts (group);
}

static gboolean
gnc_tree_model_account_iter_nth_child (GtkTreeModel *tree_model,
				       GtkTreeIter *iter,
				       GtkTreeIter *parent,
				       int n)
{
	GncTreeModelAccount *model;
	Account *account;
	AccountGroup *group;

	if (parent) {
	  gchar *parent_string;

	  parent_string = strdup(iter_to_string(parent));
	  ENTER("model %p, iter %s, parent %s, n %d",
		tree_model, iter_to_string(iter),
		parent_string, n);
	  g_free(parent_string);
	} else {
	  ENTER("model %p, iter %s, parent (null), n %d",
		tree_model, iter_to_string(iter), n);
	}
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (tree_model), FALSE);

	model = GNC_TREE_MODEL_ACCOUNT (tree_model);

	if (parent == NULL) {
		if (model->priv->toplevel != NULL) {
			if (n > 0) {
				iter->stamp = 0;
				LEAVE("failed (1)");
				return FALSE;
			} else {
				iter->user_data = model->priv->toplevel;
				iter->user_data2 = NULL;
				iter->user_data3 = GINT_TO_POINTER (0);
				iter->stamp = model->stamp;
				LEAVE("iter (1) %s", iter_to_string(iter));
				return TRUE;
			}
		}

		account = xaccGroupGetAccount (model->priv->root, n);

		if (account == NULL) {
			iter->stamp = 0;
			LEAVE("failed (2)");			
			return FALSE;
		}

		iter->user_data = account;
		iter->user_data2 = model->priv->root;
		iter->user_data3 = GINT_TO_POINTER (n);
		iter->stamp = model->stamp;
		LEAVE("iter (2) %s", iter_to_string(iter));
		return TRUE;
	}

	g_return_val_if_fail (parent->user_data != NULL, FALSE);
	g_return_val_if_fail (parent->stamp == model->stamp, FALSE);

	if (model->priv->toplevel == parent->user_data) {
		group = model->priv->root;
	} else {
		group = xaccAccountGetChildren ((Account *) parent->user_data);
	}

	if (group == NULL || xaccGroupGetNumAccounts (group) <= n) {
		iter->stamp = 0;
		LEAVE("failed (3)");
		return FALSE;
	}

	account = xaccGroupGetAccount (group, n);
	
	if (account == NULL) {
		iter->stamp = 0;
		LEAVE("failed (4)");
		return FALSE;
	}

	iter->user_data = account;
	iter->user_data2 = group;
	iter->user_data3 = GINT_TO_POINTER (n);
	iter->stamp = model->stamp;
	LEAVE("iter (3) %s", iter_to_string(iter));
	return TRUE;
}

static gboolean
gnc_tree_model_account_iter_parent (GtkTreeModel *tree_model,
				    GtkTreeIter *iter,
    				    GtkTreeIter *child)
{
	GncTreeModelAccount *model;
	Account *account;
	AccountGroup *group;
	gint i;

	if (child) {
	  gchar *child_string;

	  child_string = strdup(iter_to_string(child));
	  ENTER("model %p, iter %s, child %s",
		tree_model, iter_to_string(iter),
		child_string);
	  g_free(child_string);
	} else {
	  ENTER("model %p, iter %s, child (null)",
		tree_model, iter_to_string(iter));
	}
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (tree_model), FALSE);

	model = GNC_TREE_MODEL_ACCOUNT (tree_model);

	g_return_val_if_fail (child != NULL, FALSE);
	g_return_val_if_fail (child->user_data != NULL, FALSE);
	g_return_val_if_fail (child->stamp == model->stamp, FALSE);

	account = (Account *) child->user_data;

	if (account == model->priv->toplevel) {
		iter->stamp = 0;
		LEAVE("failed (1)");
		return FALSE;
	}

	account = xaccAccountGetParentAccount (account);
	group = xaccAccountGetParent (account);

	if (account == NULL || group == NULL) {
		if (model->priv->toplevel != NULL) {
			iter->user_data = model->priv->toplevel;
			iter->user_data2 = NULL;
			iter->user_data3 = GINT_TO_POINTER (0);
			iter->stamp = model->stamp;
			LEAVE("iter (1) %s", iter_to_string(iter));
			return TRUE;
		} else {
			iter->stamp = 0;
			LEAVE("failed (2)");
			return FALSE;
		}
	}

	for (i = 0; i < xaccGroupGetNumAccounts (group); i++) {
		if (xaccGroupGetAccount (group, i) == account) {
			iter->user_data = account;
			iter->user_data2 = group;
			iter->user_data3 = GINT_TO_POINTER (i);
			iter->stamp = model->stamp;
			LEAVE("iter (2) %s", iter_to_string(iter));
			return TRUE;	
		}
	}

	if (model->priv->toplevel != NULL) {
		iter->user_data = model->priv->toplevel;
		iter->user_data2 = NULL;
		iter->user_data3 = GINT_TO_POINTER (0);
		iter->stamp = model->stamp;
		LEAVE("iter (3) %s", iter_to_string(iter));
		return TRUE;
	}
	iter->stamp = 0;
	LEAVE("failed (3)");
	return FALSE;
}


/************************************************************/
/*             Account Tree View Root Functions             */
/************************************************************/

static gpointer
account_row_inserted (Account *account,
		      gpointer data)
{
	GtkTreePath *path;
	GtkTreeIter iter;

	ENTER("account %p (%s), model %p",
	      account, xaccAccountGetName(account), data);
	if (!gnc_tree_model_account_get_iter_from_account (GNC_TREE_MODEL_ACCOUNT (data), account, &iter))
	  return NULL;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (data), &iter);

	gtk_tree_model_row_inserted (GTK_TREE_MODEL (data), path, &iter);

	gtk_tree_path_free (path);

	LEAVE(" ");
	return NULL;
}

/*
 * Add a new top node to the model.  This node is for a pseudo-account
 * that lives above the main level accounts in the engine.
 */
Account *
gnc_tree_model_account_get_toplevel (GncTreeModelAccount *model)
{
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (model), NULL);

	return model->priv->toplevel;
}

/*
 * Add a new top node to the model.  This node is for a pseudo-account
 * that lives above the main level accounts in the engine.
 */
static void
gnc_tree_model_account_set_toplevel (GncTreeModelAccount *model,
                                     Account *toplevel)
{
	GtkTreePath *path;
	gint i;
	GtkTreeIter iter;

	ENTER("model %p, toplevel %p", model, toplevel);
	g_return_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (model));

	DEBUG("old toplevel %p", model->priv->toplevel);
	if (model->priv->toplevel != NULL) {
		path = gtk_tree_path_new_first ();
		gtk_tree_model_row_deleted (GTK_TREE_MODEL (model), path);
		gtk_tree_path_free (path);
	} else {
		path = gtk_tree_path_new_first ();
		for (i = 0; i < xaccGroupGetNumAccounts (model->priv->root); i++) {
			gtk_tree_model_row_deleted (GTK_TREE_MODEL (model), path);
		}
		gtk_tree_path_free (path);
	}

	DEBUG("set new toplevel %p", toplevel);
	model->priv->toplevel = toplevel;

	if (model->priv->toplevel != NULL) {
		path = gtk_tree_path_new_first ();
		gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path);
		gtk_tree_model_row_inserted (GTK_TREE_MODEL (model), path, &iter);
		gtk_tree_path_free (path);
	}

	if (model->priv->root != NULL) {
		xaccGroupForEachAccount (model->priv->root, account_row_inserted, model, TRUE);
	}
	LEAVE("new toplevel %p", model->priv->root);
}

/************************************************************/
/*            Account Tree View Filter Functions            */
/************************************************************/

/*
 * Convert a model/iter pair to a gnucash account.  This routine should
 * only be called from an account tree view filter function.
 */
Account *
gnc_tree_model_account_get_account (GncTreeModelAccount *model,
				    GtkTreeIter *iter)
{
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (model), NULL);
	g_return_val_if_fail (iter != NULL, NULL);
	g_return_val_if_fail (iter->user_data != NULL, NULL);
	g_return_val_if_fail (iter->stamp == model->stamp, NULL);

	return (Account *) iter->user_data;
}

/*
 * Convert a model/account pair into a gtk_tree_model_iter.  This
 * routine should only be called from the file
 * gnc-tree-view-account.c.
 */
gboolean
gnc_tree_model_account_get_iter_from_account (GncTreeModelAccount *model,
					      Account *account,
					      GtkTreeIter *iter)
{
	AccountGroup *group;
	gboolean found = FALSE;
	gint i;
	
	ENTER("model %p, account %p, iter %p", model, account, iter);
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (model), FALSE);
	g_return_val_if_fail ((account != NULL), FALSE);
	g_return_val_if_fail ((iter != NULL), FALSE);

	if (model->priv->root != xaccAccountGetRoot (account)) {
		LEAVE("Root doesn't match");
		return FALSE;
	}

	iter->user_data = account;
	iter->stamp = model->stamp;

	if (account == model->priv->toplevel) {
		iter->user_data2 = NULL;
		iter->user_data3 = GINT_TO_POINTER (0);
		LEAVE("Matched top level");
		return TRUE;
	}

	group = xaccAccountGetParent (account);
	DEBUG("Looking through %d accounts at this level", xaccGroupGetNumAccounts (group));
	for (i = 0; i < xaccGroupGetNumAccounts (group); i++) {
		if (xaccGroupGetAccount (group, i) == account) {
			found = TRUE;
			break;
		}
	}

	iter->user_data2 = group;
	iter->user_data3 = GINT_TO_POINTER (i);
	LEAVE("iter %s", iter_to_string(iter));
	return found;
}

/*
 * Convert a model/account pair into a gtk_tree_model_path.  This
 * routine should only be called from the file
 * gnc-tree-view-account.c.
 */
GtkTreePath *
gnc_tree_model_account_get_path_from_account (GncTreeModelAccount *model,
					      Account *account)
{
	GtkTreeIter tree_iter;
	GtkTreePath *tree_path;

	ENTER("model %p, account %p", model, account);
	g_return_val_if_fail (GNC_IS_TREE_MODEL_ACCOUNT (model), NULL);
	g_return_val_if_fail (account != NULL, NULL);

	if (!gnc_tree_model_account_get_iter_from_account (model, account, &tree_iter)) {
	  LEAVE("no iter");
	  return NULL;
	}

	tree_path = gtk_tree_model_get_path (GTK_TREE_MODEL(model), &tree_iter);
	if (tree_path) {
	  gchar *path_string = gtk_tree_path_to_string(tree_path);
	  LEAVE("path (2) %s", path_string);
	  g_free(path_string);
	} else {
	  LEAVE("no path");
	}
	return tree_path;
}

/************************************************************/
/*   Account Tree Model - Engine Event Handling Functions   */
/************************************************************/

typedef struct _remove_data {
  GUID                 guid;
  GncTreeModelAccount *model;
  GtkTreePath         *path;
} remove_data;

static GSList *pending_removals = NULL;

/** This function performs common updating to the model after an
 *  account has been added or removed.  The parent entry needs to be
 *  tapped on the shoulder so that it can correctly update the
 *  disclosure triangle (first added child/last removed child) or
 *  possibly rebuild its child list of that level of accounts is
 *  visible.
 *
 *  @internal
 *
 *  @param model The account tree model containing the account that
 *  has been added or deleted.
 *
 *  @param path The path to the newly added item, or the just removed
 *  item.
 */
static void
gnc_tree_model_account_path_changed (GncTreeModelAccount *model,
				     GtkTreePath *path)
{
  GtkTreeIter iter;

  if (gtk_tree_path_up (path)) {
    gtk_tree_model_get_iter (GTK_TREE_MODEL(model), &iter, path);
    gtk_tree_model_row_has_child_toggled (GTK_TREE_MODEL(model), path, &iter);
  }

  do {
    model->stamp++;
  } while (model->stamp == 0);
}


/** This function is a helper routine for the following
 *  gnc_tree_model_account_event_handler() function.  It is called
 *  from an iterator over all the pending account removals, and is
 *  responsible for selecting the right item(s), deleting it from the
 *  pending list, and then sending the "row_deleted" signal to any/all
 *  parent models.
 *
 *  @internal
 *
 *  @param data An item in the pending deletion list.
 *
 *  @param entity The guid value of the destroyed item.
 */
static void
gnc_tree_model_account_delete_event_helper (remove_data *data,
					    GUID *entity)
{
  if (!guid_equal(&data->guid, entity))
    return;

  pending_removals = g_slist_remove (pending_removals, data);

  gtk_tree_model_row_deleted (GTK_TREE_MODEL(data->model), data->path);
  gnc_tree_model_account_path_changed (data->model, data->path);
  gtk_tree_path_free(data->path);
  g_free(data);
}


/** This function is the handler for all event messages from the
 *  engine.  Its purpose is to update the account tree model any time
 *  an account is added to the engine or deleted from the engine.
 *  This change to the model is then propagated to any/all overlying
 *  filters and views.  This function listens to the ADD, REMOVE, and
 *  DESTROY events.
 *
 *  @internal
 *
 *  @warning There is a "Catch 22" situation here.
 *  gtk_tree_model_row_deleted() can't be called until after the item
 *  has been deleted from the real model (which is the engine's
 *  account tree for us), but once the account has been deleted from
 *  the engine we have no way to determine the path to pass to
 *  row_deleted().  This is a PITA, but the only ither choice is to
 *  have this model mirror the engine's accounts instead of
 *  referencing them directly.
 *
 *  @param entity The guid of the affected item.
 *
 *  @param type The type of the affected item.  This function only
 *  cares about items of type "account".
 *
 *  @param event type The type of the event. This function only cares
 *  about items of type ADD, REMOVE, and DESTROY.
 *
 *  @param user_data A pointer to the account tree model.
 */
static void
gnc_tree_model_account_event_handler (GUID *entity, QofIdType type,
				      GNCEngineEventType event_type,
				      gpointer user_data)
{
  	GncTreeModelAccount *model;
	GtkTreePath *path;
	GtkTreeIter iter;
	Account *account;
	remove_data *data;
	const gchar *account_name;

	/* hard failures */
	g_return_if_fail(GNC_IS_TREE_MODEL_ACCOUNT(user_data));

	/* soft failures */
	if (safe_strcmp(type, GNC_ID_ACCOUNT) != 0)
	  return;

	ENTER("entity %p of type %s, event %d, model %p",
	      entity, type, event_type, user_data);
	model = (GncTreeModelAccount *)user_data;

	/* Get the account.*/
	/* DRH - Put the book in the model private data so this code
	 * supports multiple simultaneous books. */
	account = xaccAccountLookup (entity, gnc_get_current_book ());
	account_name = xaccAccountGetName(account);

	switch (event_type) {
	 case GNC_EVENT_ADD:
	  /* Tell the filters/views where the new account was added. */
	  DEBUG("add account %p (%s)", account, account_name);
	  if (gnc_tree_model_account_get_iter_from_account (model, account, &iter)) {
	    path = gtk_tree_model_get_path (GTK_TREE_MODEL(model), &iter);
	    gtk_tree_model_row_inserted (GTK_TREE_MODEL(model), path, &iter);
	    gnc_tree_model_account_path_changed (model, path);
	    gtk_tree_path_free(path);
	  }
	  break;

	 case GNC_EVENT_REMOVE:
	  /* Record the path of this account for later use in destruction */
	  DEBUG("remove account %p (%s)", account, account_name);
	  path = gnc_tree_model_account_get_path_from_account (model, account);
	  if (path == NULL) {
	    LEAVE("account not in model");
	    return;
	  }

	  data = malloc(sizeof(*data));
	  data->guid = *entity;
	  data->model = model;
	  data->path = path;
	  pending_removals = g_slist_append (pending_removals, data);
	  LEAVE(" ");
	  return;

	 case GNC_EVENT_MODIFY:
	  DEBUG("change account %p (%s)", account, account_name);
	  path = gnc_tree_model_account_get_path_from_account (model, account);
	  if (path == NULL) {
	    LEAVE("account not in model");
	    return;
	  }
	  if (!gtk_tree_model_get_iter (GTK_TREE_MODEL(model), &iter, path)) {
	    gtk_tree_path_free(path);
	    LEAVE("can't find iter for path");
	    return;
	  }
	  gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, &iter);
	  gtk_tree_path_free(path);
	  LEAVE(" ");
	  return;

	 case GNC_EVENT_DESTROY:
	  /* Tell the filters/view the account has been deleted. */
	  DEBUG("destroy account %p (%s)", account, account_name);
	  g_slist_foreach (pending_removals,
			   (GFunc)gnc_tree_model_account_delete_event_helper,
			   entity);
	  break;

	 default:
	  LEAVE("ignored event for %p (%s)", account, account_name);
	  return;
	}
	LEAVE(" new stamp %u", model->stamp);
}
