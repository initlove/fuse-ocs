/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall `pkg-config fuse --cflags --libs` ocs.c -o ocs
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <rest/rest-proxy.h>
#include <rest/rest-proxy-call.h>
#include <rest/rest-xml-parser.h>
#include <json-glib/json-glib.h>


gchar *
get_payload (gchar *function, const gchar *method, ...)
{
    va_list params;

    va_start (params, method);

    RestProxy *proxy = NULL;
    RestProxyCall *call = NULL;
    GError *error = NULL;
    gchar *server = "http://localhost:3000";   

    proxy = rest_proxy_new (server, FALSE);
    call = rest_proxy_new_call (proxy);
    rest_proxy_call_set_function (call, function);
    rest_proxy_call_set_method (call, method);
    rest_proxy_call_add_params_from_valist (call, params);
    rest_proxy_call_add_param (call, "format", "json");

    if (!rest_proxy_call_sync (call, &error)) {
        g_error_free (error);
        goto out;
    }

    gchar *payload = g_strdup (rest_proxy_call_get_payload (call));

out:
    va_end (params);
    g_object_unref (proxy);
    g_object_unref (call);

    return payload;
}

//TODO: better api?
gchar *
get_string_value (gchar *data, gchar *key)
{
    JsonParser *parser = json_parser_new ();
    JsonReader *reader = json_reader_new (NULL);
    GError *error = NULL;
    gchar *val = NULL;

    json_parser_load_from_data (parser, data, -1, &error);
    if (error) {
        g_error_free (error);
        goto out;
    }

    json_reader_set_root (reader, json_parser_get_root (parser));
    if (!json_reader_is_object (reader)) {
        goto out;
    }

    json_reader_read_member (reader, key);
    val = g_strdup (json_reader_get_string_value (reader));
    json_reader_end_member (reader);
out:
    g_object_unref (parser);
    g_object_unref (reader);

    return val;
}

gchar *
get_int_value (gchar *data, gchar *key)
{
    JsonParser *parser = json_parser_new ();
    JsonReader *reader = json_reader_new (NULL);
    GError *error = NULL;
    gint *val = 0;

    json_parser_load_from_data (parser, data, -1, &error);
    if (error) {
        g_error_free (error);
        goto out;
    }

    json_reader_set_root (reader, json_parser_get_root (parser));
    if (!json_reader_is_object (reader)) {
        goto out;
    }

    json_reader_read_member (reader, key);
    val = json_reader_get_int_value (reader);
    json_reader_end_member (reader);
out:
    g_object_unref (parser);
    g_object_unref (reader);

    return val;
}

GList *
get_file_list (gchar *data)
{
    JsonParser *parser = json_parser_new ();
    JsonReader *reader = json_reader_new (NULL);
    GError *error = NULL;
    GList *val = NULL;
    gint count, i;
    const gchar *name;

    json_parser_load_from_data (parser, data, -1, &error);
    if (error) {
        g_error_free (error);
        goto out;
    }

    json_reader_set_root (reader, json_parser_get_root (parser));
    if (!json_reader_is_object (reader)) {
        goto out;
    }

    json_reader_read_member (reader, "files");
    count = json_reader_count_elements (reader);

    for (i = 0; i < count; i++) {
        json_reader_read_element (reader, i);
        name = json_reader_get_string_value (reader);
        val = g_list_prepend (val, g_strdup (name));
        json_reader_end_member (reader);
    }

    json_reader_end_member (reader);    //files
out:
    g_object_unref (parser);
    g_object_unref (reader);

    return val;
}

static int 
ocs_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
    gchar *payload = get_payload ("s3/info", "GET", "url", path, NULL);
    gchar *type = get_string_value (payload, "type");
    if (type) {
	    memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_size = get_int_value (payload, "size");
        if (strcmp (type, "dir") == 0) {
		    stbuf->st_mode = S_IFDIR | 0755;
    		stbuf->st_nlink = 2;
        } else if (strcmp (type, "file") == 0) {
		    stbuf->st_mode = S_IFREG | 0444;
    		stbuf->st_nlink = 1;
        } else
            res = -ENOENT;
        g_free (type);
    } else {
	    res = -ENOENT;
    }
    if (payload)
        g_free (payload);

    return res;
}

static int ocs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
    gchar *payload;
    GList *l, *files;
    gchar *name;

    payload = get_payload ("s3/file", "GET", "url", path, NULL);
    files = get_file_list (payload);

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
    for (l = files; l; l=l->next) {
        name = l->data;
	    filler(buf, name, NULL, 0);
        g_free (name);
    }
    g_list_free (files);

	return 0;
}

static int ocs_mkdir(const char *path, mode_t mode)
{
    gchar *payload = get_payload ("s3/dir", "POST", "url", path, NULL);
    gchar *status = get_string_value (payload, "status");

    if (payload)
        g_free (payload);
    if (status) {
        if (strcmp (status, "ok") == 0)
            return 0;
    }
        
    return -1;
}

static int ocs_rmdir(const char *path)
{
    /*I want to use DELETE, but this file server did not use file url in the body/params*/
    gchar *payload = get_payload ("s3/rmdir", "POST", "url", path, NULL);
    gchar *status = get_string_value (payload, "status");

    if (payload)
        g_free (payload);
    if (status) {
        if (strcmp (status, "ok") == 0)
            return 0;
    }
        
    return -1;
}

static int ocs_unlink (const char *path)
{
    /*I want to use DELETE, but this file server did not use file url in the body/params*/
    gchar *payload = get_payload ("s3/rmfile", "POST", "url", path, NULL);
    gchar *status = get_string_value (payload, "status");

    if (payload)
        g_free (payload);
    if (status) {
        if (strcmp (status, "ok") == 0)
            return 0;
    }
        
    return -1;
}

static int ocs_rename (const char *from, const char *to)
{
    /*I want to use DELETE, but this file server did not use file url in the body/params*/
    gchar *payload = get_payload ("s3/rename", "POST", "from", from, "to", to, NULL);
    gchar *status = get_string_value (payload, "status");

    if (payload)
        g_free (payload);
    if (status) {
        if (strcmp (status, "ok") == 0)
            return 0;
    }
        
    return -1;
}

void log_path (char *call, char *path)
{
    char cmd[1024];
    snprintf (cmd, 1024, "echo '%s %s' | tee -a /tmp/dl_ocs", call, path);
    system (cmd);
}

static int ocs_open(const char *path, struct fuse_file_info *fi)
{
    log_path ("ocs_open", path);
	return 0;
}

static int ocs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	size_t len;
	(void) fi;
    log_path ("ocs_read", path);
    char cmd[1024];
    snprintf (cmd, 1024, "echo '%d %d' | tee -a /tmp/dl_ocs", size, offset);
    system (cmd);
//TODO: this is so important ...
    static const char *ocs_str = "Hello World!\n";
	len = strlen(ocs_str);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, ocs_str + offset, size);
	} else
		size = 0;

	return size;
}


static int ocs_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    int fd;
    int res;
    char cmd[1024];
    snprintf (cmd, 1024, "echo 'ocs write %s' | tee -a /tmp/dl_ocs", path);
    system (cmd);

    return 0;
}

static struct fuse_operations ocs_oper = {
	.getattr	= ocs_getattr,
	.readdir	= ocs_readdir,
    .rename     = ocs_rename,
    .unlink     = ocs_unlink,
    .mkdir      = ocs_mkdir,
    .rmdir      = ocs_rmdir,
	.open		= ocs_open,
	.read		= ocs_read,
    .write      = ocs_write,
};

int main(int argc, char *argv[])
{
    g_type_init ();
	return fuse_main(argc, argv, &ocs_oper, NULL);
}
