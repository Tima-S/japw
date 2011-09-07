/* japw
 *
 * Copyright (C) 2009 - 2011 Timur R. Salikhov <tima-s@ya.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <string.h>

#include "jhelp.h"

#define COPYR "Copyright (C) 2009 - 2011 Timur R. Salikhov <tima-s@ya.ru>."
#define PVER "Just another picture viewer (Japw). Version 1.1."

gint RIGHT = 65363;  // right key
gint LEFT = 65361;   // left key
gint ESC = 65307;    // escape key
gint MINUS = 45;     // - key
gint PLUS = 43;      // + key
gint PGUP = 65365;   // PgUp key
gint PGDOWN = 65366; // PgDown key
gint F11 = 65480;    // F11 key
gint FIT = 48;       // fit key (0) 
gint F3 = 65472;		// F3 key, show ilist

#define MOUSE_LEFT_BUTTON 1
#define MOUSE_RIGHT_BUTTON 3
#define MOUSE_MIDDLE_BUTTON 2

gchar *keyfile;  //file with settings

#define KFILE "/.config/japw/japw.conf"
#define KEYGROUP "keys" //group of keys in file

#define SOCK_PAUSE 250000

GtkWidget *mainwin,*canvas,*scrwin,*scrwin1,*ilist, *progress;

gint current_image = 0;
gint max_count_image = 0;
gchar **fnames = NULL;

gfloat img_width;  // natural 
gfloat img_height;

gfloat pxb_width;  // temporary
gfloat pxb_height;

gint window_width = 800;
gint window_height = 600;

GdkPixbuf *img,*pxb;

GdkPixbufAnimation *anim;

gboolean WinFullscrened = FALSE;
gboolean AutoFit = FALSE;

guint fit_desc; //descriptor of idle func
guint s_watch;

GtkListStore *list_store;
gboolean iList_Showed = FALSE;
gboolean iLoad = FALSE;   //need for idle func
gboolean iListLoaded = FALSE; 
gboolean Gallery = FALSE; //command-line key -g (ilist show)
gboolean Windowed = FALSE; //key -w

guint id = 0; //descriptor for idle_ilist_load
gint ilist_counter = 0; // image for idle load

gint s_id; 					//socket
GIOChannel *channel;
gchar *sock_name;		//socket name
gint max_count_image1 = 0; 	// for socket
gchar **fnames1 = NULL;		// for socket 

gint fl_id; //descriptor for idle_create_listf

gchar **idl_flist;
gint idl_cflist;
gint idl_current_file;
gint li_id; // probe load image
gchar **tmp_fns = NULL;

// void file_list_load (gchar* filename);
void idle_ilist_load ();


void on_quit () {
	if (Windowed == FALSE) {
		g_source_remove (s_watch);
		g_io_channel_shutdown (channel, FALSE, NULL);
		close (s_id);
		(void) unlink (sock_name);
	}
	gtk_main_quit ();
} 


void pxb_scale(){
	pxb = gdk_pixbuf_scale_simple(img,
									pxb_width,
									pxb_height,
									GDK_INTERP_NEAREST);

	if (pxb) {
		gtk_image_set_from_pixbuf(GTK_IMAGE (canvas), pxb);
		g_object_unref(pxb);
		pxb = NULL;
	}
}

void fit_to_window(gboolean to_originaly) {
	gfloat ar; //aspect ratio
	
	gtk_window_get_size (GTK_WINDOW (mainwin),
								 &window_width,
								 &window_height);								 

	if (img_width > img_height) {
		ar = img_width / (window_width-15);
		pxb_width = img_width / ar;
		pxb_height = img_height / ar;
		if  (pxb_height > window_height-15) {
			ar = pxb_height / (window_height-15);
			pxb_width = pxb_width / ar;
			pxb_height = pxb_height / ar;
		}
	}
	else {
		ar = img_height / (window_height-15);
		pxb_width = img_width / ar;
		pxb_height = img_height / ar;
		if  (pxb_width > window_width-15) {
			ar = pxb_width / (window_width-15);
			pxb_width = pxb_width / ar;
			pxb_height = pxb_height / ar;
		}
	}
		
		
	if (to_originaly) if ((window_width > img_width) && (window_height > img_height)) {
		pxb_width = img_width;
		pxb_height = img_height;
	}
	pxb_scale();
}

gboolean idle_set_icon_window () {
	GdkPixbuf *img1;
	img1 = gdk_pixbuf_new_from_file_at_scale (fnames[current_image],128,128,TRUE,NULL);
	gtk_window_set_icon(GTK_WINDOW (mainwin),img1);
	g_object_unref(img1);
	gtk_window_set_title(GTK_WINDOW (mainwin),fnames[current_image]);

	return FALSE; //need to remove idle function.
}

void load_image() {
	if (img) {
		g_object_unref(img);
		img = NULL;
	}
// detect type of file
	GdkPixbufFormat *format;
	gchar **fext;
	format = gdk_pixbuf_get_file_info(fnames[current_image], NULL,NULL);
	fext = gdk_pixbuf_format_get_extensions(format);
	if (fext == NULL) return;
//----end of detect
	
	if (g_ascii_strcasecmp(fext[0],"gif")==0) {
		anim = gdk_pixbuf_animation_new_from_file(fnames[current_image],NULL);
		gtk_image_set_from_animation(GTK_IMAGE (canvas),anim);
		g_object_unref(anim);
	} 
	else {
		
		GdkPixbuf *img1;
		img1 = gdk_pixbuf_new_from_file(fnames[current_image],NULL);

// Rotate according to orientation
		img = gdk_pixbuf_apply_embedded_orientation (img1);
		g_object_unref(img1);
		
		img_width = gdk_pixbuf_get_width(img); 
		img_height = gdk_pixbuf_get_height(img);
	
		pxb_width = img_width;
		pxb_height = img_height;
		
		gtk_window_get_size (GTK_WINDOW (mainwin),
									 &window_width,
									 &window_height);								 

		fit_to_window(TRUE);
//---- end of code for orientation
		
//--- setting icon
		gint siw_id;
		siw_id = g_idle_add((GSourceFunc)idle_set_icon_window,NULL);
	
	}
	g_strfreev(fext);
}

void pxb_rotate(GdkPixbufRotation a) {
	pxb = gdk_pixbuf_rotate_simple(img,a);
	if (pxb) {
		g_object_unref(img);
		img = pxb;
		g_object_ref(img);

		img_width = gdk_pixbuf_get_width(img); //originaly
		img_height = gdk_pixbuf_get_height(img);

		pxb_width = img_width; //temporary (for scale)
		pxb_height = img_height;
		
		gtk_image_set_from_pixbuf(GTK_IMAGE (canvas), pxb);
		g_object_unref(pxb);
		pxb = NULL;
		if (AutoFit) fit_to_window (TRUE);
	}
}

void keypress1(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	guint n;
	n = event->keyval;
	GtkTreePath *path = NULL;
	if (gtk_icon_view_get_cursor (GTK_ICON_VIEW (ilist), &path, NULL)) {
		gtk_icon_view_scroll_to_path (GTK_ICON_VIEW (ilist),path,FALSE,0.5,0.5);
		gtk_tree_path_free (path);
	}
	
	if (n==F11) {
		if (WinFullscrened) { 
			gtk_window_unfullscreen(GTK_WINDOW (mainwin));
			WinFullscrened = FALSE;
		}
		else { 
			gtk_window_fullscreen(GTK_WINDOW (mainwin));
			WinFullscrened = TRUE;
		}
	}
	if ((n==F3 || n==ESC) && Gallery) {
		gtk_widget_hide(scrwin1);
		gtk_widget_show(scrwin);
		gtk_widget_grab_focus (scrwin);
		iList_Showed = FALSE;
	}
	
}

void keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	guint n;
	n = event->keyval;
//	g_printf ("%d\n",n);
	if (n==RIGHT) if (current_image < max_count_image-1) {
		current_image++;
		load_image();
	}
	if (n==LEFT) if (current_image > 0){
		current_image--;
		load_image();
	}
			
	if (n==MINUS) if (img){
		if ((pxb_width > 200) && (pxb_height > 200)) {
			pxb_width = pxb_width / 1.5;
			pxb_height = pxb_height / 1.5;
			pxb_scale();
		}
	}

	if ((n==PLUS) && (img)){
		if ((pxb_width < 5000) && (pxb_height < 5000)) {
			pxb_width = pxb_width * 1.5;
			pxb_height = pxb_height * 1.5;
			pxb_scale();
		}
	}
	
	if ((n==PGDOWN) && (img)) pxb_rotate(GDK_PIXBUF_ROTATE_CLOCKWISE);
	if ((n==PGUP) && (img)) pxb_rotate(GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);

	if (n==F11) {
		if (WinFullscrened) { 
			gtk_window_unfullscreen(GTK_WINDOW (mainwin));
			WinFullscrened = FALSE;
		}
		else { 
			gtk_window_fullscreen(GTK_WINDOW (mainwin));
			WinFullscrened = TRUE;
		}
	}
		
	if (n==FIT) fit_to_window(FALSE);

	if (n==ESC) on_quit();

	if (n==F3) {
		 if (iList_Showed == FALSE && Gallery) {
			 if (iListLoaded) {
					GtkTreePath *path;
					path = gtk_tree_path_new_from_indices (current_image,-1);
					gtk_icon_view_select_path (GTK_ICON_VIEW (ilist), path);
					gtk_icon_view_set_cursor (GTK_ICON_VIEW (ilist), path, NULL, FALSE);
					gtk_icon_view_scroll_to_path (GTK_ICON_VIEW (ilist),path,TRUE,0.5,0.5);
					gtk_tree_path_free (path);
				}
				gtk_widget_hide(scrwin);
				gtk_widget_show(scrwin1);
				gtk_widget_grab_focus (ilist);
				iList_Showed = TRUE;
			}
			else {
				gtk_widget_hide(scrwin1);
				gtk_widget_show(scrwin);
				gtk_widget_grab_focus (scrwin);

				iList_Showed = FALSE;
			}
	}
	
}

void parse_and_apply_cmd_option (gchar *cl_opt, gchar *cl_opt1) {
	if (g_strcmp0 (cl_opt,"-v") == 0) {
		g_printf ("%s\n%s\n\n",COPYR,PVER);
		exit(EXIT_SUCCESS);
	}
	if (g_strcmp0 (cl_opt,"-f") == 0) 
		WinFullscrened = TRUE;
		
	if  (g_strcmp0 (cl_opt,"-a") == 0) 
		AutoFit = TRUE;

	if  (g_strcmp0 (cl_opt,"-h") == 0) {
		g_printf ("%s", JHELP); 
		exit(EXIT_SUCCESS);
	}
/*	if  (g_strcmp0 (cl_opt,"-l") == 0) 
		if (cl_opt1) file_list_load(cl_opt1);
*/
	if  (g_strcmp0 (cl_opt,"-g") == 0) 
		Gallery = TRUE; 

	if  (g_strcmp0 (cl_opt,"-w") == 0) 
		Windowed = TRUE;
} 

void idle_create_listf () {
	GdkPixbufFormat *fmt;
	gchar *current_file = NULL;
	
	if (idl_flist[idl_current_file]) {
		if (g_path_is_absolute (idl_flist[idl_current_file]))
			current_file = g_strdup (idl_flist[idl_current_file]);
		else {
			gchar *current_dir;
			current_dir = g_get_current_dir ();
			current_file = g_strconcat (current_dir, "/", idl_flist[idl_current_file], NULL);
//			g_printf ("cur-file: %s\n",current_file);
			g_free (current_dir);
		}

		fmt = gdk_pixbuf_get_file_info(current_file,NULL,NULL);
		if (fmt) {
			
			if (fnames) {
				gchar *tmpStrFnames = g_strjoinv ("|", fnames);
				gchar *tmpStrFnamesConcat = g_strconcat(tmpStrFnames, "|",current_file, NULL);
				g_strfreev(fnames);
				fnames = g_strsplit(tmpStrFnamesConcat, "|", -1);
				g_free(tmpStrFnames);
				g_free(tmpStrFnamesConcat);
			}
			else {
				fnames = g_malloc(sizeof(gchar**) *2); 	//last member is NULL
				fnames[0] = g_strdup(current_file); 	//copy string need for drag-n-drop
				fnames[1] = NULL;			//need for g_strfreev
			}
		}
	}
	++idl_current_file;
	g_free (current_file);
	if (idl_cflist > 0) 
		gtk_progress_bar_set_fraction ( GTK_PROGRESS_BAR (progress), (gfloat) idl_current_file / idl_cflist);
	if (idl_cflist <= idl_current_file) {
		gtk_widget_hide(progress);

		//if (max_count_image > 0) fnames[max_count_image] = NULL;
		if (tmp_fns) g_strfreev (tmp_fns); 

		if (fl_id != 0) {
			g_source_remove (fl_id);
			fl_id = 0;
		}
		if (fnames1) {
			gchar *str, *fstr, *fstr1;
			if (max_count_image > 0) {
				fstr = g_strjoinv ("|", fnames);
				str = g_strjoinv ("|", fnames1);
				fstr1 = g_strconcat (fstr,"|", str, NULL);
				g_strfreev (fnames);
				fnames = g_strsplit (fstr1, "|",-1);
				g_free (str);
				g_free (fstr);
				g_free (fstr1);
				g_strfreev (fnames1);
				
			} else {
				fnames = g_strdupv (fnames1);
				g_strfreev (fnames1);
			}
			fnames1 = NULL;

		}
		if (fnames)
			max_count_image = g_strv_length (fnames);
		else
			max_count_image = 0;
						
		if (Gallery) {
			iLoad = TRUE;
			id = g_idle_add((GSourceFunc)idle_ilist_load,NULL);
		}

	}
}

void search_for_commandline_options (gint fcount, gchar **flist) {
	gint n;
	for (n = 0; n < fcount; n++) { 
		if (n < fcount) parse_and_apply_cmd_option (flist[n],flist[n+1]);
			else parse_and_apply_cmd_option (flist[n],NULL);
	}
} 

void create_listf(gint fcount, gchar **flist) {
	current_image = 0;
	if (fnames) g_strfreev(fnames);
	fnames = g_malloc(sizeof(flist));
	gint m = 0; //this is the temporary iterator for new list of files
	GdkPixbufFormat *fmt;
	gchar *current_file = NULL;
	gint n;
	for (n = 0; n < fcount; ++n) {
		if (flist[n]) {
			if (g_path_is_absolute (flist[n])) {
				current_file = g_strdup (flist[n]);
//				g_printf ("cur-file1: %s\n",current_file);
			} else {
				gchar *current_dir;
				current_dir = g_get_current_dir ();
				current_file = g_strconcat (current_dir, "/", flist[n], NULL);
//				g_printf ("cur-file2: %s\n",current_file);
				g_free (current_dir);
			}

			fmt = gdk_pixbuf_get_file_info(current_file,NULL,NULL);
			if (fmt) {
				fnames = g_realloc (fnames, sizeof(flist) * (m+2));
				fnames[m] = g_strdup(current_file); //need for drag-n-drop
				m++;
			}
			if (current_file) g_free (current_file);
		}
	}
	if (m > 0) fnames[m] = NULL; //need for g_strfreev
	if (tmp_fns) g_strfreev (tmp_fns); 
	max_count_image = m;

	if (Gallery) {
		iLoad = TRUE;
		id = g_idle_add((GSourceFunc)idle_ilist_load,NULL);
	}
}

void idle_ilist_load () {
	if (iLoad == FALSE) return; 
	iLoad = FALSE;
	GtkTreeIter iter;
	GdkPixbuf *pixbuf;
	if (ilist_counter < max_count_image) { 
		pixbuf = gdk_pixbuf_new_from_file_at_scale (fnames[ilist_counter],96,96,TRUE,NULL);
		++ilist_counter;
		gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter, 0, pixbuf, -1);
		g_object_unref(pixbuf);
		iLoad = TRUE;
	}
	else {
//		gtk_icon_view_set_model (GTK_ICON_VIEW (ilist), GTK_TREE_MODEL (list_store));
//		gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (ilist), 0);
		if (id != 0) {
			g_source_remove (id);
			id = 0;
			iListLoaded = TRUE;
		}
	}
}

gboolean idle_test_fnames () {	
	if (max_count_image >= 1) {
		load_image ();
		return FALSE;
	}
	else return TRUE;
}

void prepare_for_idle_create_listf (gint fcount, gchar **flist) {
//--- code for gallery
		if (id != 0 && Gallery) {
			g_source_remove (id);
			id = 0;
		}
		gtk_list_store_clear (list_store);
		iListLoaded = FALSE;
		ilist_counter = 0;
//--- end code for gallery

	if (fcount > 0) { 
		max_count_image = 0;
		current_image = 0;
		idl_cflist = fcount;
		idl_current_file = 0;
		idl_flist = flist;
		if (fnames) {
			g_strfreev (fnames);
			fnames = NULL;
		}


		// and before call idle fuctions, tmp_fns (tmp_fns = flist) must be set for free.
		// tmp_fns no need with argv and must be NULL.
		gtk_widget_show(progress);

		fl_id = g_idle_add((GSourceFunc)idle_create_listf,NULL);
		li_id = g_idle_add((GSourceFunc)idle_test_fnames,NULL);
	}
}



/*
void file_list_load (gchar* filename) {
	GIOChannel *fchannel;
	fchannel = g_io_channel_new_file (filename, "r", NULL);
	if (fchannel == NULL) return;
	gchar *s, **buf, *s1;
	GIOStatus fch_status;
	buf = g_malloc (sizeof(gchar*));
	*buf = NULL;
	fch_status = g_io_channel_read_line (fchannel, buf, NULL, NULL,NULL);
	s = *buf;

	gsize z;
	while (fch_status == G_IO_STATUS_NORMAL) {
		fch_status = g_io_channel_read_line (fchannel, buf, &z, NULL,NULL);
		if (z >3) {
			s1 = s;
			s = g_strconcat (s1, *buf, NULL);
			g_free(s1);
			g_free(*buf);
		}
	};
	g_free(buf);
	g_io_channel_close (fchannel);
//	g_printf("%s\n",s);

	gint l = g_utf8_strlen (s,-1);
	--l;
	s1 = g_malloc (sizeof (gchar) * l);
	g_utf8_strncpy (s1, s, l);
	
	buf = g_strsplit (s1,"\n",-1);
	

//--- code for gallery
		if (id != 0 && Gallery) {
			g_source_remove (id);
			id = 0;
		}
		gtk_list_store_clear (list_store);
		iListLoaded = FALSE;
		ilist_counter = 0;
//--- end code for gallery

		max_count_image = 0;
		current_image = 0;
		
		idl_cflist = g_strv_length(buf);
		
		g_printf ("files: %d\n", idl_cflist);
		idl_current_file = 0;
		idl_flist = buf;
		tmp_fns = buf;
//		if (fnames) g_strfreev (fnames);
		fnames = g_malloc (sizeof (gchar*));
		fl_id = g_idle_add((GSourceFunc)idle_create_listf,NULL);
		li_id = g_idle_add((GSourceFunc)idle_test_fnames,NULL);

}
*/

void get_psettings () {
	GKeyFile *kf;
	kf = g_key_file_new();
	FILE *f;
	if (g_key_file_load_from_file(kf,keyfile,G_KEY_FILE_NONE,NULL)) {
		RIGHT = g_key_file_get_integer (kf,KEYGROUP,"RIGHT",NULL);
		LEFT = g_key_file_get_integer (kf,KEYGROUP,"LEFT",NULL);
		ESC = g_key_file_get_integer (kf,KEYGROUP,"ESC",NULL);
		MINUS = g_key_file_get_integer (kf,KEYGROUP,"MINUS",NULL);
		PLUS = g_key_file_get_integer (kf,KEYGROUP,"PLUS",NULL);
		PGUP = g_key_file_get_integer (kf,KEYGROUP,"PGUP",NULL);
		PGDOWN = g_key_file_get_integer (kf,KEYGROUP,"PGDOWN",NULL);
		F11 = g_key_file_get_integer (kf,KEYGROUP,"F11",NULL);
		FIT = g_key_file_get_integer (kf,KEYGROUP,"FIT",NULL);
	}
	else {
		if (!g_file_test(g_path_get_dirname(keyfile), G_FILE_TEST_EXISTS))
			g_mkdir(g_path_get_dirname(keyfile), 0700);

		f = g_fopen (keyfile, "w");
		if (f) { 
			g_key_file_set_integer (kf,KEYGROUP,"RIGHT",RIGHT);
			g_key_file_set_integer (kf,KEYGROUP,"LEFT",LEFT);
			g_key_file_set_integer (kf,KEYGROUP,"ESC",ESC);
			g_key_file_set_integer (kf,KEYGROUP,"MINUS",MINUS);
			g_key_file_set_integer (kf,KEYGROUP,"PLUS",PLUS);
			g_key_file_set_integer (kf,KEYGROUP,"PGUP",PGUP);
			g_key_file_set_integer (kf,KEYGROUP,"PGDOWN",PGDOWN);
			g_key_file_set_integer (kf,KEYGROUP,"F11",F11);
			g_key_file_set_integer (kf,KEYGROUP,"FIT",FIT);
			gchar *wkf;
			wkf = g_key_file_to_data (kf,NULL,NULL);
			g_fprintf (f,"%s",wkf);
			fclose(f);
			g_free(wkf);
		}
	}
	g_key_file_free (kf);
}

void get_conf_file_path () {
	const gchar *home_path;
	home_path = g_getenv("HOME");  //this work only in utf8 locale
								   //may be need g_locale_to_utf8
	keyfile = g_strconcat (home_path,KFILE,NULL);
}

void on_window_resize (GtkWidget *w, GtkAllocation *al) {
	if (AutoFit) if (img) 
		if ((window_width != al->width) || (window_height != al->height))
				fit_to_window (TRUE);
}

void on_data_draged (GtkWidget          *widget,
                     GdkDragContext     *drag_context,
                     gint                x,
                     gint                y,
                     GtkSelectionData   *data,
                     guint               info,
                     guint               time) {
	if ( /* (data->type == GDK_SELECTION_TYPE_STRING) && */
			 (data->length >= 0) && (data->format == 8)) {

		drag_context->action = GDK_ACTION_COPY;
		gtk_drag_finish (drag_context, TRUE, FALSE, time);

		gchar **fns;
		fns = gtk_selection_data_get_uris(data);
		
		gint n = 0;
		gchar *tmps;
		while (fns[n] != NULL) {
			tmps = g_filename_from_uri(fns[n],NULL,NULL);
			if (tmps != NULL) {
				g_free(fns[n]);
				fns[n] = tmps;	
			}
			n++;
		}

		gint len = g_strv_length(fns);
		tmp_fns = fns;
		prepare_for_idle_create_listf (len, fns);
		if (iList_Showed == TRUE) {
			gtk_widget_hide(scrwin1);
			gtk_widget_show(scrwin);
			gtk_widget_grab_focus (scrwin);
			iList_Showed = FALSE;	
		}
	 }
	else
		gtk_drag_finish (drag_context, FALSE, FALSE, time);
}

gboolean on_mouse_button_press (GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	if (event->state == GDK_CONTROL_MASK) {
		if (event->button == MOUSE_LEFT_BUTTON) if (current_image < max_count_image-1){
			current_image++;
			load_image();
		}
		if (event->button == MOUSE_RIGHT_BUTTON) if (current_image > 0){
			current_image--;
			load_image();
		}
		if (event->button == MOUSE_MIDDLE_BUTTON) fit_to_window(FALSE);
	}
	return TRUE;
}

gboolean on_mouse_scroll (GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
	gtk_widget_grab_focus (scrwin);

	if (event->state == GDK_CONTROL_MASK) {
//		g_printf ("scroll");	
		if (event->direction == GDK_SCROLL_DOWN) if (img){
			if ((pxb_width > 200) && (pxb_height > 200)) {
				pxb_width = pxb_width / 1.5;
				pxb_height = pxb_height / 1.5;
				pxb_scale();
			}
		}
	
		if ((event->direction == GDK_SCROLL_UP) && (img)){
			if ((pxb_width < 5000) && (pxb_height < 5000)) {
				pxb_width = pxb_width * 1.5;
				pxb_height = pxb_height * 1.5;
				pxb_scale();
			}
		}
		return TRUE;
	}
	return FALSE;
}

void on_ilist_activated (GtkIconView *iconview, GtkTreePath *path, gpointer user_data) {
	current_image = gtk_tree_path_get_indices (path) [0];
	load_image ();
	gtk_widget_hide(scrwin1);
	gtk_widget_show(scrwin);
	iList_Showed = FALSE;	
	gtk_widget_grab_focus (scrwin);
}

void accept_files () { 			// accept filenames from the socket
	//g_source_remove (s_watch);
	gint s_id1;
	GIOChannel *gch;
	s_id1 = accept (s_id, NULL, 0);
	(void) usleep (SOCK_PAUSE);
	gch = g_io_channel_unix_new (s_id1);
	gchar *str;
	gsize size;
	g_io_channel_read_to_end (gch, &str, &size, NULL);
	if (size > 0) {	
		gchar *fstr, *fstr1;
		if (max_count_image1 > 0) {
			fstr = g_strjoinv ("|", fnames1);
			fstr1 = g_strconcat (fstr,"|", str, NULL);
			g_free (fstr);
			g_strfreev (fnames1);
//			g_printf ("\n%s\n",fstr1);

			fnames1 = g_strsplit (fstr1, "|",-1);
			max_count_image1 = g_strv_length (fnames1);
			g_free (fstr1);
		}
		else {
			fnames1 = g_strsplit (str, "|",-1);
			max_count_image1 = g_strv_length (fnames1);
		}
//		g_printf ("%s\n",str);
		
		g_free (str);
		
		if (fl_id == 0) {
			if (max_count_image > 0) {
				fstr = g_strjoinv ("|", fnames);
				str = g_strjoinv ("|", fnames1);
//				g_printf ("%s\n",str);
				fstr1 = g_strconcat (fstr,"|", str, NULL);
//				g_printf ("%s\n",fstr1);
				g_strfreev (fnames);
				fnames = g_strsplit (fstr1, "|",-1);
				max_count_image = g_strv_length (fnames);
				g_free (str);
				g_free (fstr);
				g_free (fstr1);
				g_strfreev (fnames1);
				max_count_image1 = 0;
			} else {
				fnames = g_strdupv (fnames1);
				g_strfreev (fnames1);
				max_count_image1 = 0;
				max_count_image = g_strv_length (fnames);
				current_image = 0;
				load_image ();
			}			
			fnames1 = NULL;
			//g_printf ("SOCK: Alloc memory for %d file(s)\n",max_count_image);
			if (id == 0 && Gallery) {
				iLoad = TRUE;
				iListLoaded = FALSE;
				id = g_idle_add((GSourceFunc)idle_ilist_load,NULL);
			}
		}
	}

	g_io_channel_shutdown (gch, FALSE, NULL);
	close (s_id1);	
	max_count_image1 = 0;
	//s_watch = g_io_add_watch (channel, G_IO_IN, (GIOFunc) accept_files, NULL);			
	
}

void sock_japw (gint fcount, gchar **flist) {				// create socket or connet to
	const gchar *tdir = g_get_tmp_dir ();			//tmp dir
	const gchar *uname = g_get_user_name ();		//user name

	sock_name = g_strconcat (tdir,"/japw-",uname,NULL);

	s_id = socket (AF_UNIX,SOCK_STREAM,0);

	gint s_status;
	struct sockaddr_un addr;
	g_stpcpy (addr.sun_path, sock_name);
	addr.sun_family = AF_UNIX;

	s_status = bind (s_id,(struct sockaddr *) &addr, sizeof(addr));
	gint s_c_status;
	if (s_status == -1) {
		gint i = 0;
		do {
			(void) usleep(SOCK_PAUSE);
			s_c_status = connect (s_id,(struct sockaddr *) &addr, sizeof(addr));
			++i;
			
		} while (s_c_status == -1 && i < 1500);
		if (s_c_status != -1) {
			create_listf (fcount, flist);
			if (max_count_image == 0) exit (EXIT_SUCCESS); 
			gchar *buf;
			buf = g_strjoinv ("|", fnames);
//			g_printf ("\n%s\n",buf);
			channel = g_io_channel_unix_new (s_id);
//			g_io_channel_set_close_on_unref (channel, TRUE);
			gsize bw,lw;
			lw = strlen (buf) +1;
			g_io_channel_write (channel, buf, lw, &bw);
//			g_printf ("\n bw= %d\n",bw);
//			g_io_channel_unref (channel);
			g_io_channel_shutdown (channel, TRUE, NULL);
			close (s_id);
			g_free (buf);
			exit (EXIT_SUCCESS);
		}
		else {
			(void) unlink (sock_name);
			s_status = bind (s_id,(struct sockaddr *) &addr, sizeof(addr));
			if (s_status == -1) {
				g_printf ("Error: create socket");
				exit (EXIT_FAILURE);
			}
		}
	}
	listen (s_id, 5);
	channel = g_io_channel_unix_new (s_id);
//	g_io_channel_set_close_on_unref (channel, TRUE);

	s_watch = g_io_add_watch_full (channel, G_PRIORITY_LOW, G_IO_IN, (GIOFunc) accept_files, NULL, NULL);
	prepare_for_idle_create_listf (fcount, flist);
}

int main (gint argc, gchar **argv) {
	gtk_init (NULL, NULL);
	GtkWidget *vbox, *align;	
	
	mainwin = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW (mainwin),
											window_width,
											window_height);
										
	gtk_window_set_title(GTK_WINDOW (mainwin),"japw");
	
	vbox = gtk_vbox_new(FALSE, 1);
	align = gtk_alignment_new (0,0,1,0);
	progress = gtk_progress_bar_new ();
	
	scrwin = gtk_scrolled_window_new(NULL,NULL);
	scrwin1 = gtk_scrolled_window_new(NULL,NULL);
	
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scrwin),
											GTK_POLICY_AUTOMATIC,
											GTK_POLICY_AUTOMATIC);
	
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scrwin1),
											GTK_POLICY_AUTOMATIC,
											GTK_POLICY_AUTOMATIC);


	gtk_container_add(GTK_CONTAINER (mainwin), vbox);
	gtk_container_add(GTK_CONTAINER (vbox), align);
	gtk_container_add(GTK_CONTAINER (align), progress);
	
	gtk_container_add(GTK_CONTAINER (vbox), scrwin);
	
	ilist = gtk_icon_view_new();
	gtk_container_add(GTK_CONTAINER (vbox), scrwin1);
	
	gtk_box_set_homogeneous ( GTK_BOX (vbox), FALSE);
	gtk_box_set_child_packing ( GTK_BOX (vbox), align, FALSE, FALSE, 0, GTK_PACK_END);
	canvas = gtk_image_new();	
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW (scrwin),canvas);
//	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW (scrwin1),ilist);
	gtk_container_add (GTK_CONTAINER (scrwin1), ilist);
	
// drag-n-drop initializing
	GtkTargetEntry target[] = {{ "text/uri-list", 0, 1}};
	gtk_drag_dest_set (mainwin, GTK_DEST_DEFAULT_ALL, target, 1, GDK_ACTION_COPY | GDK_ACTION_MOVE);
// end of initializing

	list_store = gtk_list_store_new (1, GDK_TYPE_PIXBUF); //for ilist
	
	search_for_commandline_options (argc-1,argv+1);

	if (Windowed == FALSE) sock_japw (argc-1,argv+1);
		else prepare_for_idle_create_listf (argc-1,argv+1);

 	if (WinFullscrened) gtk_window_fullscreen(GTK_WINDOW (mainwin));

     
	get_conf_file_path();
	get_psettings();
	 
	g_signal_connect(mainwin, "destroy", G_CALLBACK (on_quit), NULL);
	g_signal_connect(scrwin, "key-release-event", G_CALLBACK (keypress), NULL);
	g_signal_connect(mainwin, "size-allocate", G_CALLBACK (on_window_resize), NULL);
	g_signal_connect(mainwin, "drag-data-received", G_CALLBACK (on_data_draged), NULL);
	g_signal_connect(ilist, "item-activated", G_CALLBACK (on_ilist_activated), NULL);
	g_signal_connect(scrwin, "button-press-event", G_CALLBACK (on_mouse_button_press), NULL);
	g_signal_connect(scrwin, "scroll-event", G_CALLBACK (on_mouse_scroll), NULL);
	g_signal_connect(scrwin1, "key-release-event", G_CALLBACK (keypress1), NULL);

	gtk_icon_view_set_model (GTK_ICON_VIEW (ilist), GTK_TREE_MODEL (list_store));
	gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (ilist), 0);
	gtk_icon_view_set_selection_mode (GTK_ICON_VIEW (ilist), GTK_SELECTION_BROWSE);


	gtk_widget_show_all(mainwin);
	gtk_widget_hide(scrwin1);
//	gtk_widget_hide(progress);

	gtk_widget_grab_focus (scrwin);

	gtk_main();
	return 0;
}
