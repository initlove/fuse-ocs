/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall `pkg-config fuse --cflags --libs` hello.c -o hello
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <rest/rest-proxy.h>
#include <rest/rest-proxy-call.h>
#include <rest/rest-xml-parser.h>
#include <json-glib/json-glib.h>

char buf [1024];
static const char *hello_str = "Hello World!\n";
static const char *hello_path = "/hello";


typedef struct  _simple_file {
    gchar *name;
    gchar *type;
} simple_file;

GList *
get_file_list (gchar *data)
{
    JsonParser *parser = json_parser_new ();
    JsonReader *reader = json_reader_new (NULL);
    GError *error = NULL;
    GList *val = NULL;
    gint count, i;
    simple_file *sf;

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
        sf = malloc(sizeof (simple_file));

        json_reader_read_member (reader, "name");
        sf->name = g_strdup (json_reader_get_string_value (reader));
        json_reader_end_member (reader);

        json_reader_read_member (reader, "type");
        sf->type = g_strdup (json_reader_get_string_value (reader));
        json_reader_end_member (reader);
        val = g_list_prepend (val, sf);

        json_reader_end_member (reader);
    }

    json_reader_end_member (reader);    //files
out:
    g_object_unref (parser);
    g_object_unref (reader);

    return val;
}

gchar *
get_value (gchar *data, gchar *key)
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

static int hello_getattr(const char *path, struct stat *stbuf)
{
    RestProxy *proxy = NULL;
    RestProxyCall *call = NULL;
    GError *error = NULL;
    gchar *url = "http://localhost:3000";   

	int res = 0;
    proxy = rest_proxy_new (url, FALSE);
    call = rest_proxy_new_call (proxy);
    rest_proxy_call_set_function (call, "s3/info");
    rest_proxy_call_add_params (call, 
                        "format", "json",
                        "url", path,
                        NULL);

    if (!rest_proxy_call_sync (call, &error)) {
        snprintf (buf, 1024, "echo '%s' | tee -a /tmp/dl_fuse", error->message);
        system (buf);
        g_error_free (error);
		res = -ENOENT;
        return res;
    } else {
        snprintf (buf, 1024, "echo '%s' | tee -a /tmp/dl_fuse", rest_proxy_call_get_payload (call));
        system (buf);
    }

	return	res = -ENOENT;
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (strcmp(path, hello_path) == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(hello_str);
	} else if (strcmp(path, "/dliang") == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
	} else
		res = -ENOENT;

	return res;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, hello_path + 1, NULL, 0);
	filler(buf, "dliang", NULL, 0);

	return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi)
{
char buf [1024];
snprintf (buf, 1024, "echo 'path %s' | tee -a /tmp/dl_hello", path);
system (buf);
	if (strcmp(path, hello_path) != 0)
		return -ENOENT;

	if ((fi->flags & 3) != O_RDONLY)
		return -EACCES;

	return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	size_t len;
	(void) fi;
printf ("%s %s\n", path, buf);
	if(strcmp(path, hello_path) != 0)
		return -ENOENT;

	len = strlen(hello_str);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, hello_str + offset, size);
	} else
		size = 0;

	return size;
}

int main(int argc, char *argv[])
{
    g_type_init ();
    gchar *val;
    gchar *data = "{'status':'ok','type':'dir'}";
    val = get_value (data, "type");
    printf ("val is %s\n", val);

    GList *list, *l;
    simple_file *sf;

    gchar *file_data = "{'status':'ok','files':[{'name':'dl_gdm','type':'file'},{'name':'b','type':'dir'},{'name':'a_file','type':'file'}]}";
    list = get_file_list (file_data);

    for (l = list; l; l = l->next) {
        sf = l->data;
        printf ("sf %s %s\n", sf->name, sf->type);
    }
    g_free (val);
	return 0;
}
