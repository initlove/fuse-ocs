/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2012 David Liang <dliang@suse.com>

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
 
#define LOG_PATH	log_path (__FUNCTION__, path)
void log_path (const char *call, const char *path)
{
    char cmd[1024];
    snprintf (cmd, 1024, "echo '%s %s' | tee -a /tmp/dl_ocs", call, path);
    system (cmd);
}


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

    //TODO: FIXME: get payload : cannot check the binary file 
    gchar *payload = g_strdup (rest_proxy_call_get_payload (call));
out:
    va_end (params);
    g_object_unref (proxy);
    g_object_unref (call);

    return payload;
}

//TODO: better api?
//FIXME: if not key value, popup an error
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

gint
get_int_value (gchar *data, gchar *key)
{
    JsonParser *parser = json_parser_new ();
    JsonReader *reader = json_reader_new (NULL);
    GError *error = NULL;
    gint val = 0;

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

gint
get_errno (gchar *payload)
{
	gchar *status;
	gint res;

    status = get_string_value (payload, "status");
    if (status) {
		if (strcmp (status, "ok") == 0) {
		    res = 0;
		} else {
		    res = get_int_value (payload, "errno");
		}
		g_free (status);
    } else {
		res = -1;
	}

	return res;
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
		    stbuf->st_uid = 1001;
		    stbuf->st_gid = 100;
    		    stbuf->st_nlink = 2;
	} else if (strcmp (type, "file") == 0) {
		    stbuf->st_mode = S_IFREG | 0666;
    		    stbuf->st_nlink = 1;
		    stbuf->st_uid = 1001;
		    stbuf->st_gid = 100;
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


static int ocs_access(const char *path, int mask)
{
	log_path (__FUNCTION__, path);
    int res;
    //NOT done in server
	return 0;
}

static int ocs_readlink(const char *path, char *buf, size_t size)
{
	log_path (__FUNCTION__, path);
	//NOT done in server
	return 0;
}

static int ocs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	log_path (__FUNCTION__, path);
	gint res;
	gchar *payload;
   
	payload	= get_payload ("s3/nod", "POST", "url", path, NULL);
	res = get_errno (payload);
	g_free (payload);

	return res;
}

static int ocs_symlink(const char *from, const char *to)
{
	log_path (__FUNCTION__, from);
	log_path (__FUNCTION__, to);
	gint res;
	gchar *payload;

    payload = get_payload ("s3/symlink", "POST", "from", from, "to", to, NULL);
	res = get_errno (payload);
	g_free (payload);

	return res;
}

static int ocs_link(const char *from, const char *to)
{
	log_path (__FUNCTION__, from);
	log_path (__FUNCTION__, to);
	gint res;
	gchar *payload;

    payload = get_payload ("s3/link", "POST", "from", from, "to", to, NULL);
	res = get_errno (payload);
	g_free (payload);

	return res;
}

static int ocs_chmod(const char *path, mode_t mode)
{
	log_path (__FUNCTION__, path);

	gint res;
	gchar *payload;
	gchar *c_mode;

	c_mode = g_strdup_printf ("%d", mode);
    payload = get_payload ("s3/chmod", "POST", "url", path,	"mode", c_mode,	NULL);
	res = get_errno (payload);
	g_free (c_mode);
	g_free (payload);

	return res;
}

static int ocs_chown(const char *path, uid_t uid, gid_t gid)
{
	log_path (__FUNCTION__, path);

	gint res;
	gchar *payload;
	gchar *c_uid;
	gchar *c_gid;

	c_uid = g_strdup_printf ("%d", uid);
	c_gid = g_strdup_printf ("%d", gid);
   	payload = get_payload ("s3/chown", "POST", "url", path, "uid", c_uid, "gid", c_gid,	NULL);
	res = get_errno (payload);
	g_free (c_uid);
	g_free (c_gid);
	g_free (payload);

 	return res;
}

static int ocs_truncate(const char *path, off_t size)
{
	log_path (__FUNCTION__, path);
	gint res;
	gchar *payload;
	gchar *c_size;

	c_size = g_strdup_printf ("%d", size);
    payload = get_payload ("s3/truncate", "POST", "url", path, "size", c_size, NULL);
	res = get_errno (payload);
	g_free (c_size);
	g_free (payload);

	return res;
}

static int ocs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	log_path (__FUNCTION__, path);
	gint res;
    gchar *payload;
    GList *l, *files;
    gchar *name;

    payload = get_payload ("s3/file", "GET", "url", path, NULL);
	res = get_errno (payload);
	if (res == 0) {
	    files = get_file_list (payload);
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
    	for (l = files; l; l=l->next) {
			name = l->data;
	    	filler(buf, name, NULL, 0);
			g_free (name);
    	}
	    g_list_free (files);
	}
	g_free (payload);

	return res;
}

static int ocs_mkdir(const char *path, mode_t mode)
{
	log_path (__FUNCTION__, path);

	gint res;
    gchar *payload;
   
	payload	= get_payload ("s3/dir", "POST", "url", path, NULL);
	res = get_errno (payload);
	g_free (payload);
	
    return res;
}

static int ocs_rmdir(const char *path)
{
	log_path (__FUNCTION__, path);
    /*I want to use DELETE, but this file server did not use file url in the body/params*/
	gint res;
    gchar *payload;

	payload = get_payload ("s3/rmdir", "POST", "url", path, NULL);
	res = get_errno (payload);
	g_free (payload);
	
    return res;
}

static int ocs_unlink (const char *path)
{
	log_path (__FUNCTION__, path);
    /*I want to use DELETE, but this file server did not use file url in the body/params*/
	gint res;
    gchar *payload;

    payload	= get_payload ("s3/rmfile", "POST", "url", path, NULL);
	res = get_errno (payload);
	g_free (payload);
	
    return res;
}

static int ocs_utimens(const char *path, const struct timespec ts[2])
{
	log_path (__FUNCTION__, path);
	return 0;
}

static int ocs_rename (const char *from, const char *to)
{
    log_path (__FUNCTION__, from);
    log_path (__FUNCTION__, to);
    /*I want to use DELETE, but this file server did not use file url in the body/params*/
	gint res;
    gchar *payload;

    payload	= get_payload ("s3/rename", "POST", "from", from, "to", to, NULL);
    res = get_errno (payload);
	g_free (payload);

    return res;
}

static int ocs_open(const char *path, struct fuse_file_info *fi)
{
    log_path ("ocs_open", path);
    log_path (__FUNCTION__, path);

	gint res;
    gchar *c_flags;
	gchar *payload;

    c_flags = g_strdup_printf ("%d", fi->flags);
    payload = get_payload ("s3/open", "POST", "url", path, "flags", c_flags, NULL);
	res = get_errno (payload);
    g_free (c_flags);
	g_free (payload);

    return res;
}

static int ocs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
    log_path ("ocs_read", path);
//TODO: this is so important ... And need to make a cache 
    size_t len;
    gchar *payload;

    payload = get_payload ("s3/file", "GET", "url", path, NULL);
    len = strlen (payload);
    if (offset < len) {
		if (offset + size > len)
	    	size = len - offset;
		memcpy(buf, payload + offset, size);
    } else
		size = 0;

    return size;
}


static int ocs_write(const char *path, const char *buf, size_t size,
	             off_t offset, struct fuse_file_info *fi)
{
    log_path ("ocs_write", path);
    gint res;
    gchar *c_size, *c_offset;
    gchar *data;
    gchar *payload;

    data = g_base64_encode (buf, size);
    c_size = g_strdup_printf ("%d", size);
    c_offset = g_strdup_printf ("%d", offset),
//    log_path ("write data", data);
//    log_path (c_size, c_offset);
    payload = get_payload ("s3/data", "POST", 
					"url", path, "data", data,
					"size", c_size,
					"offset", c_offset,
					NULL);
	res = get_errno (payload);
    g_free (data);
    g_free (c_size);
    g_free (c_offset);
	g_free (payload);

    return res;
}


static int ocs_statfs(const char *path, struct statvfs *stbuf)
{
	log_path (__FUNCTION__, path);
	return 0;
}

static int ocs_release(const char *path, struct fuse_file_info *fi)
{
//TODO: should merge all the write operation to one release ..        
	log_path (__FUNCTION__, path);
	return 0;
}

static int ocs_fsync(const char *path, int isdatasync,
                     struct fuse_file_info *fi)
{
	log_path (__FUNCTION__, path);
	return 0;
}

static struct fuse_operations ocs_oper = {
	.getattr        = ocs_getattr,
	.access         = ocs_access,
	.readlink       = ocs_readlink,
	.readdir        = ocs_readdir,
	.mknod          = ocs_mknod,
	.mkdir          = ocs_mkdir,
	.symlink        = ocs_symlink,
	.unlink         = ocs_unlink,
	.rmdir          = ocs_rmdir,
	.rename         = ocs_rename,
	.link           = ocs_link,
	.chmod          = ocs_chmod,
	.chown          = ocs_chown,
	.truncate       = ocs_truncate,
	.utimens        = ocs_utimens,
	.open           = ocs_open,
	.read           = ocs_read,
	.write          = ocs_write,
	.statfs         = ocs_statfs,
	.release        = ocs_release,
	.fsync          = ocs_fsync,
};

int main(int argc, char *argv[])
{
    g_type_init ();
	return fuse_main(argc, argv, &ocs_oper, NULL);
}
